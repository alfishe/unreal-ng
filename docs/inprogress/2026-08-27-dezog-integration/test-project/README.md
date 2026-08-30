# DeZog ⇄ unreal-ng Test Project (ZRCP)

A minimal, self-contained VS Code **DeZog** project that debugs the bundled
border-cycling demo on **unreal-ng**, attached over the **ZEsarUX ZRCP text
protocol** (unreal-ng impersonates ZEsarUX; `core/automation/zesarux/`).

Status: **validated end-to-end against DeZog 3.7.4** — attach, registers,
disassembly, breakpoints (incl. conditions/pass count), watchpoints, stepping,
memory views and native instruction-level reverse debugging all work over this
configuration.

## Layout

```
test-project/
├── .vscode/
│   ├── launch.json     # DeZog "zrcp" attach config — heavily commented
│   └── tasks.json      # build / launch / verify helper tasks
├── src/
│   ├── main.asm        # tiny 48K border-cycling loop (breakpoint-friendly)
│   └── main.sna        # ready-to-load snapshot (regenerate: make-sna.py)
├── make-sna.py         # builds src/main.sna WITHOUT any assembler (python3)
└── README.md
```

## Prerequisites

1. **DeZog** VS Code extension `maziac.dezog` (3.x; tested with 3.7.4).
2. **unreal-ng built** at the repo root:
   `cmake -S . -B cmake-build-release -G Ninja && ninja -C cmake-build-release`
3. `python3` (snapshot regeneration, JSON parsing in tasks) and `curl`
   (emulator setup via WebAPI). `sjasmplus` is optional (source-level debug).

## Quick start

Open **this folder** (`test-project/`) in VS Code, then:

| Step | Action | What happens |
|------|--------|--------------|
| 1 | Task: **`launch unreal-ng + load demo (ZRCP :10000)`** | starts `unreal-qt` detached, creates a 48K emulator instance via the WebAPI (`POST /api/v1/emulator/start`) and loads `src/main.sna` into it (`POST /api/v1/emulator/{id}/snapshot/load`) |
| 2 | Press **F5** ("Attach to unreal-ng (ZRCP)") | DeZog connects to `:10000`, runs its init handshake, UI populates |
| 3 | Debug: set breakpoints, step, inspect… | see "Things to try" below |
| 4 | Task: **`stop unreal-ng`** | kills the detached emulator |

Manual alternative (no tasks): start `unreal-qt` yourself, create an emulator
instance and load `src/main.sna` via the UI (or WebAPI), then F5. Everything
works identically — the tasks only automate those host-side steps.

### Things to try

- **Breakpoint** at `0x8004` (`out (0xFE), a`) — hits every border change;
  watch the border colour in the emulator window and register A.
- **Condition**: breakpoint condition `a=3` stops only on colour 3.
- **Watchpoint** on `0x801B` (`counter`, write) — the zrcp remote's signature
  feature over the DZRP/"cspect" path.
- **Reverse debugging**: run a while, then use DeZog's step-back / history
  view — instruction history is served from unreal-ng's TTD recording
  (`cpu-history` commands; see `../reverse-debugging.md`).
- **Memory view**: watch `0x801B` (`counter`) increment 0..7.

## How it fits together

```
VS Code + DeZog ──ZRCP text :10000──▶ zrcp::Server ──▶ DezogDebugAdapter ──▶ Emulator
      │                                                     ▲
      └──(tasks only)──HTTP :8090──▶ WebAPI: start + load ──┘
```

- DeZog talks **ZRCP** (`remoteType: "zrcp"`) to `core/automation/zesarux/`.
  Protocol details: `../zrcp-server.md`.
- Program loading/reset happens **host-side** (unreal-qt UI or WebAPI) —
  DeZog's `load`/`smartload`/`hard-reset-cpu` are acknowledged no-ops
  ("machine resets are owned by the host emulator" appears in DeZog's output;
  that Log line is expected on every attach).
- The DZRP server (`remoteType: "cspect"`, port 12000) may run **concurrently**
  — let only one DeZog session own breakpoints at a time.

## Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| "machine is unknown" or attach stops after `get-current-machine` | No emulator instance running — run the launch task first (ZRCP reports UNKNOWN until one exists) |
| "ZEsarUX did not answer in time!" | Server older than the transport fixes (prompt framing / log-line stripping) — rebuild; then check port 10000 actually carries unreal-ng |
| "Connection refused" on F5 | unreal-qt not running, or `UNREAL_ZRCP_PORT` differs from `launch.json` |
| Task "launch + load" curl errors | WebAPI not up yet (check `scratch/dezog-test-project.log`) or port 8090 occupied by a stale instance — `pkill -9 unreal-qt`, retry |
| Registers/memory frozen | Emulator paused by the UI, not the debugger — resume in unreal-qt or via DeZog Continue |

## Headless verification (no VS Code)

The ZRCP server the debug session rides on is exercised by the Python verifier
(19 steps, incl. a byte-faithful replay of DeZog 3.7.4's init sequence and the
full-64K memory fetch) and by GTests:

```
python3 ../../../../tools/verification/zesarux/verify_zrcp_emulator.py --port 10000
./cmake-build-release/bin/core-tests --gtest_filter='*Zesarux*:*Zrcp*'
```
