# TTD fixture corpus

Recorded `.ttd` sessions used by the offline tooling — the analyzer
(`tools/verification/ttd-analyzer/`), the codec PoC
(`tools/poc/01-ttd-compression/`) and the PoC GUI
(`tools/poc/010-ttd-gui/`). No C++ test reads these files; the dump-format
tests generate their own fixture, so a stale corpus breaks tools, not CI.

## Provenance

Every fixture must be reproducible from a named starting point. A recording of
"whatever the instance happened to be running" is worthless as a fixture: two
of these once captured the program left over from the previous recording and
rendered identical screens.

| Fixture | Model | Starting point | Settle |
|---|---|---|---|
| `idle_session.ttd` | Pentagon 128K | cold boot, no snapshot — the recording *is* the boot, ending at the 128K menu | n/a |
| `active_demo.ttd` | Pentagon 128K | `testdata/loaders/sna/Dizzy Y.sna` (the recorder's default) | **none** — record immediately from the loaded state |
| `demo_7threality.ttd` | Pentagon 128K | `testdata/loaders/sna/7threality.sna` | default |
| `demo_across-the-edge-second.ttd` | Pentagon 128K | `testdata/loaders/sna/across-the-edge-second.sna` | default |

`active_demo` is deliberately recorded with **zero settle frames**. Loading the
snapshot on a paused machine and starting the recording at that exact state is
what makes the capture repeatable — let the program run first and the contents
depend on how many frames elapsed before recording began.

The snapshot for `active_demo` is the one hardcoded in the recorder's default
set, so `record_fixtures.py` with no `--snapshot` reproduces both default
fixtures exactly. Change it in one place (the `fixtures` list in the script) if
it ever needs to differ, not by passing a different snapshot by hand.

## Re-recording

The fixtures are produced by the same writer production uses, so they are
coupled to the on-disk format: **any format change invalidates them.** The
reader refuses a file whose `cpu_state_size` / `chipset_state_size` /
`schema_version` disagree with the running build, and the Python parser fails
with a structural error rather than silently misparsing. When that happens,
re-record — do not attempt to convert.

Start an emulator with the WebAPI enabled. The recorder drives whatever
instance is already running and does not switch models, so make sure it is the
right one — create it explicitly if in doubt:

```bash
curl -s -X POST http://localhost:8090/api/v1/emulator/start \
  -H "Content-Type: application/json" -d '{"model":"PENTAGON"}'
```

```bash
R=tools/verification/ttd-analyzer/scripts/record_fixtures.py

# The default set: idle_session (no snapshot) + active_demo (Dizzy Y.sna).
# --settle-frames 0 keeps active_demo repeatable.
python3 $R --out-dir "$(pwd)/testdata/ttd" --settle-frames 0

# The two demo captures. --name only takes effect together with --snapshot.
python3 $R --out-dir "$(pwd)/testdata/ttd" --name demo_7threality \
    --snapshot "testdata/loaders/sna/7threality.sna"
python3 $R --out-dir "$(pwd)/testdata/ttd" --name demo_across-the-edge-second \
    --snapshot "testdata/loaders/sna/across-the-edge-second.sna"
```

Paths are resolved by the **emulator**, not by the script: both the snapshot
path and the output path are interpreted on the machine running the emulator.

The recorder drives the recording length by **checkpoint count, not wall
clock**. The emulator is not obliged to run at 50 Hz — unthrottled it has run
~9x realtime, which turned a nominal "6 second" recording into 2714 frames
instead of 300 and left the fixtures unreproducible.

## Verifying a fixture

```bash
cd tools/verification/ttd-analyzer
pip install -r requirements.txt          # zstandard is required to read page slots
python3 -m src.main info     ../../../testdata/ttd/idle_session.ttd
python3 -m src.main validate ../../../testdata/ttd/idle_session.ttd
```

`info` prints the header, including the struct sizes to compare against the
current build. A fixture recorded before a format amendment shows its old
sizes there, which is the quickest way to tell a stale fixture from a corrupt
one.

## Status

Re-recorded 2026-09-12 against the model-agnostic checkpoint format (chipset
state 168 → 120 bytes, `rom_signature` in the header, the fixed per-device blob
slots replaced by the `TTDPeripheralRegistry` blob map). All four parse and
carry `cpu_state_size = 48`, `chipset_state_size = 120`, and the four core
device blobs (TurboSound / BetaDisk / Tape / Covox) through the registry.
