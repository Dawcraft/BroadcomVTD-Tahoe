# BroadcomVTD-Tahoe v0.2.17

Physically validated EXPERIMENTAL Tahoe release. Actual file:
`BroadcomVTD.kext`, identifier `local.kgp.BroadcomVTD`. See RELEASE_NOTES_v0.2.17.md
and https://github.com/kgp-macPro/BroadcomVTD-Tahoe for configuration and scope.

Back up your EFI. Establish the supported OCLP-CustoMac 3.0.3 Modern Wireless
root-patch environment separately. Manually add BroadcomVTD.kext to OpenCore's
Kexts and Kernel/Add configuration **after Lilu 1.7.2 or later**. Preserve your
existing wireless stack and other components' boot arguments. BroadcomVTD needs
zero positive arguments. No live load/unload; KGP/user controls reboot/testing.

Executable identity: 147408 bytes; SHA-256
`2a8f9641a9c9856b1e45899f31332a68ad2cf519e94951ca128bb3f262c3c349`;
UUID `D0B214B8-F096-3BC6-99AD-F1E9D8E788B6`.

Runtime selects actual verified AppleVTD identity, not OpenCore configuration
text. The tested setup keeps AppleVTD enabled and DisableIoMapper=false.

EXPERIMENTAL; NOT REV49 SOURCE-PROVEN; NOT RELEASE AUTHORITY

The successful reset/DISABLED/300-us premise is not a Broadcom/Apple rev49
specification. Use with informed acceptance and a known-good recovery route.
Do not substitute a CI artifact for this frozen physical release.
