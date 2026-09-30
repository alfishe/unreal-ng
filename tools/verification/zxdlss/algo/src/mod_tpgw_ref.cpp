/// @file mod_tpgw.cpp
/// @brief mod-tpgw-ref - the scalar reference of mod-tpgw (kept unoptimized:
/// the optimized mod-tpgw must match it bit for bit, zxdlss-render --dump).
///
/// A literal implementation of
/// docs/inprogress/2026-09-27-zxdlss-gigascreen/algorithm-mod-tpgw.md
/// (section numbers in the comments refer to it). Scalar reference: the hot
/// loops are tagged SIMD-CANDIDATE and are optimized separately, verified
/// against this code and the Python reference.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>

#include "emulator/video/zxdlss/algorithm.h"
#include "emulator/video/zxdlss/palette.h"
#include "emulator/video/zxdlss/registry.h"

namespace zxdlss
{
namespace
{

constexpr int kLookAhead = 6;                               // section 1
constexpr int kMaxSpan = 15;                                // span(5)
constexpr int kDepth = kLookAhead + kMaxSpan + 2;           // 23 (section 4.1)
constexpr int kPaperH = 192, kPaperW = 256;                // origin: FrameInput::paperX / paperY
constexpr int kMotionTile = 32, kMotionRadius = 8;          // section 4.3
constexpr int kFieldTile = 16;                              // section 4.4
constexpr int kBlock = 8, kBlockRadius = 8;                 // section 7.7

// Field stage constants (section 7)
constexpr double kFieldOn = 0.15, kFieldOff = 0.08, kUnexplained = 0.25;
constexpr int kMinTiles = 12, kPresent = 4, kGrowMin = 24;
constexpr double kWhole = 0.3, kWholeOff = 0.1;
constexpr int kWholeHold = 12;

inline int span(int p) { return std::max({2 * p, 6, 3 * p}); }

inline int floorDiv2(int v) { return v >= 0 ? v / 2 : -((-v + 1) / 2); }

inline int wrap(int v, int m) { v %= m; return v < 0 ? v + m : v; }

/// One frame in the ring with its per-frame features.
struct Frame
{
    std::vector<uint8_t> plane;
    std::vector<uint32_t> key;
    std::vector<uint16_t> hist;            // tile histograms: (th x tw) x 16
    // translation against the next older frame (section 4.3), computed on demand
    bool motionReady = false;
    std::vector<uint8_t> motion;
};

class ModTpgwRef final : public Algorithm
{
public:
    int delay() const override { return kLookAhead; }
    std::string name() const override { return "mod-tpgw-ref"; }

    std::string stats() const override
    {
        std::string s;
        char line[96];
        const double f = _frames ? static_cast<double>(_frames) : 1.0;
        for (int i = 0; i < kStages; ++i)
        {
            std::snprintf(line, sizeof(line), "  %-14s %8.3f ms/frame\n", kStageNames[i], 1000.0 * _stage[i] / f);
            s += line;
        }
        return s;
    }

