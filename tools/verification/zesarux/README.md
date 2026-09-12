# ZRCP Protocol Verification

Verification tools for the ZEsarUX ZRCP (ZEsarUX Remote Control Protocol)
server that impersonates ZEsarUX for DeZog's `remoteType: "zrcp"` sessions.

## Files

| File | Description |
|------|-------------|
| `zrcp_client.py` | Python ZRCP client library (DeZog-shaped: prompt framing, blank-line interrupt, register/history line parsers) |
| `verify_zrcp_emulator.py` | End-to-end verification against a **real emulator** in any running host |

## Usage

The verifier connects to an already running host; it does not launch anything.

```bash
# Against a host on the default port 10000
python3 tools/verification/zesarux/verify_zrcp_emulator.py

# Custom host / port
python3 tools/verification/zesarux/verify_zrcp_emulator.py --host 192.168.1.10 --port 10010
```

The ZEsarUX module is started by `Automation::start()` in every host
(unreal-qt, unreal-videowall, standalone `automation`) when the build has
`ENABLE_ZESARUX_AUTOMATION=ON` (default; requires `ENABLE_DEZOG_AUTOMATION`).
Port: first CLI-style arg for the standalone hosts, else `UNREAL_ZRCP_PORT`
env var, else 10000. In unreal-qt an emulator instance must be started from
the UI first — the module reports machine UNKNOWN until then. The unattended
smoke path for this protocol is the GTest suite (`core/tests/automation/zesarux/`),
which spins the whole stack up in-process on ephemeral ports.

The script replays DeZog's exact connect/init/disconnect sequences, installs a
small Z80 loop at 0x8000, exercises registers, breakpoints (conditions + pass
counts), watchpoints, step/step-over, interruptable run, reverse debugging,
extended-stack and the misc queries, then restores the captured registers and
memory.

## Test Coverage

The verifier exercises the full ZRCP command surface DeZog touches (19 steps).

| Step | Commands | Description |
|------|----------|-------------|
| Connect | — | Welcome banner + prompt handshake |
| Init sequence | `close-all-menus` … `cpu-history ignrepldxr` | DeZog's exact on-connect stream, order included (100× `disable-breakpoint`, code coverage, history setup) |
| Register line | `get-registers` | Byte-exact `print_registers` shape: field order, two spaces after `R=`, flags charset, `MEMPTR=0000`, `IM`/`IFF--`, 8 contiguous MMU words (first 4 = 16 KB slots, groups 5–8 repeat) |
| Registers write | `set-register` | Decimal values, 8-bit merge (A), `AF'`, `IFF1` ack-and-ignore |
| Memory | `read-memory` / `write-memory-raw` | Uppercase contiguous hex round-trip; full 64 KiB in one request (DeZog `fetch64kMemory`: answer must be exactly `len*2` hex chars, split halves equal, `len > 65536` → `Error. Invalid length`, wrap at 0xFFFF) |
| Disassembly | `disassemble` | 7-char prefix `%04X %X ` + uppercase mnemonics |
| Stepping | `cpu-step`, `cpu-step-over` | PC advance; CALL → temp bp after the call, single-line output |
| Breakpoints | `set-breakpoint[action]`, `enable/disable`, passcount | `run` banner + `Breakpoint fired:` echo; condition false → silent auto-resume; condition true → stop with condition; pass count skips hits; DeZog's `set-breakpoint <id> 0` removal idiom |
| Watchpoints | `set-membreakpoint` | Write watchpoint fires with `Memory Breakpoint Write Address:`, type 0 removes |
| Reverse debugging | `cpu-history get/size/…` | Entries coherent ((PC) opcodes match memory), deep index, cache stability, out-of-range error |
| Extended stack | `extended-stack` | `call`/`rst`/`push` classification lines |
| Misc | tstates/frequency/`get-memory-pages` | Integer replies; `RO1 RA5 RA2 RA0 `-style tokens with trailing space |
| Disconnect | quit sequence | DeZog's 8-command teardown, socket close, server survives + accepts a new session |

## DeZog Integration

A ready-made VS Code project with this config, helper tasks (launch unreal-ng,
create emulator, load demo snapshot) and docs lives at
`docs/inprogress/2026-08-27-dezog-integration/test-project/`.
To test with the actual DeZog extension, use this `launch.json`:

```json
{
    "type": "dezog",
    "request": "launch",
    "name": "Unreal-NG Debug (ZRCP)",
    "remoteType": "zrcp",
    "zrcp": {
        "hostname": "localhost",
        "port": 10000
    },
    "rootFolder": "${workspaceFolder}",
    "startAutomatically": false
}
```

Notes:
- `remoteType: "zrcp"` enables DeZog's native ZEsarUX features that the DZRP
  (`remoteType: "cspect"`, port 12000) path does not expose client-side:
  watchpoints without `supportedCommands`, and reverse debugging through the
  ZesaruxCpuHistory UI (`cpu-history`)
- Both servers can listen concurrently (10000 ZRCP + 12000 DZRP); only one
  DeZog session should own breakpoints at a time
- The DZRP-path verifier lives in `tools/verification/dezog/`

## GTest

The C++ side is covered by `core/tests/automation/zesarux/` (condition
evaluator truth tables, byte-exact wire goldens, full scripted sessions) — see
`docs/inprogress/2026-08-27-dezog-integration/zrcp-server.md`.
