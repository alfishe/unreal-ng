# Recipe: mouse input

Move the mouse, press and click its buttons and roll its wheel from an agent or a
script, and check what the guest reads. Every mouse of the machine gets the same
input: the emulator has one mouse manager, and each mouse device of the machine is
fed from it (design:
[docs/inprogress/2026-10-02-mouse-manager/design.md](../../docs/inprogress/2026-10-02-mouse-manager/design.md)).

The guest reads three registers at the Kempston mouse addresses:

| Port | Register |
|:--|:--|
| `#FADF` | buttons, active low: bit 0 left, bit 1 right, bit 2 middle; bit 3 = 1; bits 7-4: wheel counter (wheel-equipped mice) or `1111` |
| `#FBDF` | X counter, 8 bits, grows to the right |
| `#FFDF` | Y counter, 8 bits, grows **upward** |

Worked example: the mouse moves 5 right and 3 up with the left button held. X goes
from 31 to 36, Y from 85 to 88, and `#FADF` reads `#FE` on a plain Kempston
interface.

Which mouse each machine has:

| Machine | Mouse | Notes |
|:--|:--|:--|
| 48K, 128K, +3, Pentagon, Scorpion, Profi | Kempston interface | `[INPUT] Mouse=KEMPSTON|NONE`, feature `kempstonmouse`; power-on X = 31, Y = 85 |
| `ATM3` (ZX-Evo), `TSL` (TS-Conf) | PS/2 wheel mouse on the AVR | the AVR keeps the registers ([evoavrmouse.h](../../core/src/emulator/memory/atm/evoavrmouse.h)): found X = 0, Y = 1; `Mouse=NONE` = no mouse plugged in, all three read `#FF`; the wheel nibble goes **down** when the wheel rolls away; resolution 1-8 counts/mm with keypad `+` / `-` / `*` while left and right are held (default 1 = one count per pixel) |
| `SPRINTER` | the board mouse: port code `#58` and the serial mouse on SIO B | always fitted; `Mouse=NONE` removes only the optional Kempston interface |

## Timing (read this once)

The machine's own thread owns input while the emulator loop lives. A running
machine applies a change at its next instruction. A **paused** machine applies it
on its **next executed instruction** (`run_frames`, a step, resume), so the `state`
echoed in the reply can be the state *before* the change. The reproducible
pattern: pause, inject, `run_frames N`, read.

Buttons from automation and from the host's mouse are kept apart and joined:
an automation press stays held while the user clicks, and the other way round.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use
> [WebAPI](#webapi) only inside host-side pipelines or when MCP is unavailable
> (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```json
{"tool": "mouse_input", "arguments": {"action": "move", "dx": 5, "dy": 3}}
{"tool": "mouse_input", "arguments": {"action": "press", "button": "left"}}
{"tool": "mouse_input", "arguments": {"action": "release", "button": "left"}}
{"tool": "mouse_input", "arguments": {"action": "click", "button": "right"}}
{"tool": "mouse_input", "arguments": {"action": "wheel", "steps": 1}}
{"tool": "mouse_input", "arguments": {"action": "buttons", "pressed": ["left", "middle"]}}
{"tool": "mouse_input", "arguments": {"action": "release_all"}}
{"tool": "mouse_input", "arguments": {"action": "status"}}
```

`dx` / `dy` are -127..127 per call (split larger moves with `run_frames` in
between); the wheel takes -7..7 steps, `+` = away from you; `click` takes an
optional `frames` (default 2).

Worked example (TS-Conf, checked live): a fresh `TSL` reads `FADF` #FF, `FBDF` 0,
`FFDF` 1 (the AVR found the mouse). Pause, `move` 7 / -2, `press` left, `wheel`
1, `run_frames` 2: `FBDF` 7, `FFDF` #FF, `FADF` #EE (wheel nibble F - 1 = E, left
pressed).

## WebAPI

```bash
B=http://localhost:8090/api/v1/emulator; ID=<id>; J='Content-Type: application/json'
curl -s -X POST $B/$ID/mouse/move     -H "$J" -d '{"dx":5,"dy":3}'
curl -s -X POST $B/$ID/mouse/press    -H "$J" -d '{"button":"left"}'
curl -s -X POST $B/$ID/mouse/release  -H "$J" -d '{"button":"left"}'
curl -s -X POST $B/$ID/mouse/click    -H "$J" -d '{"button":"right","frames":2}'
curl -s -X POST $B/$ID/mouse/buttons  -H "$J" -d '{"pressed":["left","middle"]}'
curl -s -X POST $B/$ID/mouse/wheel    -H "$J" -d '{"steps":1}'
curl -s -X POST $B/$ID/mouse/release_all
curl -s -X POST $B/$ID/mouse/counters -H "$J" -d '{"x":128,"y":96}'   # debug: set the counters
curl -s $B/$ID/mouse/status | jq '{ports, buttons, present, routing}'
```

Assert on `ports` (`FADF`, `FBDF`, `FFDF`: what the guest's `IN` returns right now,
from whichever mouse the machine has) and `buttons`, never on the message text.
`x` / `y` / `button_mask` are the Kempston interface's own counters. Status codes:
400 for a bad name or range, 404 for an unknown id, 409 while a TTD replay drives
the machine.

## CLI, Lua, Python

```text
mouse move 5 3
mouse press left          mouse release left
mouse click right 2       mouse buttons left,middle
mouse wheel 1             mouse clear
mouse status
```

```lua
mouse_move(5, 3); mouse_click("left", 2); mouse_wheel(1)
print(mouse_status().ports.FBDF)
mouse_release_all()
```

```python
emu.mouse_move(5, 3); emu.mouse_click("left", 2); emu.mouse_wheel(1)
print(emu.mouse_status()["ports"])
emu.mouse_release_all()
```

## The host mouse (unreal-qt)

- A click on the screen captures the mouse when the machine has a mouse device;
  that click is not passed to the machine. `Ctrl+Esc` (the Control key, on macOS too;
  `[INPUT] MouseReleaseKey=`), focus loss or switching to another application
  releases it. A plain `Esc` reaches the machine.
- The mouse button on the toolbar shows the state: normal (click the screen to
  capture), highlighted (captured), crossed out (host mouse off), grayed out (no
  mouse device). Clicking it turns the host mouse off or on; View → Host Mouse
  Enabled is the same switch. Off: the host mouse never reaches the machine.
  Automation input is not affected.
- Speed: by default the captured mouse moves the guest as far as the host
  pointer would move over the picture, at any window size, zoom or DPI, with the
  host's own pointer speed and acceleration (macOS, Windows and Linux alike; the
  guest software's own acceleration comes on top). View → Mouse Follows Host
  Pointer Speed off: one host pixel of travel is one mouse count, whatever the
  window. Switchable at any time; `[INPUT] MouseScale=` (2^-3..2^3) applies in
  both modes.

## Pitfalls

- **Paused machine:** the reply's `state` is from before the change; run frames,
  then read `status`.
- **ZX-Evo / TS-Conf after `counters`:** the AVR registers take the values as
  given; detection routines that expect the found values (X = 0, Y = 1) see
  whatever was set.
- **TTD replay:** live input is refused (409) while recorded input drives the
  machine.
