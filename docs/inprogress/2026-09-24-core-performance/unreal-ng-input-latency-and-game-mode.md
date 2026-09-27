# unreal-ng: input latency analysis and "Game Mode" proposals

**Date:** 2026-09-24
**Code base:** `master` @ `4c49fb62`. File:line references below are to that commit.
**Measured baseline (your hardware key-to-photon tester):** unreal-ng ≈ **63 ms median**; MAME ≈ **20 ms**.

**Scope:** the full pipeline, from host input (USB → OS → Qt → core) through emulation scheduling, audio/video sync, presentation (Qt/OpenGL → compositor → display) and guest-side lag. It covers:
- code fixes that are free for every mode;
- a dedicated low-latency Game Mode;
- pacing strategies;
- run-ahead / rollback;
- frontend technology options, from Qt6 (current) through Qt6+QRhi, an SDL3 player client and native per-OS frontends.

**Tags used:**
- **[V]** verified in code;
- **[E]** estimate derived from code and timing arithmetic;
- **[?]** platform/driver behaviour that must be measured.

---

## 0. Executive summary

1. **The biggest single item is intentional: the A/V-sync video delay.**
   - By default every frame is shown **2 emulated frames late (≈41 ms)** so the picture lines up with the 40 ms audio ring. **[V]**
     - `Screen::CopyPresentedFramebuffer` serves slot `newest − delay` (`core/src/emulator/video/screen.cpp:982-991`).
     - Auto = 2 frames (`screen.cpp:780-787`, `screen.h:673`).
     - The ini key `[VIDEO] AVSyncDelayFrames` (default −1 = auto) is not set in any shipped config and has no UI.
   - Setting it to `0` should cut about **40 ms** from the measured 63 ms, putting unreal-ng close to MAME. **That is the first thing to verify on the tester (§3).**
2. **The remaining ~20–25 ms** is structural and common to most emulators [E]:
   - USB/OS delivery;
   - waiting for the next emulation burst (the frame is emulated in ~1–3 ms, then the thread sleeps ~17 ms);
   - the Qt paint path (`update()` → `requestUpdate` → `paintGL` on the GUI thread);
   - the vsync wait for unsynchronised 48.83 Hz content on a 60 Hz panel;
   - the compositor;
   - scanout.
3. **"Demo mode" and "game mode" have genuinely different goals.** The current design is right for demos: absolute-deadline 48.83 Hz clock, DRC-locked audio, constant A/V offset, "never let the presenter feed back into pacing" (`docs/inprogress/2026-08-17-audio-sync/audio-sync-design.md:267-281`). A Game Mode should flip these priorities:
   - no video delay;
   - a smaller or sub-frame audio buffer;
   - display-aware pacing (VRR present-on-ready, a 50/49 Hz display mode, or late-burst scheduling);
   - optionally run-ahead.
4. **Bugs and cheap wins on the input path, free for all modes:**
   - The MessageCenter worker has a **lost-wakeup race** (`wait_for(50 ms)` with no predicate), so an input or frame event can occasionally wait up to 50 ms (`core/src/3rdparty/message-center/messagecenter.cpp:166-170`). **[V]**
   - The keyboard matrix is a plain `uint8_t[8]` written by read-modify-write from two non-emulation threads (`keyboard.h:293`, `keyboard.cpp:139`, `165`), whereas `Mouse` already uses atomics. **[V]**
   - There is no host gamepad support, and Kempston is a stub returning `0x00` (`portdecoder_scorpion256.cpp:236-247`). **[V]**
   - `Info.plist` has no `LSApplicationCategoryType = public.app-category.games`, so **macOS Game Mode never engages** (which would also double Bluetooth controller polling). **[V]**
5. **Run-ahead / preemptive frames (RetroArch-style).** This is the only technique that can hide *guest* lag, i.e. games that read keys in the ISR and draw next frame. unreal-ng is unusually well positioned for it, with TTD serialisation of every peripheral and a sub-ms headless frame. **Prerequisite:** the idle-coprocessor gating from the performance review, because run-ahead multiplies per-frame cost.
6. **Frontend.** Qt `QOpenGLWindow` inside `createWindowContainer` gives little control over presentation: no present-mode choice, no frames-in-flight control, no vblank timestamps, no VRR control on macOS, and likely DWM composition on Windows.
   - **Recommendation:** keep Qt6 as the developer client and make the **planned SDL3 client the Game Mode flagship** (SDL_GPU present modes, VRR, display-mode switching to 50/49 Hz, gamepads, ns event timestamps). Share one `FramePresenter`/pacing policy layer so Qt can adopt the same policy later via QRhi.
   - A native Metal presenter can come later, reused from the iOS host work.

