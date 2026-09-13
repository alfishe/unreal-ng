# Kempston Mouse — Automation Interfaces

Status: **implemented (2026-09-12).** This document specifies how
scripts, remote tools and AI agents move and click the emulated Kempston Mouse. It covers the
shared core entry point and each automation module: the command-interface spec, WebAPI,
CLI, Lua, Python and MCP.

It was written as a proposal before the code. Sections 2–6 are kept as the design record;
**§0 below says what was built and which decisions changed**, and superseded passages are
marked *(superseded, see §0)*. User-facing reference:
[command-interface.md §11](../../emulator/design/control-interfaces/command-interface.md#11-mouse-input-injection).

Companion documents: [design.md](design.md) (device, host mapping, TTD),
[integration.md](integration.md) (core, port decoders, Qt), [hardware-reference.md](hardware-reference.md).

Citations in §2–§6 are `file:line` against the working tree on 2026-09-12 **before**
implementation; many have moved. §0 citations were checked after implementation, the same day.
The mouse files were still uncommitted, so lines can move again.

---

## 0. As built

### 0.1 What exists

| Piece | Where | State |
|---|---|---|
| Core funnel `DebugMouseManager` | `core/src/debugger/mouse/debugmousemanager.{h,cpp}`; owned by `DebugManager` (`debugmanager.cpp:28`); `OnFrame` pumped from `mainloop.cpp:572-574` | Implemented |
| Atomic counters, CAS `Move`/`SetWheel` | `core/src/emulator/io/mouse/mouse.{h,cpp}` | Implemented |
| Desktop input through the funnel | `Mouse::OnMouse*` → `DebugMouseManager::ApplyHost*` (`mouse.cpp:162-196`) | Implemented |
| TTD journal for mouse | `TTDInputKind::MouseMove / MouseButtons / MouseWheel / MouseCounters` (`ttdinputjournal.h:64-72`); `TimeTravelManager::RecordMouse*` | Implemented |
| WebAPI | `core/automation/webapi/src/api/mouse_api.cpp`; routes `emulator_api.h:356-367`; OpenAPI `src/openapi/openapi_mouse.inc` | Implemented |
| CLI | `core/automation/cli/src/commands/cli-processor-mouse.cpp`, `cli-mouse-format.h`; registered `cli-processor.cpp:209`; global help `:668-676` | Implemented |
| Python | `core/automation/python/src/emulator/python_emulator.h` (`mouse_*` methods, helpers near the top of the file) | Implemented |
| Lua | `core/automation/lua/src/emulator/lua_emulator.h` (`mouse_*` globals, resolved through `effectiveEmulator()`) | Implemented |
| MCP `mouse_input` | `core/automation/mcp/src/mcp-tools.cpp` (`RegisterMouseInput`), briefing `mcp-dispatcher.cpp:23`, README tool table | Implemented |
| Tests | `core/tests/debugger/mouse/debugmousemanager_test.cpp` (29 tests: validation, clicks, replay guard, journalling incl. host input, journal inject, concurrency, TTD blob and seek, reset, feature flag, keyboard journalling); `core/tests/automation/cli-mouse-format-test.cpp` (8); `mcp-tools-test.cpp` / `mcp-dispatcher-test.cpp` (mouse_input); `tools/verification/webapi/src/test_api_mouse.py` | Present in the tree. **Pass/fail not verified for this doc update.** |
| Docs | `docs/emulator/design/control-interfaces/{command,webapi,cli,python,lua}-interface.md` | Updated |

### 0.2 Decisions taken

| # | Decision | Outcome |
|---|---|---|
| Q1 | Delivery | **Direct call on the caller's thread (option B)**, with atomic counters. As proposed. |
| Q2 | Active-emulator WebAPI routes | **No.** Path `{id}` only; MCP `target:"auto"` covers the one-instance case. |
| Q3 | Desktop input through the funnel | **Yes, done in this work** (not deferred). Keyboard fixed the same way: desktop keystrokes and timed key operations are now journalled and replay-guarded. |
| Q6 | `SetCounters` and TTD | **Journalled as an absolute record** (`MouseCounters`), so `mouse set` / `POST /mouse/counters` is **allowed while recording**. The `Unsupported` status was dropped. |
| Q8 | Python errors | **Raise** (`ValueError` for bad arguments, `RuntimeError` for replay / no device). |
| Q9 | MCP runs frames after a click | **No.** |
| — | TTD journal support | **Supported.** `journalSupported = true`; status reports `"ttd_journal": "supported"`. |
| — | HTTP 409 | **Only during TTD replay.** No other 409 case exists. |
| — | Warnings | Success results carry a `warning` when the change cannot reach the program: mouse not fitted (`mouse not present: guest reads floating bus on the mouse ports`), or a wheel step with no wheel fitted (`no wheel fitted ([INPUT] Wheel=NONE): the guest does not see the wheel counter`). |
| — | Status adds `available` and `wheel_enabled` | `available` = a device exists; `wheel_enabled` = `[INPUT] Wheel=KEMPSTON`. |
| — | Host input vs automation limits | Host calls (`ApplyHost*`) skip the ±127 / ±7 limits (a fast flick can exceed them in one event) but keep the replay guard and journal. |

### 0.3 Differences from the proposal below

- **§2.1 is out of date.** Every model decoder on this branch now decodes the mouse, not only
  Pentagon and Scorpion; `Core::Reset` now resets the mouse; `Mouse=`/`Wheel=` are parsed and
  feature `kempstonmouse` exists (so §4.2.8's "neither exists yet" no longer holds).
- **§4.1.2 API:** no `Unsupported` status; `Click` takes `uint32_t` frames (so 70000 is
  rejected instead of wrapping); `SetCounters` takes `int` (range-checked); `MouseInjectResult`
  has a `warning` field; `MouseStateSnapshot` has `available` and `wheelEnabled`; host entry
  points `ApplyHostMove/Buttons/Wheel` were added.
- **§4.1.4:** no journal stubs, no "one warning per recording session" — the journal is real.
- **§4.2.3:** the absent-device warning text is the one in §0.2; while absent, status `ports`
  read 255.
- **CLI:** no `mouse m` alias (the proposed §11 table listed one). `mouse buttons` also accepts
  space-separated names and `none`. A `mouse help` subcommand exists.
- **Python and Lua:** `ports` values are integers, as in the WebAPI; the state has no `available` key (`mouse_status()` raises / returns `nil, err`
  instead). In Lua `pending_click` is absent (nil) rather than a null value. Both also expose
  `mouse_click_pending()`.
- **Tests (§5):** the planned `core/tests/debugger/mouse/mouse_injection_integration_test.cpp`
  (§5.3) does not exist as a separate file; its cases (run_frames hold time, journalling of
  host input, TTD seek) were folded into `debugmousemanager_test.cpp`. The planned
  `ClickRelease_UsesJournalledPath` hook became `Recording_JournalsEveryMouseMutationBeforeApplying`.
  The CLI parsers were made header-only (`cli-mouse-format.h`) and are unit-tested (§5.6 option 1).
- **Keyboard WebAPI:** unknown key names now return 400 (`keyboard_api.cpp`, `rejectUnknownKey`),
  matching its OpenAPI; covered in `test_api_mouse.py` (`test_unknown_key_400`).

### 0.4 Still open

| # | Question | State |
|---|---|---|
| Q4 | Report whether the current machine state actually routes the mouse ports (`ports_decoded`) | Open. Now narrower: every model decodes the mouse, but TR-DOS (`CF_DOSPORTS`) or a registered peripheral can still hide it, and status does not say so. |
| Q5 | Built-in "glide" for moves over ±127 | Open; not implemented. |
| Q7 | ±127 or ±63 per call | Kept ±127. Open only if real software shows otherwise. |
| Q10 | Is `dy` + = up right for real software? | Confirmed for the Scorpion ProfROM service-monitor pointer (`scorpion_kempston_mouse_test.cpp`). Not yet checked on Art Studio or similar. |
| — | `ports` as integers (WebAPI) vs hex strings (Python, Lua) | Decided: integers everywhere. |

---

## Glossary

| Term | Meaning here |
|---|---|
| **Counter** | The mouse's X or Y register. An 8-bit number that goes up or down as the mouse moves and wraps around (255 + 1 = 0). The machine never sees a screen position, only these counters. |
| **Relative device** | A device that reports *how far* it moved, not *where* it is. The Kempston Mouse is one. The program running on the machine works out its own cursor position from the counter changes. |
| **Active-low** | A pressed button reads as bit `0`, released as `1`. `0xFF` = nothing pressed. |
| **Emulated pixel** | One pixel of the machine's own screen. It has nothing to do with host window size or monitor DPI. |
| **Funnel** | The single core class that every input source calls. Checks and TTD recording then live in one place. |
| **MessageCenter** | The emulator's in-process publish/subscribe bus. Messages are delivered **later, on its own worker thread**. |
| **TTD** | Time-Travel Debugging: record execution, then seek back and replay it. |
| **Input journal** | The TTD list of timestamped input events that replay feeds back in. It is keyboard-only today. |
| **Replay guard** | A check that rejects live input while TTD is replaying recorded history. |
| **Selected emulator** | The instance chosen globally through `EmulatorManager`. The CLI, UI and Lua use it when no id is given. |

---

## 1. Goal and scope

### 1.1 Goal

Let every automation front end do the same five things to the Kempston Mouse of a chosen
emulator instance, with the same meaning everywhere:

1. **Move** by a whole number of emulated pixels (`dx`, `dy`).
2. **Press / release** a button, or set the full set of pressed buttons at once.
3. **Click**: press, hold for N emulated frames, then release on its own.
4. **Scroll** the wheel by whole notches.
5. **Query** state: counters, buttons, wheel, presence, the bytes the machine would read
   from the three ports, and any pending click.

A debug-only sixth operation, **set counters**, writes X/Y directly. Section 4.2.6 covers it.

### 1.2 Non-goals

| Non-goal | Why |
|---|---|
| Absolute "move pointer to screen (x, y)" | The device is relative (§1 Glossary). The machine's program keeps its own cursor variable, so the emulator cannot know where the cursor is on screen. See §4.2.7. |
| Host-side mapping (DPI, window upscale, sub-pixel carry, capture) | That belongs to the Qt `MouseManager` (`unreal-qt/src/emulator/mousemanager.h:20-31`). Automation sends clean emulated-pixel steps and must not go through it. |
| Button swap (`mouseswap`) | A host user preference ([design §4.2](design.md#42-state-and-register-semantics)). Automation names *device* buttons: `left` is always D0. |
| Changing presence or mouse type | That is machine configuration ([design §7.4-§7.5](design.md#74-mouse--type-selector)), not input. Status reports presence read-only. See §4.2.8. |
| The TTD journal format extension | A separate task (README D-2, [design §6.2](design.md#62-input-journal-the-real-work)). This document only fixes the **hook point**. |
| Joystick-on-mouse (`joymouse`) | Deferred ([design §7.3](design.md#73-joymouse--deferred)). |
| Kempston Joystick automation | Separate feature. The same funnel pattern would apply. |

---

## 2. Current state (verified before implementation — *superseded, see §0*)

### 2.1 The mouse device

| Fact | Where |
|---|---|
| `MouseEvent` can only be built through the factories `Move(dx, dy, targetId)`, `Buttons(mask, targetId)` and `Wheel(steps, targetId)`. The constructor is private. | `core/src/emulator/io/mouse/mouse.h:39-64` |
| Units: `dx` + = right, `dy` + = **up**. Mask is active-low, D0 = Left, D1 = Right, D2 = Middle. Wheel + = away from the user. | `mouse.h:33-37` |
| Direct methods `Move`, `SetButtons`, `SetWheel`, `SetPresent` and getters `GetX/GetY/GetButtons/GetWheel/IsPresent` | `mouse.h:79-88` |
| `Move` is a plain read-modify-write: `_x = static_cast<uint8_t>(_x + dx)`. No lock, not atomic. | `mouse.cpp:75-79` |
| `SetWheel` adds and masks to 4 bits | `mouse.cpp:86-89` |
| Register 0 = `(wheel << 4) \| 0x08 \| (buttons & 0x07)`. X and Y are raw. All registers read `0xFF` when absent. | `mouse.cpp:49-73` |
| Reset: X = 31, Y = 85, buttons `0xFF`, wheel 0, **present = true** | `mouse.cpp:38-47` |
| Observers filter by payload kind and by `targetId`. **An empty `targetId` is accepted by every instance** (broadcast). | `mouse.cpp:98-111` (line 107) |
| `Mouse` is created for every model in `Core::Init` | `core/src/emulator/cpu/core.cpp:145-153` |
| `Core::Reset` does **not** reset the mouse. Keyboard, sound, screen and others are reset. | `core.cpp:566-575` |
| Only the Pentagon 128 and Scorpion 256 decoders answer the mouse ports, and only while TR-DOS ports are off (`!CF_DOSPORTS`) | `portdecoder_pentagon128.cpp:106-108, 673-676`; `portdecoder_scorpion256.cpp:217-219, 640-643` |
| The Qt front end posts to MessageCenter with the emulator id, and negates screen Y | `unreal-qt/src/emulator/mousemanager.cpp:209, 265, 271` |
| Delivery contract test: posts, then waits with `TestWait::For` because delivery is asynchronous; covers foreign-id and wrong-kind rejection | `core/tests/emulator/io/mouse/mouse_messagecenter_test.cpp:20-22, 83-99, 150-172` |

### 2.2 No automation surface exists yet

Nothing in `core/automation/` references the mouse. `docs/emulator/design/control-interfaces/command-interface.md:2627-2628`
lists `mouse move <x> <y>` and `mouse click <button>` as "🔮 Planned" under *Future
Capabilities*. The first row suggests an absolute position, which this document rejects (§4.2.7).

---

## 3. Keyboard precedent — the pattern to mirror

### 3.1 Core: `DebugKeyboardManager`

| Aspect | Keyboard today | Where |
|---|---|---|
| Ownership | One per emulator, owned by `DebugManager`, reached with `context->pDebugManager->GetKeyboardManager()` | `debugmanager.h:32, 50`; `debugmanager.cpp:26, 105-108` |
| Immediate ops | `PressKey` / `ReleaseKey` call `Keyboard::PressKey` **directly on the caller's thread**. They do not use MessageCenter. | `debugkeyboardmanager.cpp:53-111` |
| Replay guard | `if (_context->ttdReplayActive) return;` — the call is dropped **silently** | `debugkeyboardmanager.cpp:63-64, 95-96` |
| Journal hook | `RecordInputEvent(key, pressed)` is called **before** the change is applied, and only when `IsRecording()` | `debugkeyboardmanager.cpp:70-73, 99-102` |
| Timestamp source | `RecordInputEvent` reads `frame_counter` and `z80->t` at call time | `timetravelmanager.cpp:1319-1340` |
| Timed ops | `TapKey` queues a sequence. `OnFrame()` counts frames down and releases. | `debugkeyboardmanager.cpp:118-129, 269-287, 657-700` |
| Frame pump | `MainLoop::OnFrameEnd` calls `GetKeyboardManager()->OnFrame()`. Manual `RunNFrames` also calls `OnFrameEnd` at every frame boundary. | `mainloop.cpp:563-568` (inside `OnFrameEnd`, `:381`); `emulator.cpp:2245, 2310` |
| Name resolution | `static ResolveKeyName(string)` returns `ZXKEY_NONE` for an unknown name | `debugkeyboardmanager.h:254`; test `keyboard_test.cpp:107-114` |
| State query | `IsSequenceRunning`, `GetPressedKeys`, `GetMatrixState` | `debugkeyboardmanager.h:184, 235-243` |

**Three weak spots in the precedent that the mouse must not copy.**

1. **Timed ops are not journalled.** `ExecuteEvent` calls `_keyboard->PressKey/ReleaseKey`
   directly and skips the journalled `PressKey`/`ReleaseKey` wrappers and the replay guard
   (`debugkeyboardmanager.cpp:890-940`). A `TapKey` made while recording is therefore missing
   from the journal.
2. **No locking between threads.** `QueueSequence` runs on the automation thread and
   `OnFrame` runs on the emulator thread. Both touch `_eventQueue`, `_frameCountdown` and
   `_inTapHoldPhase`, and there is no mutex (`debugkeyboardmanager.h:84-104`).
3. **The Qt path skips the funnel.** Desktop keystrokes arrive through
   `Keyboard::OnKeyPressed` on the MessageCenter thread (`keyboard.cpp:384-432`), which never
   calls `DebugKeyboardManager`. They are not journalled either. The only callers of
   `RecordInputEvent` are `debugkeyboardmanager.cpp:72, 101`.

### 3.2 Per-module keyboard surface

| Module | Surface | Emulator targeting | Invalid input | Where |
|---|---|---|---|---|
| **Spec doc** | Not listed as implemented. Only "Planned" rows under *Future Capabilities → Input Injection* | — | — | `command-interface.md:2613-2630` |
| **WebAPI** | `POST /api/v1/emulator/{id}/keyboard/{tap,press,release,combo,macro,type,release_all,abort}`, `GET …/keyboard/{status,keys}`. Body fields are snake_case (`key`, `keys`, `frames`, `delay_frames`, `tokenized`). | Path `{id}` only. No active-emulator variant. | Missing field → 400 `{"error":"Bad Request","message":…}`. Unknown emulator → 404. No manager → 500. **Unknown key name returns 200** because `PressKey` quietly ignores `ZXKEY_NONE`, even though OpenAPI documents a 400. | routes `emulator_api.h:338-354`; decls `:1043-1064`; impl `keyboard_api.cpp:24-87` (tap), `:89-149` (press); OpenAPI `openapi_keyboard.inc:1-110` (400 claim `:78`) |
| **WebAPI success shape** | `{"success":true, "key":…, "frames":…, "message":"Key tapped: a"}`. CORS header is added on every path. | | | `keyboard_api.cpp:78-86` |
| **Active-emulator precedent** (elsewhere) | e.g. `GET /api/v1/emulator/state/audio/beeper` resolves through `getEmulatorWithGlobalSelection()`. 0 instances → 404, several → 400 "specify emulator ID". | | | `emulator_api.h:222-230`; `state_audio_api.cpp:837-860`; `emulator_api.cpp:151` |
| **OpenAPI** | Maintained by hand in `src/openapi/openapi_keyboard.inc`, included from `openapi_spec.cpp:151`, tag `"Keyboard Injection"`, coverage check `tools/verification/webapi/verify_openapi_coverage.py` | | | `OPENAPI_MAINTENANCE.md:80-108` |
| **CLI** | `key` / `keyboard` → `HandleKey`, which dispatches on subcommands `press/release/tap/combo/macro/type/list/clear/help` | Selected emulator (`GetSelectedEmulator(session)`), otherwise "Error: No emulator selected." | Validates key names: `Error: Unknown key 'x'. Use 'key list'…` | `cli-processor.cpp:204-206`; `cli-processor.h:222-233`; `cli-processor-keyboard.cpp:13-82, 84-114, 116-144`; global help `cli-processor.cpp:655-663` |
| **Python** | Methods on the `Emulator` class: `key_tap(key, frames=2)`, `key_press`, `key_release`, `key_combo(keys, frames=2)`, `key_macro`, `key_type(text, delay_frames=2)`, `key_trdos_command`, `key_release_all`, `key_is_running`, `key_abort`, `key_list` | Per `Emulator` object (`emu_get(id)`, `emu_get_selected()`) | Returns `False` only if the manager is missing. Unknown names return `True` and do nothing. | `python_emulator.h:1783-1848`; selection `:113-119` |
| **Lua** | **No keyboard bindings at all.** A grep for `keyboard`/`key_tap` in `core/automation/lua/` finds nothing. The style to mirror is flat global functions (`get_pc`, `run_frames`, …). | Most bindings use the bound `_emulator` (73 `if (!_emulator)` guards). Newer ones use `effectiveEmulator()`, which falls back to the selected emulator. | Varies | `lua_emulator.h:57-71` (`effectiveEmulator`), `:176-180` (`get_pc`), `:557-560` (`run_frames`) |
| **MCP** | Smart tool `type_input` with `action` ∈ `type, tap, press, release, combo, macro, release_all, status, list_keys`, forwarding to WebAPI through `ResolveAndForward` | `target` = id or `"auto"` (0 instances → create 128K, 1 → use it, >1 → refuse) | Missing required argument → `ToolResult::Error` (`isError:true`), never a JSON-RPC error | `mcp-tools.cpp:885-1007`; `mcp-tool-utils.h:222-234`; `target-resolver.h:5-8`; README `core/automation/mcp/README.md:67-77, 88-92` |
| **MCP router** | `search_api` scores OpenAPI operations by path segment, summary, tags and parameter names. `invoke_api` substitutes `{id}`. The spec is fetched from `GET /api/v1/openapi.json`. **Any route documented in an `.inc` file becomes discoverable with no MCP code change.** | | | `mcp-router.cpp:40, 212-250, 384` |

---

## 4. Proposal

### 4.1 Core funnel: `DebugMouseManager`

#### 4.1.1 Where it lives

- `core/src/debugger/mouse/debugmousemanager.{h,cpp}`. It sits beside
  `debugger/keyboard/`, and `core/src/CMakeLists.txt:71` picks it up through `GLOB_RECURSE`.
- Owned by `DebugManager` next to `_keyboardManager`, with a `DebugMouseManager* GetMouseManager()` accessor.
- The manager reads `_context->pMouse` **on every call** instead of caching it in the
  constructor. It then does not depend on whether `DebugManager` or `Core::Init` runs first.
- `MainLoop::OnFrameEnd` calls `GetMouseManager()->OnFrame()` right after the keyboard
  call (`mainloop.cpp:563-568`).

#### 4.1.2 API

```cpp
enum class MouseButton : uint8_t { Left = 0x01, Right = 0x02, Middle = 0x04 }; // bit in the mask

enum class MouseInjectStatus : uint8_t
{
    Ok,
    NoDevice,        // _context->pMouse == nullptr
    InvalidArgument, // out-of-range value; message says which
    ReplayActive,    // TTD replay in progress - live input refused
    Unsupported      // e.g. SetCounters while TTD is recording (§4.2.6)
};

struct MouseInjectResult
{
    MouseInjectStatus status = MouseInjectStatus::Ok;
    std::string message;              // human-readable, reused by every front end
    bool ok() const { return status == MouseInjectStatus::Ok; }
};

struct MouseStateSnapshot
{
    bool     present;
    uint8_t  x, y;                    // raw counters
    uint8_t  buttonMask;              // active-low, internal value
    uint8_t  wheel;                   // 0..15
    uint8_t  portButtons, portX, portY; // what IN #FADF / #FBDF / #FFDF returns now (0xFF when absent)
    std::optional<MouseButton> pendingClickButton;
    uint16_t pendingClickFramesLeft;
    bool     journalSupported;        // false until the TTD journal task lands
};

class DebugMouseManager
{
public:
    static constexpr uint16_t DEFAULT_CLICK_FRAMES = 2;   // same default as keyboard tap
    static constexpr int      MAX_MOVE_PER_CALL    = 127; // see §4.3
    static constexpr int      MAX_WHEEL_PER_CALL   = 7;

    explicit DebugMouseManager(EmulatorContext* context);

    // Immediate - applied before the call returns
    MouseInjectResult Move(int dx, int dy);
    MouseInjectResult Wheel(int steps);
    MouseInjectResult PressButton(MouseButton button);
    MouseInjectResult ReleaseButton(MouseButton button);
    MouseInjectResult SetPressedButtons(uint8_t pressedBits); // bit set = pressed (D0 L, D1 R, D2 M)
    MouseInjectResult ReleaseAllButtons();                    // also cancels a pending click

    // Timed - press now, release after holdFrames emulated frames (OnFrame)
    MouseInjectResult Click(MouseButton button, uint16_t holdFrames = DEFAULT_CLICK_FRAMES);
    bool IsClickPending() const;
    void AbortClick();                                         // releases the held button now

    // Debug
    MouseInjectResult SetCounters(uint8_t x, uint8_t y);

    // Query
    MouseStateSnapshot GetState() const;

    // Names
    static std::optional<MouseButton> ResolveButtonName(const std::string& name); // left/l, right/r, middle/m; case-insensitive
    static std::string GetButtonName(MouseButton button);
    static std::vector<std::string> GetAllButtonNames();

    // Frame pump (emulator thread)
    void OnFrame();

private:
    MouseInjectResult Guard() const;               // NoDevice / ReplayActive checks
    void JournalMove(int dx, int dy);              // hook points - see §4.1.4
    void JournalButtons(uint8_t mask);
    void JournalWheel(int steps);
    void ApplyButtons(uint8_t activeLowMask);      // journal + Mouse::SetButtons, shared by immediate and timed paths

    EmulatorContext* _context = nullptr;
    mutable std::mutex _mutex;                     // guards pending-click state (fixes weak spot 2)
    std::optional<MouseButton> _pendingButton;
    uint16_t _pendingFrames = 0;
};
```

Unlike the keyboard manager, which returns `void` and drops bad input silently, every method
returns a **result with a reason**. Each front end then turns one status into its own error
form (HTTP code, CLI text, Python exception, Lua `nil, err`, MCP `isError`) without checking
the input again.

#### 4.1.3 How it reaches the device: direct call, not MessageCenter

Three delivery options were compared:

| | A. Post `MouseEvent` to MessageCenter | **B. Call `Mouse` directly on the caller's thread** | C. Queue and apply on the emulator thread at the next frame end |
|---|---|---|---|
| When it takes effect | Later, on the MessageCenter worker | **Before the call returns** | At the end of the current frame |
| `GET status` right after a `move` | May still show old values (the test has to poll: `mouse_messagecenter_test.cpp:94`) | **Shows new values** | Shows old values until a frame ends |
| While paused: `move` then `run_frames 1` | Race. The worker may apply after the frame has started. | **Deterministic** | Applied only at the end of that frame, so the first frame misses it |
| Journal timestamp matches when the change happened | No. Recorded at post time, applied later. | **Yes.** Same moment, same as keyboard. | Yes (frame boundary) |
| Instance targeting | Must carry `targetId`. An empty id is a broadcast (`mouse.cpp:107`). | **Not needed.** The manager belongs to exactly one emulator. | Not needed |
| Matches keyboard precedent | No | **Yes** (`debugkeyboardmanager.cpp:78-81`) | No |

**Decision: B.** It follows the keyboard precedent, gives synchronous answers, and is exactly
deterministic in the recommended *pause → inject → run_frames* workflow.

**The thread-safety cost of B, and the fix.** The Qt front end keeps posting to MessageCenter,
so the MessageCenter worker (Qt input) and an automation thread can both run
`Mouse::Move`'s `_x = _x + dx` at the same moment. One of the two updates can be lost.
Worked example: X = 31, Qt moves +3 and automation moves +10 at the same instant. Both read
31. One writes 34, the other writes 41. The expected result is 44.

Fix inside `Mouse`, as part of this work: keep `_x`, `_y`, `_buttons` and `_wheel` as
`std::atomic<uint8_t>`, and make `Move` / `SetWheel` a compare-and-swap loop:

```cpp
void Mouse::Move(int dx, int dy)
{
    uint8_t old = _x.load(std::memory_order_relaxed);
    while (!_x.compare_exchange_weak(old, static_cast<uint8_t>(old + dx), std::memory_order_relaxed)) {}
    old = _y.load(std::memory_order_relaxed);
    while (!_y.compare_exchange_weak(old, static_cast<uint8_t>(old + dy), std::memory_order_relaxed)) {}
}
```

The Z80 reading a port is a single atomic load, so nothing changes on the hot path in
practice. TTD serialisation ([design §6.1](design.md#61-device-state)) then loads each value
once into the packed state blob.

**What B does not make deterministic.** If the emulator is *running*, the change lands at
whatever t-state the Z80 has reached. The keyboard has the same property. Docs in every
module state the rule: *for reproducible scripts, pause first, inject, then `run_frames`.*

#### 4.1.4 TTD hook point

> *Superseded, see §0.* The journal is real (no stubs), desktop input goes through the funnel,
> and `SetCounters` is journalled as `MouseCounters` instead of being refused.

Every state-changing path goes through one private helper per kind, in this order. The
immediate methods and `OnFrame`'s click release both use them, which fixes weak spot 1.

```
Guard()                      -> ReplayActive?  refuse (status ReplayActive)
if pTimeTravelManager && IsRecording():
    Journal{Move|Buttons|Wheel}(...)   // BEFORE applying, same as debugkeyboardmanager.cpp:66-73
pMouse->{Move|SetButtons|SetWheel}(...)
```

- The journal calls stay **empty stubs** until the journal task
  ([design §6.2](design.md#62-input-journal-the-real-work), option (a) discriminated union)
  adds something like `TimeTravelManager::RecordMouseEvent(kind, a, b)`. The stubs mark the
  exact line that task edits.
- Until then, a recording made while automation moves the mouse **will diverge on replay**.
  `GetState().journalSupported = false` reports this, WebAPI/MCP status shows
  `"ttd_journal":"unsupported"`, and the manager logs one warning per recording session
  the first time mouse input arrives while `IsRecording()`.
- **Replay guard differs from keyboard on purpose.** Keyboard drops input silently and the
  WebAPI still answers 200. Mouse returns `ReplayActive`, which becomes HTTP 409, so a
  script knows its click did nothing.
- Frame batching of motion ([design §6.2](design.md#62-input-journal-the-real-work), "at
  most one movement record per emulated frame") is the journal's concern. Automation calls
  are rare compared with a 1000 Hz host mouse, so the manager does not batch.
- **Qt input and the funnel** (weak spot 3): [design §5.7](design.md#57-headless-injection)
  wants the UI to go through the funnel too. The smallest change: make `Mouse::OnMouseMove/Button/Wheel`
  (`mouse.cpp:113-129`) call `pDebugManager->GetMouseManager()->Move(...)` etc. when a
  manager exists. Qt then keeps its MessageCenter transport and multi-instance routing, and
  still gets guarded and journalled. That is why the atomic fix in §4.1.3 is required, not
  optional. Proposed as part of the journal task, see Q3.

#### 4.1.5 Click semantics (worked example)

`OnFrame` runs once at the end of every emulated frame (§3.1 "Frame pump").
`Click(Left, 2)` on a **paused** Pentagon:

| Step | Buttons register (#FADF low 3 bits) | Pending |
|---|---|---|
| `Click(Left, 2)` returns | `110` (left pressed, mask `0xFE`) | left, 2 frames |
| `run_frames 1`: frame 1 runs with left held, then `OnFrame` | `110` | left, 1 |
| `run_frames 1`: frame 2 runs with left held, then `OnFrame` releases | `111` | none |

So the machine sees the button held for exactly **2 full frames**, which is 40.96 ms on a
Pentagon (frame = 20480 µs). If the emulator is *running*, the first frame is partial: hold
time is 1 to 2 frames, depending on where in the frame the call lands.

Rules:
- A new `Click` while one is pending replaces it. The old button is released first, then
  the new button is pressed.
- `PressButton`, `ReleaseButton`, `SetPressedButtons` or `ReleaseAllButtons` on the pending
  button **cancels the pending release**. The explicit call wins.
- `holdFrames` = 0 is invalid (400). A 0-frame click would never be visible.

### 4.2 Shared command semantics

#### 4.2.1 Units and directions (identical in every module)

| Parameter | Unit | Sign | Valid range per call | Effect |
|---|---|---|---|---|
| `dx` | emulated pixels | + = right | −127 … +127 | X counter += dx (wraps mod 256) |
| `dy` | emulated pixels | + = **up** | −127 … +127 | Y counter += dy (wraps mod 256) |
| `steps` (wheel) | notches | + = away from user | −7 … +7, not 0 | wheel nibble += steps (wraps mod 16) |
| `button` | name | — | `left`/`l`, `right`/`r`, `middle`/`m` (case-insensitive) | — |
| `frames` (click) | emulated frames | — | 1 … 65535, default 2 | hold time |
| `x`, `y` (set counters) | raw counter | — | 0 … 255 | counter = value |

**Y is "up = positive", with no flip.** The Qt layer negates screen Y (`mousemanager.cpp:264-265`).
Automation passes `dy` straight through. Worked example: Y = 85, `move 0 5` gives Y = 90,
and the cursor in software that follows the convention moves **up** 5 pixels. The convention
itself still needs checking against real software ([design §4.2](design.md#42-state-and-register-semantics)).
If it turns out inverted, fix it in both the Qt layer and the docs, never in the automation layer alone.

#### 4.2.2 Why the ±127 and ±7 limits (reject, do not clamp)

The machine does not see the move. It sees the counter change between two of its own reads,
which it computes as `new − old` modulo 256. A jump over 127 therefore looks like a move
the *other way*. Worked example: X = 31, `move 200 0` gives X = 231. The program computes
231 − 31 = 200, which it reads as the signed byte **−56**, so the cursor jumps **left**.
The wheel nibble has the same problem at ±8.

Clamping would hide this. Rejecting with a clear message (`"dx=200 out of range -127..127;
split into several moves with run_frames between them"`) tells the caller what to do. The
limit applies to **one call**. How many pixels a program can take per read depends on how
often it polls, so a caller making a long move should run at least one frame between
chunks. Open question Q5 asks whether to add a built-in "glide over N frames".

#### 4.2.3 Presence and whether the ports are visible

Injection **changes the counters even when the device is absent** (`Mouse::Move` does not
check `_present`: `mouse.cpp:75-79`). The call succeeds, and the response carries
`present:false` plus a warning `"mouse not present: guest reads 0xFF"`. Nothing is lost, and
the caller learns why the machine does not react.

A 48K, 128K or +3 machine answers nothing on these ports today (§2.1). The machine will
never see the input, but the counters still change. Adding a decoder capability query so
status can say `"ports_decoded": false` is open question Q4.

> *As built:* the warning text is `mouse not present: guest reads floating bus on the mouse ports`
> (absent means the decoders do not claim the ports at all). 48K, 128K, +3 and Profi now decode
> the mouse too; Q4 remains open only for TR-DOS and peripheral-owned addresses (§0.4).

#### 4.2.4 Emulator targeting

| Module | Targets |
|---|---|
| WebAPI | Path `{id}`, same as keyboard. No active-emulator variant in v1 (Q2). |
| CLI | Selected emulator (`GetSelectedEmulator(session)`), same as `key` |
| Python | The `Emulator` object the method is called on |
| Lua | `effectiveEmulator()`: bound instance, or the selected one (`lua_emulator.h:57-71`) |
| MCP | `target` = id or `"auto"` |

Automation never posts with an empty `targetId` and never broadcasts. With delivery option B
there is no `targetId` at all.

#### 4.2.5 Timing: how a script waits

Every call returns after the change is applied, but **the machine only reacts when its
program next reads the port and redraws**. Recommended patterns:

```
# Reproducible (tests, TTD, agents verifying a result)
pause
mouse move 10 -4
mouse click left 2
run_frames 3        # 2 frames held + 1 frame for the program to act on the release
capture / inspect

# Live (demo, rough automation) - no pause
mouse move 10 -4
sleep ~40 ms        # about 2 frames at 50 Hz
mouse click left
```

`run_frames` is `POST /api/v1/emulator/{id}/run_frames {"count":N}` (`emulator_api.h:246`,
`debug_api.cpp:690-697`). It pauses a running emulator first (`emulator.cpp:2250-2254`).
The MCP mouse tool does **not** run frames implicitly, because changing run state behind an
agent's back is surprising. Agents call `control_execution run_frames` themselves.

#### 4.2.6 Debug op: set counters

`SetCounters(x, y)` writes the raw X/Y values. Uses:
- Putting the device into a known state before a test, e.g. back to 31/85 without a machine reset.
- Checking that a program copes with wrap-around.
- Reproducing the "equal axes means no mouse" detection
  ([hardware-reference §7](hardware-reference.md#7-presence-detection)): `set 40 40`.

It is **not** "move the cursor to a screen position" (§4.2.7). ~~While TTD is recording it
returns `Unsupported` (HTTP 409), because the journal cannot yet represent an absolute
write. This is lifted when the journal task adds it (Q6).~~ *As built:* allowed while TTD
records; journalled as `TTDInputKind::MouseCounters` (Q6, §0.2).

#### 4.2.7 Rejected: absolute "move pointer to (x, y)"

Programs such as Art Studio keep their own cursor coordinates and add counter changes to
them, clipping at the screen edges. The emulator does not know where that variable lives,
where the program has clipped, or how it scales the counter (some double it). An
absolute-move command could only guess, and a wrong guess is worse than no command.

What an agent can do instead is a closed loop: read the cursor position (from an OCR or
screenshot capture, or from a known RAM address the agent has found), `move` by the
difference, `run_frames 1`, check again. §4.8.3 shows this with numbers. If a program's
cursor variable is known, a later **per-program** helper could do this loop in one call.
That is out of scope here.

#### 4.2.8 Presence and enable are not input commands

`present` shows up read-only in every status response. Setting it belongs to the config
`Mouse=` key and the planned `kKempstonMouse` feature flag
([design §7.4-§7.5](design.md#74-mouse--type-selector)). Neither exists yet: `featuremanager.h`
has no mouse entry, and `config.cpp` does not parse the mouse fields. When it lands it is
reached through the existing settings/features routes. An input-route toggle would also be
overwritten by `Mouse::Reset` forcing `_present = true` (`mouse.cpp:46`).

> *As built:* both exist now — `[INPUT] Mouse=` (`config.cpp:312-349`) and feature
> `kempstonmouse` (`featuremanager.h:37`). `Mouse::Reset` keeps the fitting instead of forcing
> it. Presence is still not an input command; use the features route or the config.

---

### 4.3 `command-interface.md`

> *As built:* §11 was added with the adjustments in §0 (status ✅, TTD journal supported,
> `mouse set` allowed while recording, no-wheel warning, no `mouse m` alias). The draft below
> is the original proposal; the file itself is the reference. Similarly the `unsupported`
> journal strings in the §4.4.2 and §4.5 examples now read `supported`.

File: `docs/emulator/design/control-interfaces/command-interface.md`.

**Edit 1: new implemented section `### 11. Mouse Input Injection`**, inserted after
`### 10. Tape Control Commands` (starts `:2464`) and before `## Future Capabilities` (`:2504`).
Content to add:

````markdown
### 11. Mouse Input Injection

Drive the Kempston Mouse of the selected emulator. The device is **relative**: commands
change its X/Y counters, and the running program computes its own cursor position from them.
Values are whole emulated pixels, independent of window size or DPI.

**Units:** `dx` + = right, `dy` + = up, each −127…127 per call. Wheel `steps` + = away
from you, −7…7. Buttons `left|right|middle` (or `l|r|m`). Values out of range are rejected,
not clamped: a jump over 127 is read by the program as a move the other way.

**Timing:** each command is applied before it returns. For reproducible results, pause,
inject, then `run_frames N`. `click` holds the button for N emulated frames (default 2) and
releases it at a frame end.

| Command | Aliases | Arguments | Description | Status |
| :--- | :--- | :--- | :--- | :--- |
| `mouse move` | `mouse m` | `<dx> <dy>` | Add `dx`/`dy` to the X/Y counters (8-bit wrap). | 🔧 In Progress |
| `mouse press` | | `<button>` | Press and hold a button. | 🔧 In Progress |
| `mouse release` | | `<button>` | Release a button. | 🔧 In Progress |
| `mouse click` | | `<button> [frames]` | Press, hold `frames` (1–65535, default 2), auto-release. | 🔧 In Progress |
| `mouse buttons` | | `<none\|left,right,middle…>` | Set the exact set of pressed buttons. | 🔧 In Progress |
| `mouse wheel` | | `<steps>` | Scroll by −7…7 notches (4-bit wrap). | 🔧 In Progress |
| `mouse clear` | `mouse release_all` | — | Release all buttons, cancel a pending click. | 🔧 In Progress |
| `mouse status` | `mouse info` | — | Counters, buttons, wheel, presence, port bytes, pending click. | 🔧 In Progress |
| `mouse set` | | `<x> <y>` | Debug: write raw counters (0–255). Refused while TTD records. | 🔧 In Progress |

**Worked example** (Pentagon, reset state X=31 Y=85, nothing pressed):
```bash
pause
mouse move 10 -5        # X=41 Y=80
mouse press left        # #FADF = 0x0E
mouse wheel 2           # #FADF = 0x2E
mouse release left      # #FADF = 0x2F
mouse click right 3     # right held for the next 3 frames
run_frames 4
mouse status
```

**Interface mapping:** WebAPI `/api/v1/emulator/{id}/mouse/*`, Python `emu.mouse_*()`,
Lua `mouse_*()`, MCP tool `mouse_input`. See [Kempston Mouse automation design](../../../inprogress/2026-09-12-kempston-mouse/automation-interfaces.md).

**TTD:** mouse input is not yet journalled. A recording taken while injecting mouse input
diverges on replay. During replay, mouse commands are refused.
````

**Edit 2: *Future Capabilities → 3. Input Injection & Automation*** (`:2613-2630`). Remove
the two `mouse …` rows (`:2627-2628`) and add a pointer line: "Mouse: implemented — see §11."
The `mouse move <x> <y>` row reads as an absolute position, which §4.2.7 rejects.

**Edit 3, optional housekeeping:** the implemented keyboard commands are still listed only as
"Planned" in the same table (`:2619-2623`). They should get their own §12 with the same
shape. That is flagged here but outside this task.

Also update the per-interface docs in the same folder: `webapi-interface.md` (new section
after `### 9. Keyboard Inputjection`, `:809`), `python-interface.md` and `lua-interface.md`
(new "Mouse input" subsection each), and `core/automation/mcp/README.md` (tool table
`:67-77`).

---

### 4.4 WebAPI

#### 4.4.1 Routes

New file `core/automation/webapi/src/api/mouse_api.cpp`. Register the routes in
`emulator_api.h` as a new `// region Mouse Injection (implementation: api/mouse_api.cpp)`
directly after the keyboard region (`:338-354`), with declarations after `:1064`.

| Method | Path | Handler | Body |
|---|---|---|---|
| POST | `/api/v1/emulator/{id}/mouse/move` | `mouseMove` | `{"dx":int, "dy":int}` (either may be omitted = 0, at least one present) |
| POST | `/api/v1/emulator/{id}/mouse/press` | `mousePress` | `{"button":"left"}` |
| POST | `/api/v1/emulator/{id}/mouse/release` | `mouseRelease` | `{"button":"left"}` |
| POST | `/api/v1/emulator/{id}/mouse/click` | `mouseClick` | `{"button":"left", "frames":2}` |
| POST | `/api/v1/emulator/{id}/mouse/buttons` | `mouseButtons` | `{"pressed":["left","middle"]}` (`[]` = none) |
| POST | `/api/v1/emulator/{id}/mouse/wheel` | `mouseWheel` | `{"steps":int}` |
| POST | `/api/v1/emulator/{id}/mouse/release_all` | `mouseReleaseAll` | — |
| POST | `/api/v1/emulator/{id}/mouse/counters` | `mouseSetCounters` | `{"x":0..255, "y":0..255}` |
| GET | `/api/v1/emulator/{id}/mouse/status` | `mouseStatus` | — |
| GET | `/api/v1/emulator/{id}/mouse/buttons` | `mouseButtonList` | — (mirrors `keyboard/keys`) |

Naming follows keyboard: verb segments, snake_case (`release_all`), `status` for the query.
Press and release are separate routes, as keyboard has them (`keyboard/press`,
`keyboard/release`), not a single `/button {action}`.

No active-emulator routes (Q2), because keyboard has none and MCP `target:"auto"` already
covers the one-instance case.

#### 4.4.2 Responses

Every successful POST echoes its input, **and returns the resulting state**. That is cheap
with synchronous delivery, and it saves the caller a status round trip.

```jsonc
// POST /api/v1/emulator/7f3a…/mouse/move   {"dx":10,"dy":-5}   (from reset state)
200
{
  "success": true,
  "dx": 10,
  "dy": -5,
  "message": "Mouse moved: dx=+10 dy=-5",
  "state": { "x": 41, "y": 80, "buttons": {"left":false,"right":false,"middle":false},
             "button_mask": 255, "wheel": 0, "present": true }
}
```

```jsonc
// GET /api/v1/emulator/7f3a…/mouse/status   (after press left, wheel +2)
200
{
  "emulator_id": "7f3a…",
  "available": true,
  "present": true,
  "x": 41,
  "y": 80,
  "buttons": { "left": true, "right": false, "middle": false },
  "button_mask": 254,                       // active-low internal value (0xFE)
  "wheel": 2,
  "ports": { "FADF": 46, "FBDF": 41, "FFDF": 80 },  // what IN returns: 0x2E = (2<<4)|0x08|0x06
  "pending_click": null,                    // or {"button":"left","frames_left":1}
  "ttd_journal": "unsupported"              // "supported" once the journal task lands
}
```

Port bytes are integers, the same style as the existing state endpoints. Hex keys name the
port. When absent, `ports` are all 255 and a `"warning"` string is added.

#### 4.4.3 Status codes and error bodies

Error body shape is unchanged from keyboard: `{"error":"<reason phrase>","message":"<detail>"}`,
with `addCorsHeaders` on every path (`keyboard_api.cpp:34-41`).

| Condition | Code | `message` example |
|---|---|---|
| Emulator id unknown | 404 | `Emulator with specified ID not found` (keyboard wording) |
| Manager missing / `pMouse` null | 500 | `Mouse manager not available` |
| Body missing or not JSON, required field missing | 400 | `Missing 'button' field in request body` |
| Wrong JSON type (`"dx":"ten"`, `"dx":1.5`) | 400 | `'dx' must be an integer` |
| Out of range | 400 | `dx=200 out of range -127..127; split into several moves with run_frames between them` |
| Unknown button name | 400 | `Unknown button 'foo'. Valid: left, right, middle (l, r, m)` |
| `move` with `dx`=`dy`=0, or `wheel` with `steps`=0 | 400 | `move requires a non-zero dx or dy` |
| TTD replay active | 409 | `TTD replay in progress; live mouse input refused` |
| `counters` while TTD recording | 409 | `Cannot set counters while TTD is recording` |

Unlike keyboard, an unknown name really returns 400, matching what the keyboard OpenAPI
already promises (`openapi_keyboard.inc:78`). Fixing keyboard's 200-on-unknown-key is noted
as a follow-up, not part of this task.

Handlers should parse and validate through one small helper (`ParseIntField(json, "dx", -127, 127, out, err)`)
instead of repeating the keyboard file's copy-pasted blocks.

#### 4.4.4 OpenAPI

- New `core/automation/webapi/src/openapi/openapi_mouse.inc`, included after
  `openapi_keyboard.inc` in `openapi_spec.cpp:151`.
- Tag `"Mouse Injection"`. Use one `addMouseIdParam(path, method)` lambda, the same way as
  `openapi_keyboard.inc:2-8`.
- For each route: `summary`, a `description` containing the units sentence from §4.2.1,
  `requestBody` with `type`, `minimum`/`maximum`, `enum: ["left","right","middle","l","r","m"]`
  for `button`, `default: 2` for `frames`, and responses `200/400/404/409/500`.
- Write the summaries with the words agents will search for. `search_api` scores path
  segments, then summary, description and tags (`mcp-router.cpp:212-250`). Suggested:
  "Move the Kempston mouse (relative)", "Click a mouse button", "Scroll the mouse wheel",
  "Get mouse state".
- Add a `MouseStatus` schema to `openapi_schemas.inc`.
- Update the file table and counts in `OPENAPI_MAINTENANCE.md`, and run
  `python3 tools/verification/webapi/verify_openapi_coverage.py --strict`.

Worked request/response pairs:

```bash
curl -X POST localhost:8090/api/v1/emulator/$ID/mouse/click  -H 'Content-Type: application/json' -d '{"button":"left","frames":3}'
# 200 {"success":true,"button":"left","frames":3,"message":"Mouse click: left for 3 frames","state":{…"buttons":{"left":true,…}}}

curl -X POST localhost:8090/api/v1/emulator/$ID/mouse/wheel  -H 'Content-Type: application/json' -d '{"steps":-9}'
# 400 {"error":"Bad Request","message":"steps=-9 out of range -7..7"}

curl -X POST localhost:8090/api/v1/emulator/$ID/mouse/buttons -H 'Content-Type: application/json' -d '{"pressed":[]}'
# 200 {"success":true,"pressed":[],"message":"Mouse buttons set: none","state":{…"button_mask":255…}}
```

---

### 4.5 CLI

New `core/automation/cli/src/commands/cli-processor-mouse.cpp`. Register `{"mouse", &CLIProcessor::HandleMouse}`
next to `key` (`cli-processor.cpp:204-206`), declare the handlers next to the keyboard ones
(`cli-processor.h:222-233`), and add a "Mouse Injection:" block to global help after
`cli-processor.cpp:663`.

Structure mirrors `HandleKey` (`cli-processor-keyboard.cpp:13-82`): get the selected
emulator, lower-case the subcommand, dispatch, and print `Error: …` on failure.

```
Usage: mouse <subcommand> [args]

Subcommands:
  move <dx> <dy>          - Move by dx,dy emulated pixels (+x right, +y up; -127..127)
  press <button>          - Press and hold a button (left|right|middle, or l|r|m)
  release <button>        - Release a button
  click <button> [frames] - Press, hold for frames (default 2), release
  buttons <none|b1,b2..>  - Set exactly which buttons are pressed
  wheel <steps>           - Scroll wheel -7..7 (+ = away from you)
  clear                   - Release all buttons, cancel pending click
  status                  - Show counters, buttons, wheel, port values
  set <x> <y>             - Debug: write raw X/Y counters (0..255)

The mouse is relative: programs track their own cursor from counter changes.
For reproducible results: pause, inject, then 'run_frames N'.

Examples:
  mouse move 10 -5        - 10 right, 5 down
  mouse click left        - Click left button (2 frames)
  mouse click right 5     - Hold right button for 5 frames
  mouse buttons left,middle
  mouse wheel -1          - One notch towards you
```

Output lines (one line per success, with the resulting state so a human sees the effect):

```
> mouse move 10 -5
Moved: dx=+10 dy=-5 -> X=41 Y=80
> mouse press l
Pressed: left -> buttons=L-- (#FADF=0x0E)
> mouse move 300 0
Error: dx=300 out of range -127..127; split into several moves with run_frames between them
> mouse status
Kempston Mouse [present]
  X=41 (0x29)  Y=80 (0x50)
  Buttons: left=down right=up middle=up  (mask 0xFE)
  Wheel: 2
  Ports: #FADF=0x2E #FBDF=0x29 #FFDF=0x50
  Pending click: none
  TTD journal: unsupported (recordings with mouse input will diverge)
```

Integer parsing uses `std::stoi` inside `try/catch`, as `HandleKeyTap` does
(`cli-processor-keyboard.cpp:193-205`), and additionally rejects trailing junk (`"10px"`).

---

### 4.6 Python

Add to the `Emulator` class in `core/automation/python/src/emulator/python_emulator.h`,
directly after `key_list` (`:1841-1848`):

```python
emu.mouse_move(dx: int, dy: int) -> dict                 # returns state dict
emu.mouse_press(button: str) -> dict
emu.mouse_release(button: str) -> dict
emu.mouse_click(button: str, frames: int = 2) -> dict
emu.mouse_buttons(pressed: list[str]) -> dict            # [] = none
emu.mouse_wheel(steps: int) -> dict
emu.mouse_release_all() -> dict
emu.mouse_set_counters(x: int, y: int) -> dict
emu.mouse_status() -> dict                               # same keys as WebAPI GET status
emu.mouse_click_pending() -> bool
emu.mouse_button_names() -> list[str]                    # static-like, mirrors key_list
```

Errors: keyboard bindings return `bool` (`:1783-1839`), which hides mistakes. Mouse bindings
**raise**:

| Status | Python exception |
|---|---|
| `InvalidArgument` | `ValueError(message)` |
| `ReplayActive`, `Unsupported` | `RuntimeError(message)` |
| `NoDevice` / no manager | `RuntimeError("mouse manager not available")` |

Returning the state dict matches the WebAPI `state` object, so the same key names work everywhere.

```python
emu = unreal.emu_get_selected()
emu.pause()
st = emu.mouse_move(10, -5)          # {'x': 41, 'y': 80, 'buttons': {...}, 'button_mask': 255, 'wheel': 0, 'present': True}
emu.mouse_click("left", frames=2)
emu.run_frames(3)                    # python_emulator.h:468-470
assert emu.mouse_status()["buttons"]["left"] is False

try:
    emu.mouse_move(200, 0)
except ValueError as e:
    print(e)                         # dx=200 out of range -127..127; split into several moves ...
```

---

### 4.7 Lua

Add global functions to `core/automation/lua/src/emulator/lua_emulator.h`, in a new
`// Mouse injection` block. Lua has no keyboard bindings to sit next to (§3.2). All of them
resolve the instance with **`effectiveEmulator()`** (`:57-71`), not `_emulator`, so a script
started from the WebAPI interpreter targets the selected emulator. That is the newer pattern
(`:228, 253, 267, 447`).

```lua
mouse_move(dx, dy)                 --> state_table | nil, err
mouse_press(button)                --> state_table | nil, err
mouse_release(button)              --> state_table | nil, err
mouse_click(button [, frames=2])   --> state_table | nil, err
mouse_buttons({"left","middle"})   --> state_table | nil, err   ({} = none)
mouse_wheel(steps)                 --> state_table | nil, err
mouse_release_all()                --> state_table | nil, err
mouse_set_counters(x, y)           --> state_table | nil, err
mouse_status()                     --> table (same keys as WebAPI status) | nil, err
mouse_button_names()               --> {"left","right","middle"}
```

Errors use the Lua idiom of returning `nil, "message"`, so the call does not raise and can be
wrapped in `assert(...)`:

```lua
pause()
local st = assert(mouse_move(10, -5))
print(st.x, st.y)                  -- 41   80
assert(mouse_click("left", 2))
run_frames(3)                      -- lua_emulator.h:557-560
print(mouse_status().buttons.left) -- false

local ok, err = mouse_wheel(12)
print(ok, err)                     -- nil   steps=12 out of range -7..7
```

A non-integer number (`mouse_move(1.5, 0)`) is an `InvalidArgument`. Bind the parameters as
`sol::object` and check `is<int>` / integral value, so sol2 does not truncate silently.

---

### 4.8 MCP

#### 4.8.1 What happens with no MCP code at all

Once `openapi_mouse.inc` exists, `search_api {"query":"mouse click"}` finds
`POST /api/v1/emulator/{id}/mouse/click` (the path segments `mouse` and `click` both score,
`mcp-router.cpp:212-250`), and `invoke_api` calls it with `{id}` filled in (`:384`). This is
zero-cost and always in sync with the WebAPI.

#### 4.8.2 Proposal: add a `mouse_input` smart tool, do not extend `type_input`

| Option | Pros | Cons |
|---|---|---|
| Router only | No code, always in sync | Two tool calls per action (search, then invoke). Agents clicking through a GUI program repeat this constantly. Nothing in the `initialize` briefing tells them mouse input exists. |
| Extend `type_input` | No new tool | Its name and description say keyboard (`mcp-tools.cpp:926-927`). Mixing `dx/dy/button/steps` into a 9-action keyboard schema confuses both. |
| **New `mouse_input`** | Clear name, compact schema, one call per action, listed in the server briefing | Adds one tool to the list, a few hundred tokens of schema (estimate, not measured). Two tests assert the tool count and must change. |

**Recommendation: new `mouse_input` tool**, in `mcp-tools.cpp` next to `RegisterTypeInput`
(`:885-1007`), registered in `BuildFullRegistry` after it (`:1020`).

Schema:

```jsonc
{
  "type": "object",
  "required": ["action"],
  "properties": {
    "action":  { "type": "string",
                 "enum": ["move", "press", "release", "click", "buttons", "wheel", "release_all", "status"],
                 "description": "Kempston mouse (relative device). 'move' shifts counters by dx/dy emulated pixels; 'click' presses a button for N frames. Input is applied immediately; call control_execution run_frames to let the program react." },
    "target":  { "type": "string", "default": "auto" },
    "dx":      { "type": "integer", "minimum": -127, "maximum": 127, "description": "+ = right (move; optional pre-move for click)" },
    "dy":      { "type": "integer", "minimum": -127, "maximum": 127, "description": "+ = UP (move; optional pre-move for click)" },
    "button":  { "type": "string", "enum": ["left", "right", "middle"] },
    "pressed": { "type": "array", "items": { "type": "string", "enum": ["left", "right", "middle"] },
                 "description": "Exact pressed set for 'buttons' ([] = none)" },
    "frames":  { "type": "integer", "minimum": 1, "default": 2, "description": "Hold time for 'click'" },
    "steps":   { "type": "integer", "minimum": -7, "maximum": 7, "description": "Wheel notches, + = away from user" }
  }
}
```

Action → WebAPI mapping (through `ResolveAndForward`, `mcp-tool-utils.h:222-234`):

| action | Required | Calls |
|---|---|---|
| `move` | `dx` or `dy` | `POST /mouse/move` |
| `press` / `release` | `button` | `POST /mouse/press` / `/mouse/release` |
| `click` | `button` | if `dx`/`dy` given: `POST /mouse/move`, **then** `POST /mouse/click` (sequenced with `RunSeries`, `mcp-tool-utils.h:126`). Stops at the first failure. |
| `buttons` | `pressed` | `POST /mouse/buttons` |
| `wheel` | `steps` | `POST /mouse/wheel` |
| `release_all` | — | `POST /mouse/release_all` |
| `status` | — | `GET /mouse/status` |

`counters` is deliberately not a `mouse_input` action. It is a debug op and stays reachable
through `invoke_api`. Missing arguments give `ToolResult::Error("<action> requires '<field>'")`,
as `type_input` does (`mcp-tools.cpp:951`). Range errors come back from the WebAPI 400
and are surfaced as `isError:true`.

Also update:
- `kServerInstructions` (`mcp-dispatcher.cpp:21-26`): add `mouse_input` to the core tool list.
- `core/automation/mcp/README.md` tool table (Phase 1 table `:67-77`).
- The optional resource text in `mcp-resources.cpp` is not needed. The tool description
  carries the units.

#### 4.8.3 Worked agent flow (closed loop)

Goal: click an "OK" icon in a paused art program. The agent reads the program's cursor at
screen (60, 100) and the icon at (92, 84). Screen Y grows downward, mouse `dy` + = up.

```jsonc
{"name":"mouse_input","arguments":{"action":"move","dx":32,"dy":16}}   // 92-60 = 32 right; 100-84 = 16 up
{"name":"control_execution","arguments":{"action":"run_frames","count":2}}
{"name":"capture_media","arguments":{"action":"screenshot"}}           // verify: cursor at (92,84)? If the program doubles motion, it lands at (124,68): correct with dx=-16, dy=-8
{"name":"mouse_input","arguments":{"action":"click","button":"left","frames":2}}
{"name":"control_execution","arguments":{"action":"run_frames","count":3}}
```

---

### 4.9 Validation summary (all modules)

| Rule | Check | Where enforced |
|---|---|---|
| `dx`,`dy` integers in −127…127, not both 0 | reject | `DebugMouseManager` (single source); MCP schema min/max as a hint |
| `steps` integer in −7…7, not 0 | reject | manager |
| `frames` integer 1…65535 | reject | manager (WebAPI/CLI parse to `uint32` first so 70000 is rejected, not wrapped by `uint16`) |
| `x`,`y` integers 0…255 | reject | manager |
| Button name in `left/right/middle/l/r/m`, case-insensitive | reject, list valid names | `ResolveButtonName` |
| `pressed` array: each name valid, duplicates allowed and ignored | reject on first bad name | manager |
| Non-integer JSON / Lua number / Python float | reject | front end (type), manager (range) |
| TTD replay active | refuse (409 / RuntimeError / nil,err / isError) | manager `Guard()` |
| Device absent | **accept** and warn | manager adds a warning to the result |

---

## 5. Test plan

Keyboard precedent for each layer, and the mouse test that mirrors it. Every test must be
mutation-checked (break the behaviour, watch it fail), per [design §8](design.md#8-test-architecture).

### 5.1 Core unit: `core/tests/debugger/mouse/debugmousemanager_test.cpp`

Mirrors `core/tests/debugger/keyboard_test.cpp` (`DebugKeyboardManager_test`, `:45-430`).
Use a real `EmulatorContext` with `Mouse`, no running CPU.

| Test | Asserts |
|---|---|
| `ResolveButtonName_AllNamesAndAliases` | `left/L/l`, `right/r`, `middle/m` resolve; `""`, `"foo"`, `"lef"` give `nullopt` (cf. `keyboard_test.cpp:107-114`) |
| `Move_UpdatesCountersSynchronously` | From 31/85, `Move(10,-5)` → `GetX()==41`, `GetY()==80` **immediately**, no wait |
| `Move_WrapsAt8Bits` | 31 + 127 + 127 = 285 → 29 (two calls) |
| `Move_RejectsOutOfRange` | `Move(128,0)` → `InvalidArgument`, counters unchanged. `Move(0,0)` → `InvalidArgument`. |
| `PressRelease_ActiveLowMask` | press L → 0xFE; press M → 0xFA; release L → 0xFB |
| `SetPressedButtons_ExactSet` | `{R}` → 0xFD; `{}` → 0xFF |
| `Wheel_NibbleWrapAndRange` | +2 → 2; −3 → 15; `Wheel(8)` rejected |
| `Click_ReleasesAfterNFrames` | `Click(L,2)`: pressed; `OnFrame()` → pressed; `OnFrame()` → released, `IsClickPending()==false` |
| `Click_ZeroFramesRejected` | status `InvalidArgument` |
| `Click_ExplicitReleaseCancelsPending` | `Click(L,5)`, `ReleaseButton(L)`, `PressButton(L)`, 5× `OnFrame` → still pressed |
| `Click_NewClickReplacesPending` | `Click(L,5)`, `Click(R,1)` → L released, R pressed |
| `ReplayActive_RefusesAndLeavesStateUntouched` | `ttdReplayActive=true` → every mutator returns `ReplayActive`, snapshot unchanged |
| `Absent_AcceptsAndWarns` | `SetPresent(false)`; `Move(1,0)` ok with a warning; `portX==0xFF`, `x==32` |
| `GetState_PortBytesMatchReadRegister` | after press L + wheel 2: `portButtons==0x2E` |
| `SetCounters_RefusedWhileRecording` | recording on → `Unsupported` |
| `ClickRelease_UsesJournalledPath` | a test hook counts `JournalButtons` calls: `Click` + release gives 2 (guards weak spot 1) |

### 5.2 Core device: extend `core/tests/emulator/io/mouse/mouse_test.cpp`

| Test | Asserts |
|---|---|
| `Move_ConcurrentWritersLoseNothing` | 2 threads × 100 000 × `Move(1,0)` → X == (31 + 200 000) mod 256 = 95. It fails against today's non-atomic `Move`, which proves the mutation check. |

### 5.3 Core integration: `core/tests/debugger/mouse/mouse_injection_integration_test.cpp`

Mirrors `core/tests/debugger/keyboard_integration_test.cpp` (`:306-341`, real booted
instance) and `mouse_messagecenter_test.cpp` (instance creation `:46-53`, port read `:62-65`,
parameterised over PENTAGON/SCORPION/PROFSCORP `:229-231`).

| Test | Asserts |
|---|---|
| `PausedMove_VisibleOnPortsWithoutWaiting` | pause; `Move(5,-3)`; `DecodePortIn(0xFBDF)==36` on the next line, with no `TestWait` |
| `ClickHeldExactlyNFramesUnderRunNFrames` | pause; `Click(L,2)`; `RunNFrames(1)` → pressed; `RunNFrames(1)` → released (proves the `OnFrameEnd` pump in the manual stepping path, `emulator.cpp:2310`) |
| `GuestProgramSeesCounters` | poke a Z80 loop `IN A,(#FBDF)` / `LD (#8000),A` / `JR` at #8000; set PC; `Move(7,0)`; `RunNFrames(1)`; `mem[#8000]==38`. Exercises the real `IN` path, not the decoder shortcut. |
| `TwoInstancesIsolated` | move instance A; instance B's counters stay 31/85 |
| `MessageCenterAndDirectCompose` | post `MouseEvent::Move(3,0,id)` and call manager `Move(10,0)`; wait for MC delivery; X == 44 |
| `TrDosPortsHideMouse` | set `CF_DOSPORTS`; `Move` succeeds; `DecodePortIn(0xFBDF)` ≠ counter (decoder gate `portdecoder_pentagon128.cpp:675`) |

### 5.4 TTD (after the journal task)

Mirror `core/tests/debugger/ttd/ttdinputjournal_test.cpp` (`:53-130`) and `ttdreplaymode_test.cpp`:
record while injecting move/click, seek back, replay, compare `TTDHashState`. Until the
journal exists, one test pins the current contract: `GetState().journalSupported == false`
and status JSON says `"unsupported"`. It is deleted by the journal task.

> *As built:* the journal landed with the mouse, so the "unsupported" contract test was never
> needed. Coverage is in `debugmousemanager_test.cpp`: `Recording_JournalsEveryMouseMutationBeforeApplying`,
> `HostMessageCenterInput_JournalledWhileRecording`, `JournalInject_AppliesMouseKinds`,
> `TTDSeek_RestoresMouseStateFromCheckpoint`, `ReplayActive_RefusesAndLeavesStateUntouched`.
> A full record → seek → replay → compare-hash test with live mouse motion was not found.

### 5.5 WebAPI

- **Coverage**: `verify_openapi_coverage.py --strict` must pass (`OPENAPI_MAINTENANCE.md:80-92`).
- **Live contract tests**: new `tools/verification/webapi/src/test_api_mouse.py`, using the
  `api_client` / `active_emulator` fixtures (`conftest.py:30-40`, which creates a PENTAGON)
  in the style of `test_api_state.py:5`. There is **no keyboard pytest to mirror**. That is
  a gap, not a pattern.
  Cases: move → status reflects it; each 400 row of §4.4.3; unknown id → 404; click with
  `frames:3` → `pending_click.frames_left==3` right after, then `run_frames 4` → `null`; CORS
  header present on a 400 response.

### 5.6 CLI

No CLI unit tests exist in `core/tests` (grep for `CLIProcessor` finds none). Options, in order of cost:
1. Keep the parsing and formatting as free functions (`ParseMouseArgs`, `FormatMouseState`)
   in the CLI source, and unit-test them in `core-tests` (pure strings, no socket).
2. A scripted smoke check in `tools/verification/` that sends `mouse move 10 -5` / `mouse status`
   over the CLI socket. Marked optional.

### 5.7 Python and Lua

No binding unit tests exist for keyboard either. Mirror `tools/verification/webapi/src/test_api_interpreter.py`
(`:5` Python, `:22` Lua), which runs code through the interpreter endpoint. Add
`test_mouse_python_bindings` / `test_mouse_lua_bindings`: move, status keys, `ValueError` /
`nil, err` on `mouse_move(200, 0)`, and that Lua targets the selected emulator when
nothing is bound.

### 5.8 MCP

In `core/tests/automation/mcp-tools-test.cpp`, mirroring `TypeInput_Type_PostsKeyboardTypeWithText`
(`:371-383`, `FakeApiCaller`):

| Test | Asserts |
|---|---|
| `MouseInput_Move_PostsDxDy` | `POST /api/v1/emulator/emu-1/mouse/move` body `{dx:10,dy:-5}` |
| `MouseInput_ClickWithPreMove_MovesThenClicks` | two calls in order: move, then click |
| `MouseInput_ClickWithPreMove_StopsOnMoveError` | move route returns 400 → no click call, `isError` |
| `MouseInput_Press_RequiresButton` | error result, no HTTP call |
| `MouseInput_Status_GetsStatus` | `GET …/mouse/status` |

In `mcp-dispatcher-test.cpp`, update the tool count `11u` → `12u` (`:212`) and add
`"mouse_input"` to the expected names (`:214-216`). Also update the header comment (`:9`).

---

## 6. Implementation order and checklist

Each step builds and passes tests by itself.

| # | Step | Files |
|---|---|---|
| 1 | Make `Mouse` counters atomic; add the concurrency test | `core/src/emulator/io/mouse/mouse.{h,cpp}`; `core/tests/emulator/io/mouse/mouse_test.cpp` |
| 2 | `DebugMouseManager` + result/state types + journal stubs + replay guard | `core/src/debugger/mouse/debugmousemanager.{h,cpp}` (new) |
| 3 | Own it in `DebugManager`; pump `OnFrame` | `core/src/debugger/debugmanager.{h,cpp}`; `core/src/emulator/mainloop.cpp` (after `:568`) |
| 4 | Core unit + integration tests | `core/tests/debugger/mouse/*.cpp` (new) |
| 5 | WebAPI handlers, routes, OpenAPI | `webapi/src/api/mouse_api.cpp` (new); `webapi/src/emulator_api.h`; `webapi/src/openapi/openapi_mouse.inc` (new); `webapi/src/openapi_spec.cpp`; `webapi/src/openapi/openapi_schemas.inc`; `webapi/OPENAPI_MAINTENANCE.md`; `webapi/CMakeLists.txt` (sources are listed explicitly: add beside `src/api/keyboard_api.cpp`, `:240`) |
| 6 | WebAPI pytest | `tools/verification/webapi/src/test_api_mouse.py` (new) |
| 7 | CLI command + help | `cli/src/commands/cli-processor-mouse.cpp` (new); `cli/include/cli-processor.h`; `cli/src/cli-processor.cpp`; `cli/CMakeLists.txt` (explicit list: add beside `cli-processor-keyboard.cpp`, `:34`) |
| 8 | Python bindings | `python/src/emulator/python_emulator.h` |
| 9 | Lua bindings | `lua/src/emulator/lua_emulator.h` |
| 10 | Binding tests via interpreter endpoint | `tools/verification/webapi/src/test_api_interpreter.py` |
| 11 | MCP tool + tests + briefing | `mcp/src/mcp-tools.cpp`; `mcp/src/mcp-dispatcher.cpp`; `core/tests/automation/mcp-tools-test.cpp`; `core/tests/automation/mcp-dispatcher-test.cpp`; `mcp/README.md` |
| 12 | Docs | `docs/emulator/design/control-interfaces/{command-interface,webapi-interface,python-interface,lua-interface,cli-interface}.md` |
| 13 | ~~*(Journal task, separate)*~~ Journal mouse input; route `Mouse::OnMouse*` through the manager; journal `SetCounters` — **done in this work** | `debugmousemanager.cpp`; `mouse.cpp`; `debugger/ttd/*` |

All 13 steps are done as of 2026-09-12 (test pass status not verified for this doc update).

Related defects found while researching. **All four were fixed** (checked 2026-09-12):
- ~~`Core::Reset` does not call `Mouse::Reset`~~ — it now calls `Reset()` and
  `ApplyConfiguration()` (`core.cpp:569-573`).
- ~~Keyboard WebAPI answers 200 for unknown key names~~ — now 400 (`keyboard_api.cpp`, `rejectUnknownKey`).
- ~~Keyboard timed ops bypass journalling and the replay guard; sequence state unlocked~~ —
  all matrix changes go through `DebugKeyboardManager::ApplyKey`; sequence state is behind a
  recursive mutex.
- ~~`design.md` cites stale `platform.h` lines~~ — the mouse config fields are at
  `platform.h:538-542` and design.md was updated.

---

## 7. Open questions

| # | Question | Proposed answer | Outcome (2026-09-12) |
|---|---|---|---|
| Q1 | Delivery: direct call (B) vs MessageCenter (A) vs frame-boundary queue (C)? | **B**, with atomic counters (§4.1.3). C is worth revisiting only if running-mode determinism matters more than synchronous answers. | **Decided: B.** Implemented. |
| Q2 | Add active-emulator routes (`/api/v1/emulator/mouse/*`, no `{id}`)? | **No** for v1. Keyboard has none, and MCP `auto` covers it. If added, use `getEmulatorWithGlobalSelection()` with the 404/400 split from `state_audio_api.cpp:837-860`. | **Decided: no.** |
| Q3 | Should Qt mouse input go through `DebugMouseManager` (so it gets guarded and journalled)? | **Yes, in the journal task**, by having `Mouse::OnMouse*` call the manager. Keyboard has the same gap (§3.1 weak spot 3), and fixing both together keeps them consistent. | **Decided: yes, done in this work**, keyboard included. |
| Q4 | Report whether the current model/state actually decodes the mouse ports (`ports_decoded`)? | Useful for agents on 48K/128K, where input silently does nothing. Needs a new `PortDecoder` capability query plus the `CF_DOSPORTS` gate. Proposed as a small follow-up. | **Open**, narrower: all models decode the mouse now; TR-DOS and peripheral-owned addresses can still hide it without status saying so. |
| Q5 | Built-in "glide" (spread a large move over N frames) to avoid the ±127 split? | Not in v1. It needs a queued motion state in `OnFrame` and more journal records. Revisit if agents often hit the limit. | **Open** (not in v1). |
| Q6 | `SetCounters` and TTD: refuse while recording, or journal as an absolute record? | Refuse now. The journal task decides whether an "absolute" record kind is worth adding. | **Decided: journal it** (`MouseCounters`); allowed while recording. |
| Q7 | ±127 per call: right limit, or ±63 to leave headroom for programs that scale motion ×2 internally? | ±127 is the hardware ambiguity limit. Scaling is the program's business. Keep ±127 unless real software shows otherwise. | **Kept ±127**; open only if software shows otherwise. |
| Q8 | Python errors: raise (proposed) vs return `False` like `key_*`? | Raise. Silent `False` hides mistakes. The inconsistency with `key_*` is accepted and documented. | **Decided: raise.** |
| Q9 | Should `click` in MCP optionally run frames afterwards (`settle_frames`)? | No. `RunNFrames` pauses a running machine (`emulator.cpp:2250-2254`), and an input tool should not change run state. | **Decided: no.** |
| Q10 | Is `dy` + = up correct for real software? | Carried over from [design §4.2](design.md#42-state-and-register-semantics). Verify with Art Studio or similar before documenting as final. | **Partly answered:** correct for the Scorpion ProfROM pointer. Still to check on Art Studio or similar. |
