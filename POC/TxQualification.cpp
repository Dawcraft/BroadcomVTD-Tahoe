#include "TxQualification.hpp"
#include "TxDisposition.hpp"
#include "Trace.hpp"
#ifdef BVT_HOST_TEST
#include <cstring>
#else
#include <libkern/libkern.h>
#include <kern/thread.h>
#endif

namespace bvp { namespace qual {
// Exact native PCs are observation labels, never cleanup policy. ABI gate
// windows bind these and the new wrapper to the same expected Mach-O.
constexpr uint64_t FreePC=0x2c2f43;
static bool syncPC(uint64_t pc) { return pc==0x14709f || pc==0x1471a7 || pc==0x14720c; }
template<typename T> static T read(const void *p,uint64_t off) {
    T v; memcpy(&v,static_cast<const uint8_t *>(p)+off,sizeof(v));return v;
}
static Ring ring(const void *di) {
    Ring r;r.queue=reinterpret_cast<uint64_t>(di);
    r.owner=read<uint64_t>(di,0x30);r.maps=read<uint64_t>(di,0x80);
    r.packets=read<uint64_t>(di,0x70);r.descs=read<uint64_t>(di,0x58);
    r.count=read<uint16_t>(di,0x6a);r.in=read<uint16_t>(di,0x6c);r.out=read<uint16_t>(di,0x6e);
    r.control=read<uint32_t>(di,0x0c);return r;
}
static MapView view(const Mapping *m) {
    MapView v;if (!m) return v;
    v.serial=m->serial;v.packet=m->packet;v.owner=m->owner;v.queue=m->queue;
    v.start=m->startIndex;v.end=m->endIndex;v.record=m->mapRecord;
    v.state=uint32_t(stateOf(*m));v.consumed=m->consumed;v.prepared=m->prepared;return v;
}
static void slots(const Ring &r,uint32_t span,Slots &s) {
    // CPU packet/descriptor tables only; NEVER reads MMIO or packet contents.
    if (!validRing(r) || !r.packets || !r.descs || !span || span>SpanLimit) return;
    s.count=span;
    for (unsigned j=0;j<span;++j) {
        const auto index=(r.in+j)&(r.count-1);
        s.packet[j]=read<uint64_t>(reinterpret_cast<void *>(r.packets),index*8);
        s.low[j]=read<uint32_t>(reinterpret_cast<void *>(r.descs),index*16+8);
        s.high[j]=read<uint32_t>(reinterpret_cast<void *>(r.descs),index*16+12);
    }
}
struct Context { uint32_t state=0;uint64_t thread=0,wlc=0,owner=0,id=0;uint32_t bitmap=0,mode=0; };
static Context contexts[16];
struct Observation {
    uint32_t busy=0,range=0,mode=0,contextState=0;
    uint64_t id=0,caller=0,entryMask=0,sync=0,wlc=0,result=0,actualSerial=0,preSerial=0;
    bool actualCaller=false,actualCandidateMatches=false;
    Ring before,after;Slots pre,post;MapView candidate,mapping;
};
static Observation scratch[ScratchCount];
struct Watch { uint32_t busy=0;uint64_t serial=0,id=0,time=0;bool resetSeen=false; };
static Watch watches[MappingCapacity];
static uint64_t attempts,trackedRecords,emptyRecords,contextRecords,coverageDrops,lifecycleRecords;
static bool take(uint32_t &s) { uint32_t z=0;return __atomic_compare_exchange_n(&s,&z,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED); }
static void give(uint32_t &s) { __atomic_store_n(&s,0,__ATOMIC_RELEASE); }
static void coverage(uint32_t why) {
    auto n=__atomic_add_fetch(&coverageDrops,1,__ATOMIC_RELAXED);
    if (n && !(n&(n-1))) {auto e=event(TxQualification,nullptr,nullptr,nullptr,Coverage);e.payload[0]=n;e.payload[1]=why;emit(e);}
}
int enterSync(const void *wlc,uint32_t bitmap,uint32_t mode,const void *owner) {
    for (unsigned i=0;i<16;++i) if (take(contexts[i].state)) {
        auto &c=contexts[i];const auto id=__atomic_add_fetch(&contextRecords,1,__ATOMIC_RELAXED);
        __atomic_store_n(&c.thread,reinterpret_cast<uint64_t>(current_thread()),__ATOMIC_RELAXED);
        __atomic_store_n(&c.wlc,reinterpret_cast<uint64_t>(wlc),__ATOMIC_RELAXED);
        __atomic_store_n(&c.owner,reinterpret_cast<uint64_t>(owner),__ATOMIC_RELAXED);
        __atomic_store_n(&c.bitmap,bitmap,__ATOMIC_RELAXED);__atomic_store_n(&c.mode,mode&255,__ATOMIC_RELAXED);
        __atomic_store_n(&c.id,id,__ATOMIC_RELAXED);
        __atomic_store_n(&c.state,2,__ATOMIC_RELEASE);
        if (id<=RecordLimit) {auto e=event(TxQualification,wlc,nullptr,owner,SyncContext);
            e.payload[0]=id;e.payload[1]=bitmap;e.payload[2]=mode&255;emit(e);}
        return int(i);
    }
    coverage(1);return -1; // Original sync still executes normally.
}
void leaveSync(int i) {if (i>=0) give(contexts[i].state);}
static void context(Observation &o) {
    const auto thread=reinterpret_cast<uint64_t>(current_thread());
    for (auto &c:contexts) if (__atomic_load_n(&c.state,__ATOMIC_ACQUIRE)==2) {
        auto id=__atomic_load_n(&c.id,__ATOMIC_RELAXED);
        auto ct=__atomic_load_n(&c.thread,__ATOMIC_RELAXED),owner=__atomic_load_n(&c.owner,__ATOMIC_RELAXED);
        auto mode=__atomic_load_n(&c.mode,__ATOMIC_RELAXED);auto wlc=__atomic_load_n(&c.wlc,__ATOMIC_RELAXED);
        if (ct!=thread || owner!=o.before.owner) continue;
        if (__atomic_load_n(&c.state,__ATOMIC_ACQUIRE)!=2 || __atomic_load_n(&c.id,__ATOMIC_RELAXED)!=id || o.contextState) {
            o.contextState=2;return; // Reuse/nesting uncertainty, never a policy input.
        }
        o.contextState=1;o.sync=id;o.mode=mode;o.wlc=wlc;
    }
}
Observation *begin(const void *di,uint32_t range,uint64_t caller,bool actualCaller,const Mapping *actualCandidate) {
    if (!di || range!=1) return nullptr; // No normal completion hot-path census.
    if (__atomic_load_n(&trackedRecords,__ATOMIC_RELAXED)>=RecordLimit) {coverage(2);return nullptr;}
    Observation *p=nullptr;
    for (auto &s:scratch) if (take(s.busy)) {p=&s;break;}
    if (!p) {coverage(3);return nullptr;}
    // Preserve claimed busy word; initialized storage is entirely diagnostic.
    memset(reinterpret_cast<uint8_t *>(p)+sizeof(p->busy),0,sizeof(*p)-sizeof(p->busy));
    auto &o=*p;o.range=range;o.caller=caller;o.actualCaller=actualCaller;
    o.id=__atomic_add_fetch(&attempts,1,__ATOMIC_RELAXED);o.before=ring(di);
    o.actualSerial=actualCandidate?actualCandidate->serial:0;
    o.entryMask=entryReasons(txCleanupEnabled(),caller,FreePC,range,o.before.control);
    context(o);
    auto &r=o.before;
    if (validRing(r) && r.in!=r.out && r.maps && r.packets && r.descs) {
        r.nativeSpan=read<uint32_t>(reinterpret_cast<void *>(r.maps),uint64_t(r.in)*0x70+0xc);
        // Only the first native span is sampled. If the getter skips empty
        // spans the returned mapping comparison explicitly reports mismatch.
        if (r.in!=r.out && r.nativeSpan<=((r.out-r.in)&(r.count-1))) slots(r,r.nativeSpan,o.pre);
    }
    if (actualCandidate) o.candidate=view(actualCandidate);
    else if (o.pre.count) o.candidate=view(findMapping(o.pre.packet[o.pre.count-1]));
    o.preSerial=o.candidate.serial;return p;
}
void returned(Observation *p,const void *result,const Mapping *m,const Mapping *actualCandidate) {
    if (!p) return;
    auto &o=*p;o.result=reinterpret_cast<uint64_t>(result);o.mapping=view(m);
    o.actualCandidateMatches=m && m==actualCandidate;
    o.after=ring(reinterpret_cast<void *>(o.before.queue));
    if (o.before.count==o.after.count && o.before.packets==o.after.packets && o.before.descs==o.after.descs)
        slots(o.before,o.pre.count,o.post); // Same original span, not advanced txin.
    // BVD-BEGIN: independent branch inputs; no return/disposal permission.
    if (m && o.contextState==1 && syncPC(o.caller)) dispo::observe(o.id,*m,o.wlc,o.mode);
    // BVD-END
}
static Event record(const Observation &o,uint32_t stage) {
    auto e=event(TxQualification,reinterpret_cast<void *>(o.before.queue),reinterpret_cast<void *>(o.result),reinterpret_cast<void *>(o.before.owner),stage);
    e.payload[0]=o.id;e.payload[1]=o.mapping.serial;return e;
}
void finish(Observation *p,const Mapping *m,bool noteReached,bool actualDetached,bool nativeReturned) {
    if (!p) return;
    auto &o=*p;
    auto ordinal=o.mapping.packet?__atomic_add_fetch(&trackedRecords,1,__ATOMIC_RELAXED):__atomic_add_fetch(&emptyRecords,1,__ATOMIC_RELAXED);
    if (ordinal>(o.mapping.packet?RecordLimit:EmptyLimit)) {give(o.busy);coverage(4);return;}
    auto e=record(o,Entry);auto &a=o.before;auto &b=o.after;auto &v=o.mapping;
    e.payload[2]=o.caller;e.payload[3]=o.range;e.payload[4]=a.control;e.payload[5]=o.actualCaller;e.payload[6]=o.entryMask;
    e.payload[7]=a.count;e.payload[8]=a.in;e.payload[9]=a.out;e.payload[10]=a.maps;e.payload[11]=a.packets;e.payload[12]=a.descs;
    e.payload[13]=a.owner;e.payload[14]=o.sync;e.payload[15]=o.mode;e.payload[16]=o.wlc;e.payload[17]=o.contextState;emit(e);
    e=record(o,PreMapping);const auto &c=o.candidate;
    e.payload[2]=c.serial;e.payload[3]=c.packet;e.payload[4]=c.owner;e.payload[5]=c.queue;
    e.payload[6]=c.start;e.payload[7]=c.end;e.payload[8]=c.record;e.payload[9]=c.state;
    e.payload[10]=c.consumed;e.payload[11]=c.prepared;e.payload[12]=preReasons(a,c,o.pre);emit(e);
    e=record(o,Candidate);e.payload[2]=o.actualSerial;e.payload[3]=o.preSerial;
    e.payload[4]=preReasons(a,v,o.pre);e.payload[5]=a.nativeSpan;e.payload[6]=v.start;e.payload[7]=v.end;
    e.payload[8]=v.record;e.payload[9]=v.packet;e.payload[10]=v.owner;e.payload[11]=v.queue;
    e.payload[12]=validRing(a)?((v.end-a.in)&(a.count-1)):0;e.payload[13]=o.pre.count;
    e.payload[14]=v.state;e.payload[15]=v.consumed;e.payload[16]=v.prepared;
    e.payload[17]=o.preSerial && o.preSerial==v.serial;emit(e);
    e=record(o,Post);e.payload[2]=postReasons(a,b,v,o.post,o.result,o.actualCandidateMatches);
    e.payload[3]=o.result;e.payload[4]=b.count;e.payload[5]=b.in;e.payload[6]=b.out;e.payload[7]=b.maps;
    e.payload[8]=b.packets;e.payload[9]=b.descs;e.payload[10]=b.owner;e.payload[11]=o.post.count;
    e.payload[12]=o.actualCandidateMatches;emit(e);
    for (unsigned i=0;i<o.pre.count;++i) {
        e=record(o,0);e.type=TxQualificationSpan;e.payload[2]=i;e.payload[3]=o.pre.packet[i];e.payload[4]=o.post.packet[i];
        e.payload[5]=o.pre.low[i];e.payload[6]=o.pre.high[i];e.payload[7]=o.post.low[i];e.payload[8]=o.post.high[i];
        e.payload[9]=(a.in+i)&(a.count-1);e.payload[10]=i<o.post.count;emit(e);
    }
    e=record(o,Outcome);e.payload[2]=noteReached;e.payload[3]=actualDetached;
    e.payload[4]=m?m->detached:0;e.payload[5]=m?m->notified:0;e.payload[6]=m?m->cleanupBlocked:0;
    e.payload[7]=m?m->firstReason:0;e.payload[8]=m && !nativeReturned;
    e.payload[9]=disposition(o.caller,FreePC,a.control,syncPC(o.caller),o.contextState==1,o.mode);
    e.payload[10]=m?uint32_t(stateOf(*m)):0;e.payload[11]=ordinal;emit(e);
    if (m) for (unsigned i=0;i<MappingCapacity;++i) if (txMappingAt(i)==m && take(watches[i].busy)) {
        watches[i].serial=m->serial;watches[i].id=o.id;watches[i].time=e.timeNS;watches[i].resetSeen=false;
        give(watches[i].busy);break;
    }
    give(o.busy);
}
void reset(uint64_t queue,uint64_t thread,uint64_t epoch,bool disabled) {
    for (unsigned i=0;i<MappingCapacity;++i) {
        auto m=txMappingAt(i);auto &w=watches[i];
        if (!take(w.busy)) {coverage(5);continue;}
        if (w.serial && !w.resetSeen && m->packet && m->queue==queue && m->serial==w.serial) {
            auto e=event(TxQualification,reinterpret_cast<void *>(queue),reinterpret_cast<void *>(m->packet),reinterpret_cast<void *>(m->owner),ResetOpportunity);
            e.payload[0]=w.id;e.payload[1]=m->serial;e.payload[2]=epoch;e.payload[3]=disabled;
            e.payload[4]=m->resetEpoch;e.payload[5]=m->detached;e.payload[6]=m->detachedThread;
            e.payload[7]=m->detachedCaller;e.payload[8]=m->notified;e.payload[9]=m->cleanupBlocked;
            e.payload[10]=m->consumed;e.payload[11]=m->prepared;e.payload[12]=uint32_t(stateOf(*m));
            e.payload[13]=mappingsHalted();e.payload[14]=txCleanupEnabled();e.payload[15]=thread;
            e.payload[16]=e.timeNS-w.time;e.payload[17]=m->firstReason;emit(e);w.resetSeen=true;
        }
        give(w.busy);
    }
}
void lifecycle(const Mapping &m,Action action) {
    auto n=__atomic_add_fetch(&lifecycleRecords,1,__ATOMIC_RELAXED);
    if (n>RecordLimit) return;
    auto e=event(TxQualification,reinterpret_cast<void *>(m.queue),reinterpret_cast<void *>(m.packet),reinterpret_cast<void *>(m.owner),Lifecycle);
    e.payload[1]=m.serial;e.payload[2]=action;e.payload[3]=uint32_t(stateOf(m));e.payload[4]=poisoned(m);emit(e);
    // An attempted wrapped Free/Tx is NOT proof that the original ran.
}
} }
