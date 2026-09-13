# TSFM volume balance: walkthrough and analysis

Date: 2026-09-13. Everything referenced lives in this folder unless a path is given.

**Status (2026-09-13, later the same day): the config-only fix of §7.1 is applied.** All nine shipped inis under `data/configs/` carry `TSFM_FmTrimDb=7.4`; the staged copies in the build directories and app bundles were refreshed; `TsfmGain_Test` now pins the new one-carrier level (5744 in the FM buffer) and asserts the Pentagon ini carries the 7.4 dB trim; the 16 TSFM/decimator/bypass tests pass. Re-measured in the rebuilt release app with a fresh Pentagon instance (`tsfm_volume_test_unreal-ng_trim7.4.wav`): FM is now **+0.8 dB** above SSG A on the same output for both chips, against +1.2 / +0.4 dB on the hardware (average +0.8). SSG placement and levels are unchanged. The code baseline (`kFmBaseGain`) is still 0.30; moving it to 0.70 waits for the wide-mix / soft-limiter work.

This is the long-form version of `../../verification/volume-balance-report.md`. It walks through the test program, how the hardware recording and the emulator capture were measured, how unreal-ng mixes AY and FM today, what the numbers say, and what to change.

---

## 1. Files

| File | What it is | How it was made |
|---|---|---|
| `tsfm_volume_test.spg` | The test program as run on the hardware. ZX Evolution / TS-Conf "SpectrumProg" container, 3072 bytes, title "TFD player test", built with SPG Builder 1.0. | Supplied. SHA-256 `9f0db630b9a304cb6697862d13fe6512a6e89fc3e74a1e0723be762ad5e39073`. |
| `tsfm_volume_test.mp3` | Recording of that program on a real TS-Conf with a TSFM card. 44.1 kHz stereo, 320 kb/s LAME, 14.7 s. | Supplied. |
| `tsfm_volume_test.asm` | The player, disassembled and annotated, in sjasmplus syntax. The TFMD stream is included verbatim from the SPG with `INCBIN`. Emits the two snapshots with `SAVESNA`. | Disassembled with z80dasm 1.2.0, annotated by hand, verified byte-for-byte (see §3.3). |
| `tsfm_volume_test_48k.sna` | 48K snapshot of the program for Pentagon 128 with `[SOUND] TurboSound=FM`. | `sjasmplus tsfm_volume_test.asm`. SHA-256 `7b7faf0eee8a611c08e4f4b0c2f5b960c033cab5956d82394f1c5bd14dfce9ba`. |
| `tsfm_volume_test_128k.sna` | 128K snapshot, same program, port `#7FFD = 0x10`. | `sjasmplus -DSNA128 tsfm_volume_test.asm`. SHA-256 `82acb1538ac37550d6b40cca91b6d1ff4558959d6df327e1ae4caa5f4f24ddf6`. |
| `tsfm_volume_test_unreal-ng.wav` | The 128K snapshot captured in unreal-ng, 13 s, 44.1 kHz stereo, master mix. | MCP `capture_media audio_capture seconds=13 wav=true` on a Pentagon 128 instance. |

sjasmplus is not in Homebrew. It was built from https://github.com/z00m128/sjasmplus with CMake (about a minute) and is not part of the repository.

---

## 2. The SPG container

The SPG header is 512 bytes. What matters here:

| Offset | Bytes | Meaning |
|---|---|---|
| `0x000` | `TFD player test` | Title |
| `0x020` | `SpectrumProg` | Signature |
| `0x02C` | `10` | Format version 1.0 |
| `0x030` | `00 40` | Start address `0x4000` |
| `0x032` | `FF 7F` | Stack `0x7FFF` (the program moves it to `0x6E00` anyway) |
| `0x050` | `Created by SPG Builder ver.1.0` | |
| `0x400`–`0x5FF` | 512 bytes | Code block, loads at `0x4000` |
| `0x600`–`0xBFF` | 1536 bytes | `TFMD` stream, loads at `0x8000` |

The load addresses are confirmed by the code itself (`JP 0x4003` at the start, `LD HL,0x8004` to find the stream, self-modifying stores into `0x40xx`).

---

## 3. The test program

### 3.1 What it does

It is a TFD stream player, the same family as the RE_TFD player found on the *Sonic 3D Blast* disk in the test data. One stream frame is executed per frame interrupt. Every register write goes through the AY ports with a busy-wait on the YM2203 status byte before the address and again before the data:

