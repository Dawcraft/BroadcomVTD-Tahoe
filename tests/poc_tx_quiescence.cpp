#define BVT_HOST_TEST
#include "../POC/Trace.cpp"
#include "../POC/MapperCore.cpp"
#include "../POC/TxQuiescence.cpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace bvp;
template<class T>static void put(void *p,unsigned off,T v){memcpy(static_cast<uint8_t *>(p)+off,&v,sizeof(v));}
int main(){
    using namespace bvp::quiet;
    unsigned checks=0;auto check=[&](bool b){if(!b)fprintf(stderr,"failed quiescence assertion %u\n",checks+1);assert(b);++checks;};
    Bank b;b.read(1,2,4,DmaStatus0,0x10000000,1,100);b.read(1,2,4,DmaStatus0,0x20000000,2,200);
    b.read(1,2,4,DmaStatus0,0xffffffff,3,300);b.read(1,2,4,DmaStatus0,0x20000000,4,400);
    check(b.used==1 && b.samples[0].count==4 && b.samples[0].changes==3);
    check(b.samples[0].seen==14 && b.samples[0].first==0x10000000 && b.samples[0].last==0x20000000);
    check(b.samples[0].lastOrdinal==4 && b.samples[0].firstNS==100 && b.samples[0].lastNS==400);
    b.read(1,3,4,DmaStatus0,0,5,500);check(b.used==2 && b.samples[1].seen==1);
    for(unsigned i=0;i<100;++i)b.read(100+i,20,2,ObjectData,0xffff,6+i,600+i);
    check(b.used==Samples && b.overflow==6);check(b.samples[2].seen==2);
    b.read(1,2,4,DmaStatus0,0,200,20000);check(b.samples[0].last==0 && b.samples[0].seen==15);
    uint8_t hw[0x200]{},di[0x100]{},osh[0x60]{},wlc[16]{};
    put<void *>(hw,0,wlc);put<void *>(hw,0x10,osh);put<void *>(hw,0x20,di);
    put<uint64_t>(hw,0xd8,0x100000);put<uint32_t>(hw,0x84,48);
    put<void *>(di,0x30,osh);put<uint64_t>(di,0x48,0x100200);
    uint8_t beforeHw[sizeof(hw)],beforeDi[sizeof(di)];memcpy(beforeHw,hw,sizeof(hw));memcpy(beforeDi,di,sizeof(di));
    auto h=beginFlush(hw,1);check(h>=0);auto &c=contexts[h];check(c.revision==48 && c.phase==1 && c.owner==reinterpret_cast<uint64_t>(osh));
    quiet::read(osh,reinterpret_cast<void *>(0x100210),4,0x20000000,0x1061cd);
    quiet::read(osh,reinterpret_cast<void *>(0x100150),4,0,0x1061ba);
    quiet::read(osh,reinterpret_cast<void *>(0x100160),4,0x1001f,0x108624);
    quiet::read(osh,reinterpret_cast<void *>(0x100166),2,0,0x10863f);
    check(c.bank.used==4 && c.bank.samples[3].region==ObjectData);
    quiet::read(osh,reinterpret_cast<void *>(0x100160),4,0x10020,0x108624);
    quiet::read(osh,reinterpret_cast<void *>(0x100166),2,42,0x10863f);
    check(c.bank.samples[3].count==1); // Never log unrelated firmware data.
    quiet::read(reinterpret_cast<void *>(77),reinterpret_cast<void *>(0x100210),4,7,0x1061cd);
    check(c.bank.samples[0].count==1);
    control(di,9,false);control(di,9,true);check(c.operations==2 && c.lastControl==7);
    endFlush(h);check(c.phase==2);
    quiet::read(osh,reinterpret_cast<void *>(0x100210),4,7,0x1061cd);check(c.bank.samples[0].count==1);
    check(enterSync(wlc,2,0,osh)==-1);auto s=enterSync(wlc,1,2,osh);check(s==h && c.phase==3 && c.mode==2);
    IOMapper mapper;auto m=reserveMapping(reinterpret_cast<uint64_t>(osh),reinterpret_cast<uint64_t>(di),0x999);
    VirtualRange vr{0x100003,224};check(m && prepareMapping(*m,&mapper,&vr,1));m->consumed=1;
    check(transition(*m,MapState::Prepared,MapState::Submitting));check(transition(*m,MapState::Submitting,MapState::Owned));
    markQuarantine(*m,1);Mapping old=*m;
    reclaimed(di,reinterpret_cast<void *>(m->packet),m->serial,1,0x14709f);
    check(!memcmp(&old,m,sizeof(old)) && m->prepared && stateOf(*m)==MapState::Quarantine);
    check(!memcmp(hw,beforeHw,sizeof(hw)) && !memcmp(di,beforeDi,sizeof(di)));
    check(!mappingsHalted() && !firstFailureState && !stopAfter);
    Event last{};for(auto &v:ring)if(v.value.type==TxQuiescence && v.value.flags==Reclaimed && v.value.sequence>last.sequence)last=v.value;
    check(last.payload[14]==m->serial && last.payload[15]==0x14709f && last.payload[16]==1);
    // Positive native read values still do NOT certify any cleanup or disposal.
    enableTxCleanup(true);auto epoch=beginTxReset(m->queue);
    check(!completeTxReset(*m,m->queue,reinterpret_cast<uint64_t>(current_thread()),epoch,true,[](uint64_t,uint64_t){assert(false);}));
    control(di,13,false);control(di,13,true);fatal(osh);check(c.operations==4 && c.fatals==1);
    leaveSync(s);check(!c.phase && !live);
    // Lock/slot/budget pressure affects observation ONLY.
    __atomic_store_n(&mutex,1,__ATOMIC_RELEASE);check(beginFlush(hw,1)==-1);__atomic_store_n(&mutex,0,__ATOMIC_RELEASE);check(quiet::busyDrops>0);
    for(unsigned i=0;i<Contexts;++i){contexts[i].phase=1;contexts[i].thread=999;}
    check(beginFlush(hw,1)==-1);for(auto &v:contexts)v.phase=0;live=0;
    transactions=Transactions;check(beginFlush(hw,1)==-1);
    check(!mappingsHalted() && !firstFailureState && stateOf(*m)==MapState::Quarantine);
    records=Records-1;coverage(ContextFull);check(records==Records);auto n=nextSequence;coverage(ContextFull);
    check(nextSequence==n && recordDrops>=2);
    printf("PASS quiescence diagnostic: %u assertions; real observer, coalesced reads, nonmutation, no ownership authority, bounded failures.\n",checks);
}