    void process(const FrameInput& in, RGBImage& out) override
    {
        ++_frames;
        if (!_pal.sameAs(in.palette))
            _pal = Palette::fromRGBA(in.palette);
        setup(in.width, in.height, in.paperX, in.paperY);
        Timer tp(_stage[kPush]);
        push(in);
        tp.stop();
        const int n = static_cast<int>(_ring.size());
        const int L = std::min(kLookAhead, n - 1);

        out.assign(static_cast<size_t>(_w) * _h * 3, 0);
        if (n < 3)
        {
            renderRaw(L, out);
            return;
        }

        {
            Timer t(_stage[kPixel]);
            pixelStage(n, L);                                // section 5
        }
        {
            Timer t(_stage[kRender1]);
            renderStage1(L);                                 // _out1, _explained
        }

        // classifier (section 6) + field stage (section 7)
        bool fieldRan = false;
        if (n >= L + 3)
        {
            Timer tf(_stage[kFeatures]);
            tileFeatures(n, L);
            tf.stop();
            bool anySetAlt = false;
            for (uint8_t v : _setAlt)
                anySetAlt |= v != 0;
            if (anySetAlt)
            {
                Timer t(_stage[kField]);
                fieldStage(n, L);
                fieldRan = true;
            }
        }
        if (!fieldRan)
            fieldSkip();

        out = _out1;
        if (fieldRan && _anyFieldPixel)
        {
            Timer t(_stage[kRenderField]);
            renderField(L, out);
        }
    }

private:
    enum Stage { kPush, kMotion, kPixel, kRender1, kFeatures, kField, kRenderField, kStages };
    static constexpr const char* kStageNames[kStages] = {"push+keys", "translation", "pixel stage*", "render stage1",
                                                           "tile features", "field stage", "render field"};
    /// Adds the scope's duration to a stage counter.
    struct Timer
    {
        double& acc;
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        bool done = false;
        explicit Timer(double& a) : acc(a) {}
        void stop()
        {
            if (!done)
                acc += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            done = true;
        }
        ~Timer() { stop(); }
    };
    double _stage[kStages] = {};
    uint64_t _frames = 0;

    int _w = 0, _h = 0, _th = 0, _tw = 0, _paperX = 48, _paperY = 48;
    std::deque<std::unique_ptr<Frame>> _ring;                // newest first
    Palette _pal;   // the emulator's active palette (FrameInput::palette)

    // pixel stage
    std::vector<uint8_t> _period, _start;
    std::vector<uint8_t> _out1, _raw, _explained;

    // field stage
    std::vector<double> _alt;
    std::vector<uint8_t> _setAlt, _seeds, _field, _px;
    bool _wholeOn = false;
    int _below = 0;
    bool _anyFieldPixel = false;

    void setup(int w, int h, int paperX, int paperY)
    {
        if (w == _w && h == _h && paperX == _paperX && paperY == _paperY)
            return;
        _w = w;
        _h = h;
        _paperX = paperX;
        _paperY = paperY;
        _th = h / kFieldTile;
        _tw = w / kFieldTile;
        _ring.clear();
        _seeds.assign(static_cast<size_t>(_th) * _tw, 0);
        _field.assign(_seeds.size(), 0);
        _wholeOn = false;
        _below = 0;
    }

    // ---- section 4: ring, keys, histograms ----------------------------------
    void push(const FrameInput& in)
    {
        auto f = std::make_unique<Frame>();
        const size_t px = static_cast<size_t>(_w) * _h;
        f->plane.assign(in.plane, in.plane + px);
        f->key.resize(px);
        for (int y = 0; y < _h; ++y)
        {
            const bool paperRow = y >= _paperY && y < _paperY + kPaperH;
            for (int x = 0; x < _w; ++x)
            {
                const size_t i = static_cast<size_t>(y) * _w + x;
                if (paperRow && x >= _paperX && x < _paperX + kPaperW)
                {
                    const int x0 = x & ~7;
                    uint32_t byte = 0;
                    for (int k = 0; k < 8; ++k)
                        byte |= static_cast<uint32_t>(in.ink[static_cast<size_t>(y) * _w + x0 + k] & 1) << (7 - k);
                    f->key[i] = (1u << 20) | (byte << 8) | in.attr[i];
                }
                else
                {
                    f->key[i] = in.plane[i];
                }
            }
        }
        f->hist.assign(static_cast<size_t>(_th) * _tw * 16, 0);
        for (int y = 0; y < _th * kFieldTile; ++y)
            for (int x = 0; x < _tw * kFieldTile; ++x)
                ++f->hist[((static_cast<size_t>(y / kFieldTile) * _tw) + x / kFieldTile) * 16 + f->plane[static_cast<size_t>(y) * _w + x]];
        _ring.push_front(std::move(f));
        while (static_cast<int>(_ring.size()) > kDepth)
            _ring.pop_back();
    }

