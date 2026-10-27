# CD audio (CDDA) — DONE (PLAN #83 retired)

**Status:** implemented 2026-10-02. All formats, all MMC audio commands, playback, mixer, TTD,
every surface. Open only: seek latency, CD-player software for Profi/Pentagon/Scorpion/TS-Conf.
Design, tests and results: [README.md](README.md).

## Done

- [x] CD image layer: `CdImage`, ISO, CUE/BIN (BINARY / MOTOROLA / WAVE, INDEX 00 / PREGAP /
  POSTGAP, several files), raw BIN, MAME CD CHD (cdlz / cdzl / cdzs / cdfl, v5), EDC / ECC
- [x] Media manager: the CD slot's formats, `Medium::Cd()`, `targets` / `insert auto`, `ImageN=*.cue`
- [x] ATAPI audio and MMC commands (PLAY x4, PAUSE / RESUME, STOP, READ SUB-CHANNEL 1-3, READ TOC
  0 / 1 / 2, READ HEADER, MODE SENSE / SELECT 01h / 0Dh / 0Eh / 2Ah / 3Fh, READ CD, READ CD MSF)
- [x] Playback on emulated time (exact integer head), the renderer, page 0Eh routing and volume
- [x] Mixer row per drive, HUD "CD", recording source names, Qt mixer rows (follow the device list)
- [x] TTD: the CdDrive blob (id 41), only with a CD unit; no fixture re-recorded
- [x] Automation: `CdAudioControl` on WebAPI + OpenAPI, MCP, CLI, Lua, Python; docs of each surface,
  command-interface.md, recipe [cd-audio.md](../../../.recipe/media/cd-audio.md) verified on a live build
- [x] Every IDE board: a test per board (Pentagon Nemo / Nemo A8 / DivIDE, ZX-Evo, ATM Turbo 2+,
  ATM Turbo 2 4.50, Profi, Scorpion SMUC, TS-Conf, Sprinter)
- [x] Real software: NedoOS `cdplay.com` on the ZX-Evo (Nemo / DivIDE ports) and on the ATM Turbo 2+
  (ATM ports): TOC, play, position, pause, resume, stop

### Follow-up, branch `cd-folder-audio` (2026-10-02, [README §6](README.md#6-follow-up-2026-10-02-branch-cd-folder-audio))

- [x] PLAY AUDIO per MMC-3: only the start is checked (past the disc 05h / 21h, outside audio 05h / 64h);
  an end past the disc plays to the session's lead-out; a data track in the range: (10) / (12) 05h / 63h,
  MSF plays the audio before it; a refused PLAY moves nothing; the front panel follows the same rules
- [x] Sprinter `CDPLAYER.FLX` (Flex Navigator) did not play: its PLAY MSF 00:02:00 - 80:00:74 was refused
  for the end past the lead-out; fixed per MMC-3 5.13; disassembly and notes in
  [docs/disasm/software/sprinter/cdplayer-flx/](../../disasm/software/sprinter/cdplayer-flx/README.md)
- [x] CDPLAYER.FLX "no INT after Play" (branch `cd-plugin-int`): INTs and FN were fine (PC #A441 is FN's
  idle HALT); its Stop / Pause / skip buttons are unimplemented in beta1; its Eject was ignored by the
  drive - now START STOP UNIT stops the play and LoEj opens / closes a tray (NOT READY 3Ah / 02h while
  open, PREVENT ALLOW honored; TTD: in the CdDrive stage bytes, layout unchanged); sense data is
  discarded by the next command (SPC)
