# Look-Ahead / Run-Ahead — Prior Art

**Created:** 2026-09-27
**Sources:** local emulator collection `/Volumes/TB4-4Tb/Projects/emulators/github`
(paths below are relative to it). Read-only survey.

Only four codebases in the collection reduce input lag by running ahead:
xpeccy-plus, RetroArch, Mesen2 and Spectral. BizHawk has related building
blocks. No run-ahead was found in upstream Xpeccy, Zero-Emulator, fceux,
nestopia, Gearsystem, amiberry or WinUAE.

---

## 1. xpeccy-plus (ZX Spectrum, dotkoval fork of Xpeccy)

Commit `101042b1` (2026-09-09) "Run ahead, to make the machine feel quicker".
The same snapshot engine later powers fast-load rollback (`1e7e1045`) and
rewind (`6294d2cd`).

**Loop** (`src/ethread.cpp:466-495`), at every real frame end:
1. `xstate_save`, then run `conf.emu.runahead` (1 or 2) frames with
   `xstate_run_frame` — a plain `compExec` loop with a 200 000-opcode guard
   (`src/libxpeccy/xstate.c:251-259`).
2. Apply the anti-flicker filter to that ahead frame and show it.
3. `xstate_load` back to the real frame boundary; emulation continues.

Every shown frame costs N+1 emulated frames. Nothing speculative is kept
between frames.

**Snapshot** (`xstate.c/.h`): not a file format. A list of up to 40
`{pointer, size}` ranges inside the live machine, memcpy'd into one buffer:
computer struct, CPU, RAM up to the RAM mask, video, beeper/AY/FM, GS,
disk/IDE/SD controllers. Excludes input (so input survives rollback), tape,
media contents, ROM, breakpoint maps. 168 KB on a 128K machine, 4.2 MB on
TSConf. Relies on struct layout (a comment warns that fields placed between
certain members are silently skipped). The FM chip was initially missed
because it lives behind a pointer — the rollback made it process register
writes twice and audio diverged after 3.3 s (`256afa5a`).

**Side effects:** one global `x_runahead` flag. Breakpoints and heat-map
counting skip; floppy/HDD/SD writes return early. Refuses to run ahead while
the tape runs, during RZX playback, or while an FDC command is in flight
(`xstate_safe`). Also stands aside in fast mode, pause, debugger, rewind,
autostart.

**Audio:** ahead frames never call `sndSync`, so all sound comes from real
frames; the picture is N frames ahead of the sound ("the sound stays a frame
behind").

**Input:** no prediction and no rollback on change — ahead frames use the input
as it is at the frame boundary.

**Verification:** snapshot, run 6 frames, hash every range, roll back, re-run,
compare. No divergence on all 12 ZX profiles, a TR-DOS load and a TSConf demo.

**Known waste:** the real frame is fully rendered although never shown
(`ethread.cpp:350-356`).

---

## 2. RetroArch (`runahead.c`, `runloop.c`) — three modes

| Mode | How | Cost | Audio heard |
|---|---|---|---|
| Single instance | real frame, serialize, N frames with held input (only the last one shown and heard), restore | N+1 frames every host frame | from the speculative frame → discontinuities |
| **Second instance** | a second copy of the core (`dlopen` of a copied library, no shared globals). Main core: audio on, video off. Secondary: re-synced (save main → load secondary → run N−1 catch-up frames) **only when input changes or a resync is forced**; otherwise it just advances one frame per host frame and shows video | ~2 frames per host frame; burst on input change | real core → clean |
| Preemptive frames | ring of the last N states; when input changes, load the state N frames back and replay with the new input; unchanged input costs one serialize per frame | ~1 frame + serialize; N+1 on change | real frame |

Other notes:
- The frontend tells the core how the state will be used
  (`RUNAHEAD_SAME_INSTANCE` allows pointer shortcuts; `libretro.h:5658-5706`).
- Per-core support levels BASIC / SERIALIZED / DETERMINISTIC; a quirk flag
  marks cores with incomplete states. Any save/load failure disables the
  feature.
- Reset and state load force a resync; so does a frame-count gap (menu).
- Mouse deltas are stored at poll time so replays do not consume them twice.
- Documented pitfall: jitter when N exceeds the game's own internal lag.

## 3. Mesen2 (`Core/Shared/Emulator.cpp:204-233`)

Single instance, always re-runs, N ≤ 10. One global `_isRunAheadFrame` flag
suppresses video decode, PPU pixel work (a speed win), rewind capture, movie
recording, the frame limiter sleep. Pitfall: the host audio resampler, EQ and
reverb still run on discarded samples, so host-side filter state advances on
speculative audio. Auto-disabled with the debugger, rewind, or speed ≠ 1×.

## 4. Spectral (ZX Spectrum, `src/app.c:2022-2049`)

Single instance, 1 or 2 frames, UI states the cost honestly ("3x CPU cost").
State is a raw struct copy of globals, pointers kept (same-instance only).
Comment: `#define FULL_QUICKSAVES 1 // 0 breaks run-a-head`. Pitfall: the FLASH
phase toggles once per *host* frame outside the machine frame, so speculative
frames do not advance it.

## 5. BizHawk

No run-ahead. Relevant: Lua "invisible emulation" (save in-RAM state, emulate
forward with sound/render/rewind off, optionally hack memory, show, load back)
— the closest analogue to *look-ahead for analysis*. Rewind storage keeps
word-level deltas between consecutive states (`rewind/ZeldaWinder.cs`).

## 6. unreal-ng's own earlier proposal

`docs/inprogress/2026-09-24-core-performance/unreal-ng-input-latency-and-game-mode.md`
§7 already proposes a raw uncompressed in-memory rollback ring (< 0.2 ms per
snapshot/restore; headless frame ≈ 0.37 ms), preemptive frames for Game Mode,
suppression via `setSynthesisSuppressed`, coprocessor quiescence gating as a
prerequisite, and a guest-lag "detect" helper. This design builds on it.

---

## 7. Lessons adopted

| # | Lesson | Source |
|---|---|---|
| L1 | Keep the canonical timeline untouched; speculate in a **separate instance**, re-synced only on divergence | RetroArch second instance |
| L2 | Snapshots must be **complete**; a missing field (FM behind a pointer, FLASH phase) breaks it silently → a run-restore-run hash test per machine and per peripheral | xpeccy-plus, Spectral, libretro quirk flag |
| L3 | Same-process raw memcpy snapshots, no serializer in the hot path | xpeccy-plus, Spectral |
| L4 | One **context-level** speculative role checked at every sink, not scattered flags | Mesen2, xpeccy-plus (and their gaps) |
| L5 | Host-side state (audio resampler, pacing ring) must never see speculative output | Mesen2 pitfall |
| L6 | Prediction = hold the last input; store absolute mouse position | all |
| L7 | Force a resync on reset, state load, seek, media change, frame gap | RetroArch |
| L8 | Stand aside during tape, disk commands, turbo, debugger, TTD replay; degrade gracefully and log | all |
| L9 | Skip work the consumer does not need (e.g. RGBA render when only the meaning plane is consumed) | Mesen2 PPU skip; xpeccy-plus "biggest waste" |