    /// section 4.3: pixels of ring frame i changed by a translation from frame i+1
    const std::vector<uint8_t>& motion(int i)
    {
        Frame& f = *_ring[i];
        if (f.motionReady)
            return f.motion;
        Timer timer(_stage[kMotion]);
        f.motionReady = true;
        const size_t px = static_cast<size_t>(_w) * _h;
        f.motion.assign(px, 0);
        if (i + 1 >= static_cast<int>(_ring.size()))
            return f.motion;
        const uint8_t* cur = f.plane.data();
        const uint8_t* prev = _ring[i + 1]->plane.data();
        const int th = _h / kMotionTile, tw = _w / kMotionTile;
        const int nshift = (2 * kMotionRadius + 1) * (2 * kMotionRadius + 1);
        std::vector<int> cost(static_cast<size_t>(nshift) * th * tw, 0);
        // SIMD-CANDIDATE(O-2): compare + count per tile for 289 shifts
        int k = 0;
        for (int dy = -kMotionRadius; dy <= kMotionRadius; ++dy)
            for (int dx = -kMotionRadius; dx <= kMotionRadius; ++dx, ++k)
                for (int y = 0; y < th * kMotionTile; ++y)
                {
                    const uint8_t* crow = cur + static_cast<size_t>(y) * _w;
                    const uint8_t* prow = prev + static_cast<size_t>(wrap(y - dy, _h)) * _w;
                    int* ct = &cost[(static_cast<size_t>(k) * th + y / kMotionTile) * tw];
                    for (int x = 0; x < tw * kMotionTile; ++x)
                        ct[x / kMotionTile] += crow[x] != prow[wrap(x - dx, _w)];
                }
        const int zero = (2 * kMotionRadius + 1) * kMotionRadius + kMotionRadius;
        for (int ty = 0; ty < th; ++ty)
            for (int tx = 0; tx < tw; ++tx)
            {
                const size_t t = static_cast<size_t>(ty) * tw + tx;
                int best = -1, bestCost = 0;
                for (int s = 0; s < nshift; ++s)
                {
                    if (s == zero)
                        continue;
                    const int c = cost[static_cast<size_t>(s) * th * tw + t];
                    if (best < 0 || c < bestCost)
                    {
                        best = s;
                        bestCost = c;
                    }
                }
                const int c0 = cost[static_cast<size_t>(zero) * th * tw + t];
                if (!(bestCost < 0.25 * c0 && c0 > 0.02 * kMotionTile * kMotionTile))
                    continue;
                const int dy = best / (2 * kMotionRadius + 1) - kMotionRadius;
                const int dx = best % (2 * kMotionRadius + 1) - kMotionRadius;
                for (int y = ty * kMotionTile; y < (ty + 1) * kMotionTile; ++y)
                    for (int x = tx * kMotionTile; x < (tx + 1) * kMotionTile; ++x)
                    {
                        const size_t p = static_cast<size_t>(y) * _w + x;
                        const uint8_t sh = prev[static_cast<size_t>(wrap(y - dy, _h)) * _w + wrap(x - dx, _w)];
                        f.motion[p] = cur[p] == sh && cur[p] != prev[p];
                    }
            }
        return f.motion;
    }

