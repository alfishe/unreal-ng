# TODO: storage controllers survey

**Status:** survey written 2026-09-28 (research only, no code). Not yet reviewed by the owner and
tracked as [PLAN.md](../PLAN.md) row #63. The IDE part it measures against was on branch `ide-atapi`
(uncommitted as of 2026-09-28); it is on master since 2026-09-29 (`f5fc5f05`).

## Done

- Overview matrix, recommended order and cross-cutting pieces: [README.md](README.md).
- One file per controller family: [profi-atm-nemo.md](profi-atm-nemo.md),
  [zxevo-baseconf-tsconf.md](zxevo-baseconf-tsconf.md),
  [divide-divmmc-esxdos.md](divide-divmmc-esxdos.md), [zx-next.md](zx-next.md),
  [plus3e-cf.md](plus3e-cf.md), [sprinter.md](sprinter.md), [neogs-sd.md](neogs-sd.md),
  [other-machines.md](other-machines.md).

## Remaining

- [ ] Owner review: agree the order in [README.md](README.md) §3 and the 8 KB window design
      ([divide-divmmc-esxdos.md](divide-divmmc-esxdos.md) §5).
- [ ] Add PLAN rows for what is accepted (DivMMC / DivIDE + esxDOS; +3e 8-bit IDE / ZXCF / ZXATASP),
      or fold them into existing rows (#13a, #41, #59).
- [ ] Verify the items marked **unverified** in the family files (classic DivMMC `#EB` read
      behavior, +3e interface details marked so, Pentagon 1024 IDE scheme).
- [ ] Provision test firmware into `testdata/` with notices: esxDOS 0.8.5 / 0.8.9 ROMs, +3e ROMs.
- [ ] SMUC: see the sibling survey `2026-09-28-scorpion-smuc/` (not covered here).

- [x] 2026-10-08: DivMMC / DivIDE paging and automap built as `DivMmcPaging` (core/src/emulator/io/divmmc, bus overlay + M1 observer, no change to `Memory`); UnoDOS 3.141 boots and lists a folder through `RST 8` on the 48K machine. A read of `#EB` starting an exchange works with UnoDOS. Esxdos itself not run (no ROM here). Left: TTD blob, slot card, reset hook
