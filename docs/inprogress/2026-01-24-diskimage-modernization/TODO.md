# TODO — DiskImage modernization (2026-01-24)

**Status:** partially done — two phases were delivered by other tracks; the
rest is still planning. README still says "Planning (Questions Pending)".

## Progress
- **Phase 2 (write protection): shipped** at the WD1793/DiskImage level with
  tests — see [`../2026-01-24-wd1793-test-coverage/DONE.md`](../2026-01-24-wd1793-test-coverage/DONE.md).
- **Phase 1 (variable sector sizes / non-standard layouts): superseded** by the
  universal track model (`../2026-09-02-universal-track-model/`, flux-level
  MFM/FM codecs, per-track geometry).
- Format conversion: partially covered by the track model + tape/disk
  inspection APIs (`tape_disk_api.cpp`, `basic_api.cpp`).

## Remaining (value order)
1. **DiskManager** (Phase 4) — centralized disk lifecycle: insert/eject/write-
   protect flows shared by GUI + automation, one ownership point per image.
2. **Safe-save modes** (Phase 5) — save modes with confirmation, `.bak`
   strategy (open question 4), auto-save policy (open question 2).
3. **Change tracking / heatmaps** (Phase 3) — dirty flags per sector/track,
   session heatmap for wear analysis; pairs with disk-inspection endpoints.
4. Multi-instance sharing policy (open question 1) and SD/HDD management
   (open question 5) — decisions still pending, see
   [implementation-plan.md](implementation-plan.md).

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — DiskManager item (T4).
