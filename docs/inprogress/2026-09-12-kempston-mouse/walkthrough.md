# Kempston Mouse — Implementation & Verification Walkthrough

A log of what was built, in order, and how it was checked — including what the first
"done" report got wrong. Current architecture: [integration.md](integration.md). Status and
open items: [README.md](README.md).

---

## 1. First pass (Pentagon 128, Scorpion 256, Qt capture)

**Built:**
- Core device `Mouse` (`core/src/emulator/io/mouse/mouse.{h,cpp}`): X/Y counters reset to
  31/85, active-low buttons, 4-bit wheel, `0xFF` when absent, MessageCenter observers for
  `MC_MOUSE_MOVE` / `MC_MOUSE_BUTTON` / `MC_MOUSE_WHEEL` filtered by emulator id.
- Mouse decode on Pentagon 128 and Scorpion 256, replacing the Scorpion stub that returned
  `0x00` on both axes (the "equal axes = no mouse" pattern).
- Qt capture: click to capture, blank cursor, re-centre warping with the synthetic-move
  filter, DPI- and upscale-aware sub-pixel accumulation, release on focus loss.
- Tests: `Mouse_Test` (7 at the time), `PortDecoder_PentagonScorpionMouse_Test` (4).

### 1.1 What the first report claimed, and what was actually true

| Claim in the first report | Reality |
|---|---|
| "Pentagon: standard Kempston decode (`A9=1, A5=0, low byte=DF`)" | Adding "low byte = `#DF`" contradicts the standard decode: that is ZX Evo's rule, and it drops mirrors real hardware answers. **Replaced** by the pure A9/A5 rule (`PortDecoder::Standard_IsPort_KempstonMouse`). *Later (2026-09-12) narrowed to MiSTer's A5-A0 = `011111` + A9 = 1 after cross-checking four references.* |
| "Scorpion Machine Suite: 92/92 PASSED" | **Wrong.** The Scorpion mouse arm had been spliced into `DecodePortIn` as a new `if` instead of an `else if`. Every arm above it — `#FE` keyboard, AY readback, `#1FFD`, SMUC, `#7EFD` — computed its result and then had it overwritten by the trailing fallback of the new `if`. A plain Scorpion read a **dead keyboard**, and the ProfROM lost its SMUC probes. The suite that was run did not cover those reads. **Fixed** (it is an `else if` now) and pinned by the new `ScorpionPorts_Test.KeyboardRowReadSurvivesLaterDecodeArms`. |
| "Preserved exact `#FF1F` joystick decode" | True — but [integration.md](integration.md)'s earlier text said the joystick was narrowed to a `0x1F` low byte. It was not. The joystick matches exactly `#FF1F` and wins because its arm runs before the mouse arm. |
| "Release on Escape or F12" | Only `Esc` and focus loss release capture. There is no `F12` binding. |
| "Capture code in `DeviceScreen` (`_accX`, `_warpTargetGlobalPos`)" | Moved out into `MouseManager` (see §2). |
| "`unreal-qt` clean build, zero warnings" | Not re-verified for this update. |

> **Lesson.** A green run of the tests you happened to pick is not evidence the machine
> still works. When a change edits a shared if/else-if chain, run the whole model suite and
> at least one test that reads the keyboard through that chain.

---

## 2. Added after the first pass

### 2.1 Every model decodes the mouse
Shared helpers `Standard_IsPort_KempstonMouse`, `Default_IsPort_KempstonMouse`,
`Default_Port_KempstonMouse_In` (`portdecoder.cpp:470-511`), and one `else if` arm in
Spectrum 48K, 128K, +3, Profi, Pentagon 128 (512 inherits) and Scorpion. The gate refuses the
address when the mouse is not fitted, when TR-DOS ports are on the bus (`CF_DOSPORTS`), or
when a registered peripheral owns it. On Scorpion the mouse is silent while TR-DOS is selected
(the Beta decode widens to A2-A0 = `111`) and, under the monitor latch alone, on `#xx1F`/`#xx5F`.

### 2.2 Configuration and feature
- `[INPUT] Mouse=` and `Wheel=` parsed (`config.cpp:312-349`), plus `SwapMouse=` and
  `MouseScale=` (parsed, not yet used).
- Feature `kempstonmouse` (alias `kmouse`, on by default).
- `Mouse::ApplyConfiguration` sets "fitted" and "wheel fitted"; `Reset` keeps them.
  `Core::Reset` now resets the mouse (it did not before).

