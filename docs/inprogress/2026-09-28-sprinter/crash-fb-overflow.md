# Crash: heap overwritten with black pixels while recording a Sprinter (2026-10-02)

## Symptom

unreal-qt on macOS crashed with `EXC_BAD_ACCESS` at addresses such as `0xff000000ff00b078`.
The crash happened in `SoundManager::handleFrameStart()` / `MainLoop::OnFrameStart()`. A Sprinter
was running Flex Navigator, and the owner had just started a video recording. `0xFF000000` is an
opaque-black pixel (RGBA `0xAABBGGRR`). Something had written picture pixels over heap objects:
the screen object's vtable pointer and `EmulatorContext` pointers.

## Root cause

Two bugs in the recording path. Neither is in the Sprinter video code.

1. **The video timestamps stepped backwards** (`RecordingManager::CaptureFrame`). The timestamp
   was `frame count x current frame duration`. The Sprinter switches its frame length with the
   codes `#2C` / `#2D`: 320 lines (20.48 ms) or 312 lines (19.968 ms). Example: frame 100 at
   320 lines is at 2.048 s. If the machine then switches to 312 lines, frame 101 is stamped
   101 x 19.968 ms = 2.0168 s, which is earlier than frame 100. AVAssetWriter fails the whole
   file on a timestamp that does not grow (`AVFoundationErrorDomain -11800`, underlying
   `-16364`).
