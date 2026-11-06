#include "smoothlayers.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <utility>

#include "common/threadpool.h"

namespace smoothmotion
{

namespace
{
using V = LayerFlow::V;

/// A picture read clamped to its edges like Picture::At: a copy with its edges repeated `margin` pixels out, read
/// without a clamp (the per-pixel passes read it most); reads stay within the margin
struct View
{
    int width, height, stride;
    const uint32_t* origin;

    /// `padded`: the copy's storage
    View(const Picture& picture, int margin, std::vector<uint32_t>& padded)
        : width(picture.width), height(picture.height), stride(picture.width + 2 * margin)
    {
        padded.resize(static_cast<size_t>(stride) * (picture.height + 2 * margin));
        for (int y = 0; y < height + 2 * margin; y++)
        {
            const uint32_t* row = picture.pixels + static_cast<size_t>(std::clamp(y - margin, 0, height - 1)) * width;
            uint32_t* to = padded.data() + static_cast<size_t>(y) * stride;
            std::fill(to, to + margin, row[0]);
            std::copy(row, row + width, to + margin);
            std::fill(to + margin + width, to + stride, row[width - 1]);
        }
        origin = padded.data() + static_cast<size_t>(margin) * stride + margin;
    }

    uint32_t At(int x, int y) const { return origin[static_cast<ptrdiff_t>(y) * stride + x]; }
    /// The row y from the column x on
    const uint32_t* Row(int x, int y) const { return origin + static_cast<ptrdiff_t>(y) * stride + x; }
};

/// `out` = the sums of `in` (width x height) over the window of `radius` round every pixel, clipped to the picture:
/// per row, the column sums of the window's rows, then the window along the row
template <typename T>
void WindowSums(const uint8_t* in, int width, int height, int radius, T* out, std::vector<int>& column)
{
    column.assign(width, 0);
    // local pointers: a byte store may alias a vector's own pointer, its reload would stop the vectorizer
    int* const sums = column.data();
    for (int y = 0; y < std::min(radius, height); y++)
        for (int x = 0; x < width; x++)
            sums[x] += in[static_cast<size_t>(y) * width + x];
    for (int y = 0; y < height; y++)
    {
        if (y + radius < height)
        {
            const uint8_t* enter = in + static_cast<size_t>(y + radius) * width;
            for (int x = 0; x < width; x++)
                sums[x] += enter[x];
        }
        if (y - radius - 1 >= 0)
        {
            const uint8_t* leave = in + static_cast<size_t>(y - radius - 1) * width;
            for (int x = 0; x < width; x++)
                sums[x] -= leave[x];
        }
        T* line = out + static_cast<size_t>(y) * width;
        int sum = 0;
        for (int x = 0; x < std::min(radius, width); x++)
            sum += sums[x];
        if (width > 2 * radius + 1)
        {
            // the window enters, slides, leaves: no clipping tests in the row
            int x = 0;
            for (; x <= radius; x++)
            {
                sum += sums[x + radius];
                line[x] = static_cast<T>(sum);
            }
            for (; x + radius < width; x++)
            {
                sum += sums[x + radius] - sums[x - radius - 1];
                line[x] = static_cast<T>(sum);
            }
            for (; x < width; x++)
            {
                sum -= sums[x - radius - 1];
                line[x] = static_cast<T>(sum);
            }
            continue;
        }
        for (int x = 0; x < width; x++)
        {
            if (x + radius < width)
                sum += sums[x + radius];
            if (x - radius - 1 >= 0)
                sum -= sums[x - radius - 1];
            line[x] = static_cast<T>(sum);
        }
    }
}

/// `counts` = per kBlock x kBlock block of a 0/1 image (width x height), its ones (a block's 64 at most)
void BlockCounts(const uint8_t* ones, int width, int height, uint8_t* counts)
{
    constexpr int kb = MotionField::kBlock;
    static_assert(kb == 8, "a block's row is one 64-bit word");
    const int blocksWide = (width + kb - 1) / kb, blocksHigh = (height + kb - 1) / kb;
    std::fill(counts, counts + static_cast<size_t>(blocksWide) * blocksHigh, uint8_t{0});
    for (int y = 0; y < height; y++)
    {
        const uint8_t* row = ones + static_cast<size_t>(y) * width;
        uint8_t* line = counts + static_cast<size_t>(y / kb) * blocksWide;
        int i = 0;
        for (; (i + 1) * kb <= width; i++)
        {
            uint64_t word;
            std::memcpy(&word, row + i * kb, sizeof(word));
            line[i] = static_cast<uint8_t>(line[i] + std::popcount(word));
        }
        for (int x = i * kb; x < width; x++)
            line[i] = static_cast<uint8_t>(line[i] + row[x]);
    }
}

bool Slower(V u, V v)
{
    return u.x * u.x + u.y * u.y < v.x * v.x + v.y * v.y;
}

/// carryFirst's test over a picture read clamped (Picture, View)
template <typename Reader>
bool ExplainsIn(const Reader& a, const Reader& b, int width, int height, int bi, int bj, V v)
{
    int changes = 0, explained = 0;
    for (int y = bj * MotionField::kBlock; y < std::min((bj + 1) * MotionField::kBlock, height); y++)
        for (int x = bi * MotionField::kBlock; x < std::min((bi + 1) * MotionField::kBlock, width); x++)
            if (a.At(x, y) != b.At(x, y))
            {
                changes++;
                explained += b.At(x, y) == a.At(x - v.x, y - v.y) || a.At(x, y) == b.At(x + v.x, y + v.y);
            }
    return explained * 2 >= changes;
}

/// The block decisions' answers from Compose's buffers, each worked out when asked
class CpuEvidence final : public LayerFlow::Evidence
{
public:
    using Top = LayerFlow::TopOf;

