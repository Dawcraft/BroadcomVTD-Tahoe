#pragma once
#include "MapperCore.hpp"
#include <string.h>

namespace bvp { namespace txpacket {
// AirPortBrcmNIC packet frontend only. No packet bytes, coalescing, mutation,
// ownership transfer, mapper operations, or unbounded list traversal here.
enum Reject : uint64_t {
    Halted=1ULL<<0, NoMapper=1ULL<<1, NoPacket=1ULL<<2,
    RingCount=1ULL<<3, RingIndices=1ULL<<4, MissingVectors=1ULL<<5,
    AddressOffset=1ULL<<6, ChainDisabled=1ULL<<7, PacketQueue=1ULL<<8,
    EmptyFragment=1ULL<<9, LengthLimit=1ULL<<10, AllocationLimit=1ULL<<11,
    NoData=1ULL<<12, AddressOverflow=1ULL<<13, FragmentLimit=1ULL<<14,
    PageLimit=1ULL<<15, Cycle=1ULL<<16, LengthExceedsBacking=1ULL<<17
};
struct View {
    uint64_t data=0,length=0,maximum=0,next=0,nextPacket=0;
};
struct Plan {
    VirtualRange ranges[MaxRanges] {};
    uint64_t identities[MaxRanges] {};
    View head {};
    uint64_t rejected=0,total=0;
    uint32_t count=0,pages=0;
};
// Exactly the pinned OSL output ABI: head length at +8, whole-chain scatter /
// gather count at +12, then eight 12-byte {low, high, length} entries at +16.
inline void encode(void *record,const Mapping &m) {
    auto bytes=static_cast<uint8_t *>(record);
    memset(bytes,0,0x70);
    memcpy(bytes+8,&m.inputLength,4); memcpy(bytes+12,&m.count,4);
    for (unsigned i=0;i<m.count;++i) {
        memcpy(bytes+0x10+i*12,&m.segments[i].address,8);
        memcpy(bytes+0x18+i*12,&m.segments[i].length,4);
    }
}
// Reader supplies metadata for a live, exclusively driver-owned mbuf chain.
// Baseline mode reads ONLY the head and preserves 0.2.2's admission conditions;
// prepareMapping continues to reject an invalid data range before publication.
template <typename Reader> Plan collect(uint64_t packet,bool chains,Reader read) {
    Plan p;
    if (!packet) { p.rejected=NoPacket; return p; }
    uint64_t current=packet;
    while (current) {
        if (p.count==MaxRanges) { p.rejected|=FragmentLimit; break; }
        for (unsigned i=0;i<p.count;++i)
            if (p.identities[i]==current) { p.rejected|=Cycle; return p; }
        View v=read(current);
        if (!p.count) p.head=v;
        if (!chains && v.next) p.rejected|=ChainDisabled;
        if (v.nextPacket) p.rejected|=PacketQueue;
        if (!v.length) p.rejected|=EmptyFragment;
        if (v.length>MaxPacketBytes || v.length>MaxPacketBytes-p.total) p.rejected|=LengthLimit;
        if (v.maximum>MaxPacketBytes) p.rejected|=AllocationLimit;
        if (chains) {
            if (v.length>v.maximum) p.rejected|=LengthExceedsBacking;
            if (!v.data) p.rejected|=NoData;
            if (v.data+v.length<v.data) p.rejected|=AddressOverflow;
        }
        if (p.rejected) break;
        p.identities[p.count]=current;
        p.ranges[p.count++]={v.data,v.length}; p.total+=v.length;
        p.pages+=uint32_t(((v.data&4095)+v.length+4095)/4096);
        if (chains && p.pages>MaxRanges) { p.rejected|=PageLimit; break; }
        if (!chains) break;
        current=v.next;
    }
    return p;
}
} } // namespace bvp::txpacket
