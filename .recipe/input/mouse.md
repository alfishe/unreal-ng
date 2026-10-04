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

Which mouse each machine has (status `device.id`; design:
[2026-10-03-mouse-api-routing](../../docs/inprogress/2026-10-03-mouse-api-routing/design.md)):

| Machine | Mouse (`device.id`) | Notes |
|:--|:--|:--|
| 48K, 128K, +2, +2A, +3, Pentagon, Scorpion, Profi, ZX-Poly | Kempston interface (`kempston`) | `[INPUT] Mouse=KEMPSTON|NONE`, feature `kempstonmouse`; power-on X = 31, Y = 85 |
| `ATM710`, `ATM450` | Kempston card on the ZX-bus (`kempston`) | the boards have none; the configs fit the card with a wheel (`Wheel=KEMPSTON`) |
| `ATM3` (ZX-Evo), `TSL`, `TSL-VDAC2` (TS-Conf) | PS/2 wheel mouse on the AVR (`evo-ps2`) | the AVR keeps the registers ([evoavrmouse.h](../../core/src/emulator/memory/atm/evoavrmouse.h)): found X = 0, Y = 1; `Mouse=NONE` = no mouse plugged in, all three read `#FF`; the wheel nibble goes **down** when the wheel rolls away; resolution 1-8 counts/mm with keypad `+` / `-` / `*` while left and right are held (default 1 = one count per pixel) |
| `SPRINTER` | the board mouse (`sprinter`): a Microsoft serial mouse on SIO B and the PLD's Kempston view (port code `#58`) of the same counters | always fitted; `Mouse=NONE` removes only the optional Kempston interface, which no Sprinter program reads. DSS 1.71 reads 3-byte packets at 1 200 baud (status `device.serial`), DSS 1.62.9x the ports; two buttons on the serial line (left, right), three in the port view; no wheel |

A machine with **no mouse fitted** (`Mouse=NONE` on a Kempston or ZX-Evo / TS-Conf machine)
refuses every input: WebAPI 409 `{"reason":"no_mouse"}`, Python `RuntimeError`, Lua
`nil, err`, CLI `Error:`. Status answers `mouse_fitted: false`, `device: null`.

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

`dx` / `dy` are -127..127 per `move`; for longer moves use **`glide`** (below);
the wheel takes -7..7 steps, `+` = away from you; `click` takes an optional
`frames` (default 2). `status` takes an optional `device` (`kempston`,
`sprinter`, `evo-ps2`).

```json
{"tool": "mouse_input", "arguments": {"action": "glide", "dx": -1000, "dy": 1000}}
{"tool": "mouse_input", "arguments": {"action": "click", "button": "left", "dx": 300, "dy": -40}}
{"tool": "mouse_input", "arguments": {"action": "status", "device": "sprinter"}}
```

## Long moves: glide, and homing

A program reads 8-bit counters (or packets of at most ±127), so between two of
its reads the mouse may move at most 127, or the program sees a move the other
way. Several `move` calls in a row on a running machine can land between two
reads and **wrap**. `glide` (±4096 per axis) does it right: the first step (at
most 127) now, then one step per frame once the program has read the last one.
Input sent while a glide runs **queues behind it** (`"queued": true`) and is
applied in order, one item per frame, so a click after a glide lands where it
ended. `release_all` drops the queue. A `click` with a pre-move beyond ±127
(MCP) glides.

There is no "pointer to (x, y)": the program keeps its own pointer. Where it
**clamps** the pointer at the screen edge and moves one pixel per count, *home*
first: glide far up and left (`dx -1000, dy 1000`; `dy` + = up), then glide by
the target's picture coordinates (`dx x, dy -y`).

Worked example (Sprinter, default BIOS 3.07 BETA 1, the MAME pack's
`sp_hdd_sys.chd` on `ide0.master` and `sp_hdd_media.chd` on `ide0.slave`, DSS 1.71
starts Flex Navigator 1.15; checked live on a running machine). FN's picture is
640 x 256; the drive bar of the left panel holds `A B C D`, "D" at (98, 35):

