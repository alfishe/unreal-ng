// Smooth motion's live trace (UNREAL_SMOOTH_TRACE, core/src/emulator/video/smoothtrace.h) replayed through the core.
//
//   replay <trace dir> <out dir>
//
// 1. Equivalence: the same frames pushed and the same refresh times planned, in the trace's order, on the CPU: every
//    plan and every picture must be the window's (which may have drawn on the GPU). Prints the refreshes that differ.
// 2. Timing: the refresh periods (refreshes skipped), the frames' target times against a line fitted to them (the
//    jitter the audio clock gives), the look-ahead (the newest frame's target minus the refresh time).
// 3. For smoothness.py (README.md here): <out dir>/source.rgba + .json (the frames), live.rgba (the window's
//    pictures) and times.txt (each refresh's time in frames, by the fitted line), the same refreshes replayed with
//    `off` and `blend` (off.rgba, blend.rgba), and ideal/ (interpolate on a perfect clock: frames every fitted period,
//    refreshes every mean refresh period, pushed two frames ahead: what the timing costs).
#include "emulator/video/smoothmotion.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace smoothmotion;

namespace
{
struct FrameEvent
{
    uint64_t serial = 0;
    int64_t target = 0;
    size_t index = 0;  // in frames.rgba
};

struct RefreshEvent
{
    int64_t t = 0;
    SmoothMotion::Result plan;
    bool drawn = false;
    size_t index = 0;  // in refreshes.rgba
};

struct Event
{
    bool frame = false;
    size_t index = 0;  // into frames / refreshes
};

int g_width = 0;
int g_height = 0;
std::vector<FrameEvent> g_frames;
std::vector<RefreshEvent> g_refreshes;
std::vector<Event> g_events;
std::vector<uint32_t> g_framePixels;
std::vector<uint32_t> g_refreshPixels;
Mode g_mode = Mode::Interpolate;

size_t Pixels()
{
    return static_cast<size_t>(g_width) * g_height;
}
const uint32_t* FramePixels(size_t i)
{
    return g_framePixels.data() + i * Pixels();
}
const uint32_t* RefreshPixels(size_t i)
{
    return g_refreshPixels.data() + i * Pixels();
}

bool ReadAll(const std::string& path, std::vector<uint32_t>& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(in.tellg());
    in.seekg(0);
    out.resize(size / 4);
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size() * 4));
    return true;
}

bool Load(const std::string& dir)
{
    std::ifstream in(dir + "/events.tsv");
    if (!in)
    {
        std::fprintf(stderr, "no %s/events.tsv\n", dir.c_str());
        return false;
    }
    std::string line;
    bool ended = false;
    while (std::getline(in, line))
    {
        std::istringstream s(line);
        std::string word;
        s >> word;
        if (word == "start")
        {
            std::string mode;
            s >> mode;
            ParseMode(mode.c_str(), g_mode);
        }
        else if (word == "frame")
        {
            FrameEvent f;
            unsigned long long serial = 0;
            long long target = 0;
            int w = 0, h = 0;
            s >> serial >> target >> w >> h;
            if (g_width == 0)
            {
                g_width = w;
                g_height = h;
            }
            if (w != g_width || h != g_height)
            {
                std::fprintf(stderr, "the frame size changed at serial %llu: one size per trace\n", serial);
                return false;
            }
            f.serial = serial;
            f.target = target;
            f.index = g_frames.size();
            g_events.push_back({true, g_frames.size()});
            g_frames.push_back(f);
        }
        else if (word == "refresh")
        {
            RefreshEvent r;
            long long t = 0;
            int kind = 0, drawn = 0;
            unsigned long long serial = 0, from = 0, to = 0;
            s >> t >> kind >> serial >> from >> to >> r.plan.weight >> drawn;
            r.t = t;
            r.plan.kind = static_cast<SmoothMotion::Kind>(kind);
            r.plan.serial = serial;
            r.plan.from = from;
            r.plan.to = to;
            r.drawn = drawn != 0;
            r.index = g_refreshes.size();
            g_events.push_back({false, g_refreshes.size()});
            g_refreshes.push_back(r);
        }
        else if (word == "end")
            ended = true;
    }
    if (!ended)
        std::printf("note: the trace has no end line (the window closed early?)\n");
    if (!ReadAll(dir + "/frames.rgba", g_framePixels) || !ReadAll(dir + "/refreshes.rgba", g_refreshPixels))
        return false;
    if (g_framePixels.size() < g_frames.size() * Pixels() || g_refreshPixels.size() < g_refreshes.size() * Pixels())
    {
        std::fprintf(stderr, "the pixel files are shorter than the events\n");
        return false;
    }
    return g_width > 0 && !g_refreshes.empty();
}

