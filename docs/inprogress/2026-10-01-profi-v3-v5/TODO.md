# Status: TODO

Profi v3 and v5 as two machines (`PROFI3` new, `PROFI` = v5). Phases 1-7 implemented on branch `profi-v3-v5`
(worktree `scratch/wt-profi`); it goes to master after all phases and tests. Plan: [design.md](design.md) section 8.

## Done
- [x] Cross-check of every xpeccy-plus v3/v5 claim against the other emulators, Karabas-Pro, the Black_Cat table and
  the board manuals: [cross-check.md](cross-check.md)
- [x] Factory firmware found, verified by CRC32 and shipped in `data/rom/profi/` (provenance in
  `data/rom/README-ROMS.md`): [roms.md](roms.md)
- [x] Requirements, design, test plan

## Remaining
- [x] E1: sync-PROM decode ([tools/machines/profi/syncprom/profisync.py](../../../tools/machines/profi/syncprom/profisync.py)), including the v5 DD53 load value; settles Q1, Q2
- [ ] E1b: trace the INT flip-flop (INT length) and the DS80 CPU clock
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
- [ ] The third crystal (ZQ3, 16-24 MHz) clocks the CPU only in DS80 (4-6 MHz, 8-12 in turbo): settled, not
  modeled - the hi-res frame timing is open (design 5.3)
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
- [ ] H2b: AY clock 1.5 MHz in hi-res (`[PROFI] AyClock`, parsed; the AY engine's clock is a compile-time constant)
- [ ] H3: hi-res waits (v5 model rule, v3 turbo) and the v3 hi-res floating bus
- [ ] H4: automation surfaces for ZQ3MHz / AyClock and the hi-res clock in state; recipes; checks against the forum's
  speed-test figures and the CP/M disk
- [ ] A/B of H1 (the screen descale gained a branch)

