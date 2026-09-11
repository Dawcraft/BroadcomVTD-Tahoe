#define BVT_HOST_TEST
#include "../POC/Trace.hpp"
#include "../POC/RxMapperCore.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
namespace bvp {
static unsigned events[64];
Event event(uint32_t type,const void *o,const void *p,const void *a,uint32_t flags) {
    Event e{}; e.type=type; e.object=uintptr_t(o);e.packet=uintptr_t(p);e.auxiliary=uintptr_t(a);e.flags=flags;return e;
}
void emit(Event &e) { if (e.type<64) ++events[e.type]; }
}
#include "../POC/RxMapperCore.cpp"
using namespace bvp;
int main(int argc,char **argv) {
    IOMapper mapper;
    assert(!rx::reserve(1,2,3)); assert(IOMemoryDescriptor::live==0);
    rx::enable(true);
    if (argc>1 && !strcmp(argv[1],"complete-failure")) {
        auto m=rx::reserve(1,2,3); assert(m && rx::prepare(*m,&mapper,0x101123,1800));
        assert(rx::publish(*m,4,1)); IOMemoryDescriptor::failComplete=true;
        assert(!rx::completeNormally(*m)); assert(rx::halted()); assert(IOMemoryDescriptor::live==1);
        assert(!rx::abortUnpublished(*m)); assert(rx::find(3)==m);
        puts("PASS: RX failed complete quarantines packet/MD; no free, unmap retry or reuse");return 0;
    }
    auto m=rx::reserve(1,2,3); assert(m && rx::prepare(*m,&mapper,0x101123,1800));
    assert(IOMemoryDescriptor::lastPrepare==(kIODirectionInOut|kIODirectionPrepareNoFault));
    assert(m->address==0x100000 && m->range.address==0x101123);
    assert(!rx::completeNormally(*m)); assert(rx::publish(*m,4,1));
    assert(!rx::publish(*m,4,1)); assert(!rx::abortUnpublished(*m));
    assert(rx::completeNormally(*m)); assert(!rx::find(3));
    assert(IOMemoryDescriptor::lastComplete==kIODirectionInOut && IOMemoryDescriptor::live==0);
    for (unsigned mode=0;mode<4;++mode) {
        m=rx::reserve(1,2,4); assert(m);
        IOMemoryDescriptor::failCreate=mode==0; IOMemoryDescriptor::failPrepare=mode==1;
        IOMemoryDescriptor::failSegment=mode==2; IOMemoryDescriptor::segmentGap=mode==3;
        assert(!rx::prepare(*m,&mapper,0x200003,6000)); assert(rx::abortUnpublished(*m));
        assert(IOMemoryDescriptor::live==0);
        IOMemoryDescriptor::failCreate=IOMemoryDescriptor::failPrepare=IOMemoryDescriptor::failSegment=IOMemoryDescriptor::segmentGap=false;
    }
    m=rx::reserve(1,2,4); assert(m && rx::prepare(*m,&mapper,0x200003,6000));
    assert(rx::abortUnpublished(*m));
    for (unsigned i=0;i<rx::Capacity;++i) {
        m=rx::reserve(1,2,100+i); assert(m && rx::prepare(*m,&mapper,0x300003,2000));
        assert(rx::publish(*m,8,i));
    }
    auto before=IOMemoryDescriptor::completes;
    rx::quarantineQueue(2,15); rx::quarantineOwner(1,1); rx::quarantineQueue(2,17);
    assert(IOMemoryDescriptor::completes==before && IOMemoryDescriptor::live==rx::Capacity);
    assert(!rx::reserve(1,2,10000)); assert(rx::halted());
    for (unsigned i=0;i<rx::Capacity;++i) {
        m=rx::find(100+i); assert(m && !rx::completeNormally(*m) && !rx::abortUnpublished(*m));
        Event e{}; rx::snapshot(i,e); assert(e.packet==100+i && e.payload[8]);
    }
    Event summary{};rx::snapshot(rx::Capacity,summary);
    assert(summary.payload[6]==rx::Capacity);
    puts("PASS: RX opt-in isolation, contiguous IOVA validation, pre-publication rollback, normal completion, 1024-slot non-evicting quarantine, reset/free poison and footer totals");
}
