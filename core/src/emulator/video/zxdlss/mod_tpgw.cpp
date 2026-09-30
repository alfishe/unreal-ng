/// @file mod_tpgw.cpp
/// @brief mod-tpgw - the first ZX DLSS GigaScreen de-flicker algorithm, optimized.
///
/// Specification: docs/inprogress/2026-09-27-zxdlss-gigascreen/algorithm-mod-tpgw.md
/// (section numbers in the comments). The unoptimized, literal implementation
/// is mod_tpgw_ref.cpp ("mod-tpgw-ref"); this one must match it bit for bit
/// (zxdlss-render --dump + scripts/compare_dump.py).
///
/// Optimizations against the reference (same results):
///   - per-pixel rolling bit masks: bit j of eq[P] = key(j) == key(j+P), bit i
///     of mot = translation(i); a run check is one shift and mask, and a frame
///     adds 4 key compares per pixel instead of re-comparing the ring
///   - translation and two-page block motion compare whole rows against
///     horizontally pre-shifted copies (no modulo per pixel); counts per tile /
///     block with a byte-parallel trick on 8-byte words
///   - mixes are looked up in tables built with the reference's summation order
///     (pairs 16^2, triples 16^3, quadruples 16^4, quintuples on demand)
///   - a rolling mask of key(j) == key(j+1) skips pixels unchanged over the ring
///     (no run can be non-constant there) and answers the constant test
///   - the 289-shift searches and the pixel stage run on several threads
///     (ZXDLSS_THREADS, default: the hardware threads, at most 8); the block
///     search merges the threads' minima in shift order, so ties resolve as in
///     the reference

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <memory>
#include <thread>

#include "palette.h"
#include "registry.h"
#include "simd.h"
#include "common/threadpool.h"
#include "algorithm.h"

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

constexpr double kFieldOn = 0.15, kFieldOff = 0.08, kUnexplained = 0.25;
constexpr int kMinTiles = 12, kPresent = 4, kGrowMin = 24;
constexpr double kWhole = 0.3, kWholeOff = 0.1;
constexpr int kWholeHold = 12;

constexpr int span(int p) { return std::max({2 * p, 6, 3 * p}); }
inline int floorDiv2(int v) { return v >= 0 ? v / 2 : -((-v + 1) / 2); }
inline int wrap(int v, int m) { v %= m; return v < 0 ? v + m : v; }

/// dst[x] = src[(x - shift) mod w] for a row of w bytes: two memcpy segments.
inline void copyShifted(uint8_t* dst, const uint8_t* src, int w, int shift)
{
    shift = wrap(shift, w);
    std::memcpy(dst + shift, src, static_cast<size_t>(w - shift));
    std::memcpy(dst, src + (w - shift), static_cast<size_t>(shift));
}

int defaultThreads()
{
    if (const char* env = std::getenv("ZXDLSS_THREADS"))
        return std::max(1, std::atoi(env));
    const unsigned hw = std::thread::hardware_concurrency();
    return static_cast<int>(std::clamp(hw ? hw : 1u, 1u, 8u));
}

/// Mixes of palette colors, summed in the reference's order (term order = the
/// order of the arguments), tabulated. Entry: R | G << 8 | B << 16 | 1 << 24.
class MixTables
{
public:
    explicit MixTables(const Palette& palette) : _pal(palette)
    {
        const Palette& pal = _pal;
        for (int a = 0; a < 16; ++a)
        {
            _raw[a] = pack(sum(pal, {a}, 1.0));
            for (int b = 0; b < 16; ++b)
            {
                _p2[a * 16 + b] = pack(sum(pal, {a, b}, 0.5));
                for (int c = 0; c < 16; ++c)
                {
                    _p3[(a * 16 + b) * 16 + c] = pack(sum(pal, {a, b, c}, 1.0 / 3));
                    Acc tp{{0.0, 0.0, 0.0}};       // two-page: 1/2 t + 1/4 from t-1 + 1/4 from t+1
                    for (int k = 0; k < 3; ++k)
                    {
                        tp.v[k] += pal.linear[a][k] * 0.5;
                        tp.v[k] += pal.linear[b][k] * 0.25;
                        tp.v[k] += pal.linear[c][k] * 0.25;
                    }
                    _tp[(a * 16 + b) * 16 + c] = pack(tp);
                    for (int d = 0; d < 16; ++d)
                        _p4[((a * 16 + b) * 16 + c) * 16 + d] = pack(sum(pal, {a, b, c, d}, 0.25));
                }
            }
        }
        _p5.assign(1u << 20, 0);
    }

    uint32_t raw(int a) const { return _raw[a]; }
    uint32_t p2(int a, int b) const { return _p2[a * 16 + b]; }
    uint32_t p3(int a, int b, int c) const { return _p3[(a * 16 + b) * 16 + c]; }
    uint32_t p4(int a, int b, int c, int d) const { return _p4[((a * 16 + b) * 16 + c) * 16 + d]; }
    uint32_t twoPage(int t, int fp, int fn) const { return _tp[(t * 16 + fp) * 16 + fn]; }
    uint32_t p5(int a, int b, int c, int d, int e)
    {
        const uint32_t i = static_cast<uint32_t>((((a * 16 + b) * 16 + c) * 16 + d) * 16 + e);
        if (!_p5[i])
            _p5[i] = pack(sum(_pal, {a, b, c, d, e}, 0.2));
        return _p5[i];
    }

private:
    struct Acc
    {
        double v[3];
    };
    Palette _pal;   // the colors the tables were built from (p5 fills in on demand)
    std::array<uint32_t, 16> _raw{};
    std::array<uint32_t, 256> _p2{};
    std::array<uint32_t, 4096> _p3{}, _tp{};
    std::vector<uint32_t> _p4 = std::vector<uint32_t>(65536);
    std::vector<uint32_t> _p5;

