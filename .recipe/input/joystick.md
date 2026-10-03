# Recipe: Kempston joystick input

Hold, release, set and tap joystick buttons from an agent or a script. The Kempston
joystick is one byte, active high (right `0x01`, left `0x02`, down `0x04`, up `0x08`, fire `0x10`,
`b5`..`b7` free); the guest reads it with `IN #1F`. Worked example: up and fire held reads `0x18`.

Which machines let the guest see it (the others accept the input with a warning):

| Model | Port | Note |
|:--|:--|:--|
| `ATM3` (ZX-Evo) | `IN #1F` outside shadow mode | in shadow (TR-DOS active or `#BF` bit 0) `#1F` is the floppy controller; the ROM service menu is outside shadow |
| `SCORPION`, `PROFSCORP` | `#FF1F` | the Service Monitor's `IN #FF1F` idles at `0x00` |
| `TSL` (TS-Conf) | `#1F` | |
| `PENTAGON` (128 K, 512 K, 1024 K) | `#1F` outside a TR-DOS session | in a TR-DOS session `#1F` is the floppy controller |
| `PROFI`, `PROFI3` | `#1F` in the normal port set | |
| `SPRINTER` | `#1F` / `#0F` outside TR-DOS, `#FF` in it | the PLD's fixed-port rewrite (code `#15`) |

Not wired (accept the input and warn): `48K`, `128k`, `PLUS2`, `PLUS2A`, `PLUS3`, `ATM710`, `ATM450`
(`ATM3` is the only ATM board with the joystick decoder).

Fitting: `[INPUT] Joystick=KEMPSTON|NONE` and the runtime feature `kempstonjoystick` (on by
default). Not fitted: the port reads `0x00` and every input call warns. Host keypad keys
(`kp_8` up, `kp_2` down, `kp_4` left, `kp_6` right, `kp_0` fire; `[INPUT] JoystickKeys=` overrides, empty
disables) drive the same byte in the core: there is nothing to call for them.

Ground truth: [debugjoystickmanager.h](../../core/src/debugger/joystick/debugjoystickmanager.h),
design [tdd-kempston-joystick.md](../../docs/inprogress/2026-09-15-atm-baseconf-highres-ports/tdd-kempston-joystick.md),
interface reference [command-interface.md section 13](../../docs/emulator/design/control-interfaces/command-interface.md#13-joystick-input-injection).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use [WebAPI](#webapi) only
> inside host-side pipelines or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## Timing (read this once)

The machine's own thread owns input while the emulator loop lives. A running machine applies a change
at its next instruction. A **paused** machine applies it on its **next executed instruction**
(`run_frames`, a step, resume), so the `state` echoed in the reply is the state *before* the change.
The reproducible pattern: pause, inject, `run_frames N`, read.

## MCP (preferred)

```json
{"tool": "joystick_input", "arguments": {"action": "press", "buttons": "up+fire"}}
{"tool": "joystick_input", "arguments": {"action": "release", "buttons": ["up"]}}
{"tool": "joystick_input", "arguments": {"action": "set", "state": 24}}
{"tool": "joystick_input", "arguments": {"action": "set", "buttons": []}}
{"tool": "joystick_input", "arguments": {"action": "tap", "buttons": "fire", "frames": 5}}
{"tool": "joystick_input", "arguments": {"action": "status"}}
```

`press` holds buttons and leaves the others; `set` makes exactly this state (a byte, or a list; `[]`
releases everything); `tap` presses and releases on its own after `frames` (default 2, 1..65535).
Errors are `isError` results carrying the shared wording, for example
`state=300 out of range 0..255` or `unknown joystick button 'jump' (up, down, left, right, fire, b5, b6, b7)`.

## WebAPI

```bash
B=http://localhost:8090/api/v1/emulator; ID=<id>; J='Content-Type: application/json'
curl -s -X POST $B/$ID/joystick/press   -H "$J" -d '{"buttons":"up+fire"}'
curl -s -X POST $B/$ID/joystick/release -H "$J" -d '{"buttons":["up"]}'
curl -s -X POST $B/$ID/joystick/set     -H "$J" -d '{"state":24}'          # or {"buttons":[]}
curl -s -X POST $B/$ID/joystick/tap     -H "$J" -d '{"buttons":"fire","frames":5}'
curl -s $B/$ID/joystick | jq '{state, port_value, pressed, wired, pending_tap, warning}'
```

Assert on `state` / `port_value` (the byte `IN #1F` returns) and `wired` (this machine's decoder answers
the joystick), never on the message text. Status codes: 400 for a bad name, type or range, 404 for an
unknown id, 409 while a TTD replay drives the machine, 500 without a joystick device.

### Worked example: see the guest read it (ATM3)

The service menu is outside shadow mode about 60 frames after reset. A guest loop
`DI; loop: IN A,(#1F); LD (#9000),A; JR loop` stores what it reads:

```bash
curl -s -X POST $B/$ID/pause
curl -s -X POST $B/$ID/run_frames -H "$J" -d '{"count":120}'
curl -s -X POST $B/$ID/memory/write -H "$J" -d '{"address":"0xC000","data":[243,219,31,50,0,144,24,249]}'
curl -s -X PUT  $B/$ID/registers/pc -H "$J" -d '{"value":49152}'
curl -s -X POST $B/$ID/joystick/set -H "$J" -d '{"state":24}'
curl -s -X POST $B/$ID/run_frames -H "$J" -d '{"count":3}'
curl -s "$B/$ID/memory/read/0x9000?length=1&format=full" | jq .data     # [24]
```

A read of `0x84` instead means the machine is still in shadow mode (that is the floppy controller's status).

## CLI / Lua / Python

```text
joystick press up+fire     # also: release, set 0x18 | up,fire | none, tap fire 5, clear, status, list
```

```lua
local st = assert(joystick_press("up+fire"))      -- nil, "message" on an error
print(st.state, st.port_value, st.wired)          -- 24 24 true
assert(joystick_tap("fire", 5)); run_frames(6)
```

```python
st = emu.joystick_press("up+fire")                # raises ValueError / RuntimeError on an error
emu.joystick_tap("fire", frames=5); emu.run_frames(6)
print(emu.joystick_state()["pressed"])
```

## Pitfalls

- Reading the state straight after a call on a paused machine shows the old state: `run_frames 1` first.
- `IN #1F` reads `0x00` when nothing is held (the Evo board's idle), not `0xFF`.
- During a TTD replay every live call is refused (409 / error): recorded input drives the machine.
- A 48K, 128K, +2, +2A, +3, ATM710 or ATM450 accepts the input and warns `this machine does not decode a Kempston joystick port` (see the table at the top).
