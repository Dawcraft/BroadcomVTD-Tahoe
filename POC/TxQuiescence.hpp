#pragma once
#include <stdint.h>

namespace bvp { namespace quiet {
// Observations ONLY. No value in this namespace is an ownership certificate.
constexpr unsigned Contexts=4, Samples=96, Transactions=64, Records=8192;
enum Stage : uint32_t { Begin=1, ReadSample, FlushReturned, SyncEntered,
    Reclaimed, ControlActivity, SyncLeft, Coverage, NativeFatal };
enum Region : uint32_t { Channel=1, DmaControl, DmaStatus0, ObjectAddress,
    ObjectData, MacControl, MacStatus };
enum Limit : uint32_t { Busy=1, ContextFull, SampleFull, TransactionLimit,
    RecordLimit, SyncUnmatched, ReclaimUnmatched, ReplacedContext };
struct Sample {
    uint64_t reg=0,caller=0,firstOrdinal=0,lastOrdinal=0,firstNS=0,lastNS=0;
    uint32_t region=0,size=0,first=0,last=0,count=0,changes=0,seen=0;
};
// 'seen' retains raw-value classes, not success/failure classifications.
// 1=zero, 2=all-ones at read width, 4=status0 high nibble 2, 8=other.
inline void add(Sample &s,uint32_t value,uint64_t ordinal,uint64_t ns) {
    if (!s.count) { s.first=value;s.firstOrdinal=ordinal;s.firstNS=ns; }
    else if (s.last!=value) ++s.changes;
    s.last=value;s.lastOrdinal=ordinal;s.lastNS=ns;++s.count;
    s.seen|=!value?1U:(value==(s.size==2?0xffffU:0xffffffffU)?2U:
        (s.region==DmaStatus0 && (value&0xf0000000U)==0x20000000U?4U:8U));
}
struct Bank {
    Sample samples[Samples] {};
    uint32_t used=0,overflow=0;
    void read(uint64_t reg,uint64_t caller,uint32_t size,uint32_t region,
              uint32_t value,uint64_t ordinal,uint64_t ns) {
        for (unsigned i=0;i<used;++i) {
            auto &s=samples[i];
            if (s.reg==reg && s.caller==caller && s.size==size) {
                add(s,value,ordinal,ns);return;
            }
        }
        if (used==Samples) { ++overflow;return; }
        auto &s=samples[used++];s.reg=reg;s.caller=caller;s.size=size;s.region=region;
        add(s,value,ordinal,ns);
    }
};
int beginFlush(const void *hw,uint32_t bitmap);
void endFlush(int token);
int enterSync(const void *wlc,uint32_t bitmap,uint32_t mode,const void *owner);
void leaveSync(int token);
void read(const void *owner,const void *reg,uint32_t size,uint32_t result,uint64_t caller);
void control(const void *di,uint32_t operation,bool returned);
void reclaimed(const void *di,const void *packet,uint64_t serial,uint32_t range,uint64_t caller);
void fatal(const void *owner);
} }
