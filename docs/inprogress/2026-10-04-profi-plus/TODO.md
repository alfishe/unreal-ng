# Profi+ (`PROFI-PLUS`): TODO

Design: [design.md](design.md). Started 2026-10-04 on branch `profi-hires-xt` (worktree `scratch/wt-profi`).

- [x] Board test of ROM BIOS Plus 0.32 traced: FDC, drives, RTC, AY pass with `[PROFI] ExtPorts=sys`; parallel
  (8255), serial (8253 + 8251) not emulated; HDD needs an image
- [x] Research: BIOS Plus 0.241-0.41h1 found (0.40+ for DN), port list and chips documented, the V0.03 decoder
  PROM and the 5.06 CPLD sources found, PQ-DOS HDD image with DN 2.0.16 (design.md section 2.1)
- [x] P0 decode the V0.03 PROM, compare with `ExtPorts=sys` (2026-10-04): the file is "coded" = the reverse bit order of the v5
  printed table (only that order keeps the TR-DOS VG93 at `#1F..#7F`, as Djoni's `profi-ports-v0.03.pdf` lists);
  `profidecoder.py` takes it as a third argument and compares with the `sys` rule. Differences found and fixed as
  `ExtPorts=v003` (`PROFI-PLUS` uses it): in TR-DOS with ROM14 = 1 the long ports (RTC `#9F #BF #DF`, 8255, IDE
  `#8B #AB #CB`, VG93 `#83 #A3 #C3`) answer beside the short VG93, `#E3..#FF` stay the system register (so IDE `#EB` is
  the system register there); in CP/M mode a left-on DOS latch no longer opens the long map in the SYS ROM state
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
- [ ] P5 TTD, automation, recipe, docs. Done in the branch: the `DeviceState::ProfiPeripherals` report (port map in force,
  8255, 8253 counters, 8251, `#B3` latch) on CLI `state profi`, WebAPI `GET /state/profi` + OpenAPI, MCP aspect `profi`,
  Lua / Python `profi_state()`, the interface docs. The recipe is checked live on the WebAPI and the CLI (MCP / Lua / Python built, not driven live). Open: the Qt panel (debugger plan)
- [x] `profi.ext_ports` is in the TTD config fingerprint (`PortDecoder_Profi::AddTTDBoardSettings`, agreed with the TTD v2
  session 2026-10-04, `ttd-engine` landed): a recording made under another ExtPorts value is refused at load. TTD v2 takes
  PeripheralIds 54-57 (Smuc, EvoAvrVolatile, KeyboardMatrix, RzxPlayback): do not use them for new Profi chips (ours are
  50 / 51 / 52)
- [x] P6 DOS Navigator: 2.0.16 runs from the PQ-DOS 2023 HDD image on PROFI-PLUS (BIOS Plus 0.41h1)
- [x] Triage of the 116 programs on the PQ-DOS HDD image ([analysis](../../disasm/machines/profi-plus/pqdos-hdd-programs/README.md)):
  SP.COM logo = program bug ([sp-demo](../../disasm/machines/profi-plus/sp-demo/README.md)); FLINES / WERT# / PINGVIN# need
  BDOS 98, MAT hits BDOS 9 + NUL (both PQ-DOS behavior)
- [ ] JAZZY (runs data as code under PQ-DOS) and COLUMNS (open of a missing file returns "found") root causes; `S_MIN'.COM`
  by hand; confirm PQ-DOS findings with a second source
