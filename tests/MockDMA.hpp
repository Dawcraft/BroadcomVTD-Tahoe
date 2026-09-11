#pragma once
// Host-only backend: tests cleanup policy, NOT real IOMMU behavior.
#include <stdint.h>
#include <cstddef>
#include <cstdlib>
static bool failPrivateAllocation;
static void *IOMallocAligned(size_t size,size_t align) {
    if(failPrivateAllocation)return nullptr;
    void *p=nullptr;return posix_memalign(&p,align,size)==0?p:nullptr;
}
using IOByteCount=uint64_t;
using IOReturn=int;
struct IOAddressRange {uint64_t address,length;};
struct IOMapper {};
static constexpr int kIOReturnSuccess=0,kIODirectionOut=2,kIODirectionPrepareNoFault=8;
static constexpr int kIODirectionInOut=3;
static constexpr int kIOMemoryTypeVirtual=16,kIOMemoryAsReference=256;
static constexpr void *kernel_task=nullptr;
struct IOMemoryDescriptor {
    static unsigned prepares,completes,releases,live;
    static bool failCreate,failPrepare,failComplete,failSegment;
    static bool segmentGap;
    static bool uniqueAddresses;
    static uint64_t addressCounter;
    static unsigned lastPrepare,lastComplete;
    IOAddressRange *ranges;
    uint64_t busBase=0x100000;
    static IOMemoryDescriptor *withOptions(void *r,unsigned,unsigned,void *,unsigned,IOMapper *mapper) {
        if (failCreate || !mapper) return nullptr;
        ++live;auto d=new IOMemoryDescriptor {static_cast<IOAddressRange *>(r)};
        if(uniqueAddresses)d->busBase=(++addressCounter)*0x100000;
        return d;
    }
    int prepare(unsigned direction) {lastPrepare=direction;++prepares;return failPrepare?-1:0;}
    uint64_t getPhysicalSegment(uint64_t offset,uint64_t *length,unsigned) {
        *length=4096;return failSegment?0:busBase+offset+(segmentGap && offset>=4096?4096:0);
    }
    int complete(unsigned direction) {lastComplete=direction;++completes;return failComplete?-1:0;}
    void release() {++releases;--live;delete this;}
};
unsigned IOMemoryDescriptor::prepares=0,IOMemoryDescriptor::completes=0,IOMemoryDescriptor::releases=0,IOMemoryDescriptor::live=0;
bool IOMemoryDescriptor::failCreate=false,IOMemoryDescriptor::failPrepare=false,IOMemoryDescriptor::failComplete=false,IOMemoryDescriptor::failSegment=false;
bool IOMemoryDescriptor::segmentGap=false;
bool IOMemoryDescriptor::uniqueAddresses=false;
uint64_t IOMemoryDescriptor::addressCounter=0;
unsigned IOMemoryDescriptor::lastPrepare=0,IOMemoryDescriptor::lastComplete=0;