    // ---- section 5: pixel stage ---------------------------------------------
    void pixelStage(int n, int L)
    {
        const size_t px = static_cast<size_t>(_w) * _h;
        _period.assign(px, 0);
        _start.assign(px, 0);
        // motion planes needed by P >= 3: indices 0 .. L + kMaxSpan
        std::vector<const uint8_t*> mot(n, nullptr);
        for (int i = 0; i + 1 < n; ++i)
            mot[i] = motion(i).data();
        std::vector<const uint32_t*> key(n);
        std::vector<const uint8_t*> pl(n);
        for (int i = 0; i < n; ++i)
        {
            key[i] = _ring[i]->key.data();
            pl[i] = _ring[i]->plane.data();
        }
        // SIMD-CANDIDATE(O-1): period runs per pixel
        for (size_t p = 0; p < px; ++p)
        {
            for (int P = 2; P <= 5 && _period[p] == 0; ++P)
            {
                const int sp = span(P);
                const int aLo = std::max(0, L - sp + 1), aHi = std::min(L, n - sp);
                for (int a = aLo; a <= aHi; ++a)
                {
                    bool ok = true;
                    for (int j = a; j < a + sp - P && ok; ++j)
                        ok = key[j][p] == key[j + P][p];
                    if (!ok)
                        continue;
                    bool constant = true;
                    for (int j = a + 1; j < a + P; ++j)
                        constant &= key[j][p] == key[a][p];
                    if (constant)
                        continue;
                    const int b = std::max({std::min(L, a + sp - P), a, L - P + 1});
                    const float l0 = _pal.luma[pl[b][p]];
                    bool lumaOk = false;
                    for (int j = 1; j < P; ++j)
                        lumaOk |= _pal.luma[pl[b + j][p]] != l0;
                    if (!lumaOk)
                        continue;
                    if (P > 2)
                    {
                        bool moved = false;
                        for (int i = a; i <= a + sp - 2 && !moved; ++i)
                            moved = mot[i] && mot[i][p];
                        if (moved)
                            continue;
                    }
                    _period[p] = static_cast<uint8_t>(P);
                    _start[p] = static_cast<uint8_t>(b);
                    break;
                }
            }
        }
    }

    void writeRGB(uint8_t* dst, const double acc[3]) const
    {
        dst[0] = Palette::linearToSrgb(acc[0]);
        dst[1] = Palette::linearToSrgb(acc[1]);
        dst[2] = Palette::linearToSrgb(acc[2]);
    }

    void renderRaw(int L, RGBImage& out) const
    {
        const uint8_t* t = _ring[L]->plane.data();
        const size_t px = static_cast<size_t>(_w) * _h;
        for (size_t p = 0; p < px; ++p)
        {
            const auto& l = _pal.linear[t[p]];
            const double acc[3] = {0.0 + l[0], 0.0 + l[1], 0.0 + l[2]};
            writeRGB(&out[p * 3], acc);
        }
    }

    /// Stage 1 output: the period recipes, raw elsewhere; explained = differs from raw.
    void renderStage1(int L)
    {
        const size_t px = static_cast<size_t>(_w) * _h;
        _out1.assign(px * 3, 0);
        _raw.assign(px * 3, 0);
        _explained.assign(px, 0);
        renderRaw(L, _raw);
        // SIMD-CANDIDATE(O-3): table the mixes of equal weights
        for (size_t p = 0; p < px; ++p)
        {
            const int P = _period[p];
            if (P == 0)
            {
                std::memcpy(&_out1[p * 3], &_raw[p * 3], 3);
                continue;
            }
            const int b = _start[p];
            const double w = 1.0 / P;
            // term order: frame t first, then the other frames of the series ascending
            double acc[3] = {0.0, 0.0, 0.0};
            const auto& lt = _pal.linear[_ring[L]->plane[p]];
            for (int k = 0; k < 3; ++k)
                acc[k] += lt[k] * w;
            for (int j = 0; j < P; ++j)
            {
                const int i = b + j;
                if (i == L)
                    continue;
                const auto& l = _pal.linear[_ring[i]->plane[p]];
                for (int k = 0; k < 3; ++k)
                    acc[k] += l[k] * w;
            }
            writeRGB(&_out1[p * 3], acc);
            _explained[p] = std::memcmp(&_out1[p * 3], &_raw[p * 3], 3) != 0;
        }
    }

