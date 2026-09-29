# Phase E3 — ZX-Evo board NMI and M1 breakpoint

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Done. Plan: [implementation-plan.md](implementation-plan.md) phase E3 |
| **Gaps closed** | C-2 (the `#xxBE` exit now works), C-3 (NMI), C-4 (breakpoint), P-8 (the dead NMI page path) of [gap-analysis.md](gap-analysis.md) |
| **Design** | [tdd-evo-control-and-avr.md](tdd-evo-control-and-avr.md) §5.1 ("As built") |

## 1. What the board does (`fpga/base_trdemu/trunk/z80/znmi.v`, `zbreak.v`)

| Step | Hardware | Emulator |
|---|---|---|
| Request | `#BF` bit 3 falling edge or the AVR (PrintScreen / Magic) → `pending_nmi`, released at the next `int_start`; the breakpoint (M1 at `#10BD/#11BD` with `#BF` bit 4) fires immediately. No new NMI while `in_nmi` | `nmiAtIntStartPending` promoted in `Z80::ProcessInterrupts` inside the INT pulse (also while halted); breakpoint compare in the M1 hook; `OnFrameIntStartNmi` vetoes while in the NMI page |
| Entry | the FPGA drives `#00` (NOP) for the `#0066` fetch; `in_nmi` at that M1's refresh maps RAM `#FF` into `#0000-#3FFF` | `OnNmiAccepted`: the Z80 charges the NOP (4 T, R + 1), continues at `#0067`; RAM `#FF` mapped |
| Exit | `OUT (#xxBE)` → `clr_count = 3`, decremented on each M1 refresh; `in_nmi` clears after the second M1 | `pBE = 2`, decremented in the M1 hook after each opcode fetch: with `OUT (#BE),A : RETN` the RETN (ED 45) still comes from `#FF`, its stack pops use the restored map |
| DOS | `ram_exec_stb` uses the **programmed** window type | `IsDosLeavingBank`: the NMI page over a ROM window keeps TR-DOS on |
| Only board NMIs switch pages | `in_nmi_2` only from `nmi_start` | a raw `/NMI` (e.g. the debugger's plain NMI) stays a Z80 NMI at `#0066` of whatever is mapped |

## 2. Tests

| Test | Pins |
|---|---|
| `ZXEvoNmi_Test.BfEdgeNmiWaitsForIntThenEntersPageFF` | no NMI outside the INT pulse; entry at `#0067`, 11 + 4 T, R + 2, RAM `#FF`, return address pushed |
| `ZXEvoNmi_Test.ExitAfterTwoM1sRetnRunsFromPageFF` | `OUT (#BE) : RETN` returns through the NMI page, map restored |
| `ZXEvoNmi_Test.ExitCountsM1sNotInstructions` | `OUT (#BE) : NOP : RET` — both run from `#FF` |
| `ZXEvoNmi_Test.BreakpointNmiIsImmediateAndStaysArmed` | immediate NMI away from the INT, twice |
| `ZXEvoNmi_Test.MagicButtonIsIntSynchronized` | `Emulator::RequestMNI` on ZX-Evo waits for the INT |
| `ZXEvoNmi_Test.NoNestedBoardNmi` | vetoed while in the NMI page, request consumed |
| `ZXEvoNmi_Test.PlainNmiDoesNotSwitchPages` | raw /NMI: `#0066`, no page switch |
| `ZXEvoNmi_Test.DosStaysOnInsideTheNmiPage` | programmed-ROM window keeps DOS on; programmed-RAM closes it |
| `ZXEvoErs_Test.MagicButtonEntersNmiPageAndReachesMagicService` | real ROM: Magic on the ERS menu maps RAM `#FF`, the ERS leaves it through `#BE` and waits for a key in its MAGIC Service (ROM page 23, `EI : HALT` at `#281B`) |
| `TtdAtmPaging_*` (updated) | NMI state and `pBE` in the blob, hashed |

## 3. Performance

The M1 hook costs one pointer test per M1 (the call is out of line), the INT-synchronized NMI one
flag test per instruction boundary. Measured against HEAD `320321a0` in isolated worktrees, same
build flags, three interleaved rounds of 5 repetitions on a loaded machine: `BM_Frame_PureCPU`
+0.5 %, `BM_Z80_DecodeOverhead_NOP` −1.0 %, `DD_Prefix` −1.3 %, `MixedInstructions_Block` −2.8 %, with
a base-against-itself spread of 6-15 %: no measurable change.

An earlier +45 % reading on `BM_Frame_PureCPU` was a benchmark defect, fixed alongside: the frame
fixture loaded `testdata/loaders/sna/action.sna` relative to the working directory and ignored the
result, so without the file it silently measured an idle ROM. It now resolves the file through
`BenchmarkPathHelper::RequireTestDataFile` and ends the run with exit code 1 (`FATAL: benchmark setup
failed: ...`) when the file is missing or does not load.

## 4. Verification (2026-09-28)

Isolated worktree = HEAD `320321a0` + only the E3 files: `core-tests` **4030 passed, 0 failed**, zero
compiler warnings. Main tree: full build clean, 4038 tests passed.
