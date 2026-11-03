# DONE - GMX (Scorpion ZS-256 + Graphic Memory eXpander, 2048K)

**Status:** complete (2026-11-02). Design 2026-10-07, revised 2026-10-30; phases 1-8 on master 2026-10-30 - 2026-11-02.

## What landed
- Model `GMX` ("ZS Scorpion + GMX", `MM_GMX`, `RAM_2048`), config `data/configs/gmx/` with `gmx644.rom` (ProfROM +
  GMX 6.44, the default) and `gmx.rom` (Boot ROM V 5.00); `PortDecoder_ScorpionGMX` over the Scorpion 256 and its SMUC.
- Phase 1 creatable machine (175a568f0); phase 2 TTD, snapshots, state transfer, the card's state on every plane
  (d6555e918); phase 3 the 640x200 mode (f4315d276); phase 4 the turbo wait and the Pentagon frame (160914d2a);
  phase 5 the Z-Controller SD as a media slot (d014b4bd1); phase 6 evidence (e3c116e83); the loader's Pentagon schemes
  (1533c4d1d); phase 7 the SMUC peripherals of the 6.44 fast test (ccc18fbc4, f485570c3).
- 2026-11-02: no slot rule against a General Sound on a GMX (Apollonov's 1999 article stays as history, the official
  site named the 20.04.99 firmware as the fix); Puzzle Bobble plays its samples on the NeoGS and on a GS 512 KB.
- 2026-11-02: the video players' sound checked against the files' tracks: VIDEO03 on the NeoGS and a GS 512 KB
  (silent on 128 KB like the hardware), VPlayer 0.1.7 on `#B3` (NeoGS, GS) and `#FB` (SounDrive); VPlayer 0.1.6 plays
  the native 0.1.5 sample `DD-ak` with sound on `#B3`.
- 2026-11-02: CP/M 2.2 (CPMZSGMX) runs from its SMUC HDD subpartition `CPMZS0`, made in the 6.44 Hard disk utility and
  filled by its `INSTALL`; Ladder plays on the 80x25 GMX screen.
- Phase 8 (8649e564b and the follow-up): a disk autostart at power-on waits for the firmware boot (the first 48 BASIC
  KEY-SCAN) and starts TR-DOS on the floppy; the SMUC NVRAM file per firmware; ProfROM MFS hard disks in unreal-disk /
  zxdisk; fifteen programs from the collection run ([software-compatibility.md](software-compatibility.md)).

## Evidence
- Tests: `core/tests/emulator/gmx_boot_test.cpp` (`GmxBoot_Test.*`, `GmxAutostart_Test.*`), the `ScorpionGmx*` suites,
  `DiskFs_Test.ProfromMfs*` in unreal-disk.
- Usage and checks: [.recipe/machines/gmx.md](../../../.recipe/machines/gmx.md); fast-test screens in
  [evidence/](evidence/).

## Follow-ups
- None open. The Scorpion printer Covox at `#DD` (VPlayer's "DD" port) is emulated since 2026-11-03 (built-in `covox-dd`).
