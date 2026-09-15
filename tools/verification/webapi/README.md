# WebAPI Verification Suite

Black-box verification for the emulator's HTTP automation API served by
`unreal-qt` on `localhost:8090`. Everything in this directory talks to a
**running emulator process over plain HTTP** — nothing links against or
imports emulator code. The suite therefore exercises the API exactly the way
external clients (scripts, the MCP bridge, CI jobs) see it.

## How the API is verified

Three independent layers. Use the cheapest one that answers your question:

| Layer | Entry point | Needs server | Question it answers |
|-------|-------------|--------------|---------------------|
| 1. Static coverage | `verify_openapi_coverage.py` | No | Is every route registered in the C++ API headers documented in the OpenAPI spec (and vice versa)? |
| 2. Dynamic spec sweep | `run_tests.sh` (wraps `src/openapi_verification.py`) | Yes | Does every endpoint in the served OpenAPI spec respond without errors? |
| 3. Behavioral tests | `src/` pytest suite | Yes | Does each endpoint actually behave per contract — shapes, state changes, error paths? |

Layer 1 catches documentation drift at the source level, layer 2 walks the
spec the server itself serves, and layer 3 goes deepest with real scenarios
and real media fixtures. A green run of all three layers is the working
definition of "the WebAPI is verified".

## Layout

```
tools/verification/webapi/
├── README.md                    # This file
├── AGENTS.md                    # Maintenance guide (agent-oriented, useful to humans)
├── requirements.txt             # Python dependencies for all layers
├── run_tests.sh                 # Layer 2 wrapper: availability check + full spec sweep
├── verify_openapi_coverage.py   # Layer 1: static route <-> OpenAPI-path diff (stdlib only)
├── resources/                   # Tracked small binary fixtures (see "Fixtures")
├── reports/                     # Generated sweep reports (gitignored, disposable)
└── src/
    ├── conftest.py              # pytest fixtures (API client, per-test emulator)
    ├── api_client.py            # UnrealApiClient: one thin method per endpoint
    ├── openapi_verification.py  # Layer 2 engine (fetch spec, exercise, report)
    ├── test_api_lifecycle.py    # Instance lifecycle and identity
    ├── test_api_basic.py        # BASIC automation endpoints
    ├── test_api_disk.py         # Disk inspection endpoints
    ├── test_api_media.py        # Tape / disk / snapshot lifecycles
    ├── test_api_mouse.py        # Mouse + keyboard injection contract
    ├── test_api_settings.py     # Settings endpoints
    ├── test_api_state.py        # Memory / screen / audio state inspection
    └── test_api_interpreter.py  # Embedded Python / Lua interpreters
```

## Prerequisites

1. **Emulator build with the WebAPI enabled** — `-DENABLE_WEBAPI_AUTOMATION=ON`
   (this is the CMake default). The Python/Lua interpreter tests additionally
   need those embeddings compiled in; without them those tests skip cleanly.
2. **Python 3** with dependencies from `requirements.txt`:
   - Behavioral suite: `requests`, `pytest` only.
   - Spec sweep: additionally `prance`, `openapi-spec-validator`, `jsonschema`, `faker`.
3. **Port 8090 free** — exactly one `unreal-qt` process can bind it. To target
   a different server, set `UNREAL_API_URL` (e.g. `http://localhost:8091`).

## Quick start

```bash
# 0. Build (repo root)
ninja -C cmake-build-release

# 1. Kill stale instances — only one process can bind port 8090
pkill -9 unreal-qt 2>/dev/null || true
sleep 1

# 2. Start the freshly built server (macOS path shown)
./cmake-build-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt &
# Linux:  ./cmake-build-release/bin/unreal-qt &
# Windows: cmake-build-release\bin\unreal-qt.exe
sleep 4

# 3. Sanity-check the API
curl -s http://localhost:8090/api/v1/emulator | jq .

# 4. Run the behavioral suite
cd tools/verification/webapi
python3 -m pytest src/ -v

# 5. When done
pkill -9 unreal-qt 2>/dev/null || true
```

The tests create and destroy their **own** emulator instances through the API;
the server process itself stays up for the whole run and should not be
restarted mid-suite.

## Running the behavioral test suite (layer 3)

```bash
cd tools/verification/webapi

python3 -m pytest src/ -v                        # whole suite
python3 -m pytest src/test_api_mouse.py -v       # one domain file
python3 -m pytest src/ -k "tape" -v              # by keyword
python3 -m pytest src/ --tb=short -q             # compact output

UNREAL_API_URL=http://localhost:8091 python3 -m pytest src/   # custom server
```

- Run from this directory (or pass the `src/` path explicitly) — `conftest.py`
  puts `api_client.py` on `sys.path` for you.
- On a healthy `master` build the full suite is ~94 test items and finishes in
  a couple of minutes against a single server instance (the mouse/keyboard
  file is the heaviest, roughly half the wall time).
- Skips are expected and meaningful: interpreter tests skip when the binary
  lacks Python/Lua embeddings; media tests skip when their `testdata/`
  fixtures are absent.

## Running the OpenAPI spec sweep (layer 2)

```bash
cd tools/verification/webapi
./run_tests.sh
```

