#include "TxDisposition.hpp"
#include "Trace.hpp"
#ifndef BVT_HOST_TEST
#include <libkern/libkern.h>
#endif

namespace bvp { namespace dispo {
static ReadTag tagReader;
static uint8_t precedences[8];
static uint64_t observations;
constexpr uint64_t Budget=128;
template<typename T> static T read(uint64_t address,uint64_t offset) {
    T v;memcpy(&v,reinterpret_cast<const void *>(address+offset),sizeof(v));return v;
}
void configure(ReadTag reader,const uint8_t *map) {
    for (unsigned i=0;i<8;++i) precedences[i]=map[i];
    tagReader=reader; // Initialization before routes/armed publication only.
}
Input inspect(uint64_t wlc,uint32_t mode,const Tag &tag,bool valid) {
    Input s;s.wlc=wlc;s.mode=mode&255;s.tag=tag;
    if (!wlc) return s;
    if (valid) s.coverage|=TagValid;
    const auto band=read<uint64_t>(wlc,0x40),pub=read<uint64_t>(wlc,0);
    if (!band || !pub) return s;
    s.coverage|=MainBandValid;s.primary=read<uint64_t>(band,0x20);
    const auto bands=read<uint32_t>(pub,0x34);
    uint64_t other=0,bssTable=0;
    if (valid) {
        // Exact recover_pkt_scb early return compares identity, not guessed
        // cached-SCB liveness. Never dereference the packet's cached pointer.
        if (tag.cachedScb==s.primary) s.recovery=s.primary?PrimarySpecial:NullScb;
        else {
            if (bands>=2) {
                auto index=read<uint32_t>(band,4)!=1?1U:0U;
                other=read<uint64_t>(wlc,0x50+index*8);
                if (!other) return s;
                s.alternate=read<uint64_t>(other,0x20);
            }
            s.coverage|=OtherBandChecked;
            if (bands>=2 && tag.cachedScb==s.alternate) s.recovery=s.alternate?AlternateSpecial:NullScb;
            else {
                s.recovery=NativeRecoveryRequired;
                // Native BSS table has 16 slots (pinned traversal evidence).
                // An out-of-domain tag loses observation only, never admission.
                if (tag.bssIndex>=0 && tag.bssIndex<16) {
                    bssTable=read<uint64_t>(wlc,0x338);
                    if (bssTable) {
                        s.bss=read<uint64_t>(bssTable,uint64_t(tag.bssIndex)*8);
                        s.coverage|=BssSlotObserved;
                        if (!s.bss) s.recovery=NullScb;
                    }
                }
            }
        }
    }
    // Only native mode != 2 uses wlc->sync queue + 8. Mode2 obtains another
    // destination after SCB recovery; do not pretend this is that destination.
    if (s.mode!=1 && s.mode!=2) {
        s.queueInfo=read<uint64_t>(wlc,0x778);
        if (valid && tag.priority<8) {
            s.coverage|=PriorityValid;s.precedence=precedences[tag.priority];
            if (s.queueInfo && s.queueInfo+8>s.queueInfo) {
                s.queue=s.queueInfo+8;s.queuePrecisions=read<uint16_t>(s.queue,0);
                if (s.queuePrecisions && s.queuePrecisions<=16 && s.precedence<s.queuePrecisions) {
                    s.count=read<uint16_t>(s.queue,0x18+24*s.precedence);
                    s.limit=read<uint16_t>(s.queue,0x1a+24*s.precedence);s.coverage|=QueueObserved;
                }
            }
        }
    }
    bool stable=read<uint64_t>(wlc,0x40)==band && read<uint64_t>(wlc,0)==pub &&
        read<uint32_t>(pub,0x34)==bands && read<uint64_t>(band,0x20)==s.primary;
    if (other) stable=stable && read<uint64_t>(other,0x20)==s.alternate;
    if (bssTable) stable=stable && read<uint64_t>(wlc,0x338)==bssTable &&
        read<uint64_t>(bssTable,uint64_t(tag.bssIndex)*8)==s.bss;
    if (s.mode!=1 && s.mode!=2) stable=stable && read<uint64_t>(wlc,0x778)==s.queueInfo;
    if (stable) s.coverage|=StableAnchors;
    return s;
}
void observe(uint64_t id,const Mapping &m,uint64_t wlc,uint32_t mode) {
    if (!tagReader || !wlc || !m.packet) return;
    if (__atomic_add_fetch(&observations,1,__ATOMIC_RELAXED)>Budget) return;
    Tag first{},second{};const bool found=tagReader(m.packet,first);
    auto s=inspect(wlc,mode,first,found);
    const bool again=tagReader(m.packet,second);
    if (found!=again || first.cachedScb!=second.cachedScb || first.flags1!=second.flags1 ||
        first.flags7!=second.flags7 || first.bssIndex!=second.bssIndex || first.priority!=second.priority)
        s.coverage&=~StableAnchors;
    auto e=event(TxDisposition,reinterpret_cast<void *>(wlc),reinterpret_cast<void *>(m.packet),reinterpret_cast<void *>(m.owner));
    e.payload[0]=id;e.payload[1]=m.serial;e.payload[2]=s.mode;e.payload[3]=s.coverage;
    e.payload[4]=s.tag.flags1;e.payload[5]=s.tag.flags7;e.payload[6]=uint64_t(int64_t(s.tag.bssIndex));
    e.payload[7]=s.tag.cachedScb;e.payload[8]=s.primary;e.payload[9]=s.alternate;
    e.payload[10]=s.recovery;e.payload[11]=s.bss;e.payload[12]=s.queue;
    e.payload[13]=s.tag.priority;e.payload[14]=s.precedence;
    e.payload[15]=uint64_t(s.count)|(uint64_t(s.limit)<<32);
    e.payload[16]=classify(s);e.payload[17]=s.queuePrecisions;emit(e);
    // This result is NEVER returned to the frontend as an ownership decision.
}
} }
