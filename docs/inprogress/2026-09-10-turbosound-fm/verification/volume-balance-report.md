# TSFM volume balance: real hardware vs unreal-ng

Date: 2026-09-13. Material in `../materials/volume/`. Long-form walkthrough: `../materials/volume/README.md`.

**Applied:** `TSFM_FmTrimDb=7.4` in all shipped inis, tests updated and green, re-measured in the app: FM − SSG A = +0.8 dB (hardware +1.2 / +0.4). Details at the top of the walkthrough.

| File | What it is |
|---|---|
| `tsfm_volume_test.spg` | The test program as run on the hardware (ZX Evolution / TS-Conf, SPG Builder 1.0). SHA-256 `9f0db630…9073`. |
| `tsfm_volume_test.asm` | Annotated, assemblable source (sjasmplus): the player plus the TFMD stream pulled in from the SPG. Emits the snapshots below via `SAVESNA`. |
| `tsfm_volume_test.mp3` | Recording of the program on real hardware (TS-Conf + TSFM card). 44.1 kHz stereo, 320 kb/s, 14.7 s. |
| `tsfm_volume_test_48k.sna` | `sjasmplus tsfm_volume_test.asm`. 48K snapshot, Pentagon 128 with `TurboSound=FM`. SHA-256 `7b7faf0e…ce9ba`. |
| `tsfm_volume_test_128k.sna` | `sjasmplus -DSNA128 tsfm_volume_test.asm`. 128K snapshot, same program. SHA-256 `82acb153…ddf6`. |
| `tsfm_volume_test_unreal-ng.wav` | The 128K snapshot captured in unreal-ng (13 s, 44.1 kHz, master mix). |

Byte checks done on the builds: the `-DTSCONF` build reproduces the SPG's 512-byte code block exactly; the Pentagon builds differ from it only in the 25 bytes of TS-Conf port writes replaced by NOPs; the stream is byte-identical to the SPG in all three. Both snapshots were loaded in unreal-ng and play the test (dominant tone 1158–1168 Hz, same level pattern).

## 1. What the test program does

The player is a TFD stream player: one stream frame per frame interrupt, register writes go through `#FFFD` / `#BFFD` with a busy-wait on the YM2203 status byte before every write. Chip selection uses the rev C control words `#F8` (chip 0) and `#F9` (chip 1), both with FM enabled and status reads on. The init clears every register of both chips and sets prescaler `/6` (`#2F` then `#2D`).

The stream plays **one source at a time, at maximum level, for 24 frames each**, all at the same pitch (about 1165 Hz):

| Step | Source | How "maximum" is produced |
|---|---|---|
| 1–3 | chip 0 FM channel 0, 1, 2 | Algorithm 6, feedback 7, key-on of slots S1+S2 only. S2 is a carrier at TL 0 (full level), S1 modulates it lightly (TL 0x24). S3 and S4 are at TL 0x7F. Block 5, fnum 0x225. |
| 4–6 | chip 0 SSG A, B, C | Tone period 0x5E, volume 15, no envelope, only that channel un-muted in R7. |
| 7–12 | chip 1, same six steps | |

The 12 steps repeat; the stream loops after 578 frames. At the Pentagon and TS-Conf frame length (71680 T, 48.83 Hz) one step is 0.4915 s, which is exactly the step period seen in the recording. The measured FM pitch (1166 Hz) also confirms a 1.75 MHz chip clock.

**Note on "FM at max volume".** One carrier at TL 0 is one operator's full output, not the chip's full-scale DAC word. In ymfm terms this tone is a word of about ±8168, a quarter of ±32768. Both measurements below are of exactly this signal, so the comparison is valid, but the number must not be read as "YM2203 full scale".

## 2. Method

Both recordings were cut into the 24-frame steps on the stream's grid (aligned on the first SSG A step, which is the hard-left one), and the RMS level of each channel was measured over the middle of every step (60 ms after the start to 30 ms before the end, to skip key-on and key-off transients). Levels are dBFS RMS. The hardware program was run from the SPG on the real machine and recorded to MP3; the emulator ran the SNA in unreal-ng (Pentagon 128, `data/configs/pentagon128k/unreal.ini`, `TurboSound=FM`, `TSFM_FmTrimDb=0`, HQ on) and was captured through the MCP audio capture (master mix, post character chains).

Repeats of the same source agreed within 0.1 dB in both recordings, so the tables give the average per source.

## 3. Results

### 3.1 Per source, dBFS RMS

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

Absolute levels are not comparable between the two columns (recorder gain vs emulator master volume). Only the ratios within a column matter.

### 3.2 Balance ratios

"Loud side" means the channel a source is panned to (left for SSG A, right for SSG C, either for centre sources).

