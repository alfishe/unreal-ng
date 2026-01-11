# CPU Optimization Implementation Tasks

- [ ] **Phase 1: Feature Registration**
    - [ ] Update `FeatureManager` to include `sound` (performance), `sound.quality` (performance), and `breakpoints` (debug) features.
    - [ ] Add constants for new feature IDs and descriptions in `featuremanager.h`.

- [ ] **Phase 2: Sound Optimization**
    - [ ] Modify `SoundManager::handleStep` to check `sound` toggle.
    - [ ] Modify `SoundManager::handleFrameEnd` to check `sound` toggle.
    - [ ] Modify `SoundManager::updateDAC` to check `sound` toggle.

- [ ] **Phase 3: DSP Optimization**
    - [ ] Modify `SoundChip_TurboSound` to accept a reference to `FeatureManager` or cache the quality setting.
    - [ ] Implement the "Low Quality" path in `SoundChip_TurboSound::handleStep` that bypasses FIR filters.

- [ ] **Phase 4: CPU Optimization**
    - [ ] Add `bool _breakpointsEnabled` to `Z80` class (or checking `FeatureManager` directly if performance permits, though caching is better).
    - [ ] Update `Z80::Z80Step` to use the toggle before calling `BreakpointManager`.
    - [ ] Ensure `FeatureManager::onFeatureChanged` updates the cached boolean in `Z80`.

- [ ] **Phase 5: Verification**
    - [ ] Verify CPU usage improvements.
    - [ ] Verify toggles work in real-time.
