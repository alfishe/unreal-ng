# ULA snow: technical design

**Date:** 2026-09-29 · **Requirements:** [requirements.md](requirements.md) · **Research:** [research.md](research.md) ·
**Status:** [TODO.md](TODO.md)

## 1. Overview

```
Z80::m1_cycle -> Z80::rdM1 -> MemIf->MemoryReadM1      (every opcode fetch)
  plain interfaces: the plain read, nothing else
  contended interfaces: Memory::MemoryReadM1Snow: the contended read, then
      if (not the gate array && I in a slow slot) [[unlikely]] -> UlaContention::NoteRefresh(T3, R before the increment)
Z80 INT / NMI acknowledge -> Z80::NoteAcknowledgeRefresh  (once per interrupt)
UlaContention::NoteRefresh: T3's phase in the ULA's 8-tick cycle?
        phase 2 -> snow on the group's first cell  (bits 6..0 of the addresses from R)
        phase 4 -> double on the group's second cell
        marks[y][cell] = { kind, R, frame }
ScreenZX::DrawRangeZX / RenderScreen_Batch8    (when a cell's bytes are latched)
  if (the frame has marks) -> UlaContention::SnowFetch(y, cell, pixels, attributes)
UlaContention::FetchedByte                    (the floating bus)
  the same SnowFetch, so IN from an unused port sees what the ULA fetched
```

## 2. When snow can happen (R1)

`UlaContention` knows the machine: the Ferranti ULA models are the rules `Ula48` and `Ula128` (16K, 48K, 128K, +2);
the gate array and the clones never snow. Snow is part of the ULA's memory sharing, so it follows the
`contention` feature: switched off, there is neither contention nor snow.

The opcode fetch goes through a third entry of the memory interface, `MemoryReadM1` (`Z80::rdM1`). On the plain
interfaces it is the plain read. Only the contended interfaces, which `Core::SelectMemoryInterface` selects while
a machine's contention is in effect (48K, 128K, +2, +2A, +3), wrap it with the snow check
(`Memory::MemoryReadM1Snow`), which skips the gate array. So the Pentagon, the Scorpion and every other clone
run exactly the instructions they ran before (section 6). The interrupt acknowledges, once per interrupt, test
`Z80::ioContention` (set exactly on the Ferranti ULA machines).

`NoteRefresh` then checks `I`: snow needs the slot `I` points to (`I >> 6`) to be slow
(`UlaContention::IsSlotContended`): #40-#7F on every Ferranti model, #C0-#FF on the 128K / +2 with an odd page.

## 3. The tick (R2, R3, R4, R6)

- Every refresh counts: `m1_cycle` (opcodes, prefixes, the HALT's own M1 through `op_76`), the INT acknowledge
  and the NMI acknowledge.
- `R` is the value **before** this M1's increment (the refresh address; bits 6..0). unreal-ng advances `r_low`
  at the start of the M1, so the hook passes `r_low - 1`.
- The time is the refresh's **T3**. In `m1_cycle`, after the opcode read (`rd` has added any contention and
  T1-T3), `_cpu->t` is the start of T4, so T3 is `t - 1`; the INT acknowledge's T3 is its fifth tick, the NMI's
  its third.
- The phase comes from the floating bus's own arithmetic (`LocatePaperFetch`, shared with `FetchedByte`): with
  the fetch lead, `tInPaper % 8` is 2 for pixel byte 1, 3 attribute 1, 4 pixel byte 2, 5 attribute 2. **T3 at
  phase 2 = snow, at phase 4 = double.** The phase is the one Butler's hardware-measured floating-bus tests 36
  and 37 validated; the tick and `R` were fixed on the Snow Hold photos ([research.md](research.md) section 6).
  The two phases are named constants next to the code.
- The group's cells are `2 * (tInPaper / 8)` (snow) and `+1` (double), on screen line `y`.

## 4. The marks

