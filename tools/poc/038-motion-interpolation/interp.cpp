// POC 038: in-between pictures of emulated frames, three variants (README.md):
//   a-repeat  the newest frame
//   b-blend   (1 - w) A + w B
//   c-block   per 8x8 block a motion vector within +-R px, the warp by w, the blend where the warped sources disagree
//
// Modes:
//   interp eval <seq.rgba> <width> <height> <count>
//       hold-out triples: frames k and k+2 at w = 0.5 against frame k+1; one summary line per variant
//   interp render <seq.rgba> <width> <height> <count> <sourceHz> <displayHz> <out.rgba>
//       the pictures a display at displayHz shows, the three variants side by side (3 x width)
//
// Plain C++20, one thread, no SIMD (the charter's constraint).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
constexpr int BLOCK = 8;
constexpr int RANGE = 16;
constexpr int ACCEPT = 4;  // mismatching pixels of 64 still taken as a match
constexpr int BUDGET = 96; // full searches per consecutive pair

struct Frame
{
    int width = 0;
    int height = 0;
    const uint32_t* pixels = nullptr;
    uint32_t At(int x, int y) const
    {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return pixels[static_cast<size_t>(y) * width + x];
    }
};

/// A frame with PAD pixels of its edge repeated around it: a warp within RANGE (and the last partial block) reads
/// without clamping
constexpr int PAD = RANGE + BLOCK;
struct Padded
{
    int stride = 0;
    std::vector<uint32_t> data;
    void From(const Frame& f)
    {
        stride = f.width + 2 * PAD;
        data.resize(static_cast<size_t>(stride) * (f.height + 2 * PAD));
        for (int y = -PAD; y < f.height + PAD; y++)
            for (int x = -PAD; x < f.width + PAD; x++)
                data[static_cast<size_t>(y + PAD) * stride + x + PAD] = f.At(x, y);
    }
    const uint32_t* Row(int y) const { return data.data() + static_cast<size_t>(y + PAD) * stride + PAD; }
};

struct Vec
{
    int x = 0;
    int y = 0;
    bool operator==(const Vec& o) const { return x == o.x && y == o.y; }
};

uint32_t Mix(uint32_t a, uint32_t b, float w)
{
    uint32_t out = 0;
    for (int c = 0; c < 32; c += 8)
    {
        const float va = static_cast<float>((a >> c) & 0xFF);
        const float vb = static_cast<float>((b >> c) & 0xFF);
        out |= static_cast<uint32_t>(std::lround(va + (vb - va) * w)) << c;
    }
    return out;
}

/// An object moving by v from A to B is at p - round(w v) in A and at p + v - round(w v) in B when the output at w
/// shows it at p
struct Warp
{
    int ax, ay, bx, by;
    Warp(Vec v, float w)
    {
        const int sx = static_cast<int>(std::lround(w * v.x));
        const int sy = static_cast<int>(std::lround(w * v.y));
        ax = -sx;
        ay = -sy;
        bx = v.x - sx;
        by = v.y - sy;
    }
};

/// Pixels of the block at (bx0, by0) where the warped A and B disagree; stops above `limit`
int Mismatch(const Padded& a, const Padded& b, int x0, int y0, Vec v, float w, int limit)
{
    const Warp k(v, w);
    int bad = 0;
    for (int y = y0; y < y0 + BLOCK; y++)
    {
        const uint32_t* ra = a.Row(y + k.ay) + x0 + k.ax;
        const uint32_t* rb = b.Row(y + k.by) + x0 + k.bx;
        for (int x = 0; x < BLOCK; x++)
            if (ra[x] != rb[x] && ++bad > limit)
                return bad;
    }
    return bad;
}

std::vector<Vec> SearchOrder(int range)
{
    std::vector<Vec> order;
    for (int y = -range; y <= range; y++)
        for (int x = -range; x <= range; x++)
            order.push_back({x, y});
    std::stable_sort(order.begin(), order.end(),
                     [](Vec p, Vec q) { return p.x * p.x + p.y * p.y < q.x * q.x + q.y * q.y; });
    return order;
}

struct Stats
{
    int blocks = 0;
    int still = 0;     // the zero vector matched
    int moved = 0;     // a non-zero vector matched
    int fallback = 0;  // nothing matched within ACCEPT
};

