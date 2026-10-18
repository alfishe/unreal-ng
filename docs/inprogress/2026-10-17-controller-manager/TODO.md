# Controller manager: TODO

| | |
|---|---|
| **Date** | 2026-10-17 |
| **Status** | Design; no code |

## Phases

Each phase lands on its own, with tests, and leaves the emulator working.

| Phase | Content | Depends on | Status |
|:--|:--|:--|:--|
| C0 | Design set (this folder); owner review | | in review |
| C1a | Core: `ControllerManager`, connectors per machine, `IHostInputSource`, frame-boundary sample hook, `ConnectorState` journal kind, matrix overlay; Sinclair 1 / 2, Cursor, keys joystick; TTD type `Controllers` (66) | | done (11 tests in `ControllerManager_Test`, not committed) |
| C1b | `ControllerControl` (list, get, plug / auto, unplug, options, targets; the envelope and `ControllerError`); `[CONTROLLERS]` INI and legacy `[INPUT] Joystick=`; plugs journaled while the engine records (deferred to the boundary, fingerprint cut, Controllers blob v2 on every machine) | C1a | done (17 tests, not committed) |
| C1c | Fuller box as a Sinclair-edge card (`#7F` stick and its AY at `#3F` / `#5F`) through the slot manager, its stick a card-brought connector; Kempston options `extended`, `im2-leak` (no `decode`: the board's, owner 2026-10-17); `NC_CONTROLLER_CONNECTOR_CHANGED` | C1b | done (not committed) |
| C2a | Library skeleton `core/controllers` like `core/recording` (`unrealng::controllers`, `ENABLE_CONTROLLERS`); canonical vocabulary (`hostcontrols.h`); `IBackend` and the fake backend; `HostInputService` (backends, probe, a source per instance); `InputRouter`: retro targets, OR-lists, sticks and `dpad.*` as directions, derived digital with hysteresis, 8 / 4 ways, turbo / burst / toggle / autohold / tap in emulated frames, roles, instance assignment, `auto` connector, scopes device > family > global | C1 | done (16 tests, not committed) |
| C2b | Router layers: the layer stack, layer keys (hold / toggle / oneshot), chords with the 60 ms window, held targets released on a switch; ZX key, chord, macro, mouse and action targets; `consume` / `pass` for keyboard bindings | C2a | open |
| C2c | `ProfileStore`: JSON schema 1 (play / debug), built-in ⊂ community ⊂ user, field-by-field layering global → machine → family → device → game, validation with candidates, the out-of-the-box defaults per family; the seqlock state table if the C3 budget needs it | C2b | open |
| C3 | macOS backends: IOHID primary with the Xbox GIP and generic HID drivers, GameController companion, synthetic-device filter, endpoint merge | C2 | open |
| C4 | Qt dialog, Simple view: three panes, patch cords, picture with callouts, press-to-bind, test panel; status-bar LED; first-connect toast | C2, C3 | open |
| C5 | Device database and first image set (Xbox, Elite 2, DualSense, Switch Pro, generic, DB9 stick; retro: Kempston stick, Sinclair SJS-1, MD6 pad, Kempston mouse, ZX keyboard) | C4 | open |
| C6 | Port-read heuristic and the auto toast; signature match hook (waits for the signature library) | C1, C4 | open |
| C7 | Debug layer and debug profiles; action registry shared with keyboard shortcuts; Advanced view | C4 | open |
| C8 | Windows backends (GameInput, Raw Input, XInput fallback) | C2 | open |
| C9 | Linux backends (evdev, hidraw, udev) | C2 | open |
| C10 | Surfaces: WebAPI, MCP, CLI, Lua, Python; extended `joystick_input` | C1, C2 | open |
| C11 | More retro devices: Timex, Next user-defined keys and the right connector, Evo Sega pad, AMX mouse, light guns | C1 | open |
| H1-H5 | Haptics: actuators and gate; audio taps and filters; probes and haptic profiles; HD haptics; first game profiles ([haptics.md](haptics.md#13-phases-and-open-points)) | C3, C8, C9 | open |
| C12 | SDL3 backend behind `ENABLE_SDL_CONTROLLERS` (D6) | C2 | later |

## Owner questions

Answered 2026-10-17 (D13-D18 in [README.md](README.md#decisions-2026-10-17)); Q-1 and Q-8 stay open.

| # | Question | Answer |
|:--|:--|:--|
| Q-1 | Vectorize the retouched photos (one SVG style) or keep raster at 2x? | **Open**: separate study after C5 (D5) |
| Q-2 | One new journal kind `ConnectorState` for the new connector devices, or a kind per device? | One kind; Kempston, NextPad and mice keep theirs (D13) |
| Q-3 | Kempston mouse button order | Per-board property following each board's hardware (D14) |
| Q-4 | macOS: matching a `GCController` to its IOHID device | Arrival + vendor name, confirmed by button-edge correlation; input from IOHID, GameController adds outputs after the match (D18) |
| Q-5 | Windows GameInput | A prerequisite of the Windows installer, like the MSVC runtime; loaded dynamically so a portable build without it falls back to Raw Input (D17) |
| Q-6 | Signature library | Shared `swsignature` for the controller manager, the metadata manager and the Steam Deck client, a separate project before C6 (D16) |
| Q-7 | Elite paddles hidden by an onboard profile | Read the profile byte and the raw paddle bits where the OS gives them; otherwise a hint "switch to profile 0" (D15). Measured on macOS; the mirrored face button is removed (D19) |
| Q-8 | Retro Games Ltd THEJOYSTICK / THESPECTRUM pad VID/PIDs | **Open**: read from ioreg when a device is available |
| Q-9 | Manufacturer press-kit photos | Allowed with the source and terms recorded per image; platform logos removed in the retouch (D5) |

## Not checked yet

- Elite 2 paddles on macOS: all measured ([research-host-apis.md §2.3a-c](research-host-apis.md#23c-gamecontroller-over-bluetooth-and-usb)):
  raw IOHID over Bluetooth has paddles and profile on every profile; the USB dext has neither; GameController never
  reports paddles on either transport.
- Elite 2 battery on macOS: GameController says unknown on both transports, `system_profiler` says 100 % over
  Bluetooth. Check the 0x99 nibble in `[18]` (0x05 at 100 %) once the pad has discharged (not soon: it was full on
  2026-10-17). The IORegistry has no `BatteryPercent` for the Elite (only for Apple devices), so the 100 % of
  `system_profiler` comes from the Bluetooth stack (BLE Battery Service through IOBluetooth / CoreBluetooth): a
  candidate source for the macOS backend.
- Windows paddles through GameInput with an onboard profile; Linux xpad with profile 0.
- Stream Deck + XL `0FD9:00C6` is not in `usb.ids`; it is used through the companion client, not as a HID device.

## Errata found in other documents (fix separately)

Details and sources in [research-retro-hardware.md](research-retro-hardware.md).

- Kempston mouse "D0 = left on every board": `2026-09-12-kempston-mouse/hardware-reference.md` §4,
  `2026-10-03-zx-bus-slots/research-cards.md:781`.
- BC#4 machine list misread for the Kempston mouse: `2026-09-12-kempston-mouse/hardware-reference.md` §2.1-2.2,
  `2026-10-07-kay/design.md:53`.
- Timex AY ports: `docs/ports/ports.md:252-258` (#F5 select, #F6 data; #F4 is not an AY port).
- MCP `joystick_input` description lists ATM3 / Scorpion / TS-Conf only.
- ATM Turbo 2+ has no Kempston joystick or mouse on the board (manual feature table); check what our ATM2 decoder
  answers.
