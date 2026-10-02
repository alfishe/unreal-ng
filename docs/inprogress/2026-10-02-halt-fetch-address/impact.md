# What the HALT fix touches, and where

Companion to [design.md](design.md): every place in this repository (and in the unreal-z80 library repository) that
the fix changes, that sees the new fetch address without a code change, or that must be re-checked. Counted on
master `15f0a2f6` (2026-10-02).

## Summary

| Area | Files changed | Size | Behavior change |
|:--|:--|:--|:--|
| A. unreal-ng Z80 core | 2 (`z80.cpp`, `z80.h`) | ~50 lines: the reordered halted test, a cold `HaltedM1()` | the fix itself |
| B. Code that sees the new address on its own | 0 | - | yes: contention, overlays, machine hooks (listed below) |
| C. Sprinter's Z84C15 core (vendored fork) | 2 (`opcodes-callback.cpp`, `README.md`) | ~5 lines | yes: the Sprinter's halted fetches |
| D. General Sound's unreal-z80 (vendored) | 4 (`opcodes-callback.cpp`, `opcodes-paged.cpp`, `z80cpu.h`, `README.md`) | ~8 lines, a resync from the library | none (no contention hook installed) |
| E. The unreal-z80 library (its own repository) | 4-5 (the same sources, `int-contention-test.cpp`, `diff-z80ex.cpp` comment, README) | ~15 lines | yes, for hosts with a contention hook |
| F. unreal-ng tests | 3 changed, 1 new, ~10 re-checked | ~150 lines | expectations flip where they pinned the old address |
| G. Documentation | 6-8 files | text | - |
| H. Not changing | loaders, TTD format, automation, interrupt code | - | - |

In all: about **10 production source files** across the two repositories, of which **3 carry the actual logic
change** (unreal-ng's `z80.cpp`, the Z84C15 `opcodes-callback.cpp`, the library's `opcodes-callback.cpp` /
`opcodes-paged.cpp` pair, mirrored into the vendored copy), **4 test files**, and the docs.

## A. The unreal-ng Z80 core (the fix)

| File | Change |
|:--|:--|
| `core/src/emulator/cpu/z80.cpp` | `Z80::Z80Step`: the per-step test `cpu.vm1 && cpu.halted` becomes `cpu.halted` first (same byte, no extra test on the normal path); its non-`vm1` side calls `HaltedM1()`. New out-of-line `Z80::HaltedM1()`: instruction-start bookkeeping as today (`m1_pc` = the HALT), Even M1 checked on PC + 1, R + 1, `NotifyMachineM1Before` / `rdM1` / `NotifyMachineM1` at PC + 1, the byte discarded, opcode `0x76` for the trace / profiler, Q = 0. Likely shares a helper with `m1_cycle()`'s bus half: the helper must stay inlined into `m1_cycle()` (A/B) |
| `core/src/emulator/cpu/z80.h` | the `HaltedM1()` declaration |
| `core/src/emulator/cpu/op_noprefix.cpp` | `op_76` unchanged (its `pc--` keeps the "PC on the HALT" convention); its comment names the idle fetch |

The rest of the halted handling stays: the INT and NMI acknowledge step PC past the HALT before the push
(`z80.cpp` "If CPU halted - unblock it", two places), `haltpos`, `halt_cycle`, `tstates_halted_current`, the `vm1`
model (unused).

## B. Code that sees the new address without a change

These receive the idle fetch at PC + 1 instead of PC through the existing calls. No edit, but their behavior
while halted changes, and each needs a test or a re-run:

