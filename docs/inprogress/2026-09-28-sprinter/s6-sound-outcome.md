# S6 outcome: sound (2026-10-02)

Branch `sprinter-s6`. Phase S6 of [roadmap-and-plan.md](roadmap-and-plan.md): the AY at 1.75 MHz as one
chip, the Covox, the Covox-Blaster with its ring, rates, interrupt, `#FE` bits, 16-bit DAC and the
accelerator's page-`#FD` path, the ISA register stub. Design: [tdd-accel-sound-input.md](tdd-accel-sound-input.md)
§2; what was built and where it differs: §2.1 there. Recipe: [.recipe/machines/sprinter-sound.md](../../../.recipe/machines/sprinter-sound.md).

## 1. What the Covox-Blaster does, in short

The Covox-Blaster (CBL) is a Covox with a 256-entry sample ring that the PLD plays on its own clock. A
player writes control `#9B` to port `#4E` (CBL on, mono, 8-bit, interrupt on, 21.875 kHz); every 128 samples
the PLD raises an INT (vector `#FF`), and the handler sends the next 128 bytes - with `OTIR` to port `#FB`, or
with one accelerator copy into RAM page `#FD`, which the PLD also feeds into the ring. Port `#FE` bit 7 tells
which half needs data. Example: at 21.875 kHz a 30-second WAV takes 5 120 interrupts, one every 5.85 ms.

## 2. Devices

| Device | Built | Notes |
|---|---|---|
| AY-3-8910 | one chip (`[SOUND] TurboSound=Single`, new), 1.75 MHz, ABC | the clock was already 3.5 MHz / 2 for every model; the ini's `FQ=1774400` (never read) now says 1750000; TurboSound FM is gone from the Sprinter config |
| Beeper / tape out | unchanged (`#FE` bits 4 / 3) | |
| Covox | code `#88` with CBL off, both channels | the shared `Covox` / SoundDrive is not fitted (`SD=0`, `CovoxFB=0`): the Sprinter's own DAC holds the COVOX mixer slot |
| Covox-Blaster | `CovoxBlaster` (`core/src/emulator/sound/sprinter/`): ring, 16 rates, mono / stereo, 8 / 16 bit, INT, `#FE` bits 7 / 5, page `#FD` | PLD-exact write addressing and continuous output (tdd §2.1) |
| 16-bit DAC | the mixer row "Covox-Blaster": `(word - #8000) / 2` | |
| ISA register stub | code `#1B` (A19-A14) and the window-3 ISA view, as built in S1 | no new state; id 33 stays reserved for S6b |

## 3. Real software against MAME

Disk: a clone of the MAME pack's DSS 1.71 system disk (`sp_hdd_sys.img`) with the media files in the root and
`SYSTEM.BAT` running one player (recipe, step 1); BIOS 3.06 Hotfix 2 here, MAME `-bios v3.06`, the same disk as
a CHD. Our audio: the emulated-time capture of the master mix (`/audio/capture`); MAME's: `-wavwrite` (new
`SPC_WAV` option of `tools/machines/sprinter/mame-capture/`). Both machines run DSS at 21 MHz.

