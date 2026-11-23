// POC 055: a filters binary for POC 047's suite.py (the same command line and outputs as
// tools/verification/smoothmotion/filters.cpp, no videos), with the cheap interpolators of this POC beside the
// reference's.
//
//   lite <source.rgba> <width> <height> <frames> <out dir> <rom, unused> [source Hz] [display Hz]
//   FILTERS_ONLY=off,blend,layers,lite   the filters to render, each into <out dir>/<name>.rgba
//   LITE_FIELD=reference                 the lite filters' field by MotionField::Estimate (default: BitField)
//   LITE_CHECK_FIELD=1                   every pair's field both ways: the blocks that differ, the time of each
//
// Prints each filter's time a refresh (one thread), the lite ones split into the field and the composition.
//
// Build: tools/verification/smoothmotion/compile-tool.sh tools/poc/055-interpolation-low-end/cpp/lite.cpp <out>
#include "emulator/video/smoothlayers.h"
#include "emulator/video/smoothmotion.h"

#include "bitfield.h"
#include "litefield.h"
#include "planes.h"

#include <cstring>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

using namespace smoothmotion;
using namespace poc055;

namespace
{
int g_width = 0, g_height = 0, g_count = 0;
int64_t g_framePeriod = 20'000'000;
std::vector<uint32_t> g_frames;
Palette g_palette;
std::vector<Planes> g_planes;

size_t Pixels()
{
    return static_cast<size_t>(g_width) * g_height;
}
int Clamped(int k)
{
    return std::clamp(k, 0, g_count - 1);
}
Picture PictureAt(int k)
{
    return {g_width, g_height, g_frames.data() + static_cast<size_t>(Clamped(k)) * Pixels()};
}
void DueAt(int64_t t, int& k, int& w)
{
    k = static_cast<int>(t / g_framePeriod);
    w = WeightAt(t, k * g_framePeriod, (k + 1) * g_framePeriod);
}
double MsSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

/// The reference: LayerFlow over the block fields
struct Layers
{
    int cachedFrom = -1;
    MotionField next, prev;
    LayerFlow flow;

    void Render(int k, int w, uint32_t* out)
    {
        if (cachedFrom != k)
        {
            cachedFrom = k;
            next.Estimate(PictureAt(k), PictureAt(k + 1), MotionField::kRangeFrames);
            prev.Estimate(PictureAt(k - 1), PictureAt(k), MotionField::kRangeFrames);
        }
        flow.Compose(PictureAt(k - 1), PictureAt(k), PictureAt(k + 1), prev, next, w, out);
    }
};

/// c-lite, first form: the block field's vectors as layers, a pixel's layer picked by masks of whole words.
/// - a vector fits a pixel when a(x - s) and b(x - s + v) are the same colour (s its shift at the weight);
/// - a pixel takes its block's own vector if it fits; else zero if a and b are the same there; else a vector of the
///   blocks round it (or one of the two most common) that fits, the most common first; else the nearer frame.
/// No window scores, no support, no cover: what the masks give for a few word operations a vector.
struct Lite
{
    static constexpr int kVectors = 8;  ///< the moving vectors of a pair at most, the most common
    static constexpr int kEverywhere = 2;  ///< the most common ones tried in every block

    using V = MotionField::Vector;
    int cachedFrom = -1, credibleFor = -1;
    MotionField field;
    poc055::Grid grid;  ///< the pair's blocks, whichever field made them
    std::vector<V> vectors;                 ///< the moving ones, most blocks first
    std::vector<uint64_t> own, near;        ///< per vector, block row and word: the pixels of the blocks it may take
    double fieldMs = 0, composeMs = 0, credibleMs = 0, masksMs = 0;
    std::vector<uint64_t> result;           ///< planes x words of a row
    uint64_t spread[256];                   ///< a byte's bits, one a byte

    Lite()
    {
        for (int b = 0; b < 256; b++)
        {
            uint64_t s = 0;
            for (int i = 0; i < 8; i++)
                if (b & (1 << i))
                    s |= 1ull << (8 * i);
            spread[b] = s;
        }
    }

    BitField bitField;
    LiteField liteField;
    bool referenceField = std::getenv("LITE_FIELD") != nullptr;
    bool ownField = false;  ///< c-lite's field of its own (litefield.h): pairs in order only
    bool still3 = false;    ///< a pixel the same in three frames stays, before any vector
    bool carry = false;     ///< an unfitted pixel of a moving block: the nearer frame moved by the block's vector
    bool ownColour = false;  ///< a vector fits only a pixel whose own colour it carries
    /// A moving vector may take pixels of a block only where it explains changes: in the 3 x 3 blocks round it, the
    /// changed pixels it carries both ways (a(y) to b(y + v), b(y) from a(y - v)) are some, and no fewer than the
    /// changes no vector explains there. The reference's `credible` with blocks for its support window: colour
    /// changing in place fits a moving vector by chance and explains nothing
    bool credible = false;
    bool credibleWords = false;  ///< the credible counts only in the words round a vector's candidate blocks
    std::vector<int> _roundRow;
    std::vector<int> explains, unexplained;  ///< per vector and block / per block: the counts, then their 3 x 3 sums

