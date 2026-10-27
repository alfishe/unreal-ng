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
constexpr int PAD = 2 * RANGE + BLOCK;  // extrapolation warps by up to twice the vector
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
/// `w` > 1 extrapolates past B along the motion from A to B; where nothing agrees, the output is then B itself
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
                        // Extrapolating, a moving vector draws only what moved: one of its sources must change from
                        // A to B. A static area beyond a scrolling window (the border, a frame) agrees with itself
                        // under any vector and would be pulled into the window, where the real content is not
                        // known yet. (Interpolating, B holds that content: the rule would only cost quality)
                        if (w > 1.0f && !(cands[c] == Vec{}))
                        {
                            const int sax = x + k.ax, say = y + k.ay, sbx = x + k.bx, sby = y + k.by;
                            if (pa.Row(say)[sax] == pb.Row(say)[sax] && pa.Row(sby)[sbx] == pb.Row(sby)[sbx])
                                continue;
                        }
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
                    out[static_cast<size_t>(y) * a.width + x] =
                        found ? pixel : (w > 1.0f ? b.At(x, y) : Mix(a.At(x, y), b.At(x, y), w));
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

/// A game's pictures: the frames where the screen changed, a burst of changes on consecutive frames counting as one
/// picture (Darkwing Duck redraws the play area, then the status row a frame later), unless the content changes
/// every frame (then every frame is a picture)
struct Cadence
{
    std::vector<int> keys;  // frame index of each complete picture (the burst's last frame)
    double period = 1.0;    // median frames between pictures
};

Cadence DetectCadence(const std::vector<uint32_t>& seq, size_t size, int count)
{
    std::vector<int> changes;
    for (int k = 1; k < count; k++)
        if (std::memcmp(seq.data() + size * k, seq.data() + size * (k - 1), size * 4) != 0)
            changes.push_back(k);
    Cadence c;
    if (changes.empty())
        return c;
    size_t single = 0;
    for (size_t i = 1; i < changes.size(); i++)
        single += changes[i] - changes[i - 1] == 1;
    const bool everyFrame = single * 2 > changes.size();
    for (size_t i = 0; i < changes.size(); i++)
    {
        const bool burstGoesOn = !everyFrame && i + 1 < changes.size() && changes[i + 1] - changes[i] == 1;
        if (!burstGoesOn)
            c.keys.push_back(changes[i]);
    }
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

/// Cadence quality on the game's own pictures:
///   interpolation: pictures i-1 and i+1 at the middle against picture i (as `eval`, on pictures, not frames);
///   extrapolation: pictures i-1 and i carried one picture on (w = 2) against picture i+1; the error left is the
///   jump the eye sees when the real picture arrives, against the repeat's (picture i shown until i+1)
int CadenceEval(const char* path, int width, int height, int count)
{
    const size_t size = static_cast<size_t>(width) * height;
    const std::vector<uint32_t> seq = Load(path, size * count);
    const Cadence cad = DetectCadence(seq, size, count);
    std::printf("pictures %zu, period %.0f frames\n", cad.keys.size(), cad.period);
    const auto key = [&](size_t i) { return Frame{width, height, seq.data() + size * cad.keys[i]}; };
    std::vector<uint32_t> out(size);
    Stats stats;
    double err[4] = {0, 0, 0, 0};
    uint64_t px[4] = {0, 0, 0, 0};
    uint64_t exact[4] = {0, 0, 0, 0};
    std::vector<double> ns;
    for (size_t i = 1; i + 1 < cad.keys.size(); i++)
    {
        const Frame prev = key(i - 1), cur = key(i), next = key(i + 1);
        Blend(prev, next, 0.5f, out.data());
        ScoreFrame(out.data(), prev, cur, next, &err[0], &px[0], &exact[0]);
        Interpolate(prev, next, 0.5f, out.data(), &stats);
        ScoreFrame(out.data(), prev, cur, next, &err[1], &px[1], &exact[1]);
        // Extrapolation: the truth is `next`; the pixels that count are the ones changing over the three pictures
        std::memcpy(out.data(), cur.pixels, size * 4);
        ScoreFrame(out.data(), prev, next, cur, &err[2], &px[2], &exact[2]);
        const auto t0 = std::chrono::steady_clock::now();
        Interpolate(prev, cur, 2.0f, out.data(), &stats);
        ns.push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count());
        ScoreFrame(out.data(), prev, next, cur, &err[3], &px[3], &exact[3]);
    }
    const char* names[4] = {"interp b-blend", "interp c-block", "extrap repeat", "extrap d-carry"};
    for (int v = 0; v < 4; v++)
        std::printf("%-15s changed px %8llu  MAE %6.2f  exact %5.1f%%\n", names[v],
                    static_cast<unsigned long long>(px[v]), px[v] ? err[v] / (3.0 * px[v]) : 0.0,
                    px[v] ? 100.0 * exact[v] / px[v] : 100.0);
    if (!ns.empty())
    {
        std::sort(ns.begin(), ns.end());
        std::printf("extrapolation ms p50 %.2f max %.2f\n", ns[ns.size() / 2] / 1e6, ns.back() / 1e6);
    }
    return 0;
}

