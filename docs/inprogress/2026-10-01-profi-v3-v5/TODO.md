# Status: TODO

Profi v3 and v5 as two machines (`PROFI3` new, `PROFI` = v5). Phases 1-7 implemented and **on master** (the branches
`profi-v3-v5`, `profi-v3-v5-design`, `profi-xt-kbd` and `profi-ay-clock` are merged; the first three deleted). Plan: [design.md](design.md) section 8.

## Done
- [x] Cross-check of every xpeccy-plus v3/v5 claim against the other emulators, Karabas-Pro, the Black_Cat table and
  the board manuals: [cross-check.md](cross-check.md)
- [x] Factory firmware found, verified by CRC32 and shipped in `data/rom/profi/` (provenance in
  `data/rom/README-ROMS.md`): [roms.md](roms.md)
- [x] Requirements, design, test plan

## Remaining
- [x] E1: sync-PROM decode ([tools/machines/profi/syncprom/profisync.py](../../../tools/machines/profi/syncprom/profisync.py)), including the v5 DD53 load value; settles Q1, Q2
- [x] E1b: the INT flip-flop (INT length) and the DS80 CPU clock: done in the hi-res work ([design-hires.md](design-hires.md))
- [x] E2a: both port decoder PROMs obtained (v4/v5 transcribed from two manuals, v3.2 dumped by MDESK)
- [x] E2b: decoder PROM wiring and port map ([decoder-prom.md](decoder-prom.md)); settles Q7, P7, P8, P9, P13; the A2 input traced on both boards; unreal-ng's decode matches the v5 PROM everywhere
- [x] E3: v3.2 schematic study, turbo and floating bus (settles Q3, Q4):
  [research-profi-v3-turbo-floatbus.md](research-profi-v3-turbo-floatbus.md), scripts in
  [tools/machines/profi/turbomodel/](../../../tools/machines/profi/turbomodel/README.md)
- [x] E4: v5.06 netlist study, the /REDYT wait pattern (settles Q9): [research-profi-v5-wait.md](research-profi-v5-wait.md),
  the gate-level model in [tools/machines/profi/waitmodel/](../../../tools/machines/profi/waitmodel/README.md)
- [x] Phase 1 (branch `profi-v3-v5`): `MM_PROFI3` (short name `PROFI3`, alias `PROFI5` for v5), `IsProfiModel()`,
  `ProfiBoard` (`core/src/emulator/ports/models/profiboard.h`), `[ROM] PROFI3`, `data/configs/profi3`, ROM roles,
  `[ULA] ProfiMonochrome` now read (it never was), v3 always monochrome; CLI / WebAPI (`profi_board`,
  `profi_sync_prom`) / MCP resource / Qt menu / recipes / AGENTS.md. Both factory v3 BIOSes reach their menu
  (`profi3_boot_test.cpp`). Not done: launching the BIOS menu entries (also open on v5)
- [x] Phase 2 (branch): v3 port set through `ProfiBoard` (no extended map, palette, GX0, RTC, IDE; TTD = paging only);
  AY decodes A13 on both boards; the joystick at `#1F` was already on master. Tests in
  `portdecoder_profi_test.cpp` (`ProfiV3PortDecoder_Test`, `AyDecodesA13`, `DecodeMatchesThePortDecoderProm` on
  both boards: every port low byte x mode against the PROM dumps) and `profiboard_test.cpp`