```
wait1:  in   f,(c)        ; port #FFFD in status mode: bit 7 = busy
        jp   m,wait1
        out  (c),a        ; register address -> #FFFD
wait2:  in   f,(c)
        jp   m,wait2
        ld   a,(hl)
        ld   b,#BF
        out  (c),a        ; data -> #BFFD
```

The control words used are the TSFM rev C ones: `#F8` selects chip 0 and `#F9` chip 1, both with status reads and FM enabled. Before playback the init routine selects each chip in register mode (`#FE`, `#FF`, FM muted), writes 0 to every register from `0xBE` down to `0x00`, sets the SSG mixer, and writes the prescaler addresses `0x2F` then `0x2D` so both chips end at the default `/6`. It then selects chip 0 in status mode, enables interrupts and idles.

### 3.2 The stream

Stream opcodes, as implemented by the interrupt handler:

| Byte | Meaning |
|---|---|
| `FF` | End of frame. The handler re-selects chip 0 (`#F8`). |
| `FE n` | End of frame and wait `n+3` more frames. |
| `FD` | Select chip 1 (`OUT #FFFD,#F9`). |
| `FC` | Select chip 0 (`OUT #FFFD,#F8`). |
| `FB` | Jump to the loop point. |
| `FA` | Set loop point. Its operands are crossed in the original code; the stream never uses it. |
| other | Register number; the next byte is the data. |

Every stream frame ends with `FE 15` (21 + 3 = 24 frames), so each step of the test lasts exactly 24 frames. The decoded timeline:

| Frame | Chip | Step | Register writes that matter |
|---|---|---|---|
| 0 | 0 | clear + **FM ch0** | all 12 `SL/RR = FF`, key-off ch0..2, SSG `R7 = 3F`, `R8..RA = 0`; voice (below); `A4 = 2E`, `A0 = 25`; `28 = 30` key-on ch0 slots S1+S2 |
| 0 | 1 | clear | same clear sequence, SSG C period `FF0F`, `R7 = 3F` |
| 24 | 0 | FM ch1 | same voice on ch1, `28 = 00` (ch0 off), `28 = 31` |
| 48 | 0 | FM ch2 | `28 = 01`, `28 = 32` |
| 72 | 0 | **SSG A** | period `005E`, `R7 = 3E` (tone A only), `R8 = 0F`; `28 = 02` (FM ch2 off) |
| 96 | 0 | SSG B | `R7 = 3D`, `R8 = 00`, `R9 = 0F` |
| 120 | 0 | SSG C | `R7 = 3B`, `R9 = 00`, `RA = 0F` |
| 144 | 0 / 1 | chip 0 silent, **chip 1 FM ch0** | `R7 = 3F`, `RA = 00`; chip 1 voice + key-on |
| 168, 192 | 1 | FM ch1, ch2 | |
| 216, 240, 264 | 1 | SSG A, B, C | |
| 288 … 552 | | the same 12 steps again (voices already programmed, key-on only) | |
| 576 | 0 / 1 | chip 0 FM ch0 key-on, chip 1 silent | |
| 578 | | `FB` loop to frame 0 | |

The FM voice, written once per channel (register order in the YM2203 is S1, S3, S2, S4):

| Registers | Values | Meaning |
|---|---|---|
| `30/34/38/3C` DT/MUL | `02 01 01 01` | S1: DT 0, MUL 2 (modulator at twice the pitch); S3, S2, S4: DT 0, MUL 1 |
| `40/44/48/4C` TL | `24 7F 00 7F` | S1 at −27 dB, S3 silent, **S2 at 0 dB (full)**, S4 silent |
| `50..5C` KS/AR | `1F` | fastest attack |
| `60..6C`, `70..7C` | `00` | no decay, no sustain decay |
| `80..8C` SL/RR | `0F` | |
| `90..9C` SSG-EG | `00` | off |
| `B0` FB/ALG | `3E` | feedback 7, **algorithm 6** |
| `28` | `30 / 31 / 32` | key-on of slots S1 and S2 only |

In algorithm 6, S1 modulates S2, and S2, S3, S4 are carriers. With S3 and S4 silent and only S1+S2 keyed, the output is **one carrier at full operator level, lightly modulated**. Pitch: block 5, fnum `0x225` = 1166.9 Hz at a 1.75 MHz chip clock. The SSG tone period `0x5E` = 94 gives 1163.6 Hz at the same clock, so FM and SSG steps are at the same pitch and can be compared directly.

