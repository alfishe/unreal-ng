# DONE — A/V sync & presentation (2026-08-14)

**Status:** complete.

## What landed
- A/V sync presentation model: `[VIDEO] AVSyncDelayFrames` presentation delay aligned
  with the DRC audio ring buffer, so displayed frames and audible audio agree.

## Evidence
- `core/src/emulator/platform.h:505+` (AVSyncDelayFrames), DRC presentation sync in
  `core/src/emulator/sound/soundmanager.cpp` (audio-sync design Fixes 2–3).

## Follow-ups
- None. (The deeper multirate core is [2026-08-17-audio-sync](../2026-08-17-audio-sync/), also done.)
