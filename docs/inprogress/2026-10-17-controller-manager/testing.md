# Controller manager: test plan

| | |
|---|---|
| **Date** | 2026-10-17 |
| **Status** | Design, for review |
| **Rules** | [core/tests/README.md](../../../core/tests/README.md): no `sleep_for`, under 50 ms per test, `TestWait::For`, scratch paths from `TestPathHelper` |

## 1. Layers of testing

| Layer | What | Runs |
|:--|:--|:--|
| Unit, library | vocabulary parsing, device identity and tie-break, database lookup and scoring, HID report-descriptor parser, vendor drivers on recorded reports, router (bindings, OR-lists, chords, layers, analog, autofire), profile merge and match | `core-tests`, every build |
| Unit, core | connectors per machine, plug / unplug / targets, each retro device's port projection and polarity, matrix overlay AND, `ConnectorState` journal apply, checkpoint save / restore, port-read watch | `core-tests` |
| Integration | a `FakeBackend` emits scripted endpoint reports; a machine boots a test program; the port value seen by the Z80 is asserted per frame; TTD record and replay give identical frames | `core-tests` |
| Surfaces | WebAPI, MCP, CLI, Lua, Python against the fake backend | `tools/verification/webapi`, `tools/verification/mcp/run_all.py` |
| Hardware-in-the-loop | real devices, opt-in, a human presses what the script asks | `tools/verification/controllers/hil.py`, not in CI |
| Qt | dialog model tests; screenshots of the dialog for review | `unreal-qt` tests, manual |
| Data | every image has a provenance record and a hotspot map whose control ids exist in the family layout ([device-database.md](device-database.md)) | `tools/verification/controllers/check_images.py`, every build of `data/controllers/` |

## 2. Recorded reports as fixtures

Every vendor driver is tested on recorded raw reports, not on live hardware:

- `testdata/controllers/hid-captures/<family>/<os>-<transport>/*.hex`: one report per line with a label
  (`p1-down`, `profile-1-active`, `lt-full-hairtrigger`).
- First fixtures (recorded): the Elite 2 over USB through the macOS `XboxGamepad.dext` vendor pipe (report 0x20,
  18 bytes, truncated) and over BLE (report 0x01, 20 bytes, profiles 0-3), in
  `testdata/controllers/hid-captures/xbox-elite2/`, with `probes/hiddump.c`. To record: DualSense USB and Bluetooth; a DragonRise
  DB9 adapter; an 8BitDo in two modes.
- A capture tool in the dialog's Advanced test panel writes the same format, so users can attach fixtures to bug
  reports.

## 3. Key unit tests

| Test | Asserts |
|:--|:--|
| `HostDeviceId_TieBreak` | two identical pads with and without serials get stable distinct ids; a replug keeps the id |
| `SyntheticDevice_Filtered` | an `AppleGCSyntheticDevice` endpoint next to the real Elite yields one host device |
| `EliteGip_Paddles` | USB GIP: P1-P4 bits map to `paddle.p1..p4`; on the truncated macOS dext report they are reported hidden |
| `EliteBle_PaddlesAndMirror` | BLE report `[19]` bits map to P1-P4 and `[17]` to the profile; on profiles 1-3 the mirrored face button in the same report is removed after three coincidences (fixture `macos-ble/session-2026-10-17-profiles-raw.txt`) |
| `Router_OrListAndChord` | `south OR key.space` fires; `view+menu` chord enters the debug layer and suppresses both singles |
| `Router_LayerReleasesHeld` | holding fire then entering the debug layer releases fire in the next sample |
| `Analog_Hysteresis` | the stick crossing 0.45 presses, falling to 0.40 holds, below 0.35 releases |
| `Autofire_EmulatedFrames` | on 2 / off 2 gives the pattern 1100 1100 in frame samples regardless of host timing |
| `Sinclair_OverlayAnd` | joystick left and real key 6 held; releasing the joystick leaves 6 pressed in `#EFFE` |
| `Kempston_Variants` | 5-bit vs 8-bit, decode variants per machine, `0x00` idle on Evo |
| `KempstonMouse_ButtonOrder` | D0 = right on Next and Karabas, D0 = left on Evo and Sprinter; the wheel nibble wraps |
| `Next_Md6_Projection` | A / B / C / Start on `#1F` in MD1 mode; X / Y / Z / Mode only in NR `#B2` |
| `ConnectorState_Replay` | record 300 frames with scripted pad input, replay, compare port reads and frame hashes |
| `DebugActions_NotJournaled` | scrub and step during recording leave no input-journal event |
| `ProfileSelection_Order` | signature beats port-read evidence beats the machine default; the reason string names the winner |
| `PortReadWatch_Sinclair` | a test program polling `#EFFE` bits 0-4 yields the Sinclair 1 decision within 50 frames |
| `LegacyIni_Defaults` | `KJoystick=1`, `Fire=KFIRE` in an Unreal Speccy ini become connector defaults; explicit keys win |

Haptics tests (`Haptics_*`: filters, probes, gate, not journaled, fallback, no cost when idle) are listed in
[haptics.md §12](haptics.md#12-testing).

## 4. Live verification

- WebAPI: `tools/verification/webapi/src/test_api_controllers.py`: every route, every error code, the envelope.
- MCP: `tools/verification/mcp/test_controllers.py` in `run_all.py`; checks `content[]` text, not only
  `structuredContent`.
- Instances started for live tests are muted (mixer master off).

## 5. Hardware-in-the-loop checklist (per OS)

| Device | Checks |
|:--|:--|
| Xbox Elite 2 USB and Bluetooth | identity; paddles and profile byte on profiles 0-3 where the raw bits are visible (mirror removed, D19), hidden on macOS USB; rumble, trigger rumble; battery over Bluetooth (not from GameController) |
| Xbox Series X / One | identity, `share` (Series X), rumble |
| DualSense / Edge | touchpad, gyro, back buttons (Edge), LED, player LEDs |
| Switch Pro | gyro, positional face buttons |
| 8BitDo (two modes) | one family for several PIDs |
| DragonRise DB9 adapter | learn wizard, hats as axes |
| Keyboard, mouse | presets, consumed keys, mouse button order per board |
| Stream Deck + XL | companion client actions |

## 6. Benchmarks

`core/benchmarks`: `Sample` with 0, 1 and 4 devices (target < 2 µs); report parsing per driver; router throughput at
1 kHz reports. Hot-path A/B per the performance guidelines once the frame-start hook lands.