| Program, file | Measure | unreal-ng | MAME | Expected |
|---|---|---|---|---|
| `PT3PLAY.EXE` (VTII PT3 Player r.7), `GOGIN.PT3` | spectrum peaks (20 s) | 391.70, 465.35, 522.15 Hz | 391.85, 465.30, 522.40 Hz | G4 / A#4 / C5 from AY periods at 1.75 MHz (392.0 for period 279) |
| | beat (onset autocorrelation, 24 s) | 983.6 ms = 48.03 frames | 983.0 ms = 48.00 frames | a whole number of 20.48 ms frames (+0.06 %) |
| | RMS L / R | 0.119 / 0.072 | 0.145 / 0.127 | panning differs (ours: shared ABC preset; MAME: A, C at 0.5, B at 0.25 to both) |
| `WAVPLAY.EXE` v2.02, `MISSION.WAV` (8-bit mono 22 050 Hz, 30.0 s) | control | `#9B` | `#9B` (port trace) | 21.875 kHz, the nearest rate |
| | played | 655 360 samples = 29.959 s, 5 120 INTs | the same envelope | 21 875 Hz exactly; the player drops the file's last partial 16 KB block |
| | envelope vs MAME | correlation 0.918, time scale 1.0006 (-0.06 %) | | |
| `WAVPLAY.EXE`, `ST16.WAV` (16-bit stereo 44.1 kHz, L 440 Hz, R 1000 Hz, made for the test) | control; L / R | `#FD`; 436.4 / 992.0 Hz | `#FD`; **992.0 / 436.7 Hz** | x 43 750 / 44 100: L 436.5, R 992.1. MAME swaps the channels in 16-bit stereo; the PLD does not |
| `WAVPLAY.EXE`, `ST8.WAV` (8-bit stereo 22.05 kHz), `M16.WAV` (16-bit mono 700 Hz) | control; tones | `#DB`: 436.4 / 992.0 Hz; `#BB`: 694.4 Hz | - | x 21 875 / 22 050 |
| `PROPLAY.EXE`, `*.MOD` | - | not playable | (MAME: through the NeoGS on the ZX-bus card) | "ProPlay - General Sound MOD player": needs GS on the ISA ZX-bus adapter (S6b) |

The AAC recording option works on the Sprinter: `POST /video/record {"format":"h264","audio":"aac"}` over 150
frames of `ST16.WAV` gives a 3.072 s MP4 (h264 + AAC 48 kHz stereo), whose audio is 436.4 / 992.0 Hz.

**The accelerator INT suspend.** WAVPLAY refills the ring in its INT handler with accelerator copies into page
`#FD`. With `AccelIntSuspend=1` (S5's default) each refill reaches the ring as one byte: 5 537 ring writes for
3 908 INTs, sound for 0.45 s, then silence. With 0 (MAME's behavior, and the PLD's `ACC_BLK` preset read
literally): 500 736 writes = 3 908 x 128 + the BIOS's 512. The default is 0 now (tdd §1.3).

No native test program in `TESTS\` exercises sound (ACCTEST, ANSITEST, CASHTEST, DATAPI, DETECTOR, DISKTEST,
ISACHK, KEYTEST, RAMTEST, SQRT, TIMER, TIMERV2, TESTKBD2 and the TRD images).

## 4. Speed and pacing

Questions from the owner's live test ("choppy, not synchronized", "too fast?"), answered with numbers:

- **Frame length.** BIOS 3.06 and 3.07 beta 1 write code `#2C` (port `#41BD`, value `#41`; PC `#12A7` /
  `#12AA`) once at start-up: 320 lines, 71 680 T, 48.83 Hz. MAME's trace of the same boot: the same write
  (PC `#12A7`). DSS 1.71 and Flex Navigator 1.15 write neither `#2C` nor `#2D` in 1 500 frames. The 312-line
  frame (`#61BD`, 50.08 Hz) follows the CMOS "V-Sync" setting (hardware-reference §6). `ScreenSprinter` sets
  `config.frame` and `config.frame_duration_us` at the next frame start (20 480 / 19 968 us), and the main loop
  reads `frame_duration_us` every frame, so the pacing and the FPS figure follow a switch; asserted in
  `ScreenSprinter_Test.FrameLength_312And320`.
- **CPU throughput.** `INC BC : JR -3` from RAM at 21 MHz: 11 947 passes per frame here, 11 946.7 on MAME (new
  `loop` mode of the capture tool), 36 clocks a pass both. `SprinterSoundTurbo_Test.CpuThroughput_MatchesMameAt21MHz`.
