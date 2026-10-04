# TTD surface contract

`ttd_surface_contract.py` checks that the time-travel calls give the same answers on every automation surface: WebAPI, CLI, Lua and Python. It is part of Phase 5 of the TTD v2 migration ([phase-5-switchover-tdd.md](../../../docs/inprogress/2026-09-25-ttd-v2-migration/phase-5-switchover-tdd.md), requirement QR-8).

Every surface turns its input into a `TTDControl` request and the reply into its own form: JSON, CLI text, a Lua table or values, a Python dict or `RuntimeError`. The verbs, their checks and their messages live in `TTDControl` (`core/src/debugger/ttd/ttdcontrol.cpp`) and are tested in `core-tests` (`TTDControl_Test`). This script checks the part `core-tests` cannot reach: each surface's mapping inside the running application.

## What it checks

| Group | Example |
|---|---|
| Status | Every scalar of `GET /ttd/status` has the same value in Lua's `ttd_status()` and Python's `emu.ttd_status()` |
| Settings across surfaces | A history limit set from Lua is what the WebAPI and Python report |
| Refusals | While recording, `invalidate` and `journal build` are refused with the same sentence on all four surfaces (409, `false, message`, `RuntimeError`, `Error: ...`) |
| Lifecycle | Start and stop on one surface are seen by the others |

## Run it

Start the application on ports of your own, so other sessions' instances are not disturbed:

```bash
UNREAL_WEBAPI_PORT=8197 UNREAL_CLI_PORT=8797 \
    ./cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt &
python3 tools/verification/ttd-surface-contract/ttd_surface_contract.py \
    --base-url http://localhost:8197 --cli-port 8797
```

The script creates one PENTAGON instance, selects it for the CLI, Lua and Python, and removes it at the end. Exit status 0 means every check passed. A build without Python automation (`ENABLE_PYTHON_AUTOMATION` is off by default) skips the Python checks and says so.
