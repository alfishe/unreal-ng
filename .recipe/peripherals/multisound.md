# Recipe: ZX-MultiSound (fit, TSFM, SAA, MIDI, record each source)

The ZX-MultiSound (UzixLS) is one ZX-bus card with a TurboSound FM (two YM2203), a SAA1099, a General Sound with 1 or
2 MB, a SounDrive and a SAM2695 General MIDI synthesizer fed by a serial MIDI line from the first YM2203's I/O port.
No shipped configuration fits it: a test fits it in the slots ([slots.md](../machines/slots.md)). This recipe fits the
card, plays a TurboSound FM tune, a SAA tone and two MIDI notes on it, records each source on its own, reads the card's
state and stops the synthesizer with a MIDI panic. Verified 2026-10-05 against branch `zx-bus-slots` (Pentagon).

Ground truth: the card [multisoundcard.h](../../core/src/emulator/slots/cards/multisound/multisoundcard.h), its report
[multisounddevicestate.cpp](../../core/src/emulator/slots/cards/multisound/multisounddevicestate.cpp), the panic
[midicontrol.h](../../core/src/emulator/sound/midi/midicontrol.h); design
[docs/inprogress/2026-10-03-zx-multisound/](../../docs/inprogress/2026-10-03-zx-multisound/architecture.md); user guide
[docs/features/multisound.md](../../docs/features/multisound.md).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred; the WebAPI is used where MCP has no tool (writing
> a test program into memory). CLI: `multisound`, `midi`, `midi panic`. Lua / Python: `multisound_state()`,
> `midi_state()`, `midi_panic()`.

## The card in one table

| Part | Ports | Options (`zxbus.N.<option>`) | Mixer source |
|:--|:--|:--|:--|
| TurboSound FM (2 x YM2203) | `#FFFD`, `#BFFD` (control byte `#F0-#FF`) | `dip` contains `ym` | per chip select: `ms_ssg1` + `ms_fm1` (0, U4), `ms_ssg2` + `ms_fm2` (1, U10) |
| SAA1099 | `#01FF` address, `#00FF` data (ignored while the OUT runs from `#0000-#3FFF`) | `dip` contains `saa` | `ms_saa` |
| General Sound (16 MHz Z80) | `#B3`, `#BB` | `dip` contains `gs`; `gsRam` = `1m` / `2m` | `ms_pcm` (with the SounDrive) |
| SounDrive | `#0F`, `#1F`, `#4F`, `#5F` | `dip` contains `sd` | `ms_pcm` |
| SAM2695 MIDI | YM2203 U4 register 14 bit 2 (IOA2), 31 250 baud | the bank: `[MIDI] Bank=` | `ms_midi` |

`ctrlMask` = `pro` (the official firmware) or `classic` (an unofficial firmware patch, issue #11: the control byte
`d[7:3] = 11111` while the SAA DIP is off). The card also takes the jobs of a TurboSound FM, a General Sound and a
SounDrive in other slots: fitting it removes those cards (and the plan says so).

**The MIDI bank.** The synthesizer plays a General MIDI SoundFont: by default GeneralUser GS 2.0.3, shipped from
[data/midi/](../../data/midi/README.md) next to the executables (`midi/generaluser-gs.sf2`; macOS bundles:
`Contents/Resources/midi/`), found without any setting: `audio_midi` reports `bank loaded` with its name. `[MIDI]
Bank=` names another bank (looked up in the working directory, next to the executable, then in the resources);
`Bank=NONE` loads none (`bank: no bank`, the MIDI row stays silent). The bank is read when the card is built: a bank
change needs a restart (any slot change).

## MCP (preferred)

