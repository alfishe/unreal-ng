# Controller manager: automation surfaces

| | |
|---|---|
| **Date** | 2026-10-17 |
| **Status** | Design, for review |
| **Pattern** | the media manager's one core control layer ([media-control-design.md §3.8](../2026-09-28-storage-manager/media-control-design.md)) |

## Contents

- [1. One control layer](#1-one-control-layer)
- [2. Verbs](#2-verbs)
- [3. Selectors](#3-selectors)
- [4. Reply envelope and errors](#4-reply-envelope-and-errors)
- [5. Parity table](#5-parity-table)
- [6. WebAPI](#6-webapi)
- [7. MCP](#7-mcp)
- [8. CLI, Lua, Python](#8-cli-lua-python)
- [9. Events](#9-events)
- [10. Existing joystick verbs](#10-existing-joystick-verbs)

## 1. One control layer

Two control objects carry every verb, so the surfaces cannot drift:

| Object | Lives in | Scope | Verbs on |
|:--|:--|:--|:--|
| `ControllerControl` | core, `core/src/emulator/io/controllers/controllercontrol.{h,cpp}` | one emulator instance | connectors, retro devices, plugs, the auto decision, retro input |
| `HostInputControl` | library, `core/controllers/src/hostinputcontrol.{h,cpp}` | the process | host devices, roles, instance pins, bindings, profiles, the database, probes |

Both take a request `{verb, selector, options}` and return a `StateNode` reply converted to JSON, a sol2 table and a
Python dict, exactly as `MediaControl` does. Option parsing, defaults and error codes live there, not in a surface.
MCP calls the WebAPI. With `ENABLE_CONTROLLERS=OFF`, `HostInputControl` answers `unavailable` with the reason and the
connector verbs keep working (keyboard joystick, automation input).

## 2. Verbs

### Connectors (`ControllerControl`)

| Verb | Does |
|:--|:--|
| `list` | Connectors of the machine: id, alias, kind, source (built-in or which card), plugged retro device, accepted retro devices, bound host devices. |
| `get` | One connector, with the retro device's controls and current state. |
| `plug` | Plug a retro device into a connector: `{connector, device, options}`; `auto` picks the connector. |
| `unplug` | Leave the connector empty. |
| `targets` | Where a retro device could go, ordered, with a `default` index and a `refusal` reason per connector (the media `targets` shape). |
| `auto` | Explain the current automatic decision: signature match, port-read evidence, fallback; `apply` re-runs it. |
| `press` / `release` / `set` / `tap` / `status` | Retro input on any connector (the existing joystick verbs generalized, [§10](#10-existing-joystick-verbs)). |

### Host side (`HostInputControl`)

| Verb | Does |
|:--|:--|
| `devices` | Host devices: id, name, VID:PID, version, transport, family, role, instance pin, battery, capabilities, live state on request. Built: id, name, family, layout, backend, controls; with the Qt client role, layers, context, outputs, feel, `battery`, `light` and `notice` (below). |
| `device` | One device; `identify` blinks the LED and rumbles. |
| `role` | Set `player`, `debugger-remote`, `disabled`. |
| `pin` | Set `follow-focus`, `pinned:<instance>`, `all`. |
| `bindings` | Read the effective bindings of a device per layer, with the profile each one came from. |
| `bind` / `unbind` | Change one binding in the user layer of a profile. |
| `scheme` | The scheme a user profile extends (how the pad works: Kempston, Sinclair 1, QAOP + Space), or none. |
| `profiles` | List profiles (play and debug sets, scopes, match rules); `apply`, `save`, `delete`, `export`, `import`. |
| `probe` | The backend report: loaded, why not, endpoints found, permissions. |
| `database` | Look up a VID/PID or SDL GUID; import a `gamecontrollerdb` line or a RetroArch autoconfig file. Built: `database` (GET) answers the families count, the user file's path and the ids it holds, and with `vidpid` (+ `bus`, `name`) the match a device of those ids gets, or with `family` that family (`family`, `name`, `layout`, `images`, `glyphs`, `controls`, `player`, `role`, `ignore`, `step`, `score`, `user`: the entry comes from devices.user.json); `database-save` (POST `text`: one family as JSON) adds or replaces an entry of devices.user.json, field by field over a built-in family of the same id, validated against the compiled-in database and written atomically; `database-delete` (POST `id`) removes one. The active database is swapped at once (holders of the old one keep their snapshot); a name or match change applies when the device connects again. The hotspot editor saves through `database-save`. Not built: SDL GUIDs, the gamecontrollerdb / RetroArch import. |
| `learn` | Start, step and finish the learn wizard for an unknown device (the UI flow, scriptable). |
| `haptics` | Built: per device whether the emulator drives its motors (`available`, else the `reason`), its `actuators` and the test `effects` (`id`, `label`, `ms`). Planned: mode (`off`, `game`, `always`) and strength per device, the gate state, `stop` all ([haptics.md §11](haptics.md#11-surfaces)). |
| `rumble` | Built: play a test effect on a device (`device`, `effect`: `identify` by default, `low`, `high`, `clicks`, `left-trigger`, `right-trigger`, `swell`; a Surface Dial: `identify`, `click`, `clicks`, `buzz`), the dialog's Test haptics ([haptics.md §10](haptics.md#10-ui)). |
| `light` | Built: set a pad's light colour (`device`, `color`: `#RRGGBB`, `r,g,b` or `off`), a DualSense / DualShock 4 light bar, where the OS lets the emulator set it; `unavailable` with the reason elsewhere. |

### Battery, light and notice (built)

`devices` carries three things the backend says about each device (`IBackend::Battery`, `HasLight` /
`LightUnavailable` / `SetLight`, `DeviceNotice`; all defaulted, so a backend without them answers "unknown" and "no
light"):

- `battery`: `{known, text}`; when known also `level` (percent, absent when the OS gives only a state), `coarse` (a
  step, not a measurement: XInput's four, the kernel's `capacity_level`), `charging`, `wired`. `text` is what every
  surface and the dialog show: `unknown`, `80 %`, `about 67 %`, `charging, 40 %`, `wired`. Never a device round trip:
  the backends keep the last value and refresh it on their own threads (every 30 s; XInput every 10 s).
- `light`: `{available}`, else `{available: false, reason}`.
- `notice`: what the device cannot do here and how to fix it (Linux: no hidraw access and the udev rule; the Elite 2
  over USB: paddles on profile 0 only); absent when nothing is missing.

Where they come from:

| OS | Battery | Light |
|:--|:--|:--|
| macOS | GameController's `GCDeviceBattery` of the matched `GCController` (D18). An Xbox Elite 2 reports state unknown and level 0 on both transports, so it stays `unknown` (`system_profiler` knows it from the Bluetooth stack, [research-host-apis.md §2.3c](research-host-apis.md)) | a DualSense over USB through its output report (`DualSenseOutput::UsbLightReport`); otherwise `GCDeviceLight` of the matched `GCController` (a DualSense over Bluetooth, a DualShock 4), after a press confirms the match |
| Windows | XInput: `XInputGetBatteryInformation` (wired, or four coarse levels). GameInput (v3) has no battery API, Raw Input reads none: `unknown` | a DualSense over USB (Raw Input) through the same output report; nothing else |
| Linux | the HID device's power supply class (`<hid>/power_supply/<name>`: hid-playstation, hid-sony, hid-nintendo, xpadneo, hid-input): `status`, `capacity`, `capacity_level`. xpad (an Xbox pad over USB) registers none | the HID device's LED class: one multicolor LED (`multi_index` / `multi_intensity`, hid-playstation) or `:red` / `:green` / `:blue` LEDs (hid-sony). Writing needs the LED rules of `data/controllers/70-unreal-ng.rules` (sysfs takes no ACL, so they open the mode) |

| Plane | Read (in `devices`) | Set the light |
|:--|:--|:--|
| CLI | `controllers devices` (a `battery ...; light settable` and a `notice:` line per device; `--json` the reply) | `controllers light <device> <#RRGGBB\|r,g,b\|off>` |
| REST | `GET /api/v1/controllers/devices` | `POST /api/v1/controllers/light {device, color}` |
| WebSocket | `{"op":"call","method":"GET","path":"/api/v1/controllers/devices"}` | `{"op":"call","method":"POST","path":"/api/v1/controllers/light","body":{"device":"...","color":"#ff0000"}}` |
| MCP | `controllers` `devices` (the text names battery, light and notice) | `controllers` `light` (`device`, `color`) |
| Python | `controllers_host("devices")` | `controllers_light(device, color)` |
| Lua | `controllers_host("devices")` | `controllers_light(device, color)` |

No topic pushes the host devices yet (`devices` is a call; a `controllers.devices` topic for arrivals, battery and
notices is left, TODO.md).

## 3. Selectors

As media slots: a stable id (`kempston`, `sinclair-1`, `next.left`, `card.zxbus1.kempston`), an alias (`joystick`,
`left`, `player2`), `kind:index` (`joystick:1`), and `auto`. Host devices: the `HostDeviceId`, `vidpid:045e:0b00[#n]`,
`family:xbox-elite2`, `player:n`, `keyboard`, `mouse`. An unknown or ambiguous selector fails with the list of
candidates (`unknown-connector`, `ambiguous-connector`, `unknown-device`).

## 4. Reply envelope and errors

The media envelope: `ok`, `error`, `message`, `connector` or `device`, `pending`, `revision`, `report`. Changes to
connectors apply at the next frame boundary and wait up to 5 s unless `async`. The error table:

| Code | HTTP | When |
|:--|:--|:--|
| `unknown-connector`, `unknown-device`, `unknown-profile` | 404 | selector matched nothing |
| `ambiguous-connector`, `ambiguous-device` | 409 | several matched; candidates listed |
| `incompatible` | 409 | the retro device does not fit the connector (a Kempston mouse in a Sinclair socket) |
| `owned-by-replay` | 409 | TTD replay or RZX playback owns input |
| `fixed` | 409 | the connector's device is part of the board (`kempston`, `next.left`, the +3 `sinclair-1`) |
| `recording` | 409 | TTD v1 records: it keeps no configuration changes (the engine journals the plug instead) |
| `bad-request` | 400 | an unknown verb, option or device kind, a malformed option value |
| `held-by-session` | 423 | an automation session holds the machine (companion and GUI rules apply) |
| `unavailable` | 503 | library off or backend missing; the reason is in `message` |
| `permission` | 403 | an OS permission is missing; the fix is in `message` |
| `invalid-binding` | 400 | unknown control or target, bad option |

## 5. Parity table

| Capability | WebAPI | MCP | CLI | Lua | Python | Qt |
|:--|:--|:--|:--|:--|:--|:--|
| connectors list / get | yes | `controllers` | `controllers list` | yes | yes | right pane |
| plug / unplug / targets | yes | yes | yes | yes | yes | drag cords |
| auto explain / apply | yes | yes | yes | yes | yes | profile bar, toast |
| retro input press / release / set / tap / status | yes | `joystick_input` (extended) | `joystick` (extended) | yes | yes | test panel |
| host devices, roles, pins, identify | yes | `controllers` | yes | yes | yes | left pane |
| bindings / bind / unbind | yes | yes | yes | yes | yes | picture callouts |
| profiles | yes | yes | yes | yes | yes | profile bar |
| probe report | yes | yes | yes | yes | yes | Advanced |
| device database lookup / user entries | `database`, `database-save`, `database-delete` | `controllers` | `controllers database [vid:pid\|family]`, `database-save <file>`, `database-delete <id>` | no (P3) | `controllers_database`, `controllers_save_device`, `controllers_delete_device` | hotspot editor (Picture…) |
| learn wizard | yes | no (needs a human) | yes | yes | yes | wizard |
| haptics mode / strength / play / stop | yes | `controllers` (`haptics_state`, `haptics_play`, `haptics_stop`) | yes | yes | yes | device card, Haptics tab |
| debug actions | `POST /controllers/actions/{id}` | `control_execution`, `time_travel` (existing) | existing | existing | existing | debug layer |

## 6. WebAPI

Under the existing prefix, next to `/joystick` and `/media` (`core/automation/webapi/src/emulator_api.h:577-581, 89-91`):

```
GET    /api/v1/emulator/{id}/controllers                     connectors
GET    /api/v1/emulator/{id}/controllers/{connector}
POST   /api/v1/emulator/{id}/controllers/{connector}/{verb}  plug | unplug | press | release | set | tap
GET    /api/v1/emulator/{id}/controllers/targets?device=...
GET    /api/v1/emulator/{id}/controllers/auto                explain; POST .../auto/apply

GET    /api/v1/controllers/{verb}            devices | context | profiles?set=game|play|debug|scheme&machine=..
                                             | profile?id=.. | bindings?device=..&layer=play|debug
                                             | haptics?device=..
POST   /api/v1/controllers/{verb}            save {text} | delete {id} | apply {id|none}
                                             | bind {control, spec, layer?, family?|device?|profile?}
                                             | unbind {control, layer?, family?|device?|profile?}
                                             | scheme {id|none, family?|device?|profile?}
                                             | rumble {device, effect?}
                                             | output {device, pattern | steps, clicks | marks}
                                             | light {device, color}
                                             | database-save {text} | database-delete {id}
       (built: HostInputControl, core/controllers/src/hostinputcontrol.h; the wrong method answers 405)
POST   /api/v1/controllers/host/{device}/{verb}              role | pin | identify | learn (not built)
GET    /api/v1/controllers/database?vidpid=045e:0b00[&bus=usb&name=..] | family=..   (built; guid=.. not built)
POST   /api/v1/controllers/database/import                   gamecontrollerdb line or RetroArch .cfg
GET    /api/v1/controllers/probe

GET    /api/v1/controllers/actions                       the action registry: ids, enabled state, reason
POST   /api/v1/controllers/actions/{id}                  run an action (debug-and-ttd.md §3)
PUT    /api/v1/controllers/haptics                       set mode / strength (not built; GET haptics is above)
POST   /api/v1/controllers/haptics/stop                  stop every actuator (not built)
```

Actions belong to the companion scopes that already exist (`run`, `rewind`, `debug`, `media`; companion-client
D3), so a paired Stream Deck calls them without opening a session.

Each route gets an `openapi_controllers*.inc` entry, as media and joystick do.

The host verbs are one table, `HostInputControl` (process scope: the Qt client's `HostControllers` registers as its
owner and runs each request on the GUI thread; no owner answers `unavailable` 503). WebAPI `/api/v1/controllers/{verb}`,
MCP `controllers` (the same action names, no target), CLI `controllers <verb>` (positional id / control / file;
`bind south key=ENTER` builds the spec) and Python `controllers_host(verb, **options)` with the named wrappers
(`controllers_profiles`, `controllers_profile`, `controllers_save_profile`, `controllers_delete_profile`,
`controllers_apply_profile`, `controllers_bindings`, `controllers_bind`, `controllers_unbind`, `controllers_haptics`,
`controllers_rumble`, `controllers_output`, `controllers_light`, `controllers_database`, `controllers_save_device`,
`controllers_delete_device`) and Lua `controllers_host(verb, opts)` (with
`controllers_haptics`, `controllers_rumble`, `controllers_output`, `controllers_light`) all
build a `HostRequest`; the CLI and Python reach `scheme` through `controllers scheme <id|none>` and
`controllers_host`. Lua reaches the connectors through `controllers(verb, connector, device, opts)`.
`bind` without `profile` writes `user/device-<id>` (device=), `user/<family>` (family=) or
`user/global`; a shipped profile named by `profile=` is copied into the user's of the same id, which overrides it, and
`delete` of that copy brings the shipped one back.

## 7. MCP

- One tool `controllers` with `action` (as `media`): `list`, `get`, `plug`, `unplug`, `targets`, `auto`, `devices`,
  `device`, `role`, `pin`, `bindings`, `bind`, `unbind`, `profiles`, `probe`, `database` (built with
  `database-save` and `database-delete`; the text names the match in one line: "xbox-elite-2: Xbox Elite Wireless
  Controller Series 2 (layout xbox, images my-elite, exact match, score 100, from devices.user.json)").
- `joystick_input` keeps its name and gains a `connector` argument (default `auto`); its description lists the models
  that really answer (it names ATM3 / Scorpion / TS-Conf only today, but Pentagon, Profi, Sprinter and Next answer too).
- The `content[]` text states the decision in one line ("Kempston on `kempston` (port-read evidence: #1F read in 50 of
  50 frames)"), because an agent may see only the text.
- Debug actions stay in `control_execution` and `time_travel`; the controller tool does not duplicate them.

## 8. CLI, Lua, Python

- CLI: `controllers list|get|plug|unplug|targets|auto|devices|role|pin|bindings|bind|unbind|profiles|probe|haptics`, next to
  `joystick` (`cli-processor.cpp:269`), which gains `--connector`.
- Lua: `controllers_*` functions next to `joystick_*` (`lua_emulator.h:681-783`).
- Python: the same names as methods (`python_emulator.h:690+`).

## 9. Events

| Notification | When |
|:--|:--|
| `NC_CONTROLLER_HOST_CHANGED` | a host device connected, disconnected, changed role or pin |
| `NC_CONTROLLER_CONNECTOR_CHANGED` | plug or unplug on a connector (revision increments); built in C1c: `ControllerConnectorPayload` (emulator id, connector id, device kind or `none`, revision), posted after the manager's lock is released; a rebuild of the connector list posts once with an empty connector id |
| `NC_CONTROLLER_AUTO_DECIDED` | the auto decision changed (the toast listens) |
| `NC_CONTROLLER_LAYER` | the debug layer was entered or left (the status bar listens) |
| `NC_CONTROLLER_FEEDBACK` | core output for rumble and LEDs, and the haptic source levels and gate state ([architecture.md §10](architecture.md#10-outputs-rumble-leds-triggers), [haptics.md](haptics.md)) |

Surfaces poll `revision` as the media panel does; the WebAPI streams the same events where it streams media events.

## 10. Existing joystick verbs

`DebugJoystickManager` stays the funnel for automation input. It gains a connector argument and resolves names
against the plugged retro device's controls (`fire2`, `a`, `start`, `mode`, `trigger`), so `tap fire` works on
Kempston, Sinclair, Fuller and MD pads alike. Masks keep their meaning (bits 0-11 left, 16-27 right on the Next) for
backward compatibility; new code uses names. The `/joystick` routes keep working and map to `connector=auto`.