    /// `own` and `near` cut to the blocks where their vector is credible
    void Credible(const Planes& a, const Planes& b)
    {
        const int planes = a.planes, words = a.Words(), bw = grid.BlocksWide(), bh = grid.BlocksHigh();
        const size_t blocks = static_cast<size_t>(bw) * bh;
        const size_t n = vectors.size();
        explains.assign(n * blocks, 0);
        unexplained.assign(blocks, 0);
        std::vector<uint64_t> any(static_cast<size_t>(g_height) * words, 0);
        const size_t perVector = static_cast<size_t>(bh) * words;
        // a vector is counted only in the block rows where its support may be asked: a candidate there or next to it
        std::vector<uint8_t> candidate(static_cast<size_t>(bh));
        for (size_t c = 0; c < n; c++)
        {
            const V v = vectors[c];
            for (int j = 0; j < bh; j++)
            {
                uint64_t some = 0;
                for (int w = 0; w < words; w++)
                    some |= own[c * perVector + static_cast<size_t>(j) * words + w] |
                            near[c * perVector + static_cast<size_t>(j) * words + w];
                candidate[static_cast<size_t>(j)] = some != 0;
            }
            for (int j = 0; j < bh; j++)
            {
                if (!candidate[static_cast<size_t>(j)] && !(j > 0 && candidate[static_cast<size_t>(j) - 1]) &&
                    !(j + 1 < bh && candidate[static_cast<size_t>(j) + 1]))
                    continue;
                // ... and only in the words with a candidate block or one next to it
                unsigned wordMask = ~0u;
                if (credibleWords)
                {
                    wordMask = 0;
                    for (int dj = -1; dj <= 1; dj++)
                        if (j + dj >= 0 && j + dj < bh)
                            for (int w = 0; w < words; w++)
                                if (near[c * perVector + static_cast<size_t>(j + dj) * words + w])
                                    wordMask |= 7u << w >> 1;
                }
                uint64_t sums[16] = {};
                for (int r = 0; r < MotionField::kBlock; r++)
                {
                    const int y = j * MotionField::kBlock + r;
                    for (int w = 0; w < words; w++)
                    {
                        if (!((wordMask >> w) & 1))
                            continue;
                        const int jj = w + 1;
                        uint64_t same = ~0ull, onward = ~0ull, from = ~0ull;
                        for (int p = 0; p < planes; p++)
                        {
                            const uint64_t pa = a.Row(p, y)[jj], pb = b.Row(p, y)[jj];
                            same &= ~(pa ^ pb);
                            onward &= ~(pa ^ Shifted(b.Row(p, y + v.y), jj, v.x));
                            from &= ~(pb ^ Shifted(a.Row(p, y - v.y), jj, -v.x));
                        }
                        const uint64_t explained = onward & from & ~same;
                        any[static_cast<size_t>(y) * words + w] |= explained;
                        sums[w] += ByteOnes(explained);
                    }
                }
                for (int i = 0; i < bw; i++)
                    explains[c * blocks + static_cast<size_t>(j) * bw + i] =
                        static_cast<int>((sums[i >> 3] >> (8 * (i & 7))) & 0xFF);
            }
        }
        for (int j = 0; j < bh; j++)
        {
            uint64_t sums[16] = {};
            for (int r = 0; r < MotionField::kBlock; r++)
            {
                const int y = j * MotionField::kBlock + r;
                for (int w = 0; w < words; w++)
                {
                    uint64_t same = ~0ull;
                    for (int p = 0; p < planes; p++)
                        same &= ~(a.Row(p, y)[w + 1] ^ b.Row(p, y)[w + 1]);
                    uint64_t changed = ~same & ~any[static_cast<size_t>(y) * words + w];
                    if (w == words - 1 && (g_width & 63))
                        changed &= (1ull << (g_width & 63)) - 1;  // the margin past the picture is not a change
                    sums[w] += ByteOnes(changed);
                }
            }
            for (int i = 0; i < bw; i++)
                unexplained[static_cast<size_t>(j) * bw + i] = static_cast<int>((sums[i >> 3] >> (8 * (i & 7))) & 0xFF);
        }
        // 3 x 3 sums, in place: along the rows, then down the columns
        const auto round = [&](int* counts) {
            _roundRow.resize(static_cast<size_t>(bw) * bh);
            int* across = _roundRow.data();
            for (int j = 0; j < bh; j++)
                for (int i = 0; i < bw; i++)
                {
                    const int* at = counts + static_cast<size_t>(j) * bw + i;
                    across[static_cast<size_t>(j) * bw + i] = at[0] + (i > 0 ? at[-1] : 0) + (i + 1 < bw ? at[1] : 0);
                }
            for (int j = 0; j < bh; j++)
                for (int i = 0; i < bw; i++)
                {
                    const int* at = across + static_cast<size_t>(j) * bw + i;
                    counts[static_cast<size_t>(j) * bw + i] = at[0] + (j > 0 ? at[-bw] : 0) + (j + 1 < bh ? at[bw] : 0);
                }
        };
        round(unexplained.data());
        const size_t per = static_cast<size_t>(bh) * words;
        for (size_t c = 0; c < n; c++)
        {
            round(explains.data() + c * blocks);
            for (int j = 0; j < bh; j++)
                for (int i = 0; i < bw; i++)
                {
                    const int support = explains[c * blocks + static_cast<size_t>(j) * bw + i];
                    if (support > 0 && support >= unexplained[static_cast<size_t>(j) * bw + i])
                        continue;
                    const uint64_t byte = ~(0xFFull << (8 * (i & 7)));
                    own[c * per + static_cast<size_t>(j) * words + (i >> 3)] &= byte;
                    near[c * per + static_cast<size_t>(j) * words + (i >> 3)] &= byte;
                }
        }
    }
    long rowScores = 0, pairs = 0;

