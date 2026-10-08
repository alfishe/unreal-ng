# z80test IN tests and the idle EAR level (2026-10-08)

## Symptom

After the M1 / internal-cycle contention work (`add624c20`, PLAN #61) the z80test program
(Patrik Rak, `testdata/loaders/sna/z80full.sna`) failed 9 of 160 tests on every machine:

```
095 IN A,(N)   096 IN R,(C)   097 IN (C)   098 INI   099 IND
100 INIR       101 INDR       102 INIR->NOP'          103 INDR->NOP'
Result: 009 of 160 tests failed.
```

The in-tree vector suite (`Z80TestVerification.*`) stayed green.

## Cause

Not the M1 change. The failures are the same on the Pentagon, which has no ULA contention, and
they go away with the M1 code untouched once the port value is fixed.

`62782b699` (2026-09-10) changed `Tape::handlePortIn`: with no tape playing, the EAR bit (#FE
bit 6) became HIGH on every machine, also with no tape image loaded. The Scorpion's ProfROM
monitor needed that (its tape-port check reads #FFBE and treats bit 6 = 0 as "no signal",
error #61). Before, the level was LOW with no image loaded.

z80test's CRCs are taken on hardware, where #FE reads #BF (bit 6 = 0) in these tests. Every IN
test folds the value read into its CRC, so all 9 failed. The same commit regenerated the
in-tree golden CRCs of `IN R,(C)` and `IN (C)` for the HIGH level, so the vector suite stopped
catching the problem. Those vectors check only F, and for INI..INDR F is the same with #BF and
#FF, so they never saw it.

## Fix

The idle EAR level now depends on the board (`core/src/emulator/io/tape/tape.cpp`, region "Idle
EAR level"):

| Board | Idle EAR (no playback) | Why |
|:--|:--|:--|
| 48K, 128K, +2 (Ferranti ULA) | bit 4 of the last #FE write | Issue 3: the EAR output feeds back into the input |
| Scorpion, Scorpion + ProfROM | HIGH | ProfROM's tape-port check (error #61) |
| Everything else | LOW with no tape image, HIGH with one | The behavior before 2026-09-10 |

Tests:
- `TapeIdleEar_Test.*` (fast, runs by default): the three rows above through the port decoder.
- `Z80TestVerification.*`: `IN R,(C)` / `IN (C)` back on Rak's hardware CRCs (`0x61F21A52`,
  `0x8F4B242F`).
- `Models/Z80TestProgram_Test.DISABLED_Z80Full/*` (on demand): the whole z80full program per
  model, screen read through OCR. See the recipe below.

## Results per model (z80full, after the fix)

| Model | Result | Note |
|:--|:--|:--|
| 48K, 128k, PLUS2, PLUS2A, PLUS3, PENTAGON, ATM710, PROFI, PROFI3, TSL | all 160 passed | In the harness |
| SCORPION, PROFSCORP | 9 IN tests fail | EAR pulled up by design (ProfROM) |
| ATM3 | 9 IN tests fail | #FE bit 5 reads 0 (`zports.v`: `{1'b1, tape_read, 1'b0, keys_in}`) |
| ATM450 | INI, IND, INDR->NOP' fail | #FE bit 7 is the PAL marker, a function of the T-state since INT, so the result depends on timing |
| SPRINTER | not run | Refuses snapshots (`testdata/loaders/golden/commit-digests.txt`: `refused`) |

All failures in the table come from the board's #FE read, not from the CPU. These models are left
out of the harness.

## Recipe: run z80test fast (core-tests, no GUI)

The quickest path, about 5 s per model in turbo:

```bash
tools/build/build.sh core-tests
cmake-build-agent-release/bin/core-tests --gtest_also_run_disabled_tests \
    --gtest_filter='*Z80TestProgram_Test*'
# one model:
cmake-build-agent-release/bin/core-tests --gtest_also_run_disabled_tests \
    --gtest_filter='*Z80TestProgram_Test*/48K'
```

Output per model: `[z80full@48K] frames=16735 Result: all tests passed.`, and each failing line
with the program's own `CRC:` / `Expected:` line.

How it works (`core/tests/z80/z80test/z80test_program_test.cpp`):
1. `CreateStandardEmulator(model, LogError, RamPowerOn::Zero)`, `EnableTurboMode(false)`.
2. `LoadSnapshot(testdata/loaders/sna/z80full.sna)`.
3. Loop: write `SCR_CT` (23692) = #FF, so the ROM never stops at "scroll?"; `RunNFrames(5)`;
   `ScreenOCR::ocrScreen`. z80test prints through the ROM (RST #10), so ScreenOCR reads it with
   the ROM font.
4. Keep each line with `FAILED`, plus the `CRC:` line under it. Skip lines with `?`: those are
   cells OCR could not match, from a line caught mid-print. It is read again on the next poll.
5. Stop at the `Result:` line.

To try another model, add it to `INSTANTIATE_TEST_SUITE_P`. To try another program that prints
through the ROM (z80doc / z80flags / z80ccf and so on, as `.sna`), change the snapshot path and the
end marker.

## Recipe: run it on a live emulator (MCP / WebAPI)

Use this to look at the screen yourself or to debug in the running app. Setup:
`.recipe/_common/setup.md`.

```text
emulator_manage  {"action":"create","model":"48K"}
load_software    {"path":"testdata/loaders/sna/z80full.sna"}
invoke_api       {"method":"POST","path":"/api/v1/emulator/{id}/memory/write",
                  "body":{"address":"23692","data":[255]}}           # SCR_CT: no "scroll?"
control_execution {"action":"run_frames","frames":2000}            # repeat the write + run
inspect_state    {"aspects":["screen_ocr"]}                        # read the progress / "Result:"
```

The program needs about 16,500 frames, so repeat write + `run_frames` about 9 times. The
screen scrolls, so earlier FAILED lines are lost unless you OCR after each step. The core-tests
harness does this.

## ZEXALL / ZEXDOC (`data/testsoft/ZEXALL/*.tap`)

These have **no** IN / INI / IND tests: their list ends at `ld (<bc,de>),a`. INI / IND / INIR /
INDR failures come from z80test (`z80full`). ZEXALL runs for hours of emulated time at 3.5 MHz
and is loaded from tape. It is not in the harness. To run it, use the live recipe above with
the tape fast-load (`.recipe/run/tape-fastload.md`) and `run_frames` in large steps.

## Takeaways

- Do not regenerate a golden CRC that came from hardware to match the emulator. It hides exactly
  this kind of regression. If a board really differs, put the difference in the board's model
  and leave the reference value alone.
- If a change touches the port value or the EAR / tape model, run the z80full harness across the
  models. The vector suite checks only F and runs only on the Pentagon.