/// c-block: one vector per output block, then per pixel the first of (block vector, zero, neighbours' vectors) on
/// which the warped A and B agree, else the unwarped blend
/// `range`: the full search's reach (RANGE for the hold-out's two-frame motion, half of it for consecutive frames);
/// `budget`: full searches per pair, the rest of the unmatched blocks fall back to the blend (full-screen effects)
void Interpolate(const Frame& a, const Frame& b, float w, uint32_t* out, Stats* stats, int range = RANGE,
                 int budget = 1 << 30)
{
    static std::vector<Vec> order;
    static int orderRange = -1;
    if (orderRange != range)
    {
        order = SearchOrder(range);
        orderRange = range;
    }
    static Padded pa, pb;
    pa.From(a);
    pb.From(b);
    const int bw = (a.width + BLOCK - 1) / BLOCK;
    const int bh = (a.height + BLOCK - 1) / BLOCK;
    std::vector<Vec> field(static_cast<size_t>(bw) * bh);
    std::vector<int> cost(field.size());
    // The vectors other blocks of this pair took: a scroll or a sprite moves many blocks by the same vector
    std::vector<std::pair<Vec, int>> votes;

    for (int j = 0; j < bh; j++)
        for (int i = 0; i < bw; i++)
        {
            const int x0 = i * BLOCK;
            const int y0 = j * BLOCK;
            Vec best{};
            int bestBad = Mismatch(pa, pb, x0, y0, best, w, BLOCK * BLOCK);
            // Candidates first (the left and upper neighbours' vectors), then the full search nearest first
            if (bestBad > 0)
            {
                // Only a vector within ACCEPT is used: a candidate stops at the first mismatch beyond it
                const auto tryVec = [&](Vec v) {
                    if (bestBad == 0)
                        return;
                    const int bad = Mismatch(pa, pb, x0, y0, v, w, std::min(bestBad - 1, ACCEPT));
                    if (bad < bestBad)
                    {
                        bestBad = bad;
                        best = v;
                    }
                };
                if (i > 0)
                    tryVec(field[j * bw + i - 1]);
                if (j > 0)
                    tryVec(field[(j - 1) * bw + i]);
                for (size_t c = 0; c < votes.size() && c < 4; c++)
                    tryVec(votes[c].first);
                if (bestBad > ACCEPT && budget > 0)
                {
                    budget--;
                    for (const Vec& v : order)
                        tryVec(v);
                }
            }
            field[j * bw + i] = best;
            cost[j * bw + i] = bestBad;
            stats->blocks++;
            if (bestBad > ACCEPT)
                stats->fallback++;
            else if (best == Vec{})
                stats->still++;
            else
            {
                stats->moved++;
                auto it = std::find_if(votes.begin(), votes.end(), [&](const auto& e) { return e.first == best; });
                if (it == votes.end())
                    votes.push_back({best, 1});
                else
                    it->second++;
                std::stable_sort(votes.begin(), votes.end(), [](const auto& p, const auto& q) { return p.second > q.second; });
            }
        }

    for (int j = 0; j < bh; j++)
        for (int i = 0; i < bw; i++)
        {
            Vec cands[6];
            int n = 0;
            const auto add = [&](Vec v, bool ok) {
                if (!ok)
                    return;
                for (int c = 0; c < n; c++)
                    if (cands[c] == v)
                        return;
                cands[n++] = v;
            };
            add(field[j * bw + i], cost[j * bw + i] <= ACCEPT);
            add(Vec{}, true);
            if (i > 0)
                add(field[j * bw + i - 1], cost[j * bw + i - 1] <= ACCEPT);
            if (i + 1 < bw)
                add(field[j * bw + i + 1], cost[j * bw + i + 1] <= ACCEPT);
            if (j > 0)
                add(field[(j - 1) * bw + i], cost[(j - 1) * bw + i] <= ACCEPT);
            if (j + 1 < bh)
                add(field[(j + 1) * bw + i], cost[(j + 1) * bw + i] <= ACCEPT);

            for (int y = j * BLOCK; y < std::min((j + 1) * BLOCK, a.height); y++)
                for (int x = i * BLOCK; x < std::min((i + 1) * BLOCK, a.width); x++)
                {
                    // Of the candidates on which the warped A and B agree at this pixel, the one that agrees on most of
                    // its 3x3 neighbourhood: a coincidental match of one pixel (a static letter under a neighbour's
                    // vector) loses to the vector that explains the area
                    uint32_t pixel = 0;
                    bool found = false;
                    int bestScore = 0;
                    for (int c = 0; c < n; c++)
                    {
                        const Warp k(cands[c], w);
                        const uint32_t va = pa.Row(y + k.ay)[x + k.ax];
                        if (va != pb.Row(y + k.by)[x + k.bx])
                            continue;
                        int score = 0;
                        for (int dy = -1; dy <= 1; dy++)
                        {
                            const uint32_t* ra = pa.Row(y + dy + k.ay) + x + k.ax;
                            const uint32_t* rb = pb.Row(y + dy + k.by) + x + k.bx;
                            for (int dx = -1; dx <= 1; dx++)
                                score += ra[dx] == rb[dx];
                        }
                        if (score > bestScore)
                        {
                            bestScore = score;
                            pixel = va;
                            found = true;
                        }
                    }
                    out[static_cast<size_t>(y) * a.width + x] = found ? pixel : Mix(a.At(x, y), b.At(x, y), w);
                }
        }
}

