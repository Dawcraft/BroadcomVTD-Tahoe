#include "Trace.hpp"
#include "Lifetime.hpp"
#ifdef BVT_HOST_TEST
#include <cstring>
#include <chrono>
static void *current_thread() { static thread_local int identity; return &identity; }
static uint64_t mach_absolute_time() { return uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()); }
static void absolutetime_to_nanoseconds(uint64_t t, uint64_t *n) { *n=t; }
static unsigned OSBacktrace(void **bt, unsigned maxAddrs) {
    for (unsigned i=0;i<maxAddrs;++i) bt[i]=reinterpret_cast<void *>(0x10000ULL+i*16);
    return maxAddrs;
}
#else
#include <libkern/libkern.h>
#include <libkern/OSDebug.h>
#include <Headers/kern_util.hpp>
#include <kern/clock.h>
#include <kern/thread.h>
#include <mach/mach_time.h>
#include <sys/errno.h>
#include <sys/kauth.h>
#include <sys/sysctl.h>
#endif

namespace bvp {
void snapshotMapping(unsigned,Event &);
static void (*rxSnapshot)(unsigned,Event &);
void setRxSnapshot(void (*callback)(unsigned,Event &)) { rxSnapshot=callback; }
uint32_t gateStatus;
uint32_t runtimeModeInfo;
uint64_t providerID, controllerID;
struct Slot { uint32_t busy; Event value; };
static Slot ring[Capacity];
static uint64_t nextSequence, busyDrops, tableDrops, stopAfter;
// Only diagnostics change from the object-proven 0.2.5 baseline. Never reset a
// mapper halt, change native return values, read MMIO, or dispose a packet here.
static bool firstFailureEnabled=true;
static uint32_t firstFailureState; // 0 unclaimed, 1 writer, 2 immutable/published
static Event firstFailure;
static uint64_t targetBase, targetSize;
struct Tag { uint32_t busy, kind; uint64_t key, value; };
static Tag tags[8192];
static uint32_t identityUncertain;

static bool take(uint32_t &word) {
    uint32_t expected = 0;
    return __atomic_compare_exchange_n(&word, &expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}
static void give(uint32_t &word) { __atomic_store_n(&word, 0, __ATOMIC_RELEASE); }
void countTableDrop() { __atomic_fetch_add(&tableDrops, 1, __ATOMIC_RELAXED); }

// Four nonblocking probes; no eviction. Failure is a coverage gap, never a guess.
static uint64_t tagOp(const void *ptr, TagKind kind, uint64_t value, int op) {
    uint64_t key = reinterpret_cast<uint64_t>(ptr);
    if (!key) return 0;
    size_t base = ((key >> 4) ^ (key >> 17) ^ kind) & 8191;
    for (size_t n = 0; n < 4; ++n) {
        auto &t = tags[(base+n)&8191];
        if (!take(t.busy)) {
            // A busy earlier bucket could contain this same key. Never insert
            // a duplicate key into a later bucket or leave an uncertain erase.
            countTableDrop();
            __atomic_store_n(&identityUncertain, 1, __ATOMIC_RELEASE);
            __atomic_store_n(&gateStatus, 8, __ATOMIC_RELEASE);
            return 0;
        }
        uint64_t result = 0;
        bool match = t.key == key && t.kind == kind;
        if (match || (op == 1 && !t.key)) {
            if (op == 1) {
                __atomic_store_n(&t.kind, uint32_t(kind), __ATOMIC_RELAXED);
                __atomic_store_n(&t.key, key, __ATOMIC_RELAXED);
                __atomic_store_n(&t.value, value, __ATOMIC_RELEASE);
            }
            result = __atomic_load_n(&t.value, __ATOMIC_RELAXED);
            // Retain the key as a tombstone, avoiding duplicate live keys after
            // deletion in an open-addressed table. Saturation fails closed.
            if (op == 2) __atomic_store_n(&t.value, 0, __ATOMIC_RELEASE);
            give(t.busy);
            return result;
        }
        give(t.busy);
    }
    if (op == 1) countTableDrop();
    if (op == 1) {
        __atomic_store_n(&identityUncertain, 1, __ATOMIC_RELEASE);
        __atomic_store_n(&gateStatus, 8, __ATOMIC_RELEASE);
    }
    return 0;
}
bool tagSet(const void *p, TagKind k, uint64_t v) { return tagOp(p, k, v, 1) == v; }
uint64_t tagGet(const void *p, TagKind k) {
    if (__atomic_load_n(&identityUncertain, __ATOMIC_ACQUIRE)) return 0;
    uint64_t key = reinterpret_cast<uint64_t>(p);
    if (!key) return 0;
    size_t base = ((key >> 4) ^ (key >> 17) ^ k) & 8191;
    // Readers never hold a lock, so an interrupt cannot stall identity cleanup.
    // Keys are immutable once claimed; erased entries are value-zero tombstones.
    for (size_t n=0;n<4;++n) {
        auto &t=tags[(base+n)&8191];
        if (__atomic_load_n(&t.busy,__ATOMIC_ACQUIRE)) { countTableDrop(); continue; }
        auto found=__atomic_load_n(&t.key,__ATOMIC_RELAXED);
        auto kind=__atomic_load_n(&t.kind,__ATOMIC_RELAXED);
        auto value=__atomic_load_n(&t.value,__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&t.busy,__ATOMIC_ACQUIRE)) { countTableDrop(); continue; }
        if (found==key && kind==k) return value;
    }
    return 0;
}
void tagErase(const void *p, TagKind k) { (void)tagOp(p, k, 0, 2); }

Event event(uint32_t type, const void *object, const void *packet, const void *auxiliary, uint32_t flags) {
    Event e {};
    e.type = type; e.flags = flags;
    e.object = reinterpret_cast<uint64_t>(object);
    e.packet = reinterpret_cast<uint64_t>(packet);
    e.auxiliary = reinterpret_cast<uint64_t>(auxiliary);
    e.thread = reinterpret_cast<uint64_t>(current_thread());
    absolutetime_to_nanoseconds(mach_absolute_time(), &e.timeNS);
    return e;
}

static void writeRing(Event &e) {
    uint64_t stop = __atomic_load_n(&stopAfter, __ATOMIC_RELAXED);
    if (stop && __atomic_load_n(&nextSequence, __ATOMIC_RELAXED) >= stop) return;
    uint64_t seq = __atomic_fetch_add(&nextSequence, 1, __ATOMIC_RELAXED);
    if (stop && seq >= stop) return;
    auto &s = ring[seq % Capacity];
    if (!take(s.busy)) { __atomic_fetch_add(&busyDrops, 1, __ATOMIC_RELAXED); return; }
    if (s.value.sequence > seq+1) { give(s.busy); __atomic_fetch_add(&busyDrops, 1, __ATOMIC_RELAXED); return; }
    e.sequence = seq + 1; // zero means never filled
    s.value = e;
    give(s.busy);
}

void freezeSoon() {
    uint64_t expected = 0;
    uint64_t until = __atomic_load_n(&nextSequence, __ATOMIC_RELAXED) + 8192;
    __atomic_compare_exchange_n(&stopAfter, &expected, until, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

// Keep the one-shot stack workspace OUT of ordinary emit() stack frames.
static __attribute__((noinline)) void retainFirstFailure(const Event &trigger) {
    uint32_t expected=0;
    if (!__atomic_compare_exchange_n(&firstFailureState,&expected,1,false,
                                    __ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) return;
    // Stop TRACE overwrites after the existing 8192-event tail. Driver execution,
    // live mapping snapshots and all original halt/quarantine policy continue.
    freezeSoon();
    Event e=trigger;
    e.type=FirstFailure;
    memset(e.payload,0,sizeof(e.payload));
    e.payload[0]=trigger.type;
    e.payload[1]=__atomic_load_n(&targetBase,__ATOMIC_ACQUIRE);
    e.payload[2]=__atomic_load_n(&targetSize,__ATOMIC_ACQUIRE);
    e.payload[3]=__atomic_load_n(&stopAfter,__ATOMIC_RELAXED);
    // Cold, once per boot, after a permanent halt already latched. Fixed stack
    // array; OSBacktrace returns PCs only. No symbol lookup, allocation or log.
    void *frames[13] {};
    unsigned count=OSBacktrace(frames,13);
    if (count>13) count=13;
    e.payload[4]=count;
    for (unsigned i=0;i<count;++i) e.payload[5+i]=reinterpret_cast<uint64_t>(frames[i]);
    firstFailure=e;
    __atomic_store_n(&firstFailureState,2,__ATOMIC_RELEASE);
}

void emit(Event &e) {
    // Reuse existing, already-validated acquisition metadata; no new native hook.
    if (e.type==Acquisition && e.flags==4) {
        __atomic_store_n(&targetBase,e.payload[2],__ATOMIC_RELEASE);
        __atomic_store_n(&targetSize,e.payload[3],__ATOMIC_RELEASE);
    }
    writeRing(e);
    if (firstFailureEnabled && (e.type==MapperHalted || e.type==RxMapperHalted))
        retainFirstFailure(e);
}

static void snapshotFirstFailure(Event &e) {
    e={}; e.type=FirstFailure;
    uint32_t state=__atomic_load_n(&firstFailureState,__ATOMIC_ACQUIRE);
    if (state==2) e=firstFailure; // Immutable: no busy wait or torn stack copy.
    else if (state==1) e.flags=0x80000000U; // Retry retrieval, not a live-driver action.
    else if (!firstFailureEnabled) e.flags=0x40000000U;
}

#ifndef BVT_HOST_TEST
static int readTrace SYSCTL_HANDLER_ARGS {
    (void)oidp; (void)arg1; (void)arg2;
    if (req->newptr) return EPERM;
    // Contains kernel/DMA addresses: root-only, no world-readable address leak.
    if (!kauth_cred_issuser(kauth_cred_get())) return EPERM;
    if (!req->oldptr) return SYSCTL_OUT(req, nullptr, sizeof(Header)+(Capacity+TxSnapshotEntries+RxSnapshotEntries+FirstFailureEntries)*sizeof(Event));
    Header h {};
    h.magic = 0x3154434152545642ULL; // BVTRACT1, little endian
    h.version = FormatVersion; h.eventSize = sizeof(Event); h.capacity = Capacity;
    h.gateStatus = __atomic_load_n(&gateStatus, __ATOMIC_ACQUIRE);
    h.next = __atomic_load_n(&nextSequence, __ATOMIC_RELAXED);
    h.busyDrops = __atomic_load_n(&busyDrops, __ATOMIC_RELAXED);
    h.tableDrops = __atomic_load_n(&tableDrops, __ATOMIC_RELAXED);
    h.stopAfter = __atomic_load_n(&stopAfter, __ATOMIC_RELAXED);
    h.provider = __atomic_load_n(&providerID, __ATOMIC_RELAXED);
    h.controller = __atomic_load_n(&controllerID, __ATOMIC_RELAXED);
    const uint8_t uuid[] = {0xe4,0x67,0x8f,0xeb,0x1d,0xc1,0x35,0xcc,0x93,0x06,0x48,0x02,0x10,0x83,0xd0,0x4a};
    memcpy(h.uuid, uuid, sizeof(uuid));
    int result = SYSCTL_OUT(req, &h, sizeof(h));
    if (result) return result;
    for (size_t i = 0; i < Capacity; ++i) {
        Event e {};
        auto &s = ring[i];
        if (take(s.busy)) { e = s.value; give(s.busy); }
        // Copyout can block; NEVER hold a lock while calling it.
        result = SYSCTL_OUT(req, &e, sizeof(e));
        if (result) return result;
    }
    // Independent of the frozen event ring. No clear/unmap/packet operation.
    for (unsigned i=0;i<TxSnapshotEntries;++i) {
        Event e {}; snapshotMapping(i,e);
        result=SYSCTL_OUT(req,&e,sizeof(e)); if (result) return result;
    }
    for (unsigned i=0;i<RxSnapshotEntries;++i) {
        Event e {}; if (rxSnapshot) rxSnapshot(i,e);
        result=SYSCTL_OUT(req,&e,sizeof(e)); if (result) return result;
    }
    Event first {}; snapshotFirstFailure(first);
    return SYSCTL_OUT(req,&first,sizeof(first));
}
SYSCTL_PROC(_debug, OID_AUTO, broadcomvtd, CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_LOCKED,
            nullptr, 0, readTrace, "S,bvt_trace", "Read-only bounded Broadcom DMA trace (root only)");

void initializeTrace() {
    // Negative control changes capture retention only. All 0.2.5 development
    // switches and DMA/frontend semantics remain exactly as before.
    firstFailureEnabled=!checkKernelArgument("-brcmvtdrollingtrace");
    sysctl_register_oid(&sysctl__debug_broadcomvtd);
}
#else
void initializeTrace() {}
#endif
} // namespace bvp