/// Replays the trace's order with `mode` on the CPU; `pictures` gets each refresh's picture (the frame due when the
/// plan has none); `plans` the plans
void Replay(Mode mode, std::vector<uint32_t>& pictures, std::vector<SmoothMotion::Result>& plans)
{
    SmoothMotion sm;
    sm.SetMode(mode);
    pictures.assign(g_refreshes.size() * Pixels(), 0);
    plans.assign(g_refreshes.size(), {});
    std::vector<uint32_t> shownPixels(Pixels(), 0);
    SmoothMotion::Result shown;
    for (const Event& e : g_events)
    {
        if (e.frame)
        {
            const FrameEvent& f = g_frames[e.index];
            sm.Push(f.serial, f.target, FramePixels(f.index), g_width, g_height);
            continue;
        }
        const RefreshEvent& r = g_refreshes[e.index];
        const SmoothMotion::Result plan = sm.Plan(r.t);
        plans[e.index] = plan;
        uint32_t* out = pictures.data() + e.index * Pixels();
        if (plan.kind == SmoothMotion::Kind::None)
        {
            std::memcpy(out, RefreshPixels(r.index), Pixels() * 4);  // the frame due: not smooth motion's
            continue;
        }
        if (!(plan == shown) && sm.Draw(plan, shownPixels.data()))
            shown = plan;
        std::memcpy(out, shownPixels.data(), Pixels() * 4);
    }
}

void Write(const std::string& path, const void* data, size_t bytes)
{
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
    {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        std::exit(1);
    }
    std::fwrite(data, 1, bytes, f);
    std::fclose(f);
}

