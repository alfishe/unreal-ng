# Status: TODO

Profi v3 and v5 as two machines (`PROFI3` new, `PROFI` = v5). Design complete; no code yet.
Plan: [design.md](design.md) section 8.

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
- [ ] Phase 1: `MM_PROFI3`, `ProfiBoard`, ROM / config plumbing, v3 boots the Kramis BIOS
- [ ] Phase 2: v3 port set and the shared fixes (AY A13, joystick at `#1F`)
- [ ] Phase 3: `SyncProm=` table, per-board defaults (v5 INT moves to 14368 T: re-record the v5 TTD fixtures)
- [ ] Phase 3b: v5 video WAIT (`ProfiVideoWaitOverlay`)
- [ ] Phase 4: v3 floating bus
- [ ] Phase 5: turbo switch for both, `ProfiTurboOverlay` for v3
- [ ] Phase 6: automation, Qt, docs, TTD fixture
- [ ] Phase 7: v5 open items (palette gate, 15 MHz, CP/M boot switch)
