# Speed Control Widget — Feature Specification

**Date**: 2026-02-07
**Status**: In Progress
**Component**: `unreal-qt/src/debugger/speedcontrolwidget.h/.cpp`

## Overview

An audio player-style step granularity control integrated into the debugger toolbar window. Allows the user to control execution speed from full-speed (1×) down to 1 T-state (2 ULA pixels), with all timing adapted dynamically to the active emulator model's frame parameters.

## Visual Design

### Widget Layout

![Speed Control Widget](mockup_widget.png)

### Widget States

![Widget States: Frame, Line, and T-state modes](mockup_states.png)

## Controls

| Element | Icon | Function |
|---|---|---|
| **Step Back** | ⏮ | Move slider one position left (coarser granularity) |
| **Play/Pause** | ▶ / ⏸ | Start/stop auto-run timer. Toggles between play and pause |
| **Step Forward** | ⏭ | Execute one step at current granularity (single-shot) |
| **Interval** | *spinbox* | Adjustable auto-run timer interval (100–5000ms, default 1000ms) |
| **Slider** | *0–12* | 13-position granularity selector |
| **Label** | *text* | Shows preset name + computed T-state count in monospace |

## Speed Presets

All presets read frame timing dynamically from `CONFIG` struct (`platform.h:380`):

- `config.frame` (F) — total t-states per frame
- `config.t_line` (L) — t-states per scanline (including hblank)

| Position | Label | Method Called | Auto-run Interval | Example (48K) |
|---|---|---|---|---|
| 0 | 1× | `Resume()` | — | Full speed |
| 1 | 25 fps | `RunFrame()` | 40ms fixed | 69888 T/step |
| 2 | 5 fps | `RunFrame()` | 200ms fixed | 69888 T/step |
| 3 | 1 fps | `RunFrame()` | 1000ms fixed | 69888 T/step |
| 4 | ½ frame | `RunTStates(F/2)` | User spinbox | 34944 T |
| 5 | 100 lines | `RunTStates(L×100)` | User spinbox | 22400 T |
| 6 | 10 lines | `RunTStates(L×10)` | User spinbox | 2240 T |
| 7 | 1 line | `RunTStates(L)` | User spinbox | 224 T |
| 8 | ½ line | `RunTStates(L/2)` | User spinbox | 112 T |
| 9 | ¼ line | `RunTStates(L/4)` | User spinbox | 56 T |
| 10 | ⅛ line | `RunTStates(L/8)` | User spinbox | 28 T |
| 11 | 1/16 line | `RunTStates(L/16)` | User spinbox | 14 T |
| 12 | 1 T | `RunTStates(1)` | User spinbox | 1 T |

### Model Adaptation

| Model | config.frame | config.t_line | ½ frame | 1 line |
|---|---|---|---|---|
| ZX Spectrum 48K | 69888 | 224 | 34944 | 224 |
| ZX Spectrum 128K | 70908 | 228 | 35454 | 228 |
| Pentagon | 71680 | 224 | 35840 | 224 |

## Architecture

### Design Principles

1. **No custom execution logic** — all stepping uses existing `Emulator::RunFrame()` and `Emulator::RunTStates()` methods
2. **Timer is orchestrator only** — QTimer triggers step calls at configurable intervals, it does not control emulation timing
3. **Dynamic adaptation** — preset T-state counts read from `CONFIG` at runtime, not hardcoded
4. **Non-intrusive integration** — existing debugger buttons (Continue, StepIn, StepOver, etc.) auto-stop the speed control

### Integration with Existing Controls

```
User clicks Continue → m_speedControl->stop() → Resume()
User clicks StepIn   → m_speedControl->stop() → RunSingleCPUCycle()
Breakpoint hit        → m_speedControl->stop() → updateState()
```

### Class Hierarchy

```
QWidget
  └── SpeedControlWidget
        ├── QToolButton  _stepBackButton    (⏮)
        ├── QToolButton  _playPauseButton   (▶/⏸)
        ├── QToolButton  _stepForwardButton (⏭)
        ├── QSpinBox     _intervalSpinBox   (100-5000ms)
        ├── QSlider      _slider            (0-12)
        ├── QLabel       _label             (preset name + T count)
        └── QTimer       _timer             (auto-run orchestrator)
```

