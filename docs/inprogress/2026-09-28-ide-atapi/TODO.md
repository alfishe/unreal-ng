# IDE + ATAPI — TODO

**Status:** rollout 1 implemented 2026-09-28 (P1-P7), on master (`f5fc5f05`, merged in `c69486ab`). Plan and as built:
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
- [x] TSConf scheme: `data/configs/ts-conf/unreal.ini` ships `[HDD] Scheme=NEMO-DIVIDE` (the TSConf FPGA has the same NemoIDE as BaseConf; no CD drive); `IdeController::SchemeFits(IDE_NEMO_DIVIDE, MM_TSL)` is tested
- [x] TSConf DMA devices #3 (IDE to RAM) / #B (RAM to IDE): `IdeAdapter::DmaReadWord()` / `DmaWriteWord(uint16_t)` move one whole word from / to the data register past the Z80 half-word latches: the Z80 read / write pairs stay (their triggers move only on Z80 port accesses, `zports.v:784-808`), and a DMA read loads the read latch with the word's high byte, as every IDE bus cycle loads `iderdreg` (`zports.v:849-854`); no board reads #FFFF (`IdeAdapter_Test.DmaMovesWholeWordsPastTheLatches`, `IdeAdapter_Test.DmaWithoutABoardReadsAFloatingBus`)
- [x] CD audio (PLAN #83, 2026-10-02, branch `cdda`): CUE/BIN and CD CHD discs with audio tracks, the MMC
  audio commands, READ CD, the page 0Eh volume, playback on emulated time, a mixer row per drive, the
  CdDrive TTD blob (id 41), every automation surface; tested on every board and with NedoOS `cdplay.com`
  on ZX-Evo and ATM Turbo 2+: [2026-10-02-cd-audio](../2026-10-02-cd-audio/README.md)
- [x] CD follow-up (branch `cd-folder-audio`, 2026-10-02): PLAY over a data track refused per MMC-3
  (05h / 64h start, 05h / 63h range), the activity LED on data reads only, multisession discs (Enhanced
  CD: CUE `REM SESSION`, CHD `CHSE`, READ TOC 0 / 1 / 2 with sessions, nothing readable between sessions),
  a folder of MP3 / FLAC / WAV as an audio CD in any CD slot:
  [2026-10-02-cd-audio §6](../2026-10-02-cd-audio/README.md#6-follow-up-2026-10-02-branch-cd-folder-audio)
- [ ] DivIDE paging and automap (the adapter decodes its IDE ports only)
- [ ] TSConf IDE (PLAN #41 phase 6, [technical-design §3.11](../2026-09-27-tsconf/technical-design.md#311-storage)): `TryIdePortIn/Out` in the TSConf decoder, DMA #3 / #B calling the `DmaReadWord` / `DmaWriteWord` above, a "reached the drive" flag for its optional CPU stall (`[HDD] IdeStall`)
