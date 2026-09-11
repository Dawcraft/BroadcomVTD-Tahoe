#pragma once
#include <stdint.h>

namespace bvp {
// Generic ownership policy. No mbuf, PCI ID, Broadcom offsets or FIFO numbers.
constexpr unsigned MappingCapacity=64, MaxRanges=8;
constexpr uint64_t MaxPacketBytes=32768;
enum class MapState : uint32_t { Empty, Preparing, Prepared, Submitting, Owned, Completing, Quarantine };
struct Lifetime {
    uint32_t state=0, poison=0;
    uint64_t packet=0, owner=0, queue=0, serial=0;
};
inline MapState stateOf(const Lifetime &s) { return MapState(__atomic_load_n(&s.state,__ATOMIC_ACQUIRE)); }
inline bool transition(Lifetime &s,MapState from,MapState to) {
    auto f=uint32_t(from);
    return __atomic_compare_exchange_n(&s.state,&f,uint32_t(to),false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
inline void poison(Lifetime &s) { __atomic_store_n(&s.poison,1,__ATOMIC_RELEASE); }
inline bool poisoned(const Lifetime &s) { return __atomic_load_n(&s.poison,__ATOMIC_ACQUIRE)!=0; }
inline bool canCompleteNormally(const Lifetime &s,bool hardwareCompletion) {
    return hardwareCompletion && stateOf(s)==MapState::Owned && !poisoned(s);
}
// Quarantine has no implicit outgoing transition. Dedicated reset cleanup must
// prove engine-disabled AND software-detached ownership, not just a reset name,
// forced return or table pressure. Otherwise reboot retains responsibility.
inline void quarantine(Lifetime &s) {
    poison(s);
    auto old=stateOf(s);
    if (old==MapState::Owned || old==MapState::Prepared)
        (void)transition(s,old,MapState::Quarantine);
}
inline bool rangesValid(uint64_t length,unsigned count) {
    return length && length<=MaxPacketBytes && count && count<=MaxRanges;
}
} // namespace bvp
