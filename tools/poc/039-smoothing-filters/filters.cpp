// POC 039: every 50 -> 60 Hz smoothing filter on the same frames, as 60 fps videos (README.md).
//
//   filters <seq.rgba> <width> <height> <count> <out dir> <rom 48.rom> [sourceHz] [displayHz]
//
// Writes <out dir>/<filter>.mp4 (3x nearest, the whole frame) and <out dir>/grid.mp4 (the filters side by side,
// labelled, 2x nearest) through ffmpeg. Shipped variants run through core/src/emulator/video/smoothmotion.cpp.
#include "emulator/video/smoothmotion.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace smoothmotion;

namespace
{
int g_width = 0;
int g_height = 0;
int g_count = 0;
std::vector<uint32_t> g_frames;
int64_t g_framePeriod = 20'000'000;
std::vector<uint8_t> g_rom;

const uint32_t* FrameAt(int k)
{
    k = std::clamp(k, 0, g_count - 1);
    return g_frames.data() + static_cast<size_t>(k) * g_width * g_height;
}
Picture PictureAt(int k)
{
    return {g_width, g_height, FrameAt(k)};
}
size_t Pixels()
{
    return static_cast<size_t>(g_width) * g_height;
}

/// A filter: the picture a refresh reaching the screen at t shows
struct Filter
{
    std::string name;
    std::function<void(int64_t t, uint32_t* out)> render;
    double ms = 0.0;
    int calls = 0;
};

/// The shipped engine, fed frames up to two periods ahead (the audio delay's lookahead)
struct Engine
{
    SmoothMotion sm;
    GameVideo game;
    int pushed = 0;
    Engine(Mode mode, GameMode gameMode, int period)
    {
        sm.SetMode(mode);
        if (gameMode != GameMode::Off)
        {
            game.picturePeriod = period;
            game.mode = gameMode;
            sm.SetGame(&game);
        }
    }
    void Render(int64_t t, uint32_t* out)
    {
        while (pushed < g_count && pushed * g_framePeriod <= t + 2 * g_framePeriod)
        {
            sm.Push(static_cast<uint64_t>(pushed) + 1, pushed * g_framePeriod, FrameAt(pushed), g_width, g_height);
            pushed++;
        }
        if (!sm.Draw(sm.Plan(t), out))
            std::memcpy(out, FrameAt(0), Pixels() * 4);
    }
};

/// The frame due at t and the weight of the next one
void DueAt(int64_t t, int& k, int& w)
{
    k = static_cast<int>(t / g_framePeriod);
    w = WeightAt(t, k * g_framePeriod, (k + 1) * g_framePeriod);
}

// sRGB <-> linear light, integer tables (16-bit linear)
uint16_t g_toLinear[256];
uint8_t g_toSrgb[4096];
void BuildGamma()
{
    for (int i = 0; i < 256; i++)
    {
        const double c = i / 255.0;
        const double l = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
        g_toLinear[i] = static_cast<uint16_t>(std::lround(l * 65535.0));
    }
    for (int i = 0; i < 4096; i++)
    {
        const double l = i / 4095.0;
        const double c = l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
        g_toSrgb[i] = static_cast<uint8_t>(std::clamp(std::lround(c * 255.0), 0L, 255L));
    }
}
uint32_t MixLinear(uint32_t a, uint32_t b, int w)
{
    if (a == b)
        return a;
    uint32_t r = a & 0xFF000000u;
    for (int s = 0; s < 24; s += 8)
    {
        const uint32_t la = g_toLinear[(a >> s) & 0xFF];
        const uint32_t lb = g_toLinear[(b >> s) & 0xFF];
        const uint32_t l = (la * static_cast<uint32_t>(kOne - w) + lb * static_cast<uint32_t>(w) + kOne / 2) >> 8;
        r |= static_cast<uint32_t>(g_toSrgb[l >> 4]) << s;
    }
    return r;
}

/// Mismatches of B against A moved by v, over the pixels that changed (every 2nd), rows y0..y1
int PanCost(const Picture& a, const Picture& b, int dx, int dy, int y0, int y1)
{
    int cost = 0;
    for (int y = y0; y < y1; y++)
        for (int x = (y & 1); x < a.width; x += 2)
        {
            const size_t p = static_cast<size_t>(y) * a.width + x;
            if (a.pixels[p] == b.pixels[p])
                continue;
            cost += a.At(x - dx, y - dy) != b.pixels[p];
        }
    return cost;
}
/// The vector that explains most of the change of rows y0..y1, zero when none beats staying
MotionField::Vector BestPan(const Picture& a, const Picture& b, int y0, int y1, int rangeX, int rangeY)
{
    MotionField::Vector best;
    int bestCost = PanCost(a, b, 0, 0, y0, y1);
    if (bestCost == 0)
        return best;
    for (int dy = -rangeY; dy <= rangeY; dy++)
        for (int dx = -rangeX; dx <= rangeX; dx++)
        {
            if (dx == 0 && dy == 0)
                continue;
            const int cost = PanCost(a, b, dx, dy, y0, y1);
            if (cost < bestCost)
            {
                bestCost = cost;
                best = {dx, dy};
            }
        }
    return best;
}

/// A field of one vector per row of blocks (`strips`) or one for all, composed by the shipped Compose
struct Pan
{
    bool strips = false;
    bool guard = false;  ///< blocks that do not change (with a margin around them) keep zero
    int cachedFrom = -1;
    MotionField field;
    void Prepare(int k)
    {
        if (cachedFrom == k)
            return;
        cachedFrom = k;
        const Picture a = PictureAt(k), b = PictureAt(k + 1);
        const int bw = (g_width + MotionField::kBlock - 1) / MotionField::kBlock;
        const int bh = (g_height + MotionField::kBlock - 1) / MotionField::kBlock;
        std::vector<MotionField::Block> blocks(static_cast<size_t>(bw) * bh);
        if (!strips)
        {
            const MotionField::Vector v = BestPan(a, b, 0, g_height, 16, 8);
            for (auto& block : blocks)
                block = {v, true};
        }
        else
            for (int j = 0; j < bh; j++)
            {
                const int y0 = j * MotionField::kBlock;
                const MotionField::Vector v =
                    BestPan(a, b, y0, std::min(y0 + MotionField::kBlock, g_height), 16, 4);
                for (int i = 0; i < bw; i++)
                    blocks[static_cast<size_t>(j) * bw + i] = {v, true};
            }
        if (guard)
            for (int j = 0; j < bh; j++)
                for (int i = 0; i < bw; i++)
                {
                    constexpr int kMargin = 4;
                    bool same = true;
                    for (int y = j * 8 - kMargin; same && y < j * 8 + 8 + kMargin; y++)
                        for (int x = i * 8 - kMargin; same && x < i * 8 + 8 + kMargin; x++)
                            same = a.At(x, y) == b.At(x, y);
                    if (same)
                        blocks[static_cast<size_t>(j) * bw + i] = {MotionField::Vector{}, true};
                }
        field.Assign(bw, bh, std::move(blocks), 16);
    }
};

/// The shipped field, then per row of blocks: the row's most common moving vector replaces a block's own wherever it
/// matches the block as well (a periodic pattern moving by its period also matches zero; the row's motion wins)
struct RowField
{
    int slack = 0;  ///< the row's vector also wins when it mismatches up to this many more pixels of the block
                    ///< (not over a block that does not change with kStillMargin around it: a static label stays)
    static constexpr int kStillMargin = 8;
    int cachedFrom = -1;
    MotionField field;
    void Prepare(int k)
    {
        if (cachedFrom == k)
            return;
        cachedFrom = k;
        const Picture a = PictureAt(k), b = PictureAt(k + 1);
        MotionField shipped;
        shipped.Estimate(a, b, MotionField::kRangeFrames);
        const int bw = shipped.BlocksWide(), bh = shipped.BlocksHigh();
        std::vector<MotionField::Block> blocks;
        for (int j = 0; j < bh; j++)
            for (int i = 0; i < bw; i++)
                blocks.push_back(shipped.At(i, j));
        const auto cost = [&](int i, int j, MotionField::Vector v) {
            const int sx = (kOne / 2 * v.x + kOne / 2) >> 8, sy = (kOne / 2 * v.y + kOne / 2) >> 8;
            int bad = 0;
            for (int y = j * 8; y < std::min(j * 8 + 8, g_height); y++)
                for (int x = i * 8; x < std::min(i * 8 + 8, g_width); x++)
                    bad += a.At(x - sx, y - sy) != b.At(x + v.x - sx, y + v.y - sy);
            return bad;
        };
        const auto still = [&](int i, int j) {
            for (int y = j * 8 - kStillMargin; y < j * 8 + 8 + kStillMargin; y++)
                for (int x = i * 8 - kStillMargin; x < i * 8 + 8 + kStillMargin; x++)
                    if (a.At(x, y) != b.At(x, y))
                        return false;
            return true;
        };
        for (int j = 0; j < bh; j++)
        {
            std::vector<MotionField::Block> row(blocks.begin() + j * bw, blocks.begin() + (j + 1) * bw);
            const std::vector<MotionField::Vector> common = MotionField::CommonVectors(row, 1, MotionField::kRangeFrames);
            if (common.empty())
                continue;
            for (int i = 0; i < bw; i++)
            {
                MotionField::Block& block = blocks[static_cast<size_t>(j) * bw + i];
                if (block.v == common[0])
                    continue;
                const int mine = block.matched ? cost(i, j, block.v) : MotionField::kAccept + 1;
                if (cost(i, j, common[0]) <= mine + (still(i, j) ? 0 : slack))
                    block = {common[0], true};
            }
        }
        field.Assign(bw, bh, std::move(blocks), MotionField::kRangeFrames);
    }
};

/// Block vectors by local consensus (iterated conditional modes): each block takes, of its own vector, its neighbours'
/// current ones, the field's common ones and zero, the one with the fewest mismatches over the block and 4 px around
/// it plus `lambda` per moving neighbour that holds another vector. Blocks that do not change with 4 px around them
/// hold zero and do not vote. Unlike a row's vector, the consensus is local: two objects moving apart in one row keep
/// their own motions, while a block whose own vector fits by accident (black between dots, a dot pitch) follows its
/// neighbours.
struct MrfField
{
    int lambda = 16;
    int margin = 4;  ///< the cost window: the block and this many px around it
    static constexpr int kSweeps = 4;
    int stillMargin = 4;  ///< a block that does not change with this many px around it holds zero
    int trust = 0;  ///< > 0: a block's vector must leave at most 1/trust of the window's changed pixels unexplained,
                    ///< else the block is unmatched (ComposeSolid shows the nearer frame there)
    int regional = 0;  ///< > 0: a connected moving region with at least 1/regional of its blocks untrusted shows
                       ///< the nearer frame whole (no seams between interpolated and fallback blocks)
    int core = 0;  ///< extra weight of the block's own pixels over the margin (a border block keeps its own motion)
    int runs = 0;  ///< > 0: a horizontal run of moving blocks with at least 1/runs untrusted shows the nearer frame
    bool linked = false;  ///< regions join only blocks whose vectors differ by at most 1 px (one rigid layer)
    int trustMargin = -1;  ///< the trust window margin, -1: the cost window
    bool layered = false;  ///< a pixel zero explains (a static layer over or under the motion) costs nothing
    int prior = 0;  ///< energy against leaving the field's most common vector where it explains 7/8 of the window
    bool inkFresh = false;  ///< the ink share judges only a vector the block did not have from k - 1 (a steady scroll keeps it)
    std::vector<MotionField::Vector> lastLabel;  ///< inkFresh: the labels of the field from k - 1
    int lastFrom = -2;
    bool inkTelling = false;  ///< the ink share counts only ink the vector's shift alters (not a stroke along it)
    int inkMoving = 0;  ///< > 0: the ink share also needs over 1/inkMoving of the ink in changing blocks broken (a still sprite over a scroll keeps the scroll's own ink)
    int inkDoubt = 10;  ///< inkSpread: only to a block breaking over 1/inkDoubt of its own ink share
    int inkSpread = 0;  ///< > 0: a block whose vector that many neighbours lost to the ink loses it too (a stroke it fits)
    int inkBacked = 0;  ///< inkImposed: the block's own estimate escapes the ink only with that many neighbours' own estimates equal
    int inkTrust = 0;  ///< > 0: the ink share's own bound (broken * inkTrust <= held), 0: trust
    int inkQuiet = 0;  ///< > 0: the ink share judges only blocks whose changes are at most 1/inkQuiet of the ink
    bool inkPooled = false;  ///< the ink share pools the changes with the ink: a busy block's changes outvote a frozen margin
    int inkUnheld = 0;  ///< > 0: the ink share skips blocks unchanged k - inkUnheld .. k + 1 (the composer holds them)
    bool inkJoined = false;  ///< the ink share counts ink joined to a change through same-colour ink (one glyph's static part)
    int inkNear = 0;  ///< inkLive: ink of unchanged blocks still counts that close to the block (glyph bottoms)
    int inkTouch = 0;  ///< > 0: the ink share counts ink within that many pixels of a change (one object partly moving)
    bool inkLive = false;  ///< the ink share counts blocks that change (a text line), not a frozen panel beside motion
    bool inkShare = false;  ///< unchanged ink judged by its own share, not added to the changes' misfits
    bool inkImposed = false;  ///< ink judges in the trust only labels differing from the block's own estimate
    int inkHistory = 0;  ///< ink: unchanged over that many frames before k too (0..2)
    bool inkCopied = false;  ///< ink costs only where copied from unchanged content in both frames
    int inkStage = 3;  ///< ink applies to: 1 the field's cost, 2 the trust, 3 both
    int inkReach = -1;  ///< ink: the margin around the block where unchanged ink costs, -1: the whole window
    bool ink = false;  ///< layered frees only unchanged paper (a block's most common colour): unchanged ink a vector
                       ///< breaks costs (static text under a scroller's label)
    bool tieCommon = false;  ///< equal energies: the vector more common in the field wins; sweeps alternate direction
    int cachedFrom = -1;
    MotionField field;
    void Prepare(int k)
    {
        if (cachedFrom == k)
            return;
        cachedFrom = k;
        using V = MotionField::Vector;
        const Picture a = PictureAt(k), b = PictureAt(k + 1);
        const Picture earlier[2] = {PictureAt(k - 1), PictureAt(k - 2)};
        MotionField shipped;
        shipped.Estimate(a, b, MotionField::kRangeFrames);
        const int bw = shipped.BlocksWide(), bh = shipped.BlocksHigh();
        const auto index = [bw](int i, int j) { return static_cast<size_t>(j) * bw + i; };
        std::vector<char> still(static_cast<size_t>(bw) * bh);
        for (int j = 0; j < bh; j++)
            for (int i = 0; i < bw; i++)
            {
                bool same = true;
                for (int y = j * 8 - stillMargin; same && y < j * 8 + 8 + stillMargin; y++)
                    for (int x = i * 8 - stillMargin; same && x < i * 8 + 8 + stillMargin; x++)
                        same = a.At(x, y) == b.At(x, y);
                still[index(i, j)] = same;
            }
        std::vector<uint32_t> paper(static_cast<size_t>(bw) * bh);
        std::vector<char> coreChanged(static_cast<size_t>(bw) * bh);  // inkLive: ink counts in changing blocks only
        for (int j = 0; j < bh; j++)
            for (int i = 0; i < bw; i++)
                for (int y = j * 8; !coreChanged[index(i, j)] && y < j * 8 + 8; y++)
                    for (int x = i * 8; x < i * 8 + 8; x++)
                        coreChanged[index(i, j)] |= a.At(x, y) != b.At(x, y);
        std::vector<char> heldAll(static_cast<size_t>(bw) * bh);
        if (inkUnheld)
            for (int j = 0; j < bh; j++)
                for (int i = 0; i < bw; i++)
                {
                    bool same = !coreChanged[index(i, j)];
                    for (int h = 1; same && h <= inkUnheld; h++)
                    {
                        const Picture e = PictureAt(k - h);
                        for (int y = j * 8; same && y < j * 8 + 8; y++)
                            for (int x = i * 8; same && x < i * 8 + 8; x++)
                                same = e.At(x, y) == a.At(x, y);
                    }
                    heldAll[index(i, j)] = same;
                }
        if (ink)
            for (int j = 0; j < bh; j++)
                for (int i = 0; i < bw; i++)
                {
                    std::pair<uint32_t, int> seen[64];
                    int kinds = 0, most = 0;
                    for (int y = j * 8; y < j * 8 + 8; y++)
                        for (int x = i * 8; x < i * 8 + 8; x++)
                        {
                            const uint32_t v = a.At(x, y);
                            int c = 0;
                            while (c < kinds && seen[c].first != v)
                                c++;
                            if (c == kinds)
                                seen[kinds++] = {v, 0};
                            if (++seen[c].second > most)
                            {
                                most = seen[c].second;
                                paper[index(i, j)] = v;
                            }
                        }
                }
        // a pixel no vector pays for: unchanged (and paper, near the block (bi, bj))
        // inkCopied: unchanged ink costs only where the vector draws it from unchanged content in both frames (static
        // text pasted over static text), not from a moving layer (a spinning dot lattice moved by its period)
        const auto exempt = [&](int x, int y, int bi, int bj, int stage, int ax, int ay, int bx, int by) {
            if (!layered || a.At(x, y) != b.At(x, y))
                return false;
            if (!ink || !(inkStage & stage) || (inkReach >= 0 && (x < bi * 8 - inkReach || x >= bi * 8 + 8 + inkReach ||
                                           y < bj * 8 - inkReach || y >= bj * 8 + 8 + inkReach)))
                return true;
            const int i = std::clamp(x >> 3, 0, bw - 1), j = std::clamp(y >> 3, 0, bh - 1);
            if (a.At(x, y) == paper[index(i, j)])
                return true;
            for (int h = 0; h < inkHistory; h++)  // a texture's chance agreement is not a static layer
                if (earlier[h].At(x, y) != a.At(x, y))
                    return true;
            return inkCopied && (a.At(ax, ay) != b.At(ax, ay) || a.At(bx, by) != b.At(bx, by));
        };
        std::vector<std::vector<std::pair<V, int>>> memo(static_cast<size_t>(bw) * bh);
        const auto cost = [&](int i, int j, V v) {
            auto& known = memo[index(i, j)];
            for (const auto& [kv, c] : known)
                if (kv == v)
                    return c;
            const int sx = (kOne / 2 * v.x + kOne / 2) >> 8, sy = (kOne / 2 * v.y + kOne / 2) >> 8;
            int bad = 0;
            for (int y = j * 8 - margin; y < j * 8 + 8 + margin; y++)
                for (int x = i * 8 - margin; x < i * 8 + 8 + margin; x++)
                    if (a.At(x - sx, y - sy) != b.At(x + v.x - sx, y + v.y - sy) && !exempt(x, y, i, j, 1, x - sx, y - sy, x + v.x - sx, y + v.y - sy))
                        bad += 1 + (x >= i * 8 && x < i * 8 + 8 && y >= j * 8 && y < j * 8 + 8 ? core : 0);
            known.push_back({v, bad});
            return bad;
        };
        std::vector<V> label(static_cast<size_t>(bw) * bh);
        for (int j = 0; j < bh; j++)
            for (int i = 0; i < bw; i++)
                label[index(i, j)] = still[index(i, j)] ? V{} : shipped.At(i, j).v;
        const std::vector<V>& common = shipped.Common();
        const auto rank = [&](V v) {
            if (!tieCommon)
                return 0;
            for (size_t c = 0; c < common.size(); c++)
                if (common[c] == v)
                    return static_cast<int>(c);
            return static_cast<int>(common.size());
        };
        if (tieCommon)  // start from the cheapest of own and common, the more common of equals
            for (int j = 0; j < bh; j++)
                for (int i = 0; i < bw; i++)
                {
                    if (still[index(i, j)])
                        continue;
                    V best = shipped.At(i, j).v;
                    for (const V& v : common)
                        if (const int c = cost(i, j, v), cb = cost(i, j, best);
                            c < cb || (c == cb && rank(v) < rank(best)))
                            best = v;
                    label[index(i, j)] = best;
                }
        for (int sweep = 0; sweep < kSweeps; sweep++)
            for (int jj = 0; jj < bh; jj++)
                for (int ii = 0; ii < bw; ii++)
                {
                    const bool back = tieCommon && (sweep & 1);
                    const int i = back ? bw - 1 - ii : ii, j = back ? bh - 1 - jj : jj;
                    if (still[index(i, j)])
                        continue;
                    V cands[16];
                    int n = 0;
                    const auto add = [&](V v) {
                        for (int c = 0; c < n; c++)
                            if (cands[c] == v)
                                return;
                        if (n < 16)
                            cands[n++] = v;
                    };
                    add(label[index(i, j)]);
                    add(shipped.At(i, j).v);
                    for (int dj = -1; dj <= 1; dj++)
                        for (int di = -1; di <= 1; di++)
                        {
                            const int ni = i + di, nj = j + dj;
                            if ((di || dj) && ni >= 0 && nj >= 0 && ni < bw && nj < bh && !still[index(ni, nj)])
                                add(label[index(ni, nj)]);
                        }
                    for (const V& v : shipped.Common())
                        add(v);
                    add(V{});
                    int best = 0, bestEnergy = 0;
                    for (int c = 0; c < n; c++)
                    {
                        int energy = cost(i, j, cands[c]);
                        for (int dj = -1; dj <= 1; dj++)
                            for (int di = -1; di <= 1; di++)
                            {
                                const int ni = i + di, nj = j + dj;
                                if ((di || dj) && ni >= 0 && nj >= 0 && ni < bw && nj < bh && !still[index(ni, nj)] &&
                                    !(label[index(ni, nj)] == cands[c]))
                                    energy += lambda;
                            }
                        if (prior && !common.empty() && !(cands[c] == common[0]))
                        {
                            const int side = 8 + 2 * margin;
                            if (cost(i, j, common[0]) * 8 <= side * side)
                                energy += prior;
                        }
                        energy = energy * 8 + rank(cands[c]);
                        if (c == 0 || energy < bestEnergy)
                        {
                            best = c;
                            bestEnergy = energy;
                        }
                    }
                    label[index(i, j)] = cands[best];
                }
        if (const char* dump = std::getenv("MRF_DUMP"); dump && std::atoi(dump) == k)
            for (int j = 0; j < bh; j++)
            {
                std::fprintf(stderr, "%3d:", j);
                for (int i = 0; i < bw; i++)
                {
                    const V v = label[index(i, j)], o = shipped.At(i, j).v;
                    std::fprintf(stderr, still[index(i, j)] ? "    .    " : " %3d,%-2d%c", v.x, v.y,
                                 o == v ? ' ' : '*');
                }
                std::fprintf(stderr, "\n");
            }
        std::vector<MotionField::Block> blocks(static_cast<size_t>(bw) * bh);
        std::vector<char> inkLost(static_cast<size_t>(bw) * bh);  // inkSpread: rejected by the ink share alone
        std::vector<char> doubt(static_cast<size_t>(bw) * bh);  // inkSpread: judged by the ink, over 1/10 broken
        for (int j = 0; j < bh; j++)
            for (int i = 0; i < bw; i++)
            {
                const size_t q = index(i, j);
                bool ok = true;
                if (trust && !still[q] && !(label[q] == V{}))
                {
                    int changed = 0, bad = 0, held = 0, broken = 0, liveHeld = 0, liveBroken = 0;
                    const int tm = trustMargin < 0 ? margin : trustMargin;
                    const V v = label[q];
                    // inkImposed: unchanged ink judges only a label the block's own search did not pick (a
                    // neighbour's motion spread over it by the smoothness)
                    bool imposed = !inkImposed || !(shipped.At(i, j).v == v);
                    if (!imposed && inkBacked)  // an estimate of its own no neighbour shares: aliasing on a lattice
                    {
                        int backed = 0;
                        for (int dj = -1; dj <= 1; dj++)
                            for (int di = -1; di <= 1; di++)
                                backed += (di || dj) && i + di >= 0 && j + dj >= 0 && i + di < bw && j + dj < bh &&
                                          shipped.At(i + di, j + dj).v == v;
                        imposed = backed < inkBacked;
                    }
                    const int sx = (kOne / 2 * v.x + kOne / 2) >> 8, sy = (kOne / 2 * v.y + kOne / 2) >> 8;
                    const int side = 8 + 2 * tm, x0 = i * 8 - tm, y0 = j * 8 - tm;
                    std::vector<char> joined;
                    if (inkJoined && inkShare)
                    {
                        // flood from the changed pixels through unchanged ink of the same colour
                        joined.assign(static_cast<size_t>(side) * side, 0);
                        std::vector<int> queue;
                        for (int y = y0; y < y0 + side; y++)
                            for (int x = x0; x < x0 + side; x++)
                                if (a.At(x, y) != b.At(x, y))
                                {
                                    joined[static_cast<size_t>(y - y0) * side + (x - x0)] = 1;
                                    queue.push_back((y - y0) * side + (x - x0));
                                }
                        for (size_t n = 0; n < queue.size(); n++)
                        {
                            const int cx = x0 + queue[n] % side, cy = y0 + queue[n] / side;
                            for (int dy = -1; dy <= 1; dy++)
                                for (int dx = -1; dx <= 1; dx++)
                                {
                                    const int nx = cx + dx, ny = cy + dy;
                                    if (nx < x0 || ny < y0 || nx >= x0 + side || ny >= y0 + side)
                                        continue;
                                    char& m = joined[static_cast<size_t>(ny - y0) * side + (nx - x0)];
                                    const uint32_t c = a.At(nx, ny);
                                    if (m || c != b.At(nx, ny) || (c != a.At(cx, cy) && c != b.At(cx, cy)) ||
                                        exempt(nx, ny, i, j, 2, nx, ny, nx, ny))
                                        continue;
                                    m = 1;
                                    queue.push_back((ny - y0) * side + (nx - x0));
                                }
                        }
                    }
                    for (int y = j * 8 - tm; y < j * 8 + 8 + tm; y++)
                        for (int x = i * 8 - tm; x < i * 8 + 8 + tm; x++)
                        {
                            changed += a.At(x, y) != b.At(x, y);
                            const bool misfit = a.At(x - sx, y - sy) != b.At(x + v.x - sx, y + v.y - sy);
                            if (inkShare && a.At(x, y) == b.At(x, y))  // unchanged ink: its own share, below
                            {
                                const bool counted =
                                    !exempt(x, y, i, j, imposed ? 2 : 0, x - sx, y - sy, x + v.x - sx, y + v.y - sy) &&
                                    (!inkLive || ((x >> 3) == i && (y >> 3) == j) ||
                                     (x >= i * 8 - inkNear && x < i * 8 + 8 + inkNear && y >= j * 8 - inkNear && y < j * 8 + 8 + inkNear) ||
                                     coreChanged[index(std::clamp(x >> 3, 0, bw - 1), std::clamp(y >> 3, 0, bh - 1))]) &&
                                    !heldAll[index(std::clamp(x >> 3, 0, bw - 1), std::clamp(y >> 3, 0, bh - 1))] &&
                                    (!inkTelling || a.At(x - v.x, y - v.y) != a.At(x, y) || a.At(x + v.x, y + v.y) != a.At(x, y)) &&
                                    (joined.empty() || joined[static_cast<size_t>(y - y0) * side + (x - x0)]) &&
                                    (!inkTouch || [&] {
                                        for (int ty = y - inkTouch; ty <= y + inkTouch; ty++)
                                            for (int tx = x - inkTouch; tx <= x + inkTouch; tx++)
                                                if (a.At(tx, ty) != b.At(tx, ty))
                                                    return true;
                                        return false;
                                    }());
                                held += counted;
                                broken += counted && misfit;
                                const bool live = coreChanged[index(std::clamp(x >> 3, 0, bw - 1), std::clamp(y >> 3, 0, bh - 1))];
                                liveHeld += counted && live;
                                liveBroken += counted && live && misfit;
                                continue;
                            }
                            bad += misfit && !exempt(x, y, i, j, imposed ? 2 : 0, x - sx, y - sy, x + v.x - sx, y + v.y - sy);
                        }
                    // inkShare: the vector keeps 3/4 of the unchanged ink as well as it explains 3/4 of the changes
                    const bool steady = inkFresh && lastFrom == k - 1 && lastLabel.size() == label.size() && lastLabel[q] == v;
                    inkLost[q] = bad * trust <= changed;
                    doubt[q] = imposed && inkShare && broken * inkDoubt > held;
                    ok = bad * trust <= changed && (steady || (inkQuiet && changed * inkQuiet > held) || (inkPooled ? (bad + broken) * trust <= changed + held : broken * (inkTrust ? inkTrust : trust) <= held || (inkMoving && liveHeld >= 16 && liveBroken * inkMoving <= liveHeld)));
                    if (const char* dump = std::getenv("SHARE_ALL"); dump && (std::atoi(dump) < 0 || std::atoi(dump) == k) && bad * trust <= changed && !ok)
                    {
                        // the share's ink split: own core, live neighbours, static neighbours near/far, touching a change
                        int h[5] = {}, br[5] = {};
                        for (int y = j * 8 - tm; y < j * 8 + 8 + tm; y++)
                            for (int x = i * 8 - tm; x < i * 8 + 8 + tm; x++)
                            {
                                if (a.At(x, y) != b.At(x, y) || exempt(x, y, i, j, imposed ? 2 : 0, x - sx, y - sy, x + v.x - sx, y + v.y - sy))
                                    continue;
                                const bool mis = a.At(x - sx, y - sy) != b.At(x + v.x - sx, y + v.y - sy);
                                const bool own = (x >> 3) == i && (y >> 3) == j;
                                const bool live = coreChanged[index(std::clamp(x >> 3, 0, bw - 1), std::clamp(y >> 3, 0, bh - 1))];
                                const bool near = x >= i * 8 - 3 && x < i * 8 + 11 && y >= j * 8 - 3 && y < j * 8 + 11;
                                const int c = own ? 0 : live ? 1 : near ? 2 : 3;
                                h[c]++, br[c] += mis;
                                bool touch = false;
                                for (int ty = y - 2; ty <= y + 2; ty++)
                                    for (int tx = x - 2; tx <= x + 2; tx++)
                                        touch |= a.At(tx, ty) != b.At(tx, ty);
                                h[4] += touch, br[4] += touch && mis;
                            }
                        std::fprintf(stderr, "A k%d b%d,%d v%d,%d ch%d bad%d own%d/%d live%d/%d near%d/%d far%d/%d touch%d/%d\n", k, i, j, v.x, v.y, changed, bad,
                                     br[0], h[0], br[1], h[1], br[2], h[2], br[3], h[3], br[4], h[4]);
                    }
                    if (const char* dump = std::getenv("SHARE_DUMP"); dump && std::atoi(dump) == k && j >= 19 && j <= 22 && i >= 14 && i <= 30)
                        std::fprintf(stderr, "S b%d,%d v%d,%d own%d,%d changed%d bad%d held%d broken%d ok%d\n", i, j, v.x, v.y, shipped.At(i, j).v.x, shipped.At(i, j).v.y, changed, bad, held, broken, ok);
                    if (const char* dump = std::getenv("TRUST_DUMP"); dump && std::atoi(dump) == k && j >= 22 && j <= 27 && i >= 12 && i <= 30)
                    {
                        int old = 0, inkBad = 0, staticInk = 0;
                        for (int y = j * 8 - tm; y < j * 8 + 8 + tm; y++)
                            for (int x = i * 8 - tm; x < i * 8 + 8 + tm; x++)
                            {
                                const bool mis = a.At(x - sx, y - sy) != b.At(x + v.x - sx, y + v.y - sy);
                                const bool same = a.At(x, y) == b.At(x, y);
                                old += mis && !same;
                                const bool inkPx = same && !exempt(x, y, i, j, 2, x - sx, y - sy, x + v.x - sx, y + v.y - sy);
                                staticInk += inkPx || (same && ink && a.At(x, y) != paper[index(std::clamp(x >> 3, 0, bw - 1), std::clamp(y >> 3, 0, bh - 1))]);
                                inkBad += mis && inkPx;
                            }
                        std::fprintf(stderr, "T b%d,%d v%d,%d changed%d old%d ink%d staticInk%d ok%d\n", i, j, v.x, v.y, changed, old, inkBad, staticInk, ok);
                    }
                }
                inkLost[q] = inkLost[q] && !ok;
                blocks[q] = {label[q], ok};
            }
        if (inkSpread)
        {
            std::vector<char> lose(blocks.size());
            for (int j = 0; j < bh; j++)
                for (int i = 0; i < bw; i++)
                {
                    int lost = 0;
                    for (int dj = -1; dj <= 1; dj++)
                        for (int di = -1; di <= 1; di++)
                            lost += (di || dj) && i + di >= 0 && j + dj >= 0 && i + di < bw && j + dj < bh &&
                                    inkLost[index(i + di, j + dj)] && label[index(i + di, j + dj)] == label[index(i, j)];
                    lose[index(i, j)] = blocks[index(i, j)].matched && !still[index(i, j)] && lost >= inkSpread && doubt[index(i, j)];
                }
            for (size_t q = 0; q < blocks.size(); q++)
                blocks[q].matched = blocks[q].matched && !lose[q];
        }
        lastLabel = label;
        lastFrom = k;
        if (runs)
            for (int j = 0; j < bh; j++)
                for (int i0 = 0; i0 < bw;)
                {
                    if (still[index(i0, j)])
                    {
                        i0++;
                        continue;
                    }
                    int i1 = i0, failed = 0;
                    while (i1 < bw && !still[index(i1, j)])
                        failed += !blocks[index(i1++, j)].matched;
                    if (failed && failed * runs >= i1 - i0)
                        for (int i = i0; i < i1; i++)
                            blocks[index(i, j)].matched = false;
                    i0 = i1;
                }
        if (regional)
        {
            std::vector<int> region(blocks.size(), -1);
            for (size_t seed = 0; seed < blocks.size(); seed++)
            {
                if (still[seed] || region[seed] >= 0)
                    continue;
                std::vector<size_t> members{seed};
                region[seed] = static_cast<int>(seed);
                int failed = 0;
                for (size_t m = 0; m < members.size(); m++)
                {
                    const int i = static_cast<int>(members[m] % bw), j = static_cast<int>(members[m] / bw);
                    failed += !blocks[members[m]].matched;
                    for (int dj = -1; dj <= 1; dj++)
                        for (int di = -1; di <= 1; di++)
                        {
                            const int ni = i + di, nj = j + dj;
                            if (ni < 0 || nj < 0 || ni >= bw || nj >= bh)
                                continue;
                            const size_t nq = index(ni, nj);
                            const V d{label[nq].x - label[members[m]].x, label[nq].y - label[members[m]].y};
                            if (!still[nq] && region[nq] < 0 &&
                                (!linked || (std::abs(d.x) <= 1 && std::abs(d.y) <= 1)))
                            {
                                region[nq] = static_cast<int>(seed);
                                members.push_back(nq);
                            }
                        }
                }
                if (const char* dump = std::getenv("MRF_DUMP"); dump && std::atoi(dump) == k)
                    std::fprintf(stderr, "region %zu: %zu blocks, %d failed\n", seed, members.size(), failed);
                if (failed && failed * regional >= static_cast<int>(members.size()))
                    for (const size_t m : members)
                        blocks[m].matched = false;
            }
        }
        field.Assign(bw, bh, std::move(blocks), MotionField::kRangeFrames);
    }
};

/// Compose for content that changes shape while it moves (Aleste's spinning dots): a moving block never takes zero
/// (black agrees with black between the dots and punched holes into them), and a pixel no candidate explains is drawn
/// whole from the nearer frame along the block's vector instead of blended. Static and unmatched blocks as shipped,
/// their fallback the narrowed blend.
void ComposeSolid(const MotionField& f, const Picture& a, const Picture& b, int w, uint32_t* out, bool zeroLast = false,
                  bool local = false, bool refine = false, bool gate = false, bool informative = false,
                  int zeroHandicap = 3, const Picture* before = nullptr, int history = 1,
                  bool nearTie = false, bool coreStill = false, bool pixelStill = false, bool heldBlock = false, int stray = 0, bool unit = false, int strayHistory = 0, bool strayInk = false)
{
    constexpr int radius = 1;
    using V = MotionField::Vector;
    const auto shift = [&](int c) { return (w * c + kOne / 2) >> 8; };
    const int narrow = std::clamp((w - kOne / 4) * 2, 0, kOne);
    for (int j = 0; j < f.BlocksHigh(); j++)
        for (int i = 0; i < f.BlocksWide(); i++)
        {
            const MotionField::Block& own = f.At(i, j);
            const bool moving = own.matched && !(own.v == V{});
            // the block's changes explained by a pixel's motion as well as by any vector around: no position lies
            // between the frames (a glyph stretched by a row, dashes marching under a wide neighbour's label), the
            // nearer frame
            bool small = false;
            if (unit && own.matched)
            {
                const auto explained = [&](V v) {
                    const int sx = (kOne / 2 * v.x + kOne / 2) >> 8, sy = (kOne / 2 * v.y + kOne / 2) >> 8;
                    int fit = 0;
                    for (int y = j * MotionField::kBlock; y < std::min((j + 1) * MotionField::kBlock, a.height); y++)
                        for (int x = i * MotionField::kBlock; x < std::min((i + 1) * MotionField::kBlock, a.width); x++)
                            fit += a.At(x, y) != b.At(x, y) && a.At(x - sx, y - sy) == b.At(x + v.x - sx, y + v.y - sy);
                    return fit;
                };
                int wide = 0;
                for (int dj = -1; dj <= 1; dj++)
                    for (int di = -1; di <= 1; di++)
                    {
                        const int bi = i + di, bj = j + dj;
                        if (bi >= 0 && bj >= 0 && bi < f.BlocksWide() && bj < f.BlocksHigh() && f.At(bi, bj).matched)
                            wide = std::max(wide, explained(f.At(bi, bj).v));
                    }
                int changed = 0;
                for (int y = j * MotionField::kBlock; y < std::min((j + 1) * MotionField::kBlock, a.height); y++)
                    for (int x = i * MotionField::kBlock; x < std::min((i + 1) * MotionField::kBlock, a.width); x++)
                        changed += a.At(x, y) != b.At(x, y);
                for (int dy = -1; !small && dy <= 1; dy++)
                    for (int dx = -1; !small && dx <= 1; dx++)
                    {
                        const int fit = explained(V{dx, dy});
                        small = changed && fit >= wide && fit * 4 >= changed * 3;
                    }
            }
            if ((zeroLast && !own.matched) || small)  // motion that no vector explains: the nearer frame, whole
            {
                const Picture& near = w < kOne / 2 ? a : b;
                for (int y = j * MotionField::kBlock; y < std::min((j + 1) * MotionField::kBlock, a.height); y++)
                    for (int x = i * MotionField::kBlock; x < std::min((i + 1) * MotionField::kBlock, a.width); x++)
                        out[static_cast<size_t>(y) * a.width + x] = near.pixels[static_cast<size_t>(y) * a.width + x];
                continue;
            }
            V cands[32];
            int n = 0;
            const auto add = [&](V v) {
                if (moving && v == V{} && (zeroLast ? 0 : 1))
                    return;
                for (int c = 0; c < n; c++)
                    if (cands[c] == v)
                        return;
                if (n < 32)
                    cands[n++] = v;
            };
            const auto addBlock = [&](int bi, int bj) {
                if (bi >= 0 && bj >= 0 && bi < f.BlocksWide() && bj < f.BlocksHigh() && f.At(bi, bj).matched)
                    add(f.At(bi, bj).v);
            };
            addBlock(i, j);
            add(V{});
            for (int dj = -1; dj <= 1; dj++)
                for (int di = -1; di <= 1; di++)
                    if (di || dj)
                        addBlock(i + di, j + dj);
            if (refine && moving)  // a layer moving a pixel off the block's vector (marching dashes over bouncing text)
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        add(V{own.v.x + dx, own.v.y + dy});
            if (!local)
                for (const V& v : f.Common())
                    add(v);
            if (zeroLast && moving)  // the static layer: wins only where no motion explains the pixel as well
            {
                for (int c = 0; c < n; c++)
                    if (cands[c] == V{})
                        std::rotate(cands + c, cands + c + 1, cands + n);
                bool has = false;
                for (int c = 0; c < n; c++)
                    has |= cands[c] == V{};
                if (!has && n < 32)
                    cands[n++] = V{};
            }
            if (gate && moving)  // a neighbour's vector only where it explains this block's changes about as well
            {
                const auto coreCost = [&](V v) {
                    const int sx = (kOne / 2 * v.x + kOne / 2) >> 8, sy = (kOne / 2 * v.y + kOne / 2) >> 8;
                    int bad = 0;
                    for (int y = j * MotionField::kBlock; y < (j + 1) * MotionField::kBlock; y++)
                        for (int x = i * MotionField::kBlock; x < (i + 1) * MotionField::kBlock; x++)
                            bad += a.At(x, y) != b.At(x, y) && a.At(x - sx, y - sy) != b.At(x + v.x - sx, y + v.y - sy);
                    return bad;
                };
                const int ownCost = coreCost(own.v);
                int kept = 0;
                for (int c = 0; c < n; c++)
                    if (cands[c] == V{} || cands[c] == own.v || coreCost(cands[c]) <= ownCost + 4 + ownCost / 2)
                        cands[kept++] = cands[c];
                n = kept;
            }
            const int x1 = std::min((i + 1) * MotionField::kBlock, a.width);
            const int y1 = std::min((j + 1) * MotionField::kBlock, a.height);
            bool still = false;  // the block itself unchanged and its vector misfits it: zero unhandicapped, wins ties
            if (coreStill && moving)
            {
                still = true;
                for (int y = j * MotionField::kBlock; still && y < y1; y++)
                    for (int x = i * MotionField::kBlock; still && x < x1; x++)
                        still = a.At(x, y) == b.At(x, y);
                // ... and the block's vector does not explain it too (a lattice moved by its period looks still)
                const int sx = (kOne / 2 * own.v.x + kOne / 2) >> 8, sy = (kOne / 2 * own.v.y + kOne / 2) >> 8;
                int misfit = 0;
                for (int y = j * MotionField::kBlock; still && y < y1; y++)
                    for (int x = i * MotionField::kBlock; x < x1; x++)
                        misfit += a.At(x - sx, y - sy) != b.At(x + own.v.x - sx, y + own.v.y - sy);
                still = still && misfit > 0;
            }
            int staticMisfit = 0;  // the block's unchanged pixels its vector does not explain: a static layer under it
            uint32_t paper = 0;    // strayInk: the block's most common colour, which a moving layer's gaps share
            if (stray && moving)
            {
                if (strayInk)
                {
                    std::pair<uint32_t, int> seen[64];
                    int kinds = 0, most = 0;
                    for (int y = j * MotionField::kBlock; y < y1; y++)
                        for (int x = i * MotionField::kBlock; x < x1; x++)
                        {
                            const uint32_t v = a.At(x, y);
                            int c = 0;
                            while (c < kinds && seen[c].first != v)
                                c++;
                            if (c == kinds)
                                seen[kinds++] = {v, 0};
                            if (++seen[c].second > most)
                            {
                                most = seen[c].second;
                                paper = v;
                            }
                        }
                }
                const int sx = shift(own.v.x), sy = shift(own.v.y);
                for (int y = j * MotionField::kBlock; y < y1; y++)
                    for (int x = i * MotionField::kBlock; x < x1; x++)
                    {
                        bool same = a.At(x, y) == b.At(x, y) && !(strayInk && a.At(x, y) == paper);
                        for (int h = 0; same && h < strayHistory; h++)
                            same = before[-h].At(x, y) == a.At(x, y);
                        staticMisfit += same && a.At(x - sx, y - sy) != b.At(x + own.v.x - sx, y + own.v.y - sy);
                    }
            }
            bool blockHeld = before != nullptr && moving;  // the whole block unchanged history + 2 frames
            for (int y = j * MotionField::kBlock; heldBlock && blockHeld && y < y1; y++)
                for (int x = i * MotionField::kBlock; blockHeld && x < x1; x++)
                {
                    const uint32_t v = a.At(x, y);
                    blockHeld = v == b.At(x, y);
                    for (int h = 0; blockHeld && h < history; h++)
                        blockHeld = before[-h].At(x, y) == v;
                }
            uint32_t background = 0;  // the block's most common colour in A: matching it proves little
            if (informative)
            {
                std::pair<uint32_t, int> seen[64];
                int kinds = 0, most = 0;
                for (int y = j * MotionField::kBlock; y < y1; y++)
                    for (int x = i * MotionField::kBlock; x < x1; x++)
                    {
                        const uint32_t v = a.At(x, y);
                        int c = 0;
                        while (c < kinds && seen[c].first != v)
                            c++;
                        if (c == kinds)
                            seen[kinds++] = {v, 0};
                        if (++seen[c].second > most)
                        {
                            most = seen[c].second;
                            background = v;
                        }
                    }
            }
            const int weight = informative ? 2 : 1;
            for (int y = j * MotionField::kBlock; y < y1; y++)
                for (int x = i * MotionField::kBlock; x < x1; x++)
                {
                    const size_t p = static_cast<size_t>(y) * a.width + x;
                    uint32_t pixel = 0;
                    int bestScore = 0, bestTie = 0;
                    // the 3x3 unchanged and the block's vector misfits it: a static layer here, zero unhandicapped
                    // and winning ties (a lattice moved by its period fits its vector and keeps the handicap)
                    bool here = still;
                    if (stray && staticMisfit >= stray && !here)  // inside that layer: zero unhandicapped, wins ties
                    {
                        here = true;
                        bool ink = !strayInk;
                        for (int dy = -1; here && dy <= 1; dy++)
                            for (int dx = -1; here && dx <= 1; dx++)
                            {
                                here = a.At(x + dx, y + dy) == b.At(x + dx, y + dy);
                                for (int h = 0; here && h < strayHistory; h++)
                                    here = before[-h].At(x + dx, y + dy) == a.At(x + dx, y + dy);
                                ink |= a.At(x + dx, y + dy) != paper;
                            }
                        here = here && ink;
                    }
                    if (pixelStill && moving && !here)
                    {
                        const int sx = shift(own.v.x), sy = shift(own.v.y);
                        bool same = true, fits = true;
                        for (int dy = -1; dy <= 1; dy++)
                            for (int dx = -1; dx <= 1; dx++)
                            {
                                same = same && a.At(x + dx, y + dy) == b.At(x + dx, y + dy);
                                fits = fits && a.At(x + dx - sx, y + dy - sy) ==
                                                   b.At(x + dx + own.v.x - sx, y + dy + own.v.y - sy);
                            }
                        here = same && !fits;
                    }
                    for (int c = 0; c < n; c++)
                    {
                        const int sx = shift(cands[c].x), sy = shift(cands[c].y);
                        const int ax = x - sx, ay = y - sy, bx = x + cands[c].x - sx, by = y + cands[c].y - sy;
                        const uint32_t va = a.At(ax, ay);
                        if (va != b.At(bx, by))
                            continue;
                        int score = 0;
                        for (int dy = -radius; dy <= radius; dy++)
                            for (int dx = -radius; dx <= radius; dx++)
                            {
                                const uint32_t pa = a.At(ax + dx, ay + dy);
                                if (pa == b.At(bx + dx, by + dy))
                                    score += informative ? 1 + (pa != background) : 1;
                            }
                        if (zeroLast && moving && cands[c] == V{})
                        {
                            bool held = heldBlock ? blockHeld : before != nullptr;  // the 3x3 unchanged for history + 2 frames: a static layer
                            for (int dy = -1; !heldBlock && held && dy <= 1; dy++)
                                for (int dx = -1; held && dx <= 1; dx++)
                                {
                                    const uint32_t v = a.At(x + dx, y + dy);
                                    held = v == b.At(x + dx, y + dy);
                                    for (int h = 0; held && h < history; h++)
                                        held = before[-h].At(x + dx, y + dy) == v;
                                }
                            score += held ? 1 : here ? 0 : -zeroHandicap * weight;
                        }  // zero: only where it explains clearly more than the motion
                        // equal evidence: the value the nearer frame shows here (a static stroke over a moving layer's
                        // background stays drawn)
                        const int tie = here ? cands[c] == V{}
                                              : nearTie && va == (w < kOne / 2 ? a : b).At(x, y);
                        if (score > bestScore || (score == bestScore && tie > bestTie))
                        {
                            bestScore = score;
                            bestTie = tie;
                            pixel = va;
                        }
                    }
                    if (bestScore > 0)
                        out[p] = pixel;
                    else if (moving)
                    {
                        const int sx = shift(own.v.x), sy = shift(own.v.y);
                        out[p] = w < kOne / 2 ? a.At(x - sx, y - sy) : b.At(x + own.v.x - sx, y + own.v.y - sy);
                    }
                    else
                        out[p] = Mix(a.pixels[p], b.pixels[p], narrow);
                }
        }
}

/// Layers: every pixel is the layer that best explains its neighbourhood over the frames k and k + 1 (k - 1 for the
/// still and steady checks). A layer is a vector: zero (a static layer) or one of the block fields' vectors k -> k + 1
/// and k - 1 -> k (the field's common vectors everywhere, the 3 x 3 blocks' own nearby). At a pixel x at the fraction w
/// a vector v samples a(x - wv) and b(x - wv + v):
/// - it fits when the samples agree and v carries the pixel's own colour in one frame (a(x) to b(x + v) or b(x) from
///   a(x - v)): a static layer over motion keeps its pixels, a recoloured or blinking one fits nothing;
/// - a moving v needs support: a pixel in the support window where it fits and explains a change both ways (zero fails
///   there), else uniform paper "moves" anywhere and carries a blinking sprite's paper over it.
/// The window score of a vector is the count of fitting pixels round the pixel. A block's pixels take the vector most
/// of them fit (no a/b mosaic); the rules below settle ties, look-alikes and holes. Nothing fits: the nearer frame whole.
struct LayerFlow
{
    using V = MotionField::Vector;
    static constexpr int kRadius = 5;  ///< the score's window
    static constexpr int kSupportRadius = 16;  ///< the support's window: sparse content has its changes far apart
    int cachedFrom = -1;
    MotionField next, prev;
    std::vector<V> common;