/// What a display at displayHz shows, four panels: a-repeat | c-block on consecutive frames | e-cadence (the step
/// between the game's pictures spread over the frames the lookahead already holds) | f-carry (the motion of the last
/// two pictures carried on through the period, the real picture replacing it on arrival)
int CadenceRender(const char* path, int width, int height, int count, double sourceHz, double displayHz,
                  int lookahead, const char* outPath)
{
    const size_t size = static_cast<size_t>(width) * height;
    const std::vector<uint32_t> seq = Load(path, size * count);
    const Cadence cad = DetectCadence(seq, size, count);
    const auto frame = [&](int k) { return Frame{width, height, seq.data() + size * k}; };
    FILE* f = std::fopen(outPath, "wb");
    if (!f)
        return 1;
    constexpr int PANELS = 4;
    std::vector<std::vector<uint32_t>> panels(PANELS, std::vector<uint32_t>(size));
    std::vector<uint32_t> row(static_cast<size_t>(width) * PANELS);
    Stats stats;
    int refreshes = 0;
    for (int n = 0;; n++)
    {
        const double t = n / displayHz * sourceHz;
        const int k = static_cast<int>(std::floor(t));
        if (k + 1 >= count)
            break;
        std::memcpy(panels[0].data(), frame(k).pixels, size * 4);
        Interpolate(frame(k), frame(k + 1), static_cast<float>(t - k), panels[1].data(), &stats, RANGE / 2, BUDGET);

        // The game's picture shown at t, and the next one
        size_t i = 0;
        while (i + 1 < cad.keys.size() && cad.keys[i + 1] <= k)
            i++;
        const bool havePicture = !cad.keys.empty() && cad.keys[i] <= k;
        std::memcpy(panels[2].data(), frame(k).pixels, size * 4);
        std::memcpy(panels[3].data(), frame(k).pixels, size * 4);
        if (havePicture && i + 1 < cad.keys.size())
        {
            const int f0 = cad.keys[i];
            const int f1 = cad.keys[i + 1];
            // e-cadence: the next picture is visible once it is within the lookahead; the step runs from then on
            const double start = std::max(static_cast<double>(f0), static_cast<double>(f1 - lookahead));
            if (t >= start && f1 - f0 > 1)
                Interpolate(frame(f0), frame(f1), static_cast<float>((t - start) / (f1 - start)), panels[2].data(),
                            &stats);
            // f-carry: the last two pictures' motion carried on by the time since this picture, at most a period
            if (i > 0 && f1 - f0 > 1)
            {
                const double into = std::min((t - f0) / cad.period, 1.0);
                if (into > 0.0)
                    Interpolate(frame(cad.keys[i - 1]), frame(f0), static_cast<float>(1.0 + into), panels[3].data(),
                                &stats);
            }
        }
        for (int y = 0; y < height; y++)
        {
            for (int v = 0; v < PANELS; v++)
                std::memcpy(&row[static_cast<size_t>(v) * width], &panels[v][static_cast<size_t>(y) * width],
                            static_cast<size_t>(width) * 4);
            std::fwrite(row.data(), 4, row.size(), f);
        }
        refreshes++;
    }
    std::fclose(f);
    std::printf("%d refreshes, pictures %zu, period %.0f, %dx%d\n", refreshes, cad.keys.size(), cad.period,
                width * PANELS, height);
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
    if (argc >= 6 && std::strcmp(argv[1], "cadence-eval") == 0)
        return CadenceEval(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]));
    if (argc >= 10 && std::strcmp(argv[1], "cadence-render") == 0)
        return CadenceRender(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]), std::atof(argv[6]),
                             std::atof(argv[7]), std::atoi(argv[8]), argv[9]);
    std::fprintf(stderr,
                 "usage: interp eval <seq.rgba> <w> <h> <count>\n"
                 "       interp render <seq.rgba> <w> <h> <count> <sourceHz> <displayHz> <out.rgba>\n"
                 "       interp cadence-eval <seq.rgba> <w> <h> <count>\n"
                 "       interp cadence-render <seq.rgba> <w> <h> <count> <sourceHz> <displayHz> <lookahead> <out.rgba>\n");
    return 2;
}