    static Acc sum(const Palette& pal, std::initializer_list<int> colors, double w)
    {
        Acc acc{{0.0, 0.0, 0.0}};
        for (int c : colors)
            for (int k = 0; k < 3; ++k)
                acc.v[k] += pal.linear[c][k] * w;
        return acc;
    }
    static uint32_t pack(const Acc& a)
    {
        return Palette::linearToSrgb(a.v[0]) | (Palette::linearToSrgb(a.v[1]) << 8) |
               (Palette::linearToSrgb(a.v[2]) << 16) | (1u << 24);
    }
};

struct Frame
{
    std::vector<uint8_t> plane;
    std::vector<uint32_t> key;
    std::vector<uint16_t> hist;            // tile histograms: (th x tw) x 16
    bool flat = false;                     // one color on >= 99 % of the frame (a whole-screen flash)
};

/// Variant switches (spec sections 5 and 7.8).
struct Variant
{
    const char* name = "mod-tpgw";
    bool sceneAverage = false;     ///< scene stage (7.8)
    bool flatVeto = false;         ///< no period series with a whole-screen flash (5)
    int maxPeriod = 5;             ///< period detectors 2..maxPeriod
    double sceneObject = 0.17;     ///< object group share of the tiles
    double sceneDetail = 0.0;      ///< object tile: horizontal color changes >= this share (0: off)
    bool sceneSteps = false;       ///< scene render: step-aware two-page mix instead of the average
    bool seedsPaper = false;       ///< field seeds on the paper only (7.3)
    bool seedsBorderDetail = false;///< no field seeds off the paper on horizontal-stripe tiles (7.3)
};

class ModTpgw final : public Algorithm
{
public:
    /// sceneAverage: the scene stage of mod-tpgwa (section 7.8); flatVeto + maxPeriod 4:
    /// mod-tpgwaf (section 5: no series with a whole-screen flash, periods 2..4)
    explicit ModTpgw(const Variant& v)
        : _name(v.name), _sceneAverage(v.sceneAverage), _flatVeto(v.flatVeto), _maxPeriod(v.maxPeriod),
          _sceneObject(v.sceneObject), _sceneDetail(v.sceneDetail), _sceneSteps(v.sceneSteps),
          _seedsPaper(v.seedsPaper), _seedsBorderDetail(v.seedsBorderDetail) {}
    int delay() const override { return kLookAhead; }
    std::string name() const override { return _name; }

    FrameReport lastFrame() const override { return _last; }

    std::string stats() const override
    {
        std::string s = std::string("  kernels: ") + simd::path() + ", threads: " + std::to_string(_threads) + "\n";
        char line[96];
        const double f = _frames ? static_cast<double>(_frames) : 1.0;
        for (int i = 0; i < kStages; ++i)
        {
            std::snprintf(line, sizeof(line), "  %-14s %8.3f ms/frame\n", kStageNames[i], 1000.0 * _stage[i] / f);
            s += line;
        }
        // which detectors rendered what (share of all output pixels, share of frames using it)
        const double pixels = f * static_cast<double>(_w) * _h;
        s += "  detector usage (pixels mixed / frames with any):\n";
        const char* names[] = {"period2", "period3", "period4", "period5"};
        for (int P = 0; P < 4; ++P)
        {
            std::snprintf(line, sizeof(line), "  %-14s %7.3f %% of pixels  %6.1f %% of frames\n", names[P],
                          100.0 * static_cast<double>(_usePixels[P]) / pixels, 100.0 * static_cast<double>(_useFrames[P]) / f);
            s += line;
        }
        std::snprintf(line, sizeof(line), "  %-14s %7.3f %% of pixels  %6.1f %% of frames\n", "field (2-page)",
                      100.0 * static_cast<double>(_usePixels[4]) / pixels, 100.0 * static_cast<double>(_useFrames[4]) / f);
        s += line;
        std::snprintf(line, sizeof(line), "  field stage ran %.1f %% of frames, seeds %.1f %%, whole-paper %.1f %%\n",
                      100.0 * static_cast<double>(_fieldRanFrames) / f, 100.0 * static_cast<double>(_seedFrames) / f,
                      100.0 * static_cast<double>(_wholeFrames) / f);
        s += line;
        if (_sceneAverage)
        {
            std::snprintf(line, sizeof(line), "  scene average (whole frame) %.1f %% of frames\n",
                          100.0 * static_cast<double>(_sceneFrames) / f);
            s += line;
            // output frame ordinals (0 = first output frame) of the runs it was on
            s += "  scene average runs (output frame ordinals):";
            for (const auto& r : _sceneRuns)
            {
                std::snprintf(line, sizeof(line), " %llu-%llu", static_cast<unsigned long long>(r.first - kLookAhead),
                              static_cast<unsigned long long>(r.second - kLookAhead));
                s += line;
            }
            s += "\n";
        }
        return s;
    }

