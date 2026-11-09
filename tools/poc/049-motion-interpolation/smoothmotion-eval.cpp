// smoothmotion-eval: the CPU reference of smooth motion (core/src/emulator/video/smoothmotion.h) on captured frame
// sequences (tools/smoothmotion/capture.py: RGBA frames back to back), the measurements of POC 049 on the production
// code.
//
//   smoothmotion-eval eval <seq.rgba> <width> <height> <count>
//       hold-out triples: frames k and k+2 at w = 1/2 against frame k+1 (blend and interpolate), and the cost of
//       consecutive pairs (estimate + compose, the real use)
//   smoothmotion-eval cadence <seq.rgba> <width> <height> <count> [x y w h]
//       the game's pictures (frames whose play area changed, a burst on consecutive frames one picture), their period,
//       the changing area, and the cadence modes on the pictures: interpolation (pictures i-1 and i+1 at the middle
//       against picture i) and carry (i-1 -> i carried one picture on, against i+1)
//   smoothmotion-eval render <seq.rgba> <width> <height> <count> <sourceHz> <displayHz> <out.rgba>
//       what a fixed display shows: repeat | blend | interpolate side by side (3 x width), one picture per refresh
//
// Build (no CMake target: a measuring tool, as the POC):
//   CXXFLAGS=-O3 tools/verification/smoothmotion/compile-tool.sh tools/poc/049-motion-interpolation/smoothmotion-eval.cpp \
//       scratch/2026-10-26-poc-046/smoothmotion-eval
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "emulator/video/smoothmotion.h"

using namespace smoothmotion;

