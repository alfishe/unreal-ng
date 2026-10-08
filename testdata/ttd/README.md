# TTD fixture corpus

Two corpora of the same sessions:

- **`engine/`**: recorded by the engine, the application's recorder since the
  Phase 5 switch, in the engine's session file format (schema 2). Read by
  `TimeTravelControllerCorpus_Test`
  (`core/tests/debugger/ttd/timetravelcontroller_corpus_test.cpp`): each file
  loads into an instance of its model with the engine as its recorder, every
  visited checkpoint restores with no issue reported, and the session continues
  from checkpoint 37 - the 25 checkpoints recorded live restore the machine
  (CPU, chipset, every device, all of RAM) exactly as the recorded ones did.
  The per-machine fixtures (TS-Conf, Sprinter) are in this folder too.
- **this folder and `testdata/machines/*/ttd/`**: recorded by v1, in v1's
  format (schema 1), the reference for v1's own tests until v1 leaves the
  application (Phase 5, Step 4). The rest of this file is about them unless it
  says otherwise.

`record_fixtures.py` asks the instance which recorder it runs (`backend` in
`/ttd/status`): an engine recording goes to `engine/`. To re-record v1's corpus,
start the application with `UNREAL_TTD_BACKEND=v1`.

Recorded `.ttd` sessions from real emulator runs. They are read by:

- the C++ test `TTD_Corpus_Test` (`core/tests/debugger/ttd/ttdcorpus_test.cpp`).
  It loads every fixture here into an instance that already holds its own
  TurboSound FM history, restores several checkpoints, checks that every
  device matches the recording byte for byte, and replays 25 frames to check
  the replay matches the recording exactly;
- the analyzer (`tools/verification/ttd-analyzer/`);
- the codec PoC (`tools/poc/01-ttd-compression/`) and the PoC GUI
  (`tools/poc/010-ttd-gui/`).

The fixtures are written by the production writer, so they are tied to the
on-disk format: **a format change makes them stale, and `TTD_Corpus_Test`
fails.** Don't convert old files; re-record them as described below.

All paths in this file are relative to the project root.

## Corpus

| Fixture | Model | Starting point | Settle frames |
|---|---|---|---|
| `idle_session.ttd` | Pentagon 128K | cold boot, no snapshot: the recording is the boot, ending at the 128K menu | 0 |
| `active_demo.ttd` | Pentagon 128K | `testdata/loaders/sna/Dizzy Y.sna` | 0 |
| `demo_7threality.ttd` | Pentagon 128K | `testdata/loaders/sna/7threality.sna` | 100 |
| `demo_across-the-edge-second.ttd` | Pentagon 128K | `testdata/loaders/sna/across-the-edge-second.sna` | 100 |
| `tsfm_tech_support.ttd` | Pentagon 128K | `testdata/sound/tsfm/tech_support.sna` (TurboSound FM music) | 0 |
| [`../machines/tsconf/ttd/sprites.ttd`](../machines/tsconf/ttd/sprites.ttd) (`--only tsconf_sprites`) | TS-Conf | `testdata/machines/tsconf/spg/sprites.spg` (16C frame drawn by a DMA copy every frame), classic GS card swapped in | 50 |
| [`../machines/sprinter/ttd/boot.ttd`](../machines/sprinter/ttd/boot.ttd) (`--only sprinter_boot`) | Sprinter Sp2000 | cold full start of the shipped config, no snapshot: the PLD load at 3.5 MHz, then the POST of the default BIOS 3.06 Hotfix 2 at 21 MHz (re-recorded 2026-10-03 when the default moved back from 3.07 BETA 1); classic GS card swapped in (the GS behind the ISA ZX-bus adapter, since 2026-10-04) | 0 |

Each fixture records 300 frames (301 checkpoints). The table lives in code as
`CORPUS` in `tools/verification/ttd-analyzer/scripts/record_fixtures.py`. To
add or change a fixture, edit that list and this table together. A fixture of
another machine lives in `testdata/machines/<machine>/ttd/` (the script's
`FIXTURE_OPTIONS`); `TTD_Corpus_Test` runs it on a fresh machine of its
recorded model.

