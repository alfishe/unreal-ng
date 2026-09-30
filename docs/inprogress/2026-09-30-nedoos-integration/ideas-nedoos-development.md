# Ideas: faster, better NedoOS development with unreal-ng

**Status:** ideas for discussion, 2026-09-30. Nothing here is scheduled yet.
Folder index: [README.md](README.md). Builds on the NedoOS layer
([requirements-nedoos-layer.md](requirements-nedoos-layer.md), NK-1..NK-23), the
kernel reference ([nedoos-kernel-reference.md](nedoos-kernel-reference.md)), the
bugs we found ([nedoos-bugs.md](nedoos-bugs.md)), the network design
([tdd-network.md](tdd-network.md), [tdd-network-debugging.md](tdd-network-debugging.md))
and the developer toolchain program (PLAN #51,
[2026-09-21-devtools-roadmap](../2026-09-21-devtools-roadmap/unreal-ng-developer-toolchain-design.md)).
NedoOS sources quoted from the GitHub mirror (`alfishe/NedoOS`, rev 44049473 /
SVN r2698); paths below starting with `src/` or `tools/` are in that tree
unless they say otherwise.

---

## 0. In one page

Today a NedoOS program is written on Windows, built with batch files, copied
onto an SD image and tried by hand on a real machine or an emulator. When it
hangs, the author has no view of what the OS is doing. There are no unit
tests, no automatic builds, and nothing that says whether a program is safe to
ship. The emulator already has most of the parts to change that: a debugger
with page-aware breakpoints, time-travel debugging (TTD), coverage, a fast
standalone Z80 core, host folders as SD cards, five automation surfaces, and,
being designed now, a layer that understands NedoOS tasks and kernel calls.

The ideas, in seven groups:

