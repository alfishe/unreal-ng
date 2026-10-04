# Profi+ (`PROFI-PLUS`): TODO

Design: [design.md](design.md). Started 2026-10-04 on branch `profi-hires-xt` (worktree `scratch/wt-profi`).

- [x] Board test of ROM BIOS Plus 0.32 traced: FDC, drives, RTC, AY pass with `[PROFI] ExtPorts=sys`; parallel
  (8255), serial (8253 + 8251) not emulated; HDD needs an image
- [x] Research: BIOS Plus 0.241-0.41h1 found (0.40+ for DN), port list and chips documented, the V0.03 decoder
  PROM and the 5.06 CPLD sources found, PQ-DOS HDD image with DN 2.0.16 (design.md section 2.1)
- [ ] P0 decode the V0.03 PROM, compare with `ExtPorts=sys`
- [x] The 8253 / 8251 clock on the board: 1.5 MHz, from BIOS Plus's baud table (divider x baud = 1 500 000; the
  8251 at x1); the 5.06 album's COM schematic would confirm it
- [x] P1 `Ppi8255` + Profi routing (normal #1F..#7F and extended #87..#E7; Covox keeps following every B / C write;
  TTD PeripheralId 50); BIOS Plus "Parallel interface: Ok"
- [x] P2 `Pit8253` (modes 0-5, binary / BCD, latch / LSB / MSB / both, the latch command; lazy closed-form time
  from base T; TTD PeripheralId 51); BIOS Plus reads counter 1 back
- [x] P3 `Usart8251` (async mode, internal reset, mode / command / status; TTD PeripheralId 52 with the `#B3` latch),
  the machine's own serial port (`SerialPort::Profi8251`, `ComPort=` peers, `MachineSerialPeer` in TTD), `#B3`
  (D0 latch; RI / DCD reads); BIOS Plus "Serial interface: Ok"; loopback round trip at 9600 baud
- [ ] The COM interrupt (`#B3` D0: RST `#20` receive / `#28` transmit through an interrupt controller): needs a
  vectored device INT (IM 0 RST) and an 8251 time event between accesses; not modeled (the latch is kept)
- [ ] What the 8253's counters 1 / 2 drive on the board; the polarity of `#B3`'s RI / DCD bits
- [x] P4a `PROFI-PLUS` variant (Machine menu, every create path) with ROM BIOS Plus 0.41h1 in `data/rom/profi/`;
  checked by hand: PQ-DOS 2023 HDD image boots into DOS Navigator 2.0.16
- [x] P4b `ProfiPlusBoot_Test`: the board test reports FDC, parallel, serial, RTC and the sound chip Ok (BIOS result
  byte `(IY + 2)`); the COM port in the network manager; a TTD seek replays the 8253 / 8251 exactly
- [ ] P4c PQ-DOS boot tests from a floppy and from a hard disk (the HDD image is 2 GB - a cut-down image for tests)
- [x] Video recording of hi-res (512x240) frames came out stretched horizontally (1216x576 from the 608x288 buffer)
  and the other mode's frames were dropped after a switch; owner report 2026-10-04. Fixed: a full-frame Profi recording
  is the screen's 352:288 window x scale in both modes (`RecordingManager`, `RecordsProfiDisplay`;
  `RecordingManager_Test.ProfiFramesKeepTheScreensWindowInBothModes`), checked with a GIF and an H.264 recording of
  PROFI-PLUS in DOS Navigator: 704x576 at scale 2
- [ ] P5 TTD, automation, recipe, docs
- [x] P6 DOS Navigator: 2.0.16 runs from the PQ-DOS 2023 HDD image on PROFI-PLUS (BIOS Plus 0.41h1)
- [x] Triage of the 116 programs on the PQ-DOS HDD image ([analysis](../../disasm/machines/profi-plus/pqdos-hdd-programs/README.md)):
  SP.COM logo = program bug ([sp-demo](../../disasm/machines/profi-plus/sp-demo/README.md)); FLINES / WERT# / PINGVIN# need
  BDOS 98, MAT hits BDOS 9 + NUL (both PQ-DOS behavior)
- [ ] JAZZY (runs data as code under PQ-DOS) and COLUMNS (open of a missing file returns "found") root causes; `S_MIN'.COM`
  by hand; confirm PQ-DOS findings with a second source
