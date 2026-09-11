#pragma once
#include "Lifetime.hpp"
#include "PrivateTx.hpp"
#ifdef BVT_HOST_TEST
#include "../tests/MockDMA.hpp"
#else
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOMapper.h>
#endif

namespace bvp {
struct VirtualRange { uint64_t address,length; };
struct Segment { uint64_t address; uint32_t length; };
struct Mapping : Lifetime {
    IOMemoryDescriptor *descriptor=nullptr;
    IOAddressRange pages[MaxRanges] {};
    uint32_t offsets[MaxRanges] {}, lengths[MaxRanges] {};
    Segment segments[MaxRanges] {};
    uint32_t count=0, bytes=0, consumed=0, prepared=0, reported=0;
    // Frontend ABI length: osl_dma_map receives mbuf_len(head), even when the
    // cursor/explicit mapper supplies segments for the entire packet chain.
    uint32_t inputLength=0;
    uint64_t thread=0, mapRecord=0, startIndex=0, endIndex=0;
    uint64_t detachedThread=0, detachedCaller=0, resetEpoch=0;
    uint32_t detached=0, cleanupBlocked=0, firstReason=0, notified=0;
    privateTx::Record privateRecord {};
};
Mapping *reserveMapping(uint64_t owner,uint64_t queue,uint64_t packet);
Mapping *findMapping(uint64_t packet);
bool prepareMapping(Mapping &,IOMapper *,const VirtualRange *,unsigned count);
bool preparePrivateMapping(Mapping &,IOMapper *,const VirtualRange *,unsigned count,
                           const privateTx::Geometry &);
bool detachPrivateAssociation(Mapping &,bool exactSoftwareDetachment);
bool abortUnpublished(Mapping &); // No address was ever supplied to hardware.
bool finishNormally(Mapping &); // Only called for a validated hardware completion.
void markQuarantine(Mapping &,uint32_t reason);
void quarantineQueue(uint64_t queue,uint32_t reason);
void quarantineOwner(uint64_t owner,uint32_t reason);
bool mappingsHalted();
void haltMappings(uint32_t reason);
void noteTxSubmission(); // Bounded pressure telemetry only; never changes ownership.
void mappingDiagnostics(); // Metadata snapshot only; no mapper operation.
struct Event;
void snapshotMapping(unsigned index,Event &out);
void enableTxCleanup(bool);
bool txCleanupEnabled();
void noteTxDetached(Mapping &,uint64_t thread,uint64_t caller,bool proven);
uint64_t beginTxReset(uint64_t queue);
Mapping *txMappingAt(unsigned index);
// Caller holds exclusive admission, observes this reset's disabled status and
// validates the same live provider/ring. Free callback is the original frontend
// packet-release API, not a generic mbuf_free or a guessed ownership callback.
bool completeTxReset(Mapping &,uint64_t queue,uint64_t thread,uint64_t epoch,
                     bool disabled,void (*releasePacket)(uint64_t,uint64_t));
} // namespace bvp