## ZX-MultiSound sessions (recorded by the tests, not stored)

Two more sessions belong to the corpus but are not in the repository (owner decision 2026-10-07: about 19 MB of
fixtures for the two recorders): the tests record them in their own process on first use
([`core/tests/_helpers/ttdmultisoundsessions.h`](../../core/tests/_helpers/ttdmultisoundsessions.h)), save them to the
process's scratch folder, load them like the stored files and delete them when the process ends.

| Session | Model | Starting point | Settle frames | Frames |
|---|---|---|---|---|
| `multisound-pentagon` | Pentagon 128K, ZX-MultiSound in `zxbus.1` (the slot set replaces the shipped one, so the socket keeps its plain AY) | [`../sound/multisound/ttd/allsources.sna`](../sound/multisound/ttd/allsources.sna): every source of the card - both YM2203 (FM + SSG), the SAA1099, the SounDrive DACs, a General Sound command, MIDI notes bit-banged into the SAM2695 (written by [`make-program.py`](../../tools/verification/multisound/ttd-fixture/make-program.py)) | 10 | 70 |
| `multisound-zxevo` | ZX-Evo (`ATM3`), ZX-MultiSound in `zxbus.1` (the YM2149 leaves its socket) | the same program (it measures the CPU clock - the ZX-Evo starts at 7 MHz - and times its MIDI bits to it) | 10 | 70 |

Recorded as `record_fixtures.py` records a fixture: a fresh machine, 44.1 kHz core rate, Sound HQ and Screen HQ on,
the snapshot, the settle frames, then a recording with the write journal. Each is recorded twice per test process at
most: by v1 for `TTD_Corpus_Test`, `TTDSessionFile_Test` and `TTDV1Feeder_Test`, by the engine for
`TimeTravelControllerCorpus_Test`. 70 frames (71 checkpoints) instead of 300: the SAM2695's state (292 KB raw) changes
in every checkpoint, and the corpus tests need 63 checkpoints. The tests build the replay machine from the session's
header with the card in `zxbus.1` and the shipped default MIDI bank, which the sessions name
(`core/tests/_helpers/ttdslotcards.h`). A change of the card or of the program needs no re-recording.

## Port-journal fixtures (`port-journals/`)

Two real sessions with their port journals (every IN and OUT with its time
and PC, ttd-port-read-journal.md) and the emulator's answers to a set of
"when did the program ..." questions:

| Fixture | Model | Session | Journals |
|---|---|---|---|
| `dizzyx.ttd` | Pentagon 128K, classic GS | `testdata/loaders/z80/dizzyx.z80` (Dizzy X, AY music); keys 8, 0, 5, Q, SPACE pressed; 495 frames | 7,905 IN, 14,920 OUT, 16.8 KB of a 2.1 MB file |
| `greenberet-load.ttd` | 128K, classic GS | `testdata/loaders/tap/greenberet.tap` loaded by the 128K menu's Tape Loader; 1,010 frames (20 s) of loading | 722,025 IN, 28,859 OUT, 156 KB of a 1.2 MB file |
| `expected.json` | | the WebAPI's answers (`POST /ttd/port-events`) to 27 questions, asked of the live session and of the saved file (checked equal) | |