void WriteJson(const std::string& path, size_t count)
{
    std::FILE* f = std::fopen(path.c_str(), "w");
    std::fprintf(f, "{\"width\": %d, \"height\": %d, \"count\": %zu}\n", g_width, g_height, count);
    std::fclose(f);
}
}  // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: replay <trace dir> <out dir>\n");
        return 2;
    }
    const std::string traceDir = argv[1];
    const std::string outDir = argv[2];
    if (!Load(traceDir))
        return 1;
    std::printf("trace: %s, %d x %d, %zu frames, %zu refreshes\n", ModeName(g_mode), g_width, g_height,
                g_frames.size(), g_refreshes.size());

    // 1. Equivalence
    std::vector<uint32_t> replayed;
    std::vector<SmoothMotion::Result> plans;
    Replay(g_mode, replayed, plans);
    size_t planDiffs = 0, pictureDiffs = 0, none = 0, compared = 0;
    size_t worstPixels = 0;
    for (const RefreshEvent& r : g_refreshes)
    {
        if (!(plans[r.index] == r.plan))
        {
            if (planDiffs < 5)
                std::printf("  refresh %zu: plan kind %d %llu..%llu w %d, the window's kind %d %llu..%llu w %d\n",
                            r.index, static_cast<int>(plans[r.index].kind),
                            static_cast<unsigned long long>(plans[r.index].from),
                            static_cast<unsigned long long>(plans[r.index].to), plans[r.index].weight,
                            static_cast<int>(r.plan.kind), static_cast<unsigned long long>(r.plan.from),
                            static_cast<unsigned long long>(r.plan.to), r.plan.weight);
            planDiffs++;
        }
        if (r.plan.kind == SmoothMotion::Kind::None)
        {
            none++;
            continue;
        }
        compared++;
        size_t differ = 0;
        const uint32_t* a = RefreshPixels(r.index);
        const uint32_t* b = replayed.data() + r.index * Pixels();
        for (size_t p = 0; p < Pixels(); p++)
            differ += (a[p] & 0x00FFFFFFu) != (b[p] & 0x00FFFFFFu);
        if (differ)
        {
            pictureDiffs++;
            worstPixels = std::max(worstPixels, differ);
        }
    }
    std::printf("equivalence: %zu refreshes compared (%zu before the first frame), plans differ %zu, pictures differ "
                "%zu (worst %zu pixels)\n",
                compared, none, planDiffs, pictureDiffs, worstPixels);

    // 2. Timing. Frames: serials in a row?
    size_t gaps = 0;
    for (size_t i = 1; i < g_frames.size(); i++)
        gaps += g_frames[i].serial != g_frames[i - 1].serial + 1;
    // The frames' line: target = a + b k
    const size_t n = g_frames.size();
    double sk = 0, st = 0, skk = 0, skt = 0;
    const double t0 = static_cast<double>(g_frames.front().target);
    for (size_t k = 0; k < n; k++)
    {
        const double t = static_cast<double>(g_frames[k].target) - t0;
        sk += k;
        st += t;
        skk += static_cast<double>(k) * k;
        skt += k * t;
    }
    const double b = (n * skt - sk * st) / (n * skk - sk * sk);
    const double a = (st - b * sk) / n + t0;
    double jitter = 0, jitterMax = 0;
    for (size_t k = 0; k < n; k++)
    {
        const double e = static_cast<double>(g_frames[k].target) - (a + b * k);
        jitter += e * e;
        jitterMax = std::max(jitterMax, std::fabs(e));
    }
    jitter = std::sqrt(jitter / n);
    // Refreshes
    std::vector<double> periods;
    for (size_t i = 1; i < g_refreshes.size(); i++)
        periods.push_back(static_cast<double>(g_refreshes[i].t - g_refreshes[i - 1].t));
    std::vector<double> sorted = periods;
    std::sort(sorted.begin(), sorted.end());
    const double period = sorted.empty() ? 0.0 : sorted[sorted.size() / 2];
    size_t skipped = 0, repeated = 0;
    for (double p : periods)
    {
        if (p > 1.5 * period)
            skipped += static_cast<size_t>(std::lround(p / period)) - 1;
        if (p < 0.5 * period)
            repeated++;
    }
    // Look-ahead at each refresh: the newest frame pushed before it
    double aheadMin = 1e18, aheadSum = 0;
    size_t aheadCount = 0;
    int64_t newest = 0;
    bool any = false;
    for (const Event& e : g_events)
    {
        if (e.frame)
        {
            newest = g_frames[e.index].target;
            any = true;
            continue;
        }
        if (!any)
            continue;
        const double ahead = static_cast<double>(newest - g_refreshes[e.index].t);
        aheadMin = std::min(aheadMin, ahead);
        aheadSum += ahead;
        aheadCount++;
    }
    std::map<int, size_t> kinds;
    size_t drawn = 0;
    for (const RefreshEvent& r : g_refreshes)
    {
        kinds[static_cast<int>(r.plan.kind)]++;
        drawn += r.drawn;
    }
    std::printf("frames: period %.4f ms (%.3f Hz), target jitter rms %.3f ms, max %.3f ms, serial gaps %zu\n",
                b / 1e6, 1e9 / b, jitter / 1e6, jitterMax / 1e6, gaps);
    std::printf("refreshes: period %.4f ms (%.3f Hz), skipped %zu, closer than half a period %zu, drawn %zu\n",
                period / 1e6, 1e9 / period, skipped, repeated, drawn);
    if (aheadCount)
        std::printf("look-ahead: mean %.2f ms, min %.2f ms (the newest frame's target minus the refresh time)\n",
                    aheadSum / aheadCount / 1e6, aheadMin / 1e6);
    std::printf("kinds:");
    const char* names[] = {"none", "frame", "blend", "interpolate", "cadence", "carry"};
    for (const auto& [k, c] : kinds)
        std::printf(" %s %zu", k >= 0 && k < 6 ? names[k] : "?", c);
    std::printf("\n");

    // 3. For smoothness.py: the refreshes inside the frames (1 .. count - 3 frames), in frame units
    std::vector<size_t> scored;
    std::FILE* times = std::fopen((outDir + "/times.txt").c_str(), "w");
    if (!times)
    {
        std::fprintf(stderr, "cannot write %s/times.txt\n", outDir.c_str());
        return 1;
    }
    for (const RefreshEvent& r : g_refreshes)
    {
        const double frames = (static_cast<double>(r.t) - a) / b;
        if (r.plan.kind == SmoothMotion::Kind::None || frames < 1.0 || frames >= static_cast<double>(n) - 3.0)
            continue;
        scored.push_back(r.index);
        std::fprintf(times, "%.6f\n", frames);
    }
    std::fclose(times);
    Write(outDir + "/source.rgba", g_framePixels.data(), n * Pixels() * 4);
    WriteJson(outDir + "/source.json", n);
    const auto writeScored = [&](const std::string& name, const uint32_t* pictures) {
        std::vector<uint32_t> out(scored.size() * Pixels());
        for (size_t i = 0; i < scored.size(); i++)
            std::memcpy(out.data() + i * Pixels(), pictures + scored[i] * Pixels(), Pixels() * 4);
        Write(outDir + "/" + name + ".rgba", out.data(), out.size() * 4);
    };
    writeScored("live", g_refreshPixels.data());
    for (Mode mode : {Mode::Off, Mode::Blend})
    {
        std::vector<uint32_t> pictures;
        std::vector<SmoothMotion::Result> unused;
        Replay(mode, pictures, unused);
        writeScored(ModeName(mode), pictures.data());
    }

    // The perfect clock: frame k due at k periods, a refresh every mean refresh period from 3 ms, frames pushed two
    // ahead (POC 047's filters tool); smoothness.py's own times (--src-hz, --disp-hz)
    {
        const std::string ideal = outDir + "/ideal";
        SmoothMotion sm;
        sm.SetMode(g_mode);
        const int64_t framePeriod = std::llround(b);
        std::vector<uint32_t> out;
        std::vector<uint32_t> picture(Pixels(), 0);
        size_t pushed = 0;
        for (int64_t t = 3'000'000; t < static_cast<int64_t>(n - 3) * framePeriod;
             t += std::llround(period))
        {
            while (pushed < n && static_cast<int64_t>(pushed) * framePeriod <= t + 2 * framePeriod)
            {
                sm.Push(pushed + 1, static_cast<int64_t>(pushed) * framePeriod, FramePixels(pushed), g_width, g_height);
                pushed++;
            }
            if (!sm.Draw(sm.Plan(t), picture.data()))
                std::memcpy(picture.data(), FramePixels(0), Pixels() * 4);
            out.insert(out.end(), picture.begin(), picture.end());
        }
        Write(ideal + "/" + ModeName(g_mode) + ".rgba", out.data(), out.size() * 4);
        std::FILE* hz = std::fopen((ideal + "/rates.txt").c_str(), "w");
        std::fprintf(hz, "%.6f %.6f\n", 1e9 / b, 1e9 / period);
        std::fclose(hz);
    }
    return planDiffs || pictureDiffs ? 3 : 0;
}
