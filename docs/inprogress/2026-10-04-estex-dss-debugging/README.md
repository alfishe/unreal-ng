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

## Memory, leaks and system-resource corruption (added 2026-10-21)

The first research item below (the DSS memory model, call mechanics, structures and addresses) is answered by
two documents, written as the answer to an audience question ("log what happens, shout to the console when a
program does not give OS memory back - how do you catch that, and any kind of leak or corruption, in the
debugger?"):

- [dss-system-state.md](dss-system-state.md) (Russian) - the short kernel reference: entry points (`RST #10`
  DSS with the function in `C`, `RST #08` BIOS), the low-16K map, the three levels of memory ownership
  (BIOS handles #C2/#C3/#C4/#04C2 -> the DSS owner table MEMTAB -> the PAGE0-3 windows at ports
  #82/#A2/#C2/#E2), EXEC/LEAVE semantics (`LEAVEM` auto-frees a task's blocks, but **not** its file handles),
  the PSP anchors (task / memory block / EXE handle at PSP[-1..-3]), and the real addresses of the running
  1.71.66 build from its `system.exp` (`CORE_BUFFERS.MemoryTable` 0x38EB, `FM_BUF` 0x318E, `XSTACK` 0x35EB -
  the source-tree `DSS_MAP.TXT` is the older 1.70b layout).
- [leak-corruption-playbook.md](leak-corruption-playbook.md) (Russian) - detection classes A-G with recipes on
  the emulator's existing machinery: syscall traps on 0x0010/0x0008 with argument decoding, alloc/free and
  open/close ledgers, balance checks on the process lifecycle (EXEC / LEAVE at vector 0x0000 / GSWITCH), the
  INFOMEM free-page delta across a program's lifetime (the real "OS memory not given back" detector, BIOS
  level), the FM owner scan on exit, checksums of the system zones, page-ownership write triggers, TTD reverse
  to the allocating PC, console/CLI findings, a draft `dss.yaml` OS descriptor (the Phase 6 format), and
  mutation tests that prove the detectors catch a file manager that forgot a `FREEMEM`/`CLOSE`.

The NedoOS pattern ([2026-09-30-nedoos-integration](../2026-09-30-nedoos-integration/README.md)) stays the
model; NedoOS's own doc even notes `QUIT (0x0000)` as an intended debugger breakpoint - the same trick as the
DSS LEAVE vector here.