    void Prepare(int k)
    {
        cachedFrom = k;
        pairs++;
        if (ownField)
        {
            liteField.Estimate(g_planes[static_cast<size_t>(Clamped(k))], g_planes[static_cast<size_t>(Clamped(k + 1))],
                               MotionField::kRangeFrames, grid);
            rowScores += liteField.rowScores;
        }
        else if (referenceField)
        {
            field.Estimate(PictureAt(k), PictureAt(k + 1), MotionField::kRangeFrames);
            grid.From(field);
        }
        else
        {
            bitField.Estimate(g_planes[static_cast<size_t>(Clamped(k))], g_planes[static_cast<size_t>(Clamped(k + 1))],
                              MotionField::kRangeFrames, field);
            grid.From(field);
        }
        const int bw = grid.BlocksWide(), bh = grid.BlocksHigh();
        // the moving vectors by their blocks, most first (equals: the first met)
        std::vector<std::pair<V, int>> counts;
        int16_t indexOf[64][64];  // by the vector's low bits: a field's range is under 32
        std::memset(indexOf, -1, sizeof(indexOf));
        for (const MotionField::Block& block : grid.blocks)
        {
            if (!block.matched || block.v == V{})
                continue;
            int16_t& index = indexOf[block.v.y & 63][block.v.x & 63];
            if (index < 0)
            {
                index = static_cast<int16_t>(counts.size());
                counts.push_back({block.v, 1});
            }
            else
                counts[static_cast<size_t>(index)].second++;
        }
        std::stable_sort(counts.begin(), counts.end(), [](const auto& p, const auto& q) { return p.second > q.second; });
        if (counts.size() > kVectors)
            counts.resize(kVectors);
        vectors.clear();
        for (const auto& c : counts)
            vectors.push_back(c.first);
        if (const char* dump = std::getenv("LITE_DUMP"); dump && std::atoi(dump) == k)
        {
            // the pair's field: each block's vector as its index among the kept ones (. still, ? unmatched)
            std::printf("pair %d, vectors:", k);
            for (size_t c = 0; c < counts.size(); c++)
                std::printf(" %c=(%d,%d)x%d", static_cast<char>('a' + c), counts[c].first.x, counts[c].first.y, counts[c].second);
            std::printf("\n");
            for (int j = 0; j < bh; j++)
            {
                for (int i = 0; i < bw; i++)
                {
                    const MotionField::Block& block = grid.At(i, j);
                    char ch = !block.matched ? '?' : block.v == V{} ? '.' : '*';
                    for (size_t c = 0; c < vectors.size(); c++)
                        if (block.matched && block.v == vectors[c])
                            ch = static_cast<char>('a' + c);
                    std::putchar(ch);
                }
                std::putchar('\n');
            }
        }
        const int words = g_planes[0].Words();
        const size_t per = static_cast<size_t>(bh) * words;
        own.assign(vectors.size() * per, 0);
        near.assign(vectors.size() * per, 0);
        // own: one pass over the blocks; near: own spread a block each way, by words
        for (int j = 0; j < bh; j++)
            for (int i = 0; i < bw; i++)
            {
                const MotionField::Block& block = grid.At(i, j);
                if (!block.matched || block.v == V{})
                    continue;
                for (size_t c = 0; c < vectors.size(); c++)
                    if (block.v == vectors[c])
                    {
                        own[c * per + static_cast<size_t>(j) * words + (i >> 3)] |= 0xFFull << (8 * (i & 7));
                        break;
                    }
            }
        uint64_t tail = ~0ull;  // the blocks of the last word that exist
        if (bw & 7)
            tail = (1ull << (8 * (bw & 7))) - 1;
        std::vector<uint64_t> wide(static_cast<size_t>(words));
        for (size_t c = 0; c < vectors.size(); c++)
        {
            uint64_t* nearOf = near.data() + c * per;
            const uint64_t* ownOf = own.data() + c * per;
            if (c < kEverywhere)
            {
                for (int j = 0; j < bh; j++)
                    for (int w = 0; w < words; w++)
                        nearOf[static_cast<size_t>(j) * words + w] = w == words - 1 ? tail : ~0ull;
                continue;
            }
            for (int j = 0; j < bh; j++)
            {
                const uint64_t* row = ownOf + static_cast<size_t>(j) * words;
                bool some = false;
                for (int w = 0; w < words; w++)
                {
                    uint64_t spread = row[w] | (row[w] << 8) | (row[w] >> 8);
                    if (w > 0)
                        spread |= row[w - 1] >> 56;
                    if (w + 1 < words)
                        spread |= row[w + 1] << 56;
                    wide[static_cast<size_t>(w)] = w == words - 1 ? spread & tail : spread;
                    some |= spread != 0;
                }
                if (!some)
                    continue;
                for (int dj = -1; dj <= 1; dj++)
                    if (j + dj >= 0 && j + dj < bh)
                        for (int w = 0; w < words; w++)
                            nearOf[static_cast<size_t>(j + dj) * words + w] |= wide[static_cast<size_t>(w)];
            }
        }
    }