```text
# 1. Fit the card: a Pentagon created with it (no TurboSound FM in the socket: the card has its own)
emulator_manage {"action":"create","model":"PENTAGON","slots":{"zxbus.1":"multisound","zxbus.1.gsRam":"2m"}}
#    or on a running machine (the TSFM, NeoGS and SounDrive go; the machine restarts):
emulator_manage {"action":"slots_plug","slot":"zxbus.next","card":"multisound","options":"gsRam=2m",
                 "replace_if_incompatible":true}
inspect_state   {"aspects":["slots","audio_multisound","audio_midi"]}
#   [audio_multisound] ZX-MultiSound (UzixLS) in zxbus.4, dip=ym,saa,gs,sd gsRam=2m ctrlMask=pro; FM muted, SAA off,
#                      GS booting, MIDI 0 byte(s), 0 voice(s)
#     built-in ay: shadowed by zxbus.4
#   [audio_midi] bank loaded, voices 0/38, bytes 0, framing errors 0

# 2. A TurboSound FM tune (a 128K snapshot that plays TFM music through #FFFD / #BFFD); TTD before the program runs
load_software     {"path":"testdata/sound/tsfm/tech_support.sna"}
time_travel       {"action":"start"}
control_execution {"action":"run_frames","frames":150}
inspect_state     {"aspects":["audio_multisound"]}        # FM on, GS ready
capture_media     {"action":"audio_capture","seconds":2,"source":"ms_fm2"}   # chip select 1 (U10): the bass
#   dominant ~2390 Hz, RMS L 0.067 (a TSFM in the socket, source fm2, at the same moment: RMS 0.067)
capture_media     {"action":"audio_capture","seconds":2,"source":"ms_fm1"}   # chip select 0 (U4)
#   dominant ~424 Hz, RMS L 0.0078 (TSFM fm1: 0.0078)
capture_media     {"action":"audio_capture","seconds":2,"source":"ms_ssg1"}  # ms_ssg2: the other chip's SSG
```

The rows are per chip like the TurboSound FM's (`ay1` / `ay2` / `fm1` / `fm2` there): `ms_ssg1`, `ms_ssg2`, `ms_fm1`,
`ms_fm2`, then `ms_saa`, `ms_pcm` (GS + SounDrive DACs), `ms_midi`; the HUD lights one indicator per row. The FM rows
use the TurboSound FM's calibration, `[SOUND] TSFM_FmTrimDb` (7.4 dB shipped; the audio settings' "FM trim" drives
both boards; the report shows `ym.fm_trim_db`): the same tune comes out at the same FM level on the card and on a TSFM.
The card's SSG rows sit 7.6 dB lower than a TSFM's - the board's own resistor weights (SSG 24 k against FM 10 k).
Verified 2026-10-05 on a Pentagon (live instance, the numbers above).

The WebAPI report has the details behind the summary line: `ym.fm_trim_db` is the FM calibration in force,
`ym.chips[0..1].fm` is the TurboSound FM chip report
(`keyed_channels`, operators, envelopes), `ym.chips[0..1].ssg` the AY chip report, `logic` the CPLD latches.

## SAA1099 and MIDI: a test program

MCP has no memory-write tool; the WebAPI writes the program and sets PC (a debugger edit; it is a replay barrier in a
TTD recording). The SAA ports are locked while the OUT runs from the ROM, so the program runs at `#8000`.

```bash
BASE=http://localhost:8090/api/v1
EMU_ID=$(curl -s "$BASE/emulator" | jq -r '.emulators[0].id')

# SAA1099: voice 0 full amplitude, tone #80 octave 4, frequency enable, sound enable; the control byte #F2 first
# (U4 selected, register reads, FM unmuted, SAA clock on)
python3 - <<'EOF' > /tmp/saa.json
def out(port, val): return [0x01, port & 0xFF, port >> 8, 0x3E, val, 0xED, 0x79]   # LD BC,port; LD A,val; OUT (C),A
p = [0xF3] + out(0xFFFD, 0xF2)
for reg, val in [(0x00, 0xFF), (0x08, 0x80), (0x10, 0x04), (0x14, 0x01), (0x1C, 0x01)]:
    p += out(0x01FF, reg) + out(0x00FF, val)
p += [0x18, 0xFE]                                                                      # JR $
import json; print(json.dumps({"address": 0x8000, "data": p}))
EOF
curl -s -X POST "$BASE/emulator/$EMU_ID/pause"
curl -s -X POST "$BASE/emulator/$EMU_ID/memory/write" -H 'Content-Type: application/json' -d @/tmp/saa.json
curl -s -X PUT "$BASE/emulator/$EMU_ID/registers/pc" -H 'Content-Type: application/json' -d '{"value":32768}'
```

```text
control_execution {"action":"run_frames","frames":10}
capture_media     {"action":"audio_capture","seconds":1,"source":"ms_saa"}
#   Audio 1 s @ 44100 Hz - dominant 653 Hz          (8 MHz x 2^4 / 512 / (511 - 128) = 652.7 Hz)
```

MIDI: the program sends each bit of the bytes `90 3C 64 91 40 64` (Note On C4 on channel 1, E4 on channel 2) to YM
register 14, 112 T-states apart (31 250 baud at 3.5 MHz), as MIDI software on the card does:

