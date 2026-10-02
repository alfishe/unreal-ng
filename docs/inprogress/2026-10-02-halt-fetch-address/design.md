# The halted Z80's opcode fetches: the byte after the HALT

Bug: [BUGS.md](../BUGS.md) 2026-10-02 #1. Plan: [PLAN.md](../PLAN.md) #81. Every file the fix touches:
[impact.md](impact.md). Found by HALT2INT v3 through
`Halt2Int_Test` (contention backlog C9, [backlog.md](../2026-10-01-contention-backlog/backlog.md)).

## 1. The problem in plain words

`HALT` stops the program until an interrupt, but the Z80 does not stop working. Every 4 clock ticks it makes an
idle opcode fetch: it puts an address on the bus, reads the byte there, throws it away and refreshes the memory.
By the time it halts, its program counter already points past the `HALT`, so those idle fetches go to the
address **after** the `HALT`.

unreal-ng (and its library extract, unreal-z80) re-executes the `HALT` itself: `op_76` steps the program counter
back, and every idle fetch reads the `HALT` byte at its own address.

The difference shows when the `HALT` and the byte after it are in memory of different speed. On a 48K, RAM at
#4000-#7FFF is contended: while the screen is drawn the ULA holds the CPU on every access there.

Worked example, `HALT` at #7FFF:

| | Idle fetches go to | That memory | Each fetch while the picture is drawn |
|:--|:--|:--|:--|
| A real 48K | #8000 | uncontended | no wait |
| unreal-ng today | #7FFF | contended | waits up to 6 ticks |

The waits shift the phase of the 4-tick fetches, so unreal-ng takes the next interrupt a few ticks later. HALT2INT
reads that as `R`:

| Interrupt at T | Real early 48K (photo, published screen) | unreal-ng |
|:--|:--|:--|
| 14335 | R = #43 | #44 |
| 14336 | R = #43 | #44 |
| 14562 (second picture line) | R = #0B | #1C |

With the `HALT` at #BFFF and #FFFF both models agree: the `HALT` and the byte after it are both uncontended (the
byte after #FFFF is the ROM at #0000).

## 2. Evidence and the references

