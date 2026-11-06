# ZX-Evo CD boot — TODO

**Status:** research note written 2026-09-29 ([README.md](README.md)); the ERS CD boot works in
unreal-ng (two tests); the items below are open questions and follow-ups, none blocking.

## Verify in the emulator (the ERS state we only read from source)

- [x] **No `AUTORUN.ZX` on the disc** (2026-10-02): `RST 0` lands in the 48K BASIC ROM (page 28, which the
  ERS keeps at `#0000` while it runs from RAM): a 48K BASIC cold start, the (c) 1982 screen - not the ERS
  menu the source suggested. Test `ZXEvoErs_Test.CdBootWithoutAutorunColdStarts48KBasic`.
- [x] **Entry state** (2026-10-02): A `#B0`, B 0, C 0, D 1, E `#FB`, HL 4, DI, IM 1, SP `#5FFC`, IY `#5C3A`
  (23610), `#7FFD` = `#10`, `#EFF7` = `#14`, the 48K BASIC ROM (page 28) at `#0000`; README table updated.
  Test `ZXEvoErs_Test.CdBootEntersAutorunWithTheDocumentedRegisters`.
- [x] **Hard disk on the slave** (2026-10-02): the ERS keeps re-identifying the slave, the emulator keeps
  running frames. Test `ZXEvoErs_Test.CdBootWithAHardDiskOnTheSlaveKeepsAsking`. No device at all on the
  slave is not tried: the ZX-Evo config always fits a unit.
- [ ] **Hybrid ISO** (non-empty sectors 0-15): what the directory walk does after the root
  directory (`:340-355`). Low priority; real discs for the Spectrum are not hybrid.
- [x] The ERS sees the disc ejected and inserted again: `ZXEvoErs_Test.CdBootSeesTheDiscEjectedAndInsertedAgain` (`087b9ec7`)

## Try real software

- [x] **ZX-video CD No. 1** on `ATM3` (2026-09-29): the ERS boots `autorun.zx` and the player's menu runs; a clip never
  starts, because the player uses the ATM IDE ports (`#FEEF` status), which BaseConf does not have (README §3).
- [x] **ZX-video CD No. 1** on `ATM710` with a slave CD drive (2026-09-29): ATM BIOS → SPECTRUM 128 → TR-DOS →
  `RUN "boot"` (`CDRUNATM.$B`), menu, ENTER, the clip streams from the disc; no emulator change needed. The
  exploratory test needs the 455 MB ISO, so it is not in the suite; a small synthetic `.zxv` disc could make it one.

- [x] **DNA OS** `dna_nemo.iso` (2026-10-02): "D. CD boot" on `ATM3` brings up DNA_SHELL v0.14; `CD_DFS` lists
  the disc in both panels. Its driver sends TEST UNIT READY, READ TOC, READ (10), SET CD SPEED - all
  supported, no error (sampled `state/ide` `last_packet` / sense during the boot).
- [x] **Time Gal** `timegal.iso` (2026-10-02): plays on `ATM3` from the original ISO (no ZX Evo fix needed in
  the emulator): the 16-color title, then the video from the disc (40 different frames in 40 screenshots
  over 10 s). Drive-model dependence on real hardware cannot show here.
- [ ] Decide whether a small `AUTORUN.ZX` test disc belongs in `testdata/` (the tests already build
  one in memory, so probably not).

## Emulator gaps found (outside the ERS CD boot)

- [ ] **Audio CD** (NedoOS `cdplay.com`, `NedoOS/src/kapps/cdplay/main.c`): needs audio tracks
  (CUE/BIN or similar) and READ SUB-CHANNEL `#42`, PLAY AUDIO MSF `#47`, PAUSE/RESUME `#4B`,
  STOP PLAY `#4E`, plus CD audio mixed into the sound output. A separate feature; decide if wanted.
- [ ] **READ CD `#BE`**: needed by xBIOS's CD boot on ATM Turbo 2+ (inferred from
  `zxevo.pentevo/changelog.md:203`). Only if an xBIOS ROM and a CD drive are ever configured for
  `ATM710`. Cheap to add for 2048-byte user-data reads (the lvd Unreal fork accepts only that form).
- [ ] READ TOC reports one track always; multi-track / multi-session images are out of scope until
  a user needs them.

## Research left open

- [ ] xBIOS v1.36 description (nedoos.ru page, TLS error when fetched): confirm its CD boot rules
  and whether they match the ERS.
- [ ] Wild Commander: sources not local; confirm it has no CD support.
- [ ] speccy.info (SpeccyWiki) pages on CD / ATAPI: needs a browser (it blocks automated fetching).
- [x] Found `VPLAYER.ZX` (NedoVIDEO Player), the *ZX-video CD No. 1* ISO and the iS-DOS CD-Pack on atmturbo.nedopc.com (2026-09-29, README §3)