    // ---- section 7: field stage -----------------------------------------------
    void tileFeatures(int n, int L)
    {
        const size_t tiles = static_cast<size_t>(_th) * _tw;
        _alt.assign(tiles, 0.0);
        _setAlt.assign(tiles, 0);
        std::vector<int> votes(tiles, 0);
        const int i0 = std::max(0, L - 3), i1 = std::min(n - 2, L + 4);
        const int total = std::max(0, i1 - i0);
        const double area = kFieldTile * kFieldTile;
        for (int i = i0; i < i1; ++i)
        {
            const uint16_t* h0 = _ring[i]->hist.data();
            const uint16_t* h1 = _ring[i + 1]->hist.data();
            const uint16_t* h2 = _ring[i + 2]->hist.data();
            for (size_t t = 0; t < tiles; ++t)
            {
                int d01 = 0, d02 = 0;
                bool same2 = true, diff1 = false;
                for (int c = 0; c < 16; ++c)
                {
                    const int a = h0[t * 16 + c], b = h1[t * 16 + c], d = h2[t * 16 + c];
                    d01 += std::abs(a - b);
                    d02 += std::abs(a - d);
                    const bool s0 = a >= kPresent, s1 = b >= kPresent, s2 = d >= kPresent;
                    same2 &= s0 == s2;
                    diff1 |= s0 != s1;
                }
                _alt[t] += (d01 - d02) / (2.0 * area);
                votes[t] += same2 && diff1;
            }
        }
        for (size_t t = 0; t < tiles; ++t)
        {
            _alt[t] = total ? _alt[t] / total : 0.0;
            _setAlt[t] = total && votes[t] * 2 > total;
        }
    }

    bool isPaperTile(int ty, int tx) const
    {
        // the tile's center on the paper (standard frame: rows 3..14, columns 3..18)
        const int cy = ty * kFieldTile + kFieldTile / 2, cx = tx * kFieldTile + kFieldTile / 2;
        return cy >= _paperY && cy < _paperY + kPaperH && cx >= _paperX && cx < _paperX + kPaperW;
    }

    /// 4-connected components of `mask` with at least minSize tiles.
    std::vector<uint8_t> largeComponents(const std::vector<uint8_t>& mask, int minSize) const
    {
        std::vector<int> label(mask.size(), 0);
        std::vector<uint8_t> keep(mask.size(), 0);
        std::vector<int> stack, comp;
        int cur = 0;
        for (int y = 0; y < _th; ++y)
            for (int x = 0; x < _tw; ++x)
            {
                const int s = y * _tw + x;
                if (!mask[s] || label[s])
                    continue;
                ++cur;
                stack.assign(1, s);
                comp.clear();
                label[s] = cur;
                while (!stack.empty())
                {
                    const int c = stack.back();
                    stack.pop_back();
                    comp.push_back(c);
                    const int cy = c / _tw, cx = c % _tw;
                    const int nb[4][2] = {{cy - 1, cx}, {cy + 1, cx}, {cy, cx - 1}, {cy, cx + 1}};
                    for (const auto& q : nb)
                    {
                        if (q[0] < 0 || q[0] >= _th || q[1] < 0 || q[1] >= _tw)
                            continue;
                        const int m = q[0] * _tw + q[1];
                        if (mask[m] && !label[m])
                        {
                            label[m] = cur;
                            stack.push_back(m);
                        }
                    }
                }
                if (static_cast<int>(comp.size()) >= minSize)
                    for (int c : comp)
                        keep[c] = 1;
            }
        return keep;
    }

    std::vector<uint8_t> dilate4(const std::vector<uint8_t>& m) const
    {
        std::vector<uint8_t> d = m;
        for (int y = 0; y < _th; ++y)
            for (int x = 0; x < _tw; ++x)
                if (m[y * _tw + x])
                {
                    if (y > 0) d[(y - 1) * _tw + x] = 1;
                    if (y + 1 < _th) d[(y + 1) * _tw + x] = 1;
                    if (x > 0) d[y * _tw + x - 1] = 1;
                    if (x + 1 < _tw) d[y * _tw + x + 1] = 1;
                }
        return d;
    }

    void fieldSkip()
    {
        std::fill(_field.begin(), _field.end(), 0);
        std::fill(_seeds.begin(), _seeds.end(), 0);
        if (_wholeOn && ++_below >= kWholeHold)
        {
            _wholeOn = false;
            _below = 0;
        }
        _anyFieldPixel = false;
    }

