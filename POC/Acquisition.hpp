#pragma once
#include <stdint.h>

namespace bvp {
// Named fields are essential: Fallback is NOT the already-loaded flag.
template <class KextInfo> void configureTargetNotifications(KextInfo &info) {
    info.sys[KextInfo::Loaded] = true;
    info.sys[KextInfo::Reloadable] = false;
    info.sys[KextInfo::Disabled] = false;
    info.sys[KextInfo::FSOnly] = false;
    info.sys[KextInfo::FSFallback] = true;
}
enum AcquisitionStage : uint32_t {
    Registered = 1, PatcherReady, Dispatch, TargetEntered,
    BinaryAccepted, LayoutAccepted, RoutesInstalled,
    ObserverInstalled, ProviderRejected, AcquisitionFailed
};
constexpr bool exactControllerVtable(uint64_t actual, uint64_t expected) {
    return expected && actual == expected;
}
} // namespace bvp