| Ratio | HW chip 0 | HW chip 1 | unreal-ng | Difference (emulator − HW) |
|---|---|---|---|---|
| **FM vs SSG A, same channel** | **+1.2 dB** | **+0.4 dB** | **−6.6 dB** | **−7.4 dB (−7.8 … −7.0)** |
| SSG B vs SSG A (left) | −5.8 dB | −5.9 dB | −5.1 dB | +0.7 dB |
| SSG C (right) vs SSG A (left) | −2.1 dB | −1.4 dB | 0.0 dB | see recorder bias below |
| FM left − right | +1.2 dB | +1.2 dB | 0.0 dB | see recorder bias below |
| SSG B left − right | +1.4 dB | +1.3 dB | 0.0 dB | see recorder bias below |
| SSG A bleed into right | −50 dB | −49 dB | −19 dB | emulator bleeds 30 dB more |
| SSG C bleed into left | −21 dB | −22 dB | −19 dB | within 3 dB |
| chip 1 FM vs chip 0 FM | −0.8 dB | | 0.0 dB | chip-to-chip variation, not modelled |

**Recorder bias.** Every centre source in the hardware recording (FM, SSG B) reads 1.2–1.4 dB louder on the left. That is the recording chain, not the board. Correcting for it, SSG C on the right is within about 1 dB of SSG A on the left, and FM is centred, both as the emulator has them.

**Recording quality.** The hardware MP3 has one clipped sample in the left channel (at 13.3 s, during an SSG A step) and no clipping elsewhere. Sine crest factors on the FM steps are 1.1–1.4, as expected for a lightly modulated carrier, so the FM steps are not clipped.

## 4. Findings

1. **FM is about 7.4 dB too quiet in unreal-ng.** On hardware a single full-level carrier is slightly louder than one SSG channel at volume 15 on the same output (+0.4 to +1.2 dB RMS). In unreal-ng it is 6.6 dB quieter. The design's §7.1 basis ("FM full scale = 2.0 × the AC amplitude of one SSG channel", taken from MiSTer) undershoots the real board by a factor of about 2.35. Expressed the same way, the measured board gives about **4.7 ×**.
2. **SSG stereo is right.** A left, B centre 5–6 dB down per channel, C right. The emulator's B is 0.7 dB hotter relative to A than the hardware, which is inside what a listening test can tell.
3. **SSG crosstalk differs, asymmetrically.** The hardware shows essentially no A → right leakage (−50 dB) but −21 dB C → left leakage. The emulator uses a symmetric −19 dB. The asymmetry is almost certainly the board's or the recorder's ground/cable path, not a mixer design feature, so no change is recommended from this one recording.
4. **The two chips differ by 0.8 dB on FM** on the hardware, none on SSG. Chip variation; not worth modelling.

## 5. Changes required in the level mixing

**Required: raise the FM gain by 7.4 dB (× 2.35).**

- Immediate, config-only: `[SOUND] TSFM_FmTrimDb=7.4` in the shipped inis. This is exactly the knob §7.1 provided for this measurement.
- Proper: change the hardware-derived baseline `kFmBaseGain` in `soundchip_turbosoundfm.h` from `0.30` to `0.70` (0.30 × 10^(7.4/20)), update §7.1 of the design to cite this measurement instead of the MiSTer 2.0 × ratio, and keep the trim default at 0. `TsfmGain_Test.Reference` pins 0.075 for one carrier and must move to 0.176 (±5 %). The FM path is linear and its chain is bypass, so the master-mix ratio shifts by exactly the gain change; no re-measurement is needed for the ratio, only for headroom.

**Consequence: headroom.** After the change one carrier reaches 0.18 per channel in the mix and two chips playing three loud channels each can exceed ±1.0 before the int16 conversion. The implementation plan already lists "wide mix + soft limiter (TSFM makes int16 saturation likely)" as a separate design; this measurement makes it a prerequisite of the gain change rather than a follow-up. Until it lands, the trim is the safe way to apply the correction, and users with loud FM material can back it off.

**Optional, small:** SSG B pan could drop from 0.5 to about 0.46 per side to match the hardware's −5.8 dB, and the SSG opposite-side pan from 0.1 to about 0.09 (−21 dB) to match the C → left figure. Neither is audible next to the FM correction.

**No change:** FM centre panning, SSG A/C channel assignment, SSG A vs C level, per-chip equality.

## 6. How to reproduce

1. Hardware: run `tsfm_volume_test.spg` on a TS-Conf with a TSFM card, record the line output.
2. Emulator: assemble with sjasmplus (`sjasmplus -DSNA128 tsfm_volume_test.asm`, or the 48K build without the define), load the snapshot into a Pentagon 128 instance with `TurboSound=FM`, capture 13 s of audio (`capture_media audio_capture seconds=13 wav=true`).
3. Cut both into 0.4915 s steps starting at the first hard-left SSG A step minus three steps, measure RMS per channel over each step's middle, and compare "FM vs SSG A on the same channel".

The Pentagon builds differ from the SPG only in that the TS-Conf interrupt-configuration port writes (`#22AF`–`#24AF`, `#2AAF`, `#15AF`) are replaced by NOPs; the IM2 vector at `0x6EFF` and everything else are byte-identical. Code lives at `0x4000` (page 5), the TFMD stream at `0x8000` (page 2), entry `0x4000`; the 128K snapshot sets port `#7FFD = 0x10`. sjasmplus is not in Homebrew; it was built from https://github.com/z00m128/sjasmplus (CMake, one minute).
