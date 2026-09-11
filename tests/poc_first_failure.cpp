#define BVT_HOST_TEST
#include "../POC/Trace.cpp"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
namespace bvp { void snapshotMapping(unsigned,Event &) {} }
using namespace bvp;
static void noise(unsigned count) {
    for (unsigned i=0;i<count;++i) { auto e=event(MapperInventory); emit(e); }
}
int main(int argc,char **argv) {
    const char *mode=argc>1?argv[1]:"normal";
    auto acquisition=event(Acquisition,nullptr,nullptr,nullptr,4);
    acquisition.payload[2]=0x10000; acquisition.payload[3]=0x10000; emit(acquisition);
    Event s {};snapshotFirstFailure(s); assert(!s.sequence && !s.payload[0] && !s.flags);
    if (!strcmp(mode,"rolling")) {
        firstFailureEnabled=false;
        auto h=event(MapperHalted,nullptr,nullptr,nullptr,18);emit(h);
        noise(Capacity*2);snapshotFirstFailure(s);
        assert(s.flags==0x40000000 && !stopAfter && nextSequence==Capacity*2+2);
        assert(!firstFailureState);puts("PASS first-failure disabled: exact golden rolling retention");return 0;
    }
    if (!strcmp(mode,"already-frozen")) {
        freezeSoon();noise(8192);uint64_t previous=nextSequence,stop=stopAfter;
        auto h=event(MapperHalted,nullptr,nullptr,nullptr,18);emit(h);snapshotFirstFailure(s);
        assert(!h.sequence && !s.sequence && s.payload[0]==MapperHalted && s.flags==18);
        assert(nextSequence==previous && stopAfter==stop && s.payload[4]==13);
        puts("PASS already-frozen ring: persistent first halt still saved; prior freeze preserved");return 0;
    }
    if (!strcmp(mode,"dropped-trigger")) {
        ring[nextSequence%Capacity].busy=1;
        auto h=event(RxMapperHalted,nullptr,nullptr,nullptr,18);emit(h);snapshotFirstFailure(s);
        assert(!s.sequence && s.payload[0]==RxMapperHalted && busyDrops==1 && stopAfter);
        puts("PASS dropped ring trigger: persistent first halt still saved; drop remains explicit");return 0;
    }
    if (!strcmp(mode,"concurrent")) {
        // Nonblocking, single publication; losing writers may not overwrite it.
        std::vector<std::thread> threads;
        for (unsigned i=0;i<32;++i) threads.emplace_back([i]{auto h=event(MapperHalted,nullptr,nullptr,nullptr,18+i);emit(h);});
        for (auto &t:threads) t.join();
        snapshotFirstFailure(s);assert(firstFailureState==2 && s.payload[4]==13);
        Event saved=s;noise(Capacity);snapshotFirstFailure(s);assert(!memcmp(&s,&saved,sizeof(s)));
        assert(stopAfter && nextSequence==stopAfter);puts("PASS concurrent first halt publication and immutable footer");return 0;
    }
    // Wrap the ring repeatedly before a fault, exactly the overnight loss case.
    noise(Capacity*4);
    auto benign=event(FatalEnter);benign.payload[1]=45;emit(benign);assert(!stopAfter && !firstFailureState);
    auto q=event(RxMapperQuarantine,nullptr,nullptr,nullptr,18);emit(q);
    auto h=event(RxMapperHalted,nullptr,nullptr,nullptr,18);emit(h);
    const auto haltSeq=h.sequence;
    snapshotFirstFailure(s);assert(s.sequence==haltSeq && s.flags==18 && s.payload[0]==RxMapperHalted);
    assert(s.payload[1]==0x10000 && s.payload[2]==0x10000 && s.payload[4]==13);
    assert(stopAfter==haltSeq+8192);
    auto later=event(MapperHalted,nullptr,nullptr,nullptr,18);emit(later);
    auto invalid=event(InvalidRegister);invalid.payload[1]=0x10742c;emit(invalid);
    noise(Capacity*4);
    assert(nextSequence==stopAfter && ring[(haltSeq-1)%Capacity].value.sequence==haltSeq);
    assert(ring[(q.sequence-1)%Capacity].value.sequence==q.sequence);
    assert(ring[(invalid.sequence-1)%Capacity].value.sequence==invalid.sequence);
    snapshotFirstFailure(s);assert(s.sequence==haltSeq);assert(!busyDrops && !tableDrops);
    // Snapshot cannot read a footer while its sole writer is still constructing it.
    firstFailureState=1;snapshotFirstFailure(s);assert(s.flags==0x80000000 && !s.payload[0]);
    puts("PASS golden data path: only first permanent halt freezes trace; prelude/tail/PCs retained");
}