- **Rates at 21 MHz.** An AY tone (period 250: 437.5 Hz) and a Covox-Blaster square (15.625 kHz / 16 = 976.56 Hz)
  measured in the mixed output within 0.2 %, every period within 2 samples of the mean (no gap, no repeat), and
  320 play ticks per frame (`SprinterSoundTurbo_Test`).
- **"Choppy".** The host log of that session had ~400 "hard resync - dropped ~130 ms of overfilled audio": the
  automation's `run_frames` bursts (frames faster than real time fill the device ring), not playback. Real time:
  no resync in 25 s, and 410 692 play ticks = 18.8 s of WAV after ~5 s of boot. Together with the suspend above
  (WAV silent after 0.45 s) this was what was heard.
- **The Audio Player's meters** (WAVPLAY's GUI, `SOFTW.WAV`, 400 frames each, per-frame screenshots): level meters
  change in 1.5 frames per second here and 1.3 on MAME; the scope changes in 48.5 / 48.1 frames per second, by
  0.09 % / 0.08 % of its pixels per frame. The player draws this way.

## 5. TTD

Id 32 `SprinterCovoxBlaster`, v1, 545 bytes. The Sprinter fixture `testdata/machines/sprinter/ttd/boot.ttd` was
re-recorded (the TurboSound slot is the single AY now, not TSFM, and the blob joined): 301 checkpoints, validate
OK, `TTD_Corpus_Test` passes. The other fixtures and the CI gate (`TTD_Divergence_Corpus_Test`, `TTDBench_Test`)
are unchanged.

## 6. Tests

`core/tests/emulator/sound/sprinter/covoxblaster_test.cpp`: T-CBL unit tests (rate table, Covox, mono / stereo /
16-bit, INT and acknowledge, write index hold, `#FE` bits, CBL off, reserved rates, the playing entry, frame
rebase, a rendered tone), the machine wiring (codes `#88` / `#89`, the INT through the Sprinter INT source, the
page-`#FD` accelerator path, the mixer slot, the single AY, the TTD blob) and the 21 MHz suite (AY pitch,
CBL rate, CPU throughput). `ScreenSprinter_Test.FrameLength_312And320` asserts the frame durations.

## 7. Cost for the other machines

No per-instruction or per-access path of another machine changed: SoundManager tests one null pointer per
frame (the model DAC), the TurboSound chip-switch branch gained a flag only inside the rare `value > #0F` arm.
On the Sprinter the INT source asks the device only while its INT is enabled (one bit test before the time
division). A/B of `BM_HostFrame_*_Fast` (A = `c7c6e77f0`, B = this branch; rounds A B A B A B B A B A; machine
load 90-150, so +-2 % is noise):

| Benchmark | A min (us) | B min (us) | B / A |
|---|---|---|---|
| `BM_HostFrame_48K_Fast` | 1 213.1 | 1 205.0 | -0.7 % |
| `BM_HostFrame_Pentagon_Fast` | 1 617.4 | 1 635.8 | +1.1 % |
| `BM_HostFrame_Sprinter_Fast` (new) | 3 539.2 | 3 175.5 | -10.3 % (one AY instead of TSFM) |

## 8. Open points

1. **Stereo order.** The PLD's read address takes the low bit from `AUDIO_CH`, which also selects the AY's
   left / right word; even = left is the reading here and the INC's ("L, R" pairs). MAME swaps in 16-bit only.
   A real-board recording of `ST16.WAV` would settle it.
2. **Mix levels.** The PLD sums the AY (two 10-bit channels) and the CBL word (top 11 bits, signed) into one DAC
   word; here they are two mixer rows at their own scales. RMS differs from MAME's by up to 40 % per channel.
3. **MOD playback** needs the General Sound on the ISA ZX-bus adapter: S6b.
4. **The INT suspend** is off by default now; the PLD's `ACC_BLK` equation and WAVPLAY both say so, a real-board
   check would close it for good.
