# Recipe: Sprinter sound (AY, Covox, Covox-Blaster)

The Sprinter Sp2000 has one AY-3-8910 (1.75 MHz, ABC stereo), the beeper, and one 16-bit stereo DAC that
the PLD drives as a plain **Covox** (port `#FB`) or as the **Covox-Blaster** (CBL): a 256-sample ring the PLD
plays at 7.8-109 kHz, refilled by the program on an interrupt every 128 samples. This recipe plays a WAV with
DSS's own player `WAVPLAY.EXE` from a hard disk, reads the device state and records the sound.

Ground truth: design [tdd-accel-sound-input.md](../../docs/inprogress/2026-09-28-sprinter/tdd-accel-sound-input.md)
§2 / §2.1, results against MAME [s6-sound-outcome.md](../../docs/inprogress/2026-09-28-sprinter/s6-sound-outcome.md),
code `core/src/emulator/sound/sprinter/covoxblaster.{h,cpp}`.

> **How to use the sections:** [WebAPI](#webapi-verified) was run end to end (2026-10-02). With MCP use the
> same calls through `invoke_api` (`emulator_manage` creates the machine, `media` inserts the disk). Policy:
> [_common/transports.md](../_common/transports.md).

## Before you start

- **BIOS 3.06 or 3.07.** DSS 1.71 (the disk below) needs it. Create the machine with it
  (`{"model":"SPRINTER","sprinter":{"bios":"3.06"}}`, MCP `emulator_manage` `sprinter_bios`) or switch a running
  one (`POST /sprinter/bios {"bios":"3.06","reset":true}`); `[ROM] SPRINTER=` in `configs/sprinter/unreal.ini`
  still sets the default ([sprinter.md](sprinter.md) step 1).
- **The disk.** The MAME pack's system disk `sp_hdd_sys.img` (raw, 1 GiB, DSS 1.71.57; not in the repository)
  has the players in `BIN\`: `PT3PLAY.EXE` (AY), `WAVPLAY.EXE` (Covox-Blaster), `PROPLAY.EXE` (MOD files
  through a General Sound card on the ISA bus: not emulated yet). Its `SYSTEM.BAT` starts Flex Navigator;
  a copy with one command instead plays without any key presses.

## WebAPI (verified)

```bash
BASE=http://localhost:8090/api/v1           # UNREAL_WEBAPI_PORT moves the port
SYS=/path/to/sp_hdd_sys.img                 # raw image of the pack's sp_hdd_sys.chd (chdman extractraw)

# 1. A copy of the system disk whose SYSTEM.BAT plays one WAV (the FAT16 partition is at LBA 63)
cp -c "$SYS" scratch/sp-wav.img             # APFS clone: instant
mcopy -o -i scratch/sp-wav.img@@32256 mission.wav ::MISSION.WAV       # any 8 / 16-bit, mono / stereo PCM WAV
printf '@echo off\r\nset PATH=%%BOOTDSK%%\\;%%BOOTDSK%%\\BIN\\;\r\nwavplay c:\\mission.wav\r\n' > scratch/SYSTEM.BAT
mcopy -o -i scratch/sp-wav.img@@32256 scratch/SYSTEM.BAT ::

# 2. The machine with BIOS 3.06, the disk on the primary master (session: the copy is not written)
EMU_ID=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
         -d '{"model":"SPRINTER","sprinter":{"bios":"3.06"}}' | jq -r .id)
curl -s -X POST "$BASE/emulator/$EMU_ID/pause" > /dev/null
curl -s -X POST "$BASE/emulator/$EMU_ID/media/ide0.master/insert" -H 'Content-Type: application/json' \
     -d "{\"path\":\"$PWD/scratch/sp-wav.img\",\"access\":\"session\"}" | jq -c '{ok}'
curl -s -X POST "$BASE/emulator/$EMU_ID/reset" > /dev/null
curl -s -X POST "$BASE/emulator/$EMU_ID/pause" > /dev/null

# 3. Boot and start the player: ~370 frames from a full start (PLD load, BIOS, DSS 1.71)
curl -s -X POST "$BASE/emulator/$EMU_ID/run_frames" -H 'Content-Type: application/json' -d '{"frames": 450}' | jq -c '{status}'

