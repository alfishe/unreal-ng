# P-03 vblank pacing at the Quick Access refresh (gate)

**Question.** Does gamescope deliver vblanks at the refresh set in Quick Access (50 / 49 Hz on LCD and
OLED)? Can the app measure that, and notice a change, fast enough to pick display-locked pacing?
([rendering.md §4](../../../../docs/inprogress/2026-10-08-steamdeck-client/rendering.md))

**Pass.** Measured interval within ±0.1 % of the setting; a change is detected in under 1 s.

## What it does

An SDL_GPU loop with no emulation:

1. acquire;
2. clear to one of two alternating greys;
3. submit.

The time at which acquire returns, per frame, is the refresh the app really gets. Every second the
app writes to `p03-summary-*.csv`:

- the median interval and the Hz it implies;
- p1 / p99 / max interval;
- missed vblanks (intervals > 1.5 × median);
- the acquire wait;
- a change flag. On a change, the screen also turns red for one second.

Raw per-frame times go to `p03-frames-*.csv`. The first line records the SDL video driver, the GPU
driver, the present mode, frames in flight and the display mode's refresh.

## Run

```
--present vsync --fif 2                 # baseline
--present vsync --fif 1
--present mailbox                       # does gamescope offer it?
--work-ms 3                             # simulate 3 ms of emulation + upload per frame
--seconds 120                           # fixed-length run
--windowed                              # desktop development: a 1280x800 window
```

Steps:

1. Start at 60 Hz. Change the Quick Access refresh to 50, then 49, 45 and 40 Hz (OLED: 90 as well).
2. Turn the frame limiter on and off.
3. Dock to an external display and undock.

Note the time of each change, then compare it with the log.

## Results

| Date | Deck | Setting | Measured Hz | p99 interval (ms) | Missed / min | Detect time (s) | Notes |
|------|------|---------|-------------|-------------------|--------------|-----------------|-------|
| 2026-10-08 | none: macOS M1 Ultra, Metal, `--windowed` (development smoke run) | 60 Hz monitor | 60.01–60.25 | 18–21 | 0 | — | path works; the Deck runs are still to do |
