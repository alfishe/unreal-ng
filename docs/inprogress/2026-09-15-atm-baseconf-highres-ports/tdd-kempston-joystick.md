# Kempston joystick — technical design

| | |
|---|---|
| **Date** | 2026-10-01 |
| **Status** | Design; implementation follows (see §10) |
| **Closes** | the `#1F` joystick stub on ATM3, Scorpion and TS-Conf; P-2 of the BaseConf gap list (the port answered `0x00` forever); the "Kempston joystick" item of PLAN #13a (ZX Profi) can reuse the device |
| **Pattern** | the Kempston mouse ([design](../2026-09-12-kempston-mouse/design.md), `Mouse`, `DebugMouseManager`, the TTD input journal) |
| **Sources** | [sources-and-provenance.md](sources-and-provenance.md) |

## 1. What the hardware does

**ZX-Evo BaseConf (ground truth).** The joystick is not on the Z80 side of the FPGA at all.

| Fact | Evidence (pinned commit) |
|---|---|
| `#1F` is a full low-byte decode, answers **outside shadow only** (`KJOY = 8'h1F`, `loa==KJOY && !shadow`) | `fpga/base_trdemu/trunk/z80/zports.v:198, 343` |
| The read returns `kj_in`, a register in `zkbdmus.v` that the **AVR** loads over the slave SPI (`kj_stb`); the module forces it to 0 in simulation, no reset value in hardware | `z80/zkbdmus.v:62, 86`; `slave/slavespi.v:266`; `top.v:803, 839` |
| The AVR sends it as SPI register `0x23` (`SPI_KEMPSTON_JOYSTICK`) only when the value changed | `avr/baseconf/trunk/src/zx.h:41-42`, `joystick.c` (`joystick_task`, `sega_parsing`) |
| A 5-pin switch joystick on AVR port G: `kj = ~PING & 0x1F`: **D0 right, D1 left, D2 down, D3 up, D4 fire, D5-D7 = 0** | `joystick.c` (`joystick_task`), `pins.h:142-153`, `joystick.h:11-18` |
| A Sega pad (8 or 12 button) is detected at power-on; its buttons map either to the **keyboard matrix** (map 0, "default": P O A Q M ...; map 2 Super Mario; map 3 Elite) or, in the Kempston map (1), `jkey_state & 0xFF` goes to the same SPI register (all eight bits) | `joystick.c` (`joymaps`, `sega_parsing`) |
| Bit order and polarity: active high (a set bit = pressed), "bits 5-7: 0" | `joystick.h:11-18` |

**Other implementations.** xpeccy-plus (`hardware/common.c:275`, `input/joystick.c`): `#1F` full 8-bit decode (`{0x00ff, 0x001f}` in `evoPortMap`), the read is the state byte with **bits 7..5 forced to 1** unless the "extended buttons" option is on, and the port reads like any empty port when no Kempston is fitted. That 1-padding is not what the Evo does (the AVR sends D7..D5 = 0 for a switch joystick); it is the classic Kempston interface's open-bus look. Karabas Pro (`profi/rtl/karabas_pro.vhd:1790, 1822`): `#1F` from an 8-bit `joy_bus` when not in DOS/CP-M and `joy_mode = 000`. The Kempston mouse design records the decode spread across other machines (hardware-reference §3.1).

**Consequences for the model.**

1. A device holds one byte, active high, bits 0..4 named, bits 5..7 raw (a Sega pad or an extended interface drives them).
2. On the Evo the idle value is **0x00** and bits 5..7 are 0 unless the source sets them. xpeccy-plus's `| 0xE0` is a deviation we do not copy.
3. Outside shadow only (the decoder arm exists already); inside shadow `#1F` is the VG93 command/status port.
4. The Sega-pad-as-keyboard maps are the AVR translating a *gamepad* into keys. We have a keyboard, so the model needs no such map; a host gamepad source is future work (§9).

## 2. Scope and decisions