They are read by the C++ test `TimeTravelManager_PortJournalFixture_Test`
(`core/tests/debugger/ttd/timetravelmanager_portjournal_test.cpp`: the
emulator's file search must answer every question the same) and by the
analyzer's `tests/test_port_search.py` (its `search` must too). They are not
in the checkpoint corpus above (`TTD_Corpus_Test` reads `testdata/ttd/*.ttd`
only).

Re-record them (the app with the WebAPI on port 8090, as below) with

```bash
python3 tools/verification/ttd-analyzer/scripts/record_port_journal_fixtures.py
```

Every step runs an exact number of frames on a stopped machine, so the
recording depends only on the build. The General Sound slot is switched to the
classic card: NeoGS's ZX-DMA is not isolated by the port journals, which are off
with it.

## Re-recording (after a format change)

1. Build, then start the desktop app with the WebAPI on port 8090. You don't
   need to create an instance: the recorder creates a fresh one for each
   fixture and deletes it afterwards, and leaves any other instances alone.

   ```bash
   ninja -C cmake-build-agent-release
   ./cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt &   # macOS
   ```

2. Re-record the whole corpus into `testdata/ttd/`:

   ```bash
   python3 tools/verification/ttd-analyzer/scripts/record_fixtures.py
   ```

   Or re-record a single fixture:

   ```bash
   python3 tools/verification/ttd-analyzer/scripts/record_fixtures.py --only tsfm_tech_support
   ```

3. Validate the files and run the C++ gate:

   ```bash
   for f in testdata/ttd/*.ttd testdata/ttd/engine/*.ttd; do tools/verification/ttd-analyzer/run.sh validate "$f"; done
   ./cmake-build-agent-release/bin/core-tests --gtest_filter='TTD_Corpus_Test.*:TimeTravelControllerCorpus_Test.*'
   ```

   An engine recording's file is not byte-identical to the previous one (its
   header carries a fresh session id and the creation time); its content is.

The script can run from any directory. It resolves relative paths from the
project root and passes absolute paths to the emulator, so the emulator must
run on the same machine.

### What the recorder fixes, and why

A fixture is only useful if re-recording it gives the same file. So that it
does, the recorder pins everything that would otherwise depend on the
machine or on what ran before:

- **A fresh instance per fixture.** An instance that has already run something
  keeps state such as the FM chips' internal counters, and a new recording
  would pick that up.
- **Core audio rate 44100 Hz, `soundhq` and `screenhq` on.** Without this, the
  app follows the host's audio device (often 48 kHz). SSG tick scheduling
  depends on the output rate and the decimator mode, so a replay is exact
  only with the same rate and mode as the recording. `TTD_Corpus_Test` runs
  with these same settings.
- **The classic GS card in the GS slot.** The corpus was recorded while the
  shipped PENTAGON config had `GSType=Z80`; the shipped configs fit NeoGS
  since 2026-09-28, and a session loads only into the card it was recorded
  with, so `TTD_Corpus_Test` fits the classic card before loading. The recorder
  switches the slot to the classic card itself (`FIXTURE_OPTIONS`, `"gs": "z80"`),
  whatever the app is configured with (a NeoGS checkpoint carries the whole
  card, several MB, in every checkpoint).
- **Length in emulated frames, not wall-clock time.** Settling and recording
  both use `run_frames`, because the emulator doesn't run at 50 Hz. Unthrottled
  it once ran ~9x realtime and turned a nominal 6-second recording into 2714
  frames.

After a re-record, 4 of the 5 files are byte-identical to the previous
recording except for the capture timestamp (`captured_at_unix_ms`).
`idle_session` is the exception: power-on RAM is deliberately randomized
(`Memory::RandomizeMemoryContent`), so its RAM contents differ from one
recording to the next. Its checkpoints, CPU state and device blobs still
match.

### Ad-hoc recordings

To make a one-off recording from another snapshot, put it in `scratch/`, not
here:

```bash
python3 tools/verification/ttd-analyzer/scripts/record_fixtures.py \
    --out-dir scratch --snapshot "testdata/loaders/sna/Dizzy Y.sna" --frames 600 --name dizzy
```

`--emulator-id <id>` records on an existing instance (after a reset) instead of
a fresh one. That's handy for debugging, but the result is not reproducible.

## Inspecting a fixture

```bash
pip install -r tools/verification/ttd-analyzer/requirements.txt   # zstandard is required
tools/verification/ttd-analyzer/run.sh info     testdata/ttd/tsfm_tech_support.ttd
tools/verification/ttd-analyzer/run.sh validate testdata/ttd/tsfm_tech_support.ttd
```

`info` prints the header, including the struct sizes to compare against the
current build. A stale fixture shows its old sizes there, which is the quickest
way to tell a stale fixture from a corrupt one.

`validate` fails if any byte of the file is left unrecognized:

- bytes after the last section;
- unparsed CPU or chipset fields;
- an unknown device ID or a device blob that won't decode;
- a journal block that doesn't decode to exactly its directory entry (record
  count, size, time range).

