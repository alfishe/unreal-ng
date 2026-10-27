# DZRP Protocol Verification

Verification tools for the DeZog Remote Protocol implementation.

## Files

| File | Description |
|------|-------------|
| `dzrp_client.py` | Python DZRP client library (DeZog-shaped: tolerates interleaved notifications) |
| `verify_dzrp_protocol.py` | Protocol verification against the **mock** `dezog-test-server` |
| `verify_dzrp_emulator.py` | End-to-end verification against a **real emulator** (unreal-qt) |
| `gen_condition_cases.mjs` | Writes `testdata/debugger/conditions/dezog-cases.tsv`: random DeZog breakpoint conditions and the truth DeZog's own token replacement + JavaScript `eval` gives them; `DezogCondition_test` checks the core translation against it (`node tools/verification/dezog/gen_condition_cases.mjs > testdata/debugger/conditions/dezog-cases.tsv`) |

## Usage

### Mock server verification (protocol layer only)

`dezog-test-server` is behind the CMake option `BUILD_DEZOG_TEST_SERVER` (OFF by default) and is not part of
`all`: configure once with `-DBUILD_DEZOG_TEST_SERVER=ON`, then build the target.

```bash
tools/build/build.sh dezog-test-server
./cmake-build-agent-release/bin/dezog-test-server 12345 &
python3 tools/verification/dezog/verify_dzrp_protocol.py --port 12345
```

### Real emulator verification (production module)

The DeZog module is started by `Automation::start()` in every host
(unreal-qt, unreal-videowall, standalone `automation`) when the build has
`ENABLE_DEZOG_AUTOMATION=ON` (default). Port: `UNREAL_DEZOG_PORT` env var,
else 12000.

```bash
# Unattended with the normal build: unreal-qt offscreen on ports of its own (WebAPI --port-base, default 18170;
# CLI +100, MCP +200, GDB +300, DeZog +400, ZRCP +500), one muted machine of --model, stopped at the end
Q=cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt
python3 tools/verification/dezog/verify_dzrp_emulator.py --launch-qt $Q --model PENTAGON
python3 tools/verification/dezog/verify_dzrp_emulator.py --launch-qt $Q --model NEXT --port-base 18180
python3 tools/verification/dezog/verify_dzrp_emulator.py --launch-qt $Q --model 48K --port-base 18160

# The headless host (CMake option BUILD_DEZOG_EMULATOR_HOST=ON, target dezog-emulator-host)
python3 tools/verification/dezog/verify_dzrp_emulator.py --launch --build-dir cmake-build-agent-release --port 12010

# Against an already running host on the default port 12000
python3 tools/verification/dezog/verify_dzrp_emulator.py
```

Logs of a launched host go to `scratch/verification-dezog/`. Result lines: `PASS`, `FAIL` (exit code 1),
`KNOWN` (a known server issue reproduced; not counted), `SKIP` (not for this machine type).

`dezog-emulator-host [port] [model]` (built from `core/automation/dezog/test/emulator-host.cpp`)
creates one real emulator instance, starts it and exposes it through the
production `AutomationDezog` module — no GUI, no CLI/WebAPI ports. It is also
the easiest way to attach the DeZog VS Code extension to a headless emulator.
Note that unreal-qt only creates an emulator instance when the user starts one
from the UI (`--launch-qt` creates it through the WebAPI); until then a connected DeZog sees machine type `UNKNOWN`.

The script pauses the running instance, captures its state, installs small Z80 programs at 0x8000-0x8600 and
drives every command of the dispatcher (below), then restores the captured state and resumes (on the ZX Next,
which has no `.sna`, only the slots are put back).

### Unit / integration tests (GTest)

All DeZog suites are wired into the main `core-tests` binary and run against a
live emulator instance (no external process, ephemeral ports):

```bash
ninja -C cmake-build-release core-tests
./cmake-build-release/bin/core-tests \
    --gtest_filter='DZRPProtocolTest.*:DezogDebugAdapter_test.*:DZRPServer_test.*:AutomationDezog_test.*'
```

| Suite | Covers |
|-------|--------|
| `DZRPProtocolTest` | framing, LE helpers, seq masking, oversize/coalesced frames, capability bitfield |
| `DezogDebugAdapter_test` | registers (16/8-bit, alt, IX/IY halves), memory wrap, slots/ROM aliases, WRITE_BANK, BP owner/temp tracking, WP per-byte expansion + clamping, MANUAL/BREAKPOINT/WATCHPOINT notifications, cross-instance filtering, state round-trip, border |
| `DZRPServer_test` | real TCP session: INIT, capabilities, unknown cmd → empty ACK, regs/mem/slots/bank/border over the wire, CONTINUE → NTF_PAUSE, temp-BP step + auto-clear, PAUSE ordering (ACK before NTF), watchpoints, READ/WRITE_STATE, reconnect |
| `AutomationDezog_test` | port resolution (arg / env / default / garbage), start/stop idempotency, client handshake through the module, busy-port failure |