    void process(const FrameInput& in, RGBImage& out) override
    {
        ++_frames;
        if (!_mix || !_pal.sameAs(in.palette))
        {
            _pal = Palette::fromRGBA(in.palette);
            _mix = std::make_unique<MixTables>(_pal);
        }
        setup(in.width, in.height, in.paperX, in.paperY);
        {
            Timer t(_stage[kPush]);
            push(in);
        }
        const int n = static_cast<int>(_ring.size());
        {
            Timer t(_stage[kMotion]);
            updateMasks(n);
        }
        const int L = std::min(kLookAhead, n - 1);
        out.resize(static_cast<size_t>(_w) * _h * 3);
        if (n < 3)
        {
            renderRaw(L, out);
            return;
        }
        {
            Timer t(_stage[kPixel]);
            pixelStage(n, L);
        }
        {
            Timer t(_stage[kRender1]);
            renderStage1(L, out);
        }
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
        // the scene stage decides before the field render: while it is on the
        // whole frame is its average, and the field render (the block search)
        // would only be overwritten. The field stage's state is updated above.
        bool sceneOn = false;
        if (_sceneAverage && L >= 3 && n >= L + 4)
        {
            Timer t(_stage[kScene]);
            sceneOn = sceneStage(L);
            if (sceneOn && _sceneSteps)
                renderField(L, out, /*wholeFrameSteps=*/true);
            else if (sceneOn)
                renderSceneAverage(L, out);
        }
        if (fieldRan && _anyFieldPixel && !sceneOn)
        {
            Timer t(_stage[kRenderField]);
            renderField(L, out);
        }
        countUsage(fieldRan, sceneOn);
    }

private:
    enum Stage { kPush, kMotion, kPixel, kRender1, kFeatures, kField, kRenderField, kScene, kStages };
    static constexpr const char* kStageNames[kStages] = {"push+keys", "masks+transl.", "pixel stage", "render stage1",
                                                           "tile features", "field stage", "render field", "scene stage"};
    const std::string _name;
    const bool _sceneAverage;
    const bool _flatVeto;
    const int _maxPeriod;
    const double _sceneObject, _sceneDetail;
    const bool _sceneSteps;
    const bool _seedsPaper;
    const bool _seedsBorderDetail;
    bool _sceneOn = false;                   // section 7.8 state
    int _sceneOff = 0;
    uint64_t _sceneFrames = 0;
    std::vector<std::pair<uint64_t, uint64_t>> _sceneRuns;   // input frame ordinals
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
    uint64_t _usePixels[5] = {}, _useFrames[5] = {};      // period2..5, field
    uint64_t _fieldRanFrames = 0, _seedFrames = 0, _wholeFrames = 0;
    FrameReport _last;                                     // the last output frame (countUsage)

    int _w = 0, _h = 0, _th = 0, _tw = 0, _paperX = 48, _paperY = 48;
    std::deque<std::unique_ptr<Frame>> _ring;
    // The emulator's active palette (FrameInput::palette) and the mixes built
    // from it; both rebuilt when the palette changes
    Palette _pal;
    std::unique_ptr<MixTables> _mix;

    std::vector<uint32_t> _eq[4];            // rolling: bit j = key(j) == key(j + P), P = 2..5
    std::vector<uint32_t> _mot;              // rolling: bit i = translation(i)
    std::vector<uint32_t> _c1;               // rolling: bit j = key(j) == key(j + 1)
    int _threads = defaultThreads();
    // Worker threads that live as long as the algorithm (the caller is the
    // extra one): starting threads for every stage, several times a frame, cost
    // a large share of the frame on a loaded machine. The UI's priority, not more
    std::unique_ptr<ThreadPool> _pool =
        std::make_unique<ThreadPool>(static_cast<size_t>(_threads > 1 ? _threads - 1 : 0), "zxdlss",
                                     ThreadPool::Priority::Interactive);

    /// f(begin, end) over [0, n) split into `chunks` contiguous ranges, in parallel
    template <class F>
    void parallelFor(int n, int chunks, F&& f)
    {
        _pool->ParallelFor(n, chunks, f);
    }
    std::vector<uint8_t> _shifted, _motion;
    std::vector<int> _cost;

    std::vector<uint8_t> _period, _start, _explained;

    std::vector<double> _alt;
    std::vector<uint8_t> _setAlt, _seeds, _field;
    bool _wholeOn = false;
    int _below = 0;
    bool _anyFieldPixel = false;

    // ---- section 7.8: scene stage (mod-tpgwa) ---------------------------------
    static constexpr double kSceneMoving = 0.4, kSceneDyn = 0.05, kSceneStatic = 0.95;
    static constexpr int kSceneHold = 12;

    /// Features over t-3..t+3 and the on/off state; true while the whole frame is averaged.
    bool sceneStage(int L)
    {
        const int tiles = _th * _tw;
        std::vector<int> nStatic(tiles, 0), nDyn(tiles, 0), nEdge(tiles, 0);
        std::vector<uint16_t> colors(tiles, 0);
        const uint8_t* s[7];
        for (int k = 0; k < 7; ++k)
            s[k] = _ring[L - 3 + k]->plane.data();
        const uint8_t* t = s[3];
        const uint8_t* next = s[2];                  // ring L - 1 = t+1
        const uint8_t* prev = s[4];                  // ring L + 1 = t-1
        // tile rows are independent: each thread owns whole rows of tiles
        // SIMD-CANDIDATE(O-18): 16-byte compares for const / period 2 / dyn
        parallelFor(_th, _threads, [&](int ty0, int ty1) {
            for (int y = ty0 * kFieldTile; y < ty1 * kFieldTile; ++y)
                for (int x = 0; x < _tw * kFieldTile; ++x)
                {
                    const size_t p = static_cast<size_t>(y) * _w + x;
                    bool cst = true, p2 = true;
                    uint16_t c = 0;
                    for (int k = 0; k < 7; ++k)
                    {
                        cst &= s[k][p] == s[0][p];
                        if (k < 5)
                            p2 &= s[k][p] == s[k + 2][p];
                        c |= static_cast<uint16_t>(1u << s[k][p]);
                    }
                    const bool stat = cst || p2;
                    const int ti = (y / kFieldTile) * _tw + x / kFieldTile;
                    nStatic[ti] += stat;
                    nDyn[ti] += !stat && t[p] != prev[p] && t[p] != next[p];
                    nEdge[ti] += x + 1 < _w && t[p] != t[p + 1];      // horizontal color change
                    colors[ti] |= c;
                }
        });
        const double area = static_cast<double>(kFieldTile * kFieldTile);
        std::vector<uint8_t> object(tiles, 0);
        int moving = 0, paperTiles = 0;
        for (int ty = 0; ty < _th; ++ty)
            for (int tx = 0; tx < _tw; ++tx)
            {
                const int ti = ty * _tw + tx;
                object[ti] = nStatic[ti] / area >= kSceneStatic && std::popcount(colors[ti]) >= 3 &&
                             (_sceneDetail <= 0.0 || nEdge[ti] / area >= _sceneDetail);
                if (isPaperTile(ty, tx))
                {
                    ++paperTiles;
                    moving += nDyn[ti] / area > kSceneDyn;
                }
            }
        const double obj = static_cast<double>(largestComponent(object)) / tiles;
        const double mov = paperTiles ? static_cast<double>(moving) / paperTiles : 0.0;
        if (obj >= _sceneObject && mov >= kSceneMoving)
        {
            _sceneOn = true;
            _sceneOff = 0;
        }
        else if (_sceneOn && ++_sceneOff >= kSceneHold)
        {
            _sceneOn = false;
            _sceneOff = 0;
        }
        return _sceneOn;
    }

