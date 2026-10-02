# CD audio (CDDA) — TODO

**Status:** implemented on branch `cdda` (2026-10-02). PLAN.md row **#83**. Design, tests and
results: [README.md](README.md).

## Done

- [x] CD image layer: `CdImage`, ISO, CUE/BIN (BINARY / MOTOROLA / WAVE, INDEX 00 / PREGAP /
  POSTGAP, several files), raw BIN, MAME CD CHD (cdlz / cdzl / cdzs / cdfl, v5), EDC / ECC
- [x] Media manager: the CD slot's formats, `Medium::Cd()`, `targets` / `insert auto`, `ImageN=*.cue`
- [x] ATAPI audio and MMC commands (PLAY x4, PAUSE / RESUME, STOP, READ SUB-CHANNEL 1-3, READ TOC
  0 / 1 / 2, READ HEADER, MODE SENSE / SELECT 01h / 0Dh / 0Eh / 2Ah / 3Fh, READ CD, READ CD MSF)
- [x] Playback on emulated time (exact integer head), the renderer, page 0Eh routing and volume
- [x] Mixer row per drive, HUD "CD", recording source names, Qt mixer rows (follow the device list)
- [x] TTD: the CdDrive blob (id 39), only with a CD unit; no fixture re-recorded
- [x] Automation: `CdAudioControl` on WebAPI + OpenAPI, MCP, CLI, Lua, Python; docs of each surface,
  command-interface.md, recipe [cd-audio.md](../../../.recipe/media/cd-audio.md) verified on a live build
- [x] Every IDE board: a test per board (Pentagon Nemo / Nemo A8 / DivIDE, ZX-Evo, ATM Turbo 2+,
  ATM Turbo 2 4.50, Profi, Scorpion SMUC, TS-Conf, Sprinter)
- [x] Real software: NedoOS `cdplay.com` on the ZX-Evo (Nemo / DivIDE ports) and on the ATM Turbo 2+
  (ATM ports): TOC, play, position, pause, resume, stop

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