    /// `topAt`: the caller's buffer, a block's pixels
    CpuEvidence(const View& a, int blocksWide, int n, const uint8_t* fits, const uint16_t* support,
                const uint8_t* fitCounts, std::vector<std::array<Top, 3>>& topAt)
        : _a(a), _blocksWide(blocksWide), _n(n), _pixels(static_cast<size_t>(a.width) * a.height), _fits(fits),
          _support(support), _fitCounts(fitCounts), _blocks(static_cast<size_t>(blocksWide) *
                                                            ((a.height + MotionField::kBlock - 1) / MotionField::kBlock)),
          _topAt(topAt)
    {
        _topAt.resize(static_cast<size_t>(MotionField::kBlock) * MotionField::kBlock);
    }

    int Own(size_t block, int c, int s) override
    {
        const int kb = MotionField::kBlock;
        const int x0 = static_cast<int>(block % _blocksWide) * kb, x1 = std::min(x0 + kb, _a.width);
        const int y0 = static_cast<int>(block / _blocksWide) * kb, y1 = std::min(y0 + kb, _a.height);
        if (block != _topBlock)
        {
            // per pixel, the three fitting vectors with the most support: a vector other than c and s with more
            // support than c's is among them if there is one
            _topBlock = block;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++)
                {
                    auto& top = _topAt[static_cast<size_t>(y - y0) * kb + (x - x0)];
                    top = {};
                    const size_t q = static_cast<size_t>(y) * _a.width + x;
                    for (int u = 0; u < _n; u++)
                    {
                        const size_t uq = static_cast<size_t>(u) * _pixels + q;
                        if (_fitCounts[u * _blocks + block] == 0 || !_fits[uq])
                            continue;
                        Top t{u, _support[uq]};
                        for (auto& slot : top)
                            if (t.support > slot.support)
                                std::swap(t, slot);
                    }
                }
        }
        int count = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
            {
                const size_t q = static_cast<size_t>(y) * _a.width + x;
                if (!_fits[static_cast<size_t>(c) * _pixels + q] || (s >= 0 && _fits[static_cast<size_t>(s) * _pixels + q]))
                    continue;
                const int mine = _support[static_cast<size_t>(c) * _pixels + q];
                bool other = false;
                for (const Top& t : _topAt[static_cast<size_t>(y - y0) * kb + (x - x0)])
                    other |= t.vector >= 0 && t.vector != c && t.vector != s && t.support > mine;
                count += !other;
            }
        return count;
    }

    void Ask(const std::vector<std::pair<size_t, int>>& /*blocks*/, const std::vector<LayerFlow::Edge>& edges) override
    {
        _edges = &edges;
    }

    bool Continues(size_t index, int vector) override
    {
        // continued: a region takes a neighbour's vote only where the layer continues into it (its content pixels
        // at the shared edge as ambiguous as the block: the same periodic content, not another layer):
        // - (colours, own) every colour of the neighbour's edge, its paper too, is one the block has (a layer keeps
        //   its ink);
        // - (vote) the neighbour's own vector, not every tied one, fits most of its edge content (a dot row's end
        //   decides its middle though the still look-alike breaks there)
        const auto [bi, bj, di, dj] = (*_edges)[index];
        const int kb = MotionField::kBlock, width = _a.width, height = _a.height;
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
        // the neighbour's paper: its most frequent colour, the first met of equals (a block has 64 pixels at most)
        uint32_t paper = 0;
        {
            std::array<uint32_t, kBlockPixels> colours;
            std::array<int, kBlockPixels> counts;
            size_t kinds = 0;
            for (int y = (bj + dj) * kb; y < std::min((bj + dj + 1) * kb, height); y++)
                for (int x = (bi + di) * kb; x < std::min((bi + di + 1) * kb, width); x++)
                {
                    const uint32_t colour = _a.At(x, y);
                    size_t i = 0;
                    while (i < kinds && colours[i] != colour)
                        i++;
                    if (i == kinds)
                    {
                        colours[kinds] = colour;
                        counts[kinds++] = 0;
                    }
                    counts[i]++;
                }
            int most = 0;
            for (size_t i = 0; i < kinds; i++)
                if (counts[i] > most)
                {
                    most = counts[i];
                    paper = colours[i];
                }
        }
        std::array<uint32_t, kBlockPixels> own;
        size_t owned = 0;
        for (int y = bj * kb; y < std::min((bj + 1) * kb, height); y++)
            for (int x = bi * kb; x < std::min((bi + 1) * kb, width); x++)
                if (std::find(own.begin(), own.begin() + static_cast<ptrdiff_t>(owned), _a.At(x, y)) ==
                    own.begin() + static_cast<ptrdiff_t>(owned))
                    own[owned++] = _a.At(x, y);
        int content = 0, foreign = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
            {
                foreign += std::find(own.begin(), own.begin() + static_cast<ptrdiff_t>(owned), _a.At(x, y)) ==
                           own.begin() + static_cast<ptrdiff_t>(owned);
                content += _a.At(x, y) != paper;
            }
        if (content == 0 || foreign > 0)
            return false;
        // the neighbour's own vector carries its content at the edge: the layer goes on with it
        int carried = 0;
        const uint8_t* fitsOf = _fits + static_cast<size_t>(vector) * _pixels;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                carried += _a.At(x, y) != paper && fitsOf[static_cast<size_t>(y) * width + x];
        return carried * 4 >= content * 3;
    }

private:
    static constexpr size_t kBlockPixels = static_cast<size_t>(MotionField::kBlock) * MotionField::kBlock;