void Blend(const Frame& a, const Frame& b, float w, uint32_t* out)
{
    const size_t n = static_cast<size_t>(a.width) * a.height;
    for (size_t p = 0; p < n; p++)
        out[p] = a.pixels[p] == b.pixels[p] ? a.pixels[p] : Mix(a.pixels[p], b.pixels[p], w);
}

struct Score
{
    double absError = 0.0;    // sum over channels of |out - truth|
    uint64_t pixels = 0;      // pixels compared (the changed ones)
    uint64_t exact = 0;       // of these, exactly right
    uint64_t triples = 0;
    uint64_t worseThanBlend = 0;
};

/// Only the pixels that change across the triple count: a static screen says nothing about motion
void ScoreFrame(const uint32_t* out, const Frame& a, const Frame& truth, const Frame& c, double* err, uint64_t* px,
                uint64_t* exact)
{
    const size_t n = static_cast<size_t>(a.width) * a.height;
    for (size_t p = 0; p < n; p++)
    {
        if (a.pixels[p] == truth.pixels[p] && truth.pixels[p] == c.pixels[p])
            continue;
        (*px)++;
        if (out[p] == truth.pixels[p])
            (*exact)++;
        for (int s = 0; s < 24; s += 8)
            *err += std::abs(static_cast<int>((out[p] >> s) & 0xFF) - static_cast<int>((truth.pixels[p] >> s) & 0xFF));
    }
}

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

