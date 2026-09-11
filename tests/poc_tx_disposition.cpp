#define BVT_HOST_TEST
#include "../POC/Trace.cpp"
#include "../POC/MapperCore.cpp"
#include "../POC/TxDisposition.cpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace bvp;
using namespace bvp::dispo;
template<typename T> static void put(void *p,unsigned off,T v) {memcpy(static_cast<uint8_t *>(p)+off,&v,sizeof(v));}
static bvp::dispo::Tag tagInput;
static bool tagFound=true,flip;
static unsigned tagCalls;
static bool tagRead(uint64_t,bvp::dispo::Tag &v) {v=tagInput;if(flip && (++tagCalls&1)==0)v.flags7^=0x10;return tagFound;}
int main() {
    unsigned cases=0;auto check=[&](bool b){assert(b);++cases;};
    uint8_t wlc[0x800]{},pub[0x80]{},band[0x30]{},other[0x30]{},queue[8+8+16*24]{};
    uint64_t bssTable[16]{};uint8_t priorities[]={4,2,0,6,8,10,12,14};
    const auto w=reinterpret_cast<uint64_t>(wlc);
    put<uint64_t>(wlc,0,reinterpret_cast<uint64_t>(pub));put<uint64_t>(wlc,0x40,reinterpret_cast<uint64_t>(band));
    put<uint64_t>(wlc,0x50,reinterpret_cast<uint64_t>(other));put<uint64_t>(wlc,0x58,reinterpret_cast<uint64_t>(other));
    put<uint64_t>(wlc,0x338,reinterpret_cast<uint64_t>(bssTable));put<uint64_t>(wlc,0x778,reinterpret_cast<uint64_t>(queue));
    put<uint32_t>(pub,0x34,2);put<uint32_t>(band,4,1);
    // Deliberately unreadable SCB identities: observation must never dereference them.
    put<uint64_t>(band,0x20,0xdead0001);put<uint64_t>(other,0x20,0xdead0002);
    put<uint16_t>(queue,8,16);put<uint16_t>(queue,8+0x18+4*24,2);put<uint16_t>(queue,8+0x1a+4*24,10);
    configure(tagRead,priorities);bvp::dispo::Tag t;t.cachedScb=0xdead0001;t.priority=0;t.bssIndex=0;
    auto s=inspect(w,0,t,true);check(s.recovery==PrimarySpecial);check(s.precedence==4);
    check(s.count==2 && s.limit==10);check(classify(s)==RequeueEligibleSnapshot);
    s.count=10;check(classify(s)==QueueFullDisposal);
    s.coverage&=~StableAnchors;check(classify(s)==Unknown);
    s=inspect(w,2,t,true);check(s.recovery==PrimarySpecial);check(classify(s)==PrimaryMode2Disposal);
    check(!s.queue && !(s.coverage&QueueObserved));
    t.cachedScb=0xdead0002;s=inspect(w,2,t,true);check(s.recovery==AlternateSpecial);check(classify(s)==Mode2FiltersRequired);
    s=inspect(w,0,t,true);check(classify(s)==RequeueEligibleSnapshot);
    t.cachedScb=0xdead0003;bssTable[0]=0xdead0004;s=inspect(w,0,t,true);
    check(s.recovery==NativeRecoveryRequired);check(classify(s)==RecoveryRequired);
    t.flags7=0x10;s=inspect(w,0,t,true);check(classify(s)==TaggedDisposal);
    t.flags7=0;bssTable[0]=0;s=inspect(w,0,t,true);check(classify(s)==NullRecoveryDisposal);
    t.bssIndex=-1;s=inspect(w,0,t,true);check(!(s.coverage&BssSlotObserved));check(classify(s)==RecoveryRequired);
    t.bssIndex=16;s=inspect(w,0,t,true);check(!(s.coverage&BssSlotObserved));
    check(classify(inspect(w,1,t,true))==Mode1Disposal);
    check(classify(inspect(w,0,t,false))==Unknown);
    t.cachedScb=0xdead0001;t.priority=99;check(classify(inspect(w,0,t,true))==QueueUnavailable);
    t.priority=0;put<uint16_t>(queue,8,17);check(classify(inspect(w,0,t,true))==QueueUnavailable);
    put<uint16_t>(queue,8,16);put<uint64_t>(wlc,0x778,0);check(classify(inspect(w,0,t,true))==QueueUnavailable);
    put<uint64_t>(wlc,0x778,reinterpret_cast<uint64_t>(queue));
    put<uint64_t>(band,0x20,0);t.cachedScb=0;check(inspect(w,0,t,true).recovery==NullScb);
    put<uint64_t>(band,0x20,0xdead0001);t.cachedScb=0xdead0001;
    IOMapper mapper;auto m=reserveMapping(11,22,33);VirtualRange range{0x100000,224};check(m && prepareMapping(*m,&mapper,&range,1));
    m->consumed=1;check(transition(*m,MapState::Prepared,MapState::Submitting));check(transition(*m,MapState::Submitting,MapState::Owned));
    markQuarantine(*m,1);Mapping old=*m;uint8_t oldWlc[sizeof(wlc)],oldQueue[sizeof(queue)];
    memcpy(oldWlc,wlc,sizeof(wlc));memcpy(oldQueue,queue,sizeof(queue));tagInput=t;
    observe(19,*m,w,0);check(!memcmp(&old,m,sizeof(old)));check(!memcmp(wlc,oldWlc,sizeof(wlc)) && !memcmp(queue,oldQueue,sizeof(queue)));
    Event last{};for(auto &slot:bvp::ring)if(slot.value.type==TxDisposition && slot.value.sequence>last.sequence)last=slot.value;
    check(last.payload[16]==RequeueEligibleSnapshot && last.payload[0]==19 && last.payload[1]==m->serial);
    check(!m->detached && !m->notified && m->prepared && stateOf(*m)==MapState::Quarantine);
    check(!mappingsHalted() && !firstFailureState && !stopAfter);
    flip=true;tagCalls=0;observe(20,*m,w,0);
    for(auto &slot:bvp::ring)if(slot.value.type==TxDisposition && slot.value.sequence>last.sequence)last=slot.value;
    check(last.payload[16]==Unknown && !(last.payload[3]&StableAnchors));flip=false;
    tagFound=false;observe(21,*m,w,0);check(!memcmp(&old,m,sizeof(old)));tagFound=true;
    __atomic_store_n(&observations,Budget,__ATOMIC_RELAXED);const auto before=bvp::nextSequence;
    observe(22,*m,w,0);check(bvp::nextSequence==before && !mappingsHalted());
    // Actual ownership gates, not diagnostic hints, still decide disposal.
    enableTxCleanup(true);auto epoch=beginTxReset(22);
    check(!completeTxReset(*m,22,reinterpret_cast<uint64_t>(current_thread()),epoch,true,[](uint64_t,uint64_t){assert(false);}));
    check(stateOf(*m)==MapState::Quarantine && !m->detached && !m->notified);
    printf("PASS disposition inputs: %u assertions; native tag/SCB/queue branches, metadata nonmutation, no authority, stable/unknown and bounded observation.\n",cases);
}
