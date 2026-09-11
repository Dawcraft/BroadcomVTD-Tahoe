#include <mach/kmod.h>
extern "C" kern_return_t BroadcomVTD_kern_start(kmod_info_t *, void *);
extern "C" kern_return_t BroadcomVTD_kern_stop(kmod_info_t *, void *);
extern "C" {
KMOD_EXPLICIT_DECL(local.kgp.BroadcomVTD, "0.2.5", BroadcomVTD_kern_start, BroadcomVTD_kern_stop)
__private_extern__ kmod_start_func_t *_realmain = BroadcomVTD_kern_start;
__private_extern__ kmod_stop_func_t *_antimain = BroadcomVTD_kern_stop;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
}
