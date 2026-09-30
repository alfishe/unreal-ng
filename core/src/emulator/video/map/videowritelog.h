#pragma once

/// @file videowritelog.h
/// @brief What the video state was at any T of the current or the previous
/// frame (PLAN #42 phase 3, video-debug-translation design §4.6).
///
/// Video mode, screen page and border change at the port write, anywhere in a
/// frame. Screen notes the latches after each such write (InitRaster,
/// SetActiveScreen, SetBorderColor) together with the frame T; the state at T
/// is the last entry at or before T, or the frame's start. The log holds the
/// latches themselves, not the port writes, so no port decode is repeated here.
///
/// Cost: cold code - it runs only where a port handler already changed video
/// state, and appends only when the latches differ from the last entry.
///
/// Threading: the emulator thread writes. At each frame start the finished
/// frame is published under a mutex (one lock per frame); another thread reads
/// that copy (Published) while the machine runs. A paused machine is read directly.
///
/// Worked example (ATM Turbo 2+): 16-colour mode from the frame start, OUT to
/// #FF77 at T = 150 * 224 switches to hi-res. StateAt(100 * 224) = 16-colour,
/// StateAt(200 * 224) = hi-res.

#include <cstdint>
#include <mutex>
#include <vector>

namespace videomap
{
/// The latches a video mode's geometry and memory layout depend on (the value
/// part of VideoState; palettes are read live)
struct VideoLatches
{
    uint8_t mode = 0;           ///< VideoModeEnum the renderer runs
    uint8_t p7FFD = 0;
    uint8_t pEFF7 = 0;
    uint8_t pFF77 = 0;
    uint8_t pDFFD = 0;
    uint8_t aFE = 0;
    uint8_t pFE = 0;
    uint8_t borderAttr = 0;     ///< EmulatorState::border_attr
    uint8_t borderIndex = 0;    ///< Screen::GetBorderColor
    uint8_t atmBorderBright = 0;
    uint8_t activeScreen = 0;   ///< Screen: 0 normal (page 5), 1 shadow (page 7)
    uint8_t reserved = 0;

    bool operator==(const VideoLatches& o) const
    {
        return mode == o.mode && p7FFD == o.p7FFD && pEFF7 == o.pEFF7 && pFF77 == o.pFF77 && pDFFD == o.pDFFD &&
               aFE == o.aFE && pFE == o.pFE && borderAttr == o.borderAttr && borderIndex == o.borderIndex &&
               atmBorderBright == o.atmBorderBright && activeScreen == o.activeScreen;
    }
    bool operator!=(const VideoLatches& o) const { return !(*this == o); }
};

struct VideoWrite
{
    uint32_t t = 0;  ///< frame T of the write (base clock)
    VideoLatches latches;
};

/// One frame's history
struct VideoFrameLog
{
    uint64_t frame = 0;          ///< EmulatorState::frame_counter of this frame
    bool valid = false;
    VideoLatches start;          ///< latches when the frame began
    std::vector<VideoWrite> writes;
    bool partial = false;        ///< the log filled up: later writes are missing

    /// Latches in force at frame T (the last write at or before T, else the start)
    VideoLatches StateAt(uint32_t t) const;
};

class VideoWriteLog
{
public:
    static constexpr size_t kCapacity = 4096;  ///< writes per frame (border effects can make thousands)

    /// Emulator thread, frame start: the running frame becomes the previous one (published), a new one begins
    void BeginFrame(uint64_t frame, const VideoLatches& start);
    /// Emulator thread: the latches after a video port write at frame T
    void Record(uint32_t t, const VideoLatches& now);
    /// Emulator thread: would Record log these latches (they differ from the last entry)?
    /// Lets a caller skip computing T for the common unchanged case (OUT #FE with the same border)
    bool Changes(const VideoLatches& now) const
    {
        return _current.valid && now != (_current.writes.empty() ? _current.start : _current.writes.back().latches);
    }

    /// Emulator thread or a paused machine
    const VideoFrameLog& Current() const { return _current; }
    const VideoFrameLog& Previous() const { return _previous; }

    /// Any thread: a copy of the last completed frame
    VideoFrameLog Published() const;

private:
    VideoFrameLog _current;
    VideoFrameLog _previous;

    mutable std::mutex _publishMutex;
    VideoFrameLog _published;
};
} // namespace videomap
