# MGT +D: hand-over to zx-software-catalog (P6)

What the catalog's work runs can do with MGT images now that the emulator has the MGT +D. The catalog is its own
repository (`../zx-software-catalog`); this note is for its next session, which owns the change there.

## What changed in the emulator

- Two machines, creatable by name on every surface: `48K-PLUSD` and `128K-PLUSD` (the 48K / 128K with the +D in
  `edge.1`). Recipe: [.recipe/machines/plusd.md](../../../.recipe/machines/plusd.md).
- An `.mgt` (or an 819 200-byte `.img`) inserted with `autostart: true` on these machines runs the card's own autostart:
  reset, `RUN`, G+DOS from the disk or from the bundled system disk, then the start command typed from the directory.
  On any machine without the card the insert is refused (`no +D on this machine`, naming the two models), so the
  catalog's old "MGT on a Pentagon shows the 128 menu" case cannot happen any more.
- The insert reply carries `plan`, `command`, `dos`, `basic`; `GET /emulator/{id}/state/fdc` → `autostart` carries
  the stage, the steps with their frames, `finished`, `ok` and the message. MCP `inspect_state fdc` shows the same in
  its text.

## What the catalog should change

1. **Classification.** An MGT-layout image whose used directory entries have types 1-9 is +D (run it); types 16-20 are
   SAM Coupe: keep them out of the runs as `skip:not-zx-format` (the emulator has no SAM). `classify_mgt.py` in this
   folder does the split from the catalog's pass-1 rows; it belongs in the catalog's `tools/`.
2. **The machine.** Run +D images on `128K-PLUSD` first: of the 243 +D images in the pool, 241 run there, 217 also on
   `48K-PLUSD` (the 26 others load a 128K snapshot, need 128 BASIC, or are the 128K version of a title). A second config
   `48K-PLUSD` tells "48K-capable" apart; `basic: "48"` on `128K-PLUSD` is the third (a 128K in 48 BASIC: 237 run).
3. **The verdict from the plan.** `plan` is `run` / `run+load` / `system-disk+load` (the autostart starts something) or
   `mount` (nothing to start: only CODE, or a 128K snapshot on a 48K): `mount` is a `not_started` verdict with the
   reason, not a failure of the image. When the steps end (`autostart.finished`), `ok: false` with the message is a
   `load_failed`.
4. **When loading is over.** Do not wait for the +D to page out: it traps KEY-SCAN, so a BASIC `INKEY$` menu keeps paging
   it in. The corpus run took "the WD1772 idle (not busy, motor off) for 300 frames" as the end of loading; a loader's
   `PAUSE` between two loads stays under that.
5. **Expected non-runners.** Super Mario Fruit Machine 2 (`a9735f4a40b5`) checks `PEEK 23611 = 221` and stops with
   "EPIC FAIL!" on every configuration; MAME 0.289 with the same ROMs stops the same way. Черный Ворон is CODE only
   (`mount`).

## Data

The per-image table of the three configurations (`min`, `runs_on`, `fails_on`) and the 26 that do not run everywhere
are in [TODO.md, P4.5](TODO.md). `PlusDAutostart_Corpus.AllImages` (`UNREAL_PLUSD_CORPUS`, `UNREAL_PLUSD_POOL`,
`UNREAL_PLUSD_CORPUS_OUT`) regenerates the table from the pool.
