# Architecture in brief

BroadcomVTD-Tahoe is a resident Lilu plugin for one exact legacy AirPortBrcmNIC
ABI under Tahoe. It is not an alternative Wi-Fi driver or root patcher.

The Modern Wireless root-patch environment restores the legacy Broadcom stack.
BroadcomVTD operates later, at kernel runtime, to address the additional Tahoe
DMA/IOMMU lifetime incompatibility observed when the restored AirPortBrcmNIC
stack runs with verified system AppleVTD.

## Selection and scope

The Darwin/UUID/private-layout/provider checks precede target binding. The
existing mapper-selection logic must identify the verified system AppleVTD
source before corrective mode activates. Without it, native passthrough does
not allocate corrective private storage or impose mapper lifetime interception.
No OpenCore configuration is inspected. Selection is not a feature/FIFO/device
exception matrix. The documented PCI ID identifies the physically tested
hardware; it is not itself the corrective authorization boundary.

The target AirPortBrcmNIC SHA-256 is
`884e90b27cff151e432bb36275936f57b43e72419e95b51b3ac067d3f2ccd331`,
UUID `E4678FEB-1DC1-35CC-9306-48021083D04A`. Private byte/layout fingerprints
fail closed on incompatible native code. Tahoe updates carrying a different
target are outside this exact validation, even if the hardware is unchanged.

## Three separate lifetimes

1. Native packet/software ownership: native free, requeue, tags, SCB and AMPDU
   handling remain native. BroadcomVTD does not emulate those decisions.
2. Native software-ring attachment: descriptor and packet slots can be detached
   during range-1 reclaim without constituting final hardware DMA completion.
3. Private DMA lifetime: a transaction owns immutable BroadcomVTD backing and its
   mapper-backed MD/IOVA until a permitted completion boundary.

Corrective TX copies the exact validated byte/segment plan into a bounded private
pool: 64 Mapping credits, 32 KiB per slot, 2 MiB wired for the corrective instance.
Head-length ABI, chains, multi-segment accounting and native descriptor encoding
are preserved. No physical-address fallback is introduced. No backing slot can
be overwritten merely because the native packet is recycled.

Qualified range-2 hardware completion retires normally. Qualified range-1
software detachment removes only the active packet association, returns the exact
packet natively and retains OLD private DMA separately. If the same packet is
later transmitted, it obtains a fresh Mapping/serial/MD/IOVA lifetime; pointer
identity does not merge OLD and NEW transactions.

## Experimental reset terminal and 0.2.17

The existing native-owned D64 reset interception holds the exclusive TX lease.
It requires matching nonzero geometry, owner/ring/thread, observed rev49,
generation/ticket/fence, native success, exact ownership-read DISABLED
acknowledgement, no genuine halt, and unchanged geometry after the 300-us drain.
The drain is inside the wrapper before native continuation, not a watchdog reset
or time-only release. Failed, stale, ambiguous or blocked records remain retained.

v0.2.17 makes the caller PC telemetry, not ownership authority. It also preserves
observed hardware revision across same-geometry software invalidation while
still invalidating generation/ticket. Changed geometry or fatal/blocking
invalidation discards revision provenance. This includes the native bulk reset
family without a Sleep/Wake, FIFO, mode or caller exception.

Eligible active OLD private records can end their association at the same
experimental reset terminal. MD completion/slot-lease retirement does not free
or notify the native packet. Native continuation then performs its original
software disposition. Failed MD completion retains backing and fails closed.

NoCredit is temporary admission pressure, not a permanent mapper fault. A packet
that cannot be admitted follows the exact native send-side error/consume
contract once; original TX is never invoked without a safe admitted mapping.
Later independently returned credits can admit fresh TX. There is no eviction,
table enlargement or genuine-halt clearing.

RX mapper selection/generation cleanup and the distinction between ordinary
native reads and the seven exact ownership-read sites remain unchanged.
Bounded trace format 13 includes mode, pressure, private lifetime and reset
evidence without packet payloads. Pointer identities in local captures require
care before public sharing; do not upload raw captures indiscriminately.

The rev49 reset/DISABLED/300-us retirement boundary remains experimental and
is not a vendor-documented hardware contract.

EXPERIMENTAL; NOT REV49 SOURCE-PROVEN; NOT RELEASE AUTHORITY
