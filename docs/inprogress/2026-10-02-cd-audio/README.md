# CD audio (CDDA) for the shared ATAPI CD-ROM drive

| | |
|---|---|
| **Date** | 2026-10-02 |
| **Branch** | `cdda` |
| **Status** | Implemented; follow-ups in [TODO.md](TODO.md) |
| **Plan** | PLAN.md row **#83**; builds on the IDE / ATAPI work ([../2026-09-28-ide-atapi/](../2026-09-28-ide-atapi/TODO.md)) and the media manager's CHD support ([../2026-10-02-media-chd/](../2026-10-02-media-chd/README.md)) |

A CD with audio tracks plays in the emulated ATAPI CD-ROM drive on every machine whose IDE board
has a CD unit. It is one feature of the shared stack, not a per-machine one: the CD image layer
(tracks, raw frames), the drive's MMC audio commands, a player on emulated time, a mixer row per
drive, TTD, and one automation layer for all surfaces. A board only provides the IDE ports; the
Sprinter's two channels need nothing more than any other board.

Glossary: **CD-DA** / **Red Book**: audio CD, 44100 Hz 16-bit stereo, 75 frames (2352 bytes, 588
sample pairs) per second. **LBA**: frame number from the start of the program area; **MSF**:
minutes / seconds / frames, LBA 0 = 00:02:00. **INDEX 00 / 01**: a track's pregap (silence) and its
start; the TOC lists INDEX 01. **TOC**: the table of contents (READ TOC). **Sub-channel Q**: the
position the drive reports (READ SUB-CHANNEL).

## 1. What landed

