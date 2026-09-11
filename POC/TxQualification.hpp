#pragma once
#include "MapperCore.hpp"

namespace bvp { namespace qual {
// Metadata only. No predicate/result from this namespace grants authority.
enum Reason : uint64_t {
    CleanupOff=1ULL<<0, CallerMismatch=1ULL<<1, RangeMismatch=1ULL<<2,
    ControlFlag=1ULL<<3, RingCount=1ULL<<4, RingIndex=1ULL<<5,
    MissingVectors=1ULL<<6, NoMapping=1ULL<<7, QueueOwner=1ULL<<8,
    StartIndex=1ULL<<9, EndSpan=1ULL<<10, MapRecord=1ULL<<11,
    NativeSpan=1ULL<<12, PrePacketSlots=1ULL<<13, ReturnedMismatch=1ULL<<14,
    RingIdentity=1ULL<<15, PostIndex=1ULL<<16, PostPacketSlots=1ULL<<17,
    DescriptorClear=1ULL<<18, ObservationIncomplete=1ULL<<19
};
enum Stage : uint32_t { Entry=1, Candidate, Post, Outcome, ResetOpportunity,
    SyncContext, Coverage, Lifecycle, PreMapping };
enum Disposition : uint32_t { Unknown=0, FreeOwningCaller=1,
    SyncDisposeBranch=2, SyncRequeueCapable=3 };
enum Action : uint32_t { TxAttempt=1, FreeAttempt=2 };
constexpr unsigned SpanLimit=2*MaxRanges, RecordLimit=128, EmptyLimit=16, ScratchCount=16;
struct Ring {
    uint64_t queue=0,owner=0,maps=0,packets=0,descs=0;
    uint32_t count=0,in=0,out=0,control=0,nativeSpan=0;
};
struct MapView {
    uint64_t serial=0,packet=0,owner=0,queue=0,start=0,end=0,record=0;
    uint32_t state=0,consumed=0,prepared=0;
};
struct Slots {
    uint64_t packet[SpanLimit] {};
    uint32_t low[SpanLimit] {},high[SpanLimit] {},count=0;
};
inline bool validRing(const Ring &r) {
    return r.count && r.count<=4096 && !(r.count&(r.count-1)) && r.in<r.count;
}
inline uint64_t entryReasons(bool enabled,uint64_t caller,uint64_t expected,
                             uint32_t range,uint32_t control) {
    return (!enabled?CleanupOff:0ULL) | (caller!=expected?CallerMismatch:0ULL) |
        (range!=1?RangeMismatch:0ULL) | ((control&8)?ControlFlag:0ULL);
}
inline uint64_t preReasons(const Ring &r,const MapView &m,const Slots &s) {
    uint64_t why=0;
    if (!r.count || r.count>4096 || (r.count&(r.count-1))) why|=RingCount;
    if (r.in>=r.count) why|=RingIndex;
    if (!r.maps || !r.packets || !r.descs) why|=MissingVectors;
    if (!m.packet) return why|NoMapping;
    if (m.queue!=r.queue || m.owner!=r.owner) why|=QueueOwner;
    if (m.start!=r.in) why|=StartIndex;
    if (m.record!=r.maps+uint64_t(r.in)*0x70) why|=MapRecord;
    if (!validRing(r) || m.end>=r.count) return why|EndSpan;
    const auto span=(m.end-r.in)&(r.count-1);
    if (!span || span>SpanLimit) return why|EndSpan;
    if (r.nativeSpan!=span) why|=NativeSpan;
    if (s.count!=span) return why|ObservationIncomplete;
    for (unsigned j=0;j<span;++j)
        if (s.packet[j]!=(j+1==span?m.packet:0)) why|=PrePacketSlots;
    return why;
}
inline uint64_t postReasons(const Ring &a,const Ring &b,const MapView &m,
                           const Slots &s,uint64_t result,bool candidateMatches) {
    uint64_t why=0;
    if (!candidateMatches || !m.packet || m.packet!=result) why|=ReturnedMismatch;
    if (a.queue!=b.queue || a.owner!=b.owner || m.queue!=b.queue || m.owner!=b.owner) why|=QueueOwner;
    if (a.count!=b.count || a.maps!=b.maps || a.packets!=b.packets || a.descs!=b.descs) why|=RingIdentity;
    if (b.in!=m.end) why|=PostIndex;
    if (!validRing(a) || m.end>=a.count) return why|ObservationIncomplete;
    auto span=(m.end-a.in)&(a.count-1);
    if (!span || span>SpanLimit || s.count!=span) return why|ObservationIncomplete;
    for (unsigned j=0;j<span;++j) {
        if (s.packet[j]) why|=PostPacketSlots;
        if (s.low[j]!=0xdeadbeefU || s.high[j]!=0xdeadbeefU) why|=DescriptorClear;
    }
    return why;
}
inline Disposition disposition(uint64_t caller,uint64_t freePC,uint32_t control,
                               bool syncPC,bool contextKnown,uint32_t mode) {
    if (caller==freePC && !(control&8)) return FreeOwningCaller;
    if (syncPC && contextKnown) return (mode&255)==1?SyncDisposeBranch:SyncRequeueCapable;
    return Unknown;
}
struct Observation;
Observation *begin(const void *di,uint32_t range,uint64_t caller,bool actualCaller,
                   const Mapping *actualCandidate);
void returned(Observation *,const void *result,const Mapping *,const Mapping *actualCandidate);
void finish(Observation *,const Mapping *,bool noteReached,bool actualDetached,bool nativeReturned=false);
int enterSync(const void *wlc,uint32_t bitmap,uint32_t mode,const void *owner);
void leaveSync(int);
void reset(uint64_t queue,uint64_t thread,uint64_t epoch,bool disabled);
void lifecycle(const Mapping &,Action);
} }
