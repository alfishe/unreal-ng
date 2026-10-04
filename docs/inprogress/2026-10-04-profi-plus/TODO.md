# Profi+ (`PROFI-PLUS`): TODO

Design: [design.md](design.md). Started 2026-10-04 on branch `profi-hires-xt` (worktree `scratch/wt-profi`).

- [x] Board test of ROM BIOS Plus 0.32 traced: FDC, drives, RTC, AY pass with `[PROFI] ExtPorts=sys`; parallel
  (8255), serial (8253 + 8251) not emulated; HDD needs an image
- [x] Research: BIOS Plus 0.241-0.41h1 found (0.40+ for DN), port list and chips documented, the V0.03 decoder
  PROM and the 5.06 CPLD sources found, PQ-DOS HDD image with DN 2.0.16 (design.md section 2.1)
- [ ] P0 decode the V0.03 PROM, compare with `ExtPorts=sys`
- [ ] The 8253 / 8251 clock on the board (from the COM schematic in the 5.06 album)
- [x] P1 `Ppi8255` + Profi routing (normal #1F..#7F and extended #87..#E7; Covox keeps following every B / C write;
  TTD PeripheralId 50); BIOS Plus "Parallel interface: Ok"
- [ ] P2 `Pit8253` (started: header draft kept in `scratch/profi-plus-wip/timer/`)
- [ ] P3 `Usart8251`, `ISerialPeer`, `#B3`
- [x] P4a `PROFI-PLUS` variant (Machine menu, every create path) with ROM BIOS Plus 0.41h1 in `data/rom/profi/`;
  checked by hand: PQ-DOS 2023 HDD image boots into DOS Navigator 2.0.16
- [ ] P4b a `ProfiPlusBoot_Test` (board test, PQ-DOS floppy boot; the HDD image is 2 GB - a cut-down image for tests)
- [ ] Video recording of hi-res (512x240) frames is stretched horizontally (1216x576 from the 608x288 buffer,
  no pixel-aspect correction like the screen's); owner report 2026-10-04, `~/Movies/unreal_20261004_120759.mp4`
- [ ] P5 TTD, automation, recipe, docs
- [ ] P6 DOS Navigator (needs BIOS Plus 0.40+)