**Important:** one carrier at TL 0 is one operator's full output. In ymfm terms it is a word of about ±8168, a quarter of the ±32768 full-scale DAC word. Every level below is for this signal. It is *not* "YM2203 full scale".

### 3.3 The Pentagon build and the byte checks

The only TS-Conf-specific code is three groups of port writes at the start (`#22AF`–`#24AF` interrupt line/frame settings, `#2AAF` interrupt mask, `#15AF` system config). On Pentagon the standard frame interrupt with IM2 and a vector at `I*256 + 0xFF` gives the same behaviour, and the program already sets `I = 0x6E` and stores the handler address at `0x6EFF`. So the source replaces those 25 bytes with NOPs unless `-DTSCONF` is given.

Checks run on the sjasmplus output:

- `-DTSCONF` build: code block **identical** to the SPG's 512 bytes; stream **identical** to the SPG's 1536 bytes.
- 48K and 128K builds: code identical to the SPG except the 25 NOPs; stream identical; 128K extension bytes `PC = 0x4000, #7FFD = 0x10, TR-DOS = 0`; extra RAM pages all zero. sjasmplus fills the screen attribute area with `0x38` and puts its default system variables in place; the program does not use either.
- Both snapshots load in unreal-ng (Pentagon 128, `TurboSound=FM`) and play the test: dominant tone 1158–1168 Hz, same level pattern as the 13 s capture.

---

## 4. How the levels were measured

Both recordings were cut into the 24-frame steps on the stream's own grid and each step's left and right RMS was measured over its middle (from 60 ms after the step start to 30 ms before its end, to exclude key-on/key-off transients and filter settling). Levels are dBFS RMS relative to full scale of the file.

**Aligning to the grid.** At the Pentagon/TS-Conf frame length (71680 T at 3.5 MHz, 48.83 Hz) one step is 24 × 20.48 ms = **0.4915 s**. The recording's hard-left steps (SSG A: loud left, silent right) repeat every 2.95 s, which is six steps of 0.4915 s and not six steps of 0.48 s. That settles two things at once: the hardware ran at the Pentagon frame rate, and the step grid can be anchored on the first SSG A onset. The first SSG A step is step 3 of the loop; loop start in the MP3 is at 0.024 s, in the WAV at −0.035 s (the emulator capture was armed while the program was already running).

Repeats of the same source agreed within 0.1 dB in both recordings, so per-source averages are used below.

The hardware MP3 has exactly one clipped sample (left channel, 13.3 s, during an SSG A step) and no other clipping. FM steps have crest factors of 1.1 to 1.4, which is what a lightly modulated sine should have, so they are not clipped or limited.

---

## 5. Results

### 5.1 Per source, dBFS RMS

| Source | HW left | HW right | unreal-ng left | unreal-ng right |
|---|---|---|---|---|
| chip 0 FM ch0 | −8.0 | −9.2 | −23.2 | −23.2 |
| chip 0 FM ch1 | −8.0 | −9.3 | −23.2 | −23.2 |
| chip 0 FM ch2 | −8.0 | −9.3 | −23.2 | −23.2 |
| chip 0 SSG A | −9.2 | −59.2 | −16.6 | −35.7 |
| chip 0 SSG B | −15.1 | −16.4 | −21.7 | −21.7 |
| chip 0 SSG C | −32.0 | −11.3 | −35.7 | −16.6 |
| chip 1 FM ch0 | −8.8 | −10.0 | −23.2 | −23.2 |
| chip 1 FM ch1 | −8.8 | −10.0 | −23.2 | −23.2 |
| chip 1 FM ch2 | −8.8 | −10.0 | −23.2 | −23.2 |
| chip 1 SSG A | −9.2 | −57.8 | −16.6 | −35.7 |
| chip 1 SSG B | −15.1 | −16.4 | −21.7 | −21.7 |
| chip 1 SSG C | −32.1 | −10.6 | −35.7 | −16.6 |

The two columns have different absolute scales (recorder gain versus emulator master level). Only ratios within a column are meaningful.

### 5.2 Ratios

"Loud side" is the channel a source is panned to: left for SSG A, right for SSG C, either for the centre sources.

