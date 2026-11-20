# Analysis

Numbers from [measurements-2026-11-19-encoders.md](measurements-2026-11-19-encoders.md). "x" = multiple of real
time (1.0 keeps up with a running machine at 50 fps). Both hosts were loaded by other sessions throughout.

## 1. Stage 1: which intermediate keeps up

`libx264rgb -qp 0 -preset ultrafast` (bgr0, lossless) is the best stage-1 codec on both hosts: the fastest or
close to it, and by far the smallest.

| Stream | Mac x264rgb qp0 | Linux x264rgb qp0 | Linux utvideo | x264rgb MB/min | utvideo MB/min |
|---|---:|---:|---:|---:|---:|
| demo-native | 30.9 | 4.3 | 5.3 | 487 | 1226 |
| demo-fhd | 12.2 | 2.9 | 3.0 | 1563 | 5618 |
| demo-kbd | 11.8 | 1.8 | 2.2 | 1542 | 4891 |
| demo-uhd | 0.9 | 0.4 | 0.5 | 6443 | 21647 |
| game-fhd | 24.6 | 7.2 | 5.9 | 24 | 1150 |

- **x264rgb vs the others.** utvideo / magicyuv / ffvhuff run about as fast but write 3-6x more (intra only).
  x264rgb's P frames keep a mostly still game at 8-27 MB/min, against 300-2200 for the intra codecs.
- **ffv1, the expected favourite, is the slowest** at the CRT sizes: 0.8-0.9x at 1080 on Linux, 1.5x on the Mac.
  CRT scanlines and masks are high-entropy for its context model.
- **rawvideo** does not help. The disk becomes the limit: 17-67 GB/min, 0.7x at kbd on the Mac.
- **4K fails for every codec on every host** (0.4-1.5x at best, 6-67 GB/min). At 4K only VideoToolbox (live, Mac)
  is an option, and it too sat at 0.7-0.9x under this load.

### Compared with today's live encode on the same host

| Stream, Linux | live x264 | live x265 | stage 1 x264rgb | stage-1 gain over x265 |
|---|---:|---:|---:|---:|
| demo-native | 2.8 | 1.1 | 4.3 | 3.9x |
| demo-fhd | 1.2 | 0.5 | 2.9 | 5.8x |
| demo-kbd | 0.9 | 0.3 | 1.8 | 6x |
| game-fhd | 4.5 | 2.3 | 7.2 | 3.1x |

The cases the owner named (x265 at 1080, hosts without VideoToolbox) are exactly the ones below 1x live. Stage 1
lifts them above 1x with headroom: 1.8-2.9x on a loaded 8-core laptop CPU. x264 4:4:4 live is borderline there
(0.9-1.2x); on the Mac every live encoder kept up at ≤ 1080 (x265 3.0-7.3x) at load ≤ 45.

### The size of stage 1 is not free to choose

The charter assumed stage 1 records "at the picture's own size" and stage 2 scales. With CRT on this is wrong.
The window draws the CRT for the height asked (`renderPicture(pictureHeight)`, 72aced513): the mask and
scanlines are drawn at the output's pixels. A 662x542 capture scaled up later blurs them into grey. Stage 1 must
therefore capture at the output size; only the cheap pad / letterbox (`-vf pad`) can move to stage 2. With CRT
off, the picture could be captured small and scaled in stage 2, but ZX pixels at 662x542 already cost little live
(23-37x on the Mac, 2.8-15x on Linux).

## 2. Stage 2: the background transcode

Stage 2 here read the `ffv1` intermediate. That costs about 2x against encoding from raw frames (Mac demo-fhd
x264: s2 3.3x vs live 7.0x): ffv1 decodes slowly. An x264rgb intermediate decodes faster. Expect stage 2 between
the two columns.

| Codec, demo-fhd | Mac, x real time | Mac, time per minute recorded | Linux, x real time | Linux, time per minute recorded |
|---|---:|---:|---:|---:|
| x264 ultrafast | 3.3 | 18 s | 0.7 | 1.4 min |
| x264 medium | 2.4 | 25 s | 0.5 | 2 min |
| x265 ultrafast | 2.2 | 27 s | 0.4 | 2.5 min |
| x265 medium | 0.8 | 1.3 min | 0.1 | 10 min |
| VT H.264 / HEVC | 3.5 / 3.3 | 17-18 s | – | – |

- **The Mac is faster than the charter expected.** It expected x265 ultrafast 1080p at 5-10x the length; the
  measured cost was 0.45x. The 2026-11-17 figure (7.6 fps) was taken at load ~40 with full 1920-wide frames.
- **Linux matches the charter.** x265 there costs 2.5x the length (ultrafast) and 10x (medium).
- **A mostly still game costs far less.** Only unique pictures are encoded (game-fhd x265 ultrafast 6.5x real time
  on the Mac, 1.7x on Linux).