    void Prepare(int k)
    {
        if (cachedFrom == k)
            return;
        cachedFrom = k;
        next.Estimate(PictureAt(k), PictureAt(k + 1), MotionField::kRangeFrames);
        prev.Estimate(PictureAt(k - 1), PictureAt(k), MotionField::kRangeFrames);
        // the vectors several blocks share, most shared first
        std::vector<std::pair<V, int>> counts;
        for (const MotionField* f : {&next, &prev})
            for (int j = 0; j < f->BlocksHigh(); j++)
                for (int i = 0; i < f->BlocksWide(); i++)
                {
                    const auto& blk = f->At(i, j);
                    if (!blk.matched)
                        continue;
                    auto it = std::find_if(counts.begin(), counts.end(), [&](const auto& c) { return c.first == blk.v; });
                    if (it == counts.end())
                        counts.push_back({blk.v, 1});
                    else
                        it->second++;
                }
        std::stable_sort(counts.begin(), counts.end(), [](const auto& x, const auto& y) { return x.second > y.second; });
        common = {V{}};
        for (const auto& [v, n] : counts)
            if (n >= 2 && common.size() < 8 && !(v == V{}))
                common.push_back(v);
    }

    static bool Slower(V u, V v) { return u.x * u.x + u.y * u.y < v.x * v.x + v.y * v.y; }

