# TODO — media drop targets (2026-09-29)

**Status:** design written, no code. PLAN row **#77**. One core analysis of which slots take a
file, used by every entry point; a Qt slot chooser (menu at the cursor, labeled drop zones on a
1.5 s hold), a red refusal when no slot takes the file. Design: [design.md](design.md).

## Next
- [x] Decided 2026-09-29: an ISO goes only to a unit that is a CD-ROM drive (no switching on a drop); the hold is a fixed 1.5 s; several targets are the user's choice (slot chooser menu on a quick drop, labeled drop zones with device icons on the hold), the floppy's drive A with autostart the only shortcut.
- [ ] M0: reproduce "ISO dropped on the window lands in `fdd.a`", fix at its layer, regression test.
- [ ] M1: `MediaTargets` (Classify, Plan, Apply) in core; `MediaControl::ChooseSlot` on top.
- [ ] M2: `media targets` on every surface; `media insert` refuses what no slot takes.
- [ ] M3: Qt `loadFile` on the plan (no machine started for ISO / HDD / SD images), red refusal, media panel row validation.
- [ ] M4: the slot chooser component (menu at the cursor, labeled drop zones with device icons on a 1.5 s hold, File > Insert medium... dialog), multi-file sets.

## Pointers
- Media manager: [storage-manager](../2026-09-28-storage-manager/TODO.md), user reference [media.md](../../features/media.md).
- IDE / CD units and the `device=cdrom` switch: [ide-atapi](../2026-09-28-ide-atapi/TODO.md).
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — #77.