| Ratio | HW chip 0 | HW chip 1 | unreal-ng | Emulator minus hardware |
|---|---|---|---|---|
| **FM vs SSG A, same channel** | **+1.2 dB** | **+0.4 dB** | **−6.6 dB** | **−7.4 dB** (range −7.8 to −7.0) |
| SSG B vs SSG A (left) | −5.8 dB | −5.9 dB | −5.1 dB | +0.7 dB |
| SSG C (right) vs SSG A (left) | −2.1 dB | −1.4 dB | 0.0 dB | +0.8 dB after recorder-bias correction |
| FM, left minus right | +1.2 dB | +1.2 dB | 0.0 dB | recorder bias |
| SSG B, left minus right | +1.4 dB | +1.3 dB | 0.0 dB | recorder bias |
| SSG A leakage into right | −50 dB | −49 dB | −19 dB | emulator leaks 30 dB more |
| SSG C leakage into left | −21 dB | −22 dB | −19 dB | within 3 dB |
| chip 1 FM vs chip 0 FM | −0.8 dB | | 0.0 dB | chip-to-chip variation |

**Recorder bias.** Every centre source on the hardware (all FM steps, SSG B) reads 1.2 to 1.4 dB louder on the left. Nothing in the program or board makes a centre source asymmetric, so this is the recording chain. After removing it, SSG C on the right is within about 1 dB of SSG A on the left, and FM is centred, both as the emulator already has them.

---

## 6. Current state of unreal-ng mixing

This is the signal path the emulator capture went through, with the actual constants.

### 6.1 SSG (both devices, `soundchip_ay8910.cpp`)

1. Each channel's 4-bit volume is looked up in a DAC table. TSFM forces the YM2149 table; volume 15 is 1.0.
2. Each channel is panned. In ABC mode: A = 0.9 left / 0.1 right, B = 0.5 / 0.5, C = 0.1 / 0.9.
3. The three channels are summed and divided by 3 per output.
4. A DC blocker removes the square wave's offset, so a full-volume square becomes symmetric.

So one channel at volume 15 on its loud side is `1.0 × 0.9 / 3 = 0.30` peak-to-peak, or **±0.15** after the DC blocker. Its RMS as a square wave is 0.15, which is −16.5 dBFS. The emulator capture measured SSG A at −16.6 dB: the chain and the mix leave it essentially untouched.

The pan numbers explain the rest of the emulator column directly:

- SSG B vs A: `20·log10(0.5 / 0.9)` = −5.1 dB.
- Opposite-side leakage: `20·log10(0.1 / 0.9)` = −19.1 dB, symmetric for A and C.

### 6.2 FM (`soundchip_turbosoundfm.h/.cpp`)

1. ymfm produces one signed 16-bit word per FM sample per chip (the YM2203 output is mono).
2. The word is divided by 32768 and sample-and-held onto the 437.5 kHz grid, then decimated.
3. The result is multiplied by `_fmGain = kFmBaseGain × 10^(TSFM_FmTrimDb / 20)` with `kFmBaseGain = 0.30` and a trim default of 0 dB.
4. The same value is written to the left and right of that chip's FM buffer (centre pan, no attenuation).

The design (`tsfm-tdd.md` §7.1) derived 0.30 as "FM full scale = 2.0 × the AC amplitude of one SSG channel at full volume", the ratio MiSTer uses: 2.0 × 0.15 = 0.30 for a ±32768 word.

For this test's one carrier (word about ±8168): amplitude `8168 / 32768 × 0.30 = 0.075`, sine RMS 0.053 = −25.5 dBFS. The capture measured −23.2 dB. The 2.3 dB difference is the modulation by S1 and the harmonics it adds (the RMS of a modulated carrier at the same peak is higher than a pure sine); the ratio to SSG is what matters and it is measured, not calculated.

### 6.3 Mix (`soundmanager.cpp`, frame end)

1. Per-chip SSG buffers go through the AY character chains (punch on, room off, HQ only). FM buffers go through their chains with punch and room off, so they are a bypass. Beeper likewise.
2. Each source (Beeper, AY1, AY2, FM1, FM2, Covox) has a registry volume, default 1.0, plus mute/solo.
3. Sources are summed in 32-bit and clamped to int16. There is no headroom management: the sum of two SSG chips and two FM chips at full level exceeds int16 and simply clips.

### 6.4 Where the emulator stands against the hardware