    void Render(int k, int w, uint32_t* out)
    {
        if (cachedFrom != k)
        {
            const auto start = std::chrono::steady_clock::now();
            Prepare(k);
            fieldMs += MsSince(start);
        }
        if (credible && credibleFor != k)
        {
            const auto start = std::chrono::steady_clock::now();
            credibleFor = k;
            Credible(g_planes[static_cast<size_t>(Clamped(k))], g_planes[static_cast<size_t>(Clamped(k + 1))]);
            credibleMs += MsSince(start);
        }
        const auto start = std::chrono::steady_clock::now();
        const Planes& a = g_planes[static_cast<size_t>(Clamped(k))];
        const Planes& b = g_planes[static_cast<size_t>(Clamped(k + 1))];
        const Planes& before = g_planes[static_cast<size_t>(Clamped(k - 1))];
        const Planes& nearer = w < kOne / 2 ? a : b;
        const int planes = a.planes, words = a.Words(), bh = grid.BlocksHigh();
        const size_t per = static_cast<size_t>(bh) * words;
        const int n = static_cast<int>(vectors.size());
        int sx[kVectors], sy[kVectors];
        for (int c = 0; c < n; c++)
        {
            sx[c] = (w * vectors[static_cast<size_t>(c)].x + kOne / 2) >> 8;
            sy[c] = (w * vectors[static_cast<size_t>(c)].y + kOne / 2) >> 8;
        }
        const uint32_t* colours = g_palette.colours.data();
        for (int y = 0; y < g_height; y++)
        {
            const size_t blockRow = static_cast<size_t>(y / MotionField::kBlock) * words;
            for (int j = 0; j < words; j++)
            {
                const int jj = j + 1;
                uint64_t fit[kVectors];
                uint64_t res[kMaxPlanes] = {};
                uint64_t taken = 0;
                // per vector: a(x - s) against b(x - s + v)
                for (int c = 0; c < n; c++)
                {
                    const V v = vectors[static_cast<size_t>(c)];
                    // a fit is asked only under the vector's blocks (near holds own): no candidate in this word,
                    // no tests
                    if (!near[static_cast<size_t>(c) * per + blockRow + j])
                    {
                        fit[c] = 0;
                        continue;
                    }
                    uint64_t eq = ~0ull;
                    for (int p = 0; p < planes; p++)
                        eq &= ~(Shifted(a.Row(p, y - sy[c]), jj, -sx[c]) ^
                                Shifted(b.Row(p, y - sy[c] + v.y), jj, -sx[c] + v.x));
                    if (ownColour)
                    {
                        // ... and the vector carries the pixel's own colour in one frame: a(x) to b(x + v), or b(x)
                        // from a(x - v) (the reference's fit). A still pixel over a moving layer keeps its place
                        uint64_t onward = ~0ull, from = ~0ull;
                        for (int p = 0; p < planes; p++)
                        {
                            onward &= ~(a.Row(p, y)[jj] ^ Shifted(b.Row(p, y + v.y), jj, v.x));
                            from &= ~(b.Row(p, y)[jj] ^ Shifted(a.Row(p, y - v.y), jj, -v.x));
                        }
                        eq &= onward | from;
                    }
                    fit[c] = eq;
                }
                const auto take = [&](uint64_t mask, int c) {
                    if (!mask)
                        return;
                    taken |= mask;
                    for (int p = 0; p < planes; p++)
                        res[p] |= mask & Shifted(a.Row(p, y - sy[c]), jj, -sx[c]);
                };
                // still: the same pixel in k - 1, k and k + 1 stays, whatever moves round it (a panel's text over a
                // scroll took the scroll's vector where the shifted samples happened to agree)
                if (still3)
                {
                    uint64_t same = ~0ull;
                    for (int p = 0; p < planes; p++)
                        same &= ~(a.Row(p, y)[jj] ^ b.Row(p, y)[jj]) & ~(a.Row(p, y)[jj] ^ before.Row(p, y)[jj]);
                    taken |= same;
                    for (int p = 0; p < planes; p++)
                        res[p] |= same & a.Row(p, y)[jj];
                }
                // the block's own vector
                for (int c = 0; c < n; c++)
                    take(fit[c] & own[static_cast<size_t>(c) * per + blockRow + j] & ~taken, c);
                // still
                {
                    uint64_t same = ~0ull;
                    for (int p = 0; p < planes; p++)
                        same &= ~(a.Row(p, y)[jj] ^ b.Row(p, y)[jj]);
                    const uint64_t mask = same & ~taken;
                    taken |= mask;
                    for (int p = 0; p < planes; p++)
                        res[p] |= mask & a.Row(p, y)[jj];
                }
                // a vector of the blocks round it
                for (int c = 0; c < n; c++)
                    take(fit[c] & near[static_cast<size_t>(c) * per + blockRow + j] & ~taken, c);
                // nothing fits in a block that moves: the nearer frame moved as the block is (content coming in at a
                // layer's edge, a shape changing on the move), not the nearer frame where it stood
                if (carry)
                    for (int c = 0; c < n; c++)
                    {
                        const uint64_t mask = own[static_cast<size_t>(c) * per + blockRow + j] & ~taken;
                        if (!mask)
                            continue;
                        taken |= mask;
                        const V v = vectors[static_cast<size_t>(c)];
                        for (int p = 0; p < planes; p++)
                            res[p] |= mask & (w < kOne / 2 ? Shifted(a.Row(p, y - sy[c]), jj, -sx[c])
                                                           : Shifted(b.Row(p, y - sy[c] + v.y), jj, -sx[c] + v.x));
                    }
                // nothing fits: the nearer frame
                for (int p = 0; p < planes; p++)
                    res[p] |= ~taken & nearer.Row(p, y)[jj];
                // the indices, eight pixels at a time, and their colours
                uint32_t* line = out + static_cast<size_t>(y) * g_width + static_cast<size_t>(j) * 64;
                const int count = std::min(64, g_width - j * 64);
                // one colour over the word (paper, border): filled
                bool flat = true;
                unsigned flatIndex = 0;
                for (int p = 0; p < planes; p++)
                {
                    flat &= res[p] == 0 || res[p] == ~0ull;
                    flatIndex |= static_cast<unsigned>(res[p] & 1) << p;
                }
                if (flat)
                {
                    std::fill(line, line + count, colours[flatIndex]);
                    continue;
                }
                for (int byte = 0; byte * 8 < count; byte++)
                {
                    uint64_t index = 0;
                    for (int p = 0; p < planes; p++)
                        index |= spread[(res[p] >> (8 * byte)) & 0xFF] << p;
                    const int pixels = std::min(8, count - byte * 8);
                    for (int i = 0; i < pixels; i++)
                        line[byte * 8 + i] = colours[(index >> (8 * i)) & 0xFF];
                }
            }
        }
        composeMs += MsSince(start);
    }
};

struct Filter
{
    std::string name;
    std::function<void(int64_t, uint32_t*)> render;
    double ms = 0;
    int calls = 0;
};
}  // namespace