    const View& _a;
    int _blocksWide, _n;
    size_t _pixels;
    const uint8_t* _fits;
    const uint16_t* _support;
    const uint8_t* _fitCounts;
    size_t _blocks;
    std::vector<std::array<Top, 3>>& _topAt;
    size_t _topBlock = static_cast<size_t>(-1);
    const std::vector<LayerFlow::Edge>* _edges = nullptr;
};
}  // namespace

namespace
{
/// The matched vectors of both fields with their blocks, in the order met: `next` row by row, then `prev`
void CountMatched(const MotionField& prev, const MotionField& next, std::vector<std::pair<V, int>>& counts)
{
    counts.clear();
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
}

/// Of the entries `state` marks 0 with at least `least` blocks, the one with the most, the first met of equals; -1
int MostMatched(const std::vector<std::pair<V, int>>& counts, const std::vector<uint8_t>& state, int least)
{
    int best = -1;
    for (size_t i = 0; i < counts.size(); i++)
        if (state[i] == 0 && counts[i].second >= least && (best < 0 || counts[i].second > counts[best].second))
            best = static_cast<int>(i);
    return best;
}

/// The common vectors into `common` (zero, then the moving ones at least two blocks share, most shared first); their
/// entries and zero's marked 1 in `state`
void CommonOf(const std::vector<std::pair<V, int>>& counts, std::vector<V>& common, std::vector<uint8_t>& state)
{
    common.assign(1, V{});
    state.assign(counts.size(), 0);
    for (size_t i = 0; i < counts.size(); i++)
        if (counts[i].first == V{})
            state[i] = 1;
    while (common.size() < LayerFlow::kCommon)
    {
        const int i = MostMatched(counts, state, 2);
        if (i < 0)
            break;
        state[static_cast<size_t>(i)] = 1;
        common.push_back(counts[static_cast<size_t>(i)].first);
    }
}
}  // namespace

std::vector<V> LayerFlow::CommonVectors(const MotionField& prev, const MotionField& next)
{
    std::vector<std::pair<V, int>> counts;
    std::vector<uint8_t> state;
    std::vector<V> common;
    CountMatched(prev, next, counts);
    CommonOf(counts, common, state);
    return common;
}

void LayerFlow::Prepare(const MotionField& prev, const MotionField& next, int width, int height, Layers& layers)
{
    auto& all = layers.all;
    auto& counts = layers.counts;
    auto& state = layers.kept;  // 0: out, 1: common, 2: kept
    CountMatched(prev, next, counts);
    CommonOf(counts, all, state);
    layers.common = all.size();
    // the blocks' other vectors in the order met; past kMaxVectors the most matched
    const size_t rest = static_cast<size_t>(std::count(state.begin(), state.end(), uint8_t{0}));
    if (layers.common + rest <= kMaxVectors)
        std::replace(state.begin(), state.end(), uint8_t{0}, uint8_t{2});
    else
        for (size_t k = layers.common; k < kMaxVectors; k++)
            state[static_cast<size_t>(MostMatched(counts, state, 1))] = 2;
    for (size_t i = 0; i < counts.size(); i++)
        if (state[i] == 2)
            all.push_back(counts[i].first);
    const int bw = next.BlocksWide(), bh = next.BlocksHigh();
    const auto indexOf = [&](V v) { return static_cast<int>(std::find(all.begin(), all.end(), v) - all.begin()); };
    layers.reach = 0;
    for (const V& v : all)
        layers.reach = std::max({layers.reach, std::abs(v.x), std::abs(v.y)});
    layers.blocksWide = (width + MotionField::kBlock - 1) / MotionField::kBlock;
    layers.blocksHigh = (height + MotionField::kBlock - 1) / MotionField::kBlock;
    layers.allowed.resize(static_cast<size_t>(layers.blocksWide) * layers.blocksHigh);
    for (int bj = 0; bj < layers.blocksHigh; bj++)
        for (int bi = 0; bi < layers.blocksWide; bi++)
        {
            auto& allowed = layers.allowed[static_cast<size_t>(bj) * layers.blocksWide + bi];
            allowed.clear();
            for (int c = 0; c < static_cast<int>(layers.common); c++)
                allowed.push_back(c);
            for (const MotionField* f : {&next, &prev})
                for (int dj = -1; dj <= 1; dj++)
                    for (int di = -1; di <= 1; di++)
                    {
                        const int i = bi + di, j = bj + dj;
                        if (i < 0 || j < 0 || i >= bw || j >= bh || !f->At(i, j).matched)
                            continue;
                        const int c = indexOf(f->At(i, j).v);
                        if (c < static_cast<int>(all.size()) && std::find(allowed.begin(), allowed.end(), c) == allowed.end())
                            allowed.push_back(c);
                    }
        }
}