    /// 1/2 t + 1/4 t-1 + 1/4 t+1 on every pixel (the two-page mix table: same weights and order).
    void renderSceneAverage(int L, RGBImage& out)
    {
        const size_t px = static_cast<size_t>(_w) * _h;
        const uint8_t* t = _ring[L]->plane.data();
        const uint8_t* prev = _ring[L + 1]->plane.data();
        const uint8_t* next = _ring[L - 1]->plane.data();
        parallelFor(_h, _threads, [&](int y0, int y1) {
            for (size_t p = static_cast<size_t>(y0) * _w; p < static_cast<size_t>(y1) * _w && p < px; ++p)
                unpack(_mix->twoPage(t[p], prev[p], next[p]), &out[p * 3]);
        });
    }

    int largestComponent(const std::vector<uint8_t>& mask) const
    {
        std::vector<uint8_t> seen(mask.size(), 0);
        std::vector<int> stack;
        int best = 0;
        for (int s0 = 0; s0 < static_cast<int>(mask.size()); ++s0)
        {
            if (!mask[s0] || seen[s0])
                continue;
            int size = 0;
            stack.assign(1, s0);
            seen[s0] = 1;
            while (!stack.empty())
            {
                const int c = stack.back();
                stack.pop_back();
                ++size;
                const int cy = c / _tw, cx = c % _tw;
                const int nb[4][2] = {{cy - 1, cx}, {cy + 1, cx}, {cy, cx - 1}, {cy, cx + 1}};
                for (const auto& q : nb)
                {
                    if (q[0] < 0 || q[0] >= _th || q[1] < 0 || q[1] >= _tw)
                        continue;
                    const int nq = q[0] * _tw + q[1];
                    if (mask[nq] && !seen[nq])
                    {
                        seen[nq] = 1;
                        stack.push_back(nq);
                    }
                }
            }
            best = std::max(best, size);
        }
        return best;
    }

