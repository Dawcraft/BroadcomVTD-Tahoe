#pragma once
#include <stdint.h>
#include <stddef.h>

namespace bvp {
// Classify how OUR mapping ownership proof uses a read. Do not redefine every
// Broadcom register: native error detection and original results stay native.
enum class ReadUse : uint32_t { Native=0, TxCompletion=1, RxCompletion=2, TxReset=3, RxReset=4 };
struct ReadUseSite { uint64_t returnPC; ReadUse use; };
template <size_t N>
constexpr ReadUse readUse(uint64_t caller,const ReadUseSite (&sites)[N]) {
    for (size_t i=0;i<N;++i) if (sites[i].returnPC && caller==sites[i].returnPC) return sites[i].use;
    return ReadUse::Native;
}
constexpr bool nativeOwnsReadResult(bool scopedPolicy,ReadUse use,uint32_t width,uint32_t value) {
    // Ordinary data/control/diagnostic reads are not ownership certificates.
    // Invalid completion/reset observations still must NOT release mappings.
    return scopedPolicy && use==ReadUse::Native && width==4 && value==0xffffffffU;
}
} // namespace bvp
