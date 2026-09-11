#pragma once
#include <stdint.h>

namespace bvp {
enum class MapperSource : uint32_t { Unavailable, DeviceSpecific, VerifiedSystemAppleVTD };
enum class RuntimeMode : uint32_t { Unselected, NativePassthrough, AppleVTDCorrectiveExperimental };
// No configuration-text heuristic and no acceptance of an arbitrary device mapper.
constexpr RuntimeMode selectRuntimeMode(MapperSource source) {
    return source==MapperSource::VerifiedSystemAppleVTD ?
        RuntimeMode::AppleVTDCorrectiveExperimental : RuntimeMode::NativePassthrough;
}

// Pure admission policy, shared with host tests. This selects an existing
// mapper; it never creates a domain, changes provider properties or writes
// IOMapper::gSystem. A declared but unresolved per-device mapper cannot fall
// through to the system mapper. Null never reaches the packet-mapping core.
constexpr MapperSource selectMapperSource(uint64_t specific, bool declaredParent,
                                         uint64_t system, uint64_t observedAppleVTD,
                                         bool observedActive) {
    if (!observedActive || !observedAppleVTD) return MapperSource::Unavailable;
    if (specific) return MapperSource::DeviceSpecific;
    if (declaredParent) return MapperSource::Unavailable;
    // Reject uninitialized IOMapper sentinels, absent system mapper, or an
    // unrelated mapper. Equality is checked against a live, retained IOMapper
    // obtained from the AppleVTD class query, not by dereferencing gSystem.
    if (system>4095 && !(system&3) && system==observedAppleVTD)
        return MapperSource::VerifiedSystemAppleVTD;
    return MapperSource::Unavailable;
}
} // namespace bvp