# 4. The device state
curl -s "$BASE/emulator/$EMU_ID/state/sprinter" | jq -c '.sound.covox_blaster | {control, mode, stereo, bits, int_enabled, rate_hz, tick_tstates, int_requests}'
#   MISSION.WAV (8-bit mono 22 050 Hz):  {"control":"0x9B","mode":"covox-blaster","stereo":false,"bits":8,"int_enabled":true,"rate_hz":21875.0,...}
#   a 16-bit stereo 44.1 kHz WAV:        {"control":"0xFD",...,"stereo":true,"bits":16,"rate_hz":43750.0,"tick_tstates":80,...}
curl -s "$BASE/emulator/$EMU_ID/state/sprinter" | jq -c '.sound.ay | {chips, clock_hz, stereo}'
#   {"chips":1,"clock_hz":1750000,"stereo":"ABC"}
curl -s "$BASE/emulator/$EMU_ID/state/audio/covox" | jq -c '{device, fitment, mode, control}'
#   {"device":"Covox-Blaster","fitment":"machine","mode":"covox-blaster","control":"0x9B"}

# 5. The sound in emulated time: 2 s of the master mix as a WAV
curl -s -X POST "$BASE/emulator/$EMU_ID/audio/capture" -H 'Content-Type: application/json' -d '{"action":"start","seconds":2}' > /dev/null
curl -s -X POST "$BASE/emulator/$EMU_ID/run_frames" -H 'Content-Type: application/json' -d '{"frames": 100}' > /dev/null
curl -s "$BASE/emulator/$EMU_ID/audio/capture/result?wav=true" | jq -c '{duration_seconds, left: .left.rms, right: .right.rms, wav_path}'

# 5a. One device instead of the master mix: the DAC's own buffer (before mute / volume), the ring, the mixer
curl -s -X POST "$BASE/emulator/$EMU_ID/audio/capture" -H 'Content-Type: application/json' \
     -d '{"action":"start","seconds":1,"source":"covox"}' | jq -c '{armed, source}'
#   {"armed":true,"source":"covox"}
curl -s -X POST "$BASE/emulator/$EMU_ID/run_frames" -H 'Content-Type: application/json' -d '{"frames": 60}' > /dev/null
curl -s "$BASE/emulator/$EMU_ID/audio/capture/result" | jq -c '{duration_seconds, left: .left.rms, dominant_hz}'
#   a 440 Hz 8-bit mono 22 050 Hz WAV played at 21 875 Hz: {"duration_seconds":1.0,"left":0.2756,"dominant_hz":436.5}
#   (source "ay1" at the same time: rms 0 - WAVPLAY does not touch the AY)
curl -s "$BASE/emulator/$EMU_ID/state/sprinter/sound/ring" | jq -c '{mode, play_index, write_index, playing_half, row: .rows[4]}'
#   {"mode":"covox-blaster","play_index":"0xC8","write_index":"0x80","playing_half":"upper (#80-#FF)",
#    "row":"40: 8400  9100  9D00  A800  B400  BE00  C700  CF00  D600  DC00  E000  E200  E300  E300  E100  DD00 "}
curl -s -X PUT "$BASE/emulator/$EMU_ID/audio/mixer/covox" -H 'Content-Type: application/json' -d '{"gain_db":-6}' \
     | jq -c '.devices[] | select(.source == "covox") | {name, volume, gain_db}'
#   {"name":"Covox-Blaster","volume":0.501187...,"gain_db":-6.0}     (muted / solo the same way; GET /audio/mixer lists all)

# 6. The same as an MP4 with an AAC track (video + audio of 150 frames = 3.072 s)
curl -s -X POST "$BASE/emulator/$EMU_ID/video/record" -H 'Content-Type: application/json' \
     -d "{\"action\":\"start\",\"format\":\"h264\",\"audio\":\"aac\",\"filename\":\"$PWD/scratch/sprinter-wav.mp4\"}" | jq -c '{audio, audio_codec}'
