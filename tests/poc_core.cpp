#define BVT_HOST_TEST
#include "../POC/Trace.cpp"
#include "../POC/MapperCore.cpp"
#include <cassert>
#include <iostream>
int main(int argc,char **) {
    using namespace bvp;
    IOMapper mapper;
    VirtualRange r {0x100123,5000};
    if (argc>1) {
        auto fault=reserveMapping(1,2,3); assert(prepareMapping(*fault,&mapper,&r,1));
        assert(transition(*fault,MapState::Prepared,MapState::Submitting)); fault->consumed=1;
        assert(transition(*fault,MapState::Submitting,MapState::Owned));
        IOMemoryDescriptor::failComplete=true;
        assert(!finishNormally(*fault));
        assert(findMapping(3)==fault && fault->descriptor && IOMemoryDescriptor::live==1);
        assert(stateOf(*fault)==MapState::Quarantine && mappingsHalted());
        assert(!finishNormally(*fault) && IOMemoryDescriptor::completes==1 && IOMemoryDescriptor::releases==0);
        std::cout<<"PASS: failed completion retains descriptor/packet, no retry/unmap/free, halted admission\n";
        return 0;
    }
    auto m=reserveMapping(1,2,3); assert(m);
    assert(prepareMapping(*m,&mapper,&r,1));
    assert(m->count==2 && m->segments[0].address==0x100123 && m->segments[0].length==3805);
    assert(m->segments[1].address==0x101000 && m->segments[1].length==1195);
    assert(transition(*m,MapState::Prepared,MapState::Submitting)); m->consumed=1;
    assert(!finishNormally(*m));
    assert(transition(*m,MapState::Submitting,MapState::Owned));
    assert(finishNormally(*m)); assert(!findMapping(3));
    assert(IOMemoryDescriptor::completes==1 && IOMemoryDescriptor::live==0);
    m=reserveMapping(1,2,3); assert(prepareMapping(*m,&mapper,&r,1));
    assert(abortUnpublished(*m)); assert(IOMemoryDescriptor::live==0);
    IOMemoryDescriptor::failPrepare=true;
    m=reserveMapping(1,2,3); assert(!prepareMapping(*m,&mapper,&r,1)); assert(abortUnpublished(*m));
    IOMemoryDescriptor::failPrepare=false;
    assert(IOMemoryDescriptor::live==0);
    IOMemoryDescriptor::failSegment=true;
    m=reserveMapping(1,2,3); assert(!prepareMapping(*m,&mapper,&r,1)); assert(abortUnpublished(*m));
    IOMemoryDescriptor::failSegment=false;
    assert(IOMemoryDescriptor::live==0);
    auto completed=IOMemoryDescriptor::completes;
    for(unsigned i=0;i<MappingCapacity;++i) {
        m=reserveMapping(1,2,100+i); assert(m); assert(prepareMapping(*m,&mapper,&r,1));
        assert(transition(*m,MapState::Prepared,MapState::Submitting)); m->consumed=1;
        assert(transition(*m,MapState::Submitting,MapState::Owned));
        markQuarantine(*m,1); // Forced software reclaim.
        assert(!finishNormally(*m) && !abortUnpublished(*m));
        assert(findMapping(100+i)==m && m->descriptor);
    }
    quarantineQueue(2,15); quarantineOwner(1,4); // Repeated reset/stop cannot free.
    assert(!reserveMapping(1,2,999) && !mappingsHalted());
    assert(!reserveMapping(1,2,1000) && !mappingsHalted());
    assert(IOMemoryDescriptor::completes==completed && IOMemoryDescriptor::live==MappingCapacity);
    unsigned count=0;
    for(unsigned i=0;i<MappingCapacity;++i) {
        Event e {};snapshotMapping(i,e);assert(e.packet && e.payload[1]==uint32_t(MapState::Quarantine));++count;
    }
    assert(count==MappingCapacity);
    std::cout<<"PASS: mapped page/offset construction, unpublished rollback, prepare/segment failures, normal cleanup, forced reclaim/reset/stop retain all 64 packets+MDs, no-credit refuses without permanent halt or reuse, inventory\n";
}