void LayerFlow::Palette(const Picture& a, std::vector<uint32_t>& palette, uint8_t* colourAt)
{
    palette.clear();
    for (int y = 0; y < a.height; y++)
    {
        // runs of a colour look it up once
        const uint32_t* row = a.pixels + static_cast<size_t>(y) * a.width;
        uint32_t last = row[0] + 1;
        uint8_t lastIndex = 0;
        for (int x = 0; x < a.width; x++)
        {
            const uint32_t colour = row[x];
            if (colour != last)
            {
                auto it = std::find(palette.begin(), palette.end(), colour);
                if (it == palette.end() && palette.size() < kPalette)
                {
                    palette.push_back(colour);
                    it = palette.end() - 1;
                }
                last = colour;
                lastIndex = static_cast<uint8_t>(it == palette.end() ? 255 : it - palette.begin());
            }
            if (colourAt)
                colourAt[static_cast<size_t>(y) * a.width + x] = lastIndex;
        }
    }
}

bool LayerFlow::Explains(const Picture& a, const Picture& b, int bi, int bj, V v)
{
    return ExplainsIn(a, b, a.width, a.height, bi, bj, v);
}

void LayerFlow::Decide(const Layers& layers, const Picture& a, const uint8_t* fitCounts, const uint8_t* usableCounts,
                       Evidence& evidence, std::vector<int>& blockVector, std::vector<uint8_t>& tied,
                       DecideScratch& scratch)
{
    const std::vector<V>& all = layers.all;
    const int n = static_cast<int>(all.size());
    const int blocksWide = layers.blocksWide, blocksHigh = layers.blocksHigh;
    const size_t blocks = static_cast<size_t>(blocksWide) * blocksHigh;
    blockVector.assign(blocks, -1);
    auto& tiedAt = scratch.tiedAt;  // the vectors equal to the block's best
    tiedAt.resize(blocks);
    for (auto& t : tiedAt)
        t.clear();
    auto& fitCountOf = scratch.fitCountOf;
    fitCountOf.resize(static_cast<size_t>(n));
    auto& asks = scratch.asks;
    asks.clear();
    auto& nearStill = scratch.nearStill;
    nearStill.assign(blocks, 0);
    const auto blockPixels = [&](size_t block) {
        const int kb = MotionField::kBlock;
        const int x0 = static_cast<int>(block % blocksWide) * kb, y0 = static_cast<int>(block / blocksWide) * kb;
        return (std::min(x0 + kb, a.width) - x0) * (std::min(y0 + kb, a.height) - y0);
    };
    // periodic: the block's pixels unlike themselves half the main step on (read clamped): dots a step apart are,
    // flat or finely dithered paint (an attribute plasma changing in place) is not, any shift fits it
    const auto periodic = [&](size_t block) {
        const V half{all[1].x / 2, all[1].y / 2};
        const int kb = MotionField::kBlock;
        const int x0 = static_cast<int>(block % blocksWide) * kb, y0 = static_cast<int>(block / blocksWide) * kb;
        const auto at = [&](int x, int y) {
            return a.pixels[static_cast<size_t>(std::clamp(y, 0, a.height - 1)) * a.width + std::clamp(x, 0, a.width - 1)];
        };
        int unlike = 0;
        for (int y = y0; y < std::min(y0 + kb, a.height); y++)
            for (int x = x0; x < std::min(x0 + kb, a.width); x++)
                unlike += at(x, y) != at(x + half.x, y + half.y);
        return unlike >= kPeriodic;
    };
    for (size_t block = 0; block < blocks; block++)
    {
        // coherent: a block's pixels take the vector most of them fit when it fits them (no a/b mosaic);
        // ties: the slowest (look-alikes everywhere: what the eye sees), then the earlier
        int most = 0;
        for (int c : layers.allowed[block])
        {
            const int count = usableCounts[c * blocks + block];
            if (count > most)
            {
                most = count;
                blockVector[block] = c;
                tiedAt[block].assign(1, c);
            }
            else if (count == most && count > 0)
            {
                tiedAt[block].push_back(c);
                if (Slower(all[c], all[blockVector[block]]))
                    blockVector[block] = c;
            }
        }
        if (blockVector[block] < 0)
            continue;
        // fitTies: look-alikes fitting all of a block tie with or without support of their own (a dot row's
        // middle: the static look-alike and the row's motion; its ends tell). The vectors fitting as many pixels
        // of the block as the best does, support aside
        for (int c = 0; c < n; c++)
            fitCountOf[c] = fitCounts[c * blocks + block];
        // over every vector of the frame, not the block's own list: the same tie all over a layer
        int best = 0;
        for (int c = 0; c < n; c++)
            best = std::max(best, fitCountOf[c]);
        // a vector fitting more of the block than the vote's does is evidence enough (a dot row's end fits its
        // motion only; the support may be out of the window)
        tiedAt[block].clear();
        for (int c = 0; c < n; c++)
            if (fitCountOf[c] == best)
                tiedAt[block].push_back(c);
        if (fitCountOf[blockVector[block]] < best)
        {
            blockVector[block] = tiedAt[block][0];
            for (int c : tiedAt[block])
                if (Slower(all[c], all[blockVector[block]]))
                    blockVector[block] = c;
        }
        // nearTies: the frame's main motion (the most shared moving vector) fitting nearly as much of the block as
        // its own vector does (within 1/kNear): content that changes as it moves (spinning dots) fits a look-alike
        // by a few pixels of chance
        if (layers.common > 1 && blockVector[block] != 1 && !(all[blockVector[block]] == V{}) && fitCountOf[1] > 0 &&
            fitCountOf[1] * kNear >= fitCountOf[blockVector[block]] * (kNear - 1))
            blockVector[block] = 1;
        // nearStill: zero fitting best but not all of the block (something changes in it), the main motion within
        // 1/kNear, the block periodic: a dot row whose spacing is its step looks still in its middle but for the
        // dots' spin (Aleste's letters), and so does a still row of spinning dots. Neither is decided alone: the block
        // joins a region tied {zero, main} and the row's ends tell (a moving row's ends fit only its motion, a still
        // one's only zero)
        if (layers.common > 1 && blockVector[block] == 0 && tiedAt[block].size() == 1 && fitCountOf[1] > 0 &&
            fitCountOf[0] < blockPixels(block) && fitCountOf[1] * kNear >= fitCountOf[0] * (kNear - 1) &&
            periodic(block))
            nearStill[block] = 1;
        // a slower moving look-alike fitting some of the block, else the aliases rule keeps the vector
        bool lookAlike = false;
        for (int s = 1; s < n && blockVector[block] > 0; s++)
            lookAlike |= fitCountOf[s] > 0 && Slower(all[s], all[blockVector[block]]);
        if (lookAlike)
            asks.push_back({block, blockVector[block]});
    }
    // nearStill only where the frame has one motion: the blocks decided alone on a moving vector mostly (at least
    // kDominant of kDominantOf) on the main one. A still picture of look-alike tiles fits moving vectors everywhere,
    // each a different one; a scroll fits its own
    {
        int moving = 0, main = 0;
        for (size_t block = 0; block < blocks; block++)
            if (tiedAt[block].size() == 1 && blockVector[block] > 0 && !(all[blockVector[block]] == V{}))
            {
                moving++;
                main += blockVector[block] == 1;
            }
        const bool dominant = moving > 0 && main * kDominantOf >= moving * kDominant;
        for (size_t block = 0; block < blocks; block++)
            if (nearStill[block] && dominant)
                tiedAt[block].assign({0, 1});
            else
                nearStill[block] = 0;
    }
    // spread, majority: a region of blocks tied alike takes what most decided blocks round it say. Regions of
    // 4-connected blocks with the same tie set; the decided blocks bordering a region where their layer continues
    // into it vote (a dot row's ends tell its motion to the row's middle, where the static look-alike fits as well).
    // The bordering blocks are decided alone: no region changes their vectors
    auto& region = scratch.region;
    region.assign(blocks, -1);
    auto &seeds = scratch.seeds, &members = scratch.members, &memberEnd = scratch.memberEnd, &edgeEnd = scratch.edgeEnd;
    seeds.clear();
    members.clear();
    memberEnd.clear();
    edgeEnd.clear();
    auto& edges = scratch.edges;
    edges.clear();
    for (size_t seed = 0; seed < blocks; seed++)
    {
        if (tiedAt[seed].size() < 2 || region[seed] >= 0)
            continue;
        const size_t first = members.size();
        members.push_back(seed);
        region[seed] = static_cast<int>(seed);
        for (size_t m = first; m < members.size(); m++)
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
                else if (tiedAt[nb].size() == 1)
                    edges.push_back({bi, bj, di, dj});
            }
        }
        seeds.push_back(seed);
        memberEnd.push_back(members.size());
        edgeEnd.push_back(edges.size());
    }
    evidence.Ask(asks, edges);
    // aliases: a faster look-alike beats a slower one only on changes of its own: a change another vector explains
    // with more support is that layer's (a period-8 dash fits +1 and -7; -7 also fits a few changes of the text
    // under it by chance). The pixels c fits and s (if any) does not, the unchanged ones too (the block's still
    // evidence), less those another vector explains with more support
    for (const auto& [block, c] : asks)
    {
        int chosen = c;
        for (int s = 0; s < n; s++)
            if (!(all[s] == V{}) && Slower(all[s], all[chosen]) && evidence.Own(block, s, -1) > 0 &&
                evidence.Own(block, c, s) == 0)
                chosen = s;
        blockVector[block] = chosen;
    }
    auto& border = scratch.border;
    for (size_t r = 0, m0 = 0, e0 = 0; r < seeds.size(); m0 = memberEnd[r], e0 = edgeEnd[r], r++)
    {
        border.clear();
        for (size_t e = e0; e < edgeEnd[r]; e++)
        {
            const int vote = blockVector[static_cast<size_t>(edges[e].bj + edges[e].dj) * blocksWide +
                                         (edges[e].bi + edges[e].di)];
            if (evidence.Continues(e, vote))
                border.push_back(vote);
        }
        int bestVotes = 0, chosen = -1;
        for (int c : tiedAt[seeds[r]])
        {
            const int votes = static_cast<int>(std::count(border.begin(), border.end(), c));
            if (votes > bestVotes || (votes == bestVotes && votes > 0 && Slower(all[c], all[chosen])))
            {
                bestVotes = votes;
                chosen = c;
            }
        }
        if (chosen >= 0)
            for (size_t m = m0; m < memberEnd[r]; m++)
                blockVector[members[m]] = chosen;
    }
    tied.resize(blocks);
    for (size_t block = 0; block < blocks; block++)
        tied[block] = tiedAt[block].size() > 1;
}

