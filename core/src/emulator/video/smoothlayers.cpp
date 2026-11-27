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
            // Eight bytes of 0 or 1: their sum is the top byte of the word times 0x01..01 (no carry: 8 at most).
            // std::popcount without a popcnt instruction in the build's baseline is a library call per word
            uint64_t word;
            std::memcpy(&word, row + i * kb, sizeof(word));
            line[i] = static_cast<uint8_t>(line[i] + ((word * 0x0101010101010101ull) >> 56));
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

/// The block decisions' answers from Compose's buffers. The aliases rule's own(...) of the asked blocks worked out
/// at the questions, over the pool (each ask its block alone), the regions' answers when asked
class CpuEvidence final : public LayerFlow::Evidence
{
public:
    using Top = LayerFlow::TopOf;
    using TopBlock = std::array<std::array<Top, 2>, static_cast<size_t>(MotionField::kBlock) * MotionField::kBlock>;

    /// `answers`, `askOf`: the caller's buffers
    CpuEvidence(const View& a, int blocksWide, const std::vector<V>& all, const uint8_t* fits, const uint16_t* support,
                const uint8_t* fitCounts, ThreadPool* pool, std::vector<int>& answers, std::vector<int>& askOf)
        : _a(a),
          _blocksWide(blocksWide),
          _n(static_cast<int>(all.size())),
          _all(all),
          _pixels(static_cast<size_t>(a.width) * a.height),
          _fits(fits),
          _support(support),
          _fitCounts(fitCounts),
          _blocks(static_cast<size_t>(blocksWide) * ((a.height + MotionField::kBlock - 1) / MotionField::kBlock)),
          _pool(pool),
          _answers(answers),
          _askOf(askOf)
    {
    }

    int Own(size_t block, int c, int s) override
    {
        const int ask = _askOf.empty() ? -1 : _askOf[block];
        if (ask >= 0 && (s < 0 || c == _askVector[ask]))
        {
            const int answer = _answers[(static_cast<size_t>(ask) * _n + (s < 0 ? c : s)) * 2 + (s < 0 ? 0 : 1)];
            if (answer >= 0)
                return answer;
        }
        TopBlock top;
        TopsOf(block, top);
        return OwnOf(block, c, s, top);
    }