- [x] Phase 3 (branch): `[PROFI] SyncProm=` (`0a1d`, `samx6`, `fb0579b6`, `v503`; empty = the board's own), v3
  69888 T / INT 12580 T, v5 69888 T / INT 14368 T (was 12580); the INI's `intstart` / `intlen` removed so they no
  longer override it. No Profi TTD fixtures exist, so none were re-recorded. Tests in `int_timing_test.cpp`
- [x] Phase 3b (branch): v5 video WAIT in `ProfiWaitOverlay` (`core/src/emulator/memory/profi/`), `[PROFI] WaitPhase`,
  `WaitConfig`, `RomWait`; tests in `profiwaitoverlay_test.cpp`
- [x] Phase 4 (branch): v3 floating bus (`PortDecoder_Profi::FloatingBusV3`); tests in `portdecoder_profi_test.cpp`
- [x] Phase 5 (branch): the TURBO front-panel switch on both boards (`FrontPanelSwitch::Turbo`, `[PROFI] Turbo`), the
  v3 turbo waits and the HLD hold, the v5 turbo approximation
- [x] Phase 6 (branch): CLI `switch`, WebAPI `/switches` + OpenAPI, MCP resource, Lua / Python `get_switch` /
  `set_switch`, Qt Machine > TURBO Switch; TTD records the switch (`TTDInputKind::FrontPanelSwitch`, ProfiPaging
  byte 33); the Qt status line now shows the clock of a machine that never changes it. No v3 TTD fixture: no Profi
  fixtures exist
- [x] Shock Megademo: the WAIT makes its opening raster work as on the 48K (Gromov); Floating Spy on v3 reads #FF in
  the border as expected ([test-programs.md](test-programs.md))
- [ ] Shock Megademo on v5: a seam in the left border and slanted top stripes against the 48K; likely the INT
  position (14368 T, 32 T later than the 48K). Needs a photograph of a real v5
- [ ] Qarx and Academy: running, effects not yet judged against Gromov's descriptions
- [x] Phase 7 (branch): the v5 open items, from the 5.06 netlist and the 5.0 album
  ([research-profi-v5-open-items.md](research-profi-v5-open-items.md)): the palette rule stays (DS80, A7=0, A0=0; the
  manual's "CP/M + BLOCK" sentence contradicts its own schematic); the CP/M switch holds #DFFD at #00
  (`FrontPanelSwitch::Cpm`, `[PROFI] CpmSwitch`); at power-on the BIOS then starts Spectrum 128, the manual's
  behavior (`CpmSwitchAtPowerOnStartsSpectrum128`; no other emulator models the switch); `[PROFI] DffdDecode=emulators|v50|v506`
- [x] The third crystal (ZQ3, 16-24 MHz) clocks the CPU only in DS80 (4-6 MHz, 8-12 in turbo): modeled with the
  hi-res frame timing (`[PROFI] ZQ3MHz`, [design-hires.md](design-hires.md))
- [x] Emulated test programs ([test-programs.md](test-programs.md)): Tact Meter reproduces the v3.2 turbo
  measurement to four digits; TEST 4.30 reads the expected ports, frame and memory class
- [x] Keyboard (branch `profi-xt-kbd`, design section 9): `[PROFI] Keyboard=Matrix|XT|XTTable` (v5 XT, v3 Matrix);
  the MCS-48 core (`core/src/emulator/cpu/mcs48/`), `ProfiXtKbc` running the reconstructed PROFI-XT firmware
  (`data/rom/profixt/`), the table engine (`profixtkeymap`), `pckey::XtSet1Bytes`, `#FE` bit 5 = KD5 on v5, Auto route
  = the controller alone, the automation reverse map, TTD id 44, all automation surfaces + Qt, the 8035 simulator in
  `tools/machines/profi/xtkbd/`. Tests: `mcs48_test.cpp`, `profixtkbc_test.cpp` (BIOS 2.0 reads F1 as 75h)
- [ ] Keyboard: a clean re-dump of the PROFI-XT v1.27 EPROM (confirms the 5 reconstructed bytes 02Eh-032h)
- [ ] Keyboard: the v3.2 on-board XT pads (`#FE` bit 7 + the XT clock on /INT) - not built until software for it turns up
- [ ] Keyboard: the native v5 mechanical keyboard's EXT / MODE / GRAF keys (two-contact combinations, sheet 9)
- [x] H1 (branch `profi-hires-xt`): the fractional clock ratio (`hw_clock_den`) through every CPU / base T conversion;
  no machine changes ([design-hires.md](design-hires.md))
- [x] H2a: hi-res CPU clock (v3 3 MHz, v5 ZQ3 / 4, `[PROFI] ZQ3MHz`), frame and INT from the sync PROM's upper half,
  switched at the #DFFD write; the Profi TTD time grid (`TtdClockUnits`) - it was 1, so turbo halves were lost
- [x] H2b (branch `profi-ay-clock`): AY clock 1.5 MHz in hi-res (v3 always, v5 unless `[PROFI] AyClock=new`): a
  run-time AY clock on the TurboSound device, switched at its T-state, TTD-exact without new bytes, `psg_clock_hz`
  in the AY state ([design-hires.md](design-hires.md) 3.1)
- [x] The shipped Profi configs fit `TurboSound=Single` (the boards' one AY) instead of TSFM, so the hi-res AY clock
  is what users hear
- [ ] TSFM, if chosen, keeps 1.75 MHz in hi-res; the DSD native tap assumes 218.75 kHz
- [x] TTD across a v3 hi-res switch replays exactly: the "divergence" was the test's stop condition on z80.t, which
  a v3 entering hi-res rescales down (6/7); positions compare in TTD units (`V3HiresFrameSwitchesReplayExactly`)
- [x] H3: hi-res waits (v5: 0-1 T around each video request at ZQ3/4, 1-3 T in turbo, ROM one-shot; #7FFD bit 5
  runs the requests all line long; v3: none at 3 MHz, the 2/3 rule in turbo) and the v3 hi-res floating bus (the
  cell's two bytes by tick half; which latch holds which page is open)
- [x] H4: create-time `zq3_mhz` / `ay_clock` (WebAPI + OpenAPI, CLI `--profi-zq3` / `--profi-ay-clock`, MCP),
  `profi_hires_cpu_hz` / `profi_zq3_mhz` / `profi_ay_clock` in the paging state (WebAPI, CLI, Lua, Python), the
  machine list's `speed_multiplier` as a fraction (1.43 in hi-res); recipe verified live
- [x] The forum's hi-res speed-test figures: BIOS 2.0's "Тест быстродействия" reads 1.50 and 2.45 (TURBO) on the
  emulated v5, as termik's real 5.06 with a 20 MHz ZQ3 did (zx-pk 21644 p.11); without the waits it reads 1.65 / 3.35
  (`ProfiBoot_Test.BiosSpeedTestReadsWhatARealBoardReads`)
- [x] A/B of H1-H4 and the AY clock: no measurable cost on 48K / Pentagon / Scorpion / TurboSound ([design-hires.md](design-hires.md) 5)
- [x] CP/M on v5: images per board in `testdata/machines/profi/cpm/` (README there); the Kondor, HC and DN disks boot
  from "Загрузка системы CP/M" (`CpmBootsFromTheKondorSystemDisk`). The old `CPM.UDI` stops at its loader's trap:
  its LSTP driver loads `KOI8.FNT`, which the disk lacks (an inconsistent user disk, not an emulation bug)
- [x] CP/M on v3: Klug CP/M 2.3 boots from the Kramis V0.3 "Profi-DOS" entry (checked 2026-10-03; the disk is kept
  outside the repository, testdata has one disk per check: SP-DOS for the v3, `SpDosBootsToItsShell`). It
  needs TR-DOS 5.04T: V0.2's TR-DOS 5.03 double-steps on its 5 x 1024-byte disk (traced: the FDC follows TR-DOS's
  seeks; the same on a v5 with V0.2, while a v3 with BIOS 2.0 boots it) - software, not the board; `PROFI3` defaults to V0.3 since 2026-10-03
- [x] A native SP-DOS disk: found in KLUG's BBS archive (2005, area PROFI: UNICOPY, COPYK30, TERMINAL, BIOS by
  V. Tereschenko); they boot on `PROFI3` from the Kramis "Profi-DOS" entry to the SP-DOS shell
- [x] SP-DOS disks on `PROFI` (v5, BIOS 2.0) stopped at "Загрузка системы CP/M...": every read of the hi-res
  loader ended in Lost Data. The VG93 took the CPU clock as its time base, which steps back at every frame boundary
  when the CPU runs faster than 3.5 MHz (v5 hi-res, turbo). Fixed: the Profi decoder sets the base-clock time base
  for the VG93 and the tape, as the Sprinter does ([design-hires.md](design-hires.md) section 4); they now boot on both
- [x] SP-DOS system disk in testdata (`cpm/sp-dos/unicopy-sp-dos.td0`), `SpDosBootsToItsShell` on both boards
- [x] PQ-DOS and ROM BIOS Plus (Vadim): both expect the extended port map while the SYS ROM runs, as Karabas Pro
  decodes it; `[PROFI] ExtPorts=sys` adds that variant (default `cpm`, the 5.0 PROM, which BIOS 1.0 / 2.0 need).
  With it, BIOS Plus 0.32 finds the FDC, both drives, the RTC and the AY, and PQ-DOS 2.1 boots to `A:\>`
  ([software-zoo.md](software-zoo.md) sections 5, 6)
- [x] The extended-map 8255 (`#87..#E7`) and the COM port (`#8F..#EF`): emulated 2026-10-04 (`Ppi8255`, `Pit8253`, `Usart8251`, [2026-10-04-profi-plus](../2026-10-04-profi-plus/TODO.md)); the board test reports Ok
- [ ] PQ-DOS on `PROFI3` (its ROM-BIOS emulator): loops after programming the 8255 (`#52B0..#52C1`)
- [x] What the Profi+ decodes in the SYS ROM state: Djoni's V0.03 PROM opens the extended map there, the stock 5.06 CPLD does not ([2026-10-04-profi-plus](../2026-10-04-profi-plus/design.md) section 2.1); `PROFI-PLUS` uses `ExtPorts=v003`, the default stays `cpm`