    /// Usage statistics: pixels whose output came from each detector (the field
    /// stage overrides the pixel stage on its tiles, the scene stage everything).
    void countUsage(bool fieldRan, bool sceneOn)
    {
        _sceneFrames += sceneOn;
        if (sceneOn)
        {
            const uint64_t k = _frames - 1;
            if (!_sceneRuns.empty() && _sceneRuns.back().second + 1 == k)
                _sceneRuns.back().second = k;
            else
                _sceneRuns.emplace_back(k, k);
        }
        const bool field = fieldRan && _anyFieldPixel;
        uint64_t n[5] = {};
        for (int y = 0; y < _h; ++y)
            for (int x = 0; x < _w; ++x)
            {
                const size_t p = static_cast<size_t>(y) * _w + x;
                if (field && _field[static_cast<size_t>(y / kFieldTile) * _tw + x / kFieldTile])
                    ++n[4];
                else if (_explained.size() == static_cast<size_t>(_w) * _h && _explained[p] && _period[p] >= 2)
                    ++n[_period[p] - 2];
            }
        for (int k = 0; k < 5; ++k)
        {
            _usePixels[k] += n[k];
            _useFrames[k] += n[k] != 0;
        }
        _last = FrameReport{};
        _last.valid = true;
        _last.frame = _frames - 1;
        _last.pixels = static_cast<uint32_t>(_w) * _h;
        for (int k = 0; k < 4; ++k)
            _last.periodPixels[k] = static_cast<uint32_t>(n[k]);
        _last.fieldPixels = static_cast<uint32_t>(n[4]);
        _last.fieldStage = fieldRan;
        _last.wholePaper = fieldRan && _wholeOn;
        _last.sceneAverage = sceneOn;
        _fieldRanFrames += fieldRan;
        if (fieldRan)
        {
            bool seeds = false;
            for (uint8_t v : _seeds)
                seeds |= v != 0;
            _seedFrames += seeds;
            _wholeFrames += _wholeOn;
        }
    }

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
        const size_t px = static_cast<size_t>(w) * h;
        for (auto& e : _eq)
            e.assign(px, 0);
        _mot.assign(px, 0);
        _c1.assign(px, 0);
        _seeds.assign(static_cast<size_t>(_th) * _tw, 0);
        _field.assign(_seeds.size(), 0);
        _wholeOn = false;
        _below = 0;
    }

    void push(const FrameInput& in)
    {
        std::unique_ptr<Frame> f;
        if (static_cast<int>(_ring.size()) >= kDepth)
        {
            f = std::move(_ring.back());           // reuse the oldest frame's buffers
            _ring.pop_back();
        }
        else
            f = std::make_unique<Frame>();
        const size_t px = static_cast<size_t>(_w) * _h;
        f->plane.assign(in.plane, in.plane + px);
        {
            size_t count[16] = {};
            for (size_t i = 0; i < px; ++i)
                ++count[in.plane[i] & 15];
            f->flat = static_cast<double>(*std::max_element(count, count + 16)) >= 0.99 * static_cast<double>(px);
        }
        f->key.resize(px);
        for (int y = 0; y < _h; ++y)
        {
            const size_t row = static_cast<size_t>(y) * _w;
            for (int x = 0; x < _w; ++x)
                f->key[row + x] = in.plane[row + x];
            if (y < _paperY || y >= _paperY + kPaperH)
                continue;
            for (int x0 = _paperX; x0 < _paperX + kPaperW; x0 += 8)
            {
                uint32_t byte = 0;
                for (int k = 0; k < 8; ++k)
                    byte |= static_cast<uint32_t>(in.ink[row + x0 + k] & 1) << (7 - k);
                for (int k = 0; k < 8; ++k)
                    f->key[row + x0 + k] = (1u << 20) | (byte << 8) | in.attr[row + x0 + k];
            }
        }
        f->hist.assign(static_cast<size_t>(_th) * _tw * 16, 0);
        for (int y = 0; y < _th * kFieldTile; ++y)
        {
            const uint8_t* r = &f->plane[static_cast<size_t>(y) * _w];
            uint16_t* hrow = &f->hist[static_cast<size_t>(y / kFieldTile) * _tw * 16];
            for (int x = 0; x < _tw * kFieldTile; ++x)
                ++hrow[(x / kFieldTile) * 16 + r[x]];
        }
        _ring.push_front(std::move(f));
    }

    /// Shift the rolling masks by one frame and add the newest frame's bits.
    void updateMasks(int n)
    {
        const size_t px = static_cast<size_t>(_w) * _h;
        const uint32_t* k0 = _ring[0]->key.data();
        for (int P = 2; P <= 5; ++P)
        {
            uint32_t* e = _eq[P - 2].data();
            if (n > P)
            {
                const uint32_t* kp = _ring[P]->key.data();
                for (size_t p = 0; p < px; ++p)
                    e[p] = (e[p] << 1) | static_cast<uint32_t>(k0[p] == kp[p]);
            }
            else
                for (size_t p = 0; p < px; ++p)
                    e[p] <<= 1;
        }
        {
            uint32_t* c = _c1.data();
            if (n > 1)
            {
                const uint32_t* k1 = _ring[1]->key.data();
                for (size_t p = 0; p < px; ++p)
                    c[p] = (c[p] << 1) | static_cast<uint32_t>(k0[p] == k1[p]);
            }
            else
                for (size_t p = 0; p < px; ++p)
                    c[p] <<= 1;
        }
        if (n >= 2)
        {
            translation(_ring[0]->plane.data(), _ring[1]->plane.data());
            for (size_t p = 0; p < px; ++p)
                _mot[p] = (_mot[p] << 1) | _motion[p];
        }
        else
            for (size_t p = 0; p < px; ++p)
                _mot[p] <<= 1;
    }

    /// section 4.3: _motion = pixels of `cur` changed by a translation from `prev`
    void translation(const uint8_t* cur, const uint8_t* prev)
    {
        const int R = kMotionRadius, T = kMotionTile;
        const int th = _h / T, tw = _w / T;
        const size_t px = static_cast<size_t>(_w) * _h;
        const int nshift = (2 * R + 1) * (2 * R + 1);
        // shifted[dx + R][y][x] = prev[y][(x - dx) mod W]
        _shifted.resize(static_cast<size_t>(2 * R + 1) * px);
        for (int dx = -R; dx <= R; ++dx)
        {
            uint8_t* dst = &_shifted[static_cast<size_t>(dx + R) * px];
            for (int y = 0; y < _h; ++y)
                copyShifted(dst + static_cast<size_t>(y) * _w, prev + static_cast<size_t>(y) * _w, _w, dx);
        }
        _cost.assign(static_cast<size_t>(nshift) * th * tw, 0);
        // SIMD-CANDIDATE(O-2): compare + count per tile, 289 shifts (vectorized byte loop, threads)
        parallelFor(nshift, _threads, [&](int s0, int s1) {
            for (int s = s0; s < s1; ++s)
            {
                const int dy = s / (2 * R + 1) - R, dx = s % (2 * R + 1) - R;
                const uint8_t* sh = &_shifted[static_cast<size_t>(dx + R) * px];
                int* cs = &_cost[static_cast<size_t>(s) * th * tw];
                for (int y = 0; y < th * T; ++y)
                {
                    const uint8_t* a = cur + static_cast<size_t>(y) * _w;
                    const uint8_t* b = sh + static_cast<size_t>(wrap(y - dy, _h)) * _w;
                    int* ct = cs + (y / T) * tw;
                    static_assert(kMotionTile == 32, "diff32 counts one 32-pixel tile row");
                    for (int tx = 0; tx < tw; ++tx)
                        ct[tx] += simd::diff32(a + tx * T, b + tx * T);
                }
            }
        });
        _motion.assign(px, 0);
        const int zero = (2 * R + 1) * R + R;
        for (int ty = 0; ty < th; ++ty)
            for (int tx = 0; tx < tw; ++tx)
            {
                const size_t t = static_cast<size_t>(ty) * tw + tx;
                int best = -1, bestCost = 0;
                for (int k = 0; k < nshift; ++k)
                {
                    if (k == zero)
                        continue;
                    const int c = _cost[static_cast<size_t>(k) * th * tw + t];
                    if (best < 0 || c < bestCost)
                    {
                        best = k;
                        bestCost = c;
                    }
                }
                const int c0 = _cost[static_cast<size_t>(zero) * th * tw + t];
                if (!(bestCost < 0.25 * c0 && c0 > 0.02 * T * T))
                    continue;
                const int dy = best / (2 * R + 1) - R, dx = best % (2 * R + 1) - R;
                const uint8_t* sh = &_shifted[static_cast<size_t>(dx + R) * px];
                for (int y = ty * T; y < (ty + 1) * T; ++y)
                {
                    const uint8_t* srow = sh + static_cast<size_t>(wrap(y - dy, _h)) * _w;
                    for (int x = tx * T; x < (tx + 1) * T; ++x)
                    {
                        const size_t p = static_cast<size_t>(y) * _w + x;
                        _motion[p] = cur[p] == srow[x] && cur[p] != prev[p];
                    }
                }
            }
    }

    // ---- section 5 ----------------------------------------------------------
    void pixelStage(int n, int L)
    {
        const size_t px = static_cast<size_t>(_w) * _h;
        _period.assign(px, 0);
        _start.assign(px, 0);
        const uint32_t* key[kDepth];
        const uint8_t* pl[kDepth];
        for (int i = 0; i < n; ++i)
        {
            key[i] = _ring[i]->key.data();
            pl[i] = _ring[i]->plane.data();
        }
        struct Run
        {
            int a, b;
            uint32_t eqMask, motMask, constMask;
        };
        Run runs[4][kMaxSpan];
        int nruns[4];
        for (int P = 2; P <= 5; ++P)
        {
            const int sp = span(P);
            nruns[P - 2] = 0;
            if (P > _maxPeriod)
                continue;
            for (int a = std::max(0, L - sp + 1); a <= std::min(L, n - sp); ++a)
            {
                const int b = std::max({std::min(L, a + sp - P), a, L - P + 1});
                // a series holding a whole-screen flash is no GigaScreen cycle (section 5)
                bool flat = false;
                for (int j = 0; j < P && _flatVeto; ++j)
                    flat |= _ring[b + j]->flat;
                if (flat)
                    continue;
                runs[P - 2][nruns[P - 2]++] = {a, b, ((1u << (sp - P)) - 1) << a, ((1u << (sp - 1)) - 1) << a,
                                               ((1u << (P - 1)) - 1) << a};
            }
        }
        // a pixel equal to its neighbor frame across the whole ring has no non-constant run
        const uint32_t staticMask = n >= 2 ? (1u << (n - 1)) - 1 : 0;
        // SIMD-CANDIDATE(O-1): the run tests are mask tests; the rest is rare
        parallelFor(_h, _threads, [&](int y0, int y1) {
        for (size_t p = static_cast<size_t>(y0) * _w; p < static_cast<size_t>(y1) * _w; ++p)
        {
            const uint32_t c1 = _c1[p];
            if ((c1 & staticMask) == staticMask)
                continue;
            for (int P = 2; P <= _maxPeriod; ++P)
            {
                const uint32_t eq = _eq[P - 2][p];
                bool found = false;
                for (int r = 0; r < nruns[P - 2]; ++r)
                {
                    const Run& run = runs[P - 2][r];
                    if ((eq & run.eqMask) != run.eqMask)
                        continue;
                    if ((c1 & run.constMask) == run.constMask)     // key(a) == ... == key(a + P - 1)
                        continue;
                    const float l0 = _pal.luma[pl[run.b][p]];
                    bool lumaOk = false;
                    for (int j = 1; j < P; ++j)
                        lumaOk |= _pal.luma[pl[run.b + j][p]] != l0;
                    if (!lumaOk)
                        continue;
                    if (P > 2 && (_mot[p] & run.motMask))
                        continue;
                    _period[p] = static_cast<uint8_t>(P);
                    _start[p] = static_cast<uint8_t>(run.b);
                    found = true;
                    break;
                }
                if (found)
                    break;
            }
        }
        });
    }

    static void unpack(uint32_t v, uint8_t* dst)
    {
        dst[0] = static_cast<uint8_t>(v);
        dst[1] = static_cast<uint8_t>(v >> 8);
        dst[2] = static_cast<uint8_t>(v >> 16);
    }

    void renderRaw(int L, RGBImage& out)
    {
        const uint8_t* t = _ring[L]->plane.data();
        const size_t px = static_cast<size_t>(_w) * _h;
        for (size_t p = 0; p < px; ++p)
            unpack(_mix->raw(t[p]), &out[p * 3]);
    }

    /// Stage 1 into `out`; explained = the recipe renders different from raw t.
    void renderStage1(int L, RGBImage& out)
    {
        const size_t px = static_cast<size_t>(_w) * _h;
        _explained.assign(px, 0);
        const uint8_t* t = _ring[L]->plane.data();
        // SIMD-CANDIDATE(O-3): table lookups
        for (size_t p = 0; p < px; ++p)
        {
            const int P = _period[p];
            const uint32_t raw = _mix->raw(t[p]);
            uint32_t v = raw;
            if (P)
            {
                // term order: t first, then the other series frames by ascending ring index
                int o[4], k = 0;
                const int b = _start[p];
                for (int j = 0; j < P; ++j)
                    if (b + j != L)
                        o[k++] = _ring[b + j]->plane[p];
                switch (P)
                {
                case 2: v = _mix->p2(t[p], o[0]); break;
                case 3: v = _mix->p3(t[p], o[0], o[1]); break;
                case 4: v = _mix->p4(t[p], o[0], o[1], o[2]); break;
                default: v = _mix->p5(t[p], o[0], o[1], o[2], o[3]); break;
                }
                _explained[p] = (v & 0xFFFFFF) != (raw & 0xFFFFFF);
            }
            unpack(v, &out[p * 3]);
        }
    }

    // ---- section 7 ----------------------------------------------------------
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

    std::vector<uint8_t> largeComponents(const std::vector<uint8_t>& mask, int minSize) const
    {
        std::vector<int> label(mask.size(), 0);
        std::vector<uint8_t> keep(mask.size(), 0);
        std::vector<int> stack, comp;
        int cur = 0;
        for (int s0 = 0; s0 < static_cast<int>(mask.size()); ++s0)
        {
            if (!mask[s0] || label[s0])
                continue;
            ++cur;
            stack.assign(1, s0);
            comp.clear();
            label[s0] = cur;
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
        std::vector<double> un(tiles, 0.0);
        const uint8_t* t = _ring[L]->plane.data();
        const uint8_t* pv = _ring[L + 1]->plane.data();
        for (int y = 0; y < _th * kFieldTile; ++y)
        {
            const size_t row = static_cast<size_t>(y) * _w;
            double* urow = &un[static_cast<size_t>(y / kFieldTile) * _tw];
            for (int x = 0; x < _tw * kFieldTile; ++x)
                urow[x / kFieldTile] += t[row + x] != pv[row + x] && !_explained[row + x];
        }
        for (double& v : un)
            v /= kFieldTile * kFieldTile;

        // no seeds outside the paper on horizontal-stripe tiles of frame t (7.3): color
        // changes between rows and none along a row - scrolling raster bars
        std::vector<uint8_t> stripe;
        if (_seedsBorderDetail)
        {
            std::vector<uint8_t> hch(tiles, 0), vch(tiles, 0);
            const uint8_t* tp = _ring[L]->plane.data();
            for (int y = 0; y < _th * kFieldTile; ++y)
            {
                const size_t row = static_cast<size_t>(y) * _w;
                for (int x = 0; x < _tw * kFieldTile; ++x)
                {
                    const size_t ti = static_cast<size_t>(y / kFieldTile) * _tw + x / kFieldTile;
                    if (x + 1 < _w && tp[row + x] != tp[row + x + 1])
                        hch[ti] = 1;
                    if (y + 1 < _h && tp[row + x] != tp[row + _w + x])
                        vch[ti] = 1;
                }
            }
            stripe.assign(tiles, 0);
            for (size_t s = 0; s < tiles; ++s)
                stripe[s] = !hch[s] && vch[s];
        }
        std::vector<uint8_t> cand(tiles, 0);
        for (size_t s = 0; s < tiles; ++s)
        {
            const bool c = _seeds[s] ? _alt[s] >= kFieldOff : (_alt[s] >= kFieldOn && un[s] >= kUnexplained);
            const bool paperTile = isPaperTile(static_cast<int>(s) / _tw, static_cast<int>(s) % _tw);
            cand[s] = c && _setAlt[s] && (!_seedsPaper || paperTile) &&
                      (!_seedsBorderDetail || paperTile || !stripe[s]);
        }
        _seeds = largeComponents(cand, kMinTiles);

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
        for (uint8_t v : _field)
            _anyFieldPixel |= v != 0;
    }

    /// section 7.7 with pre-shifted rows; writes the two-page mix into `out` on field pixels
    /// wholeFrameSteps (the scene stage of mod-tpgwafs, spec 7.8): every pixel, and per
    /// 8x8 block the other page from the neighbor across which t's own page did not
    /// jump (t == t-2: t-1; t == t+2: t+1; both: both) - twopage.other_page_steps
    void renderField(int L, RGBImage& out, bool wholeFrameSteps = false)
    {
        const uint8_t* prev = _ring[L + 1]->plane.data();
        const uint8_t* next = _ring[L - 1]->plane.data();
        const uint8_t* t = _ring[L]->plane.data();
        const int R = kBlockRadius, H = R / 2;               // ax, bx in -4..4
        const size_t px = static_cast<size_t>(_w) * _h;
        const int bh = _h / kBlock, bw = _w / kBlock;
        // prevSh[ax + H][y][x] = prev[y][(x - ax) mod W]; nextSh[bx + H][y][x] = next[y][(x + bx) mod W]
        _shifted.resize(static_cast<size_t>(2 * (2 * H + 1)) * px);
        uint8_t* prevSh = _shifted.data();
        uint8_t* nextSh = prevSh + static_cast<size_t>(2 * H + 1) * px;
        for (int s = -H; s <= H; ++s)
            for (int y = 0; y < _h; ++y)
            {
                const size_t row = static_cast<size_t>(y) * _w;
                copyShifted(prevSh + static_cast<size_t>(s + H) * px + row, prev + row, _w, s);
                copyShifted(nextSh + static_cast<size_t>(s + H) * px + row, next + row, _w, -s);
            }
        const size_t blocks = static_cast<size_t>(bh) * bw;
        const int nshift = (2 * R + 1) * (2 * R + 1);
        // per thread chunk of shifts: the first strict minimum in shift order
        const int chunks = std::max(1, std::min(_threads, nshift));
        std::vector<double> cbest(static_cast<size_t>(chunks) * blocks, 1e300);
        std::vector<int16_t> cshift(static_cast<size_t>(chunks) * blocks, 0);
        const int per = (nshift + chunks - 1) / chunks;
        // SIMD-CANDIDATE(O-4): compare + count per 8x8 block, 289 shifts (vectorized row diff, threads)
        parallelFor(chunks, chunks, [&](int c0, int c1) {
            std::vector<int> cnt(blocks);
            for (int c = c0; c < c1; ++c)
            {
                double* best = &cbest[static_cast<size_t>(c) * blocks];
                int16_t* shift = &cshift[static_cast<size_t>(c) * blocks];
                for (int s = c * per; s < std::min(nshift, (c + 1) * per); ++s)
                {
                    const int dy = s / (2 * R + 1) - R, dx = s % (2 * R + 1) - R;
                    const int ay = floorDiv2(dy), ax = floorDiv2(dx), by = dy - ay, bx = dx - ax;
                    const uint8_t* A = prevSh + static_cast<size_t>(ax + H) * px;
                    const uint8_t* B = nextSh + static_cast<size_t>(bx + H) * px;
                    std::fill(cnt.begin(), cnt.end(), 0);
                    for (int y = 0; y < bh * kBlock; ++y)
                    {
                        const uint8_t* ar = A + static_cast<size_t>(wrap(y - ay, _h)) * _w;
                        const uint8_t* br = B + static_cast<size_t>(wrap(y + by, _h)) * _w;
                        static_assert(kBlock == 8, "addDiffBlocks8 counts 8-pixel blocks");
                        simd::addDiffBlocks8(ar, br, bw, &cnt[static_cast<size_t>(y / kBlock) * bw]);
                    }
                    const double bias = 1e-3 * (std::abs(dy) + std::abs(dx));
                    for (size_t k = 0; k < blocks; ++k)
                    {
                        const double v = cnt[k] + bias;
                        if (v < best[k])
                        {
                            best[k] = v;
                            shift[k] = static_cast<int16_t>(s);
                        }
                    }
                }
            }
        });
        // merge the chunks in shift order (strict: the earliest minimum wins, as the reference)
        std::vector<int8_t> vy(blocks, 0), vx(blocks, 0);
        for (size_t k = 0; k < blocks; ++k)
        {
            double b = 1e300;
            int bs = 0;
            for (int c = 0; c < chunks; ++c)
                if (cbest[static_cast<size_t>(c) * blocks + k] < b)
                {
                    b = cbest[static_cast<size_t>(c) * blocks + k];
                    bs = cshift[static_cast<size_t>(c) * blocks + k];
                }
            vy[k] = static_cast<int8_t>(bs / (2 * R + 1) - R);
            vx[k] = static_cast<int8_t>(bs % (2 * R + 1) - R);
        }
        // page-flip blocks (whole-frame steps render): t's block equal to t-2 / t+2
        std::vector<uint8_t> back, fwd;
        if (wholeFrameSteps)
        {
            back = blockEqual(t, _ring[L + 2]->plane.data());
            fwd = blockEqual(t, _ring[L - 2]->plane.data());
        }
        for (int y = 0; y < _h; ++y)
            for (int x = 0; x < _w; ++x)
            {
                if (!wholeFrameSteps && !_field[static_cast<size_t>(y / kFieldTile) * _tw + x / kFieldTile])
                    continue;
                const size_t p = static_cast<size_t>(y) * _w + x;
                const size_t s = static_cast<size_t>(std::min(y / kBlock, bh - 1)) * bw + std::min(x / kBlock, bw - 1);
                const int ay = floorDiv2(vy[s]), ax = floorDiv2(vx[s]), by = vy[s] - ay, bx = vx[s] - ax;
                uint8_t fp = prevSh[static_cast<size_t>(ax + H) * px + static_cast<size_t>(wrap(y - ay, _h)) * _w + x];
                uint8_t fn = nextSh[static_cast<size_t>(bx + H) * px + static_cast<size_t>(wrap(y + by, _h)) * _w + x];
                if (wholeFrameSteps && y < bh * kBlock && x < bw * kBlock)
                {
                    const size_t bi = static_cast<size_t>(y / kBlock) * bw + x / kBlock;
                    const bool b = back[bi], f = fwd[bi];
                    if (b)                          // only back, or both
                    {
                        fp = prev[p];
                        fn = f ? next[p] : prev[p];
                    }
                    else if (f)                     // only forward
                    {
                        fp = next[p];
                        fn = next[p];
                    }
                }
                unpack(_mix->twoPage(t[p], fp, fn), &out[p * 3]);
            }
    }

    /// 8x8 blocks (bh x bw grid from pixel 0) where a and b are equal.
    std::vector<uint8_t> blockEqual(const uint8_t* a, const uint8_t* b) const
    {
        const int bh = _h / kBlock, bw = _w / kBlock;
        std::vector<uint8_t> eq(static_cast<size_t>(bh) * bw, 1);
        for (int y = 0; y < bh * kBlock; ++y)
        {
            const size_t row = static_cast<size_t>(y) * _w;
            for (int x = 0; x < bw * kBlock; ++x)
                if (a[row + x] != b[row + x])
                    eq[static_cast<size_t>(y / kBlock) * bw + x / kBlock] = 0;
        }
        return eq;
    }
};

}  // namespace

