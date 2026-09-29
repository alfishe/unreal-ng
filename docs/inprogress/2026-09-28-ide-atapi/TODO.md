# IDE + ATAPI — TODO

**Status:** rollout 1 implemented 2026-09-28 on branch `ide-atapi` (P1-P7). Plan and as built:
[implementation-plan.md](implementation-plan.md).
PLAN.md rows **#58** (M6) and **#13a** (Profi IDE).

- [x] P1 disk core: `AtaDevice`, `AtaDisk`, `AtaChannel`
- [x] P2 ATAPI: `AtapiCdrom`
- [x] P3 slots, image formats (ISO, HDF, HDI, VHD), `[HDD]` config, `Core` owns the channel
- [x] P4 adapters: Profi, Nemo (A8, Evo), SMUC, ATM; config schemes per model
- [x] P5 TTD: `AtaChannel` blob (id 17), write barriers
- [x] P6 real firmware: Profi SYS ROM HDD boot and no-drive exit (R1, R2), ERS HDD boot and CD boot (R8, R9), ProfROM + SMUC IDENTIFY (R4)
- [x] P7 `state ide` report on every surface (CLI, WebAPI + OpenAPI, MCP, Lua, Python), Qt status bar LED and tooltip, docs

Follow-ups (not in rollout 1):
- [ ] NedoOS from a Nemo / NemoIDE hard disk image and from a folder (R5): needs a NedoOS HDD image or an HDD-booting build
- [ ] NedoOS reading a CD (R7): which NedoOS build and driver (IDE design Q9)
- [x] ZX-Evo ships its CD drive on the slave (`CD1=1`); the ERS sees the disc ejected (NOT READY / medium not present, retries) and inserted again (UNIT ATTENTION, then boots): `ZXEvoErs_Test.CdBootSeesTheDiscEjectedAndInsertedAgain`
- [x] TTD fixture corpus re-recorded with the IDE board (2026-09-28; the analyzer knows id 17)
- [x] Profi geometry from the `ProfiHiDD` header (LBA 256: 16 x 16, LBA 1008: 16 x 63), else 16 x 16 (IDE design §8.3); `profi_hdd_test` runs without `CHS0`
- [x] Adapter fuzzing: seeded random traffic on every scheme (`IdeAdapter_Test.RandomPortTrafficIsSafe`); the cross-emulator differential harness (IDE design §12.6) stays open
- [x] A unit's drive from the media verbs and the Qt panel: `device=cdrom|disk` on insert / swap (`IdeController::SetUnitKind`)
- [ ] DivIDE paging and automap (the adapter decodes its IDE ports only); TSConf IDE (PLAN #41)