    void fieldStage(int n, int L)
    {
        _anyFieldPixel = false;
        if (n < L + 3 || L < 1)
            return;
        const size_t tiles = static_cast<size_t>(_th) * _tw;
        // section 7.2: unexplained flicker per tile
        std::vector<double> un(tiles, 0.0);
        const uint8_t* t = _ring[L]->plane.data();
        const uint8_t* pv = _ring[L + 1]->plane.data();
        for (int y = 0; y < _th * kFieldTile; ++y)
            for (int x = 0; x < _tw * kFieldTile; ++x)
            {
                const size_t p = static_cast<size_t>(y) * _w + x;
                un[static_cast<size_t>(y / kFieldTile) * _tw + x / kFieldTile] += t[p] != pv[p] && !_explained[p];
            }
        for (double& v : un)
            v /= kFieldTile * kFieldTile;

        // section 7.3: seeds with hysteresis
        std::vector<uint8_t> cand(tiles, 0);
        for (size_t s = 0; s < tiles; ++s)
        {
            const bool c = _seeds[s] ? _alt[s] >= kFieldOff : (_alt[s] >= kFieldOn && un[s] >= kUnexplained);
            cand[s] = c && _setAlt[s];
        }
        _seeds = largeComponents(cand, kMinTiles);

        // section 7.4: growth inside the paper, whole-paper mode
        std::vector<uint8_t> region(tiles, 0), seedsPaper(tiles, 0);
        int paperTiles = 0;
        for (int y = 0; y < _th; ++y)
            for (int x = 0; x < _tw; ++x)
            {
                const size_t s = static_cast<size_t>(y) * _tw + x;
                const bool paper = isPaperTile(y, x);
                paperTiles += paper;
                region[s] = _alt[s] >= kFieldOn && _setAlt[s] && paper;
                seedsPaper[s] = _seeds[s] && paper;
            }
        std::vector<uint8_t> grown = largeComponents(seedsPaper, kGrowMin);
        const std::vector<uint8_t> big = grown;
        for (;;)
        {
            const std::vector<uint8_t> nb = dilate4(grown);
            bool added = false;
            for (size_t s = 0; s < tiles; ++s)
                if (nb[s] && region[s] && !grown[s])
                {
                    grown[s] = 1;
                    added = true;
                }
            if (!added)
                break;
        }
        for (size_t s = 0; s < tiles; ++s)
            grown[s] |= big[s];
        const std::vector<uint8_t> dg = dilate4(grown);
        int inPaper = 0;
        for (int y = 0; y < _th; ++y)
            for (int x = 0; x < _tw; ++x)
            {
                const size_t s = static_cast<size_t>(y) * _tw + x;
                _field[s] = _seeds[s] || (dg[s] && isPaperTile(y, x));
                inPaper += _field[s] && isPaperTile(y, x);
            }
        const double share = paperTiles ? static_cast<double>(inPaper) / paperTiles : 0.0;
        if (share >= kWhole)
        {
            _wholeOn = true;
            _below = 0;
        }
        else if (_wholeOn)
        {
            _below = share < kWholeOff ? _below + 1 : 0;
            if (_below >= kWholeHold)
            {
                _wholeOn = false;
                _below = 0;
            }
        }
        if (_wholeOn)
            for (int y = 0; y < _th; ++y)
                for (int x = 0; x < _tw; ++x)
                    if (isPaperTile(y, x))
                        _field[static_cast<size_t>(y) * _tw + x] = 1;

        _px.assign(static_cast<size_t>(_w) * _h, 0);
        for (int y = 0; y < _th * kFieldTile; ++y)
            for (int x = 0; x < _tw * kFieldTile; ++x)
                if (_field[static_cast<size_t>(y / kFieldTile) * _tw + x / kFieldTile])
                {
                    _px[static_cast<size_t>(y) * _w + x] = 1;
                    _anyFieldPixel = true;
                }
    }

