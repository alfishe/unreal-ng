# ZX-Evo CD boot — TODO

**Status:** research note written 2026-09-29 ([README.md](README.md)); the ERS CD boot works in
unreal-ng (two tests); the items below are open questions and follow-ups, none blocking.

## Verify in the emulator (the ERS state we only read from source)

- [ ] **No `AUTORUN.ZX` on the disc**: confirm where `RST 0` (`pentevo/rom/mainmenu/src/hdd_cd_boot.a80:356`)
  lands. Expected: the ERS menu restarts. Candidate test `ZXEvoErs_Test.CdBootWithoutAutorunReturnsToTheMenu`.
- [ ] **Entry state**: record `SP`, `IY`, `IM`, port `#7FFD`, `#EFF7`, the ROM page at `#0000` and the
  CPU speed when `AUTORUN.ZX` starts, and put them in the README table (now marked unverified).
  In particular: is `IY = 23610` and `IM 1`, as the 2006 standard says?
- [ ] **Hard disk on the slave / nothing on the slave**: confirm the endless `iniini` loop
  (`:229-259`) and that nothing hangs the emulator itself.
- [ ] **Hybrid ISO** (non-empty sectors 0-15): what the directory walk does after the root
  directory (`:340-355`). Low priority; real discs for the Spectrum are not hybrid.
- [x] The ERS sees the disc ejected and inserted again: `ZXEvoErs_Test.CdBootSeesTheDiscEjectedAndInsertedAgain` (`087b9ec7`)

## Try real software

- [x] **ZX-video CD No. 1** on `ATM3` (2026-09-29): the ERS boots `autorun.zx` and the player's menu runs; a clip never
  starts, because the player uses the ATM IDE ports (`#FEEF` status), which BaseConf does not have (README §3).
- [ ] **ZX-video CD No. 1** on `ATM710` with a slave CD drive: boot to 48K BASIC (the player opens the IDE ports
  through the TR-DOS `#3D2F` trap), run `CDRUNATM.$B` from a TRD, then play a clip.

- [ ] **DNA OS** `dna_nemo.iso` (ZET-9, 2007; `dna_nemo_iso.zip` on Alone Coder's ZX page): boot it with
  "D. CD boot" on `ATM3`. Record which ATAPI commands its `CDR_DRV` driver sends (`state ide`
  `last_packet`) and whether its ISO 9660 file system (`CD_DFS`) lists the disc. First real OS to
  exercise the drive beyond the ERS. Note: DNA OS targets ATM Turbo / Nemo IDE; its ZX-Evo
  compatibility is unknown.
- [ ] **Time Gal** (`timegal.iso`, 185 MB, Internet Archive item `timegal-zx`, plus the "ZX Evo fix"):
  boot on `ATM3`; check the 16-color mode, the frame rate at emulated drive speed, and the commands
  it sends (the source shows the same set as the ERS). Users on real hardware reported drive-model
  dependence; a success in the emulator says nothing about that.
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
