#pragma once
#include <stdint.h>

namespace bvp {
// Diagnostic admission only. Exhaustion never changes original RX/TX behavior.
constexpr uint32_t RxRecordLimit = 12288;
inline bool sparseRxPoll(uint64_t ordinal) {
    return ordinal && (ordinal <= 64 || !(ordinal & (ordinal-1)));
}
inline uint32_t rxDescriptorCount(uint32_t count, uint32_t begin, uint32_t end) {
    if (!count || count > 4096 || (count & (count-1)) || begin >= count || end >= count) return 0;
    return (end-begin)&(count-1);
}
// Saturating CAS budget; no spin or wait in a driver route. Contention drops
// observation only and is separately recorded as a trace coverage loss.
inline bool reserveRxRecord(uint32_t &used, bool &limit, bool &contended) {
    limit = false; contended = false;
    uint32_t old = __atomic_load_n(&used,__ATOMIC_RELAXED);
    if (old > RxRecordLimit) return false;
    if (!__atomic_compare_exchange_n(&used,&old,old+1,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED)) {
        contended = true; return false;
    }
    limit = old == RxRecordLimit;
    return !limit;
}
}
