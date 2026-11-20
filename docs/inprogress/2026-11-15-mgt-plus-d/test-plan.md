# MGT +D: test plan

Rules of `core/tests/README.md` apply: about 10 ms per test, integration under 1 s, full variants behind
`TestTier::Long()`, `TestWait::For`, never `sleep_for`, `EnableTurboMode()` on boot-bound tests, scratch files from
`TestPathHelper`. Per subtask: target build + filtered tests; the full run once before landing.

## 1. Fixtures

| Fixture | What | Where |
|---|---|---|
| G+DOS v1a ROM | 8 KB | `data/rom/plusd/gdos-1a.rom` (today `data/rom/gdos-pd.rom`) |
| System disk | `+SYS 2a` only | `data/boot/plusd/gdos-2a-system.mgt` ([autostart.md §2](autostart.md#2-the-emulators-system-disk)) |
| Synthetic MGT | existing | `testdata/loaders/mgt/synthetic.{mgt,img}` |
| Small work disks | 4-6 images, one per autostart plan (`+SYS` + `AUTO`; `+SYS` only; no DOS + `Loader`; no DOS + one 48K snapshot; one 128K snapshot; CODE only) | `testdata/machines/plusd/` (chosen from the corpus list in P0, or built by the emulator with G+DOS `SAVE`) |
| Corpus | the 243 +D images of the catalog | not in the repository; a `tools/verification/plusd/` script reads them from a path |

## 2. Unit tests

### 2.1 WD1772 variant (`core/tests/emulator/io/fdc/wd1772_test.cpp`)

| Test | Checks |
|---|---|
| `StepRates` | Restore / Seek with `r1 r0` = 00..11 take 6 / 12 / 2 / 3 ms per step (T-states at 3.5 MHz) |
| `SpinUp` | a command with the motor off waits 6 index pulses; `h = 1` skips it; status bit 5 set after spin-up in Type I |
| `MotorOff` | the motor goes off after 9 index pulses without a command; status bit 7 follows the motor |
| `NoReady` | an empty drive never sets bit 7 as not-ready; a read on an empty drive ends with RNF after the index count |
| `NoSideCompare` | Type II with bit 1 set reads the sector regardless of the side byte in the ID (precomp flag only) |
| `ForceInterrupt` | `D8` (I3) and `D4` (I2) act; `D1` / `D2` do nothing |
| `ReadSector512` | a 10 x 512 MGT track: sectors 1..10 read with the right DRQ count and CRC |
| `WriteTrackFormat` | G+DOS's format track writes a track that the MGT loader serializes back byte for byte |
| `Wd1793Unchanged` | the existing WD1793 suite runs unchanged (it is the gate, not a new test) |

### 2.2 Card (`core/tests/slots/plusd_test.cpp`)

| Test | Checks |
|---|---|
| `TrapsPageIn` | M1 at `0008`, `003A`, `0066`, `028E` reads the +D ROM byte at that address; M1 at `0009` does not page |
| `PortE7` | `IN (E7)` pages in, `OUT (E7)` pages out; data read is the documented value |
| `RamWindow` | while paged: writes to `2000-3FFF` land in +D RAM, `0000-1FFF` writes ignored; unpaged: Spectrum ROM / no write |
| `ResetState` | after reset the +D is paged in (H-2) and its RAM survives a reset but not a power cycle |
| `ControlLatch` | `EF` bits 0 / 1 select drive 1 / 2, `00` none, bit 7 side, bit 6 strobe edge reaches the printer sink |
| `Ports128K` | on 128K the overlay hides the ROM `7FFD.4` selects and leaves the latch untouched |
| `NoCardNoCost` | without the card `machineM1Hook` is null and no overlay is installed |
| `CoexistsWithBeta` | 48K with Beta 128 and +D: a TR-DOS read and a +D read in one run, each controller steps |
| `SlotsAndAliases` | `media` lists `fdd.plusd1/2`; `plusd1` resolves; eject empties; a write reaches `NoteWrite` (the §6 checklist of the new-platform guidelines) |
| `FitRules` | real on 48K / 128K, unrealistic on +3, refused where the M1 hook is taken (ATM3) |

### 2.3 Snapshots and TTD

| Test | Checks |
|---|---|
| `Z80Mode3LoadsWithPlusD` | a `.z80` v3 mode 3 loads on 48K with the card; byte 59 = paged restores the page state |
| `SzxPlsdRoundTrip` | save SZX with the card paged, RAM pattern, latch; load gives the same state |
| `TtdSeekDuringRead` | record a G+DOS `CAT`, seek back into the middle of a sector read, replay: same bytes, same status |

## 3. Integration tests (boot-bound, `EnableTurboMode()`)

| Test | Tier | Checks |
|---|---|---|
| `GdosBoots48K` | normal | system disk in drive 1, `RUN`: the +D RAM at `2000` holds the `+SYS` body, BASIC waits for a key |
| `GdosBoots128K` | normal | the same on 128K from `128 BASIC` (H-7) |
| `CatListsDisk` | normal | `CAT 1` prints the file names of a fixture disk (screen text through the existing OCR / screen-text helper) |
| `SaveLoadRoundTrip` | long | `SAVE D1"t" CODE 32768,100`, `LOAD D1"t" CODE` gives the same bytes; the medium has the new file |
| `SnapshotButton48K` | long | NMI, key 4: a type 5 file appears; `LOAD D1"..." S` resumes at the same PC |
| `AutostartPlans` | normal per plan, long for all | each fixture disk: the plan chosen, the command typed, the work started (PC leaves the ROM, screen changes) |

## 4. Automation

- MCP unit tests `--gtest_filter='*Mcp*'` for the new `load_software` routing and result fields.
- Live: `tools/verification/mcp/run_all.py --launch <unreal-qt>` with a new block `plusd.py`: create `48K-PLUSD`,
  `load_software` an MGT with autostart, read `inspect_state` `fdc`, eject, insert into `plusd2`.
- WebAPI: `.recipe/testing/webapi-verification.md` steps for `media insert fdd.plusd1`.

## 5. The corpus run (P4 acceptance)

`tools/verification/plusd/corpus_run.py <pool path> <classified tsv>`: for every +D image (243) create the preset,
`load_software` with autostart, run 12 s emulated, record plan, command, outcome and a screenshot. Table in the
TODO with counts per plan and outcome; every failure gets a reason (wrong plan, DOS did not load, work needs a
key, damaged image). Nothing is tuned to individual titles: the plan rules of
[autostart.md §3](autostart.md#3-the-autostart-plan) are generic.

## 6. Oracles

- MAME (`/Volumes/TB4-4Tb/Projects/emulators/github/mame/zx`, `spectrum -exp plusd`) and Fuse for the same disk:
  CAT output and the first frames of a work after `LOAD`.
- FDC: register traces of a G+DOS boot in Fuse (debugger log) against ours (WD state per command).

## 7. Performance

A/B benchmark (performance guidelines): 48K without the card vs 48K with the card idle in BASIC vs a G+DOS read
loop. Acceptance: no measurable change without the card; with the card idle within the A/A noise.
