#pragma once
#include <stdint.h>

namespace bvp { namespace tx {
// Reader admission leaves ordinary submissions/completions concurrent. A reset
// cleanup is exclusive with TX publication, reclaim and provider teardown.
// No spinning, sleeping, allocation or assumptions about a particular NIC.
class Admission {
    uint32_t users=0;
public:
    static constexpr uint32_t Writer=0x80000000U;
    bool read() {
        auto n=__atomic_load_n(&users,__ATOMIC_ACQUIRE);
        for (unsigned i=0;i<4 && !(n&Writer) && n<Writer-1;++i)
            if (__atomic_compare_exchange_n(&users,&n,n+1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return true;
        return false;
    }
    bool write() {
        uint32_t zero=0;
        return __atomic_compare_exchange_n(&users,&zero,Writer,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
    }
    void leave(bool writer) {
        if (writer) __atomic_store_n(&users,0,__ATOMIC_RELEASE);
        else __atomic_fetch_sub(&users,1,__ATOMIC_RELEASE);
    }
};
inline bool disabledAcknowledged(bool returned,uint32_t entries,bool seen,uint32_t status) {
    return returned && entries && seen && status!=0xffffffffU && !(status&0xf0000000U);
}
} }
