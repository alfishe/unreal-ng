#pragma once

#include <cstdint>

/// Which clock gave a frame its target time
enum class FrameClockSource : uint8_t
{
    None = 0,  // not timed (no frame clock runs for this screen): readers use the latch time
    Audio,     // when the frame's last sample is heard (the audio device's clock)
    Emulated,  // the emulated clock: MainLoop's frame deadline, no audio device delivering
    Latch,     // the latch time: turbo or a run not paced to real time
};

const char* FrameClockSourceName(FrameClockSource source);

/// One frame's times, as the emulation thread knows them after the frame's sound was delivered
struct FrameTimingInput
{
    int64_t latchNs = 0;          // steady-clock time of the latch
    int64_t periodNs = 0;         // the frame's nominal period
    bool haveAudio = false;
    int64_t audioNs = 0;          // when the frame's last sample is heard
    bool haveEmulated = false;
    int64_t emulatedNs = 0;       // when the frame ends on the emulated clock
    int64_t emulatedDelayNs = 0;  // the video delay on the emulated clock
    int64_t latchDelayNs = 0;     // ... on the latch clock: a temporal effect's look-ahead only (no sound to wait for)
    int64_t offsetNs = 0;         // the lip-sync trim
    /// The longest delay the reader can wait (the frames the screen holds): a target at most this after the frame's
    /// end (the emulated clock's, else the latch). 0: no limit
    int64_t maxDelayNs = 0;
};

struct FrameTimingResult
{
    int64_t targetNs = 0;
    FrameClockSource source = FrameClockSource::None;
    bool clamped = false;  // the clock's time was later than maxDelayNs allows: the frame comes before its sound
};

/// The target time of every latched frame: the time the screen should show it (frame presenter design,
/// docs/inprogress/2026-10-26-frame-presenter, section 3).
///
/// The audio clock is the master: a frame's target is when its sound is heard. Without it (no device, another
/// instance has the sound, a held output) the emulated clock stands in, and in turbo the latch time.
/// Consecutive targets must stay smooth: a presenter shows each frame for target(n+1) - target(n). So a switch
/// between the audio and the emulated clock, or a step of the audio clock (an underrun moves the stream), does
/// not jump. The step is carried and paid back by kSlewNs per frame. A break in the frame sequence (a pause, a
/// seek, a stall), turbo, or a step too large to carry cuts to the new time at once: nothing smooth was shown
/// across it.
///
/// Threading: the emulation thread only (one per screen). No OS calls, no thread.
class FrameClock
{
public:
    /// A step of the target beyond the frame period larger than this is carried, not passed through
    static constexpr int64_t kJumpNs = 2'000'000;
    /// The carried step is paid back by at most this per frame (1 % of a 50 Hz period)
    static constexpr int64_t kSlewNs = 200'000;
    /// A step larger than this is cut to at once
    static constexpr int64_t kMaxCarryNs = 500'000'000;
    /// Latches further apart than this many periods break the sequence
    static constexpr int64_t kGapPeriods = 4;

    struct Stats
    {
        uint64_t frames = 0;
        uint64_t audioFrames = 0;
        uint64_t emulatedFrames = 0;
        uint64_t latchFrames = 0;
        uint64_t carried = 0;  // steps carried and slewed
        uint64_t cuts = 0;     // sequence breaks
        int64_t carryNs = 0;   // the carry now
        uint64_t clamped = 0;  // frames whose time was cut to maxDelayNs
        bool lastClamped = false;
        FrameClockSource source = FrameClockSource::None;
    };

    FrameTimingResult Next(const FrameTimingInput& in);
    void Reset();
    const Stats& GetStats() const { return _stats; }

private:
    bool _started = false;
    FrameClockSource _lastSource = FrameClockSource::None;
    int64_t _lastRawNs = 0;
    int64_t _lastTargetNs = 0;
    int64_t _lastLatchNs = 0;
    int64_t _carryNs = 0;
    Stats _stats;
};