**Estimated result per tier.** Figures are for an ISR-polling game, measured to mid-screen on a 60 Hz panel. They are estimates to be confirmed with the tester.

| Tier | What changes | Est. key-to-photon |
|---|---|---|
| Today (demo mode) | — | ~63 ms (measured) |
| T0: `AVSyncDelayFrames=0` | config only | ~22–28 ms |
| T1: Game Mode (Qt) | + input fixes, render thread, no temporal blend, lower audio target | ~18–24 ms |
| T2: Game Mode (SDL3 player, VRR or 50/49 Hz display mode, late-burst) | + presentation control | ~10–16 ms |
| T3: + run-ahead 1 | hides one frame of guest lag | ~−20 ms more for games that respond "next frame"; can beat real hardware |

---

## 1. The pipeline today, stage by stage

### 1.1 Input half [V]

| # | Stage | Where | Delay (avg / worst) |
|---|---|---|---|
| I1 | USB keyboard polling (125 Hz typical, 1000 Hz gaming) | hardware | 4 / 8 ms at 125 Hz; 0.5 / 1 ms at 1 kHz |
| I2 | OS → Qt event on the **GUI thread** | `mainwindow.cpp:375-376` (event filter) → `devicescreenwrapper.cpp:156-169` → `DeviceScreenGLWindow::keyPressEvent` `devicescreenglwindow.cpp:589-637` | <1 ms, **but** blocked while the GUI thread is inside `paintGL`/swap (vsync) or doing UI work [?] |
| I3 | `MessageCenter::Post(MC_KEY_PRESSED, new KeyboardEvent)`: mutex queue, then a single worker thread | `devicescreenglwindow.cpp:609-610`; `eventqueue.cpp:380-392`; `messagecenter.cpp:152-172` | µs. **Worst case 50 ms** (lost wakeup), plus head-of-line blocking behind other topics |
| I4 | `Keyboard::OnKeyPressed` on the MessageCenter thread → `PressKey` → plain matrix RMW | `keyboard.cpp:399-454`, `:139` | 0 |
| I5 | Z80 sees it on the next `IN #FE` (no latch) | `portdecoder.cpp:1009-1013`, `keyboard.cpp:314-356` | 0 in emulated time |
| I6 | **Burst-then-sleep scheduling.** The frame is emulated in ~1–3 ms right after its deadline, then the thread sleeps. A key that arrives during the sleep is first seen by the next burst. | `mainloop.cpp:110` (`RunFrame`), `:236-245` (deadline, `WaitUntilPrecise`) | ~F/2 ≈ 10 ms / F ≈ 20.5 ms |
| I7 | Guest lag: most games read keys in the ISR at frame start and react in the same or the next frame; double-buffered games add one more | the program | 0–2 frames (0–41 ms) |

### 1.2 Output half [V]

| # | Stage | Where | Delay at 60 Hz (min / avg / max) |
|---|---|---|---|
| O1 | Frame end → `LatchFramebuffer` into a 4-slot ring | `mainloop.cpp:499-510`, `screen.cpp:918-937` | ~0.04 ms |
| O2 | Tape, FDC, **sound frame end**, recording and shm sync all run *before* the refresh notify | `mainloop.cpp:512-611` | 0.05 / 0.3 / 2 ms [E] |
| O3 | `Post(NC_VIDEO_FRAME_REFRESH)` → MessageCenter worker → `QMetaObject::invokeMethod(..., QueuedConnection)` | `mainloop.cpp:621-635`, `mainwindow.cpp:1680-1710` | µs. Worst case 50 ms (same race as I3) |
| O4 | GUI thread: `refresh()` → `update()` → Qt `requestUpdate` → `paintGL` | `devicescreenglwindow.cpp:238-245`, `:480` | 0 / 1–3 / 5+ ms [?] (Qt timer / display-link, per platform) |
| O5 | **A/V-sync delay line: `AVSyncDelayFrames` auto = 2** | `screen.cpp:780-787`, `:982-991` | **41 / 41 / 41 ms** |
| O6 | Texture upload + quad + optional CRT shader + optional **temporal blending** (`_frameHistory`, `devicescreenglwindow.cpp:436-454`) + HUD | `devicescreenglwindow.cpp:407-586` | 0.2 / 0.5 / 2 ms. Temporal blending adds perceived lag |
| O7 | `setSwapInterval(1)`, double buffer: the swap waits for vblank, and 48.83 Hz content is unsynchronised with 60 Hz | `devicescreenglwindow.cpp:174-181` | 0 / 8.3 / 16.7 ms |
| O8 | Compositor: WindowServer on macOS; DWM for a GL child HWND on Windows, likely even in borderless fullscreen | `devicescreenwrapper.cpp:24` (`createWindowContainer`), `mainwindow.cpp:1460-1461`, `:1531` | 0 / 8–17 / 17 ms [?] |
| O9 | Scanout to the photosensor position + panel response | display | ~8 ms mid-screen + 1–5 ms |