| Layer | Files | What |
|---|---|---|
| CD image | `core/src/emulator/io/storage/cd/` (`cdtypes.h`, `cdecc.*`, `cdimage.*`, `cdimageformats.*`) | `CdImage`: tracks (audio / mode 1 / mode 2), whole 2352-byte frames, 2048-byte user blocks, the data blocks as 512-byte sectors (the old `IBlockDevice` seam). Formats: ISO, CUE (BINARY / MOTOROLA / WAVE files, every track type of chdman's list, INDEX 00 pregaps in the file, PREGAP / POSTGAP silence, several files, track 1's INDEX 01 = LBA 0), a lone raw BIN, MAME CD CHD. Sync, header, EDC and ECC (ECMA-130) are built for frames stored without them |
| CHD | `chd/chdcodec.*`, `chd/chdfile.*`, `chd/chdflac.*` | The CD codecs `cdlz`, `cdzl`, `cdzs` (ECC bitmap, base + subcode streams, ECC rebuilt) and `cdfl` (FLAC data written back big-endian, deflate subcode); `ChdFile::OpenCd`; `flac::Decode` reports the bytes it used. CD CHDs v5 only (`chdman copy` converts v3 / v4) |
| Media manager | `mediaformatregistry.cpp`, `medium.h`, `mediatargets.cpp`, `config.cpp` | The CD slot takes `.iso`, `.cue`, `.bin`, CD `.chd`; `Medium::Cd()` hands the disc to the drive; `insert auto` / `targets` recognise CUE sheets, raw BINs and CD CHDs; a hard-disk slot refuses a CD CHD; `ImageN=*.cue` makes the unit a CD drive |
| Drive | `io/ide/ata/atapicdrom.*`, `io/ide/ata/cdaudioplayer.*` | MMC / SFF-8020 audio: PLAY AUDIO (10) / (12) / MSF / TRACK INDEX, PAUSE / RESUME, STOP PLAY / SCAN, READ SUB-CHANNEL formats 1-3 (status 11h-15h, 13h / 14h reported once), READ TOC formats 0 / 1 / 2 with audio tracks (and the SFF-8020 format field), READ HEADER, MODE SENSE / SELECT pages 01h, 0Dh, 0Eh (per-port channel select and volume, SOTC), 2Ah, 3Fh; READ CD and READ CD MSF (any sector type check, sync / header / subheader / user / EDC-ECC / C2 fields, formatted Q or raw P-W sub-channel); READ (10) of an audio frame: ILLEGAL MODE FOR THIS TRACK; a data READ or SEEK stops a play; REQUEST SENSE reports ASCQ 11h / 12h while playing |
| Time base | `CdAudioPlayer`, `IdeController::FrameElapsedBaseT` | The head moves 44100 / 3.5 MHz samples per base T-state, kept in integer units of 1 / 3,500,000 sample: exact, no drift. Inside a frame the head is "frame start + elapsed base T-states" (`EmulatorState::AudioTstate` divided by the host multiplier), so a guest polling mid-frame sees it move; at frame end the whole `config.frame` is added. Host speed, turbo and the hardware turbo do not change it |
| Mixer | `soundmanager.*`, `audiodeviceinfo.h`, `audioactivityindicators.*`, Qt HUD, recording names | One row per CD drive, `CD ide0.slave` (`AudioSourceType::CdAudio0..3`, units 0-3): volume, mute, solo, meter, recording source, HUD "CD" (category "CD Audio Activity"). The renderer reads the disc at a cursor that follows the head at the mixer rate: at 44100 Hz the samples are the track's, bit for bit; other rates interpolate linearly. Page 0Eh routes and scales. No buffer at all while the drive does not play: the mixer skips the row |
| TTD | `debugger/ttd/ide/ttdcddrive.*`, `ttd.ksy`, `ttdserializable.h` | New blob **CdDrive (id 41, v1)**: per unit the audio state (head, play range, status, SOTC, page 0Eh, 32 bytes) and the READ CD sector waiting for the 2048-byte data buffer (a raw sector with C2 and sub-channel is up to 2744 bytes). Registered only when the board has a CD unit, so the `AtaChannel` blob and every existing checkpoint are unchanged: **no fixture had to be re-recorded**. The disc is media (sealed replay reads it again at the same head) |
| Automation | `io/ide/cdaudiocontrol.*`; WebAPI `api/cdaudio_api.cpp` + `openapi_cdaudio.inc`; CLI `cli-processor-cdaudio.cpp`; Lua / Python bindings; MCP aspect `cdaudio` | One source (`CdAudioControl`): `status`, `play` (track / LBA / MSF), `pause`, `resume`, `stop`, `volume` (page 0Eh), `mixer`. Front-panel verbs act at an instruction boundary (a running emulator is parked) and are refused while TTD records (`TTDGuardedAction::CdFrontPanel`) |

Timing: as the data path, every command completes when its packet arrives (no seek or spin-up
delay is modeled anywhere in the drive); a play starts at the T-state its PLAY arrives.

## 2. Tests

