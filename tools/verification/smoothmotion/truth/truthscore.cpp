// What the eye sees against the truth: renders of a POC 039 scene (tools/poc/039-smoothing-filters/scenes.py), whose
// motion is known at any time, against the scene itself moving continuously.
//
// The eye integrates the light of the last 20 ms (a box, 10 subsamples 2 ms apart) and either follows an element at
// its seen velocity (pursuit) or rests (fixation). The reference is the truth at every subsample (truthdump.py: 500 Hz,
// whole pixels), so a sample-and-hold display scores its hold too: the truth itself refreshed at 60 Hz is the best a
// 60 Hz display can do, the source at its own rate (`<scene>.rgba@50@0`) the native presentation.
//
// Per sample (25 per second): per moving element, the eye following it, the squared difference of the two retinal
// images over the element's pixels as a share of the reference's contrast round it (variance over the element's box,
// at least 64 on 0..255 luma); per 16 x 16 tile the same with the eye at rest. A render may run late (a look-ahead): the reference's time is
// shifted by the lag that fits best, a 2 ms grid over -4..+40 ms, reported.
//
//   pursuit   mean over the moving elements and samples (%): judder, hold blur, ghosts, parts left behind
//   p95       95th percentile of the per-sample pursuit error (%)
//   fixation  mean over the tiles and samples, the eye at rest (%): flicker, shimmer, pixels that should not change
//
// TRUTH_ELEMENTS=1 adds the pursuit per element (scene order). TRUTH_REF=<render.rgba[@hz[@start]]> takes that render
// as the pursuit reference instead of the truth (fixation stays against the truth, whose mean over two frames is what
// the eye sees of a half-rate flicker): the truth refreshed at 60 Hz (truth60.py), the best a 60 Hz display shows,
// so the score is what a render adds to the hold every 60 Hz display has (judder, ghosts, flicker), free of the
// sub-pixel phase of the hold that dominates against the continuous truth.
//
//   truthscore <dir> <scene> <render.rgba[@hz[@start]]>...    (hz 60, start 0.003 s by default)
//
// Build: clang++ -std=c++20 -O2 -Wall -Wextra truthscore.cpp -o truthscore
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace
{
constexpr int kW = 352, kH = 288, kN = kW * kH;
constexpr int kTile = 16;
constexpr int kSub = 10;
constexpr double kTruthHz = 500.0, kTruthStart = 0.001;
constexpr double kSourceHz = 50.0;

float Luma(const uint8_t* rgb)
{
    return 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
}

std::vector<uint8_t> ReadAll(const std::string& path)
{
    std::vector<uint8_t> data;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return data;
    std::fseek(f, 0, SEEK_END);
    data.resize(static_cast<size_t>(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    if (std::fread(data.data(), 1, data.size(), f) != data.size())
        data.clear();
    std::fclose(f);
    return data;
}

struct Truth
{
    int count = 0;
    std::vector<float> luma;
    std::vector<int8_t> ids;
    int elements = 0;
    std::vector<std::vector<std::pair<float, float>>> eyes;  ///< per source frame, per element (vy, vx); NaN: none
    std::vector<std::vector<int>> boxes;                      ///< per truth frame, per element: y0 x0 y1 x1

    const float* At(double tau) const
    {
        const int j = std::clamp(static_cast<int>(std::lround((tau - kTruthStart) * kTruthHz)), 0, count - 1);
        return &luma[static_cast<size_t>(j) * kN];
    }
    const int8_t* IdsAt(double tau) const
    {
        const int j = std::clamp(static_cast<int>(std::lround((tau - kTruthStart) * kTruthHz)), 0, count - 1);
        return &ids[static_cast<size_t>(j) * kN];
    }
};

struct Render
{
    std::string name;
    double hz = 60.0, start = 0.003;
    int count = 0;
    std::vector<float> luma;
    const float* At(double t) const
    {
        const int i = std::clamp(static_cast<int>(std::floor((t - start) * hz)), 0, count - 1);
        return &luma[static_cast<size_t>(i) * kN];
    }
};

float Sample(const float* img, float y, float x)
{
    y = std::clamp(y, 0.0f, static_cast<float>(kH - 1));
    x = std::clamp(x, 0.0f, static_cast<float>(kW - 1));
    const int y0 = static_cast<int>(y), x0 = static_cast<int>(x);
    const int y1 = std::min(y0 + 1, kH - 1), x1 = std::min(x0 + 1, kW - 1);
    const float fy = y - y0, fx = x - x0;
    const float a = img[y0 * kW + x0] * (1 - fx) + img[y0 * kW + x1] * fx;
    const float b = img[y1 * kW + x0] * (1 - fx) + img[y1 * kW + x1] * fx;
    return a * (1 - fy) + b * fy;
}

/// The light over [t - 20 ms, t] in the eye's frame at t (moving at v px/s), over the box y0..y1, x0..x1
template <class Display>
void Retina(const Display& d, double t, float vy, float vx, int y0, int x0, int y1, int x1, std::vector<float>& out)
{
    out.assign(static_cast<size_t>(y1 - y0) * (x1 - x0), 0.0f);
    for (int s = 0; s < kSub; s++)
    {
        const double tau = t - 0.019 + 0.002 * s;
        const float dt = static_cast<float>(tau - t);
        const float* img = d.At(tau);
        size_t o = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                out[o++] += (vy == 0 && vx == 0 ? img[y * kW + x] : Sample(img, y + vy * dt, x + vx * dt)) / kSub;
    }
}

struct TruthDisplay
{
    const Truth* truth;
    double lag;
    const Render* display = nullptr;  ///< TRUTH_REF: the reference is this render (the truth refreshed at 60 Hz)
    const float* At(double tau) const { return display ? display->At(tau - lag) : truth->At(tau - lag); }
};

const Render* gReference = nullptr;

struct Result
{
    double pursuit = 0, p95 = 0, fixation = 0;
    std::vector<double> elements;  ///< per element: its mean pursuit (%), -1 when never followed
};

Result Score(const Truth& truth, const Render& ren, double lag, double rate)
{
    const TruthDisplay ref{&truth, lag, gReference};
    const double end = std::min(truth.count / kTruthHz, ren.start + ren.count / ren.hz) - std::abs(lag) - 0.002;
    std::vector<double> pursuit, fixation;
    std::vector<double> perSum(truth.elements, 0.0);
    std::vector<int> perCount(truth.elements, 0);
    std::vector<float> rr, rt;
    const auto error = [&](const int8_t* mask, int id, int y0, int x0, int y1, int x1) {
        double e = 0, s = 0, s2 = 0;
        int n = 0;
        size_t o = 0;
        int all = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++, o++)
            {
                // The contrast over the whole box (the element and what lies round it: a one-colour element's own
                // pixels have none), the error over the element's pixels
                s += rr[o];
                s2 += rr[o] * rr[o];
                all++;
                if (mask && mask[y * kW + x] != id)
                    continue;
                const double d = rr[o] - rt[o];
                e += d * d;
                n++;
            }
        if (!n)
            return -1.0;
        const double var = std::max(s2 / all - (s / all) * (s / all), 64.0);
        return e / (var * n);
    };
    // Sample times on the 2 ms grid, so every subsample falls on a truth frame
    const int step = std::max(1, static_cast<int>(std::lround(500.0 / rate)));
    for (int ms2 = static_cast<int>(std::ceil((0.04 + std::abs(lag)) * 500)); ms2 / 500.0 < end; ms2 += step)
    {
        const double t = ms2 / 500.0;
        const int k = std::clamp(static_cast<int>((t - lag) * kSourceHz), 0, static_cast<int>(truth.eyes.size()) - 1);
        const int8_t* ids = truth.IdsAt(t - lag);
        double sum = 0;
        int count = 0;
        for (int e = 0; e < truth.elements; e++)
        {
            const auto [vy, vx] = truth.eyes[k][e];
            if (std::isnan(vy))
                continue;
            int y0 = kH, x0 = kW, y1 = 0, x1 = 0;
            for (int y = 0; y < kH; y++)
                for (int x = 0; x < kW; x++)
                    if (ids[y * kW + x] == e + 1)
                    {
                        y0 = std::min(y0, y);
                        x0 = std::min(x0, x);
                        y1 = std::max(y1, y + 1);
                        x1 = std::max(x1, x + 1);
                    }
            if (y1 <= y0)
                continue;
            Retina(ref, t, vy, vx, y0, x0, y1, x1, rr);
            Retina(ren, t, vy, vx, y0, x0, y1, x1, rt);
            const double q = error(ids, e + 1, y0, x0, y1, x1);
            if (q >= 0)
            {
                sum += q;
                count++;
                perSum[e] += q;
                perCount[e]++;
            }
        }
        if (count)
            pursuit.push_back(sum / count);
        const TruthDisplay still{&truth, lag};  // fixation: against the truth itself (a half-rate flicker is its mean)
        Retina(still, t, 0, 0, 0, 0, kH, kW, rr);
        Retina(ren, t, 0, 0, 0, 0, kH, kW, rt);
        double fx = 0;
        int tiles = 0;
        for (int j = 0; j < kH; j += kTile)
            for (int i = 0; i < kW; i += kTile)
            {
                // error() reads rr / rt as the box, so copy the tile out
                std::vector<float> a, b;
                for (int y = j; y < j + kTile; y++)
                    for (int x = i; x < i + kTile; x++)
                    {
                        a.push_back(rr[static_cast<size_t>(y) * kW + x]);
                        b.push_back(rt[static_cast<size_t>(y) * kW + x]);
                    }
                std::swap(a, rr);
                std::swap(b, rt);
                fx += error(nullptr, 0, 0, 0, kTile, kTile);
                std::swap(a, rr);
                std::swap(b, rt);
                tiles++;
            }
        fixation.push_back(fx / tiles);
    }
    Result r;
    const auto mean = [](const std::vector<double>& v) {
        double s = 0;
        for (double x : v)
            s += x;
        return v.empty() ? 0.0 : s / v.size();
    };
    r.pursuit = 100 * mean(pursuit);
    for (int e = 0; e < truth.elements; e++)
        r.elements.push_back(perCount[e] ? 100 * perSum[e] / perCount[e] : -1.0);
    r.fixation = 100 * mean(fixation);
    if (!pursuit.empty())
    {
        std::sort(pursuit.begin(), pursuit.end());
        r.p95 = 100 * pursuit[std::min(pursuit.size() - 1, pursuit.size() * 95 / 100)];
    }
    return r;
}

bool LoadTruth(const std::string& dir, const std::string& scene, Truth& truth)
{
    const std::vector<uint8_t> raw = ReadAll(dir + "/" + scene + ".truth");
    const size_t frame = static_cast<size_t>(kN) * 4;
    if (raw.empty() || raw.size() % frame)
        return false;
    truth.count = static_cast<int>(raw.size() / frame);
    truth.luma.resize(static_cast<size_t>(truth.count) * kN);
    truth.ids.resize(truth.luma.size());
    for (int j = 0; j < truth.count; j++)
    {
        const uint8_t* f = &raw[j * frame];
        for (int p = 0; p < kN; p++)
        {
            truth.luma[static_cast<size_t>(j) * kN + p] = Luma(f + p * 3);
            truth.ids[static_cast<size_t>(j) * kN + p] = static_cast<int8_t>(f[kN * 3 + p]);
        }
    }
    FILE* f = std::fopen((dir + "/" + scene + ".eyes").c_str(), "r");
    if (!f || std::fscanf(f, "%d", &truth.elements) != 1)
        return false;
    char token[64];
    while (true)
    {
        std::vector<std::pair<float, float>> row;
        for (int e = 0; e < truth.elements; e++)
        {
            if (std::fscanf(f, "%63s", token) != 1)
            {
                std::fclose(f);
                return !truth.eyes.empty();
            }
            float vy = NAN, vx = NAN;
            if (token[0] != '-' || token[1] != 0)
                std::sscanf(token, "%f,%f", &vy, &vx);
            row.emplace_back(vy, vx);
        }
        truth.eyes.push_back(row);
    }
}

bool LoadRender(const std::string& spec, Render& r)
{
    std::string path = spec;
    const size_t at = spec.find('@');
    if (at != std::string::npos)
    {
        path = spec.substr(0, at);
        const std::string rest = spec.substr(at + 1);
        const size_t at2 = rest.find('@');
        r.hz = std::atof(rest.substr(0, at2).c_str());
        if (at2 != std::string::npos)
            r.start = std::atof(rest.substr(at2 + 1).c_str());
    }
    const size_t slash = path.find_last_of('/');
    const std::string parent = slash == std::string::npos ? "" : path.substr(0, slash);
    r.name = parent.substr(parent.find_last_of('/') + 1) + "/" + path.substr(slash + 1);
    r.name = r.name.substr(0, r.name.find_last_of('.'));
    const std::vector<uint8_t> raw = ReadAll(path);
    const size_t frame = static_cast<size_t>(kN) * 4;
    if (raw.empty() || raw.size() % frame)
        return false;
    r.count = static_cast<int>(raw.size() / frame);
    r.luma.resize(static_cast<size_t>(r.count) * kN);
    for (size_t p = 0; p < r.luma.size(); p++)
        r.luma[p] = Luma(&raw[p * 4]);
    return true;
}
}  // namespace

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "truthscore <dir> <scene> <render.rgba[@hz[@start]]>...\n");
        return 2;
    }
    Truth truth;
    if (!LoadTruth(argv[1], argv[2], truth))
    {
        std::fprintf(stderr, "cannot read the truth of %s in %s\n", argv[2], argv[1]);
        return 1;
    }
    Render reference;
    if (const char* path = std::getenv("TRUTH_REF"))
    {
        if (!LoadRender(path, reference))
        {
            std::fprintf(stderr, "cannot read the reference %s\n", path);
            return 1;
        }
        gReference = &reference;
    }
    const int n = argc - 3;
    std::vector<Result> results(n);
    std::vector<int> lags(n, 0);
    std::vector<std::string> names(n);
    std::vector<std::thread> threads;
    for (int i = 0; i < n; i++)
        threads.emplace_back([&, i] {
            Render ren;
            if (!LoadRender(argv[3 + i], ren))
            {
                names[i] = std::string(argv[3 + i]) + " (unreadable)";
                return;
            }
            names[i] = ren.name;
            double best = 1e30;
            for (int ms = -4; ms <= 40; ms += 2)
            {
                const Result q = Score(truth, ren, ms / 1000.0, 5.0);
                if (q.pursuit + q.fixation < best)
                {
                    best = q.pursuit + q.fixation;
                    lags[i] = ms;
                }
            }
            results[i] = Score(truth, ren, lags[i] / 1000.0, 25.0);
        });
    for (auto& t : threads)
        t.join();
    std::printf("%-28s %10s %8s %11s %7s\n", "render", "pursuit %", "p95 %", "fixation %", "lag ms");
    for (int i = 0; i < n; i++)
        std::printf("%-28s %10.2f %8.2f %11.2f %7d\n", (std::string(argv[2]) + "/" + names[i]).c_str(),
                    results[i].pursuit, results[i].p95, results[i].fixation, lags[i]);
    if (std::getenv("TRUTH_ELEMENTS"))
        for (int i = 0; i < n; i++)
        {
            std::printf("%-28s", names[i].c_str());
            for (double e : results[i].elements)
                std::printf(e < 0 ? "      -" : " %6.0f", e);
            std::printf("\n");
        }
    return 0;
}
