<p align="center">
  <img src="Assets/BroadcomVTD-Tahoe-Hero.png"
       alt="BroadcomVTD-Tahoe"
       width="100%">
</p>

[![Build and regression tests](https://github.com/kgp-macPro/BroadcomVTD-Tahoe/actions/workflows/ci.yml/badge.svg)](https://github.com/kgp-macPro/BroadcomVTD-Tahoe/actions/workflows/ci.yml)
[![License: BSD-3-Clause](https://img.shields.io/badge/license-BSD--3--Clause-blue)](LICENSE)

BroadcomVTD-Tahoe lets supported legacy Broadcom Wi-Fi work under macOS Tahoe
while keeping **AppleVTD/IOMMU enabled**, instead of disabling the IOMMU for the
whole system. It is an experimental companion kext for the legacy AirPortBrcmNIC
driver, with successful real-hardware validation on the configuration below.

**This is an independent project, not part of OpenCore Legacy Patcher or
[OCLP-CustoMac](https://github.com/kgp-macPro/OCLP-CustoMac).**
OCLP-CustoMac provides the Modern Wireless root-patch environment that restores
the legacy Broadcom stack under Tahoe. BroadcomVTD-Tahoe addresses the additional
kernel-resident Tahoe runtime DMA/IOMMU compatibility problem observed when the
restored AirPortBrcmNIC stack operates with AppleVTD enabled. It does not install
or replace the Modern Wireless root patches.

## The short version

- **v0.2.17: physically validated experimental release.** Fresh boot, repeated
  Sleep/Wake and the controlled wireless feature campaign passed.
- **Zero required positive BroadcomVTD boot arguments.** Runtime mapper identity
  selects the appropriate mode automatically.
- Project/repository: **BroadcomVTD-Tahoe**. Actual extension:
  **BroadcomVTD.kext**, bundle ID `local.kgp.BroadcomVTD`.
- The version stays **0.2.17** because this exact binary was tested. There was
  no cosmetic 1.0.0 rename/rebuild. CI builds are separate, untested artifacts.
- This is narrow, target-gated support—not a promise for every Broadcom card,
  Tahoe update, patched driver version or platform.

[Download the official v0.2.17 release](https://github.com/kgp-macPro/BroadcomVTD-Tahoe/releases/tag/v0.2.17).
The official release asset contains the exact physically tested
**BroadcomVTD.kext**. GitHub Actions CI artifacts are not substitutes.

## Physically validated configuration

| Component | Tested configuration |
| --- | --- |
| Motherboard | ASUS WS X299 Sage/10G |
| Wi-Fi | BCM943602CDP, PCI `14e4:43ba`, D11 rev49 |
| macOS | **Tahoe 26.6.2 (25G83)**; project scope: Tahoe / Darwin 25.x |
| Wireless restoration | **OCLP-CustoMac 3.0.3 with Modern Wireless root patches** |
| Lilu | 1.7.2 used for physical validation |
| IOMMU | AppleVTD enabled; `DisableIoMapper=false` |
| BroadcomVTD | Exact frozen v0.2.17 release binary; no positive BroadcomVTD arguments |

A matching Broadcom card model alone is not sufficient; BroadcomVTD also gates
the exact supported AirPortBrcmNIC UUID/ABI. The physically pinned UUID is
`E4678FEB-1DC1-35CC-9306-48021083D04A`.
See [scope and provenance](Docs/PROVENANCE.md).

## Requirements and scope

- Intel/x86_64 Hackintosh; project scope is **macOS Tahoe / Darwin 25.x**.
  The physically tested OS is **Tahoe 26.6.2 (25G83)**, not every Tahoe point release.
- A compatible Modern Wireless restoration environment. Physical validation used
  **OCLP-CustoMac 3.0.3 with Modern Wireless root patches**.
- The physically validated Broadcom device is **BCM943602CDP
  (14e4:43ba / D11 rev49)**. The exact supported AirPortBrcmNIC UUID/ABI must also
  match; another chipset is not supported merely because it is Broadcom.
- **Lilu 1.7.2** was used for physical validation. A compatible later Lilu version
  may also be used. BroadcomVTD must load **after Lilu**.
- Corrective operation requires **AppleVTD enabled / `DisableIoMapper=false`**;
  mode selection uses the actual runtime mapper identity, not configuration text.

## Installation with OpenCore

1. Keep a bootable backup of your existing EFI and a recovery route. Read the
   experimental limitation below before changing a working system.
2. Establish the supported Tahoe Modern Wireless environment separately. This
   project neither supplies OCLP payloads nor changes its patch/security setup.
3. Use **BroadcomVTD-Tahoe-v0.2.17.zip from the project's Release assets**, not a
   GitHub Actions build. Verify the executable identity below after extraction.
4. Manually copy `BroadcomVTD.kext` to `EFI/OC/Kexts`. Ensure Lilu is present and
   enabled as described above. Add BroadcomVTD under OpenCore `Kernel -> Add`
   **after Lilu**: BundlePath `BroadcomVTD.kext`, ExecutablePath
   `Contents/MacOS/BroadcomVTD`, PlistPath `Contents/Info.plist`, Enabled `true`,
   Arch `x86_64`. Limit it to Darwin 25.x/Tahoe in a multi-OS EFI if needed.
5. Retain the order and prerequisites of your existing OCLP Modern Wireless
   stack. BroadcomVTD does not replace IOSkywalkFamily, IO80211FamilyLegacy,
   AMFIPass or their existing setup. It is a **Lilu plugin**, not an AirportItlwm
   extension. Do not install duplicate BroadcomVTD copies.
6. Validate the OpenCore configuration and reboot manually when ready. Do not
   live-load/unload this resident kext. Start with Wi-Fi, then one Sleep/Wake,
   then your usual wireless functions. To roll back, restore your known-good
   EFI/entry; this project does not automate system configuration changes.

Normal operation needs none of `-brcmvtd`, `-brcmvtdrx`, `-brcmvtdtxchain`,
`-brcmvtdprivatetx300` or `-brcmvtdnotxcleanup`. **This does not remove boot
arguments required by other components**, including your root-patch setup.
The validated configuration keeps `DisableIoMapper=false`; the plugin never
parses or edits OpenCore's configuration to select its mode.

### Automatic modes

| Actual runtime mapper identity | BroadcomVTD behavior |
| --- | --- |
| Verified system AppleVTD on the exact bound target | `APPLEVTD_CORRECTIVE_EXPERIMENTAL`: uses AppleVTD-aware RX/TX DMA and lifetime handling while preserving native Broadcom packet disposition |
| No verified AppleVTD source | `NATIVE_PASSTHROUGH`: leaves the native DMA/lifetime path unchanged |

Corrective mode includes the experimental reset/drain policy automatically.
See [architecture](Docs/ARCHITECTURE.md) for the ownership and lifetime details.
Native passthrough has source/host evidence; **it did not receive a new dedicated
v0.2.17 physical campaign**.

## What was tested

Wi-Fi, repeated Sleep/Wake, bidirectional AirDrop, AirPlay, Screen Mirroring,
tested AWDL/Continuity functions including Continuity Camera, and Personal
Hotspot passed the reported controlled campaign. Bidirectional functions were
tested bidirectionally where applicable.

**Ethernet was disabled during Wi-Fi and Personal Hotspot validation.** Those
passes were not silently carried by the X550 Ethernet interfaces.

The definitive post-all-tests capture ended in corrective mode with:

| Health result | Final state |
| --- | --- |
| TX live / quarantined mappings | **0 / 0**; mapping snapshot empty |
| TX halt / pending OLD private records | **0 / 0** |
| Historical quarantine events | 206—not current occupancy |
| Cumulative NoCredit pressure events | 1071—not a permanent failure |
| Later reservations / submissions after pressure | 23748 / 23748 |
| RX | enabled, not halted; **240 live** |
| First permanent failure | not captured |

Temporary mapping pressure remained non-latching: later traffic continued and
TX occupancy returned completely to zero. This does **not** claim that every
individual NoCredit event was separately traced to completion. RX accounted for
38620 prepared/mapped, 37660 normally completed and 720 reset-completed records:
`38620 - 37660 - 720 = 240`.

### Sleep/Wake: the important change

Earlier experiments could conservatively retain an OLD private TX mapping
across certain native bulk resets. v0.2.17 generalized the reset lifetime model,
not a Sleep-, FIFO- or application-specific exception. Native code still owns
packet free/requeue behavior; BroadcomVTD ends only the eligible OLD DMA lifetime.

Physical tracing confirmed the six-ring native core-reset path and successful
retirement of an active OLD private DMA lifetime. Repeated Sleep/Wake completed
with zero retained TX mappings, no mapper halt, no pending OLD records and
RX live at 240.

The controlled post-wake validation passed Wi-Fi, bidirectional AirDrop, AirPlay,
Screen Mirroring, Continuity Camera and Personal Hotspot. See the
[detailed physical-validation report](Docs/PHYSICAL_VALIDATION_v0.2.17.md) for the
exact serial/generation/ticket and Stage17 -> Stage13 evidence, complete campaign
observations and evidence boundaries.

## Experimental hardware boundary

Internally, the available rev49 hardware evidence is classified as
**formal C / informal C+**. In practical terms, the implementation has supporting
technical evidence and successful physical validation, but the reset/drain retirement
contract is not a vendor-documented D11 rev49 hardware specification.

The assumed boundary remains **EXPERIMENTAL**: a positively successful matching
destructive D64 reset, observed DISABLED acknowledgement and the existing
**300-us drain**, with the ownership/generation checks intact.
Time alone never authorizes retirement.

```text
EXPERIMENTAL; NOT REV49 SOURCE-PROVEN; NOT RELEASE AUTHORITY
```

Successful testing is not a Broadcom/Apple hardware specification or a generic
production-safety claim. Use only with informed acceptance of this limitation.

## Exact release identity

```text
BroadcomVTD.kext — local.kgp.BroadcomVTD — 0.2.17
x86_64 executable: 147408 bytes
SHA-256: 2a8f9641a9c9856b1e45899f31332a68ad2cf519e94951ca128bb3f262c3c349
UUID: D0B214B8-F096-3BC6-99AD-F1E9D8E788B6
```

[Release notes](RELEASE_NOTES_v0.2.17.md) · [Build and CI](Docs/BUILDING.md) ·
[Architecture](Docs/ARCHITECTURE.md) · [Provenance](Docs/PROVENANCE.md) ·
[License](LICENSE)

CI performs source/build/regression/identity checks. Uploaded artifacts are
**CI BUILD — NOT PHYSICALLY VALIDATED**. They never replace the frozen official
release, even if a local rebuild happens to produce identical bytes.

## Credits

- **KGP:** project concept/direction, system integration, hardware experimentation,
  physical validation, release maintenance and documentation.
- **OpenAI ChatGPT:** research and architecture collaboration, evidence analysis,
  test strategy, independent source review and technical documentation.
- **OpenAI Codex CLI:** source implementation, local binary/source analysis,
  build/validation tooling and evidence/report generation, under KGP direction
  and independent ChatGPT review.
- **Mieze / IntelLucy:** important architectural prior art, especially Tahoe
  AppleVTD DMA and mapper-aware packet-lifetime work. BroadcomVTD is an independent
  AirPortBrcmNIC implementation with a different final private-TX-backing
  architecture—not an IntelLucy source derivative or a co-developed project.
- **Acidanthera:** Lilu framework and MacKernelSDK build infrastructure.
- **OpenCore Legacy Patcher developers:** the Modern Wireless environment used
  to restore legacy Broadcom Wi-Fi under Tahoe. BroadcomVTD is independent of OCLP.

BSD-3-Clause for independent project contributions; dependency notices are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Community project; no implied
endorsement by Apple, Broadcom, OpenAI, Acidanthera, Mieze or the OCLP developers.
