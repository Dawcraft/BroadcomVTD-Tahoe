#define BVT_HOST_TEST
#include "../POC/Trace.hpp"
#include "../POC/RxMapperCore.hpp"
#include "../POC/RxGeneration.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <atomic>
namespace bvp {
static unsigned counts[80];
Event event(uint32_t t,const void *o,const void *p,const void *a,uint32_t f) {
    Event e{}; e.type=t;e.object=uintptr_t(o);e.packet=uintptr_t(p);e.auxiliary=uintptr_t(a);e.flags=f;return e;
}
void emit(Event &e) { assert(e.type<80); ++counts[e.type]; }
}
#include "../POC/RxMapperCore.cpp"
using namespace bvp;
static IOMapper mapper;
static rx::Mapping *mapped(uint64_t packet,uint64_t queue=2) {
    auto m=rx::reserve(1,queue,packet); assert(m && rx::prepare(*m,&mapper,0x100023,2044));
    assert(rx::publish(*m,0x1000+packet*0x70,unsigned(packet&255))); return m;
}
int main(int argc,char **argv) {
    assert(!rx::disabledAcknowledged(true,0,true,0));
    assert(!rx::disabledAcknowledged(true,256,false,0));
    assert(!rx::disabledAcknowledged(false,256,true,0));
    for (uint32_t status:{0xffffffffU,0x10000000U,0x20000000U,0x30000000U,0x40000000U})
        assert(!rx::disabledAcknowledged(true,256,true,status));
    assert(rx::disabledAcknowledged(true,256,true,0));
    assert(rx::disabledAcknowledged(true,256,true,0x1234));
    rx::enable(true);
    if (argc>1 && !strcmp(argv[1],"complete-failure")) {
        auto m=mapped(99);auto epoch=rx::beginReset(2,42);
        assert(rx::acknowledgeReset(2,42,epoch,true)==1);
        IOMemoryDescriptor::failComplete=true;
        assert(!rx::completeResetReclaim(*m,42,true));
        assert(rx::halted() && IOMemoryDescriptor::live==1 && rx::find(99)==m);
        auto calls=IOMemoryDescriptor::completes;
        assert(!rx::completeResetReclaim(*m,42,true));
        assert(IOMemoryDescriptor::completes==calls && !rx::abortUnpublished(*m));
        puts("PASS: failed reset-cleanup complete retains mapping/backing, no retry or reuse"); return 0;
    }
    auto m=mapped(10);
    rx::quarantine(*m,1);
    assert(!rx::completeResetReclaim(*m,42,true)); // force alone never qualifies
    auto epoch=rx::beginReset(2,42);
    assert(!rx::acknowledgeReset(2,42,epoch,false));
    assert(!rx::completeResetReclaim(*m,42,true));
    assert(!rx::acknowledgeReset(3,42,epoch,true));
    assert(!rx::acknowledgeReset(2,43,epoch,true));
    assert(rx::acknowledgeReset(2,42,epoch,true)==1);
    assert(!rx::completeResetReclaim(*m,42,false)); // still in ring
    assert(!rx::completeResetReclaim(*m,43,true)); // cross-thread boundary
    rx::invalidateQueue(2); // fill/init/re-enable invalidates a prior epoch
    assert(!rx::completeResetReclaim(*m,42,true));
    epoch=rx::beginReset(2,42);
    assert(rx::acknowledgeReset(2,42,epoch,true)==1);
    rx::quarantine(*m,3); // uncertain free revokes permission
    assert(!rx::completeResetReclaim(*m,42,true));
    epoch=rx::beginReset(2,42);
    assert(rx::acknowledgeReset(2,42,epoch,true)==1);
    assert(rx::completeResetReclaim(*m,42,true));
    assert(!rx::completeResetReclaim(*m,42,true)); // duplicate cannot release twice
    assert(!rx::find(10) && IOMemoryDescriptor::live==0);
    // Four dozen ordinary 240-packet generations, far beyond the old capacity.
    // Newly allocated/reused slots never inherit another packet's reset proof.
    for (unsigned generation=0;generation<48;++generation) {
        for (unsigned i=0;i<240;++i) mapped(100+i);
        epoch=rx::beginReset(2,42);
        assert(rx::acknowledgeReset(2,42,epoch,true)==240);
        for (unsigned i=0;i<240;++i) {
            m=rx::find(100+i);assert(m && rx::completeResetReclaim(*m,42,true));
        }
        assert(!rx::halted() && IOMemoryDescriptor::live==0);
    }
    m=mapped(100);
    assert(!m->resetEpoch && !m->resetAcknowledged);
    assert(rx::completeNormally(*m));
    Event totals{}; rx::snapshot(rx::Capacity,totals);
    assert(totals.payload[7]==48*240+1 && totals.payload[5]==1);
    assert(counts[RxResetCompleted]==48*240+1);
    rx::OperationTable table;
    bool nested=false;auto lease=table.enter(100,42,rx::Operation::Init,nested);
    assert(lease && !nested);
    auto child=table.enter(100,42,rx::Operation::Enable,nested); assert(child==lease && nested);
    table.leave(child,nested);
    assert(!table.enter(100,43,rx::Operation::Enable,nested));
    assert(!table.enter(100,42,rx::Operation::Reclaim,nested));
    table.leave(lease,false);
    std::atomic<unsigned> inside{0}; std::atomic<bool> bad{false};
    auto worker=[&](uint64_t thread) {
        for (unsigned i=0;i<50000;++i) {
            bool inner=false; auto slot=table.enter(100,thread,rx::Operation::Reclaim,inner);
            if (!slot) continue;
            if (inside.fetch_add(1)!=0) bad=true;
            if (inside.fetch_sub(1)!=1) bad=true;
            table.leave(slot,inner);
        }
    };
    std::thread a(worker,41),b(worker,42),c(worker,43);a.join();b.join();c.join();
    assert(!bad && inside==0);
    for (uint64_t i=1;i<=31;++i) {
        lease=table.enter(i,42,rx::Operation::Reset,nested); assert(lease);table.leave(lease,nested);
    }
    assert(!table.enter(999,42,rx::Operation::Reset,nested));
    puts("PASS: disabled acknowledgement + exact detach, stale/cross-thread/re-enable rejection, duplicate safety, 48x240 slot reuse, unchanged normal RX, bounded non-waiting concurrency admission");
}
