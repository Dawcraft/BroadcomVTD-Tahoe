#include "../POC/MapperSelection.hpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace bvp;
    constexpr uint64_t vtd=0xffffff8000100000ULL, specific=0xffffff8000200000ULL;
    constexpr auto none=MapperSource::Unavailable;
    // Reproduce the established 0.2.0 blocker: absent iommu-parent, null
    // device-specific result; explicitly accept the same live system AppleVTD.
    assert(selectMapperSource(0,false,vtd,vtd,true)==MapperSource::VerifiedSystemAppleVTD);
    assert(selectMapperSource(specific,true,vtd,vtd,true)==MapperSource::DeviceSpecific);
    assert(selectMapperSource(specific,false,vtd,vtd,true)==MapperSource::DeviceSpecific);
    // Never hide an unresolved per-device override or use an arbitrary global.
    assert(selectMapperSource(0,true,vtd,vtd,true)==none);
    assert(selectMapperSource(0,false,specific,vtd,true)==none);
    assert(selectMapperSource(0,false,vtd,vtd,false)==none);
    assert(selectMapperSource(specific,true,vtd,vtd,false)==none);
    assert(selectMapperSource(specific,true,vtd,0,true)==none);
    for (uint64_t sentinel=0;sentinel<4096;++sentinel)
        assert(selectMapperSource(0,false,sentinel,vtd,true)==none);
    assert(selectMapperSource(0,false,vtd|1,vtd,true)==none);
    std::cout<<"PASS: device mapper preference; explicit verified system AppleVTD; unresolved override, inactive/absent/unrelated/sentinel rejection\n";
}
