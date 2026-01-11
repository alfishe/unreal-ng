# Proposal: Real-time Feature Toggles for CPU Optimization

## 1. Problem Statement
Performance profiling (flamegraphs) indicates that significant CPU cycles are consumed by:
1.  **Sound Generation/Mixing:** Continuous calculation of audio samples for Beeper and AY-3-8910 chips, even when not strictly necessary (e.g., in background instances or high-speed turbo modes).
2.  **Audio Post-Processing:** Heavy DSP logic (Oversampling and FIR filtering) in `SoundChip_TurboSound`, which runs a loop `DECIMATE_FACTOR` times for every output sample.
3.  **Breakpoint Processing:** Conditional checks and `BreakpointManager` calls within the core CPU instruction loop (`Z80Step`), which adds overhead to every emulated cycle.

Currently, these features are mostly hardcoded or tied to a global debug state, preventing fine-grained optimization for multi-instance scenarios (like VideoWalls) or automated testing where audio/debugging is not required.

## 2. Proposed Solution
Implement per-instance feature toggles using the existing `FeatureManager` architecture. This will allow enabling or disabling CPU-intensive subsystems in real-time without restarting the emulator.

### Core Toggles to Implement:
*   `sound`: Enable/Disable all audio generation and mixing logic.
*   `sound.quality`: Switch between "High" (Oversampling + FIR) and "Low" (Simple decimation/Nearest neighbor) audio quality.
*   `breakpoints`: Enable/Disable the execution of breakpoint checks in the CPU loop.

## 3. Implementation Plan

### Phase 1: Feature Registration
Extend `FeatureManager::setDefaults()` to include the new performance-focused toggles:
*   **ID:** `sound`, **Alias:** `snd`, **Category:** `performance`
*   **ID:** `sound.quality`, **Alias:** `sndq`, **Category:** `performance` (Modes: `high`, `low`)
*   **ID:** `breakpoints`, **Alias:** `bp`, **Category:** `debug`

### Phase 2: Sound Optimization (`SoundManager`)
Modify `SoundManager` to respect the `sound` feature state:
*   **`handleStep()`**: Return early if sound is disabled to avoid per-instruction DAC updates.
*   **`handleFrameEnd()`**: Skip audio mixing and callback execution if disabled.
*   **`updateDAC()`**: Add an early exit to prevent buffer filling logic.

### Phase 3: DSP Optimization (`SoundChip_TurboSound`)
Modify `SoundChip_TurboSound::handleStep()` to check the `sound.quality` feature mode:
*   **High Quality (Default):** Keep the existing loop with `startOversamplingBlock()`, `recalculateInterpolationCoefficients()`, and `endOversamplingBlock()`.
*   **Low Quality:** Bypass the `DECIMATE_FACTOR` loop. Simply call `updateState()` once and use the raw `mixedLeft()`/`mixedRight()` values directly for the output sample.

### Phase 4: CPU Optimization (`Z80`)
Optimize the instruction fetch/execute loop:
*   Update `Z80::Z80Step()` to query `FeatureManager::isEnabled(Features::kBreakpoints)` before invoking the `BreakpointManager`.
*   Maintain the existing `isDebugMode` master switch but allow the granular `breakpoints` toggle to override it for performance tuning while remaining in a "debug" context.

### Phase 5: Real-time Updates
Leverage the `FeatureManager::onFeatureChanged()` callback to:
*   Synchronize internal CPU flags.
*   Immediately stop/start audio processing in the `SoundManager` without losing synchronization.
*   Update caching variables (e.g., `isSoundLowQuality`) to avoid repeated string lookups in hot paths.

## 4. Expected Benefits
*   **Lower CPU Footprint:** Significant reduction in per-instance overhead for VideoWall or headless modes.
*   **Faster Turbo Mode:** Disabling sound and breakpoints will allow the emulator to reach significantly higher speeds during automation.
*   **Improved Scalability:** Capability to run more concurrent emulator instances on the same hardware.

## 5. Verification Plan
1.  **Benchmarking:** Compare CPU usage (via `top`/Activity Monitor) with sound/breakpoints ON vs OFF, and Sound Quality HIGH vs LOW.
2.  **Functional Testing:** Ensure sound resumes correctly and state is preserved when toggled back ON.
3.  **Audio Quality Check:** Verify that "Low" quality is audible and recognizable, albeit with expected aliasing/artifacts, and "High" quality retains the original fidelity.
4.  **Breakpoint Testing:** Verify that breakpoints are ignored when toggled OFF and correctly trigger when toggled ON.
