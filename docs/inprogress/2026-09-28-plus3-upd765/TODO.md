# TODO — ZX Spectrum +3 floppy controller (uPD765A)

**Status:** phases 1-4 done (2026-09-28); the protected-title sweep waits for images. PLAN.md row #57.
Design: [technical-design.md](technical-design.md).

## Done
- **Phase 1, controller:** `core/src/emulator/io/fdc/upd765.{h,cpp}`: ports `#2FFD` / `#3FFD`,
  motor on `#1FFD` bit 3, all phase-1 commands, real timing with overrun, units 2/3 aliased to
  drives A/B, TTD blob `PeripheralId::Upd765` (14). 34 controller tests, about 60 ms together.
- **Phase 2, ROM:** the +3 menu reads "128 +3" with "Drives A: and M: available"; CAT of a blank
  disk; SAVE, LOAD and RUN of a BASIC program through the command typer (turbo mode, 35-310 ms each).
- Found on the way: the 128K and +3 editors wait in a loop of their own after a report; the
  command typer now knows it (`reportShown`, input-verification.md §4.2/§4.3).
- The +3 (and the new +2 / +2A models) run at 3.5469 MHz like the 128K; `Core` gave them 3.5 MHz.
- New models `PLUS2` (grey +2: 128K hardware, `plus2.rom`, its editor in input verification §4.2a) and
  `PLUS2A` (the +3 without the uPD765: menu "128 +2A", "Drive M: available"); all three in the
  unreal-qt Machine menu.
- **Phase 3, automation:** `DeviceState::Fdc` reports the uPD765A on the +3 (phase, main status, the
  command in hand with C H R N, ST0-ST2, SPECIFY times, units, drives A/B), so WebAPI `state/fdc`,
  MCP `inspect_state` `fdc`, CLI, Lua and Python all show it; `UPD765::getSnapshot()` reads without
  running deadlines. Blank disks come from one core call, `Emulator::CreateBlankDisk` (format `auto`
  / `unformatted` / `plus3`, owned like a loaded image): the four copies in WebAPI, CLI, Lua and
  Python each leaked the image and knew only an unformatted disk. `.dsk` through `LoadDisk` reads on
  the real ROM (CAT test). The port trace names `#2FFD` / `#3FFD` (+3 only). Recipe:
  `/.recipe/media/insert-disk.md` (+3 section).

- **Phase 4, protections:** READ TRACK (MAME semantics, the consensus of 4 of 6 emulators), SCAN
  (datasheet: no emulator had it complete), and N >= 4 sectors: reads come from the raw track stream,
  so a transfer longer than its field reads on round the index (MAME); the DSK loader keeps the dumped
  length of such a sector in the stream and the save writes it back. Tests: READ TRACK order and
  over-long reads, an N = 6 sector read of 8192 bytes, SCAN equal / low / high / STP 2, the EDSK
  load-save-load of an N = 6 sector.

## Remaining (value order)
1. **Protected-title sweep:** run the protections the reference emulators name (Speedlock +3,
   Alkatraz, Hexagon, Paul Owens, Three Inch Loader) from real EDSK images. Neither the repository nor
   the reference trees hold any; needs images. No per-title patches: they must load from the image.
2. **WRITE DATA to an N >= 4 sector** writes only the model's field (`128 << (N & 3)`); protections
   read such sectors, none is known to write one.
