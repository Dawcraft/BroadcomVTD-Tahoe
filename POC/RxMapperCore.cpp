#include "RxMapperCore.hpp"
#include "Trace.hpp"
#ifndef BVT_HOST_TEST
#include <mach/task.h>
#endif

namespace bvp { namespace rx {
static Mapping mappings[Capacity];
static uint64_t serialCounter;
static uint32_t active,stop;
static uint64_t preparedCount,mappedCount,completedCount,quarantineCount;
static uint64_t resetCounter,resetCompletedCount;
void enable(bool value) { __atomic_store_n(&active,value,__ATOMIC_RELEASE); }
bool enabled() { return __atomic_load_n(&active,__ATOMIC_ACQUIRE); }
bool halted() { return __atomic_load_n(&stop,__ATOMIC_ACQUIRE); }
static void detail(uint32_t type,const Mapping &m,uint32_t reason=0) {
    auto e=event(type,reinterpret_cast<void *>(m.queue),reinterpret_cast<void *>(m.packet),m.descriptor,reason);
    e.payload[0]=m.serial; e.payload[1]=uint32_t(stateOf(m)); e.payload[2]=m.owner;
    e.payload[3]=m.range.address; e.payload[4]=m.range.length; e.payload[5]=m.address;
    e.payload[6]=m.mapRecord; e.payload[7]=m.startIndex; e.payload[8]=poisoned(m); emit(e);
}
void halt(uint32_t reason) {
    uint32_t zero=0;
    if (__atomic_compare_exchange_n(&stop,&zero,reason?reason:1,false,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) {
        auto e=event(RxMapperHalted,nullptr,nullptr,nullptr,reason); emit(e);
    }
}
Mapping *find(uint64_t packet) {
    if (!enabled() || !packet) return nullptr;
    for (auto &m:mappings) if (__atomic_load_n(&m.packet,__ATOMIC_ACQUIRE)==packet) return &m;
    return nullptr;
}
Mapping *reserve(uint64_t owner,uint64_t queue,uint64_t packet) {
    if (!enabled() || halted() || !owner || !queue || !packet || find(packet)) return nullptr;
    for (auto &m:mappings) if (transition(m,MapState::Empty,MapState::Preparing)) {
        m.owner=owner; m.queue=queue; m.serial=__atomic_add_fetch(&serialCounter,1,__ATOMIC_RELAXED);
        m.descriptor=nullptr; m.range={0,0}; m.address=m.mapRecord=m.startIndex=0;
        m.prepared=m.consumed=m.reported=m.poison=0;
        m.resetEpoch=m.resetThread=0; m.resetAcknowledged=0;
        __atomic_store_n(&m.packet,packet,__ATOMIC_RELEASE); return &m;
    }
    halt(1); return nullptr; // No eviction; TX table/capacity is independent.
}
void quarantine(Mapping &m,uint32_t reason) {
    // New uncertainty revokes a reset certificate. beginReset installs a fresh
    // epoch only after this conservative transition, never by unpoisoning.
    __atomic_store_n(&m.resetAcknowledged,0,__ATOMIC_RELEASE);
    bvp::quarantine(m);
    if (!__atomic_exchange_n(&m.reported,1,__ATOMIC_ACQ_REL)) {
        __atomic_add_fetch(&quarantineCount,1,__ATOMIC_RELAXED); detail(RxMapperQuarantine,m,reason);
    }
}
void quarantineQueue(uint64_t queue,uint32_t reason) {
    if (!enabled()) return;
    for (auto &m:mappings) if (__atomic_load_n(&m.packet,__ATOMIC_ACQUIRE) && m.queue==queue) quarantine(m,reason);
}
void quarantineOwner(uint64_t owner,uint32_t reason) {
    if (!enabled()) return;
    for (auto &m:mappings) if (__atomic_load_n(&m.packet,__ATOMIC_ACQUIRE) && (!owner || m.owner==owner)) quarantine(m,reason);
}
void invalidateQueue(uint64_t queue) {
    if (!enabled()) return;
    for (auto &m:mappings) if (__atomic_load_n(&m.packet,__ATOMIC_ACQUIRE) && m.queue==queue) {
        __atomic_store_n(&m.resetAcknowledged,0,__ATOMIC_RELEASE);
        __atomic_store_n(&m.resetEpoch,0,__ATOMIC_RELEASE);
    }
}
uint64_t beginReset(uint64_t queue,uint64_t thread) {
    if (!enabled() || !queue || !thread) return 0;
    const auto epoch=__atomic_add_fetch(&resetCounter,1,__ATOMIC_RELAXED);
    invalidateQueue(queue);
    for (auto &m:mappings) if (__atomic_load_n(&m.packet,__ATOMIC_ACQUIRE) && m.queue==queue) {
        quarantine(m,15);
        // Already-withheld mappings may receive evidence but cannot be freed
        // without the independent exact software-ring removal check below.
        if (m.consumed && m.prepared && stateOf(m)==MapState::Quarantine) {
            m.resetThread=thread; __atomic_store_n(&m.resetEpoch,epoch,__ATOMIC_RELEASE);
        }
    }
    return epoch;
}
unsigned acknowledgeReset(uint64_t queue,uint64_t thread,uint64_t epoch,bool disabled) {
    unsigned count=0;
    if (!enabled() || halted() || !epoch || !disabled) return 0;
    for (auto &m:mappings) if (__atomic_load_n(&m.packet,__ATOMIC_ACQUIRE) && m.queue==queue &&
        __atomic_load_n(&m.resetEpoch,__ATOMIC_ACQUIRE)==epoch && m.resetThread==thread &&
        stateOf(m)==MapState::Quarantine && m.consumed && m.prepared) {
        __atomic_store_n(&m.resetAcknowledged,1,__ATOMIC_RELEASE); ++count;
    }
    return count;
}
static bool releasePrepared(Mapping &m) {
    if (m.prepared && m.descriptor->complete(kIODirectionInOut)!=kIOReturnSuccess) return false;
    m.prepared=0;
    if (m.descriptor) { m.descriptor->release(); m.descriptor=nullptr; }
    __atomic_store_n(&m.packet,0,__ATOMIC_RELEASE);
    __atomic_store_n(&m.state,uint32_t(MapState::Empty),__ATOMIC_RELEASE); return true;
}
bool abortUnpublished(Mapping &m) {
    if (m.consumed || stateOf(m)==MapState::Quarantine || poisoned(m)) return false;
    if (releasePrepared(m)) return true;
    quarantine(m,9); halt(9); return false;
}
bool completeNormally(Mapping &m) {
    if (!canCompleteNormally(m,true) || !transition(m,MapState::Owned,MapState::Completing)) return false;
    if (poisoned(m)) { __atomic_store_n(&m.state,uint32_t(MapState::Quarantine),__ATOMIC_RELEASE); return false; }
    // Log after successful complete. Do not dereference a returned packet here.
    auto e=event(RxMapperCompleted,reinterpret_cast<void *>(m.queue),reinterpret_cast<void *>(m.packet),m.descriptor);
    e.payload[0]=m.serial; e.payload[1]=m.startIndex; e.payload[2]=m.address; e.payload[3]=m.range.length;
    if (!releasePrepared(m)) { __atomic_store_n(&m.state,uint32_t(MapState::Quarantine),__ATOMIC_RELEASE); quarantine(m,9); halt(9); return false; }
    __atomic_add_fetch(&completedCount,1,__ATOMIC_RELAXED);
    e.timeNS=event(RxMapperCompleted).timeNS; emit(e); return true;
}
bool completeResetReclaim(Mapping &m,uint64_t thread,bool softwareDetached) {
    // Frontend holds the queue's non-waiting operation lease through this call:
    // no admitted init/enable/fill can race it. The packet must have JUST been
    // removed by the original forced reclaim, not merely found in quarantine.
    if (!softwareDetached || halted() || !thread || m.resetThread!=thread || !m.resetEpoch ||
        !m.consumed || !m.prepared || !__atomic_load_n(&m.resetAcknowledged,__ATOMIC_ACQUIRE) ||
        !transition(m,MapState::Quarantine,MapState::Completing)) return false;
    if (!__atomic_exchange_n(&m.resetAcknowledged,0,__ATOMIC_ACQ_REL) || halted()) {
        __atomic_store_n(&m.state,uint32_t(MapState::Quarantine),__ATOMIC_RELEASE); return false;
    }
    auto e=event(RxResetCompleted,reinterpret_cast<void *>(m.queue),reinterpret_cast<void *>(m.packet),m.descriptor);
    e.payload[0]=m.serial; e.payload[1]=m.resetEpoch; e.payload[2]=m.startIndex;
    e.payload[3]=m.address; e.payload[4]=m.range.length; e.payload[5]=m.resetThread;
    if (!releasePrepared(m)) {
        __atomic_store_n(&m.state,uint32_t(MapState::Quarantine),__ATOMIC_RELEASE);
        quarantine(m,9); halt(9); return false;
    }
    __atomic_add_fetch(&resetCompletedCount,1,__ATOMIC_RELAXED);
    e.timeNS=event(RxResetCompleted).timeNS; emit(e); return true;
}
bool publish(Mapping &m,uint64_t mapRecord,uint32_t startIndex) {
    if (m.consumed || poisoned(m) || !transition(m,MapState::Prepared,MapState::Owned)) return false;
    m.mapRecord=mapRecord; m.startIndex=startIndex;
    __atomic_store_n(&m.consumed,1,__ATOMIC_RELEASE);
    __atomic_add_fetch(&mappedCount,1,__ATOMIC_RELAXED); detail(RxMapperMapped,m); return true;
}
bool prepare(Mapping &m,IOMapper *mapper,uint64_t virtualAddress,uint32_t length) {
    if (!enabled() || !mapper || stateOf(m)!=MapState::Preparing || !virtualAddress ||
        !length || length>MaxPacketBytes || virtualAddress+length<virtualAddress) return false;
    m.range={virtualAddress,length};
    // Device-visible contiguous range, NOT an assumption of physical contiguity.
    // InOut also permits the original driver's CPU RX-header initialization.
    m.descriptor=IOMemoryDescriptor::withOptions(&m.range,1,0,kernel_task,
        kIOMemoryTypeVirtual|kIODirectionInOut|kIOMemoryAsReference,mapper);
    if (!m.descriptor || m.descriptor->prepare(kIODirectionInOut|kIODirectionPrepareNoFault)!=kIOReturnSuccess) return false;
    m.prepared=1;
    uint64_t cursor=0; unsigned segments=0;
    while (cursor<length) {
        if (++segments>MaxRanges+1) return false;
        IOByteCount coverage=0;
        uint64_t address=m.descriptor->getPhysicalSegment(cursor,&coverage,0);
        if (!address || !coverage) return false;
        if (!cursor) m.address=address;
        else if (m.address+cursor<m.address || address!=m.address+cursor) return false;
        uint64_t step=coverage<length-cursor?coverage:length-cursor;
        if (address+step<address) return false;
        cursor+=step;
    }
    if (poisoned(m)) { quarantine(m,10); return false; }
    if (!transition(m,MapState::Preparing,MapState::Prepared)) return false;
    __atomic_add_fetch(&preparedCount,1,__ATOMIC_RELAXED); detail(RxMapperPrepared,m); return true;
}
void snapshot(unsigned index,Event &e) {
    e=event(RxMapperInventory); e.flags=index;
    if (index==Capacity) {
        e.flags=0xffffffffU; e.payload[0]=enabled(); e.payload[1]=halted(); e.payload[2]=Capacity;
        e.payload[3]=__atomic_load_n(&preparedCount,__ATOMIC_RELAXED);
        e.payload[4]=__atomic_load_n(&mappedCount,__ATOMIC_RELAXED);
        e.payload[5]=__atomic_load_n(&completedCount,__ATOMIC_RELAXED);
        e.payload[6]=__atomic_load_n(&quarantineCount,__ATOMIC_RELAXED);
        e.payload[7]=__atomic_load_n(&resetCompletedCount,__ATOMIC_RELAXED);
        e.payload[8]=__atomic_load_n(&resetCounter,__ATOMIC_RELAXED); return;
    }
    if (index>=Capacity || !enabled()) return;
    const auto &m=mappings[index];
    auto packet=__atomic_load_n(&m.packet,__ATOMIC_ACQUIRE),serial=__atomic_load_n(&m.serial,__ATOMIC_RELAXED);
    if (!packet) return;
    e.object=__atomic_load_n(&m.queue,__ATOMIC_RELAXED); e.packet=packet;
    e.auxiliary=reinterpret_cast<uint64_t>(__atomic_load_n(&m.descriptor,__ATOMIC_RELAXED));
    e.payload[0]=serial; e.payload[1]=uint32_t(stateOf(m)); e.payload[2]=m.owner;
    e.payload[3]=m.range.address; e.payload[4]=m.range.length; e.payload[5]=m.address;
    e.payload[6]=m.mapRecord; e.payload[7]=m.startIndex; e.payload[8]=poisoned(m);
    e.payload[9]=m.consumed; e.payload[10]=halted();
    e.payload[11]=m.resetEpoch; e.payload[12]=m.resetThread;
    e.payload[13]=__atomic_load_n(&m.resetAcknowledged,__ATOMIC_ACQUIRE);
    if (packet!=__atomic_load_n(&m.packet,__ATOMIC_ACQUIRE) || serial!=__atomic_load_n(&m.serial,__ATOMIC_RELAXED)) e.flags|=0x80000000U;
}
} }
