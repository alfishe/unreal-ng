# Status: TODO

Profi v3 and v5 as two machines (`PROFI3` new, `PROFI` = v5). Phases 1-3 implemented on branch `profi-v3-v5`
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
- [ ] E3: v3.2 schematic study, turbo and floating bus (settles Q3, Q4)
- [ ] E4: v5.06 netlist study, the /REDYT wait pattern (settles Q9)
- [x] Phase 1 (branch `profi-v3-v5`): `MM_PROFI3` (short name `PROFI3`, alias `PROFI5` for v5), `IsProfiModel()`,
  `ProfiBoard` (`core/src/emulator/ports/models/profiboard.h`), `[ROM] PROFI3`, `data/configs/profi3`, ROM roles,
  `[ULA] ProfiMonochrome` now read (it never was), v3 always monochrome; CLI / WebAPI (`profi_board`,
  `profi_sync_prom`) / MCP resource / Qt menu / recipes / AGENTS.md. Both factory v3 BIOSes reach their menu
  (`profi3_boot_test.cpp`). Not done: launching the BIOS menu entries (also open on v5)
- [x] Phase 2 (branch): v3 port set through `ProfiBoard` (no extended map, palette, GX0, RTC, IDE; TTD = paging only);
  AY decodes A13 on both boards; the joystick at `#1F` was already on master. Tests in
  `portdecoder_profi_test.cpp` (`ProfiV3PortDecoder_Test`, `AyDecodesA13`) and `profiboard_test.cpp`. Not done: the
  PROM-table test over all 256 ports x modes (tdd-plan section 2) - the Python check covers v5 today
- [x] Phase 3 (branch): `[PROFI] SyncProm=` (`0a1d`, `samx6`, `fb0579b6`, `v503`; empty = the board's own), v3
  69888 T / INT 12580 T, v5 69888 T / INT 14368 T (was 12580); the INI's `intstart` / `intlen` removed so they no
  longer override it. No Profi TTD fixtures exist, so none were re-recorded. Tests in `int_timing_test.cpp`
- [ ] Phase 3b: v5 video WAIT (`ProfiVideoWaitOverlay`)
- [ ] Phase 4: v3 floating bus
- [ ] Phase 5: turbo switch for both, `ProfiTurboOverlay` for v3
- [ ] Phase 6: automation, Qt, docs, TTD fixture
- [ ] Phase 7: v5 open items (palette gate, 15 MHz, CP/M boot switch)
