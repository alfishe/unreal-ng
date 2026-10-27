#ifndef FRAMESOURCE_H
#define FRAMESOURCE_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "emulator/video/screen.h"

/// The frame a refresh reaching the screen at `hostNs` (steady clock, ns) shows, copied into `dst` (`dstSize`
/// bytes): Screen::CopyFrameAt (frame presenter design, docs/inprogress/2026-10-26-frame-presenter, section 4.2).
/// `info` gets the frame's serial and times; serial 0 = a source without serials (every copy is a new frame).
/// @return false when there is no frame
using FrameSourceFn = std::function<bool(int64_t hostNs, uint8_t* dst, size_t dstSize, PresentFrameInfo* info)>;

/// The steady clock's now in ns: the clock frame targets are on
inline int64_t SteadyNowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

#endif  // FRAMESOURCE_H
