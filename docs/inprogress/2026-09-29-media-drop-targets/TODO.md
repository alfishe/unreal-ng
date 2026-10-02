# TODO — media drop targets (2026-09-29)

**Status:** design written, no code. PLAN row **#77**. One core analysis of which slots take a
file, used by every entry point; a Qt slot chooser (menu at the cursor, labeled drop zones on a
1.5 s hold), a red refusal when no slot takes the file. Design: [design.md](design.md).

## Next
- [x] Decided 2026-09-29: an ISO goes only to a unit that is a CD-ROM drive (no switching on a drop); the hold is a fixed 1.5 s; several targets are the user's choice (slot chooser menu on a quick drop, labeled drop zones with device icons on the hold), the floppy's drive A with autostart the only shortcut.
- [x] M0 (2026-10-01): "ISO dropped on the window lands in `fdd.a`" not reproduced - every path (Qt drop / File > Open / command line, CLI, WebAPI, MCP, model switch) ends in the floppy probe, which refuses an ISO; test `AnIsoGoesOnlyToACdRomDrive`. Found and fixed on the way: an ISO / HDD / SD image dropped on the window was ignored even with a slot for it (now `insert auto`, no machine started for it, a message when no slot takes it; part of M3), and `insert auto` put an SD image into the NeoGS card before the Z-Controller (now the `primary` slot wins; test `AutoPutsACardIntoThePrimarySdSlot`).
- [x] M1 (2026-10-01): `MediaTargets` (`core/src/emulator/media/mediatargets.{h,cpp}`: Classify, Plan, Apply) in core, `MediaControl::ChooseSlot` on top (the chooser's first entry, the alternatives in the report; design §4.3 "As built"); the TR-DOS probe capped at 2 x 86 x 16 x 256 bytes; tests `MediaTargets_Test` (§7: Classify per evidence row, Plan on Pentagon / ZX-Evo / 48K / no machine, the order, Apply).
- [x] M2 (2026-10-01): `media targets <path>` on every surface - the `targets` verb of `MediaControl` (reply fields `file`, `targets`, `default`, `refusal`; a refusal is an answer, `ok` stays true), CLI `media targets`, WebAPI `GET /media/targets?path=` + OpenAPI (`MediaFileClass`, `MediaTarget`), MCP `media` action `targets`, Lua / Python `media_targets(path)`; docs: features/media.md, the CLI / Lua / Python interface docs, the MCP README, the media-slots recipe. `media insert auto` already refuses what no slot takes (M1). Tests `MediaControl_Test.TargetsListWhereAFileCanGo`, `McpSlots_Test.TargetsBecomesAQuery`.
- [x] M2b (2026-10-01): `media insert auto` with several targets answers `ambiguous-slot` and names them (floppy drives excepted: the first empty drive); test `MediaControl_Test.AutoNamesTheSlotsWhenSeveralTakeAFile`.
- [ ] M2: `media targets` on every surface; `media insert` refuses what no slot takes.
- [x] M3 (2026-10-01): Qt `loadFile` on the plan (`placeMedium`: one target at once, several through a slot chooser menu at the cursor, the command line and the CLI's `open` take the first and log it; a floppy / tape with no machine starts one as before, a CD / hard-disk / card image never does); the drop area turns red with the reason in the status bar while a file nothing takes is dragged (blue otherwise, with the target or "choose a slot"); a drop on a media panel row checks the row's slot takes the file (an empty IDE unit may still swap its drive); a folder is also a tape candidate; File > Open lists CD, hard-disk and card images.
- [ ] M4: the slot chooser component - device icons in the menu (M3 shipped a plain one), labeled drop zones with device icons on a 1.5 s hold, File > Insert medium... dialog, multi-file sets.
- [x] Boot compatibility advisory (§10, decided + implemented 2026-09-30): `blockadvisory.{h,cpp}`,
      `DescribeBlockLayoutMismatch` wired into `MediaManager::Insert`, keyed by slot tags
      (`ide`+`hdd` vs `sd`); rides the existing `report` field, no new endpoint. Root-caused from a
      real repro (`testdata/machines/tsconf/wildcommander/`, `testdata/machines/baseconf/hdd-images/`).
      Folded into `Classify` in M1: sector 0 (FAT boot sector or MBR, `ClassifySectorZero`) is evidence for a card or hard-disk image.

## Pointers
- Media manager: [storage-manager](../2026-09-28-storage-manager/TODO.md), user reference [media.md](../../features/media.md).
- IDE / CD units and the `device=cdrom` switch: [ide-atapi](../2026-09-28-ide-atapi/TODO.md).
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — #77.
