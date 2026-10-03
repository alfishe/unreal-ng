# Recipe: keyboard input

Type text, tap, hold and release keys, press chords and run named macros from an agent
or a script, choose where the keys go (the ZX matrix, a PS/2 keyboard controller, or
both), and know what the guest sees. Covers the injection surface (`keyboard/*` on
the WebAPI, `type_input` on MCP). Not covered here: driving TR-DOS to run a program
([run/manual-trdos-run.md](../run/manual-trdos-run.md)) and the mouse
([input/mouse.md](mouse.md)).

The key names are the ones of the ZX Spectrum keyboard (`a`..`z`, `0`..`9`, `caps`,
`symbol`, `enter`, `space`) plus emulated extended keys (`up`, `down`, `left`, `right`,
`delete`, `break`, `edit`, `dot`, `comma`, ...). Names are case-insensitive. The
MCP resource `unreal://keyboard-layout` (defined in
[mcp-resources.cpp](../../core/automation/mcp/src/mcp-resources.cpp)) lists them with their aliases
(`shift` = `caps`, `sym` = `symbol`, `return` = `enter`, `backspace` = `delete`). The live list for
the machine in hand is `list_keys` / `GET .../keyboard/keys`. Machines with a PS/2
keyboard controller also accept PC key names with no ZX meaning (`f1`, `home`, ...; an
optional `pc.` prefix): only the controller hears them (see
[PS/2 machines](#ps2-machines-zx-evo-atm-turbo-2-profi)).

Worked example: `tap` `left` presses the cursor-left key; on the ZX matrix that is Caps Shift + `5`,
which the emulator presses for you. To enter E-mode (Caps Shift and Symbol Shift together),
send `combo` with `["caps","symbol"]`.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use
> [WebAPI](#webapi) only inside host-side pipelines or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## Timing (read this once)

Injection is **frame-driven**. `tap`, `combo`, `type` and `macro` queue a sequence;
the machine consumes it once per frame (`DebugKeyboardManager::OnFrame`). The WebAPI
call returns as soon as the sequence is queued, not when the guest has read the keys.

- `frames` (`tap`, `combo`) is the hold time, default **2**. `delay_frames` (`type`) is the
  pause between characters, default **2**; each character is also held 2 frames.
  A 10-character `type` therefore takes about 40 frames. Slow guests (a BASIC
  editor that scans the keyboard once per frame, TR-DOS) need the defaults or more;
  tight loops that poll `#FE` need less.
- A **paused** machine runs no frames, so a queued sequence does not advance until
  frames run (`run_frames`, resume). The reproducible pattern: queue the input,
  `run_frames N` with N at least the sequence length, then read the result. (Inferred from the
  frame-driven sequencer; not exercised live for this recipe.)
- `press` / `release` / `release_all` are applied directly, not queued; a held key stays
  held until released, whatever frames pass. A guest that reads `#FE` sees it at once.
- One sequence at a time per manager: new `tap` / `type` / `macro` calls append to the queue.
  `abort` empties it. `status` tells whether one is running.

## MCP (preferred)

```text
type_input {"action":"type","text":"PRINT 1"}
type_input {"action":"type","text":"10 PRINT 1","tokenized":true}
type_input {"action":"tap","key":"enter","frames":3}
type_input {"action":"press","key":"caps"}
type_input {"action":"release","key":"caps"}
type_input {"action":"combo","keys":["caps","symbol"],"frames":2}
type_input {"action":"macro","name":"cat"}
type_input {"action":"release_all"}
type_input {"action":"list_keys"}
type_input {"action":"status"}
type_input {"action":"route","route":"ps2"}
```

- `type` sends the text one character at a time with the shift handling the character
  needs (`delay_frames`, default 2). With `tokenized: true` it types a BASIC line into the
  ROM editor instead (48K: keywords as keys; 128K: letters), proving each key was taken;
  ENTER is **not** pressed and `delay_frames` does not apply (the ROM paces it). The reply
  names the outcome or the failure. The MCP tool forwards `text`, `delay_frames` and
  `tokenized` only: the WebAPI's `trace` option is not reachable through `type_input`
  (use `invoke_api` for that).
- `tap` needs `key`, `combo` needs a non-empty `keys` array, `macro` needs `name`, `route`
  needs `route`; a missing field is an `isError` result (`tap requires 'key'`, ...).
- Macros (the library in
  [debugkeyboardmanager.cpp](../../core/src/debugger/keyboard/debugkeyboardmanager.cpp)):
  `e_mode` (Caps Shift + Symbol Shift), `g_mode` (Caps Shift + 9), `format`, `cat`,
  `erase`, `move` (E-mode, then 0 / 9 / 7 / 6) and `break` (Caps Shift + Space, 3 frames).
- Run a BASIC command that needs ENTER and a result with `basic/run` through `invoke_api`
  (see [_common/transports.md](../_common/transports.md)), not with `type`.

## WebAPI

```bash
B=http://localhost:8090/api/v1/emulator; ID=<id>; J='Content-Type: application/json'
curl -s -X POST $B/$ID/keyboard/type        -H "$J" -d '{"text":"PRINT 1","delay_frames":2}'
curl -s -X POST $B/$ID/keyboard/type        -H "$J" -d '{"text":"10 PRINT 1","tokenized":true,"trace":false}'
curl -s -X POST $B/$ID/keyboard/tap         -H "$J" -d '{"key":"enter","frames":3}'
curl -s -X POST $B/$ID/keyboard/press       -H "$J" -d '{"key":"caps"}'
curl -s -X POST $B/$ID/keyboard/release     -H "$J" -d '{"key":"caps"}'
curl -s -X POST $B/$ID/keyboard/combo       -H "$J" -d '{"keys":["caps","symbol"],"frames":2}'
curl -s -X POST $B/$ID/keyboard/macro       -H "$J" -d '{"name":"cat"}'
curl -s -X POST $B/$ID/keyboard/release_all
curl -s -X POST $B/$ID/keyboard/abort
curl -s $B/$ID/keyboard/keys   | jq '{count, keys}'
curl -s $B/$ID/keyboard/status | jq '{sequence_running, available, host_route, host_route_effective, ps2_controller, keyboard_controller}'
curl -s -X POST $B/$ID/keyboard/route -H "$J" -d '{"route":"ps2"}' | jq
```

Responses worth asserting:

| Call | Success fields |
|:--|:--|
| `tap` | `success`, `key`, `frames` |
| `press`, `release` | `success`, `key` |
| `combo` | `success`, `keys`, `frames` |
| `macro` | `success`, `macro` (400 and `success:false` for an unknown name) |
| `type` | `success`, `text`, `length`, `delay_frames`, `tokenized` |
| `type` with `tokenized` | the CommandTyper result (outcome and failure text), `text`, `tokenized:true`; the status code follows the result (mapping in `commandtyperjson.h`; individual codes unconfirmed) |
| `status` | `sequence_running`, `available`; with a keyboard device `host_route`, `host_route_effective`, `ps2_controller`, `keyboard_controller` |
| `route` | `host_route`, `host_route_effective`, `ps2_controller`, `keyboard_controller` |

Assert on those fields, never on `message`. `sequence_running` going `false` is the
way to know a queued `tap` / `type` / `macro` has been fully consumed.

Status codes: 400 for a missing field (`key`, `keys`, `name`, `text`) or an unknown key
name (the reply points at `/keyboard/keys`), 404 for an unknown id, 500 when the keyboard
manager is not available. `release_all` and `abort` take no body.

## The route gate: host keyboard, injected keys, and what the guest sees

`keyboard/route` (`type_input` action `route`) decides where **both** the host's physical
keyboard (unreal-qt) and the injected keys go. Values: `auto`, `matrix`, `ps2`, `both`
(also `[INPUT] HostKeyboard=`, default `auto`; Qt Machine > Host Keyboard; CLI `key route`).

| Effective route | Goes to |
|:--|:--|
| `matrix` | the 40-key ZX matrix only |
| `ps2` | the machine's PS/2 keyboard controller only |
| `both` | the matrix and the controller |
| `auto` on a machine without a controller | `matrix` |
| `auto`, controller that takes the matrix's place (Profi PROFI-XT) | `ps2` |
| `auto`, any other controller (ZX-Evo AVR, ATM Turbo 2+) | `both` |

`status` / `route` report both the requested value (`host_route`) and the one in force
(`host_route_effective`): assert on `host_route_effective`. The matrix side drops a press
when the route excludes it (a release always goes through); the controller side hears the
PC keys behind each ZX key, and `type` sends it the PC keys that type each character (an
`&` is Shift + 7 for a PC, not Symbol Shift + 6).

## PS/2 machines (ZX-Evo, ATM Turbo 2+, Profi)

- ZX-Evo (`ATM3`): the AVR's PS/2 keyboard. Injected keys reach it as PC key events, journaled
  by TTD like any input.
- ATM Turbo 2+ (`ATM710`): the keyboard controller is an MCS-51 running the real firmware
  (`[ATM] Kbc=`, default `V41`); CP/M modes read only the controller, mode 0 ANDs the
  native port with it. Automation presses the PC keys of a chord one frame apart (the
  firmware keeps one received scan code at a time). `keyboard_controller` in `status` names
  it. Design: [2026-10-01-atm2-keyboard-controller](../../docs/inprogress/2026-10-01-atm2-keyboard-controller/README.md).
- Profi (`PROFI`, v5): the PROFI-XT controller, `profi_keyboard` at create (`xt` default,
  `xttable`, `matrix`); `PROFI3` defaults to `matrix`. With a controller the route `auto`
  is `ps2`. Details, the PC key mapping and the `EXT` bit: [machines/profi.md](../machines/profi.md).
- Name a PC key with no ZX meaning (`f1`, `home`, ...) and only the controller hears it; on a
  machine without one the key is accepted but nothing consumes it (unconfirmed what the
  guest sees; `list_keys` shows the names).

## Pitfalls

- **Frames do the typing.** A paused machine does not consume queued sequences, and a
  `type` of a long line outlasts a short `run_frames`. Poll `status.sequence_running`
  or run enough frames.
- **Mixing `press` with `tap` of the same key.** `release_all` is the reset; a stuck Caps Shift
  turns every later `type` into capitals / cursor keys.
- **TTD replay and RZX playback own the input.** Live `press` / `release` are ignored while
  recorded input drives the machine. `route` is refused with **409** while a TTD recording
  runs (the route decides what enters the journal): set it before `time_travel start`.
- **Wrong route.** On `ATM3` / `ATM710` the default `both` feeds the matrix and the
  controller; a program that reads only one sees one copy, a program that reads both may see
  the key twice. Set `matrix` or `ps2` to isolate.
- **Unknown key** is a 400 on the WebAPI (and an `isError` through MCP): the emulator never
  silently drops an unknown name.
- **TR-DOS and BASIC need real timing.** TR-DOS reads the keyboard slowly; keep the default
  2-frame hold, run 100+ frames between menu steps (see the TR-DOS recipe).
- **`tokenized` does not press ENTER.** Follow it with `tap` `enter` (or use `basic/run`).