    /// Window sums over an image of counts (an integral image)
    struct Box
    {
        int width, height, radius;
        std::vector<int> sum;

        Box(int w, int h, int r) : width(w), height(h), radius(r), sum(static_cast<size_t>(w + 1) * (h + 1)) {}

        int& At(int x, int y) { return sum[static_cast<size_t>(y) * (width + 1) + x]; }

        void Build(const std::vector<uint8_t>& counts)
        {
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                    At(x + 1, y + 1) = counts[static_cast<size_t>(y) * width + x] + At(x + 1, y) + At(x, y + 1) - At(x, y);
        }

        int Window(int x, int y)
        {
            const int x0 = std::max(x - radius, 0), x1 = std::min(x + radius + 1, width);
            const int y0 = std::max(y - radius, 0), y1 = std::min(y + radius + 1, height);
            return At(x1, y1) - At(x1, y0) - At(x0, y1) + At(x0, y0);
        }
    };

    void Render(int k, int w, uint32_t* out)
    {
        Prepare(k);
        const Picture a = PictureAt(k), b = PictureAt(k + 1), p = PictureAt(k - 1);
        const int width = a.width, height = a.height, bw = next.BlocksWide(), bh = next.BlocksHigh();
        // every vector some pixel may take
        std::vector<V> all = common;
        const auto add = [&](V v) {
            if (std::find(all.begin(), all.end(), v) == all.end())
                all.push_back(v);
        };
        for (const MotionField* f : {&next, &prev})
            for (int j = 0; j < bh; j++)
                for (int i = 0; i < bw; i++)
                    if (f->At(i, j).matched)
                        add(f->At(i, j).v);
        const int n = static_cast<int>(all.size());
        const size_t pixels = static_cast<size_t>(width) * height;
        std::vector<uint8_t> fits(pixels * n);
        std::vector<int> score(pixels * n), support(pixels * n);
        std::vector<uint8_t> fitting(pixels), changes(pixels);  // fitting: the pair fits the score counts
        std::vector<uint8_t> explained(pixels), changed(pixels);  // credible: a pixel some moving vector explains
        Box fitBox(width, height, kRadius), changeBox(width, height, kSupportRadius);
        const auto shift = [&](int c) { return (w * c + kOne / 2) >> 8; };
        // stopped: the pixels with no change from a to b anywhere in the window
        std::vector<uint8_t> calm(pixels);
        {
            std::vector<uint8_t> moved(pixels);
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                    moved[static_cast<size_t>(y) * width + x] = a.At(x, y) != b.At(x, y);
            Box movedBox(width, height, kRadius);
            movedBox.Build(moved);
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                    calm[static_cast<size_t>(y) * width + x] = movedBox.Window(x, y) == 0;
        }
        for (int c = 0; c < n; c++)
        {
            const V v = all[c];
            const int sx = shift(v.x), sy = shift(v.y);
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                {
                    const size_t q = static_cast<size_t>(y) * width + x;
                    const int ax = x - sx, ay = y - sy;
                    const uint32_t va = a.At(ax, ay), vb = b.At(ax, ay);
                    const bool agree = va == b.At(ax + v.x, ay + v.y);
                    const bool fit = agree && (a.At(x, y) == b.At(x + v.x, y + v.y) || b.At(x, y) == a.At(x - v.x, y - v.y));
                    // still3: zero fits only a pixel the same in k - 1, k and k + 1 (static stays static at any
                    // cadence); stopped: or where nothing changes round the pixel either (the layer stopped)
                    fits[static_cast<size_t>(c) * pixels + q] = fit && (!(v == V{}) || p.At(x, y) == va || calm[q]);
                    fitting[q] = fit;
                    changes[q] = fit && va != vb && vb == a.At(ax - v.x, ay - v.y);
                    if (!(v == V{}))
                        explained[q] |= changes[q];
                }
            fitBox.Build(fitting);
            changeBox.Build(changes);
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                {
                    const size_t q = static_cast<size_t>(y) * width + x;
                    score[static_cast<size_t>(c) * pixels + q] = fitBox.Window(x, y);
                    support[static_cast<size_t>(c) * pixels + q] = changeBox.Window(x, y);
                }
        }
        const Picture& nearer = w < kOne / 2 ? a : b;
        // credible: a moving vector explains at least the window's changes no vector explains (flux: none); the
        // changes of the window no vector explains are the bar a moving vector's support must reach
        std::vector<int> unexplained(pixels);
        {
            Box changedBox(width, height, kSupportRadius), explainedBox(width, height, kSupportRadius);
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                    changed[static_cast<size_t>(y) * width + x] = a.At(x, y) != b.At(x, y);
            changedBox.Build(changed);
            explainedBox.Build(explained);
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                    unexplained[static_cast<size_t>(y) * width + x] =
                        std::max(changedBox.Window(x, y) - explainedBox.Window(x, y), 0);
        }
        // cover: a moving layer's content passes over a still pixel when the layer covers that still colour round here
        // (still pixels of it turn into the layer's content) more than it goes under it (the layer's content due there
        // and the still colour staying): a fast sprite's holes. Per moving vector, the still pixels whose colour the
        // vector's layer covers round here more than it goes under
        std::vector<uint8_t> coverable(pixels * n);
        {
            std::vector<uint32_t> palette;
            std::vector<uint8_t> colourAt(pixels);
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                {
                    auto it = std::find(palette.begin(), palette.end(), a.At(x, y));
                    if (it == palette.end() && palette.size() < 32)
                    {
                        palette.push_back(a.At(x, y));
                        it = palette.end() - 1;
                    }
                    colourAt[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(it == palette.end() ? 255 : it - palette.begin());
                }
            std::vector<uint8_t> wins(pixels), losses(pixels);
            Box winBox(width, height, kSupportRadius), lossBox(width, height, kSupportRadius);
            for (int c = 0; c < n; c++)
            {
                const V v = all[c];
                if (v == V{})
                    continue;
                for (size_t col = 0; col < palette.size(); col++)
                {
                    bool any = false;
                    for (int y = 0; y < height; y++)
                        for (int x = 0; x < width; x++)
                        {
                            const size_t q = static_cast<size_t>(y) * width + x;
                            wins[q] = losses[q] = 0;
                            if (colourAt[q] != col)
                                continue;
                            const uint32_t here = a.At(x, y), from = a.At(x - v.x, y - v.y);
                            const bool fromMoves = from != b.At(x - v.x, y - v.y) && from != here;
                            // won: still from k - 1 to k, then the layer's content arrives
                            wins[q] = p.At(x, y) == here && b.At(x, y) == from && fromMoves;
                            // lost: still over k - 1, k, k + 1 while the layer's content, steady from k - 1, is due
                            losses[q] = p.At(x, y) == here && b.At(x, y) == here && fromMoves &&
                                        p.At(x - 2 * v.x, y - 2 * v.y) == from;
                            any |= wins[q] || losses[q];
                        }
                    if (!any)
                        continue;
                    winBox.Build(wins);
                    lossBox.Build(losses);
                    for (int y = 0; y < height; y++)
                        for (int x = 0; x < width; x++)
                        {
                            const size_t q = static_cast<size_t>(y) * width + x;
                            if (colourAt[q] == col && a.At(x, y) == b.At(x, y))
                                coverable[static_cast<size_t>(c) * pixels + q] = winBox.Window(x, y) > lossBox.Window(x, y);
                        }
                }
            }
        }
        // per block: the common vectors and the vectors of the 3 x 3 blocks round it
        const int blocksWide = (width + MotionField::kBlock - 1) / MotionField::kBlock;
        const int blocksHigh = (height + MotionField::kBlock - 1) / MotionField::kBlock;
        std::vector<std::vector<int>> allowedAt(static_cast<size_t>(blocksWide) * blocksHigh);
        std::vector<int> blockVector(allowedAt.size(), -1);
        std::vector<std::vector<int>> tiedAt(allowedAt.size());  // the vectors equal to the block's best
        // a fitting vector a pixel may take: zero, or a moving one with support that is credible
        const auto usable = [&](int c, size_t q) {
            const size_t cq = static_cast<size_t>(c) * pixels + q;
            return fits[cq] && (all[c] == V{} || (support[cq] > 0 && support[cq] >= unexplained[q]));
        };
        for (int bj = 0; bj < blocksHigh; bj++)
            for (int bi = 0; bi < blocksWide; bi++)
            {
                const size_t block = static_cast<size_t>(bj) * blocksWide + bi;
                auto& allowed = allowedAt[block];
                for (int c = 0; c < static_cast<int>(common.size()); c++)
                    allowed.push_back(c);
                for (const MotionField* f : {&next, &prev})
                    for (int dj = -1; dj <= 1; dj++)
                        for (int di = -1; di <= 1; di++)
                        {
                            const int i = bi + di, j = bj + dj;
                            if (i < 0 || j < 0 || i >= bw || j >= bh || !f->At(i, j).matched)
                                continue;
                            const int c = static_cast<int>(std::find(all.begin(), all.end(), f->At(i, j).v) - all.begin());
                            if (std::find(allowed.begin(), allowed.end(), c) == allowed.end())
                                allowed.push_back(c);
                        }
                // coherent: a block's pixels take the vector most of them fit when it fits them (no a/b mosaic);
                // ties: the slowest (look-alikes everywhere: what the eye sees), then the earlier
                int most = 0;
                for (int c : allowed)
                {
                    int count = 0;
                    for (int y = bj * MotionField::kBlock; y < std::min((bj + 1) * MotionField::kBlock, height); y++)
                        for (int x = bi * MotionField::kBlock; x < std::min((bi + 1) * MotionField::kBlock, width); x++)
                            count += usable(c, static_cast<size_t>(y) * width + x);
                    if (count > most)
                    {
                        most = count;
                        blockVector[block] = c;
                        tiedAt[block] = {c};
                    }
                    else if (count == most && count > 0)
                    {
                        tiedAt[block].push_back(c);
                        if (Slower(all[c], all[blockVector[block]]))
                            blockVector[block] = c;
                    }
                }
                if (blockVector[block] >= 0)
                {
                    // fitTies: look-alikes fitting all of a block tie with or without support of their own (a dot
                    // row's middle: the static look-alike and the row's motion; its ends tell). The vectors fitting as
                    // many pixels of the block as the best does, support aside
                    const auto fitCount = [&](int c) {
                        int count = 0;
                        for (int y = bj * MotionField::kBlock; y < std::min((bj + 1) * MotionField::kBlock, height); y++)
                            for (int x = bi * MotionField::kBlock; x < std::min((bi + 1) * MotionField::kBlock, width); x++)
                                count += fits[static_cast<size_t>(c) * pixels + static_cast<size_t>(y) * width + x];
                        return count;
                    };
                    // over every vector of the frame, not the block's own list: the same tie all over a layer
                    int best = 0;
                    for (int c = 0; c < n; c++)
                        best = std::max(best, fitCount(c));
                    // a vector fitting more of the block than the vote's does is evidence enough (a dot row's end
                    // fits its motion only; the support may be out of the window)
                    tiedAt[block].clear();
                    for (int c = 0; c < n; c++)
                        if (fitCount(c) == best)
                            tiedAt[block].push_back(c);
                    std::sort(tiedAt[block].begin(), tiedAt[block].end());
                    if (fitCount(blockVector[block]) < best)
                    {
                        blockVector[block] = tiedAt[block][0];
                        for (int c : tiedAt[block])
                            if (Slower(all[c], all[blockVector[block]]))
                                blockVector[block] = c;
                    }
                }
                if (blockVector[block] >= 0 && !(all[blockVector[block]] == V{}))
                {
                    // aliases: a faster look-alike beats a slower one only on changes of its own: a change another
                    // vector explains with more support is that layer's (a period-8 dash fits +1 and -7; -7 also fits
                    // a few changes of the text under it by chance). The pixels c fits and s (if any) does not, the
                    // unchanged ones too (the block's still evidence), less those another vector explains with more
                    // support
                    const auto own = [&](int c, int s) {
                        int count = 0;
                        for (int y = bj * MotionField::kBlock; y < std::min((bj + 1) * MotionField::kBlock, height); y++)
                            for (int x = bi * MotionField::kBlock; x < std::min((bi + 1) * MotionField::kBlock, width); x++)
                            {
                                const size_t q = static_cast<size_t>(y) * width + x;
                                if (!fits[static_cast<size_t>(c) * pixels + q] || (s >= 0 && fits[static_cast<size_t>(s) * pixels + q]))
                                    continue;
                                bool other = false;
                                for (int u = 0; u < n && !other; u++)
                                    other = u != c && u != s && fits[static_cast<size_t>(u) * pixels + q] &&
                                            support[static_cast<size_t>(u) * pixels + q] > support[static_cast<size_t>(c) * pixels + q];
                                count += !other;
                            }
                        return count;
                    };
                    const int c = blockVector[block];
                    int chosen = c;
                    for (int s = 0; s < n; s++)
                        if (!(all[s] == V{}) && Slower(all[s], all[chosen]) && own(s, -1) > 0 && own(c, s) == 0)
                            chosen = s;
                    blockVector[block] = chosen;
                }
            }
        // continued: a region takes a neighbour's vote only where the layer continues into it (its content pixels at
        // the shared edge as ambiguous as the block: the same periodic content, not another layer):
        // - (colours, own) every colour of the neighbour's edge, its paper too, is one the block has (a layer keeps its ink);
        // - (vote) the neighbour's own vector, not every tied one, fits most of its edge content (a dot row's end
        //   decides its middle though the still look-alike breaks there)
        const auto continues = [&](size_t nbOf, int bi, int bj, int di, int dj) {
            const int kb = MotionField::kBlock;
            int x0 = (bi + di) * kb, x1 = x0 + kb, y0 = (bj + dj) * kb, y1 = y0 + kb;
            if (di < 0)
                x0 = x1 - kb / 2;
            if (di > 0)
                x1 = x0 + kb / 2;
            if (dj < 0)
                y0 = y1 - kb / 2;
            if (dj > 0)
                y1 = y0 + kb / 2;
            x1 = std::min(x1, width);
            y1 = std::min(y1, height);
            // the neighbour's paper: its most frequent colour
            uint32_t paper = 0;
            {
                std::vector<std::pair<uint32_t, int>> seen;
                for (int y = (bj + dj) * kb; y < std::min((bj + dj + 1) * kb, height); y++)
                    for (int x = (bi + di) * kb; x < std::min((bi + di + 1) * kb, width); x++)
                    {
                        auto it = std::find_if(seen.begin(), seen.end(), [&](const auto& e) { return e.first == a.At(x, y); });
                        if (it == seen.end())
                            seen.push_back({a.At(x, y), 1});
                        else
                            it->second++;
                    }
                int most = 0;
                for (const auto& [colour, count] : seen)
                    if (count > most)
                    {
                        most = count;
                        paper = colour;
                    }
            }
            std::vector<uint32_t> own;
            for (int y = bj * kb; y < std::min((bj + 1) * kb, height); y++)
                for (int x = bi * kb; x < std::min((bi + 1) * kb, width); x++)
                    if (std::find(own.begin(), own.end(), a.At(x, y)) == own.end())
                        own.push_back(a.At(x, y));
            int content = 0, foreign = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++)
                {
                    foreign += std::find(own.begin(), own.end(), a.At(x, y)) == own.end();
                    content += a.At(x, y) != paper;
                }
            if (content == 0 || foreign > 0)
                return false;
            // the neighbour's own vector carries its content at the edge: the layer goes on with it
            int carried = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++)
                    carried += a.At(x, y) != paper &&
                               fits[static_cast<size_t>(blockVector[nbOf]) * pixels + static_cast<size_t>(y) * width + x];
            return carried * 4 >= content * 3;
        };
        // spread, majority: a region of blocks tied alike takes what most decided blocks round it say. Regions of
        // 4-connected blocks with the same tie set; the decided blocks bordering a region vote (a dot row's ends tell
        // its motion to the row's middle, where the static look-alike fits as well)
        std::vector<int> region(allowedAt.size(), -1);
        for (size_t seed = 0; seed < allowedAt.size(); seed++)
        {
            if (tiedAt[seed].size() < 2 || region[seed] >= 0)
                continue;
            std::vector<size_t> members{seed}, border;
            region[seed] = static_cast<int>(seed);
            for (size_t m = 0; m < members.size(); m++)
            {
                const int bi = static_cast<int>(members[m] % blocksWide), bj = static_cast<int>(members[m] / blocksWide);
                for (const auto& [di, dj] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}})
                {
                    const int i = bi + di, j = bj + dj;
                    if (i < 0 || j < 0 || i >= blocksWide || j >= blocksHigh)
                        continue;
                    const size_t nb = static_cast<size_t>(j) * blocksWide + i;
                    if (tiedAt[nb] == tiedAt[seed])
                    {
                        if (region[nb] < 0)
                        {
                            region[nb] = static_cast<int>(seed);
                            members.push_back(nb);
                        }
                    }
                    else if (tiedAt[nb].size() == 1 && continues(nb, bi, bj, di, dj))
                        border.push_back(nb);
                }
            }
            int bestVotes = 0, chosen = -1;
            for (int c : tiedAt[seed])
            {
                const int votes = static_cast<int>(std::count_if(border.begin(), border.end(), [&](size_t nb) { return blockVector[nb] == c; }));
                if (votes > bestVotes || (votes == bestVotes && votes > 0 && Slower(all[c], all[chosen])))
                {
                    bestVotes = votes;
                    chosen = c;
                }
            }
            if (chosen >= 0)
                for (size_t m : members)
                    blockVector[m] = chosen;
        }
        // carryFirst: the block's vector explains at least half of the block's changes (the motion of what is there,
        // not a chance match of content appearing)
        std::vector<int8_t> explainsAt(blockVector.size(), -1);
        const auto blockExplains = [&](size_t block) {
            if (explainsAt[block] < 0)
            {
                const int bi = static_cast<int>(block % blocksWide), bj = static_cast<int>(block / blocksWide);
                const V v = all[blockVector[block]];
                int changes = 0, explainedHere = 0;
                for (int y = bj * MotionField::kBlock; y < std::min((bj + 1) * MotionField::kBlock, height); y++)
                    for (int x = bi * MotionField::kBlock; x < std::min((bi + 1) * MotionField::kBlock, width); x++)
                        if (a.At(x, y) != b.At(x, y))
                        {
                            changes++;
                            explainedHere += b.At(x, y) == a.At(x - v.x, y - v.y) || a.At(x, y) == b.At(x + v.x, y + v.y);
                        }
                explainsAt[block] = explainedHere * 2 >= changes;
            }
            return explainsAt[block] == 1;
        };
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
            {
                const size_t block = static_cast<size_t>(y / MotionField::kBlock) * blocksWide + x / MotionField::kBlock;
                const auto& allowed = allowedAt[block];
                const size_t q = static_cast<size_t>(y) * width + x;
                if (a.At(x, y) == b.At(x, y) &&
                    !(blockVector[block] >= 0 && !(all[blockVector[block]] == V{}) && usable(blockVector[block], q)) &&
                    p.At(x, y) == a.At(x, y))
                {
                    // cover (still, three): a pixel still over k - 1, k, k + 1 the block's moving vector does not
                    // take, a moving layer passes over (a sprite's hole): its content, carried from a, other than
                    // the pixel's
                    int over = -1;
                    for (int c : allowed)
                    {
                        const V v = all[c];
                        const size_t cq = static_cast<size_t>(c) * pixels + q;
                        if (v == V{} || !coverable[cq] || support[cq] == 0)
                            continue;
                        const int ax = x - shift(v.x), ay = y - shift(v.y);
                        const uint32_t va = a.At(ax, ay);
                        if (va != b.At(ax + v.x, ay + v.y) || va == b.At(ax, ay) || va == a.At(x, y))
                            continue;
                        // steady: the sample is the layer's: it moved so from k - 1 too (rigid content, not a growing glyph)
                        if (p.At(ax - v.x, ay - v.y) != va)
                            continue;
                        if (over < 0 || support[cq] > support[static_cast<size_t>(over) * pixels + q])
                            over = c;
                    }
                    if (over >= 0)
                    {
                        const V v = all[over];
                        out[q] = a.At(x - shift(v.x), y - shift(v.y));
                        continue;
                    }
                }
                // the block's vector where it is usable, or (fitTies) fits a tied block
                if (blockVector[block] >= 0 &&
                    (usable(blockVector[block], q) ||
                     (tiedAt[block].size() > 1 && fits[static_cast<size_t>(blockVector[block]) * pixels + q])))
                {
                    const V v = all[blockVector[block]];
                    out[q] = a.At(x - shift(v.x), y - shift(v.y));
                    continue;
                }
                if (blockVector[block] >= 0 && !(all[blockVector[block]] == V{}) && tiedAt[block].size() == 1 &&
                    blockExplains(block) && !(p.At(x, y) == a.At(x, y) && a.At(x, y) != b.At(x, y)))
                {
                    // carryFirst: a decided moving block keeps a pixel its vector does not fit unless another vector
                    // fitting it explains more changes round it, or a slower one fits it (a layer behind showing
                    // through, zero included); a pixel that stood still into a and changes only towards b (a blink in
                    // place) is not carried. Carried: the block's vector from the nearer frame
                    const int cb = blockVector[block];
                    const int mine = support[static_cast<size_t>(cb) * pixels + q];
                    bool other = false;
                    for (int c : allowed)
                        other |= c != cb && usable(c, q) &&
                                 (Slower(all[c], all[cb]) || support[static_cast<size_t>(c) * pixels + q] > mine);
                    if (!other)
                    {
                        const V v = all[cb];
                        const int ax = x - shift(v.x), ay = y - shift(v.y);
                        out[q] = w < kOne / 2 ? a.At(ax, ay) : b.At(ax + v.x, ay + v.y);
                        continue;
                    }
                }
                int best = -1, bestScore = -1;
                for (int c : allowed)  // ties: the slowest, the earlier (zero, then the most shared)
                {
                    const size_t cq = static_cast<size_t>(c) * pixels + q;
                    if (usable(c, q) && (score[cq] > bestScore || (score[cq] == bestScore && Slower(all[c], all[best]))))
                    {
                        best = c;
                        bestScore = score[cq];
                    }
                }
                out[q] = best < 0 ? nearer.pixels[q] : a.At(x - shift(all[best].x), y - shift(all[best].y));
            }
    }
};

