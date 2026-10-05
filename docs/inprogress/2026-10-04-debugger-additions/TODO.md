# TODO: debugger additions (E1, E6, E4, E5, F4, F5, E8, E2, D9, E7)

Design: [tdd.md](tdd.md). Branch `debugger-additions` (worktree `scratch/wt-snapshot`), from master `b1353dd12`.
Each step ends green (full build with zero warnings, core-tests) and is committed to the branch before the next
one starts. Master only after the owner has seen the result.

| Step | Work | Status |
|---|---|---|
| A0 | Design (this folder) | done 2026-10-04 |
| A1 | E1 port write: core `PortWrite`, `RunAtCoherentMoment`, every surface, Qt, docs, OpenAPI, tests | done 2026-10-04: `PortWrite_Test` (6) + MCP test; mutation check (no out-of-time scope -> the TS-Conf wait test fails; no breakpoint gate -> the breakpoint test fails); WebAPI and CLI live-checked; Lua / Python compile-checked; GDB paging pseudo-registers now go through `PortWrite` too |
| A2 | E6 TS-Conf CRAM / SFILE regions | done 2026-10-04: regions `cram` / `sfile` through `CommitTableWord` (the FM window uses it too); `TsConfMemoryRegions_Test` (5, incl. TTD restore); mutation check (no palette version bump -> the CRAM test fails); WebAPI live-checked (color 1 -> #0000FF); every surface already had regions |
| A2q | Qt: a device memory region view in the debugger (every machine: Sprinter `vram`, TS-Conf `cram` / `sfile`) | done 2026-10-04: toolbar "Device memory" (`DeviceMemoryDialog`: region list, hex view, typed bytes through `DeviceMemory::Write`, overwrite only); `DeviceMemoryDialog_Test` (2) in `unreal-qt-tests` |
| A3 | E4 disk sector write | done 2026-10-04: core `SectorWrite` (data field via `Track::writeSectorData`, refusals with reasons, coherent moment, TTD marker); WebAPI `PUT` + OpenAPI, CLI `disk write`, Lua / Python `disk_write_sector` (0-based like their read), Qt "Disk sector" dialog; `SectorWrite_Test` (4) + `DiskSectorDialog_Test` (2); WebAPI and CLI live-checked |
| A4 | E5 NVRAM | done 2026-10-04: regions `cmos` (every CMOS-clock machine, `Ds12887` owns it, a write is a guest write) and `eeprom` (ZX-Evo AVR, 4 KiB); `DeviceMemory::Regions` adds the clock's regions; `RtcAccess_Test.CmosRegionIsTheSamePath` (4 machines) + `RtcEepromRegion_Test`; the Sprinter / TS-Conf region lists now include `cmos`; WebAPI live-checked on ATM3 (cmos == /rtc/cells, eeprom round-trip) |
| A5 | F4 + F5 run control: async long calls, claim checks | done 2026-10-04: 11 long WebAPI calls on `LongCallPool` (4 threads, same reply); one claim rule in WebAPI (`RunControlHeldReply`) and CLI (`RunControlHeld`); `UNREAL_GDB_PORT` for own test instances; live: status answered in 2 ms during three 40M-T `skip_until`, `stepout` / `run_frames` / `skip_until` / `step` 409 under a GDB `vAttach` (WebAPI and CLI), 200 after it left. Noticed: GDB auto-attach (single instance) takes no claim, only `vAttach` does - not changed here |
| A6 | E8 long-poll on `seq` | open |
| A7 | E2 PC history (A/B benchmark) | open |
| A8 | D9 TS-Conf paging `read_write` | open |
| A9 | E7 label import | open |
| A10 | Merge master, full build + tests, report to the owner | open |
