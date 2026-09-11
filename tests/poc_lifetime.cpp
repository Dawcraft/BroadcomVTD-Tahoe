#include "../POC/Lifetime.hpp"
#include <cassert>
#include <thread>
#include <vector>
#include <iostream>
int main() {
    using namespace bvp;
    Lifetime m;
    assert(transition(m,MapState::Empty,MapState::Preparing));
    assert(transition(m,MapState::Preparing,MapState::Prepared));
    assert(transition(m,MapState::Prepared,MapState::Submitting));
    assert(!canCompleteNormally(m,true));
    assert(transition(m,MapState::Submitting,MapState::Owned));
    assert(!canCompleteNormally(m,false));
    assert(canCompleteNormally(m,true));
    quarantine(m);
    assert(stateOf(m)==MapState::Quarantine && poisoned(m));
    assert(!canCompleteNormally(m,true));
    for(unsigned i=0;i<1000;++i) quarantine(m);
    assert(stateOf(m)==MapState::Quarantine);
    Lifetime submitting;
    submitting.state=uint32_t(MapState::Submitting);
    quarantine(submitting);
    assert(poisoned(submitting));
    assert(!canCompleteNormally(submitting,true));
    Lifetime slots[MappingCapacity];
    unsigned counts[8] {};
    std::vector<std::thread> workers;
    for(unsigned t=0;t<8;++t) workers.emplace_back([&,t] {
        for(auto &s:slots) if(transition(s,MapState::Empty,MapState::Preparing)) ++counts[t];
    });
    for(auto &w:workers)w.join();
    unsigned total=0; for(auto c:counts)total+=c; assert(total==MappingCapacity);
    for(auto &s:slots) assert(!transition(s,MapState::Empty,MapState::Preparing));
    assert(!rangesValid(0,1) && !rangesValid(MaxPacketBytes+1,1));
    assert(!rangesValid(1,MaxRanges+1) && rangesValid(4096,1));
    std::cout<<"PASS: normal-only completion, forced quarantine, repeated poison, submitting poison, concurrent bounded reservation, capacity/range rejection\n";
}
