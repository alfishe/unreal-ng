# P-01 SDL_GPU render path latency (gate)

**Question.** Does SDL3 + SDL_GPU give the latency and pacing control the design needs on the Deck
under gamescope? ([poc-plan.md](../../../../docs/inprogress/2026-10-08-steamdeck-client/poc-plan.md), [rendering.md §2–§3](../../../../docs/inprogress/2026-10-08-steamdeck-client/rendering.md))

**Pass.** Median input → photon ≤ 40 ms at a 50 Hz panel; host CPU < 1 ms and GPU < 2 ms per frame.

## What it does

Each display refresh the app:

1. waits for and acquires the swapchain image;
2. copies the newest presented frame with `CopyPresentedFramebuffer` straight into a mapped transfer
   buffer (cycle);
3. uploads it to an `R8G8B8A8_UNORM` texture;
4. does one nearest-filter blit with integer scale and crop;
5. submits.

The core paces itself (core-clocked). The display-locked tick is P-04. Present delay is 0 by
default (`Screen::SetPresentDelayFrames`). Audio goes from the core's callback into an SDL3 audio
stream at the device rate, and the stream fill is reported as the DRC occupancy.

The default guest program is a tight loop, poked at `#8000`:

```
DI; loop: IN A,(#1F); AND #10; border = fire ? 7 : 0; JR loop
```

It reacts within a few T-states, so all measured latency belongs to the host.

## Run

```bash
# Deck, Game Mode, non-Steam game: ~/steamdeck-poc/p01-sdl-gpu-latency  (Launch Options below)
--present vsync --fif 1 --delay 0 --crop full          # baseline
--present vsync --fif 2                                # smoother, +1 frame?
--present mailbox --fif 1                              # if gamescope offers it
--crop deckfit                                         # 320x200 x4 = 1280x800
--model PENTAGON                                       # 48.83 Hz machine
--file /home/deck/ZX/game.tap                          # any program (press logging only works for the border test)
--windowed                                             # desktop development: a 1280x800 window
```

Controls: A (or Space) = Kempston fire; D-pad = directions; View + Menu = quit.

## Measurements

| What | How |
|---|---|
| Software latency | `p01-presses-*.csv`: for each press and release, the time from the SDL event timestamp to the submit of the first frame whose border changed, and how many refreshes it took |
| Input → photon | 240 fps camera filming the A button and the screen border; count frames from button bottom-out to the border change; 30 samples per configuration |
| Pacing | `p01-frames-*.csv`: acquire wait, copy, submit times; `emu_step` per refresh (0 = repeated frame, 2 = skipped one); audio queue and underruns |

## Results

| Date | Deck | Panel Hz | Config | Software median / p99 (ms) | Photon median (ms) | Repeats / skips per min | Verdict |
|------|------|----------|--------|----------------------------|--------------------|-------------------------|---------|
| 2026-10-08 | none: macOS M1 Ultra, Metal, `--windowed`, 60 Hz monitor (development smoke run) | 60 | vsync, fif 1, delay 0, 48K | no presses (no input) | — | 250 refreshes in 4 s: 197 new frames, 53 repeats, 0 skips. That is the 50-on-60 beat (1 repeat in 5) predicted by rendering.md §4; nothing else broken | path works on Metal |
