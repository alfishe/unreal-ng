# Recipe: CD Audio — Play the Audio Tracks of a CD in the ATAPI CD Drive

Goal: put a CD with audio tracks (CUE/BIN, MAME CHD) into a machine's ATAPI CD
drive, play a track (from the guest's CD player or from outside), see where the
optical head is, change the volume, and check that sound comes out.

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
writes `scratch/cd/music.cue` + `music.bin` (a data track, then audio tracks of tones
330 / 660 / 990 Hz, each after a 2-second pregap).

## MCP (preferred)

```text
media {"action":"insert","slot":"cd","path":"/abs/path/scratch/cd/music.cue"}   # ZX-Evo: the slave is the CD drive
# another machine: {"action":"insert","slot":"ide0.slave","path":"...","device":"cdrom"}
inspect_state {"aspects":["cdaudio"]}
#  -> [cdaudio] ide0.slave: cue tracks 1-4, idle at 00:02:00 (track 1 index 1, 00:00:00), volume L255 R255
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/cdaudio/play","body":{"track":3}}
inspect_state {"aspects":["cdaudio"]}
#  -> [cdaudio] ide0.slave: cue tracks 1-4, playing at 00:34:14 (track 3 index 1, 00:04:14), volume L255 R255
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/cdaudio/pause"}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/cdaudio/volume","body":{"left":128,"route":"mono"}}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/cdaudio/mixer","body":{"volume":0.5,"mute":false}}
```

The guest's own CD player is the real path: NedoOS `cdplay.com` (ZX-Evo, ATM Turbo 2+) reads
the TOC, plays with PLAY AUDIO MSF and polls READ SUB-CHANNEL; `inspect_state cdaudio` then shows
the head it reads.

## WebAPI

```bash
BASE=http://localhost:8090/api/v1
EMU=$(curl -s -X POST $BASE/emulator/start -H 'Content-Type: application/json' -d '{"model":"ATM3"}' | jq -r .id)
curl -s -X POST $BASE/emulator/$EMU/media/cd/insert -H 'Content-Type: application/json' \
     -d "{\"path\":\"$PWD/scratch/cd/music.cue\"}" | jq '{ok, slot, report}'
#  -> report: ["tracks: 1 mode1 0-299, 2 audio 450-1949, 3 audio 2100-3599, 4 audio 3750-5249"]

curl -s $BASE/emulator/$EMU/state/cdaudio | jq '.drives[0] | {slot, tracks: .disc.tracks, audio}'

# Play track 3 to the end of the disc (to=3: only track 3); or lba=X frames=N; or msf=MM:SS:FF end=MM:SS:FF
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

## Assert on

- `drives[].audio.status`: `idle` (15h), `playing` (11h), `paused` (12h), `completed` (13h: the play
  reached its end; the guest's next READ SUB-CHANNEL reports it once), `error` (14h: the play ran
  into a data track). `status_code` is the byte READ SUB-CHANNEL returns.
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
- **A data track plays as silence** and ends a play with `error`; `play track=1` on a mixed disc
  is `bad-request` ("no audio frame").
- **A lone `.bin` of audio needs its `.cue`** (only a BIN with data sync patterns is taken alone).
- **chdman 0.289** writes CD CHDs it cannot read back itself when `cdzs` or `cdzl` is the only
  codec; use its default codecs or two of them ([chd.md](../../docs/file-formats/disk-images/chd.md)).
