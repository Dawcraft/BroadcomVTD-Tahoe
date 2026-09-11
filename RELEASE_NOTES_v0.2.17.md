# BroadcomVTD-Tahoe v0.2.17 — Initial Experimental Release

Keep AppleVTD/IOMMU enabled while using the supported legacy Broadcom wireless
stack on macOS Tahoe. BroadcomVTD-Tahoe is independent of OCLP and
[OCLP-CustoMac](https://github.com/kgp-macPro/OCLP-CustoMac).
OCLP-CustoMac provides the Modern Wireless root-patch environment that restores
the legacy Broadcom stack under Tahoe. BroadcomVTD-Tahoe addresses the additional
kernel-resident Tahoe runtime DMA/IOMMU compatibility problem observed when the
restored AirPortBrcmNIC stack operates with AppleVTD enabled. BroadcomVTD-Tahoe
does not install or replace those root patches.

## Validated configuration and result

ASUS WS X299 Sage/10G; BCM943602CDP, PCI 14e4:43ba, D11 rev49;
physically tested on **macOS Tahoe 26.6.2 (25G83)** with
**OCLP-CustoMac 3.0.3 with Modern Wireless root patches**; AppleVTD enabled and
`DisableIoMapper=false`.

Project scope: **macOS Tahoe / Darwin 25.x**.

No positive BroadcomVTD boot arguments are required. Actual mapper identity
automatically selects native passthrough or AppleVTD corrective experimental
mode. The latter is the physically validated mode; no new dedicated v0.2.17
native-passthrough hardware campaign is claimed.

Wi-Fi, repeated Sleep/Wake, bidirectional AirDrop, AirPlay, Screen Mirroring,
tested AWDL/Continuity functions and Personal Hotspot passed. **Ethernet was
disabled during Wi-Fi and Personal Hotspot testing.** The general bulk-reset
lifetime correction returned active OLD credits without emulating native
packet free/requeue semantics.

The final mapper snapshot had **zero live/retained TX mappings**, no TX halt,
no pending OLD records and RX live 240. Historical telemetry recorded 206
quarantine events and 1071 cumulative NoCredit pressure events. These are
cumulative workload/pressure counters, not retained mappings or permanent
failures. Pressure remained non-latching: later traffic continued and the final
TX mapping snapshot returned to zero.

## Important limitation

The matching successful D64 reset + observed DISABLED + 300-us drain premise
remains **C/C+, EXPERIMENTAL**. C/C+ reflects supporting technical evidence plus successful
physical validation, not a vendor-documented D11 rev49 hardware contract.
Time alone never grants retirement.

EXPERIMENTAL; NOT REV49 SOURCE-PROVEN; NOT RELEASE AUTHORITY

## Installation and immutable asset

Use the official `BroadcomVTD-Tahoe-v0.2.17.zip`, not CI output. The actual kext
remains `BroadcomVTD.kext`, ID `local.kgp.BroadcomVTD`; no 1.0.0 rename or rebuild.
Back up your EFI; add it to OpenCore while preserving the existing Modern Wireless
prerequisites. Lilu 1.7.2 was used for the validated configuration. BroadcomVTD must
load after Lilu; a compatible later Lilu version may also be used. Do not remove
other components' required arguments or live-load/unload the kext.
See the [README](README.md) for full guidance.

```text
Version: 0.2.17
Executable bytes: 147408
SHA-256: 2a8f9641a9c9856b1e45899f31332a68ad2cf519e94951ca128bb3f262c3c349
UUID: D0B214B8-F096-3BC6-99AD-F1E9D8E788B6
```

Credits: KGP (direction/integration/hardware validation/maintenance); OpenAI
ChatGPT (research, architecture, evidence, independent review/documentation);
OpenAI Codex CLI (implementation, analysis and validation tooling under KGP's
direction and ChatGPT review); Mieze/IntelLucy (architectural prior art, not
copied source or co-development); Acidanthera (Lilu/MacKernelSDK); OCLP developers
(Modern Wireless environment). Full attribution and licenses are in the README.

Documentation: https://github.com/kgp-macPro/BroadcomVTD-Tahoe