### 1.3 Reconciling with the 63 ms measurement [E]

Using averages: I1 4 + I6 10 + (ISR reaction within the same burst) 0 + O2–O4 2 + **O5 41** + O7 8 + O8 ~0–8 + O9 ~10 ≈ **75 ± 10 ms**.

- That is the same order as the 63 ms measured. The difference is probably a lower-than-assumed compositor cost and the photosensor position.
- Without O5, the same model gives **~22–34 ms**, which is where MAME sits (~20 ms). MAME presents immediately and doesn't delay video to match audio.

### 1.4 Audio side (why O5 exists) [V]

| Parameter | Value | Where |
|---|---|---|
| Output backend | miniaudio, s16, native device rate, `ma_performance_profile_low_latency` | `unreal-qt/src/emulator/soundmanager.cpp:91-99` |
| Requested device buffer | period 256 × 2 = 512 frames (≈10.7 ms at 48 k) | same |
| Windows reality | WASAPI shared ignores the request and uses a 10 ms engine period | `core/tests/emulator/sound/sound_adaptivity_test.cpp:536-544` |
| DRC target | `DRC_TARGET_MS = 40` | `core/src/emulator/sound/soundmanager.h:148` |
| Emergency refill | `EMERGENCY_REFILL_MS = 15` | `:159` |
| Hard resync | `HARD_RESYNC_MS = 160` | `:166` |
| Max rate trim | `DRC_MAX_TRIM = ±0.5%` | `:178` |

- Audio latency ≈ ring target + device buffer + OS output ≈ **52 ms+**, and the video delay of 2 frames was added to match it (`docs/emulator/design/audio/drc-rate-control.md:55-67`).
- **Likely under-count:** `updateDrcControl` samples occupancy *before* the frame's enqueue (`core/src/emulator/sound/soundmanager.cpp:784` vs `:795`). So the controller regulates the sawtooth **trough** to 40 ms, and mean audio latency is probably 5–10 ms above the documented figure [?]. Verify with `UNREAL_AUDIO_DIAG`.
- **Root cause of the high floor:** audio is produced in whole-frame (20.48 ms) chunks. The ring has to hold at least one chunk plus jitter margin plus a device period, so the 40 ms target is close to the floor for per-frame production.

---

## 2. Why a dedicated Game Mode is the right call

| Goal | Demo mode (current, keep) | Game Mode |
|---|---|---|
| Time base | Exact 48.83/50.08 Hz absolute deadline | Display-aware: VRR at native rate, 50/49 Hz display mode, or late-burst on fixed 60/120 Hz |
| A/V relation | Constant offset (video delayed to match audio) | Video first; audio as low as practical; small constant audio-behind offset is acceptable |
| Audio | 40 ms target, whole-frame chunks, DRC ±0.5% | 15–25 ms target with sub-frame production; optionally wider DRC trim |
| Video | Every frame exactly once, delayed 2 frames | Newest frame ASAP; never queue |
| Frame pacing artefacts | Judder on 60 Hz accepted | Judder avoided via VRR/50 Hz or accepted |
| Extras | Temporal blend / CRT allowed | Temporal blend off (it adds perceived lag); CRT fine (cheap single pass) |
| Debug | TTD etc. allowed | TTD recording off, or canonical frames only when run-ahead is on |
| Optional | — | Run-ahead / preemptive frames (0/1/2) |

**Implement it as one preset** (a `GameMode` feature or `[PROFILE] Mode=game`) that sets a group of settings atomically. Most of those settings already exist: `AVSyncDelayFrames`, temporal blending and TTD. Surface the preset in the UI, plus a HUD latency readout so users see the effect.

---

## 3. Measure first: instrumentation and a test ROM

### 3.1 Test program that isolates the emulator from game logic

The program polls SPACE in a tight loop and flips the border immediately, so a change is visible within ~30 T-states and every border line reacts. Point the photosensor at the border.

```asm
        org  #8000
        di
loop:   ld   a,#7F        ; half-row B,N,M,SS,SPACE
        in   a,(#FE)
        and  1            ; bit0 = SPACE (0 = pressed)
        ld   a,0          ; black (LD doesn't touch flags)
        jr   nz,out
        ld   a,7          ; white
out:    out  (#FE),a
        jr   loop
```

