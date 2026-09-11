#pragma once
#include <stdint.h>
#include <stddef.h>
#include <mach-o/loader.h>
#ifdef BVT_HOST_TEST
#include <cstring>
#else
#include <libkern/libkern.h>
#endif

namespace bvp {
constexpr bool pciIdentityMatches(uint32_t vendor, uint32_t device, uint32_t code) {
    return vendor==0x14e4 && device<=0xffff && code==0x028000;
}
inline bool instructionBytesMatch(uintptr_t base, size_t size, uint64_t offset,
                                  const void *bytes, size_t count) {
    return offset<=size && count<=size-offset &&
        !memcmp(reinterpret_cast<const void *>(base+offset),bytes,count);
}
inline bool binaryUUIDMatches(uintptr_t base, size_t size, const uint8_t uuid[16]) {
    if(size<sizeof(mach_header_64)) return false;
    const auto *h=reinterpret_cast<const mach_header_64 *>(base);
    if(h->magic!=MH_MAGIC_64 || h->cputype!=CPU_TYPE_X86_64 || h->filetype!=MH_KEXT_BUNDLE ||
       h->ncmds>256 || h->sizeofcmds>size-sizeof(*h)) return false;
    size_t off=sizeof(*h), end=off+h->sizeofcmds; bool found=false;
    for(uint32_t i=0;i<h->ncmds;++i) {
        if(off>end || end-off<sizeof(load_command)) return false;
        auto c=reinterpret_cast<const load_command *>(base+off);
        if(c->cmdsize<sizeof(*c) || c->cmdsize>end-off) return false;
        if(c->cmd==LC_UUID) {
            if(found || c->cmdsize!=sizeof(uuid_command) ||
               memcmp(reinterpret_cast<const uuid_command *>(c)->uuid,uuid,16)) return false;
            found=true;
        }
        off+=c->cmdsize;
    }
    return found && off==end;
}
} // namespace bvp