// Labels in the Spectrum ROM's font (0x3D00, ASCII 32-127)
void DrawText(uint32_t* img, int stride, int x, int y, const std::string& text, uint32_t ink)
{
    for (size_t c = 0; c < text.size(); c++)
    {
        const int ch = std::clamp(static_cast<int>(static_cast<unsigned char>(text[c])), 32, 127) - 32;
        for (int row = 0; row < 8; row++)
        {
            const uint8_t bits = g_rom.size() >= 0x4000 ? g_rom[0x3D00 + ch * 8 + row] : 0;
            for (int bit = 0; bit < 8; bit++)
                if (bits & (0x80 >> bit))
                    img[static_cast<size_t>(y + row) * stride + x + static_cast<int>(c) * 8 + bit] = ink;
        }
    }
}

FILE* OpenVideo(const std::string& path, int width, int height, int scale)
{
    char cmd[1024];
    std::snprintf(cmd, sizeof cmd,
                  "ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgba -s %dx%d -r 60 -i - "
                  "-vf scale=iw*%d:ih*%d:flags=neighbor -c:v libx264 -preset medium -crf 14 -pix_fmt yuv420p "
                  "-movflags +faststart '%s'",
                  width, height, scale, scale, path.c_str());
    return popen(cmd, "w");
}

