# TODO: Pentagon 2.666 LE

Status 2026-11-18: P0..P5 done, landed on master (8a6481e3e); real software checked
([testdata/machines/p2666/software/README.md](../../../testdata/machines/p2666/software/README.md)). Left: the gcc 16
check of the last changes (fdc report, MCP line, machine catalog) in the day's batch.
See [README.md](README.md).

## Phases

- [x] Owner decisions Q-1...Q-6 (below, 2026-11-11)
- [x] P0: ROM set `data/rom/p2666/`, `data/configs/p2666/unreal.ini`, `MM_P2666` row, factory case, decoder with
      Gluk mapping, power-on upload, read-only ROM pages
- [x] P1: full decoder (DOS machine, `xx77`, two maps, `xFF7`, `x7F7`, `xxBE`, `xxBF`, `7FFD` / `EFF7` rules and
      quirks, FE flag), turbo 1 / 4 / 8 with the I/O slowdown, INT; Beta Disk path A
- [x] POC 051: the board's VHDL in `ghdl` as the timing oracle (CPU clock, I/O stretch, INT, video fetch)
- [x] P2: video modes, geometry, palette, border, text font (FLASH: dropped by the owner 2026-11-12, "not worth
      it"; the shared ZX renderer has none)
- [x] P3: CMOS / RTC (64 MCU cells, `[P2666] CmosFile`), SD (Z-Controller, `sd.zc`), IDE (even registers read FE),
      mouse, joystick, sound (two YM2149 cores, Covox, beeper in one wrapping 8-bit DAC sum, `P2666Audio`): written,
      tests `P2666Devices_Test`; checked: Gluk's clock from the CMOS, FATALL lists the FAT32 SD, NedoOS on SD, mouse
      (`km-test`), both AY chips and the stereo (`aytest-0.2`, `aydetect`); IDE reads fail as on the board (Q-3)
- [x] P4: Beta Disk on the RAM disk (`P2666BetaDisk`, default; `[P2666] BetaDisk=wd1793` = path A), UART
      (`P2666Uart`, machine serial port `p2666-uart`): tests `P2666BetaDisk_Test`, `P2666Uart_Test`; TR-DOS disks
      (timing tests, ATM Turbo disks) boot from the RAM disk
- [x] Test software, headless (`P2666SoftwareProbe_Test.DISABLED_Run`): Gluk + TR-DOS from the RAM disk, Pentagon
      timing tests (`tests.trd`, `stime.scl`), AY / TurboSound tests, FATALL on SD, NedoOS `osp26sd.$C`
      (2026-11-16: `btime` lands on 17985 as the Pentagon; every program of the NedoOS demo card; z80test 1.2a all
      five pass after the Q latch fix on master)
- [x] P5 (done 2026-11-16):
  - [x] TTD: P2666Board (71), P2666Sound (72), P2666BetaDisk (73), P2666Uart (74) + Ds12887 (18) + EvoSdCard (15),
        one field walk per blob (`debugger/ttd/p2666/ttdp2666.*`); tests `P2666Ttd_Test` (every frame replays),
        the corpus baseline and the model-state contract list P2666
  - [x] Snapshots: a 48K / 128K file loads as a Pentagon 128 (`EnterSpectrum128Paging`, clock kept), capture only
        from that Pentagon 128 (`needs: mode:pentagon128`), state transfer family `P2666Board`; a locked `#7FFD`
        maps page 32 + bank at `#C000` (D-3), so the load puts the bank there too (`FinishSpectrum128State`); tests
        `P2666Snapshot_Test`, `MachineStateTransfer_Test.a128kStateMovesIntoTheAtmAndProfiBoards`
  - [x] Automation planes: the `p2666` node (`DeviceState::P2666Paging`) on WebAPI `/state/paging` and
        `/state/memory`, CLI `paging` / `state memory`, Lua / Python `paging_state()`, MCP `inspect_state paging`,
        OpenAPI text, the node in the MCP paging text; MCP resource `unreal://machine/p2666`; the model in the MCP
        model list
  - [x] Disk controller report (2026-11-18): `state fdc` on every plane reports the RAM-disk Beta Disk (no floppy
        controller on the board, manual 2.9): registers, status, `phase` (every level-set flag, `restore+step`),
        INTRQ / DRQ, system register, head, RAM-disk pages, drive A; WebAPI disk info `fdc_state`; MCP text line;
        tests `DeviceStateFdc_Test.P2666ReportsTheRamDiskBetaDisk`, live `test_p2666.py`
  - [x] Machine menus: the Pentagon family of `MachineCatalog` (`PENTAGON`, `P2666`; 2026-11-18): the Qt Machine
        menu, the handheld (Steam Deck) model picker, the machines lists of WebAPI / MCP / CLI / Python / Lua; test
        `MachineCatalog_Test.TheMenuTreeHoldsOnlyCreatableMachines`. Not on the Elgato Stream Deck machine page (a
        hand-made grid, owner not asked)
  - [x] `.recipe/machines/pentagon-2666.md`, `.recipe/_common/machines.md` row, `.recipe/README.md` row,
        `data/rom/README-ROMS.md` entry; slots refdata ZX-BUS note (`ebl`)
  - [x] Linux (gcc) / Windows checks (2026-11-16): gcc 16.2 (CI build-gcc-latest steps, long tier) 0 warnings, 11566
        tests, 0 failures; MSVC 2019 0 warnings after renaming a local `near` in `p2666_timing_test.cpp` (the
        `windows.h` macro, MinGW too), 11519 tests, 0 failures launched from `c:\` as from `C:\` once
        `FileHelper::AbsolutePath` and the test root spell the drive letter upper-case
  - [x] MCP live block: `tools/verification/mcp/test_p2666.py` (2026-11-16: the whole block twice, 34 of 35; only
        `test_surfaces` 50 Hz poll count fails, at load average 190 from other builds, 86..134 polls of 120 by run;
        it does not touch P2666)

## Decisions (owner, 2026-11-11)

Rule: the model is the board as built; nothing is improved. Board bugs are copied; where a fix is obvious it stays
as a commented-out `P2666-FIX <D-n>` block in the code (design §4.1), never enabled.

| ID | Question | Decision |
|---|---|---|
| Q-1 | Beta Disk path | P1 the WD1793 (drives A..D); P4 the RAM-disk emulator becomes the default, WD1793 kept as `[P2666] BetaDisk=wd1793` |
| Q-2 | Base clock | as the HDL: 3.5625 MHz (114 MHz / 32; turbo 14.25 / 28.5 MHz), frame 49.70 Hz |
| Q-3 | Even Nemo IDE registers read `FE` (D-5) | copy the VHDL: even IDE reads do not work; fix recipe commented out |
| Q-4 | 8-bit sound sum wraps (D-11) | copy the wrap; saturating mix as a commented-out recipe |
| Q-5 | SD config read (D-4) | `0xFF` |
| Q-6 | Visible area | the board's crop (HR §1.3) |

Follows from the rule: stereo A right / C left (D-6), joystick bits as the MCU pins (D-7, direction from the
schematic), undefined video modes keep the last colour (D-8), CMOS 256 cells with 64 persisted (D-10).

Still to look up (facts, not decisions): joystick pin directions and stereo channel naming on the schematic
(Appendix 2 of the manual, an image); the exact INT-to-pixel offset (VHDL oracle POC).
