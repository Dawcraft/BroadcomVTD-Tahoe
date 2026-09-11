#define BVT_HOST_TEST
#include "../POC/Trace.cpp"
#include "../POC/MapperCore.cpp"
#include "../POC/TxCleanup.hpp"
#include <cassert>
#include <cstdio>
#include <thread>
#include <atomic>
using namespace bvp;
static IOMapper mapper;
static unsigned frees;
static Mapping *mapped(uint64_t packet,uint64_t queue=2) {
    auto m=reserveMapping(1,queue,packet); assert(m);
    VirtualRange range {0x100123,5000};assert(prepareMapping(*m,&mapper,&range,1));
    assert(transition(*m,MapState::Prepared,MapState::Submitting));m->consumed=1;
    m->startIndex=1023;m->endIndex=1;m->mapRecord=0x98700;
    assert(transition(*m,MapState::Submitting,MapState::Owned));return m;
}
static void freePacket(uint64_t owner,uint64_t packet) {
    auto m=findMapping(packet);assert(owner==1 && m);
    assert(stateOf(*m)==MapState::Completing && !m->prepared && !m->descriptor);
    assert(!reserveMapping(owner,m->queue,packet)); // still protected during callback
    assert(!completeTxReset(*m,m->queue,42,m->resetEpoch,true,freePacket));
    ++frees;
}
static void detach(Mapping &m) {markQuarantine(m,1);noteTxDetached(m,42,0x2c2f43,true);m.notified=1;}
int main(int argc,char **) {
    enableTxCleanup(true);
    assert(!tx::disabledAcknowledged(true,0,true,0));
    assert(!tx::disabledAcknowledged(true,1024,false,0));
    assert(!tx::disabledAcknowledged(false,1024,true,0));
    for (uint32_t s:{0xffffffffU,0x10000000U,0x20000000U,0x30000000U,0x40000000U})
        assert(!tx::disabledAcknowledged(true,1024,true,s));
    assert(tx::disabledAcknowledged(true,1024,true,0x1234));
    if (argc>1) {
        auto m=mapped(7);detach(*m);auto epoch=beginTxReset(2);
        IOMemoryDescriptor::failComplete=true;
        assert(!completeTxReset(*m,2,42,epoch,true,freePacket));
        assert(!completeTxReset(*m,2,42,epoch,true,freePacket));
        assert(mappingsHalted() && m->descriptor && m->packet==7 && !frees);
        assert(IOMemoryDescriptor::completes==1 && !IOMemoryDescriptor::releases);
        puts("PASS TX: failed complete retains backing/MD, blocks retry and free");return 0;
    }
    auto m=mapped(7);markQuarantine(*m,1);auto epoch=beginTxReset(2);
    assert(!completeTxReset(*m,2,42,epoch,true,freePacket)); // reset alone
    noteTxDetached(*m,42,0x2c2f43,false);
    assert(!m->detached);
    noteTxDetached(*m,42,0x2c2f43,true);
    assert(!completeTxReset(*m,2,42,epoch,true,freePacket)); // notification not completed
    m->notified=1;
    assert(!completeTxReset(*m,2,42,epoch,false,freePacket));
    assert(!completeTxReset(*m,3,42,epoch,true,freePacket));
    assert(!completeTxReset(*m,2,43,epoch,true,freePacket));
    assert(!completeTxReset(*m,2,42,epoch+1,true,freePacket));
    enableTxCleanup(false);assert(!completeTxReset(*m,2,42,epoch,true,freePacket));enableTxCleanup(true);
    assert(completeTxReset(*m,2,42,epoch,true,freePacket));assert(frees==1 && !findMapping(7));
    // Repeated forced-return -> reset generations far exceed the fixed 64 slots.
    for (unsigned generation=0;generation<80;++generation) {
        for (unsigned i=0;i<61;++i) {m=mapped(100+i);detach(*m);}
        epoch=beginTxReset(2);
        for (unsigned i=0;i<61;++i) assert(completeTxReset(*findMapping(100+i),2,42,epoch,true,freePacket));
        assert(!IOMemoryDescriptor::live && !mappingsHalted());
    }
    m=mapped(777);assert(!m->detached && !m->resetEpoch && !m->cleanupBlocked);
    assert(finishNormally(*m)); // normal completion not reclassified as reset
    m=mapped(778);detach(*m);markQuarantine(*m,18);epoch=beginTxReset(2);
    assert(!completeTxReset(*m,2,42,epoch,true,freePacket)); // invalid registers remain quarantined
    m=mapped(779);detach(*m);markQuarantine(*m,3);epoch=beginTxReset(2);
    assert(!completeTxReset(*m,2,42,epoch,true,freePacket)); // unexplained free attempt
    m=mapped(780);detach(*m);noteTxDetached(*m,42,0x2c2f43,true);epoch=beginTxReset(2);
    assert(!completeTxReset(*m,2,42,epoch,true,freePacket)); // duplicate ownership
    Event totals{};snapshotMapping(MappingCapacity,totals);
    assert(totals.payload[5]==80*61+1 && totals.payload[2]==3);
    tx::Admission gate;
    assert(gate.read() && gate.read());assert(!gate.write());gate.leave(false);gate.leave(false);
    assert(gate.write());assert(!gate.read() && !gate.write());gate.leave(true);
    std::atomic<unsigned> readers{0},writers{0};std::atomic<bool> bad{false};
    auto read=[&] {for(unsigned i=0;i<30000;++i) if(gate.read()) {
        ++readers;if(writers)bad=true;--readers;gate.leave(false);
    }};
    auto write=[&] {for(unsigned i=0;i<30000;++i) if(gate.write()) {
        if(writers.fetch_add(1) || readers)bad=true;
        --writers;gate.leave(true);
    }};
    std::thread a(read),b(read),c(write);a.join();b.join();c.join();assert(!bad);
    puts("PASS TX: exact detach + disabled reset, wrong proof/callback/ownership rejection, 80x61 recycling, unchanged normal mapping, nonblocking reader/exclusive cleanup admission");
}