| ID | Decision | Why |
|---|---|---|
| J1 | One reusable device, `Joystick` (`core/src/emulator/io/joystick/joystick.{h,cpp}`), owned by `Core` like `Mouse`, `EmulatorContext::pJoystick` | Three machines carry the same stub today (`portdecoder_scorpion256.cpp`, `portdecoder_atm3.cpp`, `portdecoder_tsconf.cpp`) and Profi has the item open |
| J2 | State is one byte, active high (D0 right, D1 left, D2 down, D3 up, D4 fire, D5..D7 raw), atomic like the mouse counters | Matches the AVR register and every other emulator |
| J3 | Decoders that have the stub call `Joystick::Read()`; with no device or not fitted they keep today's `0x00` (the ZX-Evo/Scorpion/TSConf boards have the interface) | No behavior change by default except the value now follows the device |
| J4 | Fitting: `[INPUT] Joystick=KEMPSTON\|NONE`, default `KEMPSTON`; feature flag `kempstonjoystick` like `kempstonmouse` | Parity with `Mouse=` |
| J5 | All input goes through `DebugJoystickManager` (replay guard, TTD journal before the device changes, validation, timed taps), exactly as `DebugMouseManager` | One funnel, replay-safe |
| J6 | Host keyboard source lives **in the core**: `Keyboard` hands each applied PC key event (`PcKey`, the same place the PS/2 sink gets it) to `Joystick::OnPcKey` through a binding table. Default bindings: `Keypad8` up, `Keypad2` down, `Keypad4` left, `Keypad6` right, `Keypad0` fire; `[INPUT] JoystickKeys=up:kp8,down:kp2,left:kp4,right:kp6,fire:kp0` overrides, `JoystickKeys=` empty disables | PC key events are already journaled once and replayed through the same call, so a key-driven joystick replays for free; no Qt code; headless tests use it |
| J7 | Automation-driven input journals a new `TTDInputKind::Joystick` event carrying the full state byte | The same reason the mouse has its own kinds |
| J8 | TTD blob: `PeripheralId::KempstonJoystick`, the state byte (id taken after the one the A-7 design reserves: whichever lands first takes the next free number) | Checkpoints must restore the held buttons |
| J9 | No host gamepad in this change (Qt 6 has no gamepad module in the build) | Recorded in §9 |

## 3. Port behavior

