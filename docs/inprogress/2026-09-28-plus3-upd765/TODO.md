# TODO — ZX Spectrum +3 floppy controller (uPD765A)

**Status:** phases 1-2 done (2026-09-28); phases 3-4 open. PLAN.md row #57.
Design: [technical-design.md](technical-design.md).

## Done
- **Phase 1, controller:** `core/src/emulator/io/fdc/upd765.{h,cpp}`: ports `#2FFD` / `#3FFD`,
  motor on `#1FFD` bit 3, all phase-1 commands, real timing with overrun, units 2/3 aliased to
  drives A/B, TTD blob `PeripheralId::Upd765` (14). 34 controller tests, about 60 ms together.
- **Phase 2, ROM:** the +3 menu reads "128 +3" with "Drives A: and M: available"; CAT of a blank
  disk; SAVE, LOAD and RUN of a BASIC program through the command typer (turbo mode, 35-310 ms each).
- Found on the way: the 128K and +3 editors wait in a loop of their own after a report; the
  command typer now knows it (`reportShown`, input-verification.md §4.2/§4.3).


## Remaining (value order)
1. **Phase 3, automation:** `state/fdc` and MCP `inspect_state` report the uPD765 on the +3 (today
   they describe the WD1793); `disk/{drive}/create` with a `plus3` format; `.dsk` in `LoadDisk` on
   the +3 checked end to end; a recipe in `/.recipe/media/`.
2. **Phase 4, protections:** READ TRACK and SCAN (they take their parameters and answer IC=01 + MA
   today), N >= 4 sectors (Speedlock +3; the model stores `128 << (N & 3)`), a sweep over the
   protected-title lists from the reference emulators.
3. **Model clock:** `Core` gives the +3 3.5 MHz through its `default` case; the real machine runs at
   3.5469 MHz like the 128K. Affects every +3 timing, not only the disk.
