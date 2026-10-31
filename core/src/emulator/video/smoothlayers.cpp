#include "smoothlayers.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <utility>

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

/// Window sums over an image of counts (an integral image)
struct Box
{
    int width, height, radius;
    std::vector<int> sum;

    Box(int w, int h, int r) : width(w), height(h), radius(r), sum(static_cast<size_t>(w + 1) * (h + 1)) {}

    int& At(int x, int y) { return sum[static_cast<size_t>(y) * (width + 1) + x]; }

    /// `count(x, y)`: the value of a pixel
    template <typename Count>
    void Build(Count count)
    {
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
                At(x + 1, y + 1) = count(x, y) + At(x + 1, y) + At(x, y + 1) - At(x, y);
    }

    int Window(int x, int y)
    {
        const int x0 = std::max(x - radius, 0), x1 = std::min(x + radius + 1, width);
        const int y0 = std::max(y - radius, 0), y1 = std::min(y + radius + 1, height);
        return At(x1, y1) - At(x1, y0) - At(x0, y1) + At(x0, y0);
    }
};
}  // namespace

std::vector<V> LayerFlow::CommonVectors(const MotionField& prev, const MotionField& next)
{
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
    std::vector<V> common = {V{}};
    for (const auto& [v, n] : counts)
        if (n >= 2 && common.size() < kCommon && !(v == V{}))
            common.push_back(v);
    return common;
}