curl -s -X POST "$BASE/emulator/$EMU_ID/run_frames" -H 'Content-Type: application/json' -d '{"frames": 150}' > /dev/null
curl -s -X POST "$BASE/emulator/$EMU_ID/video/record" -H 'Content-Type: application/json' -d '{"action":"stop"}' | jq -c '{output, file_size}'
ffprobe -v error -show_entries stream=codec_name,duration -of compact scratch/sprinter-wav.mp4
#   stream|codec_name=h264|duration=3.071667
#   stream|codec_name=aac|duration=3.071979
```

Per-channel pitch of a capture (the `dominant_hz` of step 5 mixes both channels):

```bash
python3 -c "
import numpy as np, sys; from scipy.io import wavfile
sr, d = wavfile.read(sys.argv[1]); d = d[:, :2].astype(float); d -= d.mean(axis=0)
f = np.fft.rfftfreq(len(d), 1 / sr)
print([round(f[np.argmax(np.abs(np.fft.rfft(d[:, c] * np.hanning(len(d)))))], 1) for c in (0, 1)])" capture.wav
#   a WAV with 440 Hz left and 1000 Hz right at 44.1 kHz, played at 43.75 kHz: [436.4, 992.0]
```

PT3 (AY): the same steps with `pt3play c:\gogin.pt3` in `SYSTEM.BAT`; the AY state is `GET /state/audio/ay`,
its own sound `{"source":"ay1"}`.

The same on the other interfaces: CLI `state sprinter ring`, `mixer covox gain_db=-6`, `audiocapture start 1
covox`; Lua `sprinter_sound_ring()`, `audio_mixer_set("covox", {gain_db=-6})`, `audio_capture_start(1, "covox")`;
Python the same names (`audio_capture_start(1.0, source="covox")`); MCP `inspect_state` aspects
`sprinter_sound_ring`, `audio_mixer`, `capture_media` `audio_capture` with `source`. The `sprinter` aspect's
summary has a sound line (`sound: CBL 8-bit mono 21875 Hz, ring play 0xC8 / write 0x80, DAC ..., AY ABC`).

## The same on MAME

`tools/machines/sprinter/mame-capture/mame-capture.sh` writes MAME's sound with `SPC_WAV` (`-wavwrite`, 48 kHz,
channels 0-1 hold the speakers) and traces the Covox-Blaster ports with `SPC_CODES=88-89`:

```bash
chdman createhd -i scratch/sp-wav.img -o scratch/sp-wav.chd -chs 4096,16,32
MAME_BIN=<mame with sprinter> MAME_ROMPATH=<pack>/roms SPC_BIOS=v3.06 SPC_HARD1=scratch/sp-wav.chd \
  SPC_WAV=scratch/mame-wav.wav tools/machines/sprinter/mame-capture/mame-capture.sh boot SPC_END=1900 SPC_CODES=88-89
```

MAME plays 16-bit stereo with the channels swapped (left on the right); the rates, the INT count and the tempo
agree ([s6-sound-outcome.md](../../docs/inprogress/2026-09-28-sprinter/s6-sound-outcome.md) §3).

## The device in one table

| Port (code) | What |
|---|---|
| `#FB` / `#4F` (`#88`) | a sample: to the DAC (Covox) or into the ring (CBL) |
| `#4E` / `#0046` (`#89`) | control: 7 CBL on, 6 stereo, 5 16-bit, 4 INT on, 3-0 rate (`#08` 7.8 ... `#0F` 109.4 kHz; `#00` / `#01` 15.6 / 21.9 kHz) |
| `#FE` (`#40`) read | CBL on: bit 7 = the half of the ring that needs data, bit 5 = the beam below line 272 |
| RAM page `#FD` | accelerator copies into it also feed the ring while the CBL INT is on |
| `#FFFD` / `#BFFD` (`#90` / `#91`, read `#52`) | the AY |

## Pitfalls

| Symptom | Cause |
|---|---|
| WAVPLAY sounds for half a second, then silence | `[SPRINTER] AccelIntSuspend=1`: the player refills the ring with accelerator copies in its INT handler; the default is 0 |
| "Can't open file" | DSS starts in `C:\BIN`: give the player a full path (`c:\mission.wav`) |
| "hard resync - dropped ... of overfilled audio" in the app log | `run_frames` produces frames faster than real time; the GUI's audio device drops the excess. Captures (step 5) and recordings (step 6) are in emulated time and unaffected |
| `PROPLAY.EXE` finds no General Sound | MOD files go to a GS card on the ISA ZX-bus adapter: not emulated yet (phase S6b) |
| A MAME CHD boots on the first run only | MAME keeps the guest's writes in `diff/<name>.dif`; the capture script removes it before each run |
