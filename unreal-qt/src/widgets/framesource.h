#ifndef FRAMESOURCE_H
#define FRAMESOURCE_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "emulator/video/presentpolicy.h"
#include "emulator/video/screen.h"
#include "emulator/video/smoothmotion.h"
#include "emulator/video/windowview.h"

/// The frame a refresh reaching the screen at `hostNs` (steady clock, ns) shows, copied into `dst` (`dstSize`
/// bytes): Screen::CopyFrameAt (frame presenter design, docs/inprogress/2026-10-26-frame-presenter, section 4.2).
/// `info` gets the frame's serial and times; serial 0 = a source without serials (every copy is a new frame).
/// @return false when there is no frame
using FrameSourceFn = std::function<bool(int64_t hostNs, uint8_t* dst, size_t dstSize, PresentFrameInfo* info)>;

/// The oldest timed frame after `afterSerial`, each once and in order, read ahead of the screen (Screen::CopyNextFrame
/// without marking it shown): smooth motion keeps the frames to come. @return false when there is none yet
using NextFrameSourceFn =
    std::function<bool(uint64_t afterSerial, uint8_t* dst, size_t dstSize, PresentFrameInfo* info)>;

/// A screen's frames for the screen widget
struct ScreenFrames
{
    FrameSourceFn at;
    NextFrameSourceFn next;  ///< empty: no serials or no times (a ZX-Poly display): no smooth motion
    smoothmotion::Mode smooth = smoothmotion::Mode::Off;  ///< [VIDEO] SmoothMotion
    bool onCpu = false;  ///< [VIDEO] EffectsDevice = cpu: the GPU screen runs smooth motion on the CPU reference
    PresentPolicy policy = PresentPolicy::Auto;  ///< [VIDEO] PresentPolicy: the native presenter's
};

/// The steady clock's now in ns: the clock frame targets are on
inline int64_t SteadyNowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// Smooth motion's part of the window's present stats (the automation's `present.smooth`)
inline void FillSmoothStats(const smoothmotion::SmoothMotion& smooth, bool active, bool onGpu, WindowPresentStats& out)
{
    const smoothmotion::SmoothMotion::Stats& s = smooth.GetStats();
    out.smooth = smoothmotion::ModeName(active ? smooth.GetMode() : smoothmotion::Mode::Off);
    out.smoothDevice = active ? (onGpu ? "gpu" : "cpu") : "";
    out.game = active && smooth.HasGame();
    out.frames = s.frames;
    out.pictures = s.pictures;
    out.renders = s.renders;
    out.blended = s.blended;
    out.interpolated = s.interpolated;
    out.motionFields = s.estimates;
    out.onDevice = s.onDevice;
    out.heldFlicker = s.heldFlicker;
}

#endif  // FRAMESOURCE_H
