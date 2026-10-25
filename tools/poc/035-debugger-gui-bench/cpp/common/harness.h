#pragma once

// Measurement harness shared by every framework variant.
//
// A run goes through fixed phases, in this order:
//   warmup      2 s at 50 Hz          - not recorded (caches, glyph atlases, JIT of llvmpipe shaders)
//   active50   15 s at 50 Hz          - the debugger following a running machine: frame work, CPU %, RSS
//   unthrottled 8 s, no pacing        - headroom: how many full frames per second the framework can produce
//   idle        8 s, machine paused   - nothing changes; the framework sits in its idle path: CPU % at rest
//
// The variant drives its own loop and calls:
//   TickWorkload()            - advances the simulated machine (its cost is measured and subtracted)
//   FrameBegin() / FrameEnd() - around the framework's own work for one frame (build UI, layout, render, present)
//   MarkRenderStart()         - optional split point between "build/layout" and "render/present"
// and asks Phase() / NextDeadline() to know how to pace.

#include "workload.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bench
{

enum class Phase
{
    Warmup,
    Active50,
    Unthrottled,
    Idle,
    Switch,   // only with --switch=font|scale: toggles the setting every 250 ms at 50 Hz
    Done
};

const char* PhaseName(Phase p);

struct PhaseStats
{
    Phase phase = Phase::Warmup;
    uint64_t frames = 0;
    double wallSec = 0;
    double mainThreadCpuSec = 0;   // main thread CPU minus workload ticks
    double processCpuSec = 0;      // all threads (includes llvmpipe / Qt render threads) minus workload ticks
    double tickCpuSec = 0;         // the subtracted workload cost, for reference
    std::vector<double> frameMs;   // FrameBegin -> FrameEnd
    std::vector<double> buildMs;   // FrameBegin -> MarkRenderStart (when the variant marks it)
    std::vector<double> renderMs;  // MarkRenderStart -> FrameEnd
    std::vector<double> switchMs;  // Switch phase: the frame that applies a new font size / scale
    std::vector<double> settleMs;  // Switch phase: the 3 frames after it
    long rssKbEnd = 0;
    double wallStartEpoch = 0, wallEndEpoch = 0;  // CLOCK_REALTIME, to attribute other processes' CPU (the X server) to the phase
    // memory at the end of the phase, kB (Linux: /proc/self/smaps_rollup + status; macOS: task_info TASK_VM_INFO)
    long pssKb = 0, privateKb = 0, anonKb = 0, fileKb = 0, heapDataKb = 0, footprintKb = 0, compressedKb = 0;
    // CPU sampled once per second during the phase (process, workload ticks subtracted), %
    std::vector<double> cpuPerSecond;
};

enum ViewBit : unsigned
{
    kViewDisasm = 1,
    kViewHex = 2,
    kViewImages = 4,
    kViewTimeline = 8,
    kViewAll = 15
};

/// Memory of this process, kB. Fields a platform does not have are -1.
struct MemorySnapshot
{
    long rssKb = 0, rssPeakKb = 0;
    long footprintKb = 0;       // macOS phys_footprint; Linux private (clean + dirty)
    long footprintPeakKb = -1;  // macOS ledger peak
    long privateKb = 0, pssKb = -1, anonKb = 0, fileKb = 0, compressedKb = 0, dataKb = 0;
};
MemorySnapshot TakeMemorySnapshot();

/// "linux" / "macos" / "windows"
const char* PlatformName();
/// macOS: opt out of App Nap and timer coalescing for the run (NSActivityLatencyCritical); elsewhere nothing
void PlatformBeginBenchmark();

struct HarnessOptions
{
    std::string variant;
    unsigned views = kViewAll;     // --views=disasm,hex,images,timeline (default all); --empty = none
    bool View(ViewBit b) const { return !empty && (views & b); }
    std::string outJson;
    std::string screenshotPath;    // written once, during active50
    int screenshotFrame = 100;     // which active50 frame to capture
    bool empty = false;            // baseline: window only, no views
    float fontPx = 12;             // --font=PX   content font size
    float scale = 1;               // --scale=S   UI scale through the framework's own high-DPI mechanism
    std::string switchTest;        // --switch=font|scale  runtime switching test instead of the normal phases
    float switchFontPx = 16;       // font toggles fontPx <-> this
    float switchScale = 1.5f;      // scale toggles scale <-> this
    double warmupSec = 2, activeSec = 15, unthrottledSec = 8, idleSec = 8;
};

HarnessOptions ParseOptions(int argc, char** argv, const char* variant);

class Harness
{
public:
    explicit Harness(HarnessOptions opts);

    const HarnessOptions& Options() const { return _opts; }
    Workload& Data() { return _workload; }

    /// Current phase; advances phases by wall clock. Returns Phase::Done when the run is over.
    Phase Update();
    Phase Current() const { return _phase; }

    /// 50 Hz pacing for Warmup / Active50: the time the next frame should start
    std::chrono::steady_clock::time_point NextDeadline();
    /// Milliseconds until the current phase ends (for event-driven idle waits)
    int MsUntilPhaseEnd() const;

    /// Switch phase: true once when a toggle is due; the variant applies fontPx/scale inside the next frame
    bool PendingSwitch(float& fontPx, float& scale);

    void TickWorkload();
    void FrameBegin();
    void MarkRenderStart();
    void FrameEnd();

    /// True once, on the frame the variant should capture its window (call after rendering, before present)
    bool WantScreenshot() const;
    /// Hand the captured RGBA (top-down rows) to the harness; it writes the PNG
    void SubmitScreenshot(const uint8_t* rgba, int w, int h, int strideBytes, bool flipY);

    /// Write the JSON report; call once after Phase::Done
    void Finish(const std::string& notes = {});

    /// The variant reports the device pixel ratio it renders at (Retina = 2) and its rendering backend
    /// ("opengl", "metal", "software-gl", "qt-raster", "qt-quick-software", ...): written into the JSON
    void SetDisplay(float devicePixelRatio, std::string backend)
    {
        _devicePixelRatio = devicePixelRatio;
        _backend = std::move(backend);
    }

    static long RssKb();
    static long PeakRssKb();

private:
    void StartPhase(Phase p);
    void ClosePhase();

    HarnessOptions _opts;
    Workload _workload;
    Phase _phase = Phase::Warmup;
    std::chrono::steady_clock::time_point _phaseStart, _nextDeadline, _frameStart, _renderStart;
    bool _renderMarked = false;
    double _threadCpuStart = 0, _procCpuStart = 0, _tickCpu = 0;
    PhaseStats _cur;
    std::vector<PhaseStats> _done;
    bool _screenshotDone = false;
    uint64_t _activeFrames = 0;
    long _rssBeforeWindowKb = 0;
    Phase NextPhase(Phase p) const;
    std::chrono::steady_clock::time_point _nextSwitch;
    bool _switchState = false;
    int _framesSinceSwitch = 1000;
    bool _switchFrame = false;
    void SampleCpu(bool force);
    std::chrono::steady_clock::time_point _cpuSampleAt;
    double _cpuSampleProc = 0, _cpuSampleTick = 0;
    float _devicePixelRatio = 1;
    std::string _backend = "unknown";
};

double ThreadCpuSec();
double ProcessCpuSec();

}  // namespace bench