- Measures I1–I6 plus O1–O9 with **no guest lag**.
- Adding a second variant that polls once per frame in an IM2 ISR (`halt`-synced) measures typical-game behaviour.
- Ship both as `.sna`/`.tap` in `testdata/latency/` and add an automation recipe to `/.recipe/`, so the hardware tester runs can be scripted through WebAPI/MCP (load, run N presses, collect results).

### 3.2 Stage timestamps (host clock, ns)

Stamp each event (`steady_clock` or `mach_absolute_time`), store them in a per-emulator ring, and expose them via the HUD and WebAPI:

| Stamp | Where to add |
|---|---|
| `tHostEvent` | `QKeyEvent::timestamp()` (ms) or, better, the SDL3 event `timestamp` (ns); carry it inside `KeyboardEvent` (`devicescreenglwindow.cpp:609`) |
| `tApplied` | `Keyboard::PressKey` |
| `tFirstSeen` | first `HandlePortIn` whose result differs for that row (`keyboard.cpp:314-356`) |
| `tLatch` | exists: `Screen::GetLastLatchTimestampUs()` (`screen.h:680-683`) |
| `tPaint` | exists: `pVideoPresentLatencyUs` EMA (`mainwindow.cpp:1773-1790`) |
| `tPresented` | GL: `glFenceSync` + `glClientWaitSync` after swap (approximate). Metal: `MTLDrawable.presentedTime`. DXGI: `GetFrameStatistics`. Vulkan: `VK_GOOGLE_display_timing` / `VK_KHR_present_wait` |

The HUD shows the "input → first seen", "first seen → latch" and "latch → presented" medians. This turns the tester into a validation tool rather than the only source of truth.

---

## 4. Fixes that help every mode (no accuracy cost)

**L1: MessageCenter lost wakeup** (`messagecenter.cpp:166-170`).
Replace the bare `wait_for` with a predicate wait:
```cpp
m_cvEvents.wait_for(lock, 50ms, [this]{ return !m_queue.empty() || m_requestStop; });
```
Better still, do the empty-check and wait under one lock hold inside `EventQueue`. This removes a rare 50 ms outlier from both input (I3) and frame notify (O3).

**L2: take input off MessageCenter.**
- Input is latency-critical and shouldn't share a FIFO with debugger/HUD/audio-hunger traffic.
- Give the core an `InputSink` with lock-free semantics:
  - the keyboard matrix as `std::atomic<uint8_t> rows[8]` with `fetch_and`/`fetch_or` (the same pattern as `Mouse`, `io/mouse/mouse.h:72-76`), written directly from the input thread;
  - a small SPSC ring for events that also need the TTD journal (`RecordInputEvent`, `timetravelmanager.cpp:1335-1350`), drained by the emulation thread at instruction or frame granularity. That gives an exact, deterministic emulated-time stamp.
- This also fixes the RMW race between the MessageCenter thread and `DebugKeyboardManager::ApplyKey` (`debugkeyboardmanager.cpp:57-71`).

**L3: stop the GUI thread gating input.**
- `paintGL` + `swapBuffers` with `SwapInterval(1)` can block the GUI thread for up to a refresh, and key events queue behind it [?].
- Options:
  - move GL rendering to a **dedicated render thread** (§7.2), which is the proper fix;
  - or read input on a dedicated thread using raw APIs:
    - Windows: Raw Input / GameInput;
    - macOS: `IOHIDManager` or `GCKeyboard`/`GCController`;
    - Linux: evdev/libinput.

    Map it to the ZX matrix without the Qt event loop. Keep Qt key handling for UI shortcuts only.

