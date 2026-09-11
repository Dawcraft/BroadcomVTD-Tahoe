#define BVT_HOST_TEST
#include "../POC/Trace.cpp"
#include "../POC/MapperCore.cpp"
#include "../POC/TxPacket.hpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace bvp;
    using namespace bvp::txpacket;
    View nodes[10] {};
    unsigned reads=0;
    auto read=[&](uint64_t id) { assert(id && id<=10); ++reads; return nodes[id-1]; };
    nodes[0]={0x100123,80,256,2,0};
    nodes[1]={0x300ff0,1460,2048,0,0};
    auto baseline=collect(1,false,read);
    assert(baseline.rejected==ChainDisabled && reads==1); // No next-node access in control mode.
    auto p=collect(1,true,read);
    assert(!p.rejected && p.count==2 && p.pages==3 && p.total==1540 && p.head.length==80);
    assert(p.ranges[0].address==0x100123 && p.ranges[1].address==0x300ff0);
    IOMapper mapper;
    auto m=reserveMapping(100,200,1); assert(m);
    m->inputLength=uint32_t(p.head.length);
    assert(prepareMapping(*m,&mapper,p.ranges,p.count));
    assert(m->bytes==1540 && m->inputLength==80 && m->count==3);
    assert(m->pages[0].address==0x100000 && m->pages[1].address==0x300000 && m->pages[2].address==0x301000);
    assert(m->segments[0].address==0x100123 && m->segments[0].length==80);
    assert(m->segments[1].address==0x101ff0 && m->segments[1].length==16);
    assert(m->segments[2].address==0x102000 && m->segments[2].length==1444);
    // Exercise the actual OSL adapter, not a second test implementation.
    uint8_t record[0x70]; memset(record,0xaa,sizeof(record)); encode(record,*m);
    uint32_t head,count; memcpy(&head,record+8,4); memcpy(&count,record+12,4);
    assert(head==80 && count==3); // Never publish 1540 as osl_dma_map's head-size field.
    uint64_t total=0;
    for(unsigned i=0;i<count;++i) {
        uint64_t address;uint32_t length;
        memcpy(&address,record+16+12*i,8);memcpy(&length,record+24+12*i,4);
        assert(address==m->segments[i].address && length==m->segments[i].length);total+=length;
    }
    assert(total==1540 && record[0]==0 && record[111]==0);
    assert(transition(*m,MapState::Prepared,MapState::Submitting));m->consumed=1;
    assert(transition(*m,MapState::Submitting,MapState::Owned));
    assert(!findMapping(2)); // One submission/owner for the complete original head chain.
    assert(finishNormally(*m) && !findMapping(1) && IOMemoryDescriptor::live==0);
    assert(IOMemoryDescriptor::lastComplete==kIODirectionOut);
    m=reserveMapping(100,200,1); m->inputLength=80;
    assert(prepareMapping(*m,&mapper,p.ranges,p.count));
    assert(transition(*m,MapState::Prepared,MapState::Submitting));m->consumed=1;
    assert(transition(*m,MapState::Submitting,MapState::Owned));markQuarantine(*m,1);
    auto releases=IOMemoryDescriptor::releases;
    assert(!finishNormally(*m) && !abortUnpublished(*m));
    assert(findMapping(1)==m && m->descriptor && IOMemoryDescriptor::releases==releases);
    assert(nodes[0].next==2 && nodes[1].next==0); // No relinking/coalescing/free.
    nodes[0].next=0; reads=0;
    auto a=collect(1,false,read),b=collect(1,true,read);
    assert(!a.rejected && !b.rejected && a.total==b.total && a.pages==b.pages);
    nodes[0].nextPacket=3; assert(collect(1,true,read).rejected&PacketQueue); nodes[0].nextPacket=0;
    nodes[0].data=0; assert(collect(1,true,read).rejected&NoData);
    assert(!collect(1,false,read).rejected); // Original prepare failure, not a new baseline admission rule.
    nodes[0]={0xfffffffffffffff0ULL,32,256,0,0};assert(collect(1,true,read).rejected&AddressOverflow);
    nodes[0]={0x100000,0,256,0,0};assert(collect(1,true,read).rejected&EmptyFragment);
    nodes[0].length=257;assert(collect(1,true,read).rejected&LengthExceedsBacking);
    nodes[0].maximum=MaxPacketBytes+1;assert(collect(1,true,read).rejected&AllocationLimit);
    nodes[0]={0x100000,1,256,1,0};assert(collect(1,true,read).rejected&Cycle);
    for(unsigned i=0;i<9;++i) nodes[i]={uint64_t(0x100000+i*0x2000),1,256,i==8?0:i+2,0};
    reads=0;assert(collect(1,true,read).rejected&FragmentLimit);assert(reads==8);
    nodes[7].next=0;assert(!collect(1,true,read).rejected); // Exact eight-range/page limit.
    nodes[0]={0x100001,32768,32768,0,0};assert(collect(1,true,read).rejected&PageLimit);
    nodes[0]={0x100000,32768,32768,0,0};assert(!collect(1,true,read).rejected);
    nodes[0].next=2;assert(collect(1,true,read).rejected&LengthLimit);
    assert(collect(0,true,read).rejected==NoPacket);
    std::cout<<"PASS: explicit chain gate; bounded metadata traversal; exact whole-chain S/G/head-size ABI; noncontiguous virtual ranges/page splits; one head lifetime; normal completion; forced quarantine; range/page/length/queue/cycle rejection; linear baseline preserved\n";
}