| Code | Called through | What changes |
|:--|:--|:--|
| Contended memory interfaces (`core/src/emulator/memory/memorycontended.cpp`: the 48K / 128K Ferranti ULA, the +2A / +3 gate array, ULA snow) | `rdM1` | the wait of each idle fetch (the fix's purpose); the +2A / +3 floating-bus latch holds the byte after the HALT; snow is unchanged (the refresh address is I:R) |
| Memory access tracking (`memory.cpp`, 11 `isExecution` sites; `memorywaitoverlay.cpp`, `hostbusoverlay.cpp`) | `rdM1` with `isExecution = true` | without care, HALT + 1 counts as executed code on every halted step; design §4.1 phase 1 picks the variant that does not |
| `EvoTurboOverlay` (`core/src/emulator/memory/atm/`, ZX-Evo at 14 MHz) | `onReadM1` | the code-word cache is filled with the word of HALT + 1: a HALT on an even address now hits the HALT's own word, on an odd one fetches the next word; the 14 MHz halted waits follow the RTL |
| `ScorpionTurboOverlay` (`core/src/emulator/memory/scorpion/`) | `onReadM1` | the opcode-fetch slot wait is decided by RAM select and time: unchanged unless the HALT is the last byte before ROM |
| Machine M1 hooks: `PortDecoder_ATM3` (ZX-Evo: TR-DOS emulation page swap, the NMI exit), `PortDecoder_TSConf`, `PortDecoder_Sprinter` (through the Z84C15 engine, C) | `NotifyMachineM1Before` / `NotifyMachineM1` | the hook sees PC + 1; a HALT on the last byte before a trapped range would now trigger it, as on the hardware |
| Scorpion Even M1 (`z80.cpp`) | the halted branch | the RAM select of PC + 1 decides (a HALT at #3FFF with ROM at #0000 waits on RAM at #4000) |

The Beta 128 TR-DOS entry (`RunInstructionStartHooks`, #3Dxx) is keyed on the instruction start's PC and stays
as it is.

## C. The Sprinter: Z84C15 (vendored fork, `core/src/3rdparty/z84c15/`)

The Z84C15 core already models the halted fetch as a real read (the board sees and stretches it), at PC.

| File | Change |
|:--|:--|
| `opcodes-callback.cpp` | `Z84HaltT`: the M1 at `(uint16_t)(rf->pc + 1)` |
| `README.md` | the "Local changes" row "A halted CPU reads the byte at PC" becomes "the byte after the HALT" |
| (host) `core/src/emulator/io/z84c15/z84c15engine.cpp` | none: it passes the address on to `MemoryReadM1`, the machine hooks and `Z84WaitM1` |

The chip's wait generator (`z84waits.cpp`, `Z84WaitM1(addr, ...)`) then decides the halted waits from the right
address.

## D. General Sound: unreal-z80 (vendored, `core/src/3rdparty/unreal-z80/`)

The GS / NeoGS coprocessor host installs no contention hook, so the halted quantum's reported address changes
nothing it does. The copy is resynced from the library after E so the two do not drift:

| File | Change |
|:--|:--|
| `opcodes-callback.cpp`, `opcodes-paged.cpp` | `Z80HaltT` reports `Z80CpuAccessM1` at `pc + 1` (`opcodes-flat.cpp` has no hook: unchanged) |
| `z80cpu.h` | the `Z80CpuAccessM1` comment ("addr = PC, i.e. the HALT opcode") |
| `README.md` | the source commit / version line |

## E. The unreal-z80 library (its own repository)

The same three source changes as D, plus:

| File | Change |
|:--|:--|
| `tests/int-contention-test.cpp` | `HaltedQuantumContended` expects `{HALTQ, 0x6001}`; a new case with the HALT on the last contended byte (#7FFF) where the halted quantum is uncontended |
| `tests/diff-z80ex.cpp` | the "allowed" note on halted steps (z80ex fetches the HALT at PC) mentions the address difference |
| `tests/ttrace-golden.txt` | expected unchanged (no addresses, no contention in the trace); regenerate only if a halted case moves, and say why |
| `README.md` / `docs` | the HALT row of the behavior notes |

Every suite green (units, z80test, all ZEX, `z80ttrace`, `z80inttests`, `z80fusetests`, `z80diff-z80ex` with and
without `contend`) and a `z80bench` A/B.

## F. unreal-ng tests

| Test | Change |
|:--|:--|
| `core/tests/emulator/video/contentionprobe_test.cpp` `Halt2Int_Test` | `Halt2IntKnownDeviations` emptied: every line as the real early 48K |
| new `core/tests/emulator/cpu/z80halt_test.cpp` | the idle fetch address and its waits on the 48K (#7FFF / #3FFF / #FFFF), the 128K (#BFFF with a contended page at #C000), the +3; PC, R and the pushed return address |
| `core/tests/3rdparty/z84c15/z84cpu_test.cpp` `HaltedCpuReadsItsM1AtPc` | flips: the M1 events at #8001; renamed after what it checks |
| HALT2INT 128 (`testdata/contention/halt2int-v3/halt2int128.tap`) | new case on the 128K (no published reference: its first result recorded) |
| Re-run, expected unchanged | `int_test.cpp`, `nmi_test.cpp` (return past the HALT), `rzxsession_test.cpp` (`HaltCountsOneFetchPerCycle`: still one fetch per idle cycle), the GS / NeoGS boot and clock tests, `sprinterreference_test.cpp` (its HALT stub), the ZX-Poly group, `machinestatehash_test.cpp`, the TTD tests |
| Re-run, may move | `CoreGolden`, the contention goldens, the TTD fixture corpus (re-record where a recording halts in contended memory), the RZX corpus (`UNREAL_RZX_FULL=1`: no desync, byte-exact where it was), the timing suites (`UNREAL_TIMING_SUITES=1`) |

## G. Documentation

| File | Change |
|:--|:--|
| `docs/emulator/design/core/memory-contention.md` | a row: the halted idle fetch at PC + 1 |
| `docs/inprogress/2026-09-28-m1-contention/test-programs.md` | M1-10 closed by HALT2INT |
| `docs/inprogress/2026-10-01-contention-backlog/backlog.md` | C9 done |
| `docs/inprogress/BUGS.md`, `docs/inprogress/PLAN.md` | the bug fixed (developer marks it), #81 retired |
| `docs/inprogress/2026-09-24-core-performance/unreal-ng-core-perf-and-gating-review.md` | the `op_76` note (a halted step no longer runs the full opcode path) |
| `core/src/3rdparty/z84c15/README.md`, `core/src/3rdparty/unreal-z80/README.md` | as in C and D |
| this folder | `TODO.md` phases, then `DONE.md` |

Historical analyses that quote the old code (`2026-09-15-frame-budget-triage/06-overrun-root-cause-analysis.md`,
`2026-08-09-video-timing-analysis-mister-vs-emulator/19-io-port-write-tstate-placement.md`) stay as written. The
latter cites the MiSTer T80 core's halted M1 (`T80.vhd`): worth reading for the address it drives, as one more
reference before the fix.

## H. Not changing

| Code | Why |
|:--|:--|
| Snapshot loaders: SNA (`loader_sna.cpp`), Z80 (`loader_z80.cpp`), SZX (`loaderszx.cpp`), ZXP (`loaderzxp.cpp`) | "PC on a `#76` byte means halted": the PC convention is kept |
| TTD checkpoint format (`ttdcheckpoint.*`, `ttd.ksy`) | stores `halted` as before; no new state |
| Automation: WebAPI `profiler_api.cpp`, CLI `cli-processor-analysis.cpp`, Lua / Python emulator bindings | report `halted` and the halted T-states; meaning unchanged |
| `core/src/emulator/zxpoly/zxpolygroup.cpp`, `core/src/emulator/platform.h` | the halted flag only |
| The INT / NMI acknowledge, `op_76`'s `pc--` | the PC convention is kept |