2. **A failed writer led to a heap overflow** (`VideoToolboxEncoder::OnVideoFrame`). A failed
   AVAssetWriter still reports its input ready, but it has no pixel buffer pool any more. The
   encoder then created a `CVPixelBuffer` of the **source** size (736x576) and wrote the
   **scaled** frame into it (1472x1152 at the GUI's default 2x). That writes 4x the buffer's
   size, so every frame overwrote the heap with picture pixels, mostly black.

The overflow (2) is not tied to the Sprinter. Any recording on macOS at scale 2 or more whose
writer fails for any reason overflowed the same way. Only the trigger (1) is the Sprinter's,
because no other machine changes `config.frame` while it runs. A model switch or an SZX load
during a recording could do the same.

## Fix

- `RecordingManager`: the video timestamp is the sum of each captured frame's own duration
  (`_emulatedVideoTime`), so it only grows.
- `VideoToolboxEncoder`:
  - a frame is dropped when the writer is not in the writing state;
  - the fallback pixel buffer has the output size (`_width x _height`);
  - a pixel buffer smaller than the scaled frame is refused;
  - a frame whose timestamp does not grow is dropped, so the recording goes on.
- `GIFEncoder`: a framebuffer smaller than the encoder's `_width x _height` is skipped. A video
  mode switch while recording can shrink the framebuffer. The encoder only read past the end
  here, never wrote.

## What was checked, ASan + UBSan, full rendering, no turbo

- Sprinter BIOS boot to Flex Navigator from the DSS floppy, with the head workaround from
  `Dss162_FlexNavigatorDrawsWithTheAccelerator`. 4000 rendered frames with F4 taps, with a
  recording (H.264 + AAC, 2x) started at frame 1500. Clean: the renderer, the accelerator, the
  screenshots and the stretch of recorded lines stay in bounds.
- The same BIOS run with `#2C` / `#2D` and HOLD changes every 5 frames while recording: before
  the fix, ASan reports `memcpy-param-overlap` in `videotoolbox_encoder.mm` (the 4x write).
  After the fix it is clean and the file is valid (100 frames 1472x1152).
- unreal-qt built with ASan: a Sprinter booting DSS 1.71 from `sp_hdd_sys.img` on BIOS
  3.06-hf2, F4 taps, Flex Navigator, a WebAPI recording at 2x (602 frames, valid file). Clean.
  The crash that happened without a recording (F4 taps) was not reproduced.
- Other geometries: TS-Conf (720x288, lines doubled), Profi and ATM have a fixed frame length,
  so the timestamp trigger cannot happen there. The encoder overflow was shared by every machine
  and is fixed for all of them.

## Regression tests

- `core/tests/emulator/recording/videotoolbox_encoder_test.cpp`: a frame at an earlier
  timestamp and one at the same timestamp, at 736x576 and 2x. Before the fix: ASan
  `memcpy-param-overlap`, and the 5 frames after it are lost. After: those 2 frames are
  dropped, 10 frames are encoded, and the file is finished.
- `core/tests/emulator/recording/recordingmanager_test.cpp`: the timestamps are the sum of
  the frames across 320 / 312-line switches, both from `config.frame` directly and on a
  running Sprinter driven by `#2C` / `#2D`. Both fail before the fix.

Side note, out of scope: under ASan, `TsConfAspect_Test.EveryTsConfMode...` reports a
heap-use-after-free in `~PortDecoder_TSConf` (`TsConfMemory::SetCacheActive`) at emulator
teardown.

## Stability verification

Run on 2026-10-02 at commit `28f0c306e` plus the recording-audio changes on this branch, in the
GUI (`unreal-qt`, Release, macOS arm64), driven through the WebAPI on a private port. Recordings
through `POST /video/record`, checked with `ffprobe` / `ffmpeg astats`. The recordings stay in
`scratch/verify/` (not in the repository).

**Sprinter run.** BIOS `sp2k-3.07-beta1.rom` (`[ROM] SPRINTER=` in the app bundle's
`configs/sprinter/unreal.ini`), `sp_hdd_sys.img` (DSS 1.71) on `ide0.master` with
`access: session`, reset, F4 taps until Flex Navigator. Then 190 s of emulated time recorded at
2x, full frame, while keys (Tab, arrows, Enter: opening `C:\DEV`, `C:\BIN`, `C:\DEMOS` and back)
and mouse moves went in every 0.7 s. Flex Navigator makes no sound and the Sprinter's own DAC
(Covox-Blaster) is not emulated yet, so a short routine injected through the memory and register
API switched an AY tone (441 Hz, channel A) on and off every 20 s of emulated time, to have a
sound track worth measuring. A frame length switch (codes `#2C` / `#2D`) could not be triggered
from the automation; it is covered by `RecordingManager_Test.SprinterFrameCodes_*`.

**Other machines.** One minute each: Pentagon (128K BASIC: `BORDER 2: BEEP 1,12: BORDER 0:
PAUSE 40`, a sync marker), TS-Conf, Profi and ATM3 (boot screen, key taps, the same AY tone
switched every 10 s).

| Machine | Format | Size | fps | Frames | Audio | RMS dB | Audio - video | Crash |
|:--|:--|:--|:--|:--|:--|:--|:--|:--|
| Sprinter (FN, 190 s) | H.264 + AAC, mp4 | 1472x1152 | 48.83 | 9297 (190.40 s) | 48 kHz, 2 ch | -21.9 | +0.9 ms | no |
| Sprinter (FN, 60 s) | H.264, video only | 1472x1152 | 48.83 | 2959 (60.60 s) | none | - | - | no |
| Sprinter (FN, 30 s) | GIF | 736x576 | 50 (*) | 1497 | none | - | - | no |
| Pentagon (70 s) | H.264 + AAC | 704x576 | 48.83 | 3466 (70.98 s) | 48 kHz, 2 ch | -15.8 | +0.4 ms | no |
| TS-Conf (65 s) | H.264 + AAC | 1440x1152 | 48.83 | 3190 (65.33 s) | 48 kHz, 2 ch | -17.9 | +1.2 ms | no |
| Profi (65 s) | H.264 + AAC | 1216x576 | 50.08 | 3272 (65.34 s) | 48 kHz, 2 ch | -20.2 | +0.3 ms | no |
| ATM3 (65 s) | H.264 + AAC | 704x576 | 50.08 | 3269 (65.28 s) | 48 kHz, 2 ch | -21.3 | +0.4 ms | no |
| Sprinter (FN, 70 s), ASan + UBSan GUI | H.264 + AAC | 1472x1152 | 48.83 | 3420 (70.04 s) | 48 kHz, 2 ch | -22.4 | -1.8 ms | no |

- Sizes are the framebuffer x 2: Sprinter 736x288 and TS-Conf 720x288 are stored at half height,
  so their lines are doubled (1472x1152, 1440x1152); Pentagon / ATM3 352x288, Profi 608x288.
- Frames = emulated time / frame length in every file (20.48 ms for Pentagon, Sprinter, TS-Conf;
  19.968 ms for Profi and ATM3). Presentation times are unique and grow; the steps read 20.0 /
  21.7 ms because AVAssetWriter keeps the video track in 1/600 s units (mean 20.48 ms).
- The AAC track is converted from the core's 44.1 kHz to 48 kHz by AVAssetWriter.
- "Audio - video" is the difference of the two stream durations. On the Pentagon the BEEP onsets
  sit 3-5 ms after the red border frames over all 31 beeps (the interpreter's time between
  `BORDER` and the first speaker edge); no drift over the recording.
- (*) A GIF stores delays in 1/100 s: 20.48 ms frames are written as 20 ms, so a GIF plays 2%
  fast. GIF has no audio track; `gif` + `audio` is refused on every surface.
- ASan + UBSan: a separate RelWithDebInfo build (`-fsanitize=address,undefined`), the same
  Sprinter session for 70 s of emulated time (about 5 frames per second of wall time; a Debug ASan
  build managed 0.05 and was dropped). ASan reported nothing. UBSan reported one signed overflow
  outside the recording path: `opcodes-callback.cpp:64` (`Z80M1AtT`, `int` T-state counter at
  `INT_MAX + 1`) on the NeoGS card's Z80 (`GSCardRunner::runTo` from `SoundChip_NeoGS::handleFrameEnd`).
- No `unreal-qt` crash report appeared in `~/Library/Logs/DiagnosticReports/` during the runs
  (the newest stays `unreal-qt-2026-10-02-041615.ips`, from before them).

### Found and fixed during the verification

1. **The sound track was 44 ms late.** AVAssetWriter's AAC input was marked real-time
   (`expectsMediaDataInRealTime = YES`). A real-time input keeps the AAC encoder delay (2112
   priming frames at 48 kHz) as sound instead of trimming it with an edit list, so every
   recording on macOS played its sound 44 ms (over two frames) after the picture: measured
   47-49 ms on the Pentagon BEEP / BORDER marker, 44.0 ms on a synthetic file. The audio input is
   no longer real-time: the edit list skips the priming (`afinfo`: "192000 valid frames + 2112
   priming"), and the offset is 0 on the synthetic file and 3-5 ms (the BASIC interpreter) on
   the Pentagon. Test: `VideoToolboxEncoder_Test.AacPrimingIsTrimmed_SoundStartsWithThePicture`
   reads the sound track's edit list (media time 0 before, 2112 after).
2. **A dropped audio chunk shortened the sound track.** When the writer did not accept an audio
   chunk at once it was dropped, and the next chunk was stamped from the count of samples
   written, so the rest of the track moved earlier. The audio input now waits briefly (at most
   20 ms, like the 100 ms video wait), and each chunk is stamped with its own emulated time, so a
   chunk that is still dropped leaves a gap instead of a shift. Test:
   `RecordingManager_Test.AacAudioTrack_SamplesFollowTheFramesAndVideoOnlyStaysDefault` (25
   frames of a running Pentagon, the sound track within one frame of the picture; before: 3-7
   frames short in a fast run).

Side notes, out of scope: `POST /basic/inject` stores numbers without their hidden 5-byte form,
so `BORDER 2` stops with "C Nonsense in BASIC" (typing the line through `basic/run` works); the
Python bindings test `py::isinstance<std::string>` in four more places (always false, so a
string argument there is ignored); the CLI `videorecord` does not enable the `recording` feature
by itself as the WebAPI, Lua and Python do; the mouse tests in
`tools/verification/webapi/src/test_api_interpreter.py` fail on this branch.