### 2.3 Wheel off by default — found through the ProfROM
With the wheel nibble always on the bus, an idle mouse read `0x0F`. The Scorpion ProfROM
checks bits 3–5 of `#FADF` at page 5 `#08FB` and switched the mouse off. A classic Kempston
mouse has no wheel, so its upper bits read 1. Now:
- `Wheel=NONE` (every shipped config, and the default): `#FADF` = `0xF8 | buttons` → idle `0xFF`.
- `Wheel=KEMPSTON`: `(wheel << 4) | 0x08 | buttons` → idle `0x0F`.

New tests in `scorpion_kempston_mouse_test.cpp`: the detection mask, a cold boot that keeps
the mouse enabled, and the service-monitor pointer following injected motion (which also
confirms `dy` + = up on this program).

### 2.4 Thread safety
Counters became `std::atomic` with compare-and-swap moves, because desktop input, automation
and the click release run on three different threads.

### 2.5 Host layer rewrite
- `MouseManager` (`unreal-qt/src/emulator/mousemanager.{h,cpp}`) took over capture from
  `DeviceScreen`.
- Scaling is computed in **physical pixels over the source rectangle actually drawn**
  (`displaySourceRect()`), per axis, via the Qt-free `MouseDeltaAccumulator`
  (`core/src/emulator/io/mouse/mousedeltaaccumulator.h`, 7 accumulator + 2 wheel tests).
- Wheel notches accumulate (120 units per notch) so trackpads work.
- **macOS native relative mode** (`unreal-qt/src/platform/macos/mousecapture_macos.{h,mm}`):
  `QCursor::setPos` needs Accessibility permission, which an ad-hoc signed build loses on each
  rebuild, so the pointer silently froze. The native path freezes the cursor and reads OS deltas.
- `Esc` release is swallowed completely (press, repeats, release), so it never taps ZX BREAK.
  Buttons held at release are released in the machine.

### 2.6 The funnel and TTD
- `DebugMouseManager` (`core/src/debugger/mouse/debugmousemanager.{h,cpp}`): single entry for
  desktop, automation and tests. Validates automation ranges, refuses input during TTD
  replay, journals before applying while TTD records, runs timed click release in `OnFrame`.
- Desktop events now go `MouseManager` → MessageCenter → `Mouse::OnMouse*` →
  `DebugMouseManager::ApplyHost*` → `Mouse`.
- TTD: `PeripheralId::KempstonMouse = 7` checkpoint blob; input journal kinds `MouseMove`,
  `MouseButtons`, `MouseWheel`, `MouseCounters`; replay injects them at their time points.
- Keyboard brought to the same standard: timed key operations and desktop keystrokes are
  journalled and replay-guarded; `ReleaseAllKeys` journals `KeyboardReset`; sequence state is
  mutex-protected.

### 2.7 Automation
CLI `mouse`, WebAPI `/mouse/*` (with OpenAPI), Python `emu.mouse_*`, Lua `mouse_*`, MCP
`mouse_input`. Keyboard WebAPI unknown key names now return 400. See
[automation-interfaces.md](automation-interfaces.md).

### 2.8 Tests added with the funnel and automation
`core/tests/debugger/mouse/debugmousemanager_test.cpp` (29 tests, including keyboard
journalling), `core/tests/automation/cli-mouse-format-test.cpp`, MCP tool/dispatcher tests,
and the live WebAPI/interpreter tests `tools/verification/webapi/src/test_api_mouse.py` and
`test_api_interpreter.py`.

---

## 3. Verification status

| Check | Status for this update |
|---|---|
| Test files listed in [integration.md §8](integration.md#8-tests) exist with the named tests | Verified by reading the tree (2026-09-12) |
| Those tests pass | **Not re-run for this documentation update** (docs-only task, no build) |
| Scorpion keyboard regression covered | Test exists: `ScorpionPorts_Test.KeyboardRowReadSurvivesLaterDecodeArms` |
| `DebugMouseManager` unit tests | Present (`debugmousemanager_test.cpp`); not run for this update |
| Single end-to-end mouse record → seek → replay → compare-hash test | **Not found** |
| Qt capture on macOS / Windows / Linux by hand | Not verified here |

To run the mouse-related suites:

```bash
ninja -C cmake-build-release core-tests
./cmake-build-release/bin/core-tests \
  --gtest_filter="Mouse_Test.*:MouseDeltaAccumulator_Test.*:MouseWheelAccumulator_Test.*:*MouseMessageCenter_Test.*:PortDecoder_PentagonScorpionMouse_Test.*:ScorpionKempstonMouse_Test.*:DebugMouseManager_Test.*:Scorpion*"
```

Run the whole Scorpion and Pentagon model suites, not only the mouse filters — §1.1 is why.