```bash
curl -s $B/$ID/mouse/status | jq -c '.device | {id, in_use, s: .serial | {receiver_baud, receiver_in_tune}}'
# {"id":"sprinter","in_use":true,"s":{"receiver_baud":1215.28,"receiver_in_tune":true}}  (false during the BIOS)
curl -s -X POST $B/$ID/mouse/glide -H "$J" -d '{"dx":-1000,"dy":1000}'   # home: queued true
curl -s -X POST $B/$ID/mouse/glide -H "$J" -d '{"dx":98,"dy":-35}'
curl -s -X POST $B/$ID/mouse/click -H "$J" -d '{"button":"left","frames":5}'
# ~1 s later: the left panel reads D:\*.* (MEDIA); status serial.packets_sent 11, framing_errors 0
```

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
from whichever mouse the machine has), `device` and `buttons`, never on the message
text. The top-level `x` / `y` / `button_mask` are the Kempston interface's own
counters; the machine's mouse is `device` (its `x`, `y`, `ports`, and `serial`
on the Sprinter: packet in flight, packets sent, bytes received, the SIO B
receiver's baud and FIFO; `ps2` on ZX-Evo / TS-Conf). Status codes: 400 for a bad
name, range or `?device=`, 404 for an unknown id, 409 while a TTD replay drives
the machine or when the machine has no mouse (`"reason":"no_mouse"`).

```bash
curl -s -X POST $B/$ID/mouse/glide    -H "$J" -d '{"dx":-1000,"dy":1000}'   # home (top-left)
curl -s -X POST $B/$ID/mouse/glide    -H "$J" -d '{"dx":300,"dy":-40}'      # to (300, 40)
curl -s -X POST $B/$ID/mouse/click    -H "$J" -d '{"button":"left","frames":5}'   # queued behind
curl -s "$B/$ID/mouse/status?device=sprinter" | jq '{device, queue}'
```

## CLI, Lua, Python

```text
mouse move 5 3
mouse glide -1000 1000    mouse glide 300 -40
mouse devices             mouse status sprinter
mouse press left          mouse release left
mouse click right 2       mouse buttons left,middle
mouse wheel 1             mouse clear
mouse status
```

```lua
mouse_move(5, 3); mouse_click("left", 2); mouse_wheel(1)
mouse_glide(-1000, 1000); mouse_glide(300, -40); mouse_click("left", 5)
print(mouse_status().device.id, mouse_busy())
print(mouse_status().ports.FBDF)
mouse_release_all()
```

```python
emu.mouse_move(5, 3); emu.mouse_click("left", 2); emu.mouse_wheel(1)
emu.mouse_glide(-1000, 1000); emu.mouse_glide(300, -40); emu.mouse_click("left", 5)
print(emu.mouse_status()["device"]["id"], emu.mouse_busy())
print(emu.mouse_status()["ports"])
emu.mouse_release_all()
```

## The host mouse (unreal-qt)

- A click on the screen captures the mouse when a program is reading it (the 128K
  ROM and TR-DOS do not; on the Sprinter BIOS SETUP does not, DSS 1.71 polling SIO B
  or DSS 1.62.9x reading `#FADF` does: a click then captures nothing); that click is
  not passed to the machine. Captured, the mouse is let go after 3 s without any program
  reading it (the next click captures again once one does). `Ctrl+Esc` (the Control key, on macOS too;
  `[INPUT] MouseReleaseKey=`), focus loss or switching to another application
  releases it. A plain `Esc` reaches the machine. While captured, the status bar
  says which key releases the mouse; the hint goes with the capture.
- The mouse button on the toolbar shows the state: normal (click the screen to
  capture), highlighted (captured), crossed out (host mouse off), grayed out (no
  mouse device, or no program reading it). Clicking it turns the host mouse off or on; View → Host Mouse
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
  machine; a glide's steps are journaled like any input, so the replay repeats them.
- **Several `move`s in a row on a running machine** can wrap the 8-bit counters
  (the program reads them once a frame): use `glide`.
- **The Sprinter's serial mouse** needs SIO B clocked at ~1 200 baud: before DSS
  1.71's driver programs CTC 0, status shows `receiver_in_tune: false` and the
  characters are lost (`framing_errors` grows).
