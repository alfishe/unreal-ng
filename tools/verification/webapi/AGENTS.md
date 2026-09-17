# WebAPI Verification Suite — Maintenance Guide

Rules and hard-won knowledge for AI agents changing anything under
`tools/verification/webapi/`. The repository-root `AGENTS.md` still applies in
full (commit policy, scratch/ artifacts, zero warnings, naming); this file
adds the suite-specific procedures on top. Humans: [`README.md`](README.md)
explains how to run the layers; this file explains how to keep them honest.

## Mental model

- The test subject is a **live `unreal-qt` process**. These are network
  contract tests, not unit tests. A stale binary means you are testing an old
  API — always rebuild before debugging "impossible" failures.
- The suite is downstream of the C++ API: when an endpoint's shape or behavior
  changes, actualizing the tests belongs in the same task as the server change
  (see "Updating the suite after an API change").
- Three layers exist (static coverage, spec sweep, behavioral pytest — see
  README). They verify different things; passing one does not excuse the others.
- **Parity rule**: WebAPI and MCP are equally important surfaces fed from the
  same source (root `AGENTS.md`). A change to served data or shapes should be
  cross-checked against the MCP tools that mirror it.

## Where things live

| Concern | File | Convention |
|---------|------|------------|
| HTTP details | `src/api_client.py` | One thin method per endpoint; docstring carries the exact route; `expected_status` lists acceptable codes. All URL construction stays here |
| Strict-status assertions | `src/api_client.py` `*_raw` methods, or raw `session` calls | `_handle_response` raises on unexpected status — for 400-class body assertions use raw access (pattern: helpers in `test_api_mouse.py`) |
| Fixtures | `src/conftest.py` (shared), domain files (local) | `api_client` is session-scoped; `active_emulator` is function-scoped |
| Behavioral tests | `src/test_api_<domain>.py` | One file per API domain; create a new file only for a genuinely new domain |
| Spec sweep | `src/openapi_verification.py` + `run_tests.sh` | Wraps the sweep; note it never runs the pytest suite |
| Static coverage | `verify_openapi_coverage.py` | Stdlib-only; run after any route/spec edit |
| Server side | `core/automation/webapi/src/` | Route registry: `emulator_api.h`, handlers: `api/*.cpp`, spec: `openapi/*.inc` (own `AGENTS.md`) |

Naming carryover: `test_api_*.py` snake_case filenames and snake_case test
methods are the established Python pattern here — the C++ PascalCase rule does
not apply to this directory. Do not "fix" Python filenames.

## Golden path: adding or changing an endpoint

1. Server: implement handler + `ADD_METHOD_TO` route string in
   `core/automation/webapi/src/emulator_api.h` (or `api/interpreter_api.h`).
2. Spec: add/extend the fragment in `core/automation/webapi/src/openapi/*.inc`
   following that directory's `AGENTS.md` (tags, parameters, responses).
3. `python3 verify_openapi_coverage.py` — must be clean (`--strict` too when
   removing routes).
4. Client: add one method to `api_client.py`; docstring = exact route;
   `expected_status` for every non-200 success code (e.g. create = 201).
5. Probe before asserting (next section), then write/extend tests in the
   matching `src/test_api_<domain>.py`.
6. Full verification (see Done checklist) — one freshly started server, whole
   suite, no mid-run restarts.
7. Parity: if the endpoint data is mirrored by MCP, smoke-test the MCP side too.

## Probe first, assert second

Derive assertions from **observed responses of a freshly built server**
(`curl` + `jq`), not from reading handler C++. Handlers can diverge from wire
reality — object-vs-array choices, extra fields, renamed keys. Reading the C++
answers "why is it shaped this way"; probing answers "what is on the wire".
The 2026-09 actualization fixed an entire class of test bugs by switching to
probe-first (e.g. `banks` was asserted as a list but is an object).

## Contract gotchas (verified on master)

- **Memory banks**: `GET .../memory/ram` returns `banks` as an **object keyed
  `bank0`..`bank3`** (PENTAGON), each with `address_range`/`type`/`page` — not an array.
- **AY registers**: chip endpoint returns `registers` as an **object of 16
  descriptive names to ints**; the single-register endpoint returns
  `value_dec`/`value_bin`/`value_hex`.
- **Disk drives**: 4 drives A-D on PENTAGON; drive status key is `mounted`
  (not `inserted`).