It also decodes every referenced memory page and checks its checksum. The
write journal has no checksum of its own, so a corrupt zstd frame is caught
but a flipped byte that still decodes is not.

## Status

2026-10-08: the Pentagon fixtures of both corpora (`*.ttd`, `engine/` but `tsconf_sprites` and `sprinter_boot`) and
the port-journal fixtures re-recorded: with no tape image, #FE bit 6 now reads 0 on the Pentagon and follows the EAR
output on the 128K (idle EAR per board, `docs/inprogress/2026-10-08-z80test-in-ear/`). In `expected.json`, Dizzy X's
`key` / `in` answers changed only in the value read (bit 6); Green Beret's answers all moved, because the 128K reads
the EAR level before playback starts and the load runs on a slightly different timeline. The TS-Conf and Sprinter
fixtures replay unchanged and were left alone.

2026-10-05: the engine corpus re-recorded after master's ZX-bus slots (the slot set is in the engine's configuration
fingerprint). `record_fixtures.py` plugs the General Sound card into a ZX-bus slot when the shipped config fits none;
`sprinter_boot` has no ZX-bus and is recorded without a GS card, as the Sprinter now ships.

2026-10-05: the engine corpus re-recorded: the controller's own streams moved to the ids the Phase 4 stream table
names (8 coverage, 12 bookmarks, 17 facts; 0x02xx is reserved for branches).

2026-10-05: the engine corpus (`engine/`, all seven fixtures) recorded for the first time, by the application on the
engine after the Phase 5 switch; 3 to 7 times smaller than the v1 files of the same sessions.

2026-10-04 (zx-bus-slots, owner decision): the shipped 48K / 128K / +2 / +2A / +3, Profi and Sprinter configs fit no
General Sound any more. No fixture was re-recorded: `sprinter_boot` (Sprinter) and `greenberet-load` (128K) were
recorded with the classic GS, and the tests that load them now fit the recorded card at creation
(`GeneralSoundFitScope`, `core/tests/_helpers/soundcardscope.h`) instead of switching the shipped card. A re-record
from the stock app needs the card in the instance's `[SLOTS]` first: the recorders' `switch_personality` changes a
fitted card but cannot fill an empty slot (until SL-6).

2026-10-04: the whole corpus (all seven fixtures) re-recorded after `ttd-engine` landed on master: the engine's
peripheral ids moved to 54-57 (Smuc, EvoAvrVolatile, KeyboardMatrix, RzxPlayback; master kept 44-53), and master's
newer ROM set, Profi `ExtPorts` fingerprint and Sprinter ISA blobs had changed every file. `v1-ci-gate.txt` and
`v2/active-demo-converted.ttd` exported again from the same build. The port-journal fixtures too, and both
recorders now ask for the write journal (`/ttd/start` with `"journal": true`): it is recorded on demand since D40,
and without it the corpus would stop exercising it.

2026-10-04: the Sprinter fixture (`sprinter_boot`) re-recorded alone after master's SN4 / SN5 merge (default BIOS 3.06
Hotfix 2): the default ISA population now fits the ZX-bus adapter with a General Sound in slot 1 (ISA phase I2), so
the fixture carries the classic GS blob (id 5; the recorder swaps the card in, `"gs": "z80"`, as for the Pentagon
corpus) and still the port journals (the adapter passes no memory cycles: no NeoGS ZX-DMA). The other fixtures are
unchanged.

2026-10-03: the Sprinter fixture (`sprinter_boot`) re-recorded alone on the new default BIOS 3.06 Hotfix 2 (back
from 3.07 BETA 1, owner decision); the other fixtures are unchanged.

2026-10-03: the Sprinter fixture (`sprinter_boot`) re-recorded alone again: the default NE2000 in ISA slot 2 adds the
Ethernet cards' blob (id 45, `EthernetNics`: DP8390, packet RAM, EEPROM, the Ethernet gateway) to every checkpoint;
the other fixtures are unchanged.

2026-10-03: the Sprinter fixture (`sprinter_boot`) re-recorded alone: the ISA slots' blob (id 33, `SprinterIsa`, ISA
phase I1) joins every checkpoint; the other fixtures carry no Sprinter blob and are unchanged.