- **Stage 2 makes slow presets usable.** x265 `medium` cannot run live anywhere (0.1-0.8x), but it can as a
  background job. These runs did not measure its quality-per-byte gain over ultrafast at CRF 14.

## 3. The warning

- **On a steady host the probe works for the encoder that needs it.** On Linux (load 17-35, steady) the 25-frame
  probe with ffmpeg's start-up subtracted landed within ±25 % in:
  - 8/10, 8/10 and 10/10 runs for x265, the encoder that needs the warning;
  - 10/10 for x264 on the game.

  It under-estimated x264 on the demo by about 30 % (2/10). x264's thread pool and lookahead are not warm after
  25 frames. That error is on the safe side: it warns too early, never too late.
- **The synthetic picture predicts about as well as real frames** (`synth_net`: 8/10 for x265 on the demo). A
  probe can therefore run before the first real frame exists.
- **Probe cost.** The probe takes 0.7-0.8 s for x264 and 1.6-1.9 s for x265 at 1080 (ffmpeg's start-up 0.2-0.5 s
  is part of it). The ≤ 1 s budget holds only for x264, or for x265 with fewer frames (12 frames ≈ 1 s).
- **On the loaded Mac no probe can work.** The steady rate itself swung 5-10x between back-to-back runs (x264
  demo-fhd 26-254 fps) as other sessions' load came and went (load 72-165). The best probe reached 8/10 once and
  1-6/10 otherwise. A number given before the start is a snapshot of the host at that moment. On a busy host only
  the running recording can tell, and the recorder already measures it: `frames_pending`, the writer behind real
  time.
- **The raw probe (start-up included) is useless** at 0-4/10, always pessimistic by 20-70 %. ffmpeg's start-up has
  to be measured apart and subtracted.

## 4. Encoder pacing

**The stepped rate is not the encoder's rate.** At low load (17-32), h264 through ffmpeg:

| Size | stepped fps | the same encoder on the captured stream | ratio |
|---|---:|---:|---:|
| native | 152 | 1121 | 7x |
| fhd | 99 | 342 | 3.5x |
| kbd | 21 | 331 | 16x |

The encoders at one size, stepped at the same time, land close together (fhd: h264 17, h265 20, VT 20-32 fps at
load 37-69). The limit is the frame path, not the writer. That path is:
- the direct `RunFrame`;
- the GUI thread drawing the window (offscreen: the software CRT compose at the output size);
- the grab;
- the keyboard picture.

The kbd case (21 fps for the demo, 75 for the game) points at per-frame work that scales with the changed picture
and the keyboard. This POC did not profile it. The GPU window path was not measured (offscreen has no GL).

The criterion (stepped within 20 % of the encoder) is not met. The warning's number therefore cannot double as
the paced emulation speed. For a software encoder at 1080 and above, the encoder may become the limit again (x265
on Linux: 24 fps against a frame path that is faster).

### Sound at a paced speed

Read from the code, not measured:
- **The file's sound.** It comes from the per-frame audio tap (`addFrameAudioTap`, `WindowRecorder` delays it with
  the picture). It is complete and in sync at any emulation speed: each emulated frame brings its own samples.
  The stepped recordings already prove this.
- **The speakers.** They are fed by the emulation thread, which audio paces at 1x. A machine held below 1x leaves
  the host ring short, and `AppSoundManager` zeroes the unfilled part. The result is choppy sound, a burst every
  callback.
- **What a paced live mode would need.** It would have to mute or detach the speakers, or play the sound slowed
  down (pitch drops). There is no speed below 1x today (`speed_multiplier` ≥ 1).
- **The threading cost.** Pacing a running machine means the emulation thread waits on the writer while the GUI
  thread draws and grabs the frame it waits for. That is the shape of the deadlock fixed in 9624e5291. The stepped
  mode avoids it because its loop runs on the WebAPI thread, outside both.

## 5. Variants against the cases

| Case | a-baseline (live) | b-two-stage | c-encoder-paced |
|---|---|---|---|
| Mac, VT or ProRes, ≤ 1080 | keeps up (1.7-14x; the 1.7 was one loaded run) | not needed | not needed |
| Mac, x265 software, ≤ 1080 | keeps up at load ≤ 45 (3-22x), may fall behind under heavy load | covers heavy load (12x) | – |
| Linux / Windows / Deck, x264 at 1080 or kbd | borderline (0.9-1.2x) | 1.8-2.9x | runs slower than real time |
| Linux / Windows / Deck, x265 at 1080 or kbd | falls behind (0.3-0.5x) | 1.8-2.9x, then 2.5x the length in stage 2 | 0.3-0.5x speed |
| 4K, any software path | falls behind | falls behind (stage 1 0.4-0.9x) | the only way that keeps every frame |
| Exact, every frame, offline | – | – | the stepped mode exists (`record/frames`) |
