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
