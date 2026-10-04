# Estex DSS 1.71.66, built from source

The Sprinter's DOS as built from the community repository on 2026-10-04, the build of
[estex-dss-build.md](../../../../../docs/inprogress/2026-09-28-sprinter/estex-dss-build.md) (how to rebuild it,
what each file is).

- **Source:** <https://zxgit.org/Tolik-Trek/Estex-DSS>, branch `master`, commit
  `ae08ad9c1eba31ea22aada6ef07ae95c897d41e2` (2026-05-18); submodule `Shared_Includes` at `90d8292`.
- **Assembler:** sjasmplus 1.21.1. 0 errors, 0 warnings.
- **Version:** "Estex DSS version 1.71.66. Shell version 1.2.523." (`ver` in DSS).
- **Verified:** with `system.dos` and `system.exe` copied over the MAME-pack hard disk's files, BIOS 3.06 Hotfix 2
  boots to Flex Navigator on the emulator.

| File | Bytes | SHA-256 | What |
|---|---:|---|---|
| `system.dos` | 16 941 | `7142eb2995bcae1a6c330d0111c2b4249ed3b5b50fbbc8e59a118f0c50a21a94` | the kernel |
| `system.exe` | 7 936 | `f92fe5df8248ee061ceb0523c0dcb1d45649830bd1b7656687cc520dba461e6d` | the shell |
| `boot.exe` | 3 476 | `fab039a16f0b6ac3800580a38663a06aca3cb0f82393d7ddbd34c00d2e396ed1` | the installer (writes the loader and the files to a disk) |
| `DSSloader.bin` | 1 719 | `50056f50bc53b3649c07fa37340a18a44c6cd0e34db71960a54faa848efa8d50` | the boot loader (`Starting...`) the installer writes |
| `system.exp` | 399 | `0f0958b83c04468928c48e58bd1a8c36c2d8ebc86e798b4734356bf035bc2df9` | the kernel's exported labels |
| `system.sym` | 144 762 | `faf3fe89600e19c2e236ec89917f4936aedafcae49a2a15c6c699694fd3627f1` | all 3 640 kernel labels (addresses from `ORG 0`) |

The symbol files match only this `system.dos`.