A per-frame table in `UlaContention`: one entry per screen cell (192 x 32), each `{ kind, r, frame }` with
`frame` = `emulatorState.frame_counter` when it was noted. An entry from another frame is empty, so nothing is
cleared between frames. `_snowFrame` remembers the last frame with a mark, so the renderer tests one integer
per latched cell and nothing at all in frames without snow.

## 5. Applying the marks (R5)

`SnowFetch(y, cell, pixels, attributes)`, given the normally fetched bytes:

- snow: `pixels = screen[(pixelOffset & ~#7F) | (r & #7F)]`, `attributes = screen[(attrOffset & ~#7F) | (r & #7F)]`,
  offsets relative to the screen's start, within the active screen bank;
- double: the second cell takes the first cell's bytes, as fetched (snowed or not).

Users: `ScreenZX::DrawRangeZX` (the renderer used while emulating), `RenderScreen_Batch8` (the whole-frame batch
path), and `UlaContention::FetchedByte` (the floating bus).

The renderer draws a cell after the ULA fetched it (the fetch lead), and the CPU notes the refresh at the fetch
tick, before the beam reaches the cell, so a mark is always in place before its cell is drawn.

## 6. Performance

A first version tested `ioContention` and the slot in `m1_cycle` itself. The A/B measurement
(`BM_HostFrame_*`, docs/guidelines/performance-guidelines.md section 4) showed +1 to +2 % on machines that do not
snow (the Scorpion, and the Pentagon with the test out of line): the extra code in the hottest function, not the
branch. The design moved the check into the contended interfaces, so the other machines' opcode fetch is
unchanged; the measurements are recorded in [TODO.md](TODO.md).

## 7. What is not modeled (R7)

- The 128K / +2 with `I` in #C0-#FF: the snowed bytes come from the active screen bank here; Snow128N shows the
  colour of the bank at `I`, which needs the DRAM bank layout of the 128K. Open.
- The crash some 128K machines show under snow.
- TTD: the marks are rebuilt when a frame is re-executed; a seek into the middle of a frame renders its first
  part without the marks of the part before the checkpoint.

## 8. Tests (R8-R10)

| Level | Test |
|:--|:--|
| Invariant | `UlaSnow_Test` (core/tests/emulator/video/ulasnow_test.cpp): an `LD A,0` whose T3 falls on the tick the floating bus reads a cell's pixel byte snows that cell with `R` before the increment; on the next cell's pixel byte a double; other ticks nothing; a sweep with a positive control (48K, `I` = #40) and the negatives (`I` in fast memory or #3F, contention off, +3, Pentagon); the 128K with `I` = #C0 only with an odd page |
| Hardware | `UlaSnowRender_Test.SnowHoldMatchesTheHardwarePhotos`: the Snow Hold beta (testdata/contention/snow-hold) loaded from tape on the 48K renders its ladders at columns 2 and 16 under all three bands, one lit line every 4 lines, nothing else (AC1) |
| Test program | `SnowTest_Test` (snowtest_test.cpp): snowtest's LIVE band renders pixel-exactly as its EXPECTED band on the 48K and the 128K, again one frame later (no drift); plain on the +3 and the Pentagon (AC2, AC4); the committed files match the source; opt-in (`UNREAL_TIMING_SUITES=1`): loaded from tape / TR-DOS as a user does |
| No change elsewhere | contention fingerprints, ctprobe, the whole suite (AC3) |

### Dropped: an analytic check through the floating bus (R9)

Snow Spy (Woodmass) reads snow back through the floating bus. In this model it cannot see it: the ULA takes
`R` only for the first pixel / attribute pair of the group whose fetch the refresh meets, and an `IN` reads the
bus at least 6 ticks after its own refresh (`IN A,(#FF)`: refresh T3 at +2, lookup at +8) or 3 ticks after the
second M1's (`IN A,(C)`: T3 at +6, lookup at +9, which is attribute 2's tick, not snowed). No instruction can
refresh on pixel byte 1's tick and read the bus on it or on attribute 1's. Snow Spy has no published hardware
results, so there is nothing to anchor such a check on. The numbers the requirement asked for are in the visual
test instead: it prints the columns and characters it predicts.
