# TTD fixture corpus

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

Each fixture records 300 frames (301 checkpoints). The table lives in code as
`CORPUS` in `tools/verification/ttd-analyzer/scripts/record_fixtures.py`. To
add or change a fixture, edit that list and this table together.

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
   for f in testdata/ttd/*.ttd; do tools/verification/ttd-analyzer/run.sh validate "$f"; done
   ./cmake-build-agent-release/bin/core-tests --gtest_filter='TTD_Corpus_Test.*'
   ```

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

Re-recorded 2026-09-25 (all five; `tsfm_tech_support` is new). Header:
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