| Property | Hardware | unreal-ng now | Verdict |
|---|---|---|---|
| FM carrier vs SSG channel, same output | FM about +0.8 dB (chip average) | FM −6.6 dB | **7.4 dB too quiet** |
| FM placement | centre | centre | correct |
| SSG A / C placement | left / right, equal level | left / right, equal level | correct |
| SSG B level vs A | −5.8 dB | −5.1 dB | 0.7 dB hot, inaudible |
| SSG leakage to the other side | −50 dB (A), −21 dB (C) | −19 dB both | close for C, 30 dB more leakage for A |
| chip 0 vs chip 1 | FM differs by 0.8 dB, SSG equal | equal | chip variation, not worth modelling |

---

## 7. Proposed change

### 7.1 Required: FM gain +7.4 dB

Raise the FM path by 7.4 dB, a linear factor of 2.35. Two ways:

| | Change | Effect |
|---|---|---|
| Immediate, config only | `[SOUND] TSFM_FmTrimDb=7.4` in the shipped inis (`data/configs/*/unreal.ini`) | The trim exists for exactly this. The FM path is linear and its chain is a bypass, so the mix ratio moves by exactly 7.4 dB. |
| Proper, code | `kFmBaseGain` from `0.30` to `0.70` in `soundchip_turbosoundfm.h`; trim default stays 0; §7.1 of `tsfm-tdd.md` cites this measurement instead of the MiSTer 2.0× ratio; `TsfmGain_Test.Reference` moves its one-carrier reference from 0.075 (2458 in int16) to 0.176 (5780), ±5 % | Same result, and the baseline stops carrying a number the hardware contradicts. |

Restated in the design's own terms: the board's FM full scale is not 2.0 × but about **4.7 ×** the AC amplitude of one SSG channel at full volume (2.0 × 2.35).

### 7.2 Consequence: headroom

After the change one carrier reaches 0.18 per channel in the mix. A YM2203 channel can be louder than one carrier (up to four carriers in algorithm 7), a chip has three channels, and there are two chips plus two SSGs. Full-level material will exceed ±1.0 before the int16 clamp and clip. The implementation plan already carries "wide mix + soft limiter (TSFM makes int16 saturation likely)" as a separate design item. This measurement makes it a **prerequisite** of landing the 0.70 baseline rather than a follow-up. Until it lands, apply the correction through the trim so a user with loud FM material can back it off from the Audio Settings dialog.

### 7.3 Optional, small

- SSG B pan 0.5 / 0.5 → about 0.46 / 0.46 to match the hardware's −5.8 dB. 0.7 dB.
- SSG opposite-side pan 0.1 → about 0.09 (−21 dB) to match the C → left leakage. The A → right figure (−50 dB) is asymmetric with C → left on the hardware, which points at the recorder or cable rather than the board's mixer, so a symmetric −21 dB is the defensible target. 2 dB on leakage that is already 19 dB down.

Neither is audible next to the FM correction. Not recommended as part of this change.

### 7.4 No change

FM centre panning, SSG channel-to-side assignment, SSG A versus C level, and per-chip equality all match the hardware.

---

## 8. Clear statement of the difference

- **Today:** a full-level FM carrier is **6.6 dB quieter** than one SSG channel at volume 15 on the same output.
- **Hardware:** the same carrier is **0.4 to 1.2 dB louder** than that SSG channel.
- **Gap:** **7.4 dB**, FM too quiet. Fix: multiply the FM contribution by 2.35, via `TSFM_FmTrimDb=7.4` now and `kFmBaseGain = 0.70` once the mix has headroom.
- Everything else about placement and balance is already right within a decibel.

---

## 9. Reproducing

1. Hardware: run `tsfm_volume_test.spg` on a TS-Conf with a TSFM card; record the line output.
2. Emulator: `sjasmplus -DSNA128 tsfm_volume_test.asm` (or without the define for the 48K build), load the snapshot into a Pentagon 128 instance with `TurboSound=FM`, and capture 13 s of audio with `capture_media audio_capture seconds=13 wav=true`.
3. Analysis: cut both files into 0.4915 s steps starting three steps before the first hard-left SSG A step, measure left and right RMS over each step's middle, and compare "FM versus SSG A on the same channel". The 12-step order is chip 0 FM ch0, ch1, ch2, SSG A, B, C, then the same for chip 1.