| Test | Covers |
|---|---|
| `CdImageFormats_Test` (8) | CUE with one BIN per track (stored INDEX 00 pregap, PREGAP silence), several tracks in one BIN (INDEX 00 inside the file, POSTGAP), MOTOROLA byte order, MODE1/2048 cooked frames rebuilt raw, a WAVE file, a sheet with backslashes / other case / BOM / CRLF, every parse error naming the line, ISO, raw BIN, ECC against `testdata/media/cd/track1.bin` (built by the fixture script and checked against MAME's tables), MSF / LBA math |
| `CdImageFormats_Chd_Test` (2) | chdman's CD CHDs (`disc-default.chd`: cdlz + cdfl hunks, `disc-cdzs-cdzl.chd`: cdzs + cdzl hunks): the same layout and every sample and data byte as the CUE |
| `CdAudioPlayer_Test` (8) | 75 frames per emulated second (one second = 44100 samples exactly; 97 Pentagon frames to the integer), the head mid-frame, PLAY mid-frame, pause / resume / stop, 11h-15h (13h / 14h once), a play into a data track (14h), SOTC, sample-exact render at 44.1 kHz, page 0Eh routing / volume / mute / mono, nothing rendered while idle or in silence, 48 kHz interpolation |
| `AtapiCdromAudio_Test` (8) | READ TOC 0 / 1 / 2 byte for byte (LBA and MSF, from a track, from the lead-out, the SFF-8020 field, CD-TEXT refused), PLAY AUDIO MSF + READ SUB-CHANNEL position (absolute, relative, index, the pregap counting down), PLAY (10) / (12) / TRACK INDEX / from the current position / zero length, every error sense, PAUSE without a play (2Ch), STOP, a READ stopping a play, REQUEST SENSE 00/11h, READ (10) of audio (64h), READ HEADER, READ CD raw audio (also in 1000-byte pieces), READ CD MSF with formatted Q, raw P-W, raw and cooked data frames, the sector-type check, MODE SENSE / SELECT page 0Eh (current, default, changeable, all pages, a broken parameter list), SEEK, DEVICE RESET keeps the audio and the reset line stops it |
| `SoundManagerCd_Test` (3) | the mixer row per drive (follows `SetUnitKind`), the master mix sample-exact with the row soloed, no buffer while idle, volume / mute / meter, the head at 1x, 4x host speed and turbo identical |
| `TTDCdDrive_Test` (3) | registered only with a CD drive (a Pentagon has none: its checkpoints are unchanged), the blob round trip (head, status, page 0Eh), a play that ends inside a recording replayed exactly from the session start, a frame checkpoint and a mid-frame seek |
| `CdAudioControl_Test` (4) | the shared report and verbs, drive selectors, every error, refused while recording (the mixer allowed), no CD drive |
| `IdeControllerCd_Test` (10) | every board: Pentagon (Nemo, Nemo A8, DivIDE), ZX-Evo, ATM Turbo 2+, ATM Turbo 2 (4.50), Profi, Scorpion SMUC, TS-Conf, Sprinter (secondary slave): a PLAY through the channel, sound on the drive's row, the head on the board's frame, the CdDrive blob |
| `ZXEvoErs_Test.NedoOsCdplayPlaysAudioTracks` | **real software**, see §3 |
| `ATM710NedoOsCdplay_Test.PlaysPausesAndStopsATrack` | **real software**, see §3 |

Updated: `TTDPeripheralIdTable_Test` (id 41), the TTD bench gate's ATM3 / idle rows (the CdDrive
blob, ~42 compressed bytes per checkpoint: the ZX-Evo ships its CD drive).

Fixtures: `testdata/media/cd/` (30 KB) from `tools/cd/make-test-fixtures.py` (chdman 0.289);
the other discs are written by the tests (`core/tests/_helpers/cdtestdisc.h`).
`tools/cd/make-audio-disc.py` writes a CUE/BIN with tone tracks for people and recipes.

## 3. Real software per platform