int Eval(const char* path, int width, int height, int count)
{
    const size_t size = static_cast<size_t>(width) * height;
    const std::vector<uint32_t> seq = Load(path, size * count);
    const auto frame = [&](int k) { return Frame{width, height, seq.data() + size * k}; };
    Score scores[3];
    Stats stats;
    std::vector<uint32_t> out(size);
    double blockNs = 0.0;
    double worstBlockNs = 0.0;
    uint64_t moving = 0;
    // The real use: consecutive frames (half the hold-out's motion), every pair that differs
    std::vector<double> pairNs;
    {
        Stats ignored;
        for (int k = 0; k + 1 < count; k++)
        {
            const Frame a = frame(k), b = frame(k + 1);
            if (std::memcmp(a.pixels, b.pixels, size * 4) == 0)
                continue;
            const auto t0 = std::chrono::steady_clock::now();
            Interpolate(a, b, 0.5f, out.data(), &ignored, RANGE / 2, BUDGET);
            pairNs.push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count());
        }
        std::sort(pairNs.begin(), pairNs.end());
    }
    for (int k = 0; k + 2 < count; k++)
    {
        const Frame a = frame(k), truth = frame(k + 1), c = frame(k + 2);
        // Motion on both steps: a game that draws every few frames repeats pictures, and a repeat has no
        // in-between to recover (its own cadence is the game's, not the display's)
        if (std::memcmp(a.pixels, truth.pixels, size * 4) == 0 || std::memcmp(truth.pixels, c.pixels, size * 4) == 0)
            continue;
        moving++;
        double err[3] = {0, 0, 0};
        uint64_t px[3] = {0, 0, 0};
        uint64_t exact[3] = {0, 0, 0};

        std::memcpy(out.data(), a.pixels, size * 4);  // a-repeat: the newest frame due at w = 0.5 is A
        ScoreFrame(out.data(), a, truth, c, &err[0], &px[0], &exact[0]);
        Blend(a, c, 0.5f, out.data());
        ScoreFrame(out.data(), a, truth, c, &err[1], &px[1], &exact[1]);
        const auto t0 = std::chrono::steady_clock::now();
        Interpolate(a, c, 0.5f, out.data(), &stats);
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
        blockNs += ns;
        worstBlockNs = std::max(worstBlockNs, ns);
        ScoreFrame(out.data(), a, truth, c, &err[2], &px[2], &exact[2]);

        for (int v = 0; v < 3; v++)
        {
            scores[v].absError += err[v];
            scores[v].pixels += px[v];
            scores[v].exact += exact[v];
            scores[v].triples++;
        }
        if (err[2] > err[1] * 1.05 + 1.0)
            scores[2].worseThanBlend++;
    }

    const char* names[3] = {"a-repeat", "b-blend", "c-block"};
    for (int v = 0; v < 3; v++)
    {
        const Score& s = scores[v];
        const double mae = s.pixels ? s.absError / (3.0 * s.pixels) : 0.0;
        const double exactPct = s.pixels ? 100.0 * s.exact / s.pixels : 100.0;
        std::printf("%-9s triples %4llu  changed px %9llu  MAE %6.2f  exact %5.1f%%", names[v],
                    static_cast<unsigned long long>(s.triples), static_cast<unsigned long long>(s.pixels), mae,
                    exactPct);
        if (v == 2)
            std::printf("  worse-than-blend %llu  ms/pair avg %.2f max %.2f  blocks still %d moved %d fallback %d",
                        static_cast<unsigned long long>(s.worseThanBlend), moving ? blockNs / moving / 1e6 : 0.0,
                        worstBlockNs / 1e6, stats.still, stats.moved, stats.fallback);
        std::printf("\n");
    }
    if (!pairNs.empty())
        std::printf("consecutive pairs %zu  ms p50 %.2f p95 %.2f max %.2f\n", pairNs.size(),
                    pairNs[pairNs.size() / 2] / 1e6, pairNs[pairNs.size() * 95 / 100] / 1e6, pairNs.back() / 1e6);
    return 0;
}

int Render(const char* path, int width, int height, int count, double sourceHz, double displayHz, const char* outPath)
{
    const size_t size = static_cast<size_t>(width) * height;
    const std::vector<uint32_t> seq = Load(path, size * count);
    const auto frame = [&](int k) { return Frame{width, height, seq.data() + size * k}; };
    FILE* f = std::fopen(outPath, "wb");
    if (!f)
        return 1;
    std::vector<uint32_t> panels[3] = {std::vector<uint32_t>(size), std::vector<uint32_t>(size),
                                       std::vector<uint32_t>(size)};
    std::vector<uint32_t> row(static_cast<size_t>(width) * 3);
    Stats stats;
    int refreshes = 0;
    for (int n = 0;; n++)
    {
        const double t = n / displayHz * sourceHz;  // in source frames
        const int k = static_cast<int>(std::floor(t));
        if (k + 1 >= count)
            break;
        const float w = static_cast<float>(t - k);
        const Frame a = frame(k), b = frame(k + 1);
        std::memcpy(panels[0].data(), a.pixels, size * 4);
        Blend(a, b, w, panels[1].data());
        Interpolate(a, b, w, panels[2].data(), &stats, RANGE / 2, BUDGET);
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
    if (argc >= 9 && std::strcmp(argv[1], "render") == 0)
        return Render(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]), std::atof(argv[6]),
                      std::atof(argv[7]), argv[8]);
    std::fprintf(stderr,
                 "usage: interp eval <seq.rgba> <w> <h> <count>\n"
                 "       interp render <seq.rgba> <w> <h> <count> <sourceHz> <displayHz> <out.rgba>\n");
    return 2;
}
