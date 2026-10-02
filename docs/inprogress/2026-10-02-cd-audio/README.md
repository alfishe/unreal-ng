# CD audio (CDDA) for the shared ATAPI CD-ROM drive

| | |
|---|---|
| **Date** | 2026-10-02 |
| **Branch** | `cdda` |
| **Status** | Implemented; follow-up (branch `cd-folder-audio`, §6): data-track refusal per MMC-3, the activity LED, Enhanced CD / multisession, audio CD from a folder; open items in [TODO.md](TODO.md) |
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
| TTD | `debugger/ttd/ide/ttdcddrive.*`, `ttd.ksy`, `ttdserializable.h` | New blob **CdDrive (id 41; v1, v2 since §6: plus the disc identity per unit)**: per unit the audio state (head, play range, status, SOTC, page 0Eh, 32 bytes) and the READ CD sector waiting for the 2048-byte data buffer (a raw sector with C2 and sub-channel is up to 2744 bytes). Registered only when the board has a CD unit, so the `AtaChannel` blob and every existing checkpoint are unchanged: **no fixture had to be re-recorded**. The disc is media (sealed replay reads it again at the same head) |
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

## 6. Follow-up (2026-10-02, branch `cd-folder-audio`)

The owner's five requests after the first live runs.

| # | Request | What changed | Where |
|---|---|---|---|
| 1 | PLAY AUDIO over a data track per MMC | Per MMC-3 r10g 5.11-5.13 only the **start** is checked: not found (past the lead-out, between sessions) 05h / 21h LBA OUT OF RANGE; not in an audio track 05h / 64h / 00h ILLEGAL MODE FOR THIS TRACK. The end is not checked ("all contiguous audio sectors between the starting and the ending MSF address shall be played"): an end past the disc plays to the start track's session lead-out (13h when it gets there). A data track inside the range: PLAY AUDIO (10) / (12) 05h / 63h / 00h END OF USER AREA ENCOUNTERED ON THIS TRACK ("If the CD Sub-channel mode type ... changes within the transfer length"), PLAY AUDIO MSF (no such clause) plays the audio before it. A refused PLAY moves nothing (SCSI-2 / MMC-3: 13h / 14h only report a play that ran). The front panel (`CdAudioControl` play) applies the same rules; `track=N` without `to` plays the run of audio tracks of that session | `atapicdrom.cpp` PlayAudio, `cdaudioplayer.cpp`, `cdaudiocontrol.cpp` |
| 1 | cdplay "[PLAYING] Track 01/04 frozen at 00:00" | cdplay's `sendAtapiPacket` never reads the status after a packet: a refused PLAY (track 1 = data on the old layout) still sets `is_playing`, and its poll handles audio status 13h / 00h but not 15h, so the screen freezes. A real drive answers the same; with the new disc layout track 1 is audio | cdplay `src/kapps/cdplay/main.c` (`playTrack`, `runVisualPlayer`) |
| 1b | Sprinter `CDPLAYER.FLX` (Flex Navigator, Shaos 2002) never played | Its "Play CD" is PLAY AUDIO MSF 00:02:00 - 80:00:74; the drive refused it (end past the lead-out: 05h / 21h, a check MMC-3 does not have; MAME `t10mmc.cpp` takes such ends, "BeOS ... 99:59:71"), and the plugin never reads the status. Fixed by the start-only rule above. Track 1 must still be audio (data track 1: 05h / 64h, as on a real drive), the first press after a disc change only clears the unit attention (the plugin sends no TEST UNIT READY), and it has no track skip (its README: "only 2 functions"). The IDE handshake through the Sprinter adapter (A8 half latch, `OUTI` / `INI` with B counting down) delivers the packet intact. Disassembly: [docs/disasm/software/sprinter/cdplayer-flx](../../disasm/software/sprinter/cdplayer-flx/README.md). Live: the plugin's own routines loaded into a running Sprinter (BIOS port table) play track 1 to LBA 4800 on the Enhanced CD. MAME itself was not run (driving Flex Navigator headless was not set up); its code path was read instead | `atapicdrom.cpp`, `AtapiCdromAudio_Test.SprinterCdplayerFlxPlaysFromTheFirstTrack` |
| 2 | Activity LED on data only | `AtaDevice::CountsAsActivity()`: a disk counts every block as before; the CD drive only the blocks of READ (10) / (12), READ CD, READ CD MSF - not packets, replies, status polls or audio play. Audio playing shows on the HUD's "CD" indicator (the mixer row's activity, already there), as a real drive's busy LED shows the head reading for the host | `atadevice.*`, `atapicdrom.cpp` |
| 3 | cdplay digit keys | cdplay's `[1-9]` plays the track with that number (`key - '0'`); Enter plays the track under the cursor. On the old layout `1` asked for the data track 1: refused, cdplay ignored the error, its next READ SUB-CHANNEL saw the drive still on the track under the cursor (track 4) and "followed" it with `playTrack(4)`. Our ZX-Evo PS/2 path delivers `1` correctly (`'1'` plays track 1 in the real-software test). No emulator bug | `ZXEvoErs_Test.NedoOsCdplayPlaysAudioTracks` |
| 4 | Test disc layout: audio first | `tools/cd/make-audio-disc.py` and `cdtestdisc.h` `WriteMusicDisc` write an **Enhanced CD** by default (Blue Book: session 1 audio, session 2 one CD-ROM XA mode 2 form 1 data track), `--layout mixed` / `MusicLayout::Mixed` the old one. The CD image layer reads sessions: `Track::session`; CUE `REM SESSION nn` (IsoBuster / Redump / MAME), `REM LEAD-OUT` (one file: the lead-out's start in the file, ImgBurn filler kept as the gap; a file per track: its length), `REM LEAD-IN`, `REM PREGAP`; without them the standard 6750 (later sessions 2250) + 4500 frames (cdrecord README.multi) are put back; CHD `CHSE` "SESSION:n" entries (chdman stores no gap: the standard one is put back). Nothing reads between sessions (READ / SEEK: 21h). READ TOC format 0: every track, the last session's lead-out; format 1: first / last session and the last session's first track; format 2: per session A0 (PSEC 00h CD-DA, 20h CD-ROM XA) / A1 / A2 / tracks, after every session but the last B0 (ADR 5: next program area, 79:59:74) and in session 1 C0 (95:00:00, as MAME). The owner's demo disc (`testdata/machines/sprinter/cd/music.cue` + `.bin` in the main checkout, untracked) regenerated: audio 1-3 (20 s), data 4 at LBA 16200 | `cdtypes.h`, `cdimage.*`, `cdimageformats.cpp`, `atapicdrom.cpp`, the generators |
| 5 | Folder of MP3 / FLAC / WAV as an audio CD | `AudioFolderDisc` (rules in its header and [media.md](../../features/media.md)): natural order, one track per file, 4 s minimum, 2-second pregaps, 99 tracks, **80 minutes** (lead-out by 80:00:00: an 80-minute CD-R; the Red Book's nominal 74 min would refuse albums every burner takes), first N that fit in order, no packing, a report line per file. Decoders (`AudioFileDecoder`): minimp3 (vendored; ID3v2 / ID3v1 / APE skipped, Xing / Info / VBRI frame dropped, LAME tag delay + padding cut: gapless), **dr_flac** vendored in `core/src/3rdparty/dr_flac/` (the CHD FLAC decoder takes 16-bit stereo frames only; real files are 24-bit, mono, 5.1), WAV here (PCM 8-32, float 32 / 64, extensible). Conversion: mono on both sides, 5.1 ITU downmix, a windowed-sinc polyphase resampler in the exact ratio, round to 16 bits. Media: a folder in a CD slot (or `format: audio-cd`) builds the disc (`format` `audio-cd`, read-only); `targets` offers a music folder to the CD drive first; `info` carries `medium.disc.tracks[].title`; the CD slots accept folders. TTD: the CdDrive blob v2 carries each unit's disc identity (ContentId: an audio CD's file names + bytes + layout, not its path); a restore onto another disc logs a warning and counts it (`DiscMismatches`), restores anyway. Qt media panel: Insert Folder works on a CD drive (off the UI thread, progress for the watchdog); an empty IDE unit swaps its drive for any CD medium (`MediaTargets::Classify`: ISO, CUE, raw BIN, CD CHD, music folder), not only `.iso` | `audiofolderdisc.*`, `audiofiledecoder.*`, `mediaformatregistry.cpp`, `mediatargets.cpp`, `mediacontrol.cpp`, `ideunitslot.cpp`, `ttdcddrive.*`, `mediapanelwindow.cpp` |

**Mount time** (naive v1: everything decoded at insert, on the caller's thread): 12 MP3 (5 min,
192 kbit/s) + 3 FLAC (5 min, 48 kHz: resampled) = 75:30 of audio in **5.0 s** on the dev Mac
(Release build); the 16th file did not fit and ended the disc. Memory: the disc's PCM, 10.1 MiB per
minute (~760 MiB here); the build peaks higher (one file's float samples, the disc buffer's growth).
In unreal-qt no path builds on the UI thread: the media panel's Insert Folder, a drop on the main
window and File > Open all hand the folder to the panel's folder worker (the one HDD / SD folder
volumes use, BUGS.md #3: `MediaPanelWindow::insertFolder`): the row shows "pending", progress feeds
the stall watchdog, the table refreshes on completion. The automation surfaces build on their own
request thread.

### Tests added / changed

| Test | Covers |
|---|---|
| `AtapiCdromAudio_Test.PlayStartingInADataTrackFailsAtOnce` | the sense bytes (70h, 05h, ASC 64h, ASCQ 00h), PLAY (10) / (12) / MSF, head and audio status unchanged, a refused PLAY during a play leaves it playing |
| `AtapiCdromAudio_Test.PlayRunningIntoADataTrackIsRefused` | audio then data in one session: PLAY (10) 05h / 63h / 00h, nothing starts; PLAY MSF plays to the data track; up to the data track it plays and completes (13h) |
| `AtapiCdromAudio_Test.SprinterCdplayerFlxPlaysFromTheFirstTrack` | the plugin's sequence (IDENTIFY DEVICE aborted, IDENTIFY PACKET DEVICE, PLAY MSF 00:02:00 - 80:00:74): plays track 1 to the audio lead-out; FF:FF:FF end; the mixed-mode disc refused 05h / 64h |
| `AtapiCdromAudio_Test.EnhancedCdTocSessionsAndPlay` | READ TOC formats 0 / 1 / 2 byte for byte on a two-session disc (A0 PSEC 00h / 20h, B0, C0), a play "to the next track's start" ends at session 1's lead-out, the data track refused, READ / SEEK into the gap 21h, the data session reads |
| `AtapiCdromAudio_Test.ActivityLedLightsOnlyOnDataTransfers` | polls, TOC, INQUIRY, PLAY, REQUEST SENSE: no count; READ (10) / READ CD: one per block |
| `CdImageFormats_Test.EnhancedCdOneBinWithRemSession`, `.MultisessionRedumpAndImgBurnSheets`, `.MultisessionChdSessionEntries` | the layouts, the gap, Redump / ImgBurn sheets, REM lengths, sheet errors, a CHD with CHSE entries |
| `AudioFileDecoder_Test` (6) | WAV in every sample format bit-exact, the resampler against an ideal sine (under -60 dB), MP3 gapless length and phase, MP3 48 kHz mono, FLAC 16-bit lossless, 24-bit 96 kHz, 5.1 downmix (fixtures `testdata/media/audio/`, 57 KB, `tools/cd/make-audio-file-fixtures.py`) |
| `AudioFolderDisc_Test` (5), `AudioFolderDiscMedia_Test` (2) | natural order, pregaps, 4 s minimum, skip report, samples on the disc, the 99-track cap, capacity: first N that fit, no packing, identity by content; insert with / without `format`, refusals, `info` track list, `targets` |
| `TTDCdDrive_Test.FolderDiscReplaysIdenticallyAndAnotherDiscIsReported` | record / replay with a folder disc, the blob's disc identity, the same files elsewhere quiet, other content reported |
| `CdAudioControl_Test.EnhancedCdPlaysItsAudioSessionAndRefusesTheData` | sessions in the report, the session run, `to=` past the session, the data track and a range into it refused |
| `ZXEvoErs_Test.NedoOsCdplayPlaysAudioTracks` | **real software** on the Enhanced CD: `1` plays track 1, position, sound, pause / resume / stop, `2` to the session's lead-out, `3` refused (sense 64h) and cdplay following the drive back to track 2; then a **folder disc** (two WAV, one MP3) swapped in, `T`, the TOC by file order, `2` plays the second file |
| `ATM710NedoOsCdplay_Test.PlaysPausesAndStopsATrack` | the same player through the ATM ports on the Enhanced CD |

The unit tests that check numbers of the old layout (`CdAudioPlayer_Test`, `CdAudioControl_Test`,
`SoundManagerCd_Test`, `TTDCdDrive_Test`, `IdeControllerCd_Test`) use `MusicLayout::Mixed`, so the
mixed-mode disc stays covered.

### Live check (this branch)

`unreal-qt` on free ports (WebAPI 8190, CLI 8191, MCP 8192), ZX-Evo: the 80-minute folder above
through the WebAPI (`format: audio-cd` and without), `media/cd` info with the track titles, `cdaudio`
play / state / stop; the regenerated Enhanced CD through MCP (`media` insert: "session 2: 4 mode2
16200-16499", `inspect_state cdaudio`: "cue tracks 1-4 in 2 sessions", play track 3 to 4 ends at
LBA 4800, track 4 refused); the CLI (`media insert cd <folder> --format audio-cd`, `media info cd`,
`cdaudio play track=2`, `state cdaudio` with the file per track); Lua (`media_insert` with
`{format="audio-cd"}`, `media_info`, `cdaudio_state().drives[1].disc.sessions` = 2, `cdaudio("play",
"", {track=3, to=4})` -> end 4800, track 4 refused). Python is compiled out of the default build; its
binding passes the same options (no Python code changed).

## 7. Sources

- MMC-3 / SFF-8020i command and page layouts; MAME `src/devices/machine/t10mmc.cpp` (behavior
  reference, not copied), `src/lib/util/cdrom.cpp` (CHD track layout, ECC tables) and
  `chdcodec.cpp` (CD codecs): [github.com/mamedev/mame](https://github.com/mamedev/mame).
- ECMA-130 (CD-ROM sector format, EDC / ECC):
  [ecma-international.org](https://ecma-international.org/publications-and-standards/standards/ecma-130/).
- NedoOS `cdplay` source `src/kapps/cdplay/main.c`: [github.com/alfishe/NedoOS](https://github.com/alfishe/NedoOS).
- MMC-3 r10g (PLAY AUDIO (10), READ SUB-CHANNEL audio status, READ TOC formats 0-2, the full TOC's
  A0 PSEC program area format): [13thmonkey.org mirror](https://www.13thmonkey.org/documentation/SCSI/mmc3r10g.pdf).
- SCSI-2 X3.131 §14 (PLAY AUDIO, audio status reported once):
  [staff.uni-mainz.de/tacke/scsi/SCSI2-14.html](https://www.staff.uni-mainz.de/tacke/scsi/SCSI2-14.html).
- Multisession gaps (1:30 / 0:30 lead-out, 1:00 lead-in, 11250 + 150 frames): cdrtools
  [README.multi](https://cdrtools.sourceforge.net/private/man/README/README.multi); Enhanced CD layout:
  [Blue Book (CD standard)](https://en.wikipedia.org/wiki/Blue_Book_(CD_standard)).
- CUE `REM SESSION` / `REM LEAD-OUT` / `REM LEAD-IN` / `REM PREGAP` and the CHD `CHSE` entry: MAME
  [`src/lib/util/cdrom.cpp`](https://github.com/mamedev/mame/blob/master/src/lib/util/cdrom.cpp),
  [`chd.cpp`](https://github.com/mamedev/mame/blob/master/src/lib/util/chd.cpp); redumper
  [`cd/toc.ixx`](https://github.com/superg/redumper/blob/main/cd/toc.ixx).
- Emulator consensus on PLAY into data: MAME [`t10mmc.cpp`](https://github.com/mamedev/mame/blob/master/src/devices/machine/t10mmc.cpp)
  and 86Box [`cdrom.c`](https://github.com/86Box/86Box/blob/master/src/cdrom/cdrom.c) check the start only
  (05h / 64h); during play MAME plays data frames, 86Box mutes them. We follow the MMC text instead.
- Decoders: minimp3 [github.com/lieff/minimp3](https://github.com/lieff/minimp3) (CC0), dr_flac
  [github.com/mackron/dr_libs](https://github.com/mackron/dr_libs) (public domain / MIT-0).
