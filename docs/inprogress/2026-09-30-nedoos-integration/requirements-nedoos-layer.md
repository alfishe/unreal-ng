# NedoOS compatibility layer (analyzer) — requirements

**Status:** requirements, 2026-09-30, checked against a live NedoOS the same
day. Folder index: [README.md](README.md). Structures, addresses, procedures and
the live results: [nedoos-kernel-reference.md](nedoos-kernel-reference.md).
Prototype: POC [020-nedoos-layer](../../../tools/poc/020-nedoos-layer/).
**Related:** the NedoOS struct catalog and DSL ideas in
[2026-09-17-nedoos-future-support](../2026-09-17-nedoos-future-support/), the
developer toolchain (#51), the analyzer framework (`core/src/debugger/analyzers/`).

---

## 0. In one page

An analyzer that **understands a running NedoOS**: it finds the kernel in memory,
maps its structures, and lets every tool see and drive the system at the level of
the OS rather than the level of bytes and ports:

- **see**: which tasks run, what each owns (memory pages, files, sockets, screen),
  what each prints, what the kernel is doing right now;
- **do**: type into a task, run and stop programs, call the kernel directly
  (the same calls programs make), read and write a task's memory by its own
  addresses;
- **follow**: a trace of kernel calls and task switches, on the TTD timeline;
- **everywhere**: the same operations on CLI, WebAPI + OpenAPI, MCP, Lua, Python
  and a Qt panel.

**Worked example (checked live, see [the reference §6](nedoos-kernel-reference.md#6-the-zxdb-case-read-with-the-layer-2026-09-30)).**
An agent asks "why does `zxdb` hang". The layer answers: task 6 `zxdb` (8
pages, has the screen) is **inside the kernel** in `CMD_WIZNETWRITE`, called
from `#647A`, sending 31 bytes on socket 1: a DNS query for `next.zxart.ee`.
The W5300 driver waits at `w53_cmd0` for port `#03AB` to read 0; it reads `#FF`
because there is no adapter. The kernel is not preemptive, so every task is
stopped, not only zxdb. The layer can end the stuck call with an error (NK-22);
zxdb then prints "No results found" and the system runs on.

## 1. Terms

| Term | Meaning |
|---|---|
| **Kernel** | The NedoOS system code and data, in the system page(s) and the resident part (`src/kernel/`) |
| **Task (app)** | One of the kernel's application slots: `MAXAPPS = 16` (`syskrnl.asm:4`), each an `app` structure (§2) |
| **BDOS call** | A program's request to the kernel, CP/M style: `CALL #0005` with the command in `C` (`syskrnl.asm`: `jp callbdos` at `#0005`), commands `CMD_*` in `src/_sdk/sysdefs.asm` |
| **Page** | A 16 KB RAM page of the machine; the kernel hands pages to tasks (`CMD_NEWPAGE` / `CMD_DELPAGE`, owner via `CMD_GETPAGEOWNER`) |
| **Main page** | A task's page holding its user-kernel part and stack (`mainpg`) |
| **Build flavor** | The kernel's build settings (`_sdk/syssets.asm`, written by `build_kernel_*.bat`): machine (`atm`), `sys_npages`, `NEMOIDE`, `SYSDRV`, `INETDRV` (network driver), `PS2KBD`, defines such as `NGSSD`, `ATMRESIDENT` |
| **Labels** | The symbol file sjasmplus writes for a kernel build (`user.l` and friends, `src/kernel/Makefile`) |

## 2. What the sources say (2026-09-30, NedoOS rev 44049473 / SVN r2698)

- **Tasks.** 16 slots of `#68` bytes from `#0100` of the system page, each an
  18-byte register frame followed by the `app` structure: `flags`, `id`
  (0 = free), `parentid`, `mainpg`, `stdin`, `stdout`, `stderr`, `lasttime`,
  `border`, `screen`, `gfxmode`, `gfxkeep`, `scr0low/high`, `scr1low/high`,
  `childresult`, `textcuraddr`, `curcolor`, `dta`, `vol`, `dircluster`, a
  directory buffer, `pal[32]` (`syskrnl.asm:140-182`). There is **no name
  field**; the program name comes from the command line at `#0080` of the
  task's main page.
- **Two sets of window registers.** Tasks and the kernel use different ATM
  page register sets (switched by port `#FD`). Inside the kernel, the CPU view
  does not show the task's memory at all.
- **Not preemptive, not reentrant.** An interrupt inside the kernel never
  switches tasks, and there is one kernel stack and one saved SP. A kernel
  call that loops stops the whole OS.
- **Self-modifying.** Much kernel state lives in instruction operands
  (`label=$+1`): current task, focus task, the caller's SP, the handler of the
  current call.
- **Kernel entry.** `#0005` → `callbdos` (with an optional mutex byte before it),
  `#0009` → `sys_getchar`, `#0000+4` → `sys_quit`, `#0010+5` → `fastprchar`.
- **Commands** (`sysdefs.asm`): files (`FOPEN`, `FREAD`, handles, `OPENDIR`,
  `READDIR`, `MKDIR`, `RENAME`, `MOUNT`, `GETFILINFO`, ...), console (`PRCHAR`,
  `PRATTR`, `CLS`, `SETXY`, `GETXY`, `SETCOLOR`, `SCROLLUP/DOWN`, `PUTKEY`,
  `GETKEYMATRIX`), graphics (`SETGFX`, `GETGFX`, `SETPAL`, `GETPAL`,
  `SETBORDER`), memory (`NEWPAGE`, `DELPAGE`, `GETMAINPAGES`,
  `GETAPPMAINPAGES`, `GETPAGEOWNER`, `SETMAINPAGE`, `GETMEMPORTS`), tasks
  (`NEWAPP`, `RUNAPP`, `DROPAPP`, `FREEZEAPP`, `CHECKPID`, `YIELD`,
  `SETWAITING`, `HIDEFROMPARENT`, `GETCHILDRESULT`, `SETSTDINOUT`), time
  (`GETTIME`, `SETTIME`, `GETTIMER`), network (`WIZNETOPEN`, `WIZNETCLOSE`,
  `WIZNETREAD`, `WIZNETWRITE`), sound (`PLAYCOVOX`, `SETMUSIC`), raw sectors
  (`READSECTORS`, `WRITESECTORS`), configuration (`GETCONFIG`, `SETSYSDRV`).
- **Builds.** `release/sd_boot.$C` is the ZX-Evo kernel with the Wiznet driver
  (`build_kernel_evo.bat`: `atm=1`, `sys_npages=192`, `NEMOIDE=1`, `SYSDRV=12`,
  `INETDRV=0x01`, `PS2KBD=1`, `NGSSD`, `ATMRESIDENT`); `sd_bootesp.$C` is the
  ESP variant (`build_kernel_evo_esp.bat`). ATM2 builds differ in `atm` and
  drivers. The layer must identify the flavor, because structure offsets and
  drivers follow from it.
- **Keyboard (PS2KBD=1).** The kernel reads the AVR PS/2 log through the Gluk
  ports and drains it on task switch and app exit (`KEY_PUTREDRAW`,
  `syskrnl.asm:1196`) - see the ZX-Evo PS/2 design.

## 3. Requirements

**MUST** unless marked. Every read is side-effect free; every write or call is
explicit, journaled for TTD, and refused while TTD replays.

### 3.1 Recognition

- **NK-1** Detect that NedoOS runs: the kernel entry at `#0005`, the system page
  layout, a signature of the resident code. Report "not NedoOS" (with why) when
  it does not. Cost nothing until asked or enabled.
- **NK-2** Identify the build: machine flavor, `sys_npages`, `INETDRV`,
  `PS2KBD`, drive and IDE settings, release/revision, from a hash of the kernel
  image matched against known builds, and from constants in the image when the
  build is unknown. The hash masks self-modified operands and variables (the
  kernel rewrites its own code), or uses the kernel file on the boot medium
  (`sd_boot.$C`); a plain hash of the live pages never matches twice.
- **NK-3** Load kernel labels for known builds (bundled under
  `data/symbols/nedoos/<build>/`, generated from the NedoOS sources); fall back to
  signature search for the few anchors the layer needs (task table, page owner
  table, BDOS dispatcher) when the build is unknown. Every answer says whether it
  used labels or signatures. Each label carries its **page role** (pgsys, user
  kernel, pgtrdosfs, pgfatfs2, resident): sjasmplus `user.l` is flat and the
  same address means different things in different pages; the role comes from
  the `--sld` output plus the build's page table (reference §2). A generator
  script builds the symbol set from a NedoOS checkout on macOS / Linux / Windows
  (no IAR: FatFS exports come from `fatfs.exp`).

### 3.2 Tasks

- **NK-4** List the 16 task slots: id, parent, name (first word of the command
  line at `#0080` of the main page; there is no name field), full command line,
  state from `flags` (active, waiting, child finished, graphics) plus the
  derived states **current**, **in kernel** (current task while bank 0 is the
  system page) and **stopped with the rest** (not current while the kernel is
  busy), main page, pages owned, stdin/stdout/stderr (pipe or file), screen and
  graphics mode, border, palette, current volume and directory, child result;
  which task has the screen and keyboard; where each task stopped (PC, SP,
  registers: from the slot frame and the task stack for a waiting task, from the
  CPU for the current one).
- **NK-5** Task events: start, exit (with the result), switch of the foreground
  task, freeze / unfreeze; as a stream and as TTD timeline markers.
- **NK-6** Per-task Z80 view: which pages the task sees at `#0000`, `#4000`,
  `#8000`, `#C000` when it runs (main page + the three window bytes at
  `#44/#4A/#50` of it); translate a task address to a physical page and
  offset without switching tasks. Never through the CPU's current view: inside
  the kernel it shows the system register set (live: the task's stack page read
  as the scratch page).

### 3.3 Memory

- **NK-7** Page map of the whole machine (`sys_npages`): owner (task id,
  kernel, screen, free), with totals per task and free pages; a history of
  allocations (who took and freed which page, when) while tracing is on.
- **NK-8** Read and write memory in a task's address space (NK-6 translation):
  byte ranges, strings, structures described by the NedoOS struct catalog
  ([2026-09-17](../2026-09-17-nedoos-future-support/)).

### 3.4 Console and input

- **NK-9** Console, in three layers, each labeled in the answer:
  (a) the screen of the task with focus, from video memory (the existing
  text-grid reader works: zxdb's 80x25 ATM text screen read back exactly);
  (b) pipe queues from the kernel (`pipebufs`: what is written but not yet
  read); (c) **everything a task printed**, captured by the call trace on
  `WRITEHANDLE` to pipe handles and `PRCHAR`, kept per task as a scroll-back.
  Background console programs (cmd, nv) do not own a screen: they write to a
  pipe that `term.com` draws, and term does not keep its screen when it loses
  focus, so (c) is the only complete source. "Wait until the console shows X"
  with a timeout works on (a) and (c).
- **NK-10** Input to a task: type text and keys through the machine's keyboard
  path (PS/2 on ZX-Evo), or put keys straight into the kernel's queue
  (`CMD_PUTKEY` semantics) when the layer is told to; run a shell command line
  and collect its output.

### 3.5 Kernel calls and control

- **NK-11** Direct kernel calls: perform a BDOS command on behalf of a chosen task
  with given registers and return the result registers. Safe point: the task in
  user mode after its next `YIELD` returns, interrupts on, no task in the
  kernel. The layer pushes the task's PC, calls `#0005`, catches the return by
  task + PC + SP, then restores every register (reference §7.3, done live:
  `GETAPPMAINPAGES` for nv from zxdb's context returned exactly the pages the
  memory reader found). Buffers the call needs go into a page the layer
  borrows with `NEWPAGE` and returns with `DELPAGE`, never into the task's own
  memory. While the kernel is busy there is no safe point: refuse at once with
  the NK-21 report, never wait. The call is a journaled input event, so a TTD
  replay repeats it exactly.
- **NK-12** Task control built on NK-11: run a program (`NEWAPP` / `RUNAPP`),
  stop it (`DROPAPP`), freeze / unfreeze (`FREEZEAPP`), change directory,
  mount, set time.
- **NK-13** Files through the kernel's eyes: open handles per task (owner from
  `FIL.PAD1`, position, size, mode), mounted volumes, current directories. A
  `FIL` holds no name: the name comes from the directory entry at `DIR_SECT` /
  `DIR_PTR` read from the medium (needs a sector read for SD / IDE media), or
  from the `FOPEN` argument when the trace saw the open. Reading and
  writing file *contents* goes through the media layer on the host side
  (FAT reader of the SD / IDE image), with a warning when the kernel holds
  unwritten buffers for that file.

### 3.6 Network

- **NK-14** Sockets per task: handle and owner from the driver's table
  (`w53_socflags`, 8 x 5 bytes for the Wiznet driver; the ESP driver has its
  own), protocol, local / remote address and port and state from the emulated
  adapter (the kernel writes them straight to the chip and keeps no copy),
  bytes in / out from the call trace; the adapter the kernel was built for
  (`INETDRV`) and whether the machine has it, flagged loudly when it does not
  (every Wiznet call then hangs the OS, see NK-21).

### 3.7 Tracing and debugging

- **NK-15** Kernel call trace: every BDOS call with the calling task, command
  name, arguments, result, duration in T-states; filters by task and command;
  counters; export; on the TTD timeline. Enabled explicitly; zero cost when off
  (a gated hook at the kernel entry, not a per-instruction check). The hook is
  page-qualified: `callbdos` in the system page only (the same address is user
  code in every main page); the return is caught at `endsys_result_a` so the
  result and duration are exact. API breakpoints were tried and give about 4
  calls per second: fine for a demo, useless as a trace.
- **NK-16** Debugger integration: break on a kernel call (by command, by task),
  break when a task starts, per-task breakpoints (PC in task X's pages), a
  per-task stack and backtrace, labels for programs when a map file is
  available.

### 3.8 Kernel health

- **NK-21** Kernel-busy report: when the kernel has not returned to any task for
  N frames (default 50), report the task, the command, the caller's PC, the
  arguments the command takes (decoded: socket, buffer, length, the bytes when
  small), the driver routine the CPU spins in, and the ports it polls with
  their values. Raise it as an event, show it in the Qt panel, and include it
  in every task listing while it lasts.
- **NK-22** End a stuck call: make the current kernel call return to its caller
  with the command's error result (for the network calls `A` = error code,
  `HL` = -1), through the handler's own exit so the kernel's state stays
  consistent (live: `PC = wiznet_fail_1` with the return into `callbdos` on
  the kernel stack). A debug action, journaled, refused during replay, and
  never automatic.

### 3.9 Surfaces, versions, tests

- **NK-17** One core API (`NedoOsAnalyzer` in `core/src/debugger/analyzers/nedoos/`)
  and the same operations on CLI, WebAPI + OpenAPI, MCP (an `inspect_state`
  aspect set and a `nedoos` tool), Lua, Python and a Qt panel (tasks, pages,
  console, call trace); docs for each surface and a recipe in `.recipe/`.
- **NK-18** Versions: works on the current release and the minimal and full
  test cards (`testdata/machines/zxevo/nedoos/`); an unknown build degrades to
  what signatures can answer and says so; structures that do not validate are
  refused, never guessed.
- **NK-19** Tests: boot NedoOS headless, list tasks, run a program and read its
  output, kill it, read a page map, trace calls; each under the project's test
  rules (boot-bound tests justified, turbo mode on). The zxdb session of
  2026-09-30 becomes a TTD fixture: the reference §6 printout is its expected
  output (tasks, owners, the busy report, the socket, the DNS bytes), and
  NK-22 on it must leave zxdb showing "No results found".
- **NK-23** Emulator prerequisites (reference §9): both ATM register sets in
  the paging state, page-qualified execution breakpoints on every surface,
  sector reads for SD / IDE media, breakpoints without the manual debug-mode
  step.
- **NK-20 (SHOULD)** ATM Turbo 2+ builds too (same layer, different flavor
  tables), and TS-Conf when NedoOS runs there.

## 4. Use cases

| Who | What |
|---|---|
| AI agent | run a NedoOS program, type into it, wait for output, read the result; drive network apps end to end |
| Program author | why does my app hang / crash: its calls, its pages, its stack; break on `CMD_WIZNETREAD` |
| Kernel developer | page leaks (pages owned by dead tasks), call latency, task switching |
| Regression tests (CI) | boot NedoOS, run a list of programs, compare consoles and exit codes |
| Emulator developer | see which kernel call hit an unemulated device (e.g. the W5300 poll at `#03AB`) |

## 5. Open questions

1. Label source for release builds: build the kernels in CI from the NedoOS
   sources to get labels, or ship labels taken from a local build per release.
   (A local build reproduces the release byte for byte in code; reference §2.)
2. Safe point for injected calls when a task never yields (busy loop): force at
   the next interrupt return, or refuse.
3. Console reading on ATM text modes vs. the kernel's own screen buffer: which
   is authoritative when they differ.
