#include "../POC/ReadPolicy.hpp"
#include "../POC/Lifetime.hpp"
#include "../POC/TxCleanup.hpp"
#include "../POC/RxGeneration.hpp"
#include "../Config/TargetGate.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace bvp;
int main() {
    static_assert(sizeof(ownershipReadSites)/sizeof(ownershipReadSites[0])==7,"ABI inputs");
    for (auto site:ownershipReadSites) {
        assert(site.returnPC && site.use!=ReadUse::Native);
        assert(readUse(site.returnPC,ownershipReadSites)==site.use);
        assert(!nativeOwnsReadResult(true,site.use,4,0xffffffffU));
        Lifetime life {};life.state=uint32_t(MapState::Owned);
        // Existing error branch, not a fabricated normal completion.
        quarantine(life);
        assert(stateOf(life)==MapState::Quarantine && !canCompleteNormally(life,true));
    }
    for (uint64_t pc:{uint64_t(0x104357),uint64_t(0x10742c),uint64_t(0x109365),
                      uint64_t(0x1073fe),uint64_t(0),uint64_t(0x100000)}) {
        const auto use=readUse(pc,ownershipReadSites);
        assert(use==ReadUse::Native);
        assert(nativeOwnsReadResult(true,use,4,0xffffffffU));
        assert(!nativeOwnsReadResult(false,use,4,0xffffffffU));
        Lifetime life {};life.state=uint32_t(MapState::Owned);
        // Preserving a native data word grants no mapping-state transition.
        assert(stateOf(life)==MapState::Owned && !canCompleteNormally(life,false));
    }
    for (unsigned width:{0U,1U,2U,8U})
        assert(!nativeOwnsReadResult(true,ReadUse::Native,width,0xffffffffU));
    for (unsigned value:{0U,1U,0x12345678U,0xfffffffeU})
        assert(!nativeOwnsReadResult(true,ReadUse::Native,4,value));
    assert(!tx::disabledAcknowledged(true,240,true,0xffffffffU));
    assert(!rx::disabledAcknowledged(true,240,true,0xffffffffU));
    assert(!tx::disabledAcknowledged(true,240,false,0));
    assert(!rx::disabledAcknowledged(true,240,false,0));
    puts("PASS: native read results are not ownership grants; all seven completion/reset proof sites retain fail-closed quarantine; no reset predicate change");
}
