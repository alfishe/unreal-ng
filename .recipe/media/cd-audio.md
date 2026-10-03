# Recipe: CD Audio — Play the Audio Tracks of a CD in the ATAPI CD Drive

Goal: put a CD with audio tracks (CUE/BIN, MAME CHD, or a folder of MP3 / FLAC / WAV
files) into a machine's ATAPI CD drive, play a track (from the guest's CD player or
from outside), see where the optical head is, change the volume, and check that sound
comes out.

Reference: [docs/features/media.md](../../docs/features/media.md) (CD-ROM drive, CD audio),
[command-interface.md](../../docs/emulator/design/control-interfaces/command-interface.md) (every surface),
design and tests: [2026-10-02-cd-audio](../../docs/inprogress/2026-10-02-cd-audio/README.md).
Related: [use-media-slots.md](use-media-slots.md) (slots, insert, eject).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use [WebAPI](#webapi)
> inside host-side pipelines or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## Which machines

Any machine whose IDE board has a unit that is a CD drive. **ZX-Evo (`ATM3`) ships one**
on the IDE slave (`CD1=1`); on the others (`PENTAGON`, `ATM710`, `ATM450`, `PROFI`, `TSL`,
`SPRINTER`, a Scorpion with `[HDD] Scheme=SMUC`) make an empty unit a CD drive with the
insert option `device=cdrom` (or `CDn=1` in the config).

A test disc: `python3 tools/cd/make-audio-disc.py scratch/cd --tracks 3 --seconds 20`
writes `scratch/cd/music.cue` + `music.bin`: an **Enhanced CD** (CD-Extra): session 1 holds
audio tracks 1-3 (tones 330 / 660 / 990 Hz, a 2-second pregap before tracks 2 and 3), session 2
the data track 4 (`REM SESSION 02`; the 1:30 lead-out + 1:00 lead-in between the sessions is not in
the BIN, the reader puts it back). `--layout mixed` writes the older mixed-mode disc (data track 1,
audio tracks 2-4). Or no image at all: [a folder of music files](#audio-cd-from-a-folder).

## MCP (preferred)

```text
media {"action":"insert","slot":"cd","path":"/abs/path/scratch/cd/music.cue"}   # ZX-Evo: the slave is the CD drive
# another machine: {"action":"insert","slot":"ide0.slave","path":"...","device":"cdrom"}
inspect_state {"aspects":["cdaudio"]}
#  -> [cdaudio] ide0.slave: cue tracks 1-4 in 2 sessions, idle at 00:02:00 (track 1 index 1, 00:00:00), volume L255 R255
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/cdaudio/play","body":{"track":3}}
inspect_state {"aspects":["cdaudio"]}
#  -> [cdaudio] ide0.slave: cue tracks 1-4 in 2 sessions, playing at 00:50:14 (track 3 index 1, 00:04:14), volume L255 R255
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/cdaudio/play","body":{"track":4}}
#  -> HTTP 400 "LBA 16200 is no audio frame" (the data track)
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/cdaudio/pause"}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/cdaudio/volume","body":{"left":128,"route":"mono"}}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/cdaudio/mixer","body":{"volume":0.5,"mute":false}}
```

The guest's own CD player is the real path: NedoOS `cdplay.com` (ZX-Evo, ATM Turbo 2+) reads
the TOC, plays with PLAY AUDIO MSF and polls READ SUB-CHANNEL; `inspect_state cdaudio` then shows
the head it reads. Its keys: `1`-`9` play the track with that number, Enter the track under the
cursor, Space pause / resume, `S` stop, `T` reread the TOC (after a disc change).

## WebAPI

```bash
BASE=http://localhost:8090/api/v1
EMU=$(curl -s -X POST $BASE/emulator/start -H 'Content-Type: application/json' -d '{"model":"ATM3"}' | jq -r .id)
curl -s -X POST $BASE/emulator/$EMU/media/cd/insert -H 'Content-Type: application/json' \
     -d "{\"path\":\"$PWD/scratch/cd/music.cue\"}" | jq '{ok, slot, report}'
#  -> report: ["tracks: 1 audio 0-1499, 2 audio 1650-3149, 3 audio 3300-4799, session 2: 4 mode2 16200-16499"]

curl -s $BASE/emulator/$EMU/state/cdaudio | jq '.drives[0] | {slot, tracks: .disc.tracks, audio}'

# Play track 3 to the end of its session's audio (to=3: only track 3); or lba=X frames=N; or msf=MM:SS:FF end=MM:SS:FF
curl -s -X POST $BASE/emulator/$EMU/cdaudio/play -H 'Content-Type: application/json' -d '{"track":3}' \
     | jq '.drive.audio | {status, track, msf, play_start_lba, play_end_lba}'
sleep 3
curl -s $BASE/emulator/$EMU/state/cdaudio | jq '.drives[0] | {audio: .audio | {status, msf, relative_msf}, mixer}'
#  -> status "playing", relative_msf about "00:03:xx", mixer.active true, mixer.peak > 0

curl -s -X POST $BASE/emulator/$EMU/cdaudio/pause  | jq '.drive.audio.status'     # "paused"
curl -s -X POST $BASE/emulator/$EMU/cdaudio/resume | jq '.drive.audio.status'     # "playing"
curl -s -X POST $BASE/emulator/$EMU/cdaudio/stop   | jq '.drive.audio.status'     # "idle"
curl -s -X POST $BASE/emulator/$EMU/cdaudio/pause                                 # 409 not-playing

# Page 0Eh (what MODE SELECT sets) and the drive's mixer row
curl -s -X POST $BASE/emulator/$EMU/cdaudio/volume -H 'Content-Type: application/json' \
     -d '{"left":128,"right":255,"route":"mono","sotc":true}' | jq .drive.drive_volume
curl -s -X POST "$BASE/emulator/$EMU/cdaudio/mixer?volume=0.5&mute=false" | jq .drive.mixer

# One minute of disc: 75 frames a second of emulated time
curl -s -X POST "$BASE/emulator/$EMU/cdaudio/play?msf=00:08:00&end=00:09:00" | jq .drive.audio.play_end_lba   # 525
```

CLI (`UNREAL_CLI_PORT`, default 8765): `cdaudio play track=3`, `state cdaudio`, `cdaudio stop --json`,
`cdaudio help`. Lua: `cdaudio("play", "", {track=3})`, `cdaudio_state()`. Python:
`emu.cdaudio("play", track=3)`, `emu.cdaudio_state()`.

## Audio CD from a folder

A folder of `*.mp3`, `*.flac`, `*.wav` files in a CD slot is a Red Book audio CD: one track per
file in natural name order (`2 b.mp3` before `10 a.mp3`), decoded at insert to 44.1 kHz 16-bit
stereo, a 2-second pregap before every track after the first, at most 99 tracks and 80 minutes -
the files are taken in order while they fit, the first that does not fit ends the disc. Other files
and subfolders are left out. Rules and the worked example:
[docs/features/media.md](../../docs/features/media.md) ("Audio CD from a folder").

```text
media {"action":"insert","slot":"cd","path":"/abs/path/music","format":"audio-cd"}
#  -> insert ide0.slave: ok
#     note: track 01: 01 intro.mp3 (mp3, 5:00)
#     note: not taken: 16 track.flac: does not fit: needs 5:02 (with its pregap), 4:30 left on the 80-minute disc
#     note: audio CD: lead-out at 75:30 of 80:00, built in 5.03 s
media {"action":"info","slot":"cd"}       # structuredContent.info.medium.disc.tracks[]: number, start_msf, length_msf, title (the file)
```

```bash
curl -s -X POST $BASE/emulator/$EMU/media/cd/insert -H 'Content-Type: application/json' \
     -d "{\"path\":\"$PWD/scratch/music\",\"format\":\"audio-cd\"}" | jq '.report'
curl -s $BASE/emulator/$EMU/media/cd | jq '.info.medium | {format, disc: .disc.tracks | map({number, start_msf, title})}'
#  -> format "audio-cd", tracks with the file names
curl -s -X POST $BASE/emulator/$EMU/cdaudio/play -H 'Content-Type: application/json' -d '{"track":2}' | jq .drive.audio.status
```

CLI: `media insert cd ~/music --format audio-cd`, `media info cd`, `state cdaudio` (each track with its
file). Lua: `media_insert("cd", "/abs/music", {format = "audio-cd"})`, `media_info("cd").info.medium.disc`.
Python: `emu.media_insert("cd", "/abs/music", format="audio-cd")`. Without `format` a folder in a CD
slot is an audio CD anyway; `media targets <folder>` offers the CD drive first.

## Assert on

- `drives[].audio.status`: `idle` (15h), `playing` (11h), `paused` (12h), `completed` (13h: the play
  reached its end; the guest's next READ SUB-CHANNEL reports it once), `error` (14h: the play ended
  in an error). `status_code` is the byte
  READ SUB-CHANNEL returns.
- `drives[].disc.sessions` and `tracks[].session`: 2 on an Enhanced CD; `tracks[].title`: the file
  of an audio CD built from a folder.
- `audio.lba` / `msf` / `track` / `index` / `relative_msf`: the head. `index` 0 and a leading `-` on
  `relative_msf` mean the pregap before the track's INDEX 01.
- `mixer.active` / `mixer.peak`: sound left the drive in the last frame (a pregap is silence).
- `ok` / `error`: `no-cd-drive` 404, `no-disc` / `not-playing` / `recording` 409, `bad-request` 400.

## Pitfalls

- **Front-panel control is refused while TTD records** (`recording`, 409): a replay would not
  repeat it. A guest's PLAY / PAUSE / STOP commands are recorded and replay exactly. The
  `mixer` verb (host side) is always allowed.
- **Turbo mode renders no audio** (like every sound source), but the head keeps moving with the
  emulated frames: a guest polling the position sees the same values at any speed.
- **A data track never plays.** The drive refuses a PLAY that starts in a data track at once (sense
  05h / 64h ILLEGAL MODE FOR THIS TRACK) and a PLAY AUDIO (10) / (12) whose range runs into a data
  track (05h / 63h END OF USER AREA ENCOUNTERED ON THIS TRACK); PLAY AUDIO MSF plays the audio before
  it. A refused PLAY moves nothing. Outside the guest a start in data is `bad-request`. Only the start
  is checked (MMC-3): an end past the disc plays to the session's lead-out - on an Enhanced CD
  "track 3 up to the data track's start" plays to the end of the audio session.
- **Sprinter `CDPLAYER.FLX`** (Flex Navigator plugin) plays from the first track only (it has no
  track skip), asks for 00:02:00 - 80:00:74, never reads the status and sends no TEST UNIT READY:
  track 1 must be audio, and the first press after a disc change only clears the drive's unit
  attention ([its disassembly](../../docs/disasm/software/sprinter/cdplayer-flx/README.md)). Its
  Pause / Stop / skip buttons do nothing (beta1); Eject stops the music and opens the drive's tray.
- **A guest eject opens the tray** (START STOP UNIT LoEj): `cdaudio` state `tray_open` true, the drive
  answers NOT READY (3Ah / 02h) and `cdaudio play` `no-disc` until the guest loads it again or the disc
  is inserted again from outside; the medium stays in its slot.
- **cdplay does not check the drive's errors**: `3` on the Enhanced CD's data track (or `1` on a
  mixed-mode disc) shows `[PLAYING]` and then follows the drive back to the track that still
  plays (or stays at 00:00 when nothing played) - the player's behavior, a real drive answers the same.
- **The IDE LED** lights on data reads only (READ, READ CD); a player polling READ SUB-CHANNEL does
  not light it. Playing audio shows on the HUD's "CD" indicator.
- **A folder disc is decoded at insert** (about 5 s for 80 minutes of MP3 on a desktop) and held in
  memory (10 MiB a minute); the folder is not read again.
- **A lone `.bin` of audio needs its `.cue`** (only a BIN with data sync patterns is taken alone).
- **chdman 0.289** writes CD CHDs it cannot read back itself when `cdzs` or `cdzl` is the only
  codec; use its default codecs or two of them ([chd.md](../../docs/file-formats/disk-images/chd.md)).