/// The game's picture period: the most common gap between frames that change more than 1 % of the pixels
int DetectPeriod()
{
    std::vector<int> gaps(17, 0);
    int last = -1;
    for (int k = 1; k < g_count; k++)
    {
        size_t changed = 0;
        const uint32_t* a = FrameAt(k - 1);
        const uint32_t* b = FrameAt(k);
        for (size_t p = 0; p < Pixels(); p++)
            changed += a[p] != b[p];
        if (changed * 100 < Pixels())
            continue;
        if (last >= 0 && k - last <= 16)
            gaps[k - last]++;
        last = k;
    }
    return static_cast<int>(std::max_element(gaps.begin() + 1, gaps.end()) - gaps.begin());
}
}  // namespace

int main(int argc, char** argv)
{
    if (argc < 7)
    {
        std::fprintf(stderr, "filters <seq.rgba> <width> <height> <count> <out dir> <48.rom> [sourceHz] [displayHz]\n");
        return 2;
    }
    g_width = std::atoi(argv[2]);
    g_height = std::atoi(argv[3]);
    g_count = std::atoi(argv[4]);
    const std::string outDir = argv[5];
    const double sourceHz = argc > 7 ? std::atof(argv[7]) : 50.0;
    const double displayHz = argc > 8 ? std::atof(argv[8]) : 60.0;
    g_framePeriod = std::llround(1e9 / sourceHz);
    const int64_t refreshPeriod = std::llround(1e9 / displayHz);

    g_frames.resize(Pixels() * g_count);
    FILE* in = std::fopen(argv[1], "rb");
    if (!in || std::fread(g_frames.data(), 4, g_frames.size(), in) != g_frames.size())
    {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    std::fclose(in);
    if (FILE* rom = std::fopen(argv[6], "rb"))
    {
        g_rom.resize(0x4000);
        if (std::fread(g_rom.data(), 1, g_rom.size(), rom) != g_rom.size())
            g_rom.clear();
        std::fclose(rom);
    }
    BuildGamma();
    const int period = DetectPeriod();

    std::vector<std::unique_ptr<Engine>> engines;
    const auto engine = [&](Mode mode, GameMode game) {
        engines.push_back(std::make_unique<Engine>(mode, game, std::max(period, 2)));
        Engine* e = engines.back().get();
        return [e](int64_t t, uint32_t* out) { e->Render(t, out); };
    };
    auto global = std::make_shared<Pan>();
    auto strips = std::make_shared<Pan>();
    strips->strips = true;
    auto carryField = std::make_shared<std::pair<int, MotionField>>(-1, MotionField{});

    std::vector<Filter> filters;
    filters.push_back({"off", engine(Mode::Off, GameMode::Off)});
    filters.push_back({"blend", engine(Mode::Blend, GameMode::Off)});
    filters.push_back({"blend-linear", [](int64_t t, uint32_t* out) {
                           int k, w;
                           DueAt(t, k, w);
                           const uint32_t *a = FrameAt(k), *b = FrameAt(k + 1);
                           for (size_t p = 0; p < Pixels(); p++)
                               out[p] = MixLinear(a[p], b[p], w);
                       }});
    filters.push_back({"blend-narrow", [](int64_t t, uint32_t* out) {
                           // the frame as it is near either end of the step, the blend only in its middle half
                           int k, w;
                           DueAt(t, k, w);
                           w = std::clamp((w - kOne / 4) * 2, 0, kOne);
                           Blend(PictureAt(k), PictureAt(k + 1), w, out);
                       }});
    filters.push_back({"shutter", [refreshPeriod](int64_t t, uint32_t* out) {
                           // a refresh-long exposure centred on t: each frame by its share of the time
                           const int64_t t0 = t - refreshPeriod / 2, t1 = t0 + refreshPeriod;
                           const int k = static_cast<int>(t0 / g_framePeriod);
                           const int64_t edge = (k + 1) * g_framePeriod;
                           const int w = edge >= t1 ? 0 : WeightAt(t1 - edge, 0, refreshPeriod);
                           Blend(PictureAt(k), PictureAt(k + 1), w, out);
                       }});
    filters.push_back({"interpolate", engine(Mode::Interpolate, GameMode::Off)});
    filters.push_back({"pan-global", [global](int64_t t, uint32_t* out) {
                           int k, w;
                           DueAt(t, k, w);
                           global->Prepare(k);
                           global->field.Compose(PictureAt(k), PictureAt(k + 1), w, out);
                       }});
    filters.push_back({"pan-strips", [strips](int64_t t, uint32_t* out) {
                           int k, w;
                           DueAt(t, k, w);
                           strips->Prepare(k);
                           strips->field.Compose(PictureAt(k), PictureAt(k + 1), w, out);
                       }});
    auto rowField = std::make_shared<RowField>();
    filters.push_back({"interp-rows", [rowField](int64_t t, uint32_t* out) {
                           int k, w;
                           DueAt(t, k, w);
                           rowField->Prepare(k);
                           rowField->field.Compose(PictureAt(k), PictureAt(k + 1), w, out);
                       }});
    auto layers = std::make_shared<LayerFlow>();
    filters.push_back({"layers", [layers](int64_t t, uint32_t* out) {
                           int k, w;
                           DueAt(t, k, w);
                           layers->Render(k, w, out);
                       }});
    filters.push_back({"interp-solid", [rowField](int64_t t, uint32_t* out) {
                           int k, w;
                           DueAt(t, k, w);
                           rowField->Prepare(k);
                           ComposeSolid(rowField->field, PictureAt(k), PictureAt(k + 1), w, out);
                       }});
    filters.push_back({"pan-solid", [strips](int64_t t, uint32_t* out) {
                           int k, w;
                           DueAt(t, k, w);
                           strips->Prepare(k);
                           ComposeSolid(strips->field, PictureAt(k), PictureAt(k + 1), w, out);
                       }});
    for (const int slack : {12, 24, 40, 64})
    {
        auto rowSlack = std::make_shared<RowField>();
        rowSlack->slack = slack;
        filters.push_back({slack == 12 ? "solid-slack" : "solid-slack" + std::to_string(slack),
                           [rowSlack](int64_t t, uint32_t* out) {
                               int k, w;
                               DueAt(t, k, w);
                               rowSlack->Prepare(k);
                               ComposeSolid(rowSlack->field, PictureAt(k), PictureAt(k + 1), w, out);
                           }});
    }
    struct MrfVariant
    {
        int lambda = 8;
        bool tieCommon = true, layered = true, zeroLast = true;
        int trust = 4, regional = 0;
        bool linked = false, local = false, refine = false, gate = false, informative = false;
        int core = 0, handicap = 3;
        bool nearTie = false;
        int runs = 0, trustMargin = -1;
        bool coreStill = false, pixelStill = false, heldBlock = false;
        int stray = 0;  ///< > 0: a moving block whose vector misfits >= stray unchanged pixels keeps them static
        int strayHistory = 0;  ///< the stray layer's pixels also unchanged over the frames before
        int inkReach = -1, inkStage = 3, inkHistory = 0;
        bool inkCopied = false, inkImposed = false, inkShare = false, inkLive = false;
        int inkTouch = 0, inkNear = 0;
        bool inkFresh = false, inkJoined = false;
        int inkUnheld = 0;
        bool inkPooled = false;
        int inkQuiet = 0, inkTrust = 0, inkBacked = 0, inkSpread = 0;
        bool inkTelling = false;
        int inkDoubt = 10, inkMoving = 0;
        bool ink = false;  ///< the field's layered cost frees unchanged paper only
        bool strayInk = false;  ///< the stray layer proven by ink (not the block's paper) only
        bool unit = false;  ///< moving blocks with every vector around at most a pixel show the nearer frame
        int held = 0;  ///< > 0: a pixel unchanged for held + 2 frames (k - held .. k + 1) is static, zero wins
    };
    for (const MrfVariant& m : std::initializer_list<MrfVariant>{
             {.tieCommon = false, .layered = false, .zeroLast = false, .trust = 0},
             {.layered = false, .zeroLast = false, .trust = 0},
             {.zeroLast = false, .trust = 0},
             {.trust = 0},
             {.trust = 2},
             {},
             {.regional = 4},
             {.local = true},
             {.regional = 4, .local = true},
             {.regional = 4, .local = true, .gate = true},
             {.regional = 4, .local = true, .informative = true},
             {.regional = 4, .linked = true, .local = true},
             {.regional = 2, .linked = true, .local = true},
             {.local = true, .core = 8},
             {.local = true, .held = 1},
             {.local = true, .held = 3},
             {.local = true, .held = 5},
             {.local = true, .nearTie = true, .held = 3},
             {.local = true, .nearTie = true, .runs = 4, .held = 3},
             {.local = true, .nearTie = true, .runs = 4, .trustMargin = 0, .held = 3},
             {.local = true, .nearTie = true, .runs = 8, .trustMargin = 0, .held = 3},
             {.local = true, .nearTie = true, .runs = 4, .coreStill = true, .held = 3},
             {.local = true, .nearTie = true, .runs = 4, .pixelStill = true, .held = 3},
             {.local = true, .heldBlock = true, .held = 3},
             {.local = true, .coreStill = true, .heldBlock = true, .held = 3},
             {.local = true, .heldBlock = true, .held = 5},
             {.local = true, .heldBlock = true, .held = 6},
             {.local = true, .coreStill = true, .heldBlock = true, .held = 6},
             {.local = true, .heldBlock = true, .stray = 1, .held = 6},
             {.local = true, .heldBlock = true, .stray = 4, .held = 6},
             {.local = true, .heldBlock = true, .stray = 8, .held = 6},
             {.local = true, .heldBlock = true, .unit = true, .held = 6},
             {.local = true, .heldBlock = true, .stray = 4, .unit = true, .held = 6},
             {.local = true, .heldBlock = true, .stray = 1, .strayHistory = 1, .held = 6},
             {.local = true, .heldBlock = true, .stray = 1, .strayHistory = 2, .held = 6},
             {.local = true, .heldBlock = true, .stray = 4, .strayHistory = 2, .held = 6},
             {.local = true, .heldBlock = true, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 1, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkCopied = true, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkHistory = 1, .inkCopied = true, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkHistory = 2, .inkCopied = true, .ink = true, .held = 6},
             {.local = true, .trustMargin = 0, .heldBlock = true, .inkStage = 2, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkShare = true, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkShare = true, .inkLive = true, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkReach = 2, .inkStage = 2, .inkShare = true, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkShare = true, .inkTouch = 1, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkShare = true, .inkFresh = true, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkShare = true, .inkJoined = true, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkShare = true, .inkUnheld = 6, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkShare = true, .inkPooled = true, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkShare = true, .inkQuiet = 2, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .inkTrust = 5, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .inkTrust = 5, .inkBacked = 1, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .inkTrust = 5, .inkBacked = 1, .inkSpread = 3, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .inkTrust = 5, .inkBacked = 1, .inkSpread = 3, .inkDoubt = 8, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .inkTrust = 5, .inkBacked = 1, .inkSpread = 3, .inkDoubt = 6, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .inkTrust = 5, .inkBacked = 1, .inkSpread = 5, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .inkTrust = 5, .inkBacked = 1, .inkSpread = 3, .ink = true, .unit = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .inkTrust = 5, .inkBacked = 1, .inkSpread = 3, .inkTelling = true, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .inkTrust = 4, .inkBacked = 1, .inkSpread = 3, .inkTelling = true, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .inkTrust = 5, .inkBacked = 1, .inkSpread = 4, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .inkTrust = 5, .inkBacked = 1, .inkSpread = 4, .inkMoving = 8, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .inkTrust = 5, .inkBacked = 1, .inkSpread = 4, .inkMoving = 16, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .inkLive = true, .inkNear = 3, .inkTrust = 5, .inkBacked = 1, .inkSpread = 4, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .inkLive = true, .inkNear = 6, .inkTrust = 5, .inkBacked = 1, .inkSpread = 4, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .inkTrust = 5, .inkBacked = 2, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .inkTrust = 5, .inkBacked = 3, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .inkTrust = 6, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkImposed = true, .inkShare = true, .inkTrust = 8, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkShare = true, .inkQuiet = 3, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkShare = true, .inkUnheld = 2, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkShare = true, .inkLive = true, .inkTouch = 0, .inkNear = 3, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkShare = true, .inkLive = true, .inkTouch = 0, .inkNear = 4, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkShare = true, .inkTouch = 2, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkReach = 4, .inkStage = 2, .inkShare = true, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .inkHistory = 2, .inkShare = true, .inkLive = true, .ink = true, .held = 6},
             {.local = true, .trustMargin = 0, .heldBlock = true, .inkStage = 2, .inkShare = true, .ink = true, .held = 6},
             {.local = true, .trustMargin = 4, .heldBlock = true, .inkStage = 2, .inkShare = true, .ink = true, .held = 6},
             {.local = true, .trustMargin = 0, .heldBlock = true, .inkStage = 2, .inkImposed = true, .ink = true, .held = 6},
             {.local = true, .trustMargin = 4, .heldBlock = true, .inkStage = 2, .inkImposed = true, .ink = true, .held = 6},
             {.local = true, .trustMargin = 0, .heldBlock = true, .inkStage = 2, .inkHistory = 2, .inkCopied = true, .ink = true, .held = 6},
             {.local = true, .trustMargin = 2, .heldBlock = true, .inkStage = 2, .inkHistory = 2, .inkCopied = true, .ink = true, .held = 6},
             {.local = true, .trustMargin = 4, .heldBlock = true, .inkStage = 2, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkStage = 2, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkReach = 0, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .inkReach = 2, .ink = true, .held = 6},
             {.local = true, .heldBlock = true, .stray = 1, .strayInk = true, .held = 6},
             {.local = true, .heldBlock = true, .stray = 4, .strayInk = true, .held = 6},
             {.local = true, .heldBlock = true, .stray = 1, .strayHistory = 1, .strayInk = true, .held = 6},
             {.local = true, .trustMargin = 0, .heldBlock = true, .stray = 1, .strayHistory = 1, .held = 6},
             {.local = true, .trustMargin = 4, .heldBlock = true, .stray = 1, .strayHistory = 1, .held = 6},
         })
    {
        auto mrf = std::make_shared<MrfField>();
        mrf->lambda = m.lambda;
        mrf->margin = 12;
        mrf->stillMargin = 12;
        mrf->tieCommon = m.tieCommon;
        mrf->layered = m.layered;
        mrf->trust = m.trust;
        mrf->regional = m.regional;
        mrf->linked = m.linked;
        mrf->core = m.core;
        mrf->runs = m.runs;
        mrf->trustMargin = m.trustMargin;
        mrf->ink = m.ink;
        mrf->inkReach = m.inkReach;
        mrf->inkStage = m.inkStage;
        mrf->inkCopied = m.inkCopied;
        mrf->inkHistory = m.inkHistory;
        mrf->inkImposed = m.inkImposed;
        mrf->inkShare = m.inkShare;
        mrf->inkLive = m.inkLive;
        mrf->inkTouch = m.inkTouch;
        mrf->inkNear = m.inkNear;
        mrf->inkFresh = m.inkFresh;
        mrf->inkJoined = m.inkJoined;
        mrf->inkUnheld = m.inkUnheld;
        mrf->inkPooled = m.inkPooled;
        mrf->inkQuiet = m.inkQuiet;
        mrf->inkTrust = m.inkTrust;
        mrf->inkBacked = m.inkBacked;
        mrf->inkSpread = m.inkSpread;
        mrf->inkDoubt = m.inkDoubt;
        mrf->inkMoving = m.inkMoving;
        mrf->inkTelling = m.inkTelling;
        const std::string name = (m.tieCommon ? "mrft" : "mrf") + std::to_string(m.lambda) + "m12s12" +
                                 (m.layered ? "L" : "") + (m.zeroLast ? "z" : "") +
                                 (m.trust ? "t" + std::to_string(m.trust) : "") +
                                 (m.regional ? "g" + std::to_string(m.regional) : "") + (m.linked ? "n" : "") +
                                 (m.local ? "l" : "") + (m.refine ? "r" : "") +
                                 (m.core ? "c" + std::to_string(m.core) : "") + (m.gate ? "k" : "") +
                                 (m.informative ? "i" : "") + (m.handicap != 3 ? "h" + std::to_string(m.handicap) : "") + (m.held ? "s" + std::to_string(m.held) : "") + (m.nearTie ? "e" : "") + (m.runs ? "u" + std::to_string(m.runs) : "") +
                                 (m.trustMargin >= 0 ? "w" + std::to_string(m.trustMargin) : "") + (m.coreStill ? "q" : "") + (m.pixelStill ? "p" : "") + (m.heldBlock ? "b" : "") + (m.stray ? "x" + std::to_string(m.stray) : "") + (m.strayHistory ? "y" + std::to_string(m.strayHistory) : "") + (m.strayInk ? "j" : "") + (m.ink ? "a" + (m.inkReach >= 0 ? std::to_string(m.inkReach) : std::string()) + (m.inkStage != 3 ? "s" + std::to_string(m.inkStage) : std::string()) + (m.inkCopied ? "c" : "") + (m.inkImposed ? "m" : "") + (m.inkShare ? "r" : "") + (m.inkLive ? "v" + (m.inkNear ? std::to_string(m.inkNear) : std::string()) : "") + (m.inkFresh ? "f" : "") + (m.inkJoined ? "g" : "") + (m.inkPooled ? "p" : "") + (m.inkTelling ? "i" : "") + (m.inkMoving ? "M" + std::to_string(m.inkMoving) : std::string()) + (m.inkSpread ? "e" + std::to_string(m.inkSpread) + (m.inkDoubt != 10 ? "D" + std::to_string(m.inkDoubt) : std::string()) : std::string()) + (m.inkBacked ? "n" + std::to_string(m.inkBacked) : std::string()) + (m.inkTrust ? "k" + std::to_string(m.inkTrust) : std::string()) + (m.inkQuiet ? "q" + std::to_string(m.inkQuiet) : std::string()) + (m.inkUnheld ? "u" + std::to_string(m.inkUnheld) : std::string()) + (m.inkTouch ? "d" + std::to_string(m.inkTouch) : std::string()) + (m.inkHistory ? "h" + std::to_string(m.inkHistory) : std::string()) : "") + (m.unit ? "o" : "");
        filters.push_back({name, [mrf, m](int64_t t, uint32_t* out) {
                               int k, w;
                               DueAt(t, k, w);
                               mrf->Prepare(k);
                               Picture before[6] = {PictureAt(k - 6), PictureAt(k - 5), PictureAt(k - 4),
                                                    PictureAt(k - 3), PictureAt(k - 2), PictureAt(k - 1)};
                               ComposeSolid(mrf->field, PictureAt(k), PictureAt(k + 1), w, out, m.zeroLast, m.local,
                                            m.refine, m.gate, m.informative, m.handicap,
                                            m.held ? &before[5] : nullptr, m.held, m.nearTie, m.coreStill, m.pixelStill, m.heldBlock, m.stray, m.unit, m.strayHistory, m.strayInk);
                           }});
    }
    auto guarded = std::make_shared<Pan>();
    guarded->strips = true;
    guarded->guard = true;
    filters.push_back({"pan-guard", [guarded](int64_t t, uint32_t* out) {
                           int k, w;
                           DueAt(t, k, w);
                           guarded->Prepare(k);
                           ComposeSolid(guarded->field, PictureAt(k), PictureAt(k + 1), w, out);
                       }});
    filters.push_back({"extrapolate", [carryField](int64_t t, uint32_t* out) {
                           // no lookahead: the last pair's motion carried past the frame due
                           int k, w;
                           DueAt(t, k, w);
                           if (k < 1)
                           {
                               std::memcpy(out, FrameAt(k), Pixels() * 4);
                               return;
                           }
                           if (carryField->first != k)
                           {
                               carryField->first = k;
                               carryField->second.Estimate(PictureAt(k - 1), PictureAt(k), MotionField::kRangeFrames);
                           }
                           carryField->second.Compose(PictureAt(k - 1), PictureAt(k), kOne + w, out);
                       }});
    filters.push_back({"game-cadence", engine(Mode::Interpolate, GameMode::Cadence)});
    filters.push_back({"game-carry", engine(Mode::Interpolate, GameMode::Carry)});

    // FILTERS_ONLY=a,b,...: only these, in this order, into <FILTERS_GRID or "grid-a-b-...">.mp4, no single videos
    std::string gridName = "grid";
    const bool only = std::getenv("FILTERS_ONLY") != nullptr;
    if (only)
    {
        std::vector<Filter> chosen;
        std::string list = std::getenv("FILTERS_ONLY");
        gridName = std::getenv("FILTERS_GRID") ? std::getenv("FILTERS_GRID") : "grid";
        for (size_t start = 0; start <= list.size();)
        {
            const size_t comma = std::min(list.find(',', start), list.size());
            const std::string name = list.substr(start, comma - start);
            for (const Filter& f : filters)
                if (f.name == name)
                    chosen.push_back(f);
            if (!std::getenv("FILTERS_GRID"))
                gridName += "-" + name;
            start = comma + 1;
        }
        filters = std::move(chosen);
    }

    // Grid: cells of the frame minus most of the border, 4 x 3 for every filter, 2 x 2 (3x) for up to four
    const int n = static_cast<int>(filters.size());
    const int cropX = 16, cropY = 24, cellW = 320, cellH = 240;
    const int cols = n <= 4 ? 2 : 4, rows = (n + cols - 1) / cols;
    const int gridW = cellW * cols, gridH = cellH * rows + 16;  // a line of the clip's facts below
    std::vector<uint32_t> grid(static_cast<size_t>(gridW) * gridH);
    std::vector<FILE*> videos;
    for (const Filter& f : filters)
        videos.push_back(only ? nullptr : OpenVideo(outDir + "/" + f.name + ".mp4", g_width, g_height, 3));
    // FILTERS_RAW=1: every filter's pictures also as raw RGBA (<filter>.rgba) for the analysis
    const bool raw = std::getenv("FILTERS_RAW") != nullptr;
    std::vector<FILE*> raws;
    for (const Filter& f : filters)
        raws.push_back(raw ? std::fopen((outDir + "/" + f.name + ".rgba").c_str(), "wb") : nullptr);
    FILE* gridVideo = OpenVideo(outDir + "/" + gridName + ".mp4", gridW, gridH, n <= 4 ? 3 : 2);

    std::vector<uint32_t> out(Pixels());
    const int64_t phase = 3'000'000;  // the refreshes off the frames' grid
    const int64_t end = (g_count - 3) * g_framePeriod;
    int refreshes = 0;
    for (int64_t t = phase; t < end; t += refreshPeriod, refreshes++)
    {
        std::fill(grid.begin(), grid.end(), 0xFF000000u);
        for (size_t i = 0; i < filters.size(); i++)
        {
            const auto t0 = std::chrono::steady_clock::now();
            filters[i].render(t, out.data());
            filters[i].ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            filters[i].calls++;
            if (videos[i])
                std::fwrite(out.data(), 4, out.size(), videos[i]);
            if (raws[i])
                std::fwrite(out.data(), 4, out.size(), raws[i]);
            const int gx = static_cast<int>(i % cols) * cellW, gy = static_cast<int>(i / cols) * cellH;
            for (int y = 0; y < cellH; y++)
                std::memcpy(&grid[static_cast<size_t>(gy + y) * gridW + gx],
                            &out[static_cast<size_t>(cropY + y) * g_width + cropX], cellW * 4);
            for (int y = 0; y < 10; y++)
                std::fill_n(&grid[static_cast<size_t>(gy + y) * gridW + gx], filters[i].name.size() * 8 + 4,
                            0xFF000000u);
            DrawText(grid.data(), gridW, gx + 2, gy + 1, filters[i].name, 0xFF00FFFFu);
        }
        const std::string name = outDir.substr(outDir.find_last_of('/') + 1);
        char line[160];
        std::snprintf(line, sizeof line, "%s  %.2f Hz -> %.0f Hz  game period %d  refresh %d", name.c_str(), sourceHz,
                      displayHz, period, refreshes);
        DrawText(grid.data(), gridW, 8, rows * cellH + 4, line, 0xFFFFFFFFu);
        std::fwrite(grid.data(), 4, grid.size(), gridVideo);
    }
    for (FILE* v : videos)
        if (v)
            pclose(v);
    for (FILE* r : raws)
        if (r)
            std::fclose(r);
    pclose(gridVideo);

    std::printf("%s: %d refreshes, game period %d\n", outDir.c_str(), refreshes, period);
    for (const Filter& f : filters)
        std::printf("  %-13s %6.2f ms per refresh\n", f.name.c_str(), f.calls ? f.ms / f.calls : 0.0);
    return 0;
}