| # | Group | The one-line pitch |
|---|---|---|
| 1 | [App debugger](#3-nedoos-application-debugger) | Debug one task by its own addresses and source lines while the rest of the OS keeps running; rewind it |
| 2 | [Profiler](#4-profiling) | Exact T-state cost per task, function and line, kernel time and waiting time split out, flame graphs; repeatable to the T-state |
| 3 | [C to fast asm](#5-from-c-to-fast-assembly) | Measure, then rewrite only the hot C functions in assembly, and prove the new version does the same thing |
| 4 | [Unit tests](#6-unit-tests-for-apps-and-libraries) | Library tests in milliseconds on the bare Z80 core; app tests inside a booted NedoOS; network tests against local fake servers |
| 5 | [CI/CD](#7-cicd) | Build kernel and apps on Linux / macOS, boot them headless, test, publish SD images, symbols and size / speed charts every night |
| 6 | [Certification](#8-app-and-driver-certification) | A checklist and an automated suite: no hang without hardware, no leaks, works under memory pressure and on each kernel flavor |
| 7 | [Other speed-ups](#9-other-speed-ups) | Build-and-run in seconds, AI agents over MCP, generated SDK docs, templates |

The order of work is in [§10](#10-roadmap), the decisions we need in [§11](#11-open-questions).

## 1. Glossary

| Term | Meaning here |
|---|---|
| **BDOS call** | A program asking the kernel for a service: `ld c,CMD : call #0005` (CP/M style). Commands are in `src/_sdk/sysdefs.asm` |
| **Task** | One running program; NedoOS has 16 slots (reference §4.1) |
| **Page** | A 16 KB block of RAM. A task sees four pages at a time, one in each 16 KB window of the Z80's 64 KB |
| **Physical page** | The real RAM page number, independent of which window shows it. The same Z80 address in two tasks is two different physical places |
| **Symbols / debug info** | Files the assembler or compiler writes that map addresses to names and source lines: sjasmplus `.l` / `.sld`, IAR link map, SDCC `.cdb` / `.map` |
| **SLD** | sjasmplus "source level debug" file (`--sld`): every emitted byte range with its source file, line and compile page |
| **T-state** | One Z80 clock tick. Instruction cost is exact in T-states; this is the unit of every speed number below |
| **TTD** | Time-travel debugging: the emulator records a session and can go back to any instruction in it (docs/emulator/design/debugger/time-travel-debug/) |
| **Standalone Z80 core** | POC 017 (`tools/poc/017-z80-standalone-cpu/` in the unreal-ng main checkout): the emulator's Z80 as a small library, passes ZEXALL, about 1-2.8 billion T-states per second |
| **Differential test** | Run two implementations on the same inputs and compare the results |
| **Fuzzing** | Differential or crash testing with many generated random inputs |
| **Flame graph** | A picture of where time goes: each bar is a function, its width is its share of time, callers below callees |
| **Golden output** | A saved "known good" result (console text, screen) that later runs are compared with |
| **Kernel flavor** | One kernel build configuration: ZX-Evo with the Wiznet (W5300) driver, ZX-Evo with the ESP driver, ATM Turbo 2, ... (reference §2) |

## 2. Where we start

### 2.1 How NedoOS is built today

| Part | Language and tool | Where | Notes |
|---|---|---|---|
| Kernel | Z80 asm, sjasmplus | `src/kernel/main.asm`, flags written to `src/_sdk/syssets.asm` by `build_kernel_*.bat` | One `.bat` per flavor (`build_kernel_evo.bat`, `..._evo_esp.bat`, `..._atm2*.bat`) |
| FatFS in the kernel | C, **IAR** | `src/fatfs4os/` | Not buildable without IAR; its entry points are in `fatfs.exp` (the symbol POC uses that, `tools/poc/020-nedoos-layer/generate-symbols.sh` in unreal-ng) |
| System apps (`cmd`, `nv`, `term`, ...) | Z80 asm, sjasmplus | `src/cmd`, `src/nv`, `src/term`, ... | Common include rules in `src/_sdk/common.mk`; Linux build via `src/Makefile` (`make evolution`), see `src/README.linux` |
| Network and utility apps (33 programs: `zxdb`, `dns`, `gopher`, `svnesp`, `updater`, ...) | C, **IAR `iccz80` (Wine on Linux)** | `src/kapps/*/Makefile` → `include ../../kapps/iarlib/iar.mk` | Flags `-ml -s7 ...` (large model, optimized for speed); `iar/` is **not in the repository**, so nobody without an IAR license can build them |
| C runtime for kapps | C + asm (IAR) | `src/kapps/iarlib/` (`oscalls.asm`, `cstartup.asm`, `socket.asm`, `tcp.asm`, ...) | Link script per app (`lnk.xcl`): code `#0100-#FF00`, `CSTACK+274` (zxdb's stack is 274 bytes) |
| Shared libraries | C, **included as source** | `src/kapps/common/network.c`, `esp-com.c`, `espnet.c`, `ini.c`, `terminal.c` | `#include <../common/terminal.c>` (zxdb `main.c:6`); each app compiles its own copy, so a library fix means rebuilding every app (upstream commit message: "network software rebuilt with the new libraries") |
| Games | C, **SDCC** (Evo SDK) | `src/games/_sdk/_compile_nedoos.bat` | `sdcc -mz80 --opt-code-size --nogcse ...` |
| Future C toolchain | SDCC 4.2.0 source tarball | `tools/src/sdcc/` | `tools/Makefile:17`: "SDCC - will add in the future" |
| Native languages | NedoLang, BDS C | `src/nedolang/`, `src/bdsc/` | Built and run on NedoOS itself |
| Release | batch files | `src/mk*.bat`, `src/makeall.bat`, `src/netbuild.bat` | Binaries land in `release/` (committed); `tools/dmimg` puts files into `us/sd_nedo.vhd`; `build.bat` then starts `us/emul.exe` |
| CI | GitHub Actions, sync only | `.github/workflows/sync-svn.yml`, `scripts/sync-svn.py` | Nightly SVN → Git mirror. `rsync --delete` excludes only `.svn`, `.git`, `.github`, `scripts`: **any folder we add on the mirror is deleted by the next sync** unless it is added to that list |
| Tests | CPU exercisers only | `src/ztst/` (zexall, zexdoc, z80test), `src/kapps/dhrystone` | No app or library tests |
| On-target tools | Z80 monitor / debugger | `src/z80/` (`debugger.asm`, `disasm.asm`) | Runs inside NedoOS; sees one machine state, no source |

### 2.2 Pain points

| Pain | Effect |
|---|---|
| C apps need IAR (proprietary, Windows, not in the repo) | No open or automatic build of a third of the programs; libraries cannot be fixed by others |
| Libraries copied by `#include` of `.c` | Every app carries its own, possibly old, copy; a fix does not reach binaries until each is rebuilt by hand |
| No view into the running OS | A hang like zxdb's (reference §6) looks like "the computer froze"; the cause (a driver loop with no timeout, bug B-2) took a prototype layer to find |
| No per-app debug info in use | Debugging a C app means reading raw Z80 at unknown addresses in unknown pages |
| No tests, no CI beyond sync | Regressions are found by users; binaries are committed without a check |
| Size and speed are guessed | 64 KB minus the kernel page is a hard ceiling (zxdb.com is already 33 375 bytes); nobody sees which function is slow or big |
| Hardware needed to try network code | Without a ZXNETUSB card or ESP module the W5300 kernel hangs (B-2) |

### 2.3 What unreal-ng already has

| Piece | Where (unreal-ng) | Use below |
|---|---|---|
| Page-qualified breakpoints (exec / read / write) | `core/src/debugger/breakpoints/breakpointmanager.h` (`AddExecutionBreakpointInPage`, ...) | Per-task breakpoints; not yet on WebAPI / MCP (reference §9) |
| Labels with bank info, `.sym` / `.map` loaders; sjasmplus listing parser | `core/src/debugger/labels/labelmanager.h`, `core/src/debugger/listing/` | Symbol loading; no SLD reader yet |
| Coverage analyzer, opcode profiler | `core/src/debugger/analyzers/coverage/`, `core/src/emulator/cpu/opcode_profiler.h` | Starting point for the profiler (both are 64 KB flat today) |
| Analyzer framework | `core/src/debugger/analyzers/` (`ianalyzer.h`, `analyzermanager.h`) | Home of `NedoOsAnalyzer` (NK-17) and the profiler |
| TTD with coverage index, reverse search, port search, bookmarks | `core/src/debugger/ttd/` (`ttdcoverageindex`, `ttdportsearch`, `ttdbookmarks`) | Rewind an app; deterministic profiling by replay |
| Per-step hook with zero cost when unused | `Z80::machineStepHook` (PLAN #60a) | Exact profiling, call tracking |
| Host folders as SD / IDE volumes, `media rescan` | `core/src/emulator/io/storage/hostfolder/hostfolderfat.h`, `docs/features/media.md` | Build output visible to NedoOS without making an image |
| Headless NedoOS boot and typing in tests | `core/tests/emulator/machines/zxevo/zxevo_ers_test.cpp` (`NedoOsShellRunsATypedCommand`) | Base of the app test runner |
| Standalone Z80 core | POC 017 (main checkout) | Library unit tests and equivalence checks at ~300x real time |
| GDB stub and DeZog server | `core/automation/gdb/`, `core/automation/dezog/` | Source-level debugging from VS Code; tasks as threads (devtools §14) |
| Co-emulation harness | `tools/verification/coemu/` | Pattern for "same program, several runners, one table" |
| Automation surfaces | `core/automation/` (CLI, WebAPI, MCP, Lua, Python) | Every tool below is reachable from scripts and AI agents |
| NedoOS layer prototype | POC 020 (`tools/poc/020-nedoos-layer/`, main checkout) | Tasks, pages, pipes, sockets, files, trace, kernel calls, unstick |
| Designs to reuse | debug-info model and OS descriptors (devtools design §8, §14); edit-and-replay (§13.1); struct DSL ([2026-09-17-nedoos-future-support](../2026-09-17-nedoos-future-support/)); expression evaluator (PLAN #6, not built) | Referenced per idea |

## 3. NedoOS application debugger

Goal: an author debugs **their program**, not the machine. They name the task,
see its source, step it, and the rest of NedoOS keeps working (the terminal
redraws, the network driver runs).

| ID | Idea | Today | What we build | Plugs into | Effort | Value |
|---|---|---|---|---|---|---|
| DBG-1 | **Per-task breakpoints** | Page-qualified breakpoints exist in the core only | "break at `zxdb:fillTable`": the layer translates the task address to a physical page (NK-6) and sets a page-qualified breakpoint; rebinds when the task changes a window (`SETMAINPAGE`, window bytes `#44/#4A/#50`) or exits | `breakpointmanager`, NK-6, NK-16 | S | High: the base of everything else |
| DBG-2 | **Stepping one task while others run** ("non-stop" mode) | Stepping stops the whole machine | Step = a temporary task-qualified breakpoint at the next line / instruction / return, then run; other tasks and interrupts keep going. Step over a BDOS call = break on return by task + PC + SP (the NK-11 procedure) | DBG-1, NK-11 | M | High: pausing the whole machine breaks timing-dependent code (network, UART) |
| DBG-3 | **Symbols per app, loaded when the app starts** | Kernel symbols only (POC 020) | On task start (NK-5) take the program name from the command line at `#0080`, look for `<name>.sld` / `.l` / `.map` / `.cdb` next to the `.com` (same host folder) or in a symbol path; check a hash of the loaded image against the build (devtools §8.3 "staleness"); bind to the task's pages | `labelmanager`, devtools debug-info model §8.1 | M | High |
| DBG-4 | **Source-level debugging of asm** | Listing parser for one `.lst`, flat 64 KB | SLD reader (sjasmplus `--sld`, already used by POC 020): line tables per compile page, bound to runtime pages by DBG-3 | devtools §8, VS Code via DAP (#51) | M | High for asm apps and the kernel |
| DBG-5 | **Source-level debugging of C** | Nothing | IAR: function addresses from the link map (`-l obj/cout.html`, `-xehinms`) and line-to-code from the compiler listings (`-L`, `-A`); to verify on a real build. SDCC: `.cdb` gives lines, locals, globals and types directly (reason to prefer SDCC, §5.2) | DBG-3, devtools §8.1 `types[]` | M (IAR) / S (SDCC) | High |
| DBG-6 | **Watch expressions on task memory** | Memory views of the CPU's current mapping | `task(zxdb).limiter.total`, `*(char*)task(zxdb).netbuf@64`: evaluated through the task's windows even while the CPU is inside the kernel; C types from DBG-5, asm structures from the struct DSL | expression evaluator (PLAN #6), NK-8 | M | High |
| DBG-7 | **BDOS call trace per task** | POC: 4 calls/s via API breakpoints | The NK-15 core hook, shown per task with decoded arguments and results; "break on `CMD_WIZNETREAD` in task zxdb" | NK-15, NK-16 | S after NK-15 | High |
| DBG-8 | **Rewind an app (TTD)** | TTD rewinds the whole machine | Task-aware reverse commands: "back to the previous line executed **by zxdb**" (reverse-continue filtered by task: bank-0 page = its main page), "when did zxdb last write `limiter.second`" (write search limited to the task's physical pages), task start / exit / BDOS calls as timeline markers (NK-5) | `core/src/debugger/ttd/` (coverage index, reverse search), NK-5 | M | High: most C bugs are "a value went wrong earlier" |
| DBG-9 | **Crash and hang diagnosis** | NK-21 designed (kernel busy) | Per-task watchdogs: task stack below its reserved area (`CSTACK` size from `lnk.xcl`), jump into a page the task does not own or into data, `RST #38` loop, no `YIELD` for N frames while it has focus, write into a page owned by another task (devtools §14.2). Each produces a dossier: last N source lines (TTD), call stack, last BDOS calls, pages, a short TTD clip | NK-21, TTD clip export (`ttdclipexport.cpp`) | M | High: turns "it froze" into a bug report |
| DBG-10 | **Tasks as threads in VS Code / GDB** | GDB stub reports one thread | Thread list from the task table; select a task to see its saved registers (reference §4.2) | `core/automation/gdb/`, devtools §14 | M | Medium (after #51) |

**Worked example: "zxdb shows garbage in the Year column".**

1. `nedoos debug zxdb` (CLI) attaches: DBG-3 finds `zxdb.cdb` beside `zxdb.com`
   in the SD host folder and binds it to task 6's pages.
2. `watch task(zxdb).table[0].year`; `break zxdb:fillTable if limiter.total == 5`
   (DBG-1, DBG-6).
3. The breakpoint hits; the terminal task still redraws (DBG-2).
4. `reverse-find write task(zxdb).limiter.second` (DBG-8) lands on
   `findLimiters` at `main.c:1666`; the reply buffer shows a missing `^`
   separator. Fix is in the parser, found without a single print statement.

## 4. Profiling

Goal: know where the time goes, per task, per function, per line, and whether a
change made it faster, to the T-state.

A key property: on an emulator the cost of every instruction is exact, and a
TTD recording replays bit-exactly. **Profiling a replay gives the same numbers
every time**, so "before vs after" comparisons have no noise: a 0.5 %
improvement is real if the number moved.

| ID | Idea | Today | What we build | Plugs into | Effort | Value |
|---|---|---|---|---|---|---|
| PRF-1 | **Exact per-task T-state accounting** | Opcode profiler counts opcodes, 64 KB flat | A profiler analyzer on the per-step hook: each instruction's T-states go to (task, physical page, PC). Task = current `appaddr` (reference §3); zero cost when off | `machineStepHook`, analyzer framework | M | High |
| PRF-2 | **Kernel time vs app time vs waiting** | Nothing | Split each task's time into: own code; kernel **on its behalf** (bank 0 = pgsys, `CMD_*` from NK-15); **waiting** (in `YIELD`, or in a known device poll loop such as `w53_cmd0`, `esp-com.c` UART reads); interrupt handler; idle task | NK-15, kernel symbols | S after PRF-1 | High: shows "zxdb spends 70 % waiting for the UART" vs "70 % in its own parser" |
| PRF-3 | **Per-function and per-line cost** | Nothing | Map (page, PC) through DBG-3/4/5 to function and line; "self" and "total" time | debug info | S after DBG-3 | High |
| PRF-4 | **Call graph and flame graphs** | Nothing | Shadow call stack **per task** (each task has its own stack; switch stacks on task switch): `CALL`/`RST` push, `RET` pops when SP matches, tolerate `jp (hl)` tricks and stack switching. Export folded stacks (flamegraph.pl, speedscope), callgrind (KCachegrind / qcachegrind), Chrome trace (timeline with task switches and BDOS calls) | PRF-1 | M | High |
| PRF-5 | **Sampling mode** | Nothing | Record (task, page, PC, stack top) every N T-states instead of every instruction: cheap enough to leave on during interactive use; exact mode (PRF-1) for measurements | PRF-1 | S | Medium |
| PRF-6 | **Profile from a TTD recording** | TTD replay exists | Run the profiler on a replay of a recorded session, several times with different settings, with no effect on the recording; profile "frames 1200-1900 only" | TTD | S | High: record once, analyze many ways |
| PRF-7 | **Hot loops** | Nothing | Count backward jumps per (page, target): the top 20 loops with trip counts and cost per iteration; the input for §5 | PRF-1 | S | High |
| PRF-8 | **Memory and page use over time** | NK-7 designs page history | Page count per task over time, peak, leaks at exit; heap / buffer use when the runtime's allocator is known (IAR `malloc` in `clz80.r01`) | NK-7 | S | Medium |
| PRF-9 | **I/O profile** | Port trace (PLAN #60g) | Per task: ports touched, bytes per device, time between request and answer (network latency seen by the app) | port trace, tdd-network-debugging §3 | S | Medium |

**Worked example: profile zxdb's result parser.** A search reply arrives as one
text buffer with fields separated by `^`. `fillTable` (`src/kapps/zxdb/main.c:1672`)
reads 12 rows x 6 fields; each field calls `findLimiters`, which calls `pos`
twice; `pos` (`main.c:757`) first runs `strlen` over **the whole buffer** and
then searches from `startPos`. That is 144 full-buffer scans per reply. For a
2 KB reply at ~30 T-states per byte (a C byte loop), that is about 9 million
T-states: 0.6 s at 14 MHz, 2.5 s at 3.5 MHz, before any real work. This is a
hypothesis from reading the source; the profiler settles it:

1. Record a TTD session of one search against a local fake server (§6.3), so
   the reply is always the same.
2. `profile --task zxdb --from marker:search-start --to marker:table-drawn`
   (PRF-6): self time by function; expected top entry `pos`, caller
   `findLimiters`.
3. Fix the algorithm first (keep the running position, stop at the next `^`
   with `strchr`): O(n) instead of O(n x fields). No assembly needed.
4. Replay the same recording with the new binary (edit-and-replay, devtools
   §13.1): the T-state total before and after is exact and goes into the
   speed dashboard (CI-6).

## 5. From C to fast assembly

Principle: **measure, fix the algorithm, try the compiler, and only then
hand-write assembly, one hot function at a time, with the C kept as the
reference and a machine check that both do the same.**

### 5.1 The pipeline

```
profile (§4) ──> hot function list ──> algorithm fix? ──> compiler options / compiler choice
                                                          │
                                                          ▼
                                   asm rewrite (by hand or AI-assisted, OPT-5)
                                                          │
                                                          ▼
               equivalence harness (OPT-6): same inputs → same outputs, same side effects
               + T-states and size compared ──> both versions kept, asm used by default
```

### 5.2 Ideas

| ID | Idea | Today | What we build | Effort | Value |
|---|---|---|---|---|---|
| OPT-1 | **Hot list from the profiler** | Guessing | PRF-3 / PRF-7 output as a ranked list: function, share of time, calls, size in bytes; the only input to the steps below | S | High |
| OPT-2 | **Compiler options, measured** | IAR `-s7` everywhere; SDCC `--opt-code-size --nogcse` for games | A matrix build of a few apps (`dhrystone`, `dns`, `zxdb`) with option sets (IAR speed vs size levels; SDCC `--opt-code-speed`, `--max-allocs-per-node`, `--sdcccall 1` register arguments, `--reserve-regs-iy`), recording size and T-states from CI-6. Cheap, often 10-20 % | S | Medium |
| OPT-3 | **SDCC port of the kapps** | 33 apps on IAR, `iar/` not in the repo; SDCC 4.2.0 source in `tools/src/sdcc/` | A thin `iarlib` replacement for SDCC (`oscalls`, `cstartup`, sockets, console) with the same C API (`oscalls.h`, `tcp.h`, `osfs.h`); build three apps both ways and compare size / speed. Wins: open build for CI, `.cdb` debug info (DBG-5), custom peephole rules | L | High (unblocks CI of C apps) |
| OPT-4 | **Peephole rules from real code** | Nothing | Collect repeated instruction patterns in the compiled output of all apps (e.g. 16-bit compares, `ld a,(hl) : inc hl` loops, pointer increments); write them as SDCC peephole rules (`--peep-file`), prove each with OPT-6 on the affected functions | M | Medium, applies to every app |
| OPT-5 | **AI-assisted rewrite of one function** | Nothing | An MCP-driven agent task: input = the C function, its compiled asm, its profile, the calling convention of the compiler, the contract (below); output = an asm version. The agent runs OPT-6 itself and iterates until it passes and is faster; a human reviews. The C stays in the repo as the reference | M | High, if gated by OPT-6 |
| OPT-6 | **Equivalence harness** (`z80equiv`) | Nothing; POC 017 core exists | See §5.3 | M | High: makes every rewrite safe |
| OPT-7 | **Shared fast library** | Each app includes its own copy of `network.c` etc.; string functions from the compiler runtime | One NedoOS C library (strings: `strlen`, `strchr`, `strstr`, `memcpy`, `memset`; number formatting; small `printf` (the apps already swap in `_medium_write`, `zxdb/lnk.xcl`); network helpers: header parsing, DNS, `tcpRead` loops) built once as a linkable library, with an asm implementation per hot function, C reference and OPT-6 contract per function | M | High: one fix reaches all apps |
| OPT-8 | **Size budget per app** | None | CI fails when an app grows past its budget or its code + data + stack cross `#FF00` (`lnk.xcl`); size per function in the dashboard (CI-6) | S | Medium |

### 5.3 The equivalence harness

What it runs: two builds of one function (C reference, asm candidate), each
linked into a tiny test image with a harness stub, on the **standalone Z80
core** (POC 017, flat 64 KB, ~1 billion T-states per second), or, for code
that needs the OS, inside NedoOS in the emulator (§6.2).

A **contract** file per function says what "the same" means:

```yaml
# lib/net/dnsbuildquery.equiv.yml
function: dns_build_query          # C: src/kapps/common/network.c:60
abi: sdcc-sdcccall1                # or iar-clz80; the harness knows where args and results live
args:
  - name: domainName
    kind: cstring
    gen: {charset: "a-z0-9.-", len: [1, 126], also: ["a..b", "x.", ".x", "<63 x a>.ru"]}
  - name: domainLng
    derive: strlen(domainName)
result: u16                        # packet size
outputs:
  - memory: netbuf[0 .. result]    # the packet must be byte-identical
  - memory: netbuf[256 .. 256+128] # label scratch area (network.c: "labels at netbuf[256..]")
may_clobber: [AF, BC, DE, HL, IX]  # registers the ABI lets the callee change
must_preserve: [IY, SP]
bdos: none                         # no kernel calls allowed
budget: {tstates_max: 12000, bytes_max: 160}
```

How it checks:

| Mode | When | How |
|---|---|---|
| Vectors | always | the `also:` list plus cases from bug reports |
| Fuzz | always | N random inputs from `gen` (N = 100 000 takes seconds on the standalone core); failing input shrunk to a minimal one |
| Exhaustive | inputs of 16 bits or less | all 65 536 inputs (a 1 000 T-state function: 65 M T-states, well under a second) |
| Side effects | always | every memory write outside `outputs` and the stack is a failure; BDOS calls through a recording stub must match in order and arguments |
| Symbolic (research) | later | translate both versions to logic formulas over registers and memory (Z80 semantics from the core's opcode tables) and ask a solver for a differing input; only for short, loop-free functions |
| Cost | always | T-states min / mean / max over the inputs, size in bytes, both versions side by side |

**Worked example: port `dnsResolve` to assembly and prove it.**
`dnsResolve` (`network.c:415`) mixes pure work and kernel calls. Split it:

1. `dns_parse_ipv4` and `dns_build_query` are pure (string in, bytes out). Port
   them first; the contract above checks them by fuzzing: every generated
   name gives the same packet and size, and the asm is, say, 4x faster and
   40 % smaller.
2. `dnsResolve` itself calls `OpenSock`, `OS_WIZNETWRITE_UDP`, `YIELD` and
   reads the answer. Its contract uses a **scripted BDOS stub**: scenarios
   "answer at once", "EAGAIN twice then answer", "EMSGSIZE", "no answer in
   `DNS_RECV_TRIES` tries", "truncated datagram" (cf. bug B-1). Both versions
   must make the same sequence of kernel calls with the same arguments and
   return the same result and `targetadr` in every scenario.
3. Last, the whole app with the asm version runs its app tests (§6.2) on both
   kernel flavors.

## 6. Unit tests for apps and libraries

Three levels, from fastest to most realistic. A program author writes tests at
the lowest level that can show the bug.

| ID | Level | Runs on | Speed | Good for |
|---|---|---|---|---|
| TST-1 | **Library tests** | Standalone Z80 core + **BDOS stub** (`nedoos-sim`) | milliseconds | string, parsing, protocol code; the OPT-6 harness is the same machinery |
| TST-2 | **App tests** | Full emulator, ZX-Evo, booted NedoOS, headless | ~0.1-1 s per test from a boot snapshot | a `.com` doing its job: console I/O, files, exit code, resources |
| TST-3 | **Network tests** | TST-2 + virtual network with local fake servers | seconds | clients (zxdb, gopher, girc, updater) end to end, faults |
| TST-4 | **Golden output** | TST-2 / TST-3 | - | console transcripts and screens compared with saved ones |

### 6.1 Library tests on the bare core (TST-1)

- **What exists:** POC 017's `tests/hostmachine.{h,cpp}` already runs CP/M-style
  programs (ZEXALL) with a soft ROM on a flat 64 KB machine.
- **What we build:** `nedoos-sim`, a host-side BDOS: `CALL #0005` is trapped
  and served in C++: console output to a buffer, `GETKEY` from a script, files
  from a host folder, `NEWPAGE` / `DELPAGE` from a page array, `YIELD` as a
  no-op with a counter, network calls from a script (as in §5.3), time from a
  fixed clock. Unknown commands fail the test loudly. Command table generated
  from `src/_sdk/sysdefs.asm` and the argument comments in `sys_h.asm`
  (reference §5).
- **How tests look:** either a test `.com` (test functions + a tiny runner,
  built with the library) or direct calls from Python: set registers and
  memory, call a symbol, run until return or a T-state budget, assert
  (devtools §13.4). Effort M; value high.

### 6.2 App tests in NedoOS (TST-2)

- **What exists:** `NedoOsShellRunsATypedCommand` boots NedoOS from a host
  folder and types `free`; POC 020 reads tasks, console, pages.
- **What we build:**
  - **Boot snapshot**: boot once per kernel flavor, save the machine state at
    the shell prompt; every test starts from it in milliseconds instead of a
    boot (boot is the slowest part; project test rules allow boot-bound tests
    only with a reason).
  - **Deploy**: the built `.com` goes into the SD host folder (`HostFolderFat`);
    `media rescan` rebuilds the volume. A rescan under a mounted FatFS is
    unsafe, so the test resumes from the snapshot after the rescan, or remounts
    through a direct kernel call (NK-11, `MOUNT`).
  - **Run and observe**: start the program with arguments (NK-12), type
    (NK-10), wait for console text with a timeout (NK-9), read the exit code
    (`childresult`), then run the resource checks of CRT-3.
  - **A test file per app**, next to its sources:

```yaml
# src/kapps/dns/tests/dns.nedotest.yml
app: dns.com
flavors: [evo-w5300, evo-esp]
network:
  hosts: {example.test: 10.0.2.50}
tests:
  - name: resolves a name
    run: "dns example.test"
    expect_console: "10.0.2.50"
    expect_exit: 0
    budget_frames: 250
  - name: no network card
    machine: {network: none}
    run: "dns example.test"
    expect_console_any: ["error", "not found"]
    expect_no_hang: true          # fails on a kernel-busy report (NK-21)
```

- **Runner:** first a Python tool over the WebAPI growing out of POC 020
  (it already has the pieces); later a core-native runner (C++, same file
  format) for speed and for CI without a GUI build. Output: JUnit XML, a
  console transcript and, on failure, a TTD clip. Effort M; value high.

### 6.3 Network tests (TST-3)

- The virtual network's **hosts table** points real names at local servers
  (tdd-network.md §5.2); tests start small Python servers on `127.0.0.1`: a
  fake zxart search API (fixed `^`-separated replies), an HTTP file server,
  an IRC echo server, a DNS that answers slowly.
- **Fault injection** (tdd-network-debugging.md §9): delay, drop, refuse,
  reset in the middle of a transfer, throttle to 1 KB/s, DNS failure; each
  fault is a journaled event, so a failing run replays exactly.
- The **pcapng** capture of every network test is kept as an artifact: a
  failing test comes with its Wireshark trace. Effort M (after network N0-N1).

### 6.4 Golden output (TST-4)

Console transcripts (NK-9 layer c: everything the task printed, not just the
last screen) and text-mode screens (the existing text-grid reader) are
compared as text; graphics apps by screen digest with a saved PNG for humans.
`--update-golden` rewrites them after a reviewed change. Effort S.

## 7. CI/CD

| ID | Idea | Today | What we build | Effort | Value |
|---|---|---|---|---|---|
| CI-1 | **Build on Linux / macOS** | `src/Makefile` builds the asm apps on Linux; kernel flavors only via `.bat`; C apps need IAR | A make target (or script) per kernel flavor that writes `syssets.asm` like `build_kernel_*.bat` (POC 020 `generate-symbols.sh` already does this for Evo W5300 / ESP); sjasmplus from `tools/src/sjasmplus`; FatFS from `fatfs.exp` as today; always `--sld` and `.l` output for symbols | S-M | High |
| CI-2 | **C apps in CI** | IAR only | Choice (open question 1): (a) a self-hosted runner with a licensed IAR under Wine; (b) the SDCC port (OPT-3); (c) meanwhile, test the committed binaries in `release/` without rebuilding them | a: S, b: L, c: S | High |
| CI-3 | **SD image without an image** | `dmimg` into `us/sd_nedo.vhd` on Windows | Mount the built `release/` folder straight as the SD card (`HostFolderFat`); also produce an `.img` for real hardware with `tools/src/dmimg` or `mkfs.fat` | S | High |
| CI-4 | **Boot and test headless** | Manual | For each flavor: boot, snapshot, run TST-2 / TST-3 suites and the certification suite (§8); a matrix: Evo W5300 (`sd_boot.$C`), Evo ESP (`sd_bootesp.$C`), later ATM2 (NK-20) | M | High |
| CI-5 | **Artifacts** | `release/` committed | Per build: SD folder zip and image, kernel and app symbols (`.l`, `.sld`, maps, `.cdb`), test reports (JUnit), console transcripts, pcapng of network tests, TTD clips of failures (small enough to attach, devtools §13.1) | S | High |
| CI-6 | **Size and speed dashboard** | Nothing | Per app per commit: size in bytes (and per function), T-states of benchmark scenarios (dhrystone, zxdb parse of a fixed reply, `dns` resolve, a file copy); deterministic numbers, so any change is real; a static page with charts, alarms on regressions | M | High |
| CI-7 | **Nightly on the GitHub mirror** | `sync-svn.yml` syncs SVN nightly | A second job after the sync: build, boot, test, publish (CI-1..CI-6); results as a status page and an issue per new failure, since upstream works in SVN and cannot be gated by PR checks. Our files must be added to the `rsync` excludes in `scripts/sync-svn.py`, or live in a separate repo that pins the mirror revision | M | High |
| CI-8 | **Bisect by revision** | Manual | When a test starts failing, bisect over the mirror's commits (one per SVN revision) with the same runner; report "broke in r2699 (commit ...)" | S | Medium |
| CI-9 | **unreal-ng headless runner** | Tests run inside `core-tests`; no stand-alone headless tool for other repos | A small CLI binary (built from `unrealng::core`, no Qt) that runs a `.nedotest.yml` suite; released as a download for the NedoOS CI | M | High (needed by CI-4 outside our repo) |

Pipeline for one night:

```
svn sync ──> build kernels (per flavor) + asm apps [+ C apps] ──> symbols
   ──> SD folder ──> unreal-ng headless: boot + snapshot per flavor
   ──> TST-2/3 suites + certification suite ──> reports, artifacts, dashboard
   ──> issue on new failure (with transcript, pcapng, TTD clip) ──> bisect
```

## 8. App and driver certification

A **checklist** a program or driver must pass, checked automatically where
possible, with **levels** so authors can climb step by step. Every automatic
check uses the NedoOS layer; nothing needs changes to the program.

### 8.1 Checks

| ID | Check | How it is tested | Why (evidence) |
|---|---|---|---|
| CRT-1 | Starts, does its job, exits with a result | TST-2: run, expected console, `childresult` | basic |
| CRT-2 | **Never hangs the OS** | run every feature with the device missing (no network card, no ESP, no GS, no mouse); fail on a kernel-busy report (NK-21) or a focused task not yielding for N frames | bug B-2: one missing card stops everything |
| CRT-3 | **Cleans up on exit** | page map, open files, sockets and pipes before vs after, per task (NK-7, NK-13, NK-14); also when killed with `DROPAPP` in the middle of work | bug B-3 (socket leak on close) |
| CRT-4 | Esc / Break works | inject Esc during long operations; the app must react within N frames | zxdb could not see Esc during the hang |
| CRT-5 | **Memory pressure** | before start, take pages with direct `NEWPAGE` calls (NK-11) until K remain; the app must fail cleanly or work, never crash | 16 KB pages are scarce with several apps open |
| CRT-6 | Stays in its own memory | no write into pages owned by another task or the kernel (page-ownership watch, DBG-9) | no memory protection on the Z80 |
| CRT-7 | Stack within bounds | stack watermark vs the reserved `CSTACK` size | zxdb reserves 274 bytes |
| CRT-8 | **Network faults** | TST-3 fault suite: DNS failure, refused, reset mid-transfer, 1 KB/s link, truncated / odd datagrams, zero-length writes | bugs B-1, B-4; `network.c` comments on 10 s stalls that "looked like a hang" |
| CRT-9 | Works on every kernel flavor it claims | same suite on Evo W5300, Evo ESP, ATM2 (NK-20) | the ESP and Wiznet paths differ (`INETDRV`) |
| CRT-10 | Console behaves | output through a pipe (`app > file`, run under `term` and without focus), 80x25 and other text modes, CP866 names | background tasks write to pipes (reference §4.5) |
| CRT-11 | Size and speed budgets | CI-6 numbers under the app's declared budget | keeps the dashboard honest |

**Drivers** (kernel network, disk, sound drivers) add:

| ID | Check | How |
|---|---|---|
| CRT-D1 | Every wait is bounded | static: list the driver's poll loops from the SLD + disassembly and flag loops without a counter or `sys_timer` check (the B-2 fix pattern); dynamic: CRT-2 with the device absent and with a device that stops answering mid-command (a fault mode of the emulated chip) |
| CRT-D2 | Device detection | the kernel reports "no device" instead of hanging (B-2 fix step 1) |
| CRT-D3 | Register-level conformance | the bus decoders (tdd-network-debugging.md §4) check the driver's access sequences against the chip's rules; warnings are failures |
| CRT-D4 | Edge cases from the bug list | zero-length write (B-4), close with TX pending (B-3), non-default TX memory (B-5), odd datagram lengths (B-1) |
| CRT-D5 | Cost | T-states per byte transferred, kernel time per call (PRF-2) |

### 8.2 Levels

| Level | Name | Passes |
|---|---|---|
| 1 | **Runs** | CRT-1, CRT-3, CRT-6, CRT-7 on one flavor |
| 2 | **Robust** | Level 1 + CRT-2, CRT-4, CRT-5, CRT-10 |
| 3 | **Network-ready** (network apps and drivers) | Level 2 + CRT-8 (+ CRT-D1..D4 for drivers) |
| 4 | **Portable** | Level 2 or 3 on every claimed flavor (CRT-9) + CRT-11 |

Each app gets a small manifest (`<app>.cert.yml`: flavors it claims, devices
it uses, budgets) and a generated badge line in the release listing, e.g.
`zxdb 1.0 - level 3 on Evo W5300, level 2 on Evo ESP`. Effort: M for the
suite once TST-2 / TST-3 exist; value high, because it turns the bug list in
[nedoos-bugs.md](nedoos-bugs.md) into checks that stop the same bugs coming back.

## 9. Other speed-ups

| ID | Idea | Today | What we build | Effort | Value |
|---|---|---|---|---|---|
| DX-1 | **Edit, build, run in seconds** | Build → `dmimg` into a VHD → start the emulator → navigate | `nedoos run zxdb` : build, copy into the SD host folder, resume from the boot snapshot, start the program, attach the debugger with symbols (DBG-3) | S-M | High |
| DX-2 | **Hot reload of an app** | Nothing | Safe version: stop the task (`DROPAPP`), reload, start with the same command line (NK-12). Strong version: edit-and-replay (devtools §13.1): replay a recorded session with the new binary and show the first difference. In-place patching of a running task only for code-only changes with an unchanged data layout, refused otherwise | S / M | High |
| DX-3 | **AI agent workflows over MCP** | MCP tools for the machine; NedoOS layer planned on MCP (NK-17) | An agent that builds, deploys, runs tests, reads console and BDOS traces, profiles, proposes a fix or an asm rewrite (OPT-5), and proves it (OPT-6); a recipe in `.recipe/` for each loop | M | High |
| DX-4 | **Generated SDK reference** | `src/_sdk/api_base.txt`, `api_net.txt` (Russian, CP866, hand-written); argument comments in `sys_h.asm` | Generate one API reference (Markdown / HTML, English and Russian) from `sysdefs.asm` + `sys_h.asm` comments + `iarlib/oscalls.h`; check that the C wrappers match the asm macros (same command numbers and registers); the same table feeds `nedoos-sim` and the BDOS trace decoder | S | Medium |
| DX-5 | **Project templates** | `src/emptyapp`, `src/hello` (asm) | `nedoos new myapp --lang c|asm --net`: sources, Makefile, test file, cert manifest, `.vscode` launch config | S | Medium |
| DX-6 | **VS Code integration** | DeZog works for flat programs | The #51 program (DAP, language server) with NedoOS awareness: tasks as threads, per-app symbols, BDOS trace view | L | High (long term) |
| DX-7 | **Shared library as a real library** | `#include "../common/*.c"` | One versioned library build (OPT-7) linked by all apps; a changelog; CI rebuilds and retests every app that uses a changed function | M | High |
| DX-8 | **Crash reports from users** | Nothing | Users of unreal-ng press one key to export the DBG-9 dossier + TTD clip; authors open it in their emulator and rewind | S after DBG-9 | Medium |

## 10. Roadmap

| Phase | What | Why first |
|---|---|---|
| **0 (prerequisites, planned)** | NedoOS layer core (NK-1..NK-16, NK-21..NK-23); network N0 + N1 (virtual network, W5300) | Everything below reads tasks, pages and kernel calls through the layer; network apps are the main users |
| **1 (fast wins)** | DBG-1, DBG-3 (asm symbols from `.l` / `.sld`), DBG-7; TST-2 runner in Python over the WebAPI with boot snapshots and host-folder deploy (DX-1); CRT-1..CRT-7 as the first certification checks | Uses pieces that exist or are in phase 0; gives authors a debugger and a test runner within weeks; finds B-2-class bugs automatically |
| **2 (see the time)** | PRF-1..PRF-4, PRF-6, PRF-7; the zxdb parser example as the acceptance test; DBG-8 task-aware rewind | Measurement comes before any optimization; deterministic replay makes it exact |
| **3 (automatic nights)** | CI-1, CI-3, CI-4 (asm + kernel flavors + committed C binaries), CI-5, CI-6, CI-7, CI-9; TST-3 network suite with fake servers; CRT-8 | Stops regressions reaching users; the dashboard shows the effect of phase 4 |
| **4 (faster code)** | TST-1 `nedoos-sim`; OPT-6 equivalence harness; OPT-7 shared library with the first asm functions (strings, `dns_build_query`); OPT-2 option matrix | Safe optimization needs the harness; the shared library spreads each win to all apps |
| **5 (open C toolchain)** | OPT-3 SDCC pilot on 3 apps → decision; DBG-5 C source debugging; OPT-4 peephole rules; OPT-5 AI-assisted rewrites | Biggest effort; worth it only after data from phase 4 and the upstream's answer on compilers |
| **6 (long term)** | DX-6 VS Code (#51), DBG-10, symbolic equivalence, CRT-D1 static loop checker | Depends on #6 expressions, #23 event push and the devtools daemon |

## 11. Open questions

Each with the answer we recommend.

1. **Which C compiler for NedoOS apps going forward: IAR (today) or SDCC?**
   Recommended: keep IAR for releases for now; run the SDCC pilot (OPT-3) on
   `dns`, `sleep` and `zxdb` and decide on measured size, speed and effort.
   An open compiler is the only way to build all apps in CI.
2. **Where do tests, cert manifests and CI files live, given that the nightly
   sync deletes unknown folders on the mirror?** Recommended: a `ci/` folder
   on the mirror added to the `rsync` excludes in `scripts/sync-svn.py` (the
   script is ours); per-app test files move next to the sources only once
   upstream accepts them into SVN.
3. **Do we propose tests, fixes and asm versions upstream?** Recommended: yes,
   starting with the bug reports in `nedoos-bugs.md` and the B-2 fix, then
   the certification results; ask before sending large changes such as a
   compiler switch.
4. **Test runner: Python over the WebAPI or C++ in the core?** Recommended:
   Python first (grows from POC 020, available now), the same test file
   format later run by the core-native headless runner (CI-9) for speed.
5. **Accept AI-written assembly?** Recommended: only through the equivalence
   harness (vectors + fuzz, exhaustive where the input is small) and human
   review; the C version stays in the repo as the reference and a build switch
   selects it for debugging.
6. **Which kernel flavors in the first CI matrix?** Recommended: Evo W5300
   and Evo ESP (both run on our ZX-Evo today); ATM2 once NK-20 lands.
7. **Certification: public badges or an internal checklist?** Recommended:
   internal for the first release cycle, public once the suite is stable and
   upstream agrees.