**L4: gamepads and joysticks.**
- Add SDL3's gamepad subsystem *inside the Qt app* (`SDL_Init(SDL_INIT_GAMEPAD)`, no SDL video).
- Poll it on the input thread and map to Kempston (`#1F`), Sinclair 1/2 (keys 6–0 / 1–5), Cursor or custom keys.
- This also replaces the Kempston stub (`portdecoder_scorpion256.cpp:236-247`; other models don't decode `#1F` as joystick at all).
- It's needed for the Steam Deck anyway.

**L5: macOS Game Mode.**
- Add `LSApplicationCategoryType` = `public.app-category.games` to `unreal-qt/install/macos/Info.plist`.
- Game Mode activates for fullscreen games. It gives CPU/GPU priority and doubles Bluetooth sampling for controllers and AirPods. Currently absent. **[V]**

**L6: scheduling hygiene on Windows** (from the frame-budget triage, still open):
- hybrid sleep + ~1 ms spin in `TimeHelper::WaitUntilPrecise` (`core/src/common/timehelper.cpp:41-94`) in Game Mode only;
- opt out of EcoQoS/power throttling;
- optional `AvSetMmThreadCharacteristics("Games")` for the emulation thread. Keep it below the MMCSS "Pro Audio"/"Audio" class of the miniaudio thread, as the comment in `threadhelper.cpp:84-91` requires.

**L7: presentation order.** Post `NC_VIDEO_FRAME_REFRESH` (or signal the presenter) **immediately after `LatchFramebuffer`**, before tape/FDC/sound frame-end work (`mainloop.cpp:509` vs `:627`). That saves 0.3–2 ms per frame for free.

**L8: temporal blending.** Force it off in Game Mode (`_temporalEnabled`, `devicescreenglwindow.cpp:436-454`). It blends history frames, which visually smears motion across frames.

---

## 5. Game Mode: audio

In Game Mode the video no longer waits for audio (`AVSyncDelayFrames=0`). Audio then trails the picture by the ring depth (~50–60 ms), which is noticeable on sharp sound effects. Ways to shrink it, in order of payoff:

**A1: make `DRC_TARGET_MS` and `EMERGENCY_REFILL_MS` runtime settings** (they are `constexpr` now, `soundmanager.h:148`, `:159`).
- Game Mode could use ~25–30 ms with per-frame production.
- The `SoundAdaptivity_Test.AVLatencyBudget` invariants (`sound_adaptivity_test.cpp:499-556`) must become per-profile. The Windows floor is ~32 ms with the 10 ms WASAPI shared period and per-frame chunks.

**A2: sub-frame production (structural).**
- Emulate and enqueue in K slices per frame (K = 2–4), each followed by a sleep to its slice deadline.
- The ring sawtooth shrinks from 20.5 ms to 5–10 ms, so a 12–20 ms target becomes safe.
- **Side benefit for input:** games that poll in the main loop (not the ISR) see input up to (K−1)/K frame sooner.
- **Cost:**
  - K× wakeups;
  - `CPUFrameCycle` needs a "run until T" entry (`Z80FrameCycle` currently always runs to `_frameLimit`, `z80.cpp:547-571`);
  - frame-end work stays once per frame;
  - DRC and refill must be retuned per slice.
- This is the same mechanism that §6 E/F builds on.

**A3: device buffer.**
- Request period 128 × 2 in Game Mode.
- On Windows, prefer the low-latency shared path (IAudioClient3) or exclusive mode where miniaudio supports it [?]. Exclusive mode takes the device from other apps; make it opt-in.
- On macOS, CoreAudio handles 128-frame buffers fine.

**A4: accept a fixed audio offset.**
- The audio-behind-video offset in Game Mode is constant, so it's much less objectionable than jitter.
- Expose it in the HUD, and optionally let the user trade 1 frame of video delay for sync (`AVSyncDelayFrames=1`, ≈20 ms).

**A5: widen DRC trim in display-locked modes (§6 C).**
- Pentagon 48.83 Hz on a 50 Hz display needs +2.4% (≈41 cents), which is audible to musicians.
- On a 49 Hz display it needs only 0.35%, which the current trim already covers.
- Prefer choosing the display rate over bending pitch.

---

## 6. Game Mode: pacing alternatives

Let F be the emulated frame period (20.48 ms Pentagon, 19.97 ms 48K/128K) and V the display period.

| Option | How | Latency effect | Audio | Accuracy | Platform needs | Effort |
|---|---|---|---|---|---|---|
| **A. Current:** absolute deadline, burst then sleep, present on next vsync | `mainloop.cpp:190-245` | baseline: I6 ≈ F/2, O7 ≈ V/2 | perfect (DRC) | exact speed | none | — |
| **B. Late burst ("frame delay")** | Sleep first, run the burst so it ends just before the vblank that will show it: start = next_vblank − (burst_estimate + margin) | Removes O7 (≈ V/2 = 8 ms at 60 Hz); input sampled later, so the key → latch gap shrinks too | Frame production jitters by up to V around the ideal cadence; the ring absorbs it at ≥25 ms targets | exact speed; one frame is shown twice every ~5 frames (judder) | vblank timestamps (CVDisplayLink / `CAMetalDisplayLink`, DXGI frame statistics or waitable swapchain, SDL3 swapchain waits) | medium |
| **C. Display-locked at a matching refresh** | Switch the display to 50 Hz (48K/128K) or 49 Hz (Pentagon, where available); pace emulation from vsync | O7 → ~0; no judder; B becomes trivial | DRC trim covers ≤0.5% (50 Hz vs 50.08, 49 vs 48.83); must disable emergency-refill fighting | exact within 0.35% | exclusive fullscreen / mode switch (SDL3 `SDL_SetWindowFullscreenMode`, macOS `CGDisplaySetDisplayMode`, Windows `ChangeDisplaySettingsEx`); **Steam Deck refresh slider (40–60 Hz LCD, 45–90 Hz OLED)** [?] | medium |
| **D. VRR present-on-ready** | Present every frame immediately at native 48.83/50.08 Hz; the panel follows | O7 → ~0 (+ LFC doubling below the VRR floor, still fine); no judder | unchanged (emulation clock stays master) | exact | VRR display + a flip-model or native swapchain: DXGI flip + `ALLOW_TEARING`, Metal on macOS 14+ with adaptive-sync displays / ProMotion, Vulkan `FIFO_RELAXED`/`IMMEDIATE` on gamescope/KWin. **Not reliably reachable through Qt `QOpenGLWindow`**, especially on macOS | medium (with a new presenter) |
| **E. Sub-frame slicing** | Emulate in K slices per F, in real time (see A2) | Input seen up to (K−1)/K·F sooner for main-loop pollers; enables small audio buffers | better (small ring) | exact | none | medium–high |
| **F. Beam racing / "lagless vsync"** (WinUAE style) | Emulate raster slices in lockstep with the real display scanout and present partial frames with tearing | theoretical minimum: a few ms total | fine | exact | display rate = emulated rate (C), tearing allowed (no compositor), front-buffer-ish presentation; fragile under compositors | high (research) |
| G. Vsync-paced at 60 Hz (speed-up) | Run 60 emulated frames/s | O7 → 0 | would need a time-stretch; pitch or time +23% | **wrong speed**: rejected | — | — |

**Recommendation by display class:**
- **VRR display:** D, plus B-style late input sampling, which is almost free with VRR. Best overall.
- **Display with a 50/49 Hz mode (TVs, many monitors, Steam Deck):** C.
- **Fixed 60/120/144 Hz without VRR:** B (late burst). At 120/144 Hz, O7 is already small (V/2 = 4.2/3.5 ms), so just A with delay 0 is decent.
- **E:** worth doing regardless, for the audio floor.
- **F:** optional research track. It is the path to "better than MAME".

---

## 7. Game Mode: run-ahead and preemptive frames

### 7.1 What they are

**Run-ahead N** (RetroArch):
- Each host frame: save state S, emulate N frames silently with the current input, show the last one's video, then restore S and emulate one frame "for real" (audio from this one).
- Result: N frames of **guest lag disappear**.

**Preemptive frames** (RetroArch 1.15+; cheaper):
- Keep a ring of the last N frame-start snapshots.
- Only when input *changes*: restore the snapshot N frames back, re-emulate N frames with the new input applied from that point, and show the newest.
- With no input change the cost is ~0.

### 7.2 Why unreal-ng is well placed

- Every peripheral already has `TTDSaveState`/`TTDLoadState` through the TTD peripheral registry.
- RAM has dirty-page tracking (`memory.cpp:333` `MarkDirty`, the TTD page store).
- Headless frame cost is ~0.37 ms, full ~0.8 ms (your benchmark).
- **What to build:** a **raw in-memory rollback ring**, separate from TTD's compressed checkpoints:
  - RAM pages marked dirty since the snapshot get a memcpy;
  - CPU registers and peripheral blobs;
  - no compression.
  - Target: < 0.2 ms per snapshot and per restore.

### 7.3 Requirements and caveats

1. **Determinism.** Everything that feeds the emulated machine must be a function of state + input.
   - The RTC reads `system_clock` (`io/rtc/smucnvram.cpp:226-227`), which is harmless at second granularity but should use the emulated-time-derived value within a rollback window.
   - Tape and disk are state-driven.
   - Audio devices must be serialised (they are).
2. **Audio in speculative frames.**
   - Suppress output with the existing `setSynthesisSuppressed`, while coprocessor cores keep advancing (D3 semantics).
   - Take audio from canonical frames only.
   - Preemptive re-simulation replays already-emitted audio: discard it (RetroArch does the same).
3. **Cost.**
   - Run-ahead 1 means ~2× frame emulation, plus snapshot/restore.
   - With GS + MoonSound + TSFM idle-running (~2+ ms per frame today, see the performance review), that is 2×–3× of a large number. **The quiescence gating from the performance review is a prerequisite.**
   - Preemptive frames mostly avoid the multiplier.
4. **TTD interplay.**
   - Speculative frames must not be journalled.
   - Simplest: TTD recording and run-ahead are mutually exclusive in Game Mode.
   - Later: record canonical frames only.
5. **FDC/tape side effects.** Speculative frames may issue disk writes. Buffer image writes, or suppress write-back until frames are canonical. The disk model already has dirty tracking.
6. **Philosophy.** Run-ahead makes the emulator respond *faster than the real machine*. That's great for play, but wrong for demos and timing research. It is opt-in, Game Mode only, and labelled as such.
7. **Per-game guest lag differs.** Offer 0/1/2 and a "detect" helper: run a keypress in a sandbox copy and count frames until the framebuffer changes. This is feasible with the snapshot ring and the screen digest (`screendigest.cpp`).

---

## 8. Frontend and presentation technology options

### 8.1 Current: Qt6 widgets + `QOpenGLWindow` in `createWindowContainer`

- **Setup:** `SwapInterval(1)`, double buffer, paint on the GUI thread (`devicescreenglwindow.cpp:171-181`, `:480`), pulled on notification via queued invoke. **[V]**
- **Limits:**
  - no present-mode choice (mailbox/immediate/tearing);
  - no frames-in-flight control;
  - no vblank timestamps;
  - `requestUpdate` timing is Qt-internal;
  - GL on macOS is deprecated (layered over Metal) with no VRR/adaptive-sync control;
  - on Windows a GL child HWND is typically DWM-composed (+~1 refresh). NVIDIA's "OpenGL present method: prefer layered on DXGI swapchain" driver option can change this [?].
- **Cheap improvements inside the current stack:**
  - L7 and L8;
  - a render thread with its own `QOpenGLContext` that waits on the frame-latch event directly (no MessageCenter hop) and swaps;
  - Game Mode `SwapInterval(0)` plus own pacing, where tearing is acceptable.

  The gain is modest and platform-dependent.

### 8.2 Qt6 + QRhi on a dedicated `QWindow` (keep the Qt shell)

- A top-level or child `QWindow` driven by a render thread using QRhi: Metal on macOS, D3D11/12 on Windows, Vulkan on Linux.
- `QRhiSwapChain` flags include `NoVSync` and `MinimalBufferCount`.
- Port the CRT shader through `qsb` (GLSL → SPIR-V → MSL/HLSL).
- **Pros:**
  - keeps the Qt6 developer UI;
  - native graphics APIs;
  - no-vsync presentation;
  - fewer buffers.
- **Cons:**
  - QRhi is a semi-public API with limited compatibility guarantees;
  - still no VRR/display-mode switching or vblank timestamps from Qt; per-platform native calls are needed for those;
  - still the Qt event loop for input unless L3 is done.
- **Effort:** medium. Good for bringing Qt to T1/T2-ish.

### 8.3 SDL3 "player" client (already planned for Steam Deck)

- **Graphics:** SDL_GPU (Metal/D3D12/Vulkan) with explicit present modes via `SDL_SetGPUSwapchainParameters` (VSYNC / MAILBOX / IMMEDIATE) and frames-in-flight control (`SDL_SetGPUAllowedFramesInFlight`, SDL ≥ 3.2 [?]).
- **Display:** exclusive fullscreen with display-mode selection (`SDL_SetWindowFullscreenMode`), for 50/49 Hz (§6 C).
- **Input:** gamepad/joystick subsystem; event timestamps in ns (§3.2); raw scancodes; a Steam Deck/gamescope-friendly event model.
- **UI:** minimal library/launcher UI with Dear ImGui (the ImGui debugger client you planned can share the backend).
- **Pros:** the best latency control of any cross-platform option; one code path for macOS/Windows/Linux/Deck; aligned with your two-client plan (casual SDL3 player + Qt6 developer client).
- **Cons:** a second client to maintain. The macOS main-thread/NSApp constraints mean SDL and Qt shouldn't share a process as two UIs; ship it as a separate executable linking `core` directly (not over IPC; remote clients add latency by design).
- **Effort:** medium. **Recommended flagship for Game Mode.**

### 8.4 Native per-OS frontends

| OS | Presenter | Input | Notes |
|---|---|---|---|
| macOS/iOS | `CAMetalLayer` (`displaySyncEnabled`, `maximumDrawableCount=2`), `CAMetalDisplayLink` (macOS 14+) for vblank-aligned late burst, `presentedTime` for measurement; ProMotion/adaptive sync | `GCKeyboard`/`GCController`, `IOHIDManager` | Reuses the iOS host presenter work. Fullscreen opaque Metal layers can bypass composition ("direct to display") [?] |
| Windows | D3D11/12 flip-model swapchain, `FRAME_LATENCY_WAITABLE_OBJECT` + `SetMaximumFrameLatency(1)`, `DXGI_PRESENT_ALLOW_TEARING` for VRR/tearing, independent flip in borderless fullscreen | Raw Input / GameInput, XInput | Best-in-class latency control on Windows |
| Linux | Vulkan (`FIFO_RELAXED`/`MAILBOX`/`IMMEDIATE`), `VK_KHR_present_wait`; KMS/DRM direct scanout for kiosk builds | evdev/libinput | gamescope on Deck already supports tearing/VRR policies |

- **Pros:** absolute best control.
- **Cons:** three presenters to maintain.
- **Recommendation:** don't build full native apps. Implement **native presenter backends only where SDL3 falls short**, for example macOS `CAMetalDisplayLink` timing or DXGI waitable-object pacing, behind the same `FramePresenter` interface.

### 8.5 Recommended architecture

```
core (emulation, pacing policy hooks)
  └─ FrameSource: latch ring + latch timestamp + "frame ready" futex/semaphore
       └─ PacingPolicy {Demo, LateBurst, DisplayLocked, VRR} + RunAhead(0..2)
            ├─ Qt6 client  → Presenter: QOpenGLWindow today → QRhi render thread later
            └─ SDL3 player → Presenter: SDL_GPU (+ optional native Metal/DXGI timing shims)
InputSink (lock-free matrix + timestamped SPSC) ← Qt keys / raw input / SDL gamepad
```

- The pacing policy lives in the core (it already owns `MainLoop`). Clients provide display facts: refresh rate (`common/displayrefreshrate.h` already detects rate and VRR), vblank timestamps and present feedback.
- This preserves "the presenter never pulls audio around" for Demo mode. Game Mode explicitly allows presenter feedback.

---

## 9. Roadmap

| Phase | Items | Expected result |
|---|---|---|
| **0: Verify (hours)** | Run the tester with `[VIDEO] AVSyncDelayFrames=0`. Add the §3.1 test ROM. Fix L1 (lost wakeup). Add L5 (`Info.plist` category). | Confirms the ~40 ms attribution; ~63 → ~25 ms |
| **1: Game Mode in Qt (days)** | Preset (delay 0, temporal blend off, TTD off); L2 input sink + atomics; L3 render thread / raw input; L4 SDL gamepad; L7 notify order; A1 runtime DRC target; HUD latency stats (§3.2) | ~18–24 ms, stable, measurable in-app |
| **2: Presentation control** | SDL3 player with SDL_GPU; §6 D (VRR), C (50/49 Hz), B (late burst); optional Qt QRhi presenter | ~10–16 ms |
| **3: Audio floor** | A2 sub-frame production (+ §6 E); A3 device buffer profile | Audio-behind-video from ~55 to ~15–25 ms |
| **4: Run-ahead** | After the coprocessor gating from the performance review: raw rollback ring; preemptive frames; run-ahead 0/1/2 with auto-detect | −1 frame of guest lag per level, "better than hardware" |
| **5: Research** | §6 F beam racing on 50 Hz displays | Few-ms theoretical floor |

---

## Appendix A: references used

| Topic | Location |
|---|---|
| Present delay | `core/src/emulator/video/screen.cpp:780-787`, `:918-937`, `:980-992`; `screen.h:669-673`, `:705-711`; `config.cpp:356-358` |
| Main loop pacing | `core/src/emulator/mainloop.cpp:94-254`, `:499-635` |
| Timer | `core/src/common/timehelper.cpp:41-94` |
| MessageCenter | `core/src/3rdparty/message-center/messagecenter.cpp:152-172`, `eventqueue.cpp:380-436` |
| Keyboard | `core/src/emulator/io/keyboard/keyboard.h:293`, `keyboard.cpp:139`, `:165`, `:314-356`, `:399-454` |
| Qt input / presentation | `unreal-qt/src/widgets/devicescreenglwindow.cpp:171-181`, `:238-245`, `:407-586`, `:589-637`; `devicescreenwrapper.cpp:24`, `:156-169`; `mainwindow.cpp:375-376`, `:1460-1461`, `:1531`, `:1680-1710`, `:1773-1790` |
| Audio | `unreal-qt/src/emulator/soundmanager.cpp:91-121`, `:245-291`; `core/src/emulator/sound/soundmanager.h:148-181`, `soundmanager.cpp:784-848`; `core/src/common/sound/filters/resampler_drc.h` |
| Design docs | `docs/emulator/design/audio/drc-rate-control.md:37-69`, `:145-167`; `docs/inprogress/2026-08-17-audio-sync/audio-sync-design.md:95`, `:267-281`, `:357`; `docs/inprogress/2026-08-14-av-sync-and-presentation/README.md`; `docs/inprogress/2026-09-15-frame-budget-triage/05-optimization-roadmap.md`, `06-overrun-root-cause-analysis.md` |
| Threads | `core/src/common/threadhelper.cpp:53-93` |
| Joystick stub | `core/src/emulator/ports/models/portdecoder_scorpion256.cpp:236-247` |
| macOS bundle | `unreal-qt/install/macos/Info.plist` (no `LSApplicationCategoryType`) |