void LayerFlow::Compose(const Picture& pictureP, const Picture& pictureA, const Picture& pictureB, const MotionField& prev,
                        const MotionField& next, int w, uint32_t* out)
{
    Prepare(prev, next, pictureA.width, pictureA.height, _layers);
    const std::vector<V>& all = _layers.all;
    const int width = pictureA.width, height = pictureA.height;
    const int n = static_cast<int>(all.size());
    // the reads reach two vectors out (the cover's steady check)
    const int reach = _layers.reach;
    const View p(pictureP, 2 * reach + 1, _paddedP), a(pictureA, 2 * reach + 1, _paddedA),
        b(pictureB, 2 * reach + 1, _paddedB);
    const size_t pixels = static_cast<size_t>(width) * height;
    // fits: per vector, the pixels it fits; score (window kRadius, at most 121) and support (window kSupportRadius).
    // The buffers are written whole before they are read, explained excepted; the hot loops go through raw pointers
    // (a byte store may alias a vector's own pointer, its reload would stop the vectorizer)
    auto &fits = _fits, &score = _score, &usableAt = _usable;
    auto &changed = _changed, &explained = _explained, &calm = _calm;
    auto& support = _support;
    auto &column = _column, &sums = _sums, &unexplained = _unexplained;
    fits.resize(pixels * n);
    score.resize(pixels * n);
    support.resize(pixels * n);
    changed.resize(pixels);
    calm.resize(pixels);
    sums.resize(pixels);
    unexplained.resize(pixels);
    explained.assign(pixels, 0);
    const auto shift = [&](int c) { return (w * c + kOne / 2) >> 8; };
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            changed[static_cast<size_t>(y) * width + x] = a.At(x, y) != b.At(x, y);
    // stopped: the pixels with no change from a to b anywhere in the window
    WindowSums(changed.data(), width, height, kRadius, sums.data(), column);
    for (size_t q = 0; q < pixels; q++)
        calm.data()[q] = sums.data()[q] == 0;
    // per vector its fits, score and support; the moving ones' explained changes. Split over the pool by vectors,
    // each part with its own buffers, its explained changes or'ed in after (the same whatever the split)
    const int parts = _pool ? std::min(n, kVectorParts) : 1;
    const int perPart = (n + parts - 1) / parts;
    _parts.resize(static_cast<size_t>(parts));
    for (Part& part : _parts)
    {
        part.fitting.resize(pixels);
        part.changes.resize(pixels);
        part.explained.assign(pixels, 0);
    }
    const auto vectors = [&](int c0, int c1) {
        Part& part = _parts[static_cast<size_t>(c0 / perPart)];
        for (int c = c0; c < c1; c++)
        {
            const V v = all[c];
            const bool still = v == V{};
            const int sx = shift(v.x), sy = shift(v.y);
            uint8_t* const fitsOf = fits.data() + static_cast<size_t>(c) * pixels;
            uint8_t *const fittingOf = part.fitting.data(), *const changesOf = part.changes.data(), *const explainedOf = part.explained.data();
            const uint8_t* const calmOf = calm.data();
            for (int y = 0; y < height; y++)
            {
                // the rows the pixels x of the row y read, at x: the samples a(x - wv), b(x - wv), b(x - wv + v),
                // a(x - wv - v); the pixel's own a(x), b(x + v), b(x), a(x - v), p(x)
                const uint32_t *sampleA = a.Row(-sx, y - sy), *sampleB = b.Row(-sx, y - sy);
                const uint32_t *sampleOn = b.Row(v.x - sx, y - sy + v.y), *sampleFrom = a.Row(-sx - v.x, y - sy - v.y);
                const uint32_t *hereA = a.Row(0, y), *hereB = b.Row(0, y), *hereP = p.Row(0, y);
                const uint32_t *onB = b.Row(v.x, y + v.y), *fromA = a.Row(-v.x, y - v.y);
                const size_t row = static_cast<size_t>(y) * width;
                for (int x = 0; x < width; x++)
                {
                    const uint32_t va = sampleA[x], vb = sampleB[x];
                    const uint8_t fit = (va == sampleOn[x]) & ((hereA[x] == onB[x]) | (hereB[x] == fromA[x]));
                    // still3: zero fits only a pixel the same in k - 1, k and k + 1 (static stays static at any
                    // cadence); stopped: or where nothing changes round the pixel either (the layer stopped)
                    fitsOf[row + x] = fit & (!still | (hereP[x] == va) | calmOf[row + x]);
                    fittingOf[row + x] = fit;
                    changesOf[row + x] = fit & (va != vb) & (vb == sampleFrom[x]);
                }
                if (!still)
                    for (int x = 0; x < width; x++)
                        explainedOf[row + x] |= changesOf[row + x];
            }
            WindowSums(part.fitting.data(), width, height, kRadius, score.data() + static_cast<size_t>(c) * pixels, part.column);
            WindowSums(part.changes.data(), width, height, kSupportRadius, support.data() + static_cast<size_t>(c) * pixels, part.column);
        }
    };
    if (_pool && parts > 1)
        _pool->ParallelFor(n, parts, vectors);
    else
        vectors(0, n);
    for (const Part& part : _parts)
    {
        const uint8_t* const from = part.explained.data();
        uint8_t* const into = explained.data();
        for (size_t q = 0; q < pixels; q++)
            into[q] |= from[q];
    }
    const Picture& nearer = w < kOne / 2 ? pictureA : pictureB;
    // credible: a moving vector explains at least the window's changes no vector explains (flux: none); the
    // changes of the window no vector explains are the bar a moving vector's support must reach
    WindowSums(changed.data(), width, height, kSupportRadius, unexplained.data(), column);
    WindowSums(explained.data(), width, height, kSupportRadius, sums.data(), column);
    for (size_t q = 0; q < pixels; q++)
        unexplained.data()[q] = std::max(unexplained.data()[q] - sums.data()[q], 0);
    // cover: a moving layer's content passes over a still pixel when the layer covers that still colour round here
    // (still pixels of it turn into the layer's content) more than it goes under it (the layer's content due there
    // and the still colour staying): a fast sprite's holes. Per moving vector, the still pixels whose colour the
    // vector's layer covers round here more than it goes under
    const std::vector<uint32_t>& palette = _palette;
    std::vector<uint8_t>& colourAt = _colourAt;
    colourAt.resize(pixels);
    Palette(pictureA, _palette, colourAt.data());
    // per moving vector and colour: the wins less the losses of the colour's pixels round a pixel. Summed at the
    // pixel while few pixels ask (a sprite's holes), a window image built once many do
    const auto balance = [&](V v, int col, int px, int py) {
        if (colourAt[static_cast<size_t>(py) * width + px] != col)
            return 0;
        const uint32_t here = a.At(px, py), from = a.At(px - v.x, py - v.y);
        if (p.At(px, py) != here || from == b.At(px - v.x, py - v.y) || from == here)
            return 0;
        // won: still from k - 1 to k, then the layer's content arrives
        if (b.At(px, py) == from)
            return 1;
        // lost: still over k - 1, k, k + 1 while the layer's content, steady from k - 1, is due
        return b.At(px, py) == here && p.At(px - 2 * v.x, py - 2 * v.y) == from ? -1 : 0;
    };
    // a fitting vector a pixel may take: zero, or a moving one with support that is credible
    usableAt.resize(pixels * n);
    std::copy(fits.begin(), fits.begin() + static_cast<ptrdiff_t>(pixels), usableAt.begin());
    // per vector (its own rows of the buffers), split over the pool
    const auto byVectors = [&](int first, const std::function<void(int, int)>& pass) {
        if (_pool && n - first > 1)
            _pool->ParallelFor(n - first, std::min(n - first, kVectorParts),
                               [&](int c0, int c1) { pass(first + c0, first + c1); });
        else
            pass(first, n);
    };
    byVectors(1, [&](int c0, int c1) {
        for (int c = c0; c < c1; c++)
        {
            const uint8_t* fitsOf = fits.data() + static_cast<size_t>(c) * pixels;
            const uint16_t* supportOf = support.data() + static_cast<size_t>(c) * pixels;
            uint8_t* usableOf = usableAt.data() + static_cast<size_t>(c) * pixels;
            const int* const bar = unexplained.data();
            for (size_t q = 0; q < pixels; q++)
                usableOf[q] = fitsOf[q] & (supportOf[q] > 0) & (supportOf[q] >= bar[q]);
        }
    });
    const auto usable = [&](int c, size_t q) { return usableAt[static_cast<size_t>(c) * pixels + q] != 0; };
    // per vector and block, the pixels it fits and the ones it may take
    const size_t blocks = _layers.allowed.size();
    std::vector<uint8_t>&fitCounts = _fitCounts, &usableCounts = _usableCounts;
    fitCounts.resize(blocks * n);
    usableCounts.resize(blocks * n);
    byVectors(0, [&](int c0, int c1) {
        for (int c = c0; c < c1; c++)
        {
            BlockCounts(fits.data() + static_cast<size_t>(c) * pixels, width, height, fitCounts.data() + c * blocks);
            BlockCounts(usableAt.data() + static_cast<size_t>(c) * pixels, width, height,
                        usableCounts.data() + c * blocks);
        }
    });
    const int blocksWide = _layers.blocksWide;
    std::vector<int>& blockVector = _blockVector;
    std::vector<uint8_t>& tied = _tied;
    CpuEvidence evidence(a, blocksWide, n, fits.data(), support.data(), fitCounts.data(), _topAt);
    Decide(_layers, pictureA, fitCounts.data(), usableCounts.data(), evidence, blockVector, tied, _decide);
    // carryFirst: the block's vector explains at least half of the block's changes (the motion of what is there,
    // not a chance match of content appearing)
    std::vector<int8_t>& explainsAt = _explainsAt;
    explainsAt.assign(blockVector.size(), -1);
    const auto blockExplains = [&](size_t block) {
        if (explainsAt[block] < 0)
            explainsAt[block] = ExplainsIn(a, b, width, height, static_cast<int>(block % blocksWide),
                                           static_cast<int>(block / blocksWide), all[blockVector[block]]);
        return explainsAt[block] == 1;
    };
    // The pixels, split over the pool by block rows: a block's lazy answers (explainsAt) are its band's alone; each
    // band its own cover windows (summed at the pixel or built, the same count either way), over its rows and the
    // window's reach round them
    const int blockRows = (height + MotionField::kBlock - 1) / MotionField::kBlock;
    const int bandRows = (blockRows + kBandParts - 1) / kBandParts;
    _bands.resize(static_cast<size_t>(_pool ? kBandParts : 1));
    const auto band = [&](int by0, int by1) {
        constexpr int kAsksBeforeBuild = 64;  // a window image costs about as much as 64 windows summed at the pixel
        Band& own = _bands[static_cast<size_t>(_pool ? by0 / bandRows : 0)];
        own.balanceOf.assign(static_cast<size_t>(n) * palette.size(), -1);
        own.asks.assign(own.balanceOf.size(), 0);
        own.used = 0;
        auto &balanceOf = own.balanceOf, &asks = own.asks;
        const int rowFirst = std::max(by0 * MotionField::kBlock - kSupportRadius, 0);
        const int rowEnd = std::min(by1 * MotionField::kBlock + kSupportRadius, height);
        const auto coverable = [&](int c, int x, int y) {
            const size_t q = static_cast<size_t>(y) * width + x;
            const int col = colourAt[q];
            if (col == 255)
                return false;
            const V v = all[c];
            const size_t pair = static_cast<size_t>(c) * palette.size() + col;
            int& index = balanceOf[pair];
            if (index < 0 && ++asks[pair] <= kAsksBeforeBuild)
            {
                int sum = 0;
                for (int py = std::max(y - kSupportRadius, 0); py < std::min(y + kSupportRadius + 1, height); py++)
                    for (int px = std::max(x - kSupportRadius, 0); px < std::min(x + kSupportRadius + 1, width); px++)
                        sum += balance(v, col, px, py);
                return sum > 0;
            }
            if (index < 0)
            {
                index = static_cast<int>(own.used++);
                if (own.boxes.size() < own.used)
                    own.boxes.emplace_back();
                Box& box = own.boxes[static_cast<size_t>(index)];
                box.Reset(width, height, kSupportRadius, rowFirst, rowEnd);
                box.Build([&](int px, int py) { return balance(v, col, px, py); });
            }
            return own.boxes[static_cast<size_t>(index)].Window(x, y) > 0;
        };
        for (int y = by0 * MotionField::kBlock; y < std::min(by1 * MotionField::kBlock, height); y++)
            for (int x = 0; x < width; x++)
            {
                const size_t block = static_cast<size_t>(y / MotionField::kBlock) * blocksWide + x / MotionField::kBlock;
                const auto& allowed = _layers.allowed[block];
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
                        if (v == V{} || support[cq] == 0)
                            continue;
                        const int ax = x - shift(v.x), ay = y - shift(v.y);
                        const uint32_t va = a.At(ax, ay);
                        if (va != b.At(ax + v.x, ay + v.y) || va == b.At(ax, ay) || va == a.At(x, y))
                            continue;
                        // steady: the sample is the layer's: it moved so from k - 1 too (rigid content, not a growing glyph)
                        if (p.At(ax - v.x, ay - v.y) != va || !coverable(c, x, y))
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
                     (tied[block] && fits[static_cast<size_t>(blockVector[block]) * pixels + q])))
                {
                    const V v = all[blockVector[block]];
                    out[q] = a.At(x - shift(v.x), y - shift(v.y));
                    continue;
                }
                if (blockVector[block] >= 0 && !(all[blockVector[block]] == V{}) && !tied[block] &&
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
    };
    if (_pool)
        _pool->ParallelFor(blockRows, kBandParts, band);
    else
        band(0, blockRows);
    KeepInk(pictureA, pictureB, nearer, out);
}