    /// section 7.7: block motion t-1 -> t+1 on the midway grid, other page at t
    void otherPage(const uint8_t* prev, const uint8_t* next, std::vector<uint8_t>& fromPrev,
                   std::vector<uint8_t>& fromNext) const
    {
        const int bh = _h / kBlock, bw = _w / kBlock;
        std::vector<double> best(static_cast<size_t>(bh) * bw, 1e300);
        std::vector<int> vy(best.size(), 0), vx(best.size(), 0);
        std::vector<int> cnt(best.size(), 0);
        // SIMD-CANDIDATE(O-4): compare + count per block for 289 shifts
        for (int dy = -kBlockRadius; dy <= kBlockRadius; ++dy)
            for (int dx = -kBlockRadius; dx <= kBlockRadius; ++dx)
            {
                const int ay = floorDiv2(dy), ax = floorDiv2(dx);
                const int by = dy - ay, bx = dx - ax;
                std::fill(cnt.begin(), cnt.end(), 0);
                for (int y = 0; y < bh * kBlock; ++y)
                {
                    const uint8_t* ar = prev + static_cast<size_t>(wrap(y - ay, _h)) * _w;
                    const uint8_t* br = next + static_cast<size_t>(wrap(y + by, _h)) * _w;
                    int* cr = &cnt[static_cast<size_t>(y / kBlock) * bw];
                    for (int x = 0; x < bw * kBlock; ++x)
                        cr[x / kBlock] += ar[wrap(x - ax, _w)] != br[wrap(x + bx, _w)];
                }
                const double bias = 1e-3 * (std::abs(dy) + std::abs(dx));
                for (size_t s = 0; s < best.size(); ++s)
                {
                    const double c = cnt[s] + bias;
                    if (c < best[s])
                    {
                        best[s] = c;
                        vy[s] = dy;
                        vx[s] = dx;
                    }
                }
            }
        fromPrev.resize(static_cast<size_t>(_w) * _h);
        fromNext.resize(fromPrev.size());
        for (int y = 0; y < _h; ++y)
            for (int x = 0; x < _w; ++x)
            {
                const int by_ = std::min(y / kBlock, bh - 1), bx_ = std::min(x / kBlock, bw - 1);
                const size_t s = static_cast<size_t>(by_) * bw + bx_;
                const int ay = floorDiv2(vy[s]), ax = floorDiv2(vx[s]);
                const int byy = vy[s] - ay, bxx = vx[s] - ax;
                const size_t p = static_cast<size_t>(y) * _w + x;
                fromPrev[p] = prev[static_cast<size_t>(wrap(y - ay, _h)) * _w + wrap(x - ax, _w)];
                fromNext[p] = next[static_cast<size_t>(wrap(y + byy, _h)) * _w + wrap(x + bxx, _w)];
            }
    }

    void renderField(int L, RGBImage& out) const
    {
        std::vector<uint8_t> fp, fn;
        otherPage(_ring[L + 1]->plane.data(), _ring[L - 1]->plane.data(), fp, fn);
        const uint8_t* t = _ring[L]->plane.data();
        const size_t px = static_cast<size_t>(_w) * _h;
        for (size_t p = 0; p < px; ++p)
        {
            if (!_px[p])
                continue;
            const auto& lt = _pal.linear[t[p]];
            const auto& la = _pal.linear[fp[p]];
            const auto& lb = _pal.linear[fn[p]];
            double acc[3];
            for (int k = 0; k < 3; ++k)
            {
                acc[k] = 0.0;
                acc[k] += lt[k] * 0.5;
                acc[k] += la[k] * 0.25;
                acc[k] += lb[k] * 0.25;
            }
            writeRGB(&out[p * 3], acc);
        }
    }
};

const Registration kRegistration("mod-tpgw-ref", [] { return std::make_unique<ModTpgwRef>(); });

}  // namespace
}  // namespace zxdlss