/// The mod-tpgw family (called by registry(): see registry.h).
void registerModTpgw(std::map<std::string, Factory>& registry)
{
    registry["mod-tpgw"] = [] { return std::make_unique<ModTpgw>(Variant{}); };
    registry["mod-tpgwa"] = [] {
        Variant v;
        v.name = "mod-tpgwa";
        v.sceneAverage = true;
        return std::make_unique<ModTpgw>(v);
    };
    // + no series with a whole-screen flash, periods 2..4, detailed object tiles >= 4 %
    registry["mod-tpgwaf"] = [] {
        Variant v;
        v.name = "mod-tpgwaf";
        v.sceneAverage = true;
        v.flatVeto = true;
        v.maxPeriod = 4;
        v.sceneObject = 0.04;
        v.sceneDetail = 0.1;
        return std::make_unique<ModTpgw>(v);
    };
    // + the scene stage renders the step-aware two-page mix (the accepted baseline, 2026-09-29)
    registry["mod-tpgwafs"] = [] {
        Variant v;
        v.name = "mod-tpgwafs";
        v.sceneAverage = true;
        v.flatVeto = true;
        v.maxPeriod = 4;
        v.sceneObject = 0.04;
        v.sceneDetail = 0.1;
        v.sceneSteps = true;
        return std::make_unique<ModTpgw>(v);
    };
    // + field seeds on the paper only: the hip-hop border's scrolling raster bars seeded a
    // field and the two-page render doubled them
    registry["mod-tpgwafsp"] = [] {
        Variant v;
        v.name = "mod-tpgwafsp";
        v.sceneAverage = true;
        v.flatVeto = true;
        v.maxPeriod = 4;
        v.sceneObject = 0.04;
        v.sceneDetail = 0.1;
        v.sceneSteps = true;
        v.seedsPaper = true;
        return std::make_unique<ModTpgw>(v);
    };
    // + instead: no field seeds off the paper on horizontal-stripe tiles (hip-hop's raster
    // bars lose theirs, the tunnel's border texture keeps its seeds)
    registry["mod-tpgwafsd"] = [] {
        Variant v;
        v.name = "mod-tpgwafsd";
        v.sceneAverage = true;
        v.flatVeto = true;
        v.maxPeriod = 4;
        v.sceneObject = 0.04;
        v.sceneDetail = 0.1;
        v.sceneSteps = true;
        v.seedsBorderDetail = true;
        return std::make_unique<ModTpgw>(v);
    };
}

}  // namespace zxdlss