    void Ask(const std::vector<std::pair<size_t, int>>& blocks, const std::vector<LayerFlow::Edge>& edges) override
    {
        _edges = &edges;
        // the aliases rule asks own(s, none), then own(c, s) where that is positive, for the moving vectors s slower
        // than the one chosen so far, itself c or slower (Slower is an order): every s slower than c
        const int asks = static_cast<int>(blocks.size());
        _askOf.assign(asks > 0 ? _blocks : 0, -1);
        _askVector.resize(blocks.size());
        _answers.assign(blocks.size() * _n * 2, -1);
        for (int i = 0; i < asks; i++)
        {
            _askOf[blocks[i].first] = i;
            _askVector[i] = blocks[i].second;
        }
        const auto work = [&](int i0, int i1) {
            TopBlock top;
            for (int i = i0; i < i1; i++)
            {
                const auto [block, c] = blocks[i];
                TopsOf(block, top);
                for (int s = 0; s < _n; s++)
                    if (!(_all[s] == V{}) && Slower(_all[s], _all[c]))
                    {
                        int* answer = &_answers[(static_cast<size_t>(i) * _n + s) * 2];
                        answer[0] = OwnOf(block, s, -1, top);
                        if (answer[0] > 0)
                            answer[1] = OwnOf(block, c, s, top);
                    }
            }
        };
        if (_pool && asks > 1)
            _pool->ParallelFor(asks, std::min(asks, kAskParts), work);
        else
            work(0, asks);
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

    static constexpr int kAskParts = 12;  ///< the asks' split over a pool

    /// per pixel of the block, the two fitting vectors with the most support (equals: the earlier first)
    void TopsOf(size_t block, TopBlock& topAt) const
    {
        const int kb = MotionField::kBlock;
        const int x0 = static_cast<int>(block % _blocksWide) * kb, x1 = std::min(x0 + kb, _a.width);
        const int y0 = static_cast<int>(block / _blocksWide) * kb, y1 = std::min(y0 + kb, _a.height);
        // the vectors fitting some of the block, in order
        std::array<int, LayerFlow::kMaxVectors> fitting;
        int count = 0;
        for (int u = 0; u < _n; u++)
            if (_fitCounts[u * _blocks + block] > 0)
                fitting[static_cast<size_t>(count++)] = u;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
            {
                auto& top = topAt[static_cast<size_t>(y - y0) * kb + (x - x0)];
                top = {};
                const size_t q = static_cast<size_t>(y) * _a.width + x;
                for (int k = 0; k < count; k++)
                {
                    const int u = fitting[static_cast<size_t>(k)];
                    const size_t uq = static_cast<size_t>(u) * _pixels + q;
                    if (!_fits[uq])
                        continue;
                    Top t{u, _support[uq]};
                    for (auto& slot : top)
                        if (t.support > slot.support)
                            std::swap(t, slot);
                }
            }
    }

    /// own(c, s) in the block: the pixels c fits and s (if any) does not, less those another vector explains with
    /// more support. Of the vectors other than c and s fitting a pixel, the one with the most support is the first
    /// of the two that is not c: s fits none of the pixels counted
    int OwnOf(size_t block, int c, int s, const TopBlock& topAt) const
    {
        if (_fitCounts[c * _blocks + block] == 0)
            return 0;
        const int kb = MotionField::kBlock;
        const int x0 = static_cast<int>(block % _blocksWide) * kb, x1 = std::min(x0 + kb, _a.width);
        const int y0 = static_cast<int>(block / _blocksWide) * kb, y1 = std::min(y0 + kb, _a.height);
        int count = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
            {
                const size_t q = static_cast<size_t>(y) * _a.width + x;
                if (!_fits[static_cast<size_t>(c) * _pixels + q] ||
                    (s >= 0 && _fits[static_cast<size_t>(s) * _pixels + q]))
                    continue;
                const auto& top = topAt[static_cast<size_t>(y - y0) * kb + (x - x0)];
                const Top& other = top[0].vector == c ? top[1] : top[0];
                count += !(other.vector >= 0 && other.support > _support[static_cast<size_t>(c) * _pixels + q]);
            }
        return count;
    }

    const View& _a;
    int _blocksWide, _n;
    const std::vector<V>& _all;
    size_t _pixels;
    const uint8_t* _fits;
    const uint16_t* _support;
    const uint8_t* _fitCounts;
    size_t _blocks;
    ThreadPool* _pool;
    std::vector<int>& _answers;  ///< per ask and vector s: own(s, none), own(c, s); -1 not worked out
    std::vector<int>& _askOf;    ///< per block its ask, -1 none
    std::vector<int> _askVector;
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

namespace
{
/// The matched moving vectors `counts` holds cut to the confirmed ones: matched by a block of `next` that is
/// detailed in a (its pixels unlike their right and lower neighbours in the block at least kDetail times each: too
/// much to match by chance), or whose pixels in a the vector carries to b exactly and that came there exactly as
/// well: from p by the same vector (the motion going on) or from p in place (a held frame, a motion starting from
/// rest). The field matches a block within MotionField::kAccept of its 64 pixels: a block of lines of one color each
/// holds 8 values, not 64, and content new every frame (a tape loader's lines) matches some vector there by chance
/// in every pair, never exactly over three frames
void KeepConfirmed(const MotionField& next, const Picture& p, const Picture& a, const Picture& b,
                   std::vector<std::pair<V, int>>& counts)
{
    const int kb = MotionField::kBlock;
    const int bw = next.BlocksWide(), bh = next.BlocksHigh();
    std::vector<V> confirmed;
    const auto at = [&](const Picture& f, int x, int y) { return f.pixels[static_cast<size_t>(y) * f.width + x]; };
    for (int j = 0; j < bh; j++)
        for (int i = 0; i < bw; i++)
        {
            const MotionField::Block& block = next.At(i, j);
            const V v = block.v;
            if (!block.matched || v == V{} || std::find(confirmed.begin(), confirmed.end(), v) != confirmed.end())
                continue;
            const int x0 = i * kb, x1 = std::min(x0 + kb, a.width), y0 = j * kb, y1 = std::min(y0 + kb, a.height);
            int across = 0, down = 0;
            bool carried = true, going = true, still = true;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++)
                {
                    const uint32_t here = at(a, x, y);
                    across += x + 1 < x1 && here != at(a, x + 1, y);
                    down += y + 1 < y1 && here != at(a, x, y + 1);
                    carried &= here == b.At(x + v.x, y + v.y);
                    going &= here == p.At(x - v.x, y - v.y);
                    still &= here == at(p, x, y);
                }
            if ((across >= LayerFlow::kDetail && down >= LayerFlow::kDetail) || (carried && (going || still)))
                confirmed.push_back(v);
        }
    counts.erase(std::remove_if(counts.begin(), counts.end(),
                                [&](const auto& c) {
                                    return !(c.first == V{}) &&
                                           std::find(confirmed.begin(), confirmed.end(), c.first) == confirmed.end();
                                }),
                 counts.end());
}
}  // namespace

namespace
{
/// A cut: a quarter of the picture's blocks or more changed from a to b, and the kCutLayers vectors `next` matches
/// most among them hold under two thirds of those it matches to a moving vector. A page of text or a room of tiles
/// flipped: many a new cell is an old one a cell over, matched exactly, its detail confirming the vector; one cell
/// moves as a letter would. No few layers explain the picture's changes, so none moves. Measured: flipped pages of
/// text 0.45-0.58, the synthetic suite's pairs that change a quarter of the picture (scrolls, parallax) 0.80 and
/// more
bool IsCut(const MotionField& next, const Picture& a, const Picture& b)
{
    const int kb = MotionField::kBlock;
    const int bw = next.BlocksWide(), bh = next.BlocksHigh();
    int changed = 0, moving = 0;
    std::vector<std::pair<V, int>> counts;
    for (int j = 0; j < bh; j++)
        for (int i = 0; i < bw; i++)
        {
            bool differs = false;
            for (int y = j * kb; y < std::min((j + 1) * kb, a.height) && !differs; y++)
            {
                const size_t row = static_cast<size_t>(y) * a.width;
                const int x0 = i * kb, x1 = std::min(x0 + kb, a.width);
                differs = !std::equal(a.pixels + row + x0, a.pixels + row + x1, b.pixels + row + x0);
            }
            if (!differs)
                continue;
            changed++;
            const MotionField::Block& block = next.At(i, j);
            if (!block.matched || block.v == V{})
                continue;
            moving++;
            auto it = std::find_if(counts.begin(), counts.end(), [&](const auto& c) { return c.first == block.v; });
            if (it == counts.end())
                counts.push_back({block.v, 1});
            else
                it->second++;
        }
    if (changed * LayerFlow::kCutChanged < bw * bh)
        return false;
    std::sort(counts.begin(), counts.end(), [](const auto& x, const auto& y) { return x.second > y.second; });
    int top = 0;
    for (size_t k = 0; k < counts.size() && k < static_cast<size_t>(LayerFlow::kCutLayers); k++)
        top += counts[k].second;
    return top * LayerFlow::kCutShareOf < moving * LayerFlow::kCutShare;
}
}  // namespace

std::vector<V> LayerFlow::CommonVectors(const MotionField& prev, const MotionField& next, const Picture& p,
                                        const Picture& a, const Picture& b)
{
    std::vector<std::pair<V, int>> counts;
    std::vector<uint8_t> state;
    std::vector<V> common;
    CountMatched(prev, next, counts);
    KeepConfirmed(next, p, a, b, counts);
    CommonOf(counts, common, state);
    return common;
}

void LayerFlow::Prepare(const MotionField& prev, const MotionField& next, const Picture& p, const Picture& a,
                        const Picture& b, Layers& layers)
{
    const int width = a.width, height = a.height;
    auto& all = layers.all;
    auto& counts = layers.counts;
    auto& state = layers.kept;  // 0: out, 1: common, 2: kept
    CountMatched(prev, next, counts);
    KeepConfirmed(next, p, a, b, counts);
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
    // a cut: every block zero alone (the vectors stay: the passes' work is the same)
    layers.cut = IsCut(next, a, b);
    for (int bj = 0; bj < layers.blocksHigh; bj++)
        for (int bi = 0; bi < layers.blocksWide; bi++)
        {
            auto& allowed = layers.allowed[static_cast<size_t>(bj) * layers.blocksWide + bi];
            allowed.clear();
            if (layers.cut)
            {
                allowed.push_back(0);
                continue;
            }
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
    if (layers.cut)
    {
        // no layer moves: zero where it may take pixels, the nearer frame elsewhere
        evidence.Ask({}, {});
        for (size_t block = 0; block < blocks; block++)
            blockVector[block] = usableCounts[block] > 0 ? 0 : -1;
        tied.assign(blocks, 0);
        return;
    }
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

void LayerFlow::Compose(const Picture& pictureP, const Picture& pictureA, const Picture& pictureB,
                        const MotionField& prev, const MotionField& next, int weight, uint32_t* out)
{
    // a weight past the pair would sample beyond the padded copies' margins (two vectors out)
    const int w = std::clamp(weight, 0, kOne);
    Prepare(prev, next, pictureP, pictureA, pictureB, _layers);
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
                // A chunk of the row at a time: the pixels' compares into arrays of this scope (nothing else can
                // point into them, so the compiler makes vector code of the loop), then narrowed to the byte
                // buffers. Written straight to the bytes the loop stayed scalar (a byte store may alias any of
                // the nine rows read): 14 of an interpolated picture's 32 ms on the Steam Deck
                constexpr int kChunk = 64;
                const uint8_t moving = still ? 0 : 1;
                for (int x0 = 0; x0 < width; x0 += kChunk)
                {
                    const int count = std::min(kChunk, width - x0);
                    uint32_t fit32[kChunk], stay32[kChunk], change32[kChunk];
                    for (int i = 0; i < count; i++)
                    {
                        const int x = x0 + i;
                        const uint32_t va = sampleA[x], vb = sampleB[x];
                        const uint32_t fit = (va == sampleOn[x]) & ((hereA[x] == onB[x]) | (hereB[x] == fromA[x]));
                        fit32[i] = fit;
                        stay32[i] = hereP[x] == va;
                        change32[i] = fit & (va != vb) & (vb == sampleFrom[x]);
                    }
                    for (int i = 0; i < count; i++)
                    {
                        const size_t q = row + static_cast<size_t>(x0 + i);
                        const uint8_t fit = static_cast<uint8_t>(fit32[i]);
                        // still3: zero fits only a pixel the same in k - 1, k and k + 1 (static stays static at any
                        // cadence); stopped: or where nothing changes round the pixel either (the layer stopped)
                        fitsOf[q] = fit & (moving | static_cast<uint8_t>(stay32[i]) | calmOf[q]);
                        fittingOf[q] = fit;
                        changesOf[q] = static_cast<uint8_t>(change32[i]);
                    }
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
            // A chunk at a time through an array of this scope, as the fits above: 16- and 32-bit compares stored
            // as bytes in one loop stayed scalar
            constexpr size_t kChunk = 256;
            for (size_t q0 = 0; q0 < pixels; q0 += kChunk)
            {
                const size_t count = std::min(kChunk, pixels - q0);
                int32_t reached[kChunk];
                for (size_t i = 0; i < count; i++)
                {
                    const int32_t s = supportOf[q0 + i];
                    reached[i] = (s > 0) & (s >= bar[q0 + i]);
                }
                for (size_t i = 0; i < count; i++)
                    usableOf[q0 + i] = fitsOf[q0 + i] & static_cast<uint8_t>(reached[i]);
            }
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
    CpuEvidence evidence(a, blocksWide, all, fits.data(), support.data(), fitCounts.data(), _pool, _ownAnswers, _askOf);
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
    // the block of a sample read clamped, as the frames are, decided still
    const auto stillBlock = [&](int sx, int sy) {
        const int bv = blockVector[static_cast<size_t>(std::clamp(sy, 0, height - 1) / MotionField::kBlock) * blocksWide +
                                   std::clamp(sx, 0, width - 1) / MotionField::kBlock];
        return bv >= 0 && all[bv] == V{};
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
                        // the sample moves: it leaves its place (b has another color there) and arrives at its
                        // landing (a had another color there; a still layer's pixel of the same color by chance
                        // is not the moving layer's)
                        if (va != b.At(ax + v.x, ay + v.y) || va == b.At(ax, ay) || va == a.At(ax + v.x, ay + v.y) ||
                            va == a.At(x, y))
                            continue;
                        // steady: the sample is the layer's: it moved so from k - 1 too (rigid content, not a growing glyph)
                        if (p.At(ax - v.x, ay - v.y) != va)
                            continue;
                        // behind: the layer's pixel a would show here is seen in k - 1 and b at its places (x - v,
                        // x + v), another color than the pixel's: the layer is opaque here, the still pixel in front
                        // of it, not seen through a hole (stripes going down under the paper fit sideways shifts too)
                        const uint32_t hidden = p.At(x - v.x, y - v.y);
                        if (hidden == b.At(x + v.x, y + v.y) && hidden != a.At(x, y))
                            continue;
                        // slowest: a slower vector fitting the sample is the layer's motion there (stripes fit every
                        // sideways shift, the eye sees the slowest), and that one does not carry the sample over the
                        // pixel. The sample read clamped, as the frames are
                        const size_t sq = static_cast<size_t>(std::clamp(ay, 0, height - 1)) * width + std::clamp(ax, 0, width - 1);
                        bool slower = false;
                        for (int u : allowed)
                            slower |= Slower(all[u], v) && fits[static_cast<size_t>(u) * pixels + sq];
                        if (slower || !coverable(c, x, y))
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
                    // place) is not carried. Carried: the block's vector from the nearer frame, from the other where
                    // the nearer one's sample stood still over the three frames in a block decided still, and the
                    // other's did not (another layer, still, the moving one goes under: stripes going down under the
                    // paper's edge)
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
                        const uint32_t fromA = a.At(ax, ay), fromB = b.At(ax + v.x, ay + v.y);
                        const bool stillA = p.At(ax, ay) == fromA && fromA == b.At(ax, ay);
                        const bool stillB = p.At(ax + v.x, ay + v.y) == fromB && fromB == a.At(ax + v.x, ay + v.y);
                        const bool nearA = w < kOne / 2;
                        const bool swap = nearA ? stillA && !stillB && stillBlock(ax, ay)
                                                : stillB && !stillA && stillBlock(ax + v.x, ay + v.y);
                        out[q] = nearA != swap ? fromA : fromB;
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