```bash
python3 - <<'EOF' > /tmp/midi.json
def out(port, val): return [0x01, port & 0xFF, port >> 8, 0x3E, val, 0xED, 0x79]
def ym(reg, val): return out(0xFFFD, reg) + out(0xBFFD, val)
HI, LO = 0xFF, 0xFB                                    # R14 with IOA2 = 1 (idle) / 0
line = [HI, HI]
for b in [0x90, 0x3C, 0x64, 0x91, 0x40, 0x64]:
    line += [LO] + [HI if (b >> i) & 1 else LO for i in range(8)] + [HI]      # start, 8 data bits, stop
org, table = 0x8000, 0x9000
p = [0xF3] + out(0xFFFD, 0xF2) + ym(0x0E, HI) + ym(0x07, 0x7F) + out(0xFFFD, 0x0E)   # IOA an output, R14 selected
p += [0x01, 0xFD, 0xBF, 0x21, table & 0xFF, table >> 8, 0x16, len(line)]           # LD BC,#BFFD; LD HL,table; LD D,n
loop = org + len(p)
p += [0x7E, 0xED, 0x79, 0x23] + [0x1E, 0x00] * 9                                    # LD A,(HL); OUT (C),A; INC HL; delay
nxt = org + len(p) + 3
p += [0xC3, nxt & 0xFF, nxt >> 8, 0x15, 0xC2, loop & 0xFF, loop >> 8, 0x18, 0xFE]  # JP next; DEC D; JP NZ,loop; JR $
import json; print(json.dumps([{"address": org, "data": p}, {"address": table, "data": line}]))
EOF
curl -s -X POST "$BASE/emulator/$EMU_ID/pause"
jq -c '.[0]' /tmp/midi.json | curl -s -X POST "$BASE/emulator/$EMU_ID/memory/write" -H 'Content-Type: application/json' -d @-
jq -c '.[1]' /tmp/midi.json | curl -s -X POST "$BASE/emulator/$EMU_ID/memory/write" -H 'Content-Type: application/json' -d @-
curl -s -X PUT "$BASE/emulator/$EMU_ID/registers/pc" -H 'Content-Type: application/json' -d '{"value":32768}'
```

```text
control_execution {"action":"run_frames","frames":5}
inspect_state     {"aspects":["audio_midi"]}
#   [audio_midi] bank loaded, voices 2/38, bytes 12, framing errors 0
#     ch1 prog 1 Grand Piano: C4
#     ch2 prog 1 Grand Piano: E4
capture_media     {"action":"audio_capture","seconds":1,"source":"ms_midi"}
#   Audio 1 s @ 44100 Hz - dominant 376 Hz (C4 + E4 with their harmonics)

# MIDI panic: every voice stops (programs and the MIDI stream in progress stay); a TTD input, replayed by a seek
invoke_api        {"method":"POST","path":"/api/v1/emulator/{id}/control/audio/midi","body":{"action":"panic"}}
control_execution {"action":"run_frames","frames":2}
inspect_state     {"aspects":["audio_midi"]}               # voices 0/38
```

The CLI shows the same: `midi` prints one line per part (`ch  1  prog   1 Grand Piano  vol 100 pan  64 voices  1 C4`),
`multisound` the card in a few lines (`--full` every register, `--json` the WebAPI object); the Qt window Tools > MIDI
Activity shows the parts and the notes on a key strip, with a Panic button.

## Pitfalls

- **The synthesizer ignores MIDI for 50 ms after a reset.** A program that sends right after the machine was created
  or restarted loses its bytes: `counters.dropped_busy` counts them. Run a few frames first.
- **The SAA and SounDrive ports are locked while the OUT runs from `#0000-#3FFF`** (the card's ROM-fetch lock): BASIC
  `OUT` does not reach them; run code from RAM.
- **The control byte decides who answers `#FFFD`:** `#F2` selects U4 (the MIDI chip) with FM on and the SAA clock on;
  without it the YM writes go to whichever chip was selected and the SAA clock stays off.
- **A panic during a TTD replay is refused** ("a TTD replay owns the input"); while recording it is journaled.
- **Changing the card's options or the bank restarts the machine** (a slot change: a new emulator id); refused while
  TTD records.
- **No shipped config fits the card** (owner decision): create the machine with `"slots"` or plug it in.
