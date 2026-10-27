#include "lab.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <random>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#endif

namespace poc037
{

int64_t NowNs()
{
#ifdef _WIN32
    static const int64_t freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<int64_t>(f.QuadPart);
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<int64_t>(static_cast<double>(c.QuadPart) * 1e9 / static_cast<double>(freq));
#elif defined(__APPLE__)
    return static_cast<int64_t>(clock_gettime_nsec_np(CLOCK_UPTIME_RAW));  // mach_absolute_time in ns
#else
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
#endif
}

void SleepUntilNs(int64_t t)
{
#ifdef _WIN32
    // TimeHelper::WaitUntilPrecise: the C++ library waits on MinGW have ~10-15 ms granularity; a high-resolution
    // waitable timer (Windows 10 1803+) wakes within ~0.5 ms
    thread_local HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /* HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
#endif
    for (;;)
    {
        const int64_t left = t - NowNs();
        if (left <= 0)
            return;
#ifdef _WIN32
        if (timer != nullptr && left > 700000)
        {
            LARGE_INTEGER due;
            due.QuadPart = -(left - 500000) / 100;  // 100 ns units, relative
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
            {
                WaitForSingleObject(timer, INFINITE);
                continue;
            }
        }
        std::this_thread::yield();  // the last 0.7 ms: a library sleep could overshoot by a whole tick
        continue;
#endif
        if (left > 1500000)
            std::this_thread::sleep_for(std::chrono::nanoseconds(left - 1000000));
        else if (left > 200000)
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        else
            std::this_thread::yield();
    }
}

void KeepDisplayOn()
{
#ifdef _WIN32
    SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;  // a zero move: the user's pointer stays where it is
    SendInput(1, &in, sizeof in);
    SleepUntilNs(NowNs() + 2000000000LL);  // the monitor's wake-up
#endif
}

void RaiseThreadPriority(bool realtime)
{
#ifdef __APPLE__
    if (realtime)
    {
        // ThreadHelper::setRealtimePriority: a time-constraint thread, 20 ms period, 4 ms budget, start within 10 ms
        mach_timebase_info_data_t tb;
        mach_timebase_info(&tb);
        auto abs = [&](uint64_t ns) { return static_cast<uint32_t>(ns * tb.denom / tb.numer); };
        thread_time_constraint_policy_data_t policy;
        policy.period = abs(20000000ULL);
        policy.computation = abs(4000000ULL);
        policy.constraint = abs(10000000ULL);
        policy.preemptible = 1;
        thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
                          reinterpret_cast<thread_policy_t>(&policy), THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    }
    else
    {
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    }
#elif defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), realtime ? THREAD_PRIORITY_ABOVE_NORMAL : THREAD_PRIORITY_HIGHEST);
#else
    (void)realtime;
#endif
}

int64_t ThreadCpuNs()
{
#ifdef _WIN32
    FILETIME c, e, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u))
        return 0;
    auto ft = [](const FILETIME& f) { return (static_cast<int64_t>(f.dwHighDateTime) << 32 | f.dwLowDateTime) * 100; };
    return ft(k) + ft(u);
#else
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
#endif
}

double RateOf(const std::string& name)
{
    // T-states per frame at the 3.5 MHz CPU clock (CLK_28 / 8 on the Next), as CalculateFrameDurationUs and
    // PortDecoder_Next::FrameDurationUs take them; the Next's clocks are NextBoard::kClk28Artix
    struct Preset
    {
        const char* name;
        double tstates;
        double clockHz;
    };
    static const Preset presets[] = {
        {"pentagon", 71680, 3500000},        {"zx48", 69888, 3500000},
        {"next128-50", 70908, 28000000 / 8.0}, {"next60-vga0", 60192, 28000000 / 8.0},
        {"next60-vga1", 60192, 28583333 / 8.0}, {"next60-vga2", 60192, 29437500 / 8.0},
        {"next48-60", 59136, 28000000 / 8.0},
    };
    for (const Preset& p : presets)
        if (name == p.name)
            return p.clockHz / p.tstates;
    return std::atof(name.c_str());
}

Options ParseOptions(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--variant")
            o.variant = next();
        else if (a == "--backend")
            o.backend = next();
        else if (a == "--target")
            o.target = next();
        else if (a == "--link")
            o.link = next();
        else if (a == "--vrr")
            o.vrr = next();
        else if (a == "--rate")
        {
            o.tag = o.tag.empty() ? argv[i + 1] : o.tag;
            o.rate = RateOf(next());
        }
        else if (a == "--seconds")
            o.seconds = std::atof(next().c_str());
        else if (a == "--delay-ms")
            o.delayMs = std::atof(next().c_str());
        else if (a == "--work-ms")
            o.workMs = std::atof(next().c_str());
        else if (a == "--frame-latency")
            o.frameLatency = std::atof(next().c_str());
        else if (a == "--scale")
            o.scale = std::atoi(next().c_str());
        else if (a == "--out")
            o.out = next();
        else if (a == "--tag")
            o.tag = next();
        else if (a == "--fullscreen")
            o.fullscreen = true;
        else if (a == "--exclusive")
            o.exclusive = true;
    }
    if (!o.variant.empty())
        o.variant = o.variant.substr(0, 1);
    return o;
}

std::string RunPath(const Options& o, const std::string& shell)
{
    char rate[32];
    std::snprintf(rate, sizeof rate, "%.3f", o.rate);
    std::string name = shell + "-" + o.backend + "-" + o.variant;
    if (o.variant == "c")
        name += "-" + o.vrr;
    if (o.backend == "native" && o.link != "metal")
        name += "-" + o.link;
    name += "-" + std::string(rate);
    if (o.variant == "c" && o.target != "latch")
        name += "-" + o.target;
    if (!o.tag.empty() && o.tag != rate)
        name += "-" + o.tag;
    return o.out + "/" + name + ".csv";
}

// region FrameRing

FrameRing::FrameRing()
{
    for (Frame& f : _slots)
        f.pixels.assign(static_cast<size_t>(kWidth) * kHeight, 0);
}

void FrameRing::Latch(uint64_t number, int64_t latchNs, const std::vector<uint32_t>& pixels)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        Frame& f = _slots[number % kSlots];
        f.number = number;
        f.latchNs = latchNs;
        std::memcpy(f.pixels.data(), pixels.data(), pixels.size() * sizeof(uint32_t));
        _count = number + 1;
    }
    _cv.notify_all();
}

bool FrameRing::CopyAt(int64_t t, Frame& out)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_count == 0)
        return false;
    uint64_t n = _count - 1;
    if (t != 0)
    {
        // Walk back to the newest frame latched by t (the ring holds kSlots of them)
        uint64_t k = 0;
        while (k + 1 < kSlots && n > 0 && _slots[n % kSlots].latchNs > t)
        {
            n--;
            k++;
        }
        if (_slots[n % kSlots].latchNs > t)
            return false;
    }
    const Frame& f = _slots[n % kSlots];
    out.number = f.number;
    out.latchNs = f.latchNs;
    out.pixels = f.pixels;
    return true;
}

bool FrameRing::Newest(uint64_t& number, int64_t& latchNs)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_count == 0)
        return false;
    number = _count - 1;
    latchNs = _slots[number % kSlots].latchNs;
    return true;
}

bool FrameRing::CopyNumber(uint64_t number, Frame& out)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (number >= _count || number + kSlots < _count)
        return false;
    const Frame& f = _slots[number % kSlots];
    out.number = f.number;
    out.latchNs = f.latchNs;
    out.pixels = f.pixels;
    return true;
}

bool FrameRing::WaitNewer(uint64_t after, int64_t deadlineNs)
{
    std::unique_lock<std::mutex> lock(_mutex);
    for (;;)
    {
        if (_count > 0 && _count - 1 > after)
            return true;
        if (after == UINT64_MAX && _count > 0)
            return true;
        const int64_t left = deadlineNs - NowNs();
        if (left <= 0)
            return false;
        _cv.wait_for(lock, std::chrono::nanoseconds(left));
    }
}

// endregion

// region Producer

Producer::Producer(FrameRing& ring, double rateHz, double workMs) : _ring(ring), _rate(rateHz), _workMs(workMs)
{
    _pixels.assign(static_cast<size_t>(kWidth) * kHeight, 0);
    _latches.reserve(200000);
}

double Producer::LatchJitterP99Ms() const
{
    if (_latches.size() < 3)
        return 0;
    const double period = 1e9 / _rate;
    std::vector<double> dev;
    for (size_t i = 1; i < _latches.size(); i++)
        if (_latches[i] - _latches[0] > 2000000000)
            dev.push_back(std::abs(static_cast<double>(_latches[i] - _latches[i - 1]) - period) / 1e6);
    if (dev.empty())
        return 0;
    std::sort(dev.begin(), dev.end());
    return dev[static_cast<size_t>(static_cast<double>(dev.size() - 1) * 0.99)];
}

Producer::~Producer()
{
    Stop();
}

void Producer::StartClocked()
{
    _thread = std::thread([this] { Run(false); });
}

void Producer::StartTicked()
{
    _thread = std::thread([this] { Run(true); });
}

void Producer::Tick()
{
    {
        std::lock_guard<std::mutex> lock(_tickMutex);
        _ticks++;
    }
    _tickCv.notify_one();
}

void Producer::Stop()
{
    _stop = true;
    _tickCv.notify_all();
    if (_thread.joinable())
        _thread.join();
}

void Producer::Render(uint64_t n)
{
    // Vertical bars scrolling 4 px per frame over a grey ramp: a repeated or skipped frame is a visible jerk
    const int shift = static_cast<int>((n * 4) % kWidth);
    for (int y = 0; y < kHeight; y++)
    {
        uint32_t* row = _pixels.data() + static_cast<size_t>(y) * kWidth;
        const uint32_t grey = 0x30 + static_cast<uint32_t>(y * 0x40 / kHeight);
        const uint32_t bg = 0xFF000000u | grey << 16 | grey << 8 | grey;
        for (int x = 0; x < kWidth; x++)
        {
            const int u = (x + shift) % 48;
            row[x] = u < 6 ? 0xFFFFFFFFu : (u < 8 ? 0xFFFF3030u : bg);
        }
    }
    // The frame number, 16 bits as 16 x 8 blocks in the top-left corner (white = 1), for a camera or a capture
    for (int b = 0; b < 16; b++)
    {
        const uint32_t c = (n >> b) & 1 ? 0xFFFFFFFFu : 0xFF000000u;
        for (int y = 4; y < 20; y++)
            for (int x = 0; x < 16; x++)
                _pixels[static_cast<size_t>(y) * kWidth + 4 + b * 18 + x] = c;
    }
}

void Producer::Run(bool ticked)
{
    RaiseThreadPriority(true);
    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> work(0.5, 1.5);
    const int64_t period = static_cast<int64_t>(1e9 / _rate);
    int64_t next = NowNs();
    uint64_t seenTicks = 0;
    uint64_t n = 0;
    while (!_stop)
    {
        if (ticked)
        {
            std::unique_lock<std::mutex> lock(_tickMutex);
            _tickCv.wait(lock, [&] { return _stop || _ticks > seenTicks; });
            if (_stop)
                break;
            seenTicks = _ticks;
        }
        // "Emulate": render, then hold the thread for the rest of the frame's emulation time
        const int64_t start = NowNs();
        Render(n);
        SleepUntilNs(start + static_cast<int64_t>(_workMs * work(rng) * 1e6));
        const int64_t latch = NowNs();
        _ring.Latch(n, latch, _pixels);
        if (_latches.size() < _latches.capacity())
            _latches.push_back(latch);
        _frames = n + 1;
        if (_onLatch)
            _onLatch(n);
        n++;
        if (!ticked)
        {
            // MainLoop: re-anchor when far off, then advance the absolute deadline by one frame
            const int64_t now = NowNs();
            if (next < now - 2 * period || next > now + period)
                next = now;
            next += period;
            SleepUntilNs(next);
        }
    }
}

// endregion

// region PresentLog

PresentLog::PresentLog(size_t capacity)
{
    _records.reserve(capacity);
}

size_t PresentLog::Add(const PresentRecord& r)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _records.push_back(r);
    return _records.size() - 1;
}

void PresentLog::SetPresented(size_t id, int64_t presentedNs)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (id < _records.size())
        _records[id].presentedNs = presentedNs;
}

void PresentLog::SetTiming(size_t id, int64_t submitNs, int64_t cpuNs)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (id < _records.size())
    {
        _records[id].submitNs = submitNs;
        _records[id].cpuNs = cpuNs;
    }
}

void PresentLog::ResolveByVBlanks(const std::vector<int64_t>& vblanks)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto onAfter = [&](int64_t t) -> int64_t {
        const auto it = std::lower_bound(vblanks.begin(), vblanks.end(), t);
        return it == vblanks.end() ? -1 : *it;
    };
    for (size_t i = 0; i < _records.size(); i++)
    {
        PresentRecord& r = _records[i];
        if (r.submitNs <= 0)
            continue;
        const int64_t on = onAfter(r.submitNs);
        const bool replaced = i + 1 < _records.size() && _records[i + 1].submitNs > 0 && onAfter(_records[i + 1].submitNs) == on;
        r.presentedNs = on < 0 ? -1 : (replaced ? 0 : on);
    }
}

void PresentLog::Meta(const std::string& key, const std::string& value)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _meta.emplace_back(key, value);
}

void PresentLog::Write(const std::string& path)
{
    std::lock_guard<std::mutex> lock(_mutex);
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f)
    {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        return;
    }
#ifndef _WIN32
    double load[3] = {0, 0, 0};
    if (getloadavg(load, 3) == 3)
        std::fprintf(f, "# load=%.1f %.1f %.1f\n", load[0], load[1], load[2]);
#endif
    for (const auto& [k, v] : _meta)
        std::fprintf(f, "# %s=%s\n", k.c_str(), v.c_str());
    std::fprintf(f, "frame,latch_ns,tick_ns,target_ns,submit_ns,presented_ns,cpu_ns\n");
    for (const PresentRecord& r : _records)
        std::fprintf(f, "%llu,%lld,%lld,%lld,%lld,%lld,%lld\n", static_cast<unsigned long long>(r.frame),
                     static_cast<long long>(r.latchNs), static_cast<long long>(r.tickNs), static_cast<long long>(r.targetNs),
                     static_cast<long long>(r.submitNs), static_cast<long long>(r.presentedNs), static_cast<long long>(r.cpuNs));
    std::fclose(f);
    std::printf("wrote %s (%zu presents)\n", path.c_str(), _records.size());
}

// endregion

}  // namespace poc037
