#include "../POC/TxStatusWord.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace bvp;
static constexpr uint64_t target=0x10742c;
struct FakeWrapper {
    unsigned calls=0,poisons=0,returns=0;
    uint32_t run(bool candidate,bool owned,uint64_t caller,uint32_t width,uint32_t nativeValue) {
        // Mirrors the production ordering without calling any real kernel API.
        ++calls;const auto result=nativeValue;
        if (owned && width==4 && result==0xffffffffU) {
            if (returnNativeTxStatusWord(candidate,target,caller,width,result)) ++returns;
            else ++poisons;
        }
        return result;
    }
};
int main() {
    assert(returnNativeTxStatusWord(true,target,target,4,0xffffffffU));
    for (uint64_t caller:{uint64_t(0x1073fe),uint64_t(0x107444),uint64_t(0x10746d),
                         uint64_t(0x2c4245),uint64_t(0x2c2fb8),uint64_t(0x104357),uint64_t(0),target-1,target+1})
        assert(!returnNativeTxStatusWord(true,target,caller,4,0xffffffffU));
    assert(!returnNativeTxStatusWord(false,target,target,4,0xffffffffU));
    assert(!returnNativeTxStatusWord(true,0,0,4,0xffffffffU));
    for (unsigned width:{0U,1U,2U,8U})assert(!returnNativeTxStatusWord(true,target,target,width,0xffffffffU));
    for (unsigned value:{0U,1U,0x80000000U,0xfffffffeU})assert(!returnNativeTxStatusWord(true,target,target,4,value));
    FakeWrapper candidate,legacy;
    for (unsigned v:{0U,1U,0x12345678U,0xffffffffU}) {
        assert(candidate.run(true,true,target,4,v)==v);
        assert(legacy.run(false,true,target,4,v)==v);
    }
    assert(candidate.calls==4 && !candidate.poisons && candidate.returns==1);
    assert(legacy.calls==4 && legacy.poisons==1 && !legacy.returns);
    assert(candidate.run(true,false,target,4,0xffffffffU)==0xffffffffU && !candidate.poisons);
    assert(candidate.run(true,true,0x2c4245,4,0xffffffffU)==0xffffffffU && candidate.poisons==1);
    puts("PASS: exact data-word exception, original read/result preserved, negative control and all other guards unchanged; no ownership grant");
}
