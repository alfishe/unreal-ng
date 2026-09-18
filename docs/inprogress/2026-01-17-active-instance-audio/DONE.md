# DONE — Active-instance audio (2026-01-17)

**Status:** complete.

## What landed
- Audio follows the active emulator instance (implementation plan + task + walkthrough
  in this folder), integrated into `SoundManager` device selection.

## Evidence
- `core/src/emulator/sound/soundmanager.{h,cpp}` (per-instance audio ownership; later
  refined by the multirate work — [2026-08-17-audio-sync](../2026-08-17-audio-sync/), done).

## Follow-ups
- None.