- ATM3: arm `PortArm::Joystick` (`#1F`, shadow off): `Joystick::Read()`. In shadow the FDC arm answers, unchanged.
- Scorpion: `IsPort_KempstonJoystick` (`#FF1F`, exact): `Joystick::Read()`; the Service Monitor's `IN #FF1F` still sees `0x00` with nothing pressed.
- TS-Conf: `PortArm::Joystick` (`#1F` otherwise): same.
- Machines whose decoder has no joystick arm are unchanged; adding Pentagon/Profi arms is the follow-up the device enables (PLAN #13a).

## 4. Device API (core)

```
class Joystick : public ttd::TTDSerializable {
  static constexpr uint8_t kRight=0x01, kLeft=0x02, kDown=0x04, kUp=0x08, kFire=0x10;
  void Reset();                         // all released
  uint8_t Read() const;                 // the port value (state)
  void SetState(uint8_t state);         // raw, atomic
  void Press(uint8_t mask); void Release(uint8_t mask);
  uint8_t State() const;
  void SetPresent(bool); bool IsPresent() const;   // fitted
  void ApplyConfiguration();            // [INPUT] Joystick=, JoystickKeys=, feature flag
  bool OnPcKey(PcKey key, bool pressed);// binding table; returns true when bound
  // TTD blob: 1 byte
};
```

`DebugJoystickManager` (`core/src/debugger/joystick/`): `Press(name|mask)`, `Release`, `SetState(mask)`, `ReleaseAll`, timed `Tap(button, frames)` (default 2 frames, released in `OnFrame` like `Click`), `GetState()` -> `JoystickStateSnapshot { available, present, state, buttons[], pendingTap }`, names `up/down/left/right/fire` plus `fire2`..: bits 5..7 as `b5`, `b6`, `b7`; results as `MouseInjectResult` (reuse the shape: `JoystickInjectResult`).

## 5. Automation parity

Each surface gets the same four things, built on `DebugJoystickManager`, with the surface's docs and OpenAPI updated (the mouse is the template for each):

| Surface | Where the mouse lives | Joystick verbs |
|---|---|---|
| CLI | `cli-processor-mouse.cpp` | `joystick press\|release\|set\|tap\|status\|list` |
| WebAPI + OpenAPI | `api/mouse_api.cpp`, `openapi_spec.cpp` | `GET /emulator/{id}/joystick`, `POST .../joystick/press`, `/release`, `/set`, `/tap` |
| MCP | `mcp-tools.cpp` (`mouse_input`) | `joystick_input` tool: actions `press`, `release`, `set`, `tap`, `status` |
| Lua | `lua_emulator.h` | `emu:joystickPress(name)`, `joystickRelease`, `joystickSet`, `joystickTap`, `joystickState` |
| Python | `python_emulator.h` | the same names in snake case |
| Qt | `mousemanager.cpp`, status bar | a status indicator with the held directions; the host keys arrive through the core mapping (J6), no Qt key handling |
| Docs | `.recipe/`, automation READMEs | one recipe `input/joystick.md`; `.recipe/machines/atm.md` line |

## 6. TTD

- `TTDInputKind::Joystick` event (`buttonMask` reused for the state byte), recorded by `DebugJoystickManager` before the device changes, replayed by `InjectDueEvents`; live sources refused while `ttdReplayActive`.
- `KempstonJoystick` blob: the state byte and a version byte; part of every machine that has the device.
- PC-key-driven input is already journaled as PC keys (`TTDInputKind::PcKey`), so the binding must be applied from the replay path too: `Joystick::OnPcKey` is called from the same function the PS/2 sink is.

## 7. Tests (written first)

| ID | Test | Expect |
|---|---|---|
| JOY-1 | device: press / release / set / read | active-high byte, D5..D7 preserved |
| JOY-2 | `Reset()`, power-on state | `0x00` |
| JOY-3 | ATM3 `IN #1F` outside shadow | the state; inside shadow the VG93 |
| JOY-4 | ATM3 `#FB1F`/`#FF1F` (mouse addresses) | not the joystick (full low-byte decode, exact `#1F` low byte only) |
| JOY-5 | Scorpion `IN #FF1F`, TS-Conf `IN #1F` | the state; Service Monitor idle reads `0x00` |
| JOY-6 | not fitted (`Joystick=NONE`, feature off) | decoders answer as today (`0x00`), `GetState().present == false`, the manager reports a warning |
| JOY-7 | `OnPcKey`: default bindings, custom `JoystickKeys=`, empty disables, unknown key ignored | state follows; unbound keys untouched |
| JOY-8 | a bound PC key still reaches the PS/2 sink (ATM3) | NedoOS keypad input unchanged |
| JOY-9 | manager: validation, unknown name, `tap` releases after N frames, `ReleaseAll` cancels a tap | statuses and messages as the mouse |
| JOY-10 | replay guard | live input refused while replaying |
| JOY-11 | TTD journal: record, seek back, replay | identical state at every frame; PC-key-driven too |
| JOY-12 | TTD blob round trip + hash | state restored |
| JOY-13 | one test per surface (CLI command text, WebAPI JSON incl. the OpenAPI document, MCP tool, Lua, Python) | the same state change and the same errors |

## 8. Risks

- Keypad keys also reach the AVR PS/2 log (NedoOS reads it). Binding must not swallow them (JOY-8).
- A guest that reads `#1F` outside the game loop (loaders testing "joystick present" with `IN #1F` = 0xFF): our idle is 0x00, as the Evo board gives; the Service Monitor case is covered by JOY-5.
- The mouse design's open item (frame batching of journal events) does not apply: joystick changes are few.

## 9. Not in scope

Host gamepad (SDL / Qt gamepad), the Sega-pad keyboard maps of the AVR, the 12-button pad's extra buttons beyond raw D5..D7, Pentagon/Profi decoder arms (PLAN #13a), the 1-padding of the upper bits xpeccy-plus adds.

## 10. What landed

Core part landed 2026-10-01 (uncommitted at the time of writing). The automation surfaces (§5) are a separate task.

**Done**

- `Joystick` (`core/src/emulator/io/joystick/joystick.{h,cpp}`), owned by `Core`, `EmulatorContext::pJoystick`; state byte, active high, atomic; `[INPUT] Joystick=KEMPSTON|NONE` (default KEMPSTON, `config.input.joystick`), `JoystickKeys=` (absent: keypad defaults, present and empty: no host keys), feature `kempstonjoystick` (alias `kjoy`, on by default). Like the mouse, a Z80 reset re-applies the fitting but keeps the held buttons.
- Decoders: the three `0x00` stubs (Scorpion `#FF1F`, ATM3 `PortArm::Joystick`, TS-Conf `PortArm::Joystick`) read `Joystick::Read()`; each machine's gating is unchanged; no device or not fitted still gives `0x00`. New `PortDecoder::HasKempstonJoystick()` (true on those three) tells the rest of the core where the device is wired.
- Host keys in the core (J6): `Keyboard::ApplyPcKey` hands the key to the PS/2 sink **and** to `Joystick::OnPcKey`. `SubmitHostPcKey`, `DebugKeyboardManager::ApplyPcKey` and `ApplyInputEvent(PcKey)` now gate on `Keyboard::WantsPcKey` (PS/2 sink present, or the key is bound and the machine decodes the joystick) instead of on the PS/2 sink alone, so Scorpion and TS-Conf journal and apply a bound key too, and a machine without the arm (Pentagon, ...) still pays nothing.
- `DebugJoystickManager` (`core/src/debugger/joystick/`): `Press` / `Release` (a name or a `+` / `,` list), `PressMask` / `ReleaseMask`, `SetState`, `ReleaseAll`, timed `Tap` (default 2 frames, released in `OnFrame`, which `MainLoop::CompleteFrame` pumps), `GetState()` snapshot, `JoystickInjectResult` shaped like `MouseInjectResult`. Every change goes through `SubmitLiveInput` as `TTDInputKind::Joystick` (14, `buttonMask` = the whole state byte), so the replay guard, the journal-before-apply rule and replay come from the existing gateway.
- TTD: `PeripheralId::KempstonJoystick = 23` (version byte + state byte), `ttd.ksy`, `ttdfileinfo.cpp`, contract-test row; registered **only for machines whose decoder answers `#1F`** (ATM3, Scorpion, TS-Conf), so the checkpoints of every other machine are unchanged. Also portable in the machine-state transfer.
- Tests (all new rows pass): `joystick_test.cpp` (JOY-1, 2, 7, 12 and the device half of JOY-6, config keys, feature flag), `debugjoystickmanager_test.cpp` (JOY-6 manager half, 9, 10, 11 incl. a PC-key and automation mixed record / seek back and forth), rows in `portdecoder_atm3_test.cpp` (JOY-3, 4, 6, 8), `scorpionports_test.cpp` and `portdecoder_tsconf_test.cpp` (JOY-5, 6).

**Moved baselines (intended, caused by the new blob only)**

- `testdata/machines/tsconf/ttd/sprites.ttd` re-recorded (the TS-Conf checkpoints carry one more device; `TTD_Corpus_Test` compares device sets).
- `testdata/ttd/bench/v1-ci-gate.txt`: three ATM3/idle rows (`bm3_device_blobs_bpf`, `bm7_file_bpf`, `bm7_file_bytes`) = +14 bytes per capture (854 over 61). The RAM, write-journal, coverage and checkpoint rows did not move, which isolates the change to the blob. CoreGolden and ScreenZXFrames did not move.

**Deviations from this design**

- JOY-4 as written ("`#FB1F` / `#FF1F` are not the joystick") contradicts the full low-byte decode of §1: on ATM3 every `#xx1F` outside shadow IS the joystick. The test pins that, and that the mouse addresses (`#xxDF`) and the neighbors of `#1F` are not.
- `pckey` names the keypad `kp_8`; the short `kp8` of §2 / J6 is accepted as an alias by `JoystickKeys=` and by `SetBindings`. `BindingsSpec()` prints the `kp_8` spelling.
- The state byte of `TTDInputKind::Joystick` rides in `buttonMask` (as §6 says); only machines with the arm have the blob, so the blob is not on "every machine that has the Joystick object" (the object exists everywhere, as the mouse does).
- "Release all keys" (`KeyboardReset` event) also releases every button that is bound to a key, whoever pressed it (an automation press of a key-bound button included): the key-held buttons are not tracked apart from the state byte.
- Not in the design: the manager reports a warning when the machine's decoder has no joystick arm, and the snapshot has a `wired` flag and the bindings text.
- Peripheral id 23 is taken; the raster blob of [tdd-a7-raster-selection.md](tdd-a7-raster-selection.md) must shift (note added there).
- `RecordSeekReplay_KeyDrivenAndAutomationInput` takes about 100 ms (ATM3 machine, a dozen recorded frames, 16 seeks); the rest are a few ms.

**Not done**

- Every automation surface of §5 and JOY-13: CLI, WebAPI + OpenAPI, MCP `joystick_input`, Lua, Python, Qt status indicator, `.recipe/input/joystick.md`.
- A ZX-Poly group does not route the joystick (no ZX-Poly base model has the arm).
- No Pentagon / Profi arms (PLAN #13a), as §9.