`run_tests.sh` first verifies that the server and its `/api/v1/openapi.json`
are reachable (`--check-only`), then exercises every endpoint documented in
the served spec with generated request data and writes a timestamped markdown
report: `reports/YYYYMMDD-HHMM-api-verification-report.md`. The script's exit
code reflects the sweep verdict.

Two things to know:

- **`run_tests.sh` does not run the pytest suite.** The name predates the
  layering; use it for the spec sweep only.
- Run it from this directory — the report directory `reports/` is created
  relative to the current working directory.

Direct invocation without the wrapper:

```bash
python3 src/openapi_verification.py --check-only   # availability probe only
python3 src/openapi_verification.py --verbose      # full sweep + report
```

## Running the static coverage check (layer 1)

```bash
python3 verify_openapi_coverage.py          # missing routes fail
python3 verify_openapi_coverage.py --strict # also fail on stale spec entries
```

Compares the routes registered via `ADD_METHOD_TO` in
`core/automation/webapi/src/emulator_api.h` and `.../api/interpreter_api.h`
against the paths declared in `core/automation/webapi/src/openapi/*.inc`.
No server and no third-party Python packages are needed.

Exit codes: `0` clean · `1` missing routes (or spec-only routes with
`--strict`) · `2` environment error (project root or spec directory not found).

## Test suite map

| File | Domain | Highlights |
|------|--------|------------|
| `test_api_lifecycle.py` | Instance lifecycle | list/models/status, create-delete cycle, start/stop/pause/resume/reset, `creatable` flags, unknown model returns 400, non-creatable model returns 400 with reason, identity fields |
| `test_api_basic.py` | BASIC automation | fresh-boot state (`menu128k`), empty extract, `run` enters the editor, `run` with a command, inject-then-extract round-trip, `clear` as NEW, invalid emulator id returns 404 |
| `test_api_disk.py` | Disk inspection | drive list (A-D), sector/track reads decoded and raw-base64, sysinfo, catalog, image export, invalid geometry params, no-disk errors, blank disk creation (default and custom geometry) |
| `test_api_media.py` | Media lifecycle | tape load/info/rewind/eject, disk insert/info/eject, snapshot load/info — against real images from `testdata/` |
| `test_api_mouse.py` | Input injection | mouse status shape, movement and counter wrap, buttons, wheel, click frame timing, request validation 400s, unknown emulator 404s, CORS headers on errors, unknown-key handling |
| `test_api_settings.py` | Settings | listing shape, boolean round-trip (`fast_tape`) with restore, unknown setting name returns 404 |
| `test_api_state.py` | State inspection | memory overview / RAM banks / ROM pages, screen state/mode/flash phase, AY chips and registers, beeper, channel groups |
| `test_api_interpreter.py` | Embedded interpreters | Python/Lua exec, status, stop; Kempston mouse bindings through both interpreters |

## Fixtures

**`resources/` (tracked in git, small, deterministic)** — used by the spec
sweep and the disk inspection tests:

- `test_disk.trd` — TR-DOS image with a known catalog (disk tests, sweep)
- `test_tape.tap`, `test_snapshot.sna` — minimal media for sweep requests
- `test_script.py`, `test_script.lua` — interpreter scripts for the sweep

**`testdata/` (repo root, real-world images)** — used by the behavioral media
tests; these skip with a clear reason if a file is missing:

- `testdata/sound/ay/otomata_labs-atarized.tap`
- `testdata/sound/The_Viewer1.0.trd`
- `testdata/sound/covox/scroller_by_demarche.sna`

Fixtures are only read, never modified. The only files the suite writes are
pytest's `.pytest_cache/` and the sweep's `reports/` — both are gitignored.

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `Could not connect to Unreal API server` | Server not running or wrong URL | Start `unreal-qt`, check `UNREAL_API_URL` (the session fixture retries 5 times, 1s apart) |
| `Port 8090 still in use` warning | A stale emulator process holds the port | `pkill -9 unreal-qt`, wait a second, start the fresh binary |
| Mid-run burst of `ConnectionRefused` errors | The server process crashed | Do not rerun blindly: check the OS crash report (macOS: `~/Library/Logs/DiagnosticReports/*.ips`), identify the last test that ran, and fix the server. Re-running the single file reproduces it quickly |
| Interpreter tests skip | Binary built without Python/Lua embeddings | Rebuild with the interpreter options if you need those paths covered |
| Media tests skip | `testdata/` fixtures absent | Restore `testdata/` (git or LFS) |
| Creating model X fails with 400 | Model is not `creatable` on this build | Check `GET /api/v1/emulator/models`; some machines require a feature branch build (see the models note in the root `AGENTS.md`) |
| Tests fail right after an API change | The suite has drifted from the new contract | Expected — update the suite; see the maintenance workflow in `AGENTS.md` |

## Further reading

- [`AGENTS.md`](AGENTS.md) — maintenance guide for this suite: how to add
  endpoints, client conventions, contract gotchas, failure triage.
- Root [`AGENTS.md`](../../../AGENTS.md) — project-wide rules, including the
  canonical WebAPI server bring-up sequence and the model availability note.
- [`core/automation/webapi/src/openapi/AGENTS.md`](../../../core/automation/webapi/src/openapi/AGENTS.md) —
  how to maintain the OpenAPI spec fragments consumed by layers 1 and 2.
- The live spec itself: `curl -s http://localhost:8090/api/v1/openapi.json | jq .`