- [x] Guest eject unmounts the slot (branch `cd-eject-unmount`): an accepted START STOP UNIT eject asks the
  media manager (`MediaManager::GuestEject`, the drive's eject listener from `IdeUnitSlot`) for its normal
  eject at the frame boundary - slot empty on every surface; tray open until a Load (no disc: 3Ah / 01h)
  or an insert; no recording guard / invalidation, skipped during TTD replay (sealed: the detach after a
  guest eject changes no drive state, so replay matches the recording); every IDE board
- [x] cdplay's frozen `[PLAYING] 00:00` and "`1` played the track under the cursor": cdplay ignores the
  drive's errors (documented; the ZX-Evo keyboard delivers the digits right)
- [x] Activity LED: data reads only (READ, READ CD); audio shows on the HUD's "CD"
- [x] Enhanced CD / multisession: CUE `REM SESSION` / `LEAD-OUT` / `LEAD-IN` / `PREGAP`, CHD `CHSE`, the
  gap between sessions, READ TOC formats 0 / 1 / 2 with sessions; the generators write an Enhanced CD by
  default (`--layout mixed` for the old disc); the owner's demo disc regenerated
- [x] Audio CD from a folder of MP3 / FLAC / WAV (`AudioFolderDisc`, dr_flac vendored), media manager,
  `targets`, info track list, TTD disc identity (CdDrive v2), Qt media panel, every surface + docs + recipe

## Follow-ups

- [ ] **Real CD-player software for the other boards.** Searched (2026-10-02): `~/Downloads`, `testdata/`,
  the MAME pack (`mame_release_v306_25.05.2025`, no software lists), the NedoOS trees, the Profi,
  ATM, TS-Conf and Wild Commander collections. Missing:
  - Profi (PROFI board): a CP/M or TR-DOS CD player for the Profi IDE - none found;
  - Pentagon (Nemo): NedoOS's Pentagon build (`osp26.trd`, Pentagon 2.666, ATM-type memory map) does
    not run on the Pentagon 128 / 512 / 1024 model; a Pentagon CD player - none found;
  - Scorpion (SMUC), TS-Conf, ATM Turbo 2 4.50: none found (NedoOS has no TS-Conf build; its ATM2
    kernel was not tried on the 4.50's 512 KB);
  - a commercial or demo disc with CD-DA tracks for these machines - none found; the tests use
    synthetic discs (`tools/cd/make-audio-disc.py`).
- [ ] Sprinter (S7, [2026-09-28-sprinter/TODO.md](../2026-09-28-sprinter/TODO.md)): run `CD_PLAY.TRD`
  (Peters Plus 2001, TR-DOS in Spectrum mode) and the Flex Navigator plugin `CDPLAYER.FLX`; decide
  whether the shipped config puts the CD drive on `ide0.slave` like MAME (it changes the BIOS's drive
  detection screens). CD audio itself needs nothing Sprinter-specific (`IdeControllerCd_Test` plays on
  the secondary slave).
- [ ] Seek and spin-up latency: the drive answers every command at once, as its data path always has;
  a model of access time (and BSY / DSC during a seek) would come for data and audio together.
- [ ] CD-TEXT, ISRC / MCN from the image (CUE `ISRC`, `CATALOG`, CHD subcode); READ SUB-CHANNEL
  reports MCVAL / TCVAL 0 today. R-W sub-channel (CD+G) reads zeros.
- [ ] SCAN (BAh) and the PLAY AUDIO TRACK RELATIVE commands are not implemented (ILLEGAL REQUEST).
- [ ] v3 / v4 CD CHDs (`CHCD` metadata) and GD-ROM / DVD CHDs are refused with the reason.
- [ ] A better resampler than linear interpolation when the mixer does not run at 44.1 kHz.
- [ ] Audio CD from a folder, v2 ideas (not needed now): decode lazily per track or on a worker thread
  with a spill file instead of ~10 MiB a minute in memory and ~5 s for 80 minutes at insert (off the
  UI thread already: every Qt path uses the panel's folder worker); CD-TEXT from the files' tags; Ogg /
  Opus; a `.m3u` playlist as the track order.
- [ ] TTD: the CdDrive blob's disc identity is reported (log + `DiscMismatches`), not shown in the TTD
  status / GUI; a UNS media section (roadmap UNS-6) would carry it for every medium.
- [ ] MAME reads a multisession CHD without the lead-out / lead-in gap (its CHD layout adds none); we put
  the standard gap back, so LBAs past session 1 differ from MAME's for such a CHD (the disc's are ours).
