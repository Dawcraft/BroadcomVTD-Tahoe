#pragma once
#include <stdint.h>

namespace bvp {
constexpr uint32_t Capacity = 32768;
// 8/9 belong to rejected experiments, not compatibility inputs to this branch.
constexpr uint32_t FormatVersion = 13;
constexpr uint32_t FirstFailureEntries = 1;
constexpr uint32_t TxSnapshotEntries = 65; // 64 slots plus persistent halt/cleanup totals
constexpr uint32_t RxSnapshotEntries = 1025; // 1024 slots plus total counters
enum EventType : uint32_t {
    Bound = 1, StartResult, Stopped, OslBound, OslDetached,
    MapResult, TxEnter, TxExit, Descriptor, ReclaimEnter, ReclaimExit,
    FreeEnter, FreeExit, SuspendEnter, SuspendExit, ResetEnter, ResetExit,
    FifoBinding, FlushEnter, FlushExit, FatalEnter, FatalExit,
    PoolBound, PacketBound, SkyPrepareEnter, SkyPrepareExit,
    SkyCompleteEnter, SkyCompleteExit, SkyCopyEnter, SkyCopyExit,
    SkyDequeue, SkyEnqueue, Limitation, Acquisition,
    MapperReady, MapperPrepared, MapperSubmitted, MapperCompleted,
    MapperQuarantine, MapperRejected, MapperHalted, MapperInventory,
    TxInitEnter, TxInitExit, InvalidRegister, MapperSelection,
    RxFillEnter, RxFillExit, RxMapOriginal, RxDescriptor,
    RxReclaimEnter, RxReclaimExit, RxStatusRead, RxObservationLimit,
    RxMode, RxMapperPrepared, RxMapperMapped, RxMapperCompleted,
    RxMapperQuarantine, RxMapperRejected, RxMapperHalted, RxMapperInventory,
    RxResetEnter, RxResetExit, RxResetStatus, RxResetCompleted,
    RxInitEnter, RxInitExit, RxEnableEnter, RxEnableExit, RxCleanupDenied,
    TxSoftwareDetached, TxResetStatus, TxResetCompleted, TxCleanupDenied, TxCleanupMode, TxReclaimNotified,
    TxPacketShape, TxPacketMode,
    // Footer only; no new AirPortBrcmNIC hook or register interpretation.
    FirstFailure = 90, NativeTxStatusData, TxStatusWordMode, NativeReadResult, ReadPolicyMode,
    MapperNoCredit, MapperAdmissionResumed, MapperCreditReturned,
    // Additive format-13 event IDs: layout/footer/first-failure unchanged.
    TxQualification, TxQualificationSpan, TxDisposition, TxQuiescence, TxPrivate, RuntimeModeSelected
};
// No packet bytes. payload[] holds only addresses, lengths, indices and results.
struct Event {
    uint64_t sequence, timeNS, thread, object, packet, auxiliary;
    uint32_t type, flags;
    uint64_t payload[18];
};
static_assert(sizeof(Event) == 200, "wire format");
struct Header {
    uint64_t magic;
    uint32_t version, eventSize, capacity, gateStatus;
    uint64_t next, busyDrops, tableDrops, stopAfter, provider, controller;
    uint8_t uuid[16];
};
static_assert(sizeof(Header) == 88, "wire format");
} // namespace bvp