| Platform (board) | Program | Result |
|---|---|---|
| ZX-Evo (`ATM3`, NemoIDE / DivIDE ports, CD on the slave) | NedoOS `cdplay.com` ("Audio CD Player 1.2", NedoOS release `44049473`) from the SD card | reads the TOC and lists the tracks with their start and length, `2` plays track 2 (PLAY AUDIO MSF from its TOC start to the next track's), the status line shows `[PLAYING] Track: 02 / 03` and `Time: 00:01 / 00:08` from its READ SUB-CHANNEL polls one emulated second later, the drive's mixer row carries the 660 Hz tone, Space pauses (the head stays, `[PAUSE]`) and resumes, `S` stops (`[STOPPED]`) |
| ATM Turbo 2+ (`ATM710`, ATM ports `#FEEF` ... `#FF0F`) | the same `cdplay.com` on the NedoOS ATM2 floppy kernel (`osatm2.trd`), from TR-DOS | the same: TOC, play, position 00:01, pause / resume, stop |
| Pentagon (Nemo), Profi, Scorpion (SMUC), TS-Conf, ATM Turbo 2 4.50 | none found (see TODO) | the board test above; the ports are the ones the IDE adapters already route (`IdeAdapter_Test`) |
| Sprinter | `CD_PLAY.TRD` (Peters Plus, 2001) and `CDPLAYER.FLX` exist, not run here (Sprinter S7) | the board test above (secondary slave) |

The player's screen on the ZX-Evo, one second into track 2:

```
   Audio CD Player 1.2
  Track 01 [AUDIO]  Start 00:02:00  Dur 00:03
  Track 02 [AUDIO]  Start 00:05:00  Dur 00:08
  Track 03 [AUDIO]  Start 00:13:00  Dur 00:06
  Status: [PLAYING] Track: 02 / 03
  Time:   00:01 / 00:08
  [....................................]  12%
 [Space] Pause [S] Stop [T] Reread TOC [1-9] Track [<-][->] FFD/FBD [Q] Quit
```

(cdplay tests bit 6 of the ADR / control byte for "data", so it labels the data track 1 `[AUDIO]`;
the drive reports control 4 for it, as MMC says. "Dur" counts the next track's 2-second pregap.)

## 4. Live check

`unreal-qt` from this build (WebAPI, MCP and CLI on their own ports), ZX-Evo with
`tools/cd/make-audio-disc.py` output: the recipe [cd-audio.md](../../../.recipe/media/cd-audio.md)
step by step on the WebAPI (insert, `state/cdaudio`, play track 3, the head after 3 s, pause,
resume, stop, 409 not-playing, volume, mixer, MSF range), the CLI (`cdaudio play`, `state
cdaudio`, `--json`, `help`), MCP (`media` insert / eject, `inspect_state` aspect `cdaudio`,
`invoke_api` play) and Lua (`cdaudio()`, `cdaudio_state()` through `/api/v1/lua/exec`). Python is
compiled out of the default build (`ENABLE_PYTHON_AUTOMATION=OFF`); its binding was compiled
against pybind11 separately.

## 5. Design notes

- **Why a separate TTD blob.** Extending `AtaChannel` (id 17) would change the blob of every
  machine with an IDE board, so every fixture with a Nemo or Sprinter board would have to be
  re-recorded. The CD drive's extra state lives in its own blob, present only with a CD unit; the
  `AtaDeviceState` buffer stays 2048 bytes and a longer READ CD sector waits in the blob's stage.
- **Machine state vs renderer.** The head, play range and status are machine state (READ
  SUB-CHANNEL, TTD); the render cursor, interpolation and output are host state. A frame in turbo
  without audio advances the head and renders nothing; the renderer re-syncs to the head after a
  jump (play start, seek, TTD restore) and never otherwise, so at 44.1 kHz the output is the disc.
- **Front-panel control.** Real drives had a play button; automation acting on the drive is an
  outside input that the journal does not hold, so it is refused while TTD records.
- **Performance.** Nothing per instruction or per port access changed (the drive answers inside
  the existing IDE port paths; no A/B benchmark needed). Per frame: one pointer test without an
  IDE board; with CD units a status test per unit and no mixing while nothing plays.

## 6. Sources

- MMC-3 / SFF-8020i command and page layouts; MAME `src/devices/machine/t10mmc.cpp` (behavior
  reference, not copied), `src/lib/util/cdrom.cpp` (CHD track layout, ECC tables) and
  `chdcodec.cpp` (CD codecs): [github.com/mamedev/mame](https://github.com/mamedev/mame).
- ECMA-130 (CD-ROM sector format, EDC / ECC):
  [ecma-international.org](https://ecma-international.org/publications-and-standards/standards/ecma-130/).
- NedoOS `cdplay` source `src/kapps/cdplay/main.c`: [github.com/alfishe/NedoOS](https://github.com/alfishe/NedoOS).
