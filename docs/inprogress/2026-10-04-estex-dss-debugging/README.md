# Estex DSS aware debugging

Started 2026-10-04 (owner request). Status: [TODO.md](TODO.md). Plan row: #99 in [PLAN.md](../PLAN.md).

Estex DSS is the disk operating system of the Peters Plus Sprinter (DSS 1.62 from Peters Plus, the community DSS
1.70 / 1.71 by the Sprinter Team). This folder is for an OS-level layer in the debugger and the analyzers that
**understands a running DSS**, the way [2026-09-30-nedoos-integration](../2026-09-30-nedoos-integration/README.md)
does for NedoOS: see the system as files, programs, drives and memory blocks instead of bytes and ports, follow its
calls on the TTD timeline, and drive it from every automation surface.

## Scope to research and describe

- **Calls**: the DSS API (`RST #10`, function number in C; `Shared_Includes` `constants/dss_equ.inc`) and the BIOS
  API underneath (`RST #08`; `Shared_Includes` `Docs/BIOS functions.asm`) - a decoded call trace with arguments,
  results and error codes (`DSS_Error.*`, `BIOS.Error.*`), per program, on the TTD timeline.
- **Programs**: the running EXE (its header: load / start / stack address, the code it was loaded from), the shell
  and batch state, how a program ends (normal, "Unexpected application termination"), the return to Flex
  Navigator or the shell.
- **Files and drives**: open file handles and their positions, the current drive and directory, the drive tables
  (devices, logical drives, RAM disks; `DSS/DRV-MAIN.ASM`, `ReScanDRV.ASM`), the file systems (FAT12 / 16 / 32,
  the CD file system in progress).
- **Memory**: the DSS memory manager's blocks (who owns which 16 KB page), the windows of the running program, the
  environment variables, the system page.
- **Symbols**: the build's export file (`system.exp`, section 3 of the Sprinter
  [build guide](../2026-09-28-sprinter/estex-dss-build.md)) and the BIOS's export, so traces show names.
- **Do**: run a program, type into the shell, call DSS directly (the same calls programs make), read a file through
  the system - on CLI, WebAPI + OpenAPI, MCP, Lua, Python and in Qt.

## Sources

- Estex-DSS sources (zxgit.org/Tolik-Trek/Estex-DSS) and their `Shared_Includes` submodule; the build guide in the
  Sprinter folder: [estex-dss-build.md](../2026-09-28-sprinter/estex-dss-build.md).
- The Sprinter folder's software notes: [2026-09-28-sprinter](../2026-09-28-sprinter/README.md) (BIOS versions,
  storage, the ZX mode launcher, the CD boot page).
- The NedoOS layer as the pattern: its requirements (NK-1..NK-23), the kernel reference and POC 020.

## Known starting points (2026-10-04)

- DSS 1.71.57 on the MAME-pack disk; DSS 1.71.66 built from the master sources runs on the emulator.
- `TYPE` in the DSS shell ends with "Unexpected application termination" after leaving Flex Navigator (seen on
  BIOS 3.06 Hotfix 2, any file) - a first case for the layer to explain.