namespace
{
std::vector<uint32_t> Load(const char* path, size_t pixels)
{
    std::vector<uint32_t> data(pixels);
    FILE* f = std::fopen(path, "rb");
    if (!f || std::fread(data.data(), 4, pixels, f) != pixels)
    {
        std::fprintf(stderr, "cannot read %zu pixels from %s\n", pixels, path);
        std::exit(1);
    }
    std::fclose(f);
    return data;
}

struct Score
{
    double absError = 0.0;
    uint64_t pixels = 0;
    uint64_t exact = 0;
    void Add(const uint32_t* out, const uint32_t* a, const uint32_t* truth, const uint32_t* c, size_t n)
    {
        for (size_t p = 0; p < n; p++)
        {
            if (a[p] == truth[p] && truth[p] == c[p])
                continue;
            pixels++;
            exact += out[p] == truth[p];
            for (int s = 0; s < 24; s += 8)
                absError += std::abs(static_cast<int>((out[p] >> s) & 0xFF) - static_cast<int>((truth[p] >> s) & 0xFF));
        }
    }
    void Print(const char* name) const
    {
        std::printf("%-16s changed px %9llu  MAE %6.2f  exact %5.1f%%\n", name, static_cast<unsigned long long>(pixels),
                    pixels ? absError / (3.0 * pixels) : 0.0, pixels ? 100.0 * exact / pixels : 100.0);
    }
};

double Ms(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void PrintTimes(const char* name, std::vector<double> ms)
{
    if (ms.empty())
        return;
    std::sort(ms.begin(), ms.end());
    std::printf("%s %zu  ms p50 %.2f p95 %.2f max %.2f\n", name, ms.size(), ms[ms.size() / 2], ms[ms.size() * 95 / 100],
                ms.back());
}

int Eval(const char* path, int width, int height, int count)
{
    const size_t size = static_cast<size_t>(width) * height;
    const std::vector<uint32_t> seq = Load(path, size * count);
    const auto frame = [&](int k) { return Picture{width, height, seq.data() + size * k}; };
    std::vector<uint32_t> out(size);
    Score repeat, blend, interp;
    uint64_t triples = 0;
    MotionField field;
    for (int k = 0; k + 2 < count; k++)
    {
        const Picture a = frame(k), truth = frame(k + 1), c = frame(k + 2);
        if (std::memcmp(a.pixels, truth.pixels, size * 4) == 0 || std::memcmp(truth.pixels, c.pixels, size * 4) == 0)
            continue;
        triples++;
        repeat.Add(a.pixels, a.pixels, truth.pixels, c.pixels, size);
        Blend(a, c, kOne / 2, out.data());
        blend.Add(out.data(), a.pixels, truth.pixels, c.pixels, size);
        field.Estimate(a, c, MotionField::kRangePictures);
        field.Compose(a, c, kOne / 2, out.data());
        interp.Add(out.data(), a.pixels, truth.pixels, c.pixels, size);
    }
    std::printf("triples %llu\n", static_cast<unsigned long long>(triples));
    repeat.Print("repeat");
    blend.Print("blend");
    interp.Print("interpolate");

    std::vector<double> estimate, compose;
    MotionField::Stats stats{};
    for (int k = 0; k + 1 < count; k++)
    {
        const Picture a = frame(k), b = frame(k + 1);
        if (std::memcmp(a.pixels, b.pixels, size * 4) == 0)
            continue;
        auto t0 = std::chrono::steady_clock::now();
        field.Estimate(a, b, MotionField::kRangeFrames);
        estimate.push_back(Ms(t0));
        t0 = std::chrono::steady_clock::now();
        field.Compose(a, b, 100, out.data());
        compose.push_back(Ms(t0));
        stats.blocks += field.GetStats().blocks;
        stats.still += field.GetStats().still;
        stats.moved += field.GetStats().moved;
        stats.unmatched += field.GetStats().unmatched;
    }
    PrintTimes("estimate, consecutive pairs", estimate);
    PrintTimes("compose, per refresh", compose);
    std::printf("blocks still %d moved %d unmatched %d\n", stats.still, stats.moved, stats.unmatched);
    return 0;
}

struct Cadence
{
    std::vector<int> keys;  // frame index of each picture (a burst's last frame)
    double period = 1.0;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // the bounding box of the changes
};

/// As POC 049: a burst of changes on consecutive frames is one picture unless the content changes every frame
Cadence DetectCadence(const std::vector<uint32_t>& seq, int width, int height, int count, const Rect& window)
{
    const size_t size = static_cast<size_t>(width) * height;
    Cadence c;
    c.x0 = width;
    c.y0 = height;
    const auto inWindow = [&](int x, int y) {
        return window.Empty() ||
               (x >= window.x && y >= window.y && x < window.x + window.width && y < window.y + window.height);
    };
    std::vector<int> changes;
    for (int k = 1; k < count; k++)
    {
        const uint32_t* a = seq.data() + size * (k - 1);
        const uint32_t* b = seq.data() + size * k;
        bool changed = false;
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
                if (inWindow(x, y) && a[static_cast<size_t>(y) * width + x] != b[static_cast<size_t>(y) * width + x])
                {
                    changed = true;
                    c.x0 = std::min(c.x0, x);
                    c.y0 = std::min(c.y0, y);
                    c.x1 = std::max(c.x1, x + 1);
                    c.y1 = std::max(c.y1, y + 1);
                }
        if (changed)
            changes.push_back(k);
    }
    if (changes.empty())
        return c;
    size_t single = 0;
    for (size_t i = 1; i < changes.size(); i++)
        single += changes[i] - changes[i - 1] == 1;
    const bool everyFrame = single * 2 > changes.size();
    for (size_t i = 0; i < changes.size(); i++)
        if (everyFrame || i + 1 >= changes.size() || changes[i + 1] - changes[i] != 1)
            c.keys.push_back(changes[i]);
    std::vector<int> gaps;
    for (size_t i = 1; i < c.keys.size(); i++)
        gaps.push_back(c.keys[i] - c.keys[i - 1]);
    if (!gaps.empty())
    {
        std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
        c.period = gaps[gaps.size() / 2];
    }
    return c;
}

int CadenceEval(const char* path, int width, int height, int count, const Rect& window)
{
    const size_t size = static_cast<size_t>(width) * height;
    const std::vector<uint32_t> seq = Load(path, size * count);
    const Cadence cad = DetectCadence(seq, width, height, count, window);
    std::printf("pictures %zu, period %.0f frames, changes in x %d y %d w %d h %d\n", cad.keys.size(), cad.period,
                cad.x0, cad.y0, std::max(0, cad.x1 - cad.x0), std::max(0, cad.y1 - cad.y0));
    const auto key = [&](size_t i) { return Picture{width, height, seq.data() + size * cad.keys[i]}; };
    std::vector<uint32_t> out(size);
    Score blend, interp, repeat, carry;
    MotionField field;
    std::vector<double> ms;
    for (size_t i = 1; i + 1 < cad.keys.size(); i++)
    {
        const Picture prev = key(i - 1), cur = key(i), next = key(i + 1);
        Blend(prev, next, kOne / 2, out.data());
        blend.Add(out.data(), prev.pixels, cur.pixels, next.pixels, size);
        field.Estimate(prev, next, MotionField::kRangePictures);
        field.Compose(prev, next, kOne / 2, out.data());
        interp.Add(out.data(), prev.pixels, cur.pixels, next.pixels, size);
        repeat.Add(cur.pixels, prev.pixels, next.pixels, cur.pixels, size);
        const auto t0 = std::chrono::steady_clock::now();
        field.Estimate(prev, cur, MotionField::kRangePictures);
        std::memcpy(out.data(), cur.pixels, size * 4);
        field.Compose(prev, cur, 2 * kOne, out.data());
        ms.push_back(Ms(t0));
        carry.Add(out.data(), prev.pixels, next.pixels, cur.pixels, size);
    }
    blend.Print("interp blend");
    interp.Print("interp motion");
    repeat.Print("extrap repeat");
    carry.Print("extrap carry");
    PrintTimes("carry (estimate + compose)", ms);
    return 0;
}

int Render(const char* path, int width, int height, int count, double sourceHz, double displayHz, const char* outPath)
{
    const size_t size = static_cast<size_t>(width) * height;
    const std::vector<uint32_t> seq = Load(path, size * count);
    FILE* f = std::fopen(outPath, "wb");
    if (!f)
        return 1;
    SmoothMotion engines[3];
    engines[1].SetMode(Mode::Blend);
    engines[2].SetMode(Mode::Interpolate);
    const int64_t frameNs = static_cast<int64_t>(1e9 / sourceHz);
    std::vector<std::vector<uint32_t>> panels(3, std::vector<uint32_t>(size));
    std::vector<uint32_t> row(static_cast<size_t>(width) * 3);
    int pushed = 0;
    int refreshes = 0;
    for (int n = 0;; n++)
    {
        const int64_t t = static_cast<int64_t>(n * 1e9 / displayHz);
        // The frames the presenter has at t: up to the one after the frame due
        while (pushed < count && static_cast<int64_t>(pushed) * frameNs <= t + frameNs)
        {
            for (SmoothMotion& e : engines)
                e.Push(static_cast<uint64_t>(pushed) + 1, pushed * frameNs, seq.data() + size * pushed, width, height);
            pushed++;
        }
        if (t >= (count - 1) * frameNs)
            break;
        for (int v = 0; v < 3; v++)
            engines[v].Render(t, panels[v].data());
        for (int y = 0; y < height; y++)
        {
            for (int v = 0; v < 3; v++)
                std::memcpy(&row[static_cast<size_t>(v) * width], &panels[v][static_cast<size_t>(y) * width],
                            static_cast<size_t>(width) * 4);
            std::fwrite(row.data(), 4, row.size(), f);
        }
        refreshes++;
    }
    std::fclose(f);
    std::printf("%d refreshes at %.3f Hz, %dx%d\n", refreshes, displayHz, width * 3, height);
    return 0;
}
}  // namespace

int main(int argc, char** argv)
{
    if (argc >= 6 && std::strcmp(argv[1], "eval") == 0)
        return Eval(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]));
    if (argc >= 6 && std::strcmp(argv[1], "cadence") == 0)
    {
        Rect window;
        if (argc >= 10)
            window = {std::atoi(argv[6]), std::atoi(argv[7]), std::atoi(argv[8]), std::atoi(argv[9])};
        return CadenceEval(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]), window);
    }
    if (argc >= 9 && std::strcmp(argv[1], "render") == 0)
        return Render(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]), std::atof(argv[6]),
                      std::atof(argv[7]), argv[8]);
    std::fprintf(stderr,
                 "usage: smoothmotion-eval eval <seq.rgba> <w> <h> <count>\n"
                 "       smoothmotion-eval cadence <seq.rgba> <w> <h> <count> [x y w h]\n"
                 "       smoothmotion-eval render <seq.rgba> <w> <h> <count> <sourceHz> <displayHz> <out.rgba>\n");
    return 2;
}
