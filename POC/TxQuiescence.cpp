#include "TxQuiescence.hpp"
#include "Trace.hpp"
#ifndef BVT_HOST_TEST
#include <kern/thread.h>
#include <libkern/libkern.h>
#endif

namespace bvp { namespace quiet {
template<class T> static T get(const void *p,unsigned offset) {
    T v;memcpy(&v,static_cast<const uint8_t *>(p)+offset,sizeof(v));return v;
}
struct Context {
    Bank bank;
    uint64_t id=0,thread=0,hw=0,wlc=0,owner=0,regs=0,ordinal=0;
    uint64_t queue[6] {},dma[6] {},lastControl=0;
    uint32_t bitmap=0,revision=0,mode=0,phase=0,operations=0,fatals=0,crossThread=0,objectAddress=0;
};
static Context contexts[Contexts];
static uint32_t mutex,live,transactions,records;
static uint64_t busyDrops,recordDrops;
// Try once. Diagnostic contention never affects native execution or ownership.
struct Lock {
    bool held;
    Lock():held(false) {
        uint32_t zero=0;held=__atomic_compare_exchange_n(&mutex,&zero,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
        if (!held) __atomic_add_fetch(&busyDrops,1,__ATOMIC_RELAXED);
    }
    ~Lock(){if(held)__atomic_store_n(&mutex,0,__ATOMIC_RELEASE);}
};
static uint64_t thread(){return reinterpret_cast<uint64_t>(current_thread());}
static void send(Event &e) {
    if (records>=Records) { ++recordDrops;return; }
    if (records==Records-1) {
        e=event(TxQuiescence,nullptr,nullptr,nullptr,Coverage);
        e.payload[1]=RecordLimit;e.payload[2]=__atomic_load_n(&busyDrops,__ATOMIC_RELAXED);
        e.payload[3]=++recordDrops;e.payload[4]=transactions;
    }
    ++records;emit(e);
}
static void coverage(uint32_t reason,uint64_t id=0) {
    auto e=event(TxQuiescence,nullptr,nullptr,nullptr,Coverage);
    e.payload[0]=id;e.payload[1]=reason;e.payload[2]=__atomic_load_n(&busyDrops,__ATOMIC_RELAXED);
    e.payload[3]=recordDrops;e.payload[4]=transactions;send(e);
}
static void summary(Context &c,Stage stage,const void *di=nullptr,const void *packet=nullptr,
                    uint64_t serial=0,uint64_t caller=0,uint32_t range=0) {
    auto e=event(TxQuiescence,di?di:reinterpret_cast<void *>(c.hw),packet,reinterpret_cast<void *>(c.owner),stage);
    e.payload[0]=c.id;e.payload[1]=c.wlc;e.payload[2]=c.bitmap;e.payload[3]=c.revision;
    e.payload[4]=c.mode;e.payload[5]=c.phase;e.payload[6]=c.regs;e.payload[7]=c.ordinal;
    e.payload[8]=c.bank.used;e.payload[9]=c.bank.overflow;e.payload[10]=c.operations;
    e.payload[11]=c.lastControl;e.payload[12]=c.fatals;e.payload[13]=c.crossThread;
    e.payload[14]=serial;e.payload[15]=caller;e.payload[16]=range;
    e.payload[17]=__atomic_load_n(&busyDrops,__ATOMIC_RELAXED);send(e);
}
static void dump(Context &c) {
    for(unsigned i=0;i<c.bank.used;++i) {
        const auto &s=c.bank.samples[i];
        auto e=event(TxQuiescence,reinterpret_cast<void *>(c.hw),nullptr,reinterpret_cast<void *>(s.reg),ReadSample);
        uint64_t p[]={c.id,s.region,s.caller,s.size,s.first,s.last,s.count,s.changes,s.seen,
            s.firstOrdinal,s.lastOrdinal,s.firstNS,s.lastNS,c.owner,c.regs,c.bank.overflow,c.phase,i};
        memcpy(e.payload,p,sizeof(p));send(e);
    }
}
int beginFlush(const void *hw,uint32_t bitmap) {
    Lock lock;if(!lock.held)return -1;
    if (transactions==Transactions) { coverage(TransactionLimit);return -1; }
    const auto tid=thread(),owner=get<uint64_t>(hw,0x10);
    int slot=-1;
    for(unsigned i=0;i<Contexts;++i) {
        if (contexts[i].phase==2 && contexts[i].thread==tid && contexts[i].owner==owner) {
            coverage(ReplacedContext,contexts[i].id);contexts[i].phase=0;__atomic_sub_fetch(&live,1,__ATOMIC_RELEASE);
        }
        if(!contexts[i].phase && slot<0)slot=int(i);
    }
    if(slot<0){coverage(ContextFull);return -1;}
    auto &c=contexts[slot];memset(&c,0,sizeof(c));
    c.id=++transactions;c.thread=tid;c.hw=reinterpret_cast<uint64_t>(hw);c.owner=owner;
    c.wlc=get<uint64_t>(hw,0);c.regs=get<uint64_t>(hw,0xd8);c.revision=get<uint32_t>(hw,0x84);
    c.bitmap=bitmap;c.phase=1;
    // Exactly the same six live native data-ring entries already read by wrapFlush.
    for(unsigned i=0;i<6;++i)if(bitmap&(1U<<i)) {
        c.queue[i]=get<uint64_t>(hw,0x20+8*i);
        if(c.queue[i] && get<uint64_t>(reinterpret_cast<void *>(c.queue[i]),0x30)==owner)
            c.dma[i]=get<uint64_t>(reinterpret_cast<void *>(c.queue[i]),0x48);
    }
    __atomic_add_fetch(&live,1,__ATOMIC_RELEASE);summary(c,Begin);return slot;
}
void endFlush(int token) {
    if(token<0)return;Lock lock;if(!lock.held)return;
    auto &c=contexts[token];if(c.thread!=thread() || c.phase!=1)return;
    c.phase=2;dump(c);summary(c,FlushReturned);
}
int enterSync(const void *wlc,uint32_t bitmap,uint32_t mode,const void *owner) {
    Lock lock;if(!lock.held)return -1;int found=-1;
    for(unsigned i=0;i<Contexts;++i) {
        const auto &c=contexts[i];
        if(c.phase==2 && c.thread==thread() && c.owner==reinterpret_cast<uint64_t>(owner) &&
           c.wlc==reinterpret_cast<uint64_t>(wlc) && c.bitmap==bitmap) {
            if(found>=0){coverage(SyncUnmatched);return -1;}found=int(i);
        }
    }
    if(found<0){coverage(SyncUnmatched);return -1;}
    auto &c=contexts[found];c.phase=3;c.mode=mode;summary(c,SyncEntered);return found;
}
void leaveSync(int token) {
    if(token<0)return;Lock lock;if(!lock.held)return;
    auto &c=contexts[token];if(c.thread!=thread() || c.phase!=3)return;
    summary(c,SyncLeft);c.phase=0;__atomic_sub_fetch(&live,1,__ATOMIC_RELEASE);
}
void read(const void *owner,const void *reg,uint32_t size,uint32_t result,uint64_t caller) {
    if(!__atomic_load_n(&live,__ATOMIC_ACQUIRE))return;
    Lock lock;if(!lock.held)return;
    const auto tid=thread(),osh=reinterpret_cast<uint64_t>(owner),address=reinterpret_cast<uint64_t>(reg);
    for(auto &c:contexts)if(c.phase==1 && c.owner==osh) {
        uint32_t region=0;
        if(size==4) {
            if(address==c.regs+0x150)region=Channel;
            else if(address==c.regs+0x160 && caller==0x108624) {
                region=ObjectAddress;if(c.thread==tid)c.objectAddress=result;
            }
            else if(address==c.regs+0x120)region=MacControl;
            else if(address==c.regs+0x128)region=MacStatus;
            for(unsigned i=0;i<6;++i)if(c.dma[i]) {
                if(address==c.dma[i])region=DmaControl;
                if(address==c.dma[i]+0x10)region=DmaStatus0;
            }
        }else if(size==2 && address==c.regs+0x166 && caller==0x10863f &&
                 c.objectAddress==0x1001f)region=ObjectData;
        if(region) {
            if(c.thread!=tid){++c.crossThread;continue;}
            auto e=event(TxQuiescence); // Clock only; no emission for each polling read.
            c.bank.read(address,caller,size,region,result,++c.ordinal,e.timeNS);
        }
    }
}
void control(const void *di,uint32_t operation,bool returned) {
    if(!__atomic_load_n(&live,__ATOMIC_ACQUIRE))return;
    Lock lock;if(!lock.held)return;
    for(auto &c:contexts)if(c.phase)for(unsigned i=0;i<6;++i)if(c.queue[i]==reinterpret_cast<uint64_t>(di)) {
        ++c.operations;c.lastControl=++c.ordinal;if(c.thread!=thread())++c.crossThread;
        auto e=event(TxQuiescence,di,nullptr,reinterpret_cast<void *>(c.owner),ControlActivity);
        e.payload[0]=c.id;e.payload[1]=operation;e.payload[2]=returned;e.payload[3]=c.phase;
        e.payload[4]=c.ordinal;e.payload[5]=c.thread;e.payload[6]=c.crossThread;send(e);break;
    }
}
void reclaimed(const void *di,const void *packet,uint64_t serial,uint32_t range,uint64_t caller) {
    if(range!=1 || !serial)return;Lock lock;if(!lock.held)return;
    for(auto &c:contexts)if(c.phase==3 && c.thread==thread())
        for(unsigned i=0;i<6;++i)if(c.queue[i]==reinterpret_cast<uint64_t>(di)) {
            // Repeat compact, coalesced hardware evidence with this tracked return.
            // Missing/overflowed observations remain UNKNOWN, never permission.
            dump(c);summary(c,Reclaimed,di,packet,serial,caller,range);return;
        }
    coverage(ReclaimUnmatched);
}
void fatal(const void *owner) {
    if(!__atomic_load_n(&live,__ATOMIC_ACQUIRE))return;
    Lock lock;if(!lock.held)return;
    for(auto &c:contexts)if(c.phase && c.owner==reinterpret_cast<uint64_t>(owner)) {
        ++c.fatals;summary(c,NativeFatal);
    }
}
} }