2026-10-02: the Sprinter fixture (`sprinter_boot`) re-recorded alone after the Z84C15 blob (id 29) went to v2
(the CTC counter mode, 1 + 227 bytes); the other fixtures carry no Sprinter blob and are unchanged.

2026-10-02: the Sprinter fixture added (`sprinter_boot`, phase S7); the other fixtures are unchanged
and still pass (device blobs are now restored in ascending id order). When Sprinter phase S6 adds its
sound / ISA / pad blobs (reserved ids 32-34), only this fixture is re-recorded: the existing Sprinter
blobs keep their layout.

Re-recorded 2026-09-28 (all five) with the IDE board the shipped PENTAGON
config now fits (`[HDD] Scheme=NEMO`, no disks): every checkpoint carries the
`AtaChannel` blob (id 17: both IDE units, the adapter latches).

Re-recorded 2026-09-27 (all five) after the General Sound fixed window
`0x4000-0x7FFF` moved to the upper half of MPAG 1 (RAM page 1, was page 3):
the firmware's variables and DAC buffers now sit in a different part of the GS
RAM image, and the firmware holds back page 1 instead of page 2.

Re-recorded 2026-09-27 (all five) after the General Sound coprocessor moved
from z80ex to unreal-z80: the GS device blob (id 5) keeps its size, but its
Z80 block now carries the library's register set (R as one byte, Q, the
instruction-boundary state and the NMI session flag instead of z80ex's
16-bit R, R7 and prefix bytes).

Re-recorded 2026-09-26 again (all five) after the instruction-start fix: for
CB/DD/FD/DDCB instructions the write journal's writer PC (`m1_pc`) is now the
instruction's first byte, not its second. Checkpoints are unaffected.

Re-recorded 2026-09-26 (all five) after Z80 core fixes: the INT/NMI
acknowledge now advances R (every recording diverged at the first frame's INT
otherwise), and `TTDCpuState` carries the instruction-boundary state
(`boundary`: INT shadow after EI or RETN/RETI, pending DD/FD prefix, LD A,I/R
quirk, NMI just acknowledged) in the former padding byte at offset 35. Struct
sizes are unchanged (`cpu_state_size = 48`); older files read 0 (none) there.

Previously re-recorded 2026-09-25 (all five; `tsfm_tech_support` is new). Header:
`cpu_state_size = 48`, `chipset_state_size = 120`. Changes since the previous
recording:

- `TTDChipsetState` now stores the CPU's T-state within the frame
  (`cpu_t_in_frame`, 3 bytes taken from `reserved`). A restore puts the CPU back
  exactly where the capture was taken, not at T 0. The struct size didn't
  change, and older files read 0 there.
- SSG register writes are now timed. The AY blob grew from 57 to 73 bytes (the
  registers the generators actually use are appended). The TurboSound FM blob
  is v4, 2000 bytes: the render-cursor offset plus both chips' queues of
  pending timed writes.

## Schema 2: the engine's session file (`v2/`)

Files in the time-travel engine's format (TTD v2, Phase 4), written by the C++
writer for the analyzer's conformance test:

| Fixture | What |
|---|---|
| `v2/synthetic.ttd` | a synthetic 60-frame session in three segments, finished |
| `v2/synthetic-unfinished.ttd` | the same, cut inside its last part (a crash while recording) |
| `v2/synthetic-ancillary.ttd` | the same with records of an ancillary stream readers do not know (0x0100) |
| `v2/active-demo-converted.ttd` | `active_demo.ttd` converted from schema 1 (D31) |
| `v2/expected.json` | what the C++ reader finds in each: checkpoints, versions, parts, events, bus records, finished, converted |

Write them again after a format change (then `git add -f`, `*.ttd` is ignored):

```bash
./cmake-build-agent-release/bin/core-tests --gtest_also_run_disabled_tests \
    --gtest_filter='TTDSessionFile_Test.DISABLED_WriteAnalyzerFixtures'
```

`TTDSessionFile_Test.CommittedFixturesStillLoad` fails when they are stale;
`tools/verification/ttd-analyzer/tests/test_ttdcontainer.py` checks the analyzer
against them.
