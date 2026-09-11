#pragma once
#include <stdint.h>

namespace bvp {
// An exact ABI-gated TX-status data word is not a DMA ownership/error verdict.
// The original native read and its own error handling always execute first.
// This predicate grants NO completion, reset, unmap, free or reuse permission.
constexpr bool returnNativeTxStatusWord(bool enabled, uint64_t validatedReturnPC,
                                       uint64_t caller, uint32_t width, uint32_t value) {
    return enabled && validatedReturnPC && caller==validatedReturnPC &&
           width==4 && value==0xffffffffU;
}
} // namespace bvp