void LayerFlow::Compose(const Picture& pictureP, const Picture& pictureA, const Picture& pictureB, const MotionField& prev,
                        const MotionField& next, int w, uint32_t* out)
{
    const std::vector<V> common = CommonVectors(prev, next);
    const int width = pictureA.width, height = pictureA.height, bw = next.BlocksWide(), bh = next.BlocksHigh();
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
    // the reads reach two vectors out (the cover's steady check)
    int reach = 0;
    for (const V& v : all)
        reach = std::max({reach, std::abs(v.x), std::abs(v.y)});
    const View p(pictureP, 2 * reach + 1, _paddedP), a(pictureA, 2 * reach + 1, _paddedA),
        b(pictureB, 2 * reach + 1, _paddedB);
    const size_t pixels = static_cast<size_t>(width) * height;
    // fits: per vector, the pixels it fits; score (window kRadius, at most 121) and support (window kSupportRadius).
    // The buffers are written whole before they are read, explained excepted
    // The buffers are written whole before they are read, explained excepted; the hot loops go through raw pointers
    // (a byte store may alias a vector's own pointer, its reload would stop the vectorizer)
    auto &fits = _fits, &score = _score, &usableAt = _usable, &fitting = _fitting, &changes = _changes;
    auto &changed = _changed, &explained = _explained, &calm = _calm;
    auto& support = _support;
    auto &column = _column, &sums = _sums, &unexplained = _unexplained;
    fits.resize(pixels * n);
    score.resize(pixels * n);
    support.resize(pixels * n);
    fitting.resize(pixels);  // fitting: the pair fits the score counts
    changes.resize(pixels);
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
    for (int c = 0; c < n; c++)
    {
        const V v = all[c];
        const bool still = v == V{};
        const int sx = shift(v.x), sy = shift(v.y);
        uint8_t* const fitsOf = fits.data() + static_cast<size_t>(c) * pixels;
        uint8_t *const fittingOf = fitting.data(), *const changesOf = changes.data(), *const explainedOf = explained.data();
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
        WindowSums(fitting.data(), width, height, kRadius, score.data() + static_cast<size_t>(c) * pixels, column);
        WindowSums(changes.data(), width, height, kSupportRadius, support.data() + static_cast<size_t>(c) * pixels, column);
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
    std::vector<uint32_t> palette;
    std::vector<uint8_t> colourAt(pixels);
    for (int y = 0; y < height; y++)
    {
        // runs of a colour look it up once
        uint32_t last = a.At(0, y) + 1;
        uint8_t lastIndex = 0;
        for (int x = 0; x < width; x++)
        {
            const uint32_t colour = a.At(x, y);
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
            colourAt[static_cast<size_t>(y) * width + x] = lastIndex;
        }
    }
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
    constexpr int kAsksBeforeBuild = 64;  // a window image costs about as much as 64 windows summed at the pixel
    std::vector<int> balanceOf(static_cast<size_t>(n) * palette.size(), -1), asks(balanceOf.size());
    std::vector<Box> balances;
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
            index = static_cast<int>(balances.size());
            balances.emplace_back(width, height, kSupportRadius);
            balances.back().Build([&](int px, int py) { return balance(v, col, px, py); });
        }
        return balances[static_cast<size_t>(index)].Window(x, y) > 0;
    };
    // per block: the common vectors and the vectors of the 3 x 3 blocks round it
    const int blocksWide = (width + MotionField::kBlock - 1) / MotionField::kBlock;
    const int blocksHigh = (height + MotionField::kBlock - 1) / MotionField::kBlock;
    std::vector<std::vector<int>> allowedAt(static_cast<size_t>(blocksWide) * blocksHigh);
    std::vector<int> blockVector(allowedAt.size(), -1);
    std::vector<std::vector<int>> tiedAt(allowedAt.size());  // the vectors equal to the block's best
    // a fitting vector a pixel may take: zero, or a moving one with support that is credible
    usableAt.resize(pixels * n);
    std::copy(fits.begin(), fits.begin() + static_cast<ptrdiff_t>(pixels), usableAt.begin());
    for (int c = 1; c < n; c++)
    {
        const uint8_t* fitsOf = fits.data() + static_cast<size_t>(c) * pixels;
        const uint16_t* supportOf = support.data() + static_cast<size_t>(c) * pixels;
        uint8_t* usableOf = usableAt.data() + static_cast<size_t>(c) * pixels;
        const int* const bar = unexplained.data();
        for (size_t q = 0; q < pixels; q++)
            usableOf[q] = fitsOf[q] & (supportOf[q] > 0) & (supportOf[q] >= bar[q]);
    }
    const auto usable = [&](int c, size_t q) { return usableAt[static_cast<size_t>(c) * pixels + q] != 0; };
    // per vector and block, the pixels it fits and the ones it may take
    const size_t blocks = allowedAt.size();
    std::vector<uint8_t> fitCounts(blocks * n), usableCounts(blocks * n);
    for (int c = 0; c < n; c++)
    {
        BlockCounts(fits.data() + static_cast<size_t>(c) * pixels, width, height, fitCounts.data() + c * blocks);
        BlockCounts(usableAt.data() + static_cast<size_t>(c) * pixels, width, height, usableCounts.data() + c * blocks);
    }
    std::vector<int> fitCountOf(n);
    struct Top
    {
        int vector = -1, support = -1;
    };
    std::vector<std::array<Top, 3>> topAt(static_cast<size_t>(MotionField::kBlock) * MotionField::kBlock);
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
            const int x0 = bi * MotionField::kBlock, x1 = std::min(x0 + MotionField::kBlock, width);
            const int y0 = bj * MotionField::kBlock, y1 = std::min(y0 + MotionField::kBlock, height);
            int most = 0;
            for (int c : allowed)
            {
                const int count = usableCounts[c * blocks + block];
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
                for (int c = 0; c < n; c++)
                    fitCountOf[c] = fitCounts[c * blocks + block];
                const auto fitCount = [&](int c) { return fitCountOf[c]; };
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
            // a slower moving look-alike fitting some of the block, else the aliases rule keeps the vector
            bool lookAlike = false;
            for (int s = 1; s < n && blockVector[block] > 0; s++)
                lookAlike |= fitCountOf[s] > 0 && Slower(all[s], all[blockVector[block]]);
            if (lookAlike)
            {
                // aliases: a faster look-alike beats a slower one only on changes of its own: a change another
                // vector explains with more support is that layer's (a period-8 dash fits +1 and -7; -7 also fits
                // a few changes of the text under it by chance). The pixels c fits and s (if any) does not, the
                // unchanged ones too (the block's still evidence), less those another vector explains with more
                // support
                // per pixel, the three fitting vectors with the most support: a vector other than c and s with more
                // support than c's is among them if there is one
                for (int y = y0; y < y1; y++)
                    for (int x = x0; x < x1; x++)
                    {
                        auto& top = topAt[static_cast<size_t>(y - y0) * MotionField::kBlock + (x - x0)];
                        top = {};
                        const size_t q = static_cast<size_t>(y) * width + x;
                        for (int u = 0; u < n; u++)
                        {
                            const size_t uq = static_cast<size_t>(u) * pixels + q;
                            if (fitCountOf[u] == 0 || !fits[uq])
                                continue;
                            Top t{u, support[uq]};
                            for (auto& slot : top)
                                if (t.support > slot.support)
                                    std::swap(t, slot);
                        }
                    }
                const auto own = [&](int c, int s) {
                    int count = 0;
                    for (int y = y0; y < y1; y++)
                        for (int x = x0; x < x1; x++)
                        {
                            const size_t q = static_cast<size_t>(y) * width + x;
                            if (!fits[static_cast<size_t>(c) * pixels + q] || (s >= 0 && fits[static_cast<size_t>(s) * pixels + q]))
                                continue;
                            const int mine = support[static_cast<size_t>(c) * pixels + q];
                            bool other = false;
                            for (const Top& t : topAt[static_cast<size_t>(y - y0) * MotionField::kBlock + (x - x0)])
                                other |= t.vector >= 0 && t.vector != c && t.vector != s && t.support > mine;
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

}  // namespace smoothmotion