int main(int argc, char** argv)
{
    if (argc < 6)
    {
        std::fprintf(stderr, "usage: lite <source.rgba> <width> <height> <frames> <out dir> [rom] [source Hz] [display Hz]\n");
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
    // the clip's colours, then every frame's planes
    for (uint32_t pixel : g_frames)
        if (g_palette.Index(pixel) < 0)
        {
            std::fprintf(stderr, "more than 256 colours\n");
            return 1;
        }
    g_planes.resize(static_cast<size_t>(g_count));
    double packMs = 0;
    for (int k = 0; k < g_count; k++)
    {
        const auto start = std::chrono::steady_clock::now();
        g_planes[static_cast<size_t>(k)].Pack(PictureAt(k).pixels, g_width, g_height, g_palette, g_palette.Planes());
        packMs += MsSince(start);
    }

    if (std::getenv("LITE_CHECK_FIELD"))
    {
        // b-bitplanes: the field on the planes against the reference's, pair by pair
        MotionField reference, onPlanes;
        BitField bits;
        double referenceMs = 0, planesMs = 0;
        long differ = 0, blocks = 0, scored = 0;
        for (int k = 0; k + 1 < g_count; k++)
        {
            auto start = std::chrono::steady_clock::now();
            reference.Estimate(PictureAt(k), PictureAt(k + 1), MotionField::kRangeFrames);
            referenceMs += MsSince(start);
            start = std::chrono::steady_clock::now();
            bits.Estimate(g_planes[static_cast<size_t>(k)], g_planes[static_cast<size_t>(k + 1)],
                          MotionField::kRangeFrames, onPlanes);
            planesMs += MsSince(start);
            scored += bits.vectorsScored;
            for (int j = 0; j < reference.BlocksHigh(); j++)
                for (int i = 0; i < reference.BlocksWide(); i++, blocks++)
                    differ += reference.At(i, j).matched != onPlanes.At(i, j).matched ||
                              !(reference.At(i, j).v == onPlanes.At(i, j).v);
        }
        const int pairs = g_count - 1;
        std::printf("field: %d pairs, %ld of %ld blocks differ; the reference %.3f ms a pair, on planes %.3f ms "
                    "(%.1f vectors' worth of rows scored a pair), packing %.3f ms a frame\n",
                    pairs, differ, blocks, referenceMs / pairs, planesMs / pairs,
                    static_cast<double>(scored) / pairs / reference.BlocksHigh(), packMs / g_count);
        return differ != 0;
    }

    auto layers = std::make_shared<Layers>();
    auto lite = std::make_shared<Lite>();
    auto lite2 = std::make_shared<Lite>();
    lite2->ownField = true;
    auto lite3 = std::make_shared<Lite>();
    lite3->ownField = true;
    lite3->still3 = true;
    auto lite4 = std::make_shared<Lite>();
    lite4->ownField = true;
    lite4->still3 = true;
    lite4->carry = true;
    auto lite5 = std::make_shared<Lite>();
    lite5->ownField = true;
    lite5->ownColour = true;
    auto lite6 = std::make_shared<Lite>();
    lite6->ownField = true;
    lite6->ownColour = true;
    lite6->carry = true;
    auto lite7 = std::make_shared<Lite>();
    lite7->ownField = lite7->ownColour = lite7->carry = lite7->credible = true;
    auto lite8 = std::make_shared<Lite>();
    lite8->ownField = lite8->ownColour = lite8->carry = lite8->credible = true;
    lite8->liteField.budgetOfPair = 864;
    auto lite9 = std::make_shared<Lite>();
    lite9->ownField = lite9->ownColour = lite9->carry = lite9->credible = true;
    lite9->liteField.slowestOfEquals = true;
    auto lite10 = std::make_shared<Lite>();
    lite10->ownField = lite10->ownColour = lite10->carry = lite10->credible = true;
    lite10->liteField.slowestOfEquals = true;
    lite10->liteField.budgetOfPair = 864;
    auto lite11 = std::make_shared<Lite>();
    lite11->ownField = lite11->ownColour = lite11->carry = lite11->credible = true;
    lite11->liteField.slowestOfEquals = lite11->liteField.verifySlower = true;
    auto lite12 = std::make_shared<Lite>();
    lite12->ownField = lite12->ownColour = lite12->carry = lite12->credible = true;
    lite12->liteField.slowestOfEquals = lite12->liteField.verifySlower = true;
    lite12->liteField.budgetOfPair = 864;
    auto lite13 = std::make_shared<Lite>();
    lite13->ownField = lite13->ownColour = lite13->carry = lite13->credible = true;
    lite13->liteField.slowestOfEquals = lite13->liteField.verifySlower = lite13->liteField.regions = true;
    auto lite14 = std::make_shared<Lite>();
    lite14->ownField = lite14->ownColour = lite14->carry = lite14->credible = true;
    lite14->liteField.slowestOfEquals = lite14->liteField.regions = true;
    auto lite15 = std::make_shared<Lite>();
    lite15->ownField = lite15->ownColour = lite15->carry = lite15->credible = lite15->credibleWords = true;
    lite15->liteField.slowestOfEquals = lite15->liteField.regions = lite15->liteField.searchOpenWords = true;
    std::vector<Filter> all;
    all.push_back({"off", [](int64_t t, uint32_t* out) {
                       int k, w;
                       DueAt(t, k, w);
                       std::copy_n(PictureAt(k).pixels, Pixels(), out);
                   }});
    all.push_back({"blend", [](int64_t t, uint32_t* out) {
                       int k, w;
                       DueAt(t, k, w);
                       Blend(PictureAt(k), PictureAt(k + 1), w, out);
                   }});
    all.push_back({"layers", [layers](int64_t t, uint32_t* out) {
                       int k, w;
                       DueAt(t, k, w);
                       layers->Render(k, w, out);
                   }});
    all.push_back({"lite", [lite](int64_t t, uint32_t* out) {
                       int k, w;
                       DueAt(t, k, w);
                       lite->Render(k, w, out);
                   }});
    all.push_back({"lite2", [lite2](int64_t t, uint32_t* out) {
                       int k, w;
                       DueAt(t, k, w);
                       lite2->Render(k, w, out);
                   }});
    all.push_back({"lite3", [lite3](int64_t t, uint32_t* out) {
                       int k, w;
                       DueAt(t, k, w);
                       lite3->Render(k, w, out);
                   }});
    all.push_back({"lite4", [lite4](int64_t t, uint32_t* out) {
                       int k, w;
                       DueAt(t, k, w);
                       lite4->Render(k, w, out);
                   }});
    all.push_back({"lite5", [lite5](int64_t t, uint32_t* out) {
                       int k, w;
                       DueAt(t, k, w);
                       lite5->Render(k, w, out);
                   }});
    all.push_back({"lite6", [lite6](int64_t t, uint32_t* out) {
                       int k, w;
                       DueAt(t, k, w);
                       lite6->Render(k, w, out);
                   }});
    all.push_back({"lite7", [lite7](int64_t t, uint32_t* out) {
                       int k, w;
                       DueAt(t, k, w);
                       lite7->Render(k, w, out);
                   }});
    all.push_back({"lite8", [lite8](int64_t t, uint32_t* out) {
                       int k, w;
                       DueAt(t, k, w);
                       lite8->Render(k, w, out);
                   }});
    all.push_back({"lite9", [lite9](int64_t t, uint32_t* out) {
                       int k, w;
                       DueAt(t, k, w);
                       lite9->Render(k, w, out);
                   }});
    all.push_back({"lite10", [lite10](int64_t t, uint32_t* out) {
                       int k, w;
                       DueAt(t, k, w);
                       lite10->Render(k, w, out);
                   }});
    all.push_back({"lite11", [lite11](int64_t t, uint32_t* out) {
                       int k, w;
                       DueAt(t, k, w);
                       lite11->Render(k, w, out);
                   }});
    all.push_back({"lite12", [lite12](int64_t t, uint32_t* out) {
                       int k, w;
                       DueAt(t, k, w);
                       lite12->Render(k, w, out);
                   }});
    all.push_back({"lite13", [lite13](int64_t t, uint32_t* out) {
                       int k, w;
                       DueAt(t, k, w);
                       lite13->Render(k, w, out);
                   }});
    all.push_back({"lite14", [lite14](int64_t t, uint32_t* out) {
                       int k, w;
                       DueAt(t, k, w);
                       lite14->Render(k, w, out);
                   }});
    all.push_back({"lite15", [lite15](int64_t t, uint32_t* out) {
                       int k, w;
                       DueAt(t, k, w);
                       lite15->Render(k, w, out);
                   }});
    std::vector<std::pair<std::string, std::shared_ptr<Lite>>> more;
    for (int form = 16; form <= 23; form++)
    {
        auto f = std::make_shared<Lite>();
        f->ownField = f->ownColour = f->carry = f->credible = true;
        f->liteField.slowestOfEquals = f->liteField.regions = true;
        f->liteField.sleepRows = form == 16 || form == 18;
        f->liteField.keptLocal = form == 17 || form == 18;
        if (form == 19)
            f->liteField.budgetOfPair = 144;
        if (form == 20)
            f->liteField.budgetOfPair = 72;
        if (form >= 21)
        {
            f->liteField.budgetOfPair = 144;
            f->liteField.keptLimit = form == 21 ? 6 : form == 22 ? 4 : 3;
        }
        more.push_back({"lite" + std::to_string(form), f});
        all.push_back({more.back().first, [f](int64_t t, uint32_t* out) {
                           int k, w;
                           DueAt(t, k, w);
                           f->Render(k, w, out);
                       }});
    }
    more.push_back({"lite14", lite14});
    std::vector<Filter> filters;
    const std::string list = std::getenv("FILTERS_ONLY") ? std::getenv("FILTERS_ONLY") : "off,blend,layers,lite";
    for (size_t start = 0; start <= list.size();)
    {
        const size_t comma = std::min(list.find(',', start), list.size());
        const std::string name = list.substr(start, comma - start);
        for (const Filter& f : all)
            if (f.name == name)
                filters.push_back(f);
        start = comma + 1;
    }
    std::vector<FILE*> raws;
    for (const Filter& f : filters)
        raws.push_back(std::fopen((outDir + "/" + f.name + ".rgba").c_str(), "wb"));

    std::vector<uint32_t> out(Pixels());
    const int64_t phase = 3'000'000;  // the refreshes off the frames' grid, as the harness
    const int64_t end = (g_count - 3) * g_framePeriod;
    int refreshes = 0;
    for (int64_t t = phase; t < end; t += refreshPeriod, refreshes++)
        for (size_t i = 0; i < filters.size(); i++)
        {
            const auto start = std::chrono::steady_clock::now();
            filters[i].render(t, out.data());
            filters[i].ms += MsSince(start);
            filters[i].calls++;
            if (raws[i])
                std::fwrite(out.data(), 4, out.size(), raws[i]);
        }
    for (FILE* r : raws)
        if (r)
            std::fclose(r);
    std::printf("%s: %d refreshes, %zu colours (%d planes), packing %.3f ms a frame\n", outDir.c_str(), refreshes,
                g_palette.colours.size(), g_palette.Planes(), packMs / g_count);
    for (const Filter& f : filters)
        std::printf("  %-13s %6.3f ms per refresh\n", f.name.c_str(), f.calls ? f.ms / f.calls : 0.0);
    if (std::getenv("LITE_STATS"))
        for (const auto& [name, f] : more)
            if (f->composeMs > 0)
            {
                const LiteField& l = f->liteField;
                const double pairs = static_cast<double>(f->pairs);
                std::printf("  %s a pair: zero %.0f, kept %.0f (%.0f on open rows; %.1f kept vectors), search %.0f; rows "
                            "open after zero %.1f, at the end %.1f with %.1f blocks, asleep %.1f\n",
                            name.c_str(), l.zeroScores / pairs, l.keptScores / pairs, l.keptOnOpen / pairs,
                            l.keptCount / pairs, (l.scored - l.zeroScores - l.keptScores) / pairs,
                            l.openRowsAfterZero / pairs, l.openRowsAtEnd / pairs, l.openBlocksAtEnd / pairs,
                            l.asleep / pairs);
                std::printf("    kept vectors by rank, pairs used %% / blocks a pair:");
                for (int c = 0; c < 12; c++)
                    std::printf(" %.0f/%.1f", 100.0 * l.usedAtRank[c] / pairs, l.blocksAtRank[c] / pairs);
                std::printf("\n");
            }
    for (const auto& [name, f] : more)
        if (f->composeMs > 0)
            std::printf("  %s: the field %.3f ms a refresh (%.1f row scores a pair, %.0f %% stopped early, counts %.3f "
                        "ms), credible %.3f ms, the composition %.3f ms\n",
                        name.c_str(), f->fieldMs / refreshes, static_cast<double>(f->rowScores) / f->pairs,
                        100.0 * f->liteField.abandoned / std::max(1LL, f->liteField.scored),
                        f->liteField.countsNs / 1e6 / refreshes, f->credibleMs / refreshes, f->composeMs / refreshes);
    if (lite->composeMs > 0)
        std::printf("  lite: the field %.3f ms a refresh, the composition %.3f ms\n",
                    lite->fieldMs / refreshes, lite->composeMs / refreshes);
    if (lite2->composeMs > 0)
        std::printf("  lite2: the field %.3f ms a refresh (%.1f row scores a pair, %.1f pictures' worth), the "
                    "composition %.3f ms\n",
                    lite2->fieldMs / refreshes, static_cast<double>(lite2->rowScores) / lite2->pairs,
                    static_cast<double>(lite2->rowScores) / lite2->pairs / (g_height / 8), lite2->composeMs / refreshes);
    return 0;
}