- **Settings values must be JSON-native** (bool/number, not strings). Sending
  `"false"` as a string currently makes the server return 500 — a known
  server-side wart, deliberately not encoded in tests. If you fix the server,
  tighten the tests in the same task; never bless a wart in assertions.
- **BASIC timing**: a fresh 128K-class boot lands in `menu128k`; `run` must
  navigate the menu, and ENTER injected during ROM boot (< ~1s after create)
  is swallowed. The fixture's 0.5s settle plus the server-side poll/re-press
  loop handle this — do not remove either.
- **BASIC semantics**: `inject` (program mode) writes a deterministic binary
  and `extract` returns the program text plus a trailing newline;
  `clear` is equivalent to NEW (VARS == PROG).
- **Models**: `GET .../models` carries `creatable` flags; a create for a
  non-creatable model is 400 with a reason — never a silent 48K fallback.
- **`run_frames`** (`POST .../run_frames {"count": N}`) is the deterministic
  way to advance a paused instance; mouse/keyboard timing tests rely on it.

## Fixture semantics and timing rules

- `api_client` (session): one server for the whole run. Never restart the
  server mid-suite; a restart invalidates every emulator id.
- `active_emulator` (function): creates **PENTAGON** (48K has no disk drives),
  settles 0.5s after start, teardown stops then deletes, printing instead of
  raising on teardown errors.
- The fixture sleeps are the **only sanctioned fixed sleeps**. In test bodies,
  never sleep to wait for an API effect — poll the endpoint or use
  `run_frames`. (Same philosophy as the C++ `TestWait` rule in root AGENTS.md.)
- Restore anything you change on a shared surface inside `finally`
  (see `test_fast_tape_round_trip`).
- Skip — don't fail — when optional prerequisites are missing (fixtures,
  interpreter availability). Skipping communicates "not covered here",
  failing communicates "broken".

## Test-writing rules

- Assert **shapes and values you probed**, using subset checks (`in`, key
  membership) for payloads that legitimately evolve; never assert incidental
  key ordering.
- Negative paths: assert status, an error-message fragment, and — where the
  contract promises it — that state is unchanged afterwards.
- Unknown-emulator class: the all-zeros UUID expecting 404, one per route
  family (pattern in `test_api_basic.py`).
- Parametrize matrices (bad request bodies, routes) instead of copying tests
  (pattern in `test_api_mouse.py`).
- Keep each test to one concern; a failure should point at one contract clause.
- Housekeeping: no artifacts outside `reports/`/`.pytest_cache/`; scratch
  files (if a test ever needs one) go to the repo `scratch/` rule.

## Failure triage

- **Burst of `ConnectionRefused` mid-run** = the server died. Get the OS crash
  report (macOS: `~/Library/Logs/DiagnosticReports/*.ips`), find the last test
  that ran, reproduce with that single file, and **fix the server**. Never add
  client retries or rerun-loops to hide a crash. Historical example: the HUD
  feature-notification use-after-free that this suite caught in 2026-09.
- **Everything fails from the first test** = wrong URL, stale server binary,
  or server not up. Check `curl -s http://localhost:8090/api/v1/emulator`.
- **One domain red after your API change** = expected drift; probe the new
  contract and actualize that domain file.
- **Flaky-looking timing failures** = suspect a missing poll; convert the wait
  to polling or `run_frames` before touching timeouts.

## Updating the suite after an API change

1. Rebuild, restart the server fresh (root `AGENTS.md` sequence).
2. Probe the changed endpoints with curl; capture real JSON.
3. Update `api_client.py` wrappers if routes/params/status codes moved.
4. Update the domain test file against probed reality.
5. Run the domain file, then the full suite on one server instance.
6. If the OpenAPI spec changed: `verify_openapi_coverage.py`, then the sweep
   (`run_tests.sh`) for a fresh report.
7. Update the gotchas above if a new wire-shape surprise emerged.

## Done checklist

- [ ] `ninja -C cmake-build-release` clean, zero warnings
- [ ] `verify_openapi_coverage.py` clean
- [ ] Full pytest suite green on one freshly started server (no restarts)
- [ ] C++ tests for any touched server code green (`core-tests`)
- [ ] This file and `README.md` updated if procedures, layout, or gotchas changed
- [ ] Nothing committed — root policy requires an explicit user instruction
