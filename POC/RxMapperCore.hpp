#pragma once
#include "MapperCore.hpp"

namespace bvp { namespace rx {
constexpr unsigned Capacity=1024;
struct Mapping : Lifetime {
    IOMemoryDescriptor *descriptor=nullptr;
    IOAddressRange range {};
    uint64_t address=0, mapRecord=0, startIndex=0;
    uint32_t prepared=0, consumed=0, reported=0;
    uint64_t resetEpoch=0, resetThread=0;
    uint32_t resetAcknowledged=0;
};
void enable(bool);
bool enabled();
bool halted();
void halt(uint32_t);
Mapping *find(uint64_t packet);
Mapping *reserve(uint64_t owner,uint64_t queue,uint64_t packet);
bool prepare(Mapping &,IOMapper *,uint64_t virtualAddress,uint32_t length);
bool abortUnpublished(Mapping &);
bool completeNormally(Mapping &);
bool publish(Mapping &,uint64_t mapRecord,uint32_t startIndex);
void quarantine(Mapping &,uint32_t);
void quarantineQueue(uint64_t,uint32_t);
void quarantineOwner(uint64_t,uint32_t);
void invalidateQueue(uint64_t);
uint64_t beginReset(uint64_t queue,uint64_t thread);
unsigned acknowledgeReset(uint64_t queue,uint64_t thread,uint64_t epoch,bool disabled);
bool completeResetReclaim(Mapping &,uint64_t thread,bool softwareDetached);
void snapshot(unsigned,Event &);
} }
