#pragma once
#include "MapperCore.hpp"

namespace bvp { namespace dispo {
// Read-only branch INPUTS. These are not native execution/ownership results.
enum Recovery : uint32_t { RecoveryUnknown=0, NullScb, PrimarySpecial,
    AlternateSpecial, NativeRecoveryRequired };
enum Hint : uint32_t { Unknown=0, Mode1Disposal, NullRecoveryDisposal,
    TaggedDisposal, PrimaryMode2Disposal, Mode2FiltersRequired,
    RecoveryRequired, QueueUnavailable, QueueFullDisposal, RequeueEligibleSnapshot };
enum Coverage : uint32_t { TagValid=1, MainBandValid=2, OtherBandChecked=4,
    BssSlotObserved=8, QueueObserved=16, PriorityValid=32, StableAnchors=64 };
struct Tag {
    uint64_t cachedScb=0,priority=0;
    uint32_t flags1=0,flags7=0;
    int32_t bssIndex=-1;
};
struct Input {
    Tag tag;
    uint64_t wlc=0,primary=0,alternate=0,bss=0,queueInfo=0,queue=0;
    uint32_t mode=0,coverage=0,recovery=0,precedence=0,count=0,limit=0,queuePrecisions=0;
};
using ReadTag=bool (*)(uint64_t,Tag &);
inline Hint classify(const Input &s) {
    if (!(s.coverage&StableAnchors)) return Unknown;
    if (s.mode==1) return Mode1Disposal;
    if (!(s.coverage&TagValid)) return Unknown;
    // These imply a disposal branch IF native processing reaches the unchanged
    // sampled predicates. They do not imply native recovery has run or is pure.
    if (s.recovery==NullScb) return NullRecoveryDisposal;
    if (s.tag.flags7&0x10) return TaggedDisposal;
    if (s.recovery!=PrimarySpecial && s.recovery!=AlternateSpecial) return RecoveryRequired;
    if (s.mode==2) return s.recovery==PrimarySpecial?PrimaryMode2Disposal:Mode2FiltersRequired;
    if (!(s.coverage&QueueObserved) || !(s.coverage&PriorityValid)) return QueueUnavailable;
    return s.count>=s.limit?QueueFullDisposal:RequeueEligibleSnapshot;
}
void configure(ReadTag,const uint8_t *priorityMap);
// Only live wlc-owned metadata and packet tag are read. Cached SCB is NEVER
// dereferenced. No call to mutating recover_pkt_scb, no packet bytes logged.
Input inspect(uint64_t wlc,uint32_t mode,const Tag &,bool tagValid);
void observe(uint64_t id,const Mapping &,uint64_t wlc,uint32_t mode);
} }