void LayerFlow::Box::Reset(int w, int h, int r, int first, int end)
{
    width = w;
    height = h;
    radius = r;
    y0 = first;
    y1 = end;
    sum.assign(static_cast<size_t>(w + 1) * (end - first + 1), 0);
}

int LayerFlow::Box::Window(int x, int y)
{
    const int x0 = std::max(x - radius, 0), x1 = std::min(x + radius + 1, width);
    const int ya = std::max(y - radius, 0), yb = std::min(y + radius + 1, height);
    return At(x1, yb) - At(x1, ya) - At(x0, yb) + At(x0, ya);
}

void LayerFlow::KeepInk(const Picture& a, const Picture& b, const Picture& nearer, uint32_t* out)
{
    const int width = a.width, height = a.height;
    std::array<uint32_t, kInkTile * kInkTile> colours;
    std::array<int, kInkTile * kInkTile> counts;
    for (int y0 = 0; y0 < height; y0 += kInkTile)
        for (int x0 = 0; x0 < width; x0 += kInkTile)
        {
            const int x1 = std::min(x0 + kInkTile, width), y1 = std::min(y0 + kInkTile, height);
            // the paper: a's most frequent colour in the tile, the first met of equals
            size_t kinds = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++)
                {
                    const uint32_t colour = a.pixels[static_cast<size_t>(y) * width + x];
                    size_t i = 0;
                    while (i < kinds && colours[i] != colour)
                        i++;
                    if (i == kinds)
                    {
                        colours[kinds] = colour;
                        counts[kinds++] = 0;
                    }
                    counts[i]++;
                }
            size_t top = 0;
            for (size_t i = 1; i < kinds; i++)
                if (counts[i] > counts[top])
                    top = i;
            const uint32_t paper = colours[top];
            int inkA = 0, inkB = 0, inkOut = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++)
                {
                    const size_t q = static_cast<size_t>(y) * width + x;
                    inkA += a.pixels[q] != paper;
                    inkB += b.pixels[q] != paper;
                    inkOut += out[q] != paper;
                }
            const int low = std::min(inkA, inkB), high = std::max(inkA, inkB);
            const int lost = low - inkOut, gained = inkOut - high;
            if ((lost > 0 && lost * kInkOff > low) || (gained > 0 && gained * kInkOff > high))
                for (int y = y0; y < y1; y++)
                    std::copy(nearer.pixels + static_cast<size_t>(y) * width + x0,
                              nearer.pixels + static_cast<size_t>(y) * width + x1, out + static_cast<size_t>(y) * width + x0);
        }
}

}  // namespace smoothmotion
