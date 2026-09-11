# Third-party notices and provenance

BroadcomVTD is independently implemented for AirPortBrcmNIC. The project's
original code is BSD-3-Clause, as specified in LICENSE. This does not relicense
dependencies, Apple software, or compatibility fingerprint data derived from
analysis of the target binary.

## Lilu

The build compiles `Lilu/Library/plugin_start.cpp` and consumes Lilu headers.
The released binary therefore includes Lilu support code. Lilu 1.7.2 is
Copyright (c) 2016–2018 vit9696, BSD-3-Clause; the full redistribution notice is
in [Licenses/Lilu-BSD-3-Clause.txt](Licenses/Lilu-BSD-3-Clause.txt), which also
accompanies the release archive. Source:
https://github.com/acidanthera/Lilu/tree/e4748cc081bf060302c7d3c44a643ce1d11b7e1d

Neither Lilu.kext nor its complete source tree is vendored here. Users obtain
Lilu separately; public source builds download the hash-pinned archive in
`Config/dependencies.lock.json`. Lilu's unrelated disassembler libraries are
not compiled into BroadcomVTD by this build.

## MacKernelSDK / Apple interfaces

MacKernelSDK supplies the kernel headers and `Library/x86_64/libkmod.a` build
input. Its Apple-derived files retain their individual notices; the SDK's
APSL-2.0 license is reproduced in
[Licenses/MacKernelSDK-APSL-2.0.txt](Licenses/MacKernelSDK-APSL-2.0.txt), which also
accompanies the public release archive.
Source, including the kmod source directory, is available under its applicable
notices at https://github.com/acidanthera/MacKernelSDK . The public build pins
commit `05094e5e88cec7caedbfb35e8449ed0db94bf95b` and its archive hash. These
inputs are external dependencies, not relicensed project files. No SDK tree or
Apple driver executable is redistributed in this repository or release ZIP.

The tested build used a locally retained MacKernelSDK tree obtained with the
IntelLucy research checkout. That storage location does not make BroadcomVTD an
IntelLucy derivative. Public CI builds use the explicitly pinned upstream SDK;
they are not substitutes for the tested binary or claims of cross-toolchain
byte-for-byte reproducibility.

## Target compatibility fingerprints

`Config/TargetGate.hpp` preserves the tested generated UUID, symbol/layout
offsets and short byte-matching signatures. They are used to reject mismatched
native code, not to reproduce native driver algorithms. The originating
AirPortBrcmNIC identity and exact header hash are documented in Docs/PROVENANCE.md.
No Apple/Broadcom driver, firmware, native disassembly, or native implementation
source is included. The project license does not grant rights to Apple/Broadcom
software, nor permission to redistribute root-patch payloads.

## IntelLucy and other research

Mieze's IntelLucy, especially its Tahoe AppleVTD DMA work, provided important
architectural prior art about explicit mapper/packet lifetime ownership.
The selected runtime source, tests and build inputs contain no identified
copied IntelLucy implementation. IntelLucy source is not redistributed or linked.
BroadcomVTD uses a distinct final private-TX-backing/split-lifetime architecture;
Mieze is not presented as a co-developer of this independent project.

OpenCore Legacy Patcher supplies the independently maintained Modern Wireless
patch environment used by the validated setup. No OCLP source/payload is
redistributed here. Attribution does not imply endorsement by any credited
person, project, company or OpenAI.