| Source | Idle fetch address | Kind |
|:--|:--|:--|
| HALT2INT v3 on real machines: an early 48K (photo in the release zip), a late 48K (Issue 6A, a video) and a +2 (photos), listed on the [Z80 tests wiki](https://github.com/redcode/Z80/wiki/HALT2INT) | HALT + 1 | hardware |
| HALT2INT's published expected screens, early and late 48K | HALT + 1 | author, from hardware |
| MAME (`src/devices/cpu/z80/z80.lst`: the halted loop does `@rop` at PC, then `PC--`; `halt()` leaves PC on HALT + 1) | HALT + 1 | emulator |
| FUSE 1.6.0 (`z80/opcodes_base.c`: `case 0x76: z80.halted=1; PC--;`) | HALT | emulator |
| Xpeccy, ZXMAK2, SkoolKit (`simulator.py` `halt`) | HALT | emulator |
| unreal-ng (`op_76`: `cpu->pc--`), unreal-z80 (`op_76`; `Z80HaltT` reports `Z80CpuAccessM1` at PC) | HALT | this project |

The hardware decides (project rule: hardware facts by evidence, not by the majority of emulators). The emulators
that fetch the HALT copied the same shortcut; none cites evidence for it.

## 3. What must not change

The fix changes **where the idle fetch goes on the bus**, nothing else that programs or tools can see:

- **The program counter while halted stays on the HALT.** unreal-ng, unreal-z80, FUSE's test vectors
  (`testdata/z80/fuse`, opcode `76`: PC stays 0000 after the HALT), the SNA / Z80 / SZX loaders (a PC that points at
  a `#76` byte means "halted"), the debugger's PC display and the TTD state all use that convention. The interrupt
  and NMI acknowledge keep stepping PC past the HALT before the push (`z80.cpp`, "If CPU halted - unblock it").
- **A halted step stays one step**: the same instruction-start work as today (`m1_pc` = the HALT, the TTD probes,
  the debugger's breakpoint on the HALT, the opcode profiler's `0x76`, Q = 0), so TTD positions, stepping and the
  recordings' structure keep their meaning.
- **The refresh**: R advances once per idle fetch, the refresh address is I:R as today (ULA snow unchanged).
- **The TR-DOS entry trap** is checked on the instruction start's PC (`RunInstructionStartHooks`, #3Dxx), not on
  the bus address; it stays there. (A HALT at #3CFF in the 48K ROM does not exist; with RAM at #0000 the Beta 128
  trap is off.)

## 4. The change

### 4.1 unreal-ng core (`core/src/emulator/cpu/z80.cpp`, `Z80::Z80Step`)

Today the halted CPU runs the full opcode path every step: `m1_cycle()` at PC (the HALT), then `op_76` steps PC back
again. New: a halted step takes its own branch, which costs nothing on the hot path. The existing per-step test
`cpu.vm1 && cpu.halted` is reordered so that `halted` is tested first, and the branch handles both HALT models:

```cpp
if (cpu.halted && !prefixPending) [[unlikely]]
{
    if (cpu.vm1) { /* unchanged: burn 1 T, R every 4th */ }
    else
        HaltedM1();   // the idle fetch at PC + 1
}
else
{
    /* the normal fetch and dispatch, unchanged */
}
```

`HaltedM1()` (out of line, cold) does what `m1_cycle()` + `op_76` do today, with the bus address moved:

| Step | Today (`m1_cycle` + `op_76`) | `HaltedM1` |
|:--|:--|:--|
| Instruction start (`m1_pc`, observers) | `m1_pc = PC` (the HALT) | the same |
| Scorpion Even M1 | on PC | on PC + 1 (the board looks at the fetch's RAM select) |
| R | +1 | +1 |
| Machine M1 hooks (`NotifyMachineM1Before`, `NotifyMachineM1`) | PC | PC + 1 |
| The bus read (`rdM1`: contention, the +2A / +3 latch, ULA snow, host bus overlays) | PC | PC + 1, the byte discarded |
| Opcode for trace / profiler | `0x76` | `0x76` |
| PC after the step | PC (op_76 undoes the increment) | PC, untouched |
| Q | 0 (F unchanged) | 0 |

The memory access tracker: the idle fetch is a bus access, not an executed instruction. Phase 1 checks what the
debug interfaces do with `isExecution` and picks the variant that does not mark HALT + 1 as executed code (the
coverage and the "executed" view would otherwise show a byte that never ran).

`op_76` itself keeps `pc--`: the first execution of the HALT is a normal instruction; only the idle fetches after
it change.

### 4.2 unreal-z80 (the library)

`z80lib/src/z80step.inc` `HaltedQuantum` calls `Z80HaltT`, which reports `Z80CpuAccessM1` at `rf->pc`. New: at
`(uint16_t)(rf->pc + 1)`, in all three bus variants (callback, paged, flat: the flat bus has no hook, no change).
The header comment of `Z80CpuAccessM1` ("addr = PC, i.e. the HALT opcode") changes with it. The library's suites
must stay green: units, z80test, all ZEX, `z80ttrace` against `tests/ttrace-golden.txt` (regenerate only where a
halted quantum is traced, and say so), `z80inttests`, `z80fusetests`, `z80diff-z80ex` with and without `contend`;
plus a `z80bench` A/B (the halt-step microbench included).

### 4.3 Performance

The non-halted path keeps exactly one test of the same byte (`cpu.halted` instead of `cpu.vm1`, both in `cpu`).
A/B per `docs/guidelines/performance-guidelines.md`: `BM_HostFrame_*` (48K, 128K, Pentagon, Scorpion, ATM3)
interleaved A B A B A B B A B A, on a quiet machine (load < 12), plus a halted-heavy frame (a `HALT` loop: most
games idle in `HALT`) to show the cold path is no slower than today's full opcode path.

## 5. What else moves

- **TTD fixture corpus**: timing changes in every recording whose code halts in contended memory; re-record
  (memory note: the corpus is re-recorded after any timing change) and say which fixtures changed.
- **RZX corpus**: the 16 archive recordings must still play to the end without a desync and stay byte-exact where
  they were (RZX frames are counted in fetches; a halted fetch is still one fetch).
- **`CoreGolden` and the contention golden tables**: re-run; only HALT-in-contended-memory cases may move.
- **ctprobe, fusetest, Butler, Rak**: unchanged expected (none halts in contended memory inside a measurement).

## 6. Tests

| Test | Checks |
|:--|:--|
| `Halt2Int_Test.EarlyMatchesTheHardware` | the three pinned deviations go: every line as the real early 48K |
| new `Z80Halt_Test` (host, `core/tests/emulator/cpu/z80halt_test.cpp`) | a HALT at #7FFF on the 48K: each idle fetch is uncontended (#8000) - the T of the interrupt acceptance as computed by hand; a HALT at #3FFF: the fetches at #4000 wait; a HALT at #FFFF: the fetch at #0000 (ROM); PC stays on the HALT, R advances by one per fetch, the pushed return address is HALT + 1 |
| the same on the 128K and the +3 | the 128K's contended page at #C000 (a HALT at #BFFF with page 5 / page 1 at #C000); the gate array's pattern |
| `HALT2INT 128` (`halt2int128.tap`, in the tree) | run on the 128K; no published reference: record the screen in the test's comment as the first measurement |
| unreal-z80 | a `contend`-hook test: the halted quantum reports HALT + 1 |
| full `core-tests`, with `UNREAL_TIMING_SUITES=1` and `UNREAL_RZX_FULL=1` | nothing else moves |

## 7. Phases

1. Check the tracker's `isExecution` path and the TTD instruction-start observers on a halted step (what they see
   today); write `Z80Halt_Test` against today's behavior first (it pins the wrong address), then flip it.
2. unreal-ng: `HaltedM1`, the reordered test; `Halt2Int_Test` loses its deviations; the full suite, the timing
   suites, RZX full; A/B.
3. unreal-z80: `Z80HaltT` at PC + 1, every suite, `z80bench` A/B.
4. TTD fixtures re-recorded; docs: `docs/emulator/design/core/memory-contention.md` (HALT row), test-programs.md
   (M1-10 closed), the contention backlog (C9 done), BUGS.md, PLAN.md.

## 8. Open questions

- The 128K and +2A / +3 have no published HALT2INT reference; the 128K one runs in the tree (phase 2 records it).
  The rule is the Z80's own, so the machines differ only in which addresses are contended.
- The NMI while halted: the same acknowledge path; covered by the host test (an NMI accepted from a HALT at #7FFF).
