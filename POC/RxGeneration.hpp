#pragma once
#include <stdint.h>

namespace bvp { namespace rx {
// Driver-independent, non-waiting admission for operations which may race
// quiescence cleanup. Queue keys are never reused during this experimental boot.
enum class Operation : uint32_t { Fill=1, Reclaim, Reset, Init, Enable };
struct OperationSlot {
    uint64_t queue=0, thread=0;
    uint32_t busy=0, operation=0;
};
class OperationTable {
    OperationSlot slots[32] {};
public:
    OperationSlot *enter(uint64_t queue,uint64_t thread,Operation op,bool &nested) {
        nested=false;
        if (!queue || !thread) return nullptr;
        for (auto &s:slots) {
            uint64_t key=__atomic_load_n(&s.queue,__ATOMIC_ACQUIRE);
            if (!key) {
                uint64_t zero=0;
                if (__atomic_compare_exchange_n(&s.queue,&zero,queue,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) key=queue;
                else key=zero;
            }
            if (key!=queue) continue;
            uint32_t zero=0;
            if (__atomic_compare_exchange_n(&s.busy,&zero,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
                __atomic_store_n(&s.thread,thread,__ATOMIC_RELAXED);
                __atomic_store_n(&s.operation,uint32_t(op),__ATOMIC_RELAXED);
                __atomic_store_n(&s.busy,2,__ATOMIC_RELEASE); return &s;
            }
            // Only the original Init -> Enable nesting is admitted. Not a
            // general recursive lock, and never a wait in an interrupt path.
            if (zero==2 && op==Operation::Enable &&
                __atomic_load_n(&s.thread,__ATOMIC_RELAXED)==thread &&
                __atomic_load_n(&s.operation,__ATOMIC_RELAXED)==uint32_t(Operation::Init) &&
                __atomic_load_n(&s.busy,__ATOMIC_ACQUIRE)==2) {
                nested=true; return &s;
            }
            return nullptr;
        }
        return nullptr;
    }
    void leave(OperationSlot *slot,bool nested) {
        if (slot && !nested) __atomic_store_n(&slot->busy,0,__ATOMIC_RELEASE);
    }
};
// A true reset return without a real status read (nrxd==0) is NOT evidence.
inline bool disabledAcknowledged(bool returned,uint32_t entries,bool read,uint32_t status) {
    return returned && entries && read && status!=0xffffffffU && !(status&0xf0000000U);
}
} }