## Test-Driven Development

### Unit Tests (Proposed)

```cpp
// Test: Preset T-state calculations match expected values for 48K model
TEST(SpeedControlWidget, TStatePresets48K) {
    // config.frame = 69888, config.t_line = 224
    EXPECT_EQ(getTStatesForPreset(POS_HALF_FRAME),     69888 / 2);   // 34944
    EXPECT_EQ(getTStatesForPreset(POS_100_LINES),      224 * 100);   // 22400
    EXPECT_EQ(getTStatesForPreset(POS_10_LINES),       224 * 10);    // 2240
    EXPECT_EQ(getTStatesForPreset(POS_1_LINE),         224);         // 224
    EXPECT_EQ(getTStatesForPreset(POS_HALF_LINE),      224 / 2);     // 112
    EXPECT_EQ(getTStatesForPreset(POS_QUARTER_LINE),   224 / 4);     // 56
    EXPECT_EQ(getTStatesForPreset(POS_EIGHTH_LINE),    224 / 8);     // 28
    EXPECT_EQ(getTStatesForPreset(POS_SIXTEENTH_LINE), 224 / 16);    // 14
    EXPECT_EQ(getTStatesForPreset(POS_1_TSTATE),       1);           // 1
}

// Test: Preset T-state calculations for 128K model (different scanline width)
TEST(SpeedControlWidget, TStatePresets128K) {
    // config.frame = 70908, config.t_line = 228
    EXPECT_EQ(getTStatesForPreset(POS_1_LINE),       228);
    EXPECT_EQ(getTStatesForPreset(POS_HALF_LINE),    114);
    EXPECT_EQ(getTStatesForPreset(POS_QUARTER_LINE), 57);
}

// Test: Timer intervals
TEST(SpeedControlWidget, TimerIntervals) {
    EXPECT_EQ(getTimerIntervalMs(POS_FULL_SPEED), 0);
    EXPECT_EQ(getTimerIntervalMs(POS_25_FPS),     40);
    EXPECT_EQ(getTimerIntervalMs(POS_5_FPS),      200);
    EXPECT_EQ(getTimerIntervalMs(POS_1_FPS),      1000);
    // Positions 4-12 return user spinbox value
}

// Test: isFullSpeed()
TEST(SpeedControlWidget, IsFullSpeed) {
    // At position 0 → true
    // At any other position → false
}

// Test: stop() stops timer and resets button state
TEST(SpeedControlWidget, StopResetsState) {
    // Start auto-run, verify isRunning() == true
    // Call stop(), verify isRunning() == false
    // Verify button text reverts to ▶
}
```

### Integration Tests

| Test | Steps | Expected Result |
|---|---|---|
| **Step Forward at 1 line** | Set slider to pos 7, click ⏭ | PC advances by ~1 instruction per 224T |
| **Auto-run at 1 fps** | Set slider to pos 3, click ▶ | Screen updates once per second |
| **Auto-run at 1 T** | Set slider to pos 12, click ▶ | Disassembler advances 1 instruction per tick |
| **Stop on Continue** | Start auto-run, click Continue | Timer stops, emulator runs at full speed |
| **Stop on breakpoint** | Set breakpoint, start auto-run | Timer stops when breakpoint hit |
| **Model switch** | Switch 48K → 128K while widget visible | Label updates from "224T" to "228T" |
| **Interval adjustment** | Set to 500ms during auto-run | Step rate doubles |

## Files

| File | Type | Description |
|---|---|---|
| `speedcontrolwidget.h` | NEW | Widget class declaration |
| `speedcontrolwidget.cpp` | NEW | Widget implementation with dark styling |
| `debuggerwindow.h` | MODIFIED | Added `m_speedControl` field |
| `debuggerwindow.cpp` | MODIFIED | Construction, signal wiring, stop-on-manual-action |
