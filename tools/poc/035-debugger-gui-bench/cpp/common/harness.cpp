#include "harness.h"
#include "layout.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

#include <sys/resource.h>
#include <time.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace bench
{

using Clock = std::chrono::steady_clock;

const char* PhaseName(Phase p)
{
    switch (p)
    {
    case Phase::Warmup: return "warmup";
    case Phase::Active50: return "active50";
    case Phase::Unthrottled: return "unthrottled";
    case Phase::Idle: return "idle";
    case Phase::Switch: return "switch";
    default: return "done";
    }
}

double ThreadCpuSec()
{
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

double ProcessCpuSec()
{
    timespec ts{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

#if defined(__APPLE__)
// macOS: the kernel's own accounting of the task. phys_footprint is what Activity Monitor shows as "Memory" and what
// jetsam / memory pressure act on: dirty private memory + compressed + IOKit/GPU memory charged to the task (Metal and
// IOSurface buffers included). There is no PSS: pss_kb is reported as -1.
static bool VmInfo(task_vm_info_data_t& vi)
{
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    return task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vi), &count) == KERN_SUCCESS;
}

MemorySnapshot TakeMemorySnapshot()
{
    MemorySnapshot m;
    task_vm_info_data_t vi{};
    if (!VmInfo(vi))
        return m;
    m.rssKb = long(vi.resident_size / 1024);
    m.rssPeakKb = long(vi.resident_size_peak / 1024);
    m.footprintKb = long(vi.phys_footprint / 1024);
    m.footprintPeakKb = long(vi.ledger_phys_footprint_peak / 1024);
    m.privateKb = m.footprintKb;
    m.anonKb = long(vi.internal / 1024);
    m.fileKb = long(vi.external / 1024);
    m.compressedKb = long(vi.compressed / 1024);
    m.pssKb = -1;
    m.dataKb = long(vi.virtual_size / 1024);
    return m;
}
#else
static long StatusField(const char* key)
{
    std::ifstream f("/proc/self/status");
    std::string line;
    const size_t n = std::strlen(key);
    while (std::getline(f, line))
        if (line.compare(0, n, key) == 0)
            return std::atol(line.c_str() + n);
    return 0;
}

static long RollupField(const char* key)
{
    std::ifstream f("/proc/self/smaps_rollup");
    std::string line;
    const size_t n = std::strlen(key);
    while (std::getline(f, line))
        if (line.compare(0, n, key) == 0)
            return std::atol(line.c_str() + n);
    return 0;
}

// Linux: /proc/self/smaps_rollup + status. "footprint" = private (clean + dirty), the closest to macOS phys_footprint
MemorySnapshot TakeMemorySnapshot()
{
    MemorySnapshot m;
    m.rssKb = StatusField("VmRSS:");
    m.rssPeakKb = StatusField("VmHWM:");
    m.pssKb = RollupField("Pss:");
    m.privateKb = RollupField("Private_Clean:") + RollupField("Private_Dirty:");
    m.footprintKb = m.privateKb;
    m.footprintPeakKb = -1;
    m.anonKb = RollupField("Anonymous:");
    m.fileKb = StatusField("RssFile:");
    m.dataKb = StatusField("VmData:");
    return m;
}
#endif

long Harness::RssKb() { return TakeMemorySnapshot().rssKb; }
long Harness::PeakRssKb() { return TakeMemorySnapshot().rssPeakKb; }

HarnessOptions ParseOptions(int argc, char** argv, const char* variant)
{
    HarnessOptions o;
    o.variant = variant;
    o.outJson = std::string("results/") + variant + ".json";
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        auto val = [&](const char* k) -> const char* {
            const size_t n = std::strlen(k);
            return a.compare(0, n, k) == 0 ? a.c_str() + n : nullptr;
        };
        if (auto v = val("--out="))
            o.outJson = v;
        else if (auto v = val("--screenshot="))
            o.screenshotPath = v;
        else if (auto v = val("--screenshot-frame="))
            o.screenshotFrame = std::atoi(v);
        else if (a == "--empty")
            o.empty = true;
        else if (auto v = val("--font="))
            o.fontPx = float(std::atof(v));
        else if (auto v = val("--scale="))
            o.scale = float(std::atof(v));
        else if (auto v = val("--switch="))
            o.switchTest = v;
        else if (auto v = val("--views="))
        {
            const std::string list = v;
            o.views = 0;
            if (list.find("disasm") != std::string::npos) o.views |= kViewDisasm;
            if (list.find("hex") != std::string::npos) o.views |= kViewHex;
            if (list.find("images") != std::string::npos) o.views |= kViewImages;
            if (list.find("timeline") != std::string::npos) o.views |= kViewTimeline;
        }
        else if (auto v = val("--active="))
            o.activeSec = std::atof(v);
        else if (auto v = val("--unthrottled="))
            o.unthrottledSec = std::atof(v);
        else if (auto v = val("--idle="))
            o.idleSec = std::atof(v);
        else if (auto v = val("--warmup="))
            o.warmupSec = std::atof(v);
    }
    if (o.empty)
        o.views = 0;
    return o;
}

Harness::Harness(HarnessOptions opts) : _opts(std::move(opts))
{
    PlatformBeginBenchmark();
    layout::SetContentFont(_opts.fontPx);
    _rssBeforeWindowKb = RssKb();
    StartPhase(Phase::Warmup);
}

void Harness::StartPhase(Phase p)
{
    _phase = p;
    _cur = PhaseStats{};
    _cur.phase = p;
    _phaseStart = Clock::now();
    _cur.wallStartEpoch = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    _nextDeadline = _phaseStart;
    _threadCpuStart = ThreadCpuSec();
    _procCpuStart = ProcessCpuSec();
    _tickCpu = 0;
    _workload.SetPaused(p == Phase::Idle);
    _nextSwitch = _phaseStart + std::chrono::milliseconds(250);
    _cpuSampleAt = _phaseStart;
    _cpuSampleProc = _procCpuStart;
    _cpuSampleTick = 0;
}

void Harness::SampleCpu(bool force)
{
    const auto now = Clock::now();
    const double el = std::chrono::duration<double>(now - _cpuSampleAt).count();
    if (el < 1.0 && !(force && el > 0.25))
        return;
    const double proc = ProcessCpuSec();
    _cur.cpuPerSecond.push_back(100.0 * ((proc - _cpuSampleProc) - (_tickCpu - _cpuSampleTick)) / el);
    _cpuSampleAt = now;
    _cpuSampleProc = proc;
    _cpuSampleTick = _tickCpu;
}

Phase Harness::NextPhase(Phase p) const
{
    if (!_opts.switchTest.empty())
        return p == Phase::Warmup ? Phase::Switch : Phase::Done;
    return p == Phase::Idle ? Phase::Done : Phase(int(p) + 1);
}

bool Harness::PendingSwitch(float& fontPx, float& scale)
{
    if (_phase != Phase::Switch || Clock::now() < _nextSwitch)
        return false;
    _nextSwitch += std::chrono::milliseconds(250);
    _switchState = !_switchState;
    fontPx = _opts.fontPx;
    scale = _opts.scale;
    if (_switchState && _opts.switchTest == "font")
        fontPx = _opts.switchFontPx;
    if (_switchState && _opts.switchTest == "scale")
        scale = _opts.switchScale;
    layout::SetContentFont(fontPx);
    _switchFrame = true;
    return true;
}

void Harness::ClosePhase()
{
    _cur.wallSec = std::chrono::duration<double>(Clock::now() - _phaseStart).count();
    _cur.wallEndEpoch = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    _cur.tickCpuSec = _tickCpu;
    _cur.mainThreadCpuSec = ThreadCpuSec() - _threadCpuStart - _tickCpu;
    _cur.processCpuSec = ProcessCpuSec() - _procCpuStart - _tickCpu;
    SampleCpu(true);
    const MemorySnapshot m = TakeMemorySnapshot();
    _cur.rssKbEnd = m.rssKb;
    _cur.pssKb = m.pssKb;
    _cur.privateKb = m.privateKb;
    _cur.anonKb = m.anonKb;
    _cur.fileKb = m.fileKb;
    _cur.heapDataKb = m.dataKb;
    _cur.footprintKb = m.footprintKb;
    _cur.compressedKb = m.compressedKb;
    if (_phase != Phase::Warmup)
        _done.push_back(std::move(_cur));
}

Phase Harness::Update()
{
    if (_phase == Phase::Done)
        return _phase;
    SampleCpu(false);
    const double el = std::chrono::duration<double>(Clock::now() - _phaseStart).count();
    double limit = 0;
    switch (_phase)
    {
    case Phase::Warmup: limit = _opts.warmupSec; break;
    case Phase::Active50: limit = _opts.activeSec; break;
    case Phase::Unthrottled: limit = _opts.unthrottledSec; break;
    case Phase::Idle: limit = _opts.idleSec; break;
    case Phase::Switch: limit = 10.0; break;
    default: break;
    }
    if (el >= limit)
    {
        ClosePhase();
        StartPhase(NextPhase(_phase));
        if (_phase == Phase::Done)
            return _phase;
    }
    return _phase;
}

Clock::time_point Harness::NextDeadline()
{
    _nextDeadline += std::chrono::microseconds(20000);
    const auto now = Clock::now();
    if (_nextDeadline < now - std::chrono::milliseconds(40))
        _nextDeadline = now;  // fell behind: do not try to catch up with a burst
    return _nextDeadline;
}

int Harness::MsUntilPhaseEnd() const
{
    double limit = _phase == Phase::Switch ? 10.0 : _phase == Phase::Idle ? _opts.idleSec : _phase == Phase::Unthrottled ? _opts.unthrottledSec
                   : _phase == Phase::Active50 ? _opts.activeSec : _opts.warmupSec;
    const double el = std::chrono::duration<double>(Clock::now() - _phaseStart).count();
    return std::max(0, int((limit - el) * 1000.0) + 1);
}

void Harness::TickWorkload()
{
    const double t0 = ThreadCpuSec();
    _workload.Tick();
    _tickCpu += ThreadCpuSec() - t0;
}

void Harness::FrameBegin()
{
    _frameStart = Clock::now();
    _renderMarked = false;
}

void Harness::MarkRenderStart()
{
    _renderStart = Clock::now();
    _renderMarked = true;
}

void Harness::FrameEnd()
{
    const auto end = Clock::now();
    _cur.frames++;
    _cur.frameMs.push_back(std::chrono::duration<double, std::milli>(end - _frameStart).count());
    if (_renderMarked)
    {
        _cur.buildMs.push_back(std::chrono::duration<double, std::milli>(_renderStart - _frameStart).count());
        _cur.renderMs.push_back(std::chrono::duration<double, std::milli>(end - _renderStart).count());
    }
    if (_phase == Phase::Active50)
        _activeFrames++;
    if (_phase == Phase::Switch)
    {
        const double ms = _cur.frameMs.back();
        if (_switchFrame)
        {
            _cur.switchMs.push_back(ms);
            _switchFrame = false;
            _framesSinceSwitch = 0;
        }
        else if (++_framesSinceSwitch <= 3)
            _cur.settleMs.push_back(ms);
    }
}

bool Harness::WantScreenshot() const
{
    return !_screenshotDone && !_opts.screenshotPath.empty() && _phase == Phase::Active50 && _activeFrames == uint64_t(_opts.screenshotFrame);
}

void Harness::SubmitScreenshot(const uint8_t* rgba, int w, int h, int strideBytes, bool flipY)
{
    std::vector<uint8_t> rows(size_t(w) * h * 4);
    for (int y = 0; y < h; y++)
    {
        const uint8_t* src = rgba + size_t(flipY ? h - 1 - y : y) * strideBytes;
        uint8_t* dst = rows.data() + size_t(y) * w * 4;
        std::memcpy(dst, src, size_t(w) * 4);
        for (int x = 0; x < w; x++)
            dst[x * 4 + 3] = 255;
    }
    stbi_write_png(_opts.screenshotPath.c_str(), w, h, 4, rows.data(), w * 4);
    _screenshotDone = true;
}

static double Percentile(std::vector<double> v, double p)
{
    if (v.empty())
        return 0;
    std::sort(v.begin(), v.end());
    const size_t i = std::min(v.size() - 1, size_t(p * (v.size() - 1) + 0.5));
    return v[i];
}

static double Mean(const std::vector<double>& v)
{
    if (v.empty())
        return 0;
    double s = 0;
    for (double x : v)
        s += x;
    return s / v.size();
}

void Harness::Finish(const std::string& notes)
{
    std::ostringstream o;
    o.setf(std::ios::fixed);
    o.precision(4);
    o << "{\n  \"variant\": \"" << _opts.variant << "\",\n  \"views\": " << _opts.views
      << ",\n  \"font_px\": " << _opts.fontPx << ", \"scale\": " << _opts.scale << ", \"switch\": \"" << _opts.switchTest
      << "\",\n  \"platform\": \"" << PlatformName() << "\", \"device_pixel_ratio\": " << _devicePixelRatio
      << ", \"backend\": \"" << _backend << "\""
      << ",\n  \"rss_kb_start\": " << _rssBeforeWindowKb << ",\n  \"rss_kb_peak\": " << PeakRssKb()
      << ", \"footprint_kb_peak\": " << TakeMemorySnapshot().footprintPeakKb
      << ",\n  \"notes\": \"" << notes << "\",\n  \"phases\": [\n";
    for (size_t i = 0; i < _done.size(); i++)
    {
        const auto& s = _done[i];
        o << "    {\"phase\": \"" << PhaseName(s.phase) << "\", \"frames\": " << s.frames << ", \"wall_s\": " << s.wallSec
          << ", \"fps\": " << (s.wallSec > 0 ? s.frames / s.wallSec : 0) << ", \"main_cpu_pct\": "
          << (s.wallSec > 0 ? 100.0 * s.mainThreadCpuSec / s.wallSec : 0) << ", \"process_cpu_pct\": "
          << (s.wallSec > 0 ? 100.0 * s.processCpuSec / s.wallSec : 0) << ", \"tick_cpu_s\": " << s.tickCpuSec
          << ", \"frame_ms_mean\": " << Mean(s.frameMs) << ", \"frame_ms_p50\": " << Percentile(s.frameMs, 0.5)
          << ", \"frame_ms_p95\": " << Percentile(s.frameMs, 0.95) << ", \"frame_ms_p99\": " << Percentile(s.frameMs, 0.99)
          << ", \"frame_ms_max\": " << Percentile(s.frameMs, 1.0) << ", \"build_ms_p50\": " << Percentile(s.buildMs, 0.5)
          << ", \"render_ms_p50\": " << Percentile(s.renderMs, 0.5) << ", \"switch_n\": " << s.switchMs.size()
          << ", \"switch_ms_p50\": " << Percentile(s.switchMs, 0.5) << ", \"switch_ms_max\": " << Percentile(s.switchMs, 1.0)
          << ", \"settle_ms_p50\": " << Percentile(s.settleMs, 0.5) << ", \"cpu_1s_mean\": " << Mean(s.cpuPerSecond)
          << ", \"cpu_1s_max\": " << Percentile(s.cpuPerSecond, 1.0) << ", \"rss_kb_end\": " << s.rssKbEnd
          << ", \"t0\": " << std::setprecision(3) << s.wallStartEpoch << ", \"t1\": " << s.wallEndEpoch << std::setprecision(4)
          << ", \"pss_kb\": " << s.pssKb << ", \"private_kb\": " << s.privateKb << ", \"anon_kb\": " << s.anonKb
          << ", \"rss_file_kb\": " << s.fileKb << ", \"vm_data_kb\": " << s.heapDataKb << ", \"footprint_kb\": " << s.footprintKb
          << ", \"compressed_kb\": " << s.compressedKb << "}"
          << (i + 1 < _done.size() ? "," : "") << "\n";
    }
    o << "  ]\n}\n";
    // notes come from the variants: keep the JSON valid
    std::ofstream f(_opts.outJson);
    f << o.str();
    std::fprintf(stderr, "%s", o.str().c_str());
}

const char* PlatformName()
{
#if defined(__APPLE__)
    return "macos";
#elif defined(_WIN32)
    return "windows";
#else
    return "linux";
#endif
}

#if !defined(__APPLE__)
void PlatformBeginBenchmark() {}
#endif

}  // namespace bench