## Test Coverage

### Real emulator (`verify_dzrp_emulator.py`)

Every command `dzrp::Server::handleCommand` dispatches, with DeZog's payloads (`dzrpbufferremote.ts`) and the
reply checked against the machine. Runs on a 128K (`PENTAGON`), a `NEXT` and a `48K`; the 128K / Next rows are
skipped elsewhere.

| Command | Checks |
|---------|--------|
| CMD_INIT (1) | DeZog's `onConnect` rules: error 0, DZRP 2.x, a known machine type, a program name |
| CMD_CLOSE (2) | ACK; then a second session (INIT, GET_REGISTERS, CLOSE) on the same server |
| CMD_GET_REGISTERS (3) | every field after every CMD_SET_REGISTER; slot layout per machine (128K ROM 8/9, 5, 2; 48K 0, 1; Next 8 MMU banks) |
| CMD_SET_REGISTER (4) | every id 0-35 (16-bit, 8-bit merged into its pair, shadow set, I, R with bit 7, IM incl. clamping 5 -> 2); R counts M1 cycles and keeps bit 7 over 4 NOPs |
| CMD_WRITE_BANK (5) | 128K: 16K bank seen through slot 3 / 1 / 2; Next: an 8K bank as DeZog's `loadBinSna` sends it |
| CMD_CONTINUE (6) | plain; DeZog's step shapes: bp1 + bp2 for step into CALL / RET, bp1 for step over CALL and step out; the temporaries are gone after the stop; a step from a user breakpoint passes it once and keeps it; 30 steps = 30 notifications |
| CMD_PAUSE (7) + NTF_PAUSE | MANUAL while running (address = PC) and when already paused; BREAKPOINT / WATCHPOINT reasons and addresses on every stop |
| CMD_READ_MEM (8) / CMD_WRITE_MEM (9) | 4 B and 4 KB round trips, the 64K wrap, a full 64K read; 128K: writes land in the bank the slot shows; Next: slot 7 |
| CMD_SET_SLOT (10) | 128K: RAM banks in slots 1-3, ROM 8 / 9, aliases 0xFE / 0xFF; Next: MMU registers (GET_TBBLUE_REG agrees) |
| CMD_GET_TBBLUE_REG (11) | Next: NR #00 = 10, NR #50-#57 = the slots; elsewhere one zero byte |
| CMD_SET_BORDER (12) | the border in the saved `.sna` (header byte 26), masked to 3 bits |
| CMD_GET_SPRITES_PALETTE (16) | Next: entries written through NR #43 / #40 / #41, both sprite palettes, the 9th blue bit |
| CMD_GET_SPRITES_CLIP_WINDOW_AND_CONTROL (17) | Next: NR #19 clip window + NR #15 |
| CMD_GET_SPRITES (18) / CMD_GET_SPRITE_PATTERNS (19) | Next: attributes and a pattern written by the CPU (OUT / OTIR to #303B, #57, #5B) |
| CMD_READ_PORT (20) / CMD_WRITE_PORT (21) | one-byte reply / empty ACK; the device behind it: AY register 1 round trip, 128K `#7FFD` paging, Next sprite port |
| CMD_GET_SUPPORTED_COMMANDS (24) | the exact set (the Next one adds 11, 16-19, 25-27) |
| CMD_READ_BANK_MEM (25) / CMD_WRITE_BANK_MEM (26) | Next: round trip, the offset wraps in the 8K bank, the bank through a slot, bank 0xFF = the ROM |
| CMD_SET_NEXTREGS (27) | Next: MMU6 / MMU7 pairs seen in the slots and NR reads |
| CMD_READ_MEM_BLOCKS (28) | the blocks back to back, equal to separate reads |
| CMD_ADD_BREAKPOINT (40) | plain (two ids, both stop); long addresses (128K bank 2 stops, bank 4 not; the ROM bank slot 0 shows, both `#7FFD` ROMs; Next 8K bank); DeZog conditions (below) |
| CMD_REMOVE_BREAKPOINT (41) | removed ones never stop again; an unknown id is an empty ACK |
| CMD_ADD_WATCHPOINT (42) / CMD_REMOVE_WATCHPOINT (43) | write, read, a 4-byte range (the address of the byte hit), `rw`, a long (bank) address, error 1 for access 0 / size 0, idempotent add and remove |
| CMD_READ_STATE (50) / CMD_WRITE_STATE (51) | registers, memory and a paged-out bank come back; the original state is restored at the end |
| CMD_GET_HISTORY_INFO / ENTRY (0xE0 / 0xE1) | TTD history: 10 entries coherent with memory, 200 + deep jumps (under DeZog's 5 s), out of range harmless, continue after browsing |
| unknown (99, 0xFF) and unhandled DZRP 13, 14, 15, 22, 23 | an empty ACK (no NAK: DeZog compares the seq byte raw), the session goes on |

**Conditional breakpoints** (CMD_ADD_BREAKPOINT's condition, translated by `dezogcondition.cpp`): a loop counts B
(and DE in 16 bits) past a breakpoint; every stop is checked, so a stop where the condition is false fails.
`B == 5` stops at B = 5 only (DE 4, 260, 516); `b == 0x05 && A != 0`; `B == 1 && A != 0` never stops; a shadow
register (`B' == 7 && B == 3`, `B' == 6 && B == 3` never); numbers `$0A`, `0Bh`, `1100b`, `0x0D`, decimal, `||`,
`&` with parentheses; `DE == 0x0100`, `PC == 0x8007 && B == 2`. Conditions the core refuses (`B / 2 == 2`,
`LABEL == 1`, `B == FFh`, `2 ** 3 == 8`) fall back to an unconditional breakpoint: every pass stops. Each one is
removed and must not stop again; a step onto a conditional breakpoint's address keeps it.

### Mock server (`verify_dzrp_protocol.py`)

The verifier exercises **all 18 advertised commands** plus notifications and
protocol robustness (34 tests total). Seq-number echo is asserted on every
command/response exchange.

| Test | Command | Description |
|------|---------|-------------|
| Connection | - | TCP connect |
| CMD_INIT | 1 | Version handshake (default + DeZog's real `(2,0,0)`) |
| CMD_GET_SUPPORTED_COMMANDS | 24 | Capability query (full 18-command set asserted) |
| CMD_GET_REGISTERS | 3 | Read all Z80 registers + slots |
| CMD_SET_REGISTER | 4 | Write PC + 8-bit sub-register (A, merge semantics) |
| CMD_READ_MEM | 8 | Length, known-pattern values, 64K wrap round-trip |
| CMD_WRITE_MEM | 9 | Write + read-back |
| CMD_SET_SLOT | 10 | Slot mapping, verified via GET_REGISTERS slots |
| CMD_WRITE_BANK | 5 | Bank write (ACK + error byte) |
| CMD_SET_BORDER | 12 | Border color (masked to 3 bits) |
| CMD_ADD_BREAKPOINT | 40 | Plain + condition string |
| CMD_REMOVE_BREAKPOINT | 41 | Valid id + invalid id (session survives) |
| CMD_ADD_WATCHPOINT | 42 | Read and write access types |
| CMD_REMOVE_WATCHPOINT | 43 | Remove by key |
| CMD_PAUSE | 7 | Pause execution |
| CMD_CONTINUE | 6 | Resume, with temp BPs (DeZog step-over shape) |
| NTF_PAUSE | - | Async stop notification: reason + address asserted |
| CMD_READ_STATE | 50 | Capture state |
| CMD_WRITE_STATE | 51 | Restore state |
| Unknown cmd | - | Empty ACK (no NAK bit), session survives |
| Seq echo | - | Sequence number echo validation |
| Fragmented frames | - | Byte-at-a-time delivery |
| Coalesced frames | - | Two commands in one TCP segment |
| CMD_CLOSE | 2 | Disconnect |

## DeZog Integration

To test with actual DeZog extension, use this `launch.json`:

```json
{
    "type": "dezog",
    "request": "launch",
    "name": "Unreal-NG Debug",
    "remoteType": "cspect",
    "cspect": {
        "hostname": "localhost",
        "port": 12000,
        "supportedCommands": "1,2,3,4,5,6,7,8,9,10,12,24,40,41,42,43,50,51"
    },
    "rootFolder": "${workspaceFolder}",
    "startAutomatically": false
}
```

**Important notes:**
- DeZog uses `remoteType: "cspect"` for DZRP connections (CSpect uses the same protocol)
- `supportedCommands` is required to enable watchpoints (42/43) and state save/restore (50/51)
- Without it, WPMEM/assertions and reverse debugging are disabled client-side
- CMD_GET_SUPPORTED_COMMANDS (24) is implemented but DeZog doesn't query it
