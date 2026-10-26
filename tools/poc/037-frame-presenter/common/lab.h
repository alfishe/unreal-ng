#pragma once

// POC 037: the display-side lab. A synthetic producer that paces like MainLoop (absolute deadline at the source rate)
// or follows the display (one frame per tick), a latch ring whose slots carry the frame number and the latch time, and
// the per-present log the analysis reads. No emulator core: the question is what reaches the screen.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace poc037
{

/// Monotonic time in ns, on the base the platform's presentation timestamps use (macOS: mach host time, the base of
/// CACurrentMediaTime and MTLDrawable.presentedTime; Windows: QPC)
int64_t NowNs();
/// Sleep to an absolute time: coarse sleep, then a short spin (the way TimeHelper::WaitUntilPrecise ends)
void SleepUntilNs(int64_t t);

constexpr int kWidth = 384;
constexpr int kHeight = 288;

struct Options
{
    std::string variant = "b";      ///< a-signal, b-vsync, c-vrr, d-locked (the first letter is enough)
    std::string backend = "native"; ///< gl (Qt QOpenGLWindow), gpu (SDL_GPU), native (Metal / DXGI on the shell's window)
    std::string vrr = "at";         ///< c-vrr: at (present at a time), min (present after a minimum duration), link (display link at the rate)
    std::string link = "metal";  ///< macOS refresh callback: metal (CAMetalDisplayLink) or ca (CADisplayLink + nextDrawable)
    std::string tag;                ///< free text for the run's file name
    std::string out = ".";          ///< the folder for the logs
    double rate = 50.080128;        ///< the source rate, Hz
    double seconds = 60;
    double delayMs = 0;             ///< c-vrr: the present target is latch + delay (plus one frame to absorb the jitter)
    double workMs = 1.0;            ///< the producer's mean "emulation" time per frame (a random 0.5x..1.5x of it)
    double frameLatency = 1;       ///< CAMetalDisplayLink.preferredFrameLatency (refreshes between the callback and the target)
    int scale = 3;                  ///< window size in source pixels
    bool fullscreen = false;
};
Options ParseOptions(int argc, char** argv);
/// A rate from a preset name: pentagon, zx48, next60-vga0..2, or a number
double RateOf(const std::string& name);

struct Frame
{
    uint64_t number = 0;
    int64_t latchNs = 0;
    std::vector<uint32_t> pixels;  ///< kWidth * kHeight, BGRA in memory (0xAARRGGBB little endian)
};

/// The latch ring: the producer writes slot number % N; a reader copies the newest, or the newest latched by a time
class FrameRing
{
public:
    static constexpr int kSlots = 8;
    FrameRing();
    /// Write the next frame (the producer's latch)
    void Latch(uint64_t number, int64_t latchNs, const std::vector<uint32_t>& pixels);
    /// Copy the newest frame latched at or before t (t = 0: the newest); false when none
    bool CopyAt(int64_t t, Frame& out);
    /// The newest frame number and its latch time (no copy); false when none
    bool Newest(uint64_t& number, int64_t& latchNs);
    /// Copy a frame by number; false when it is no longer (or not yet) in the ring
    bool CopyNumber(uint64_t number, Frame& out);
    /// Block until a frame newer than `after` is latched, or until `deadlineNs`; true when one is there
    bool WaitNewer(uint64_t after, int64_t deadlineNs);

private:
    std::mutex _mutex;
    std::condition_variable _cv;
    Frame _slots[kSlots];
    uint64_t _count = 0;  ///< frames latched so far; the newest is _count - 1
};

/// The synthetic emulator: a scroller that moves 4 px per frame and the frame number as a block code
class Producer
{
public:
    Producer(FrameRing& ring, double rateHz, double workMs);
    ~Producer();
    /// Clocked mode: the producer keeps its own absolute deadline (MainLoop)
    void StartClocked();
    /// Ticked mode (d-locked): the producer emulates one frame per Tick()
    void StartTicked();
    void Tick();
    void Stop();
    /// A callback after every latch (the frame-ready signal of a-signal)
    void OnLatch(std::function<void(uint64_t)> fn) { _onLatch = std::move(fn); }
    uint64_t Frames() const { return _frames.load(); }
    /// The latch interval's deviation from the period, p99 over the run, ms (after the first 2 s)
    double LatchJitterP99Ms() const;

private:
    void Run(bool ticked);
    void Render(uint64_t n);

    FrameRing& _ring;
    double _rate;
    double _workMs;
    std::thread _thread;
    std::atomic<bool> _stop{false};
    std::atomic<uint64_t> _frames{0};
    std::mutex _tickMutex;
    std::condition_variable _tickCv;
    uint64_t _ticks = 0;
    std::vector<uint32_t> _pixels;
    std::vector<int64_t> _latches;
    std::function<void(uint64_t)> _onLatch;
};

/// One present: which frame went into which refresh, and when it reached the screen
struct PresentRecord
{
    uint64_t frame = 0;
    int64_t latchNs = 0;
    int64_t tickNs = 0;        ///< the refresh callback's time (display link, acquire return), 0 if none
    int64_t targetNs = 0;      ///< the time the present asked for (display link target, present-at time), 0 if none
    int64_t submitNs = 0;      ///< after the present call returned
    int64_t presentedNs = -1;  ///< the time on screen from the system (-1: no feedback; 0: the system dropped it)
    int64_t cpuNs = 0;         ///< CPU time of the present on its thread
};

class PresentLog
{
public:
    explicit PresentLog(size_t capacity = 20000);
    /// Add a record; returns its id for SetPresented
    size_t Add(const PresentRecord& r);
    void SetPresented(size_t id, int64_t presentedNs);
    /// After the present call: its return time and the CPU time it took
    void SetTiming(size_t id, int64_t submitNs, int64_t cpuNs);
    /// Meta lines (key=value) written as comments at the head of the CSV
    void Meta(const std::string& key, const std::string& value);
    void Write(const std::string& path);

private:
    std::mutex _mutex;
    std::vector<PresentRecord> _records;
    std::vector<std::pair<std::string, std::string>> _meta;
};

/// realtime: the emulation thread's priority as MainLoop sets it (ThreadHelper::setRealtimePriority); else the
/// presenter's (user-interactive QoS on macOS)
void RaiseThreadPriority(bool realtime);
/// Per-thread CPU time in ns
int64_t ThreadCpuNs();
/// The log file name for a run
std::string RunPath(const Options& o, const std::string& shell);

}  // namespace poc037
