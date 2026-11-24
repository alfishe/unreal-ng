// POC 055, b-bitplanes: MotionField::Estimate on colour-index bit planes, the same field block for block.
//
// The reference scores a vector for one block at a time: 64 pixel compares, stopped early past the best so far. Here
// a vector is scored for a whole row of blocks at once: the pictures' rows shifted by the vector's warp, xor-ed plane
// by plane into a mismatch word (64 pixels), its ones counted per byte (a block is 8 pixels wide: a byte of the
// word), summed over the block's 8 rows. The counts are exact, so "the first vector of the order with the fewest
// mismatches" is the same choice; a row of blocks all matched exactly is not scored again.
#pragma once

#include "emulator/video/smoothmotion.h"
#include "planes.h"

#include <vector>

namespace poc055
{
class BitField
{
public:
    using Field = smoothmotion::MotionField;
    using V = Field::Vector;

    /// The field a -> b, as Field::Estimate(a, b, range). The pictures' width and height: multiples of the block
    void Estimate(const Planes& a, const Planes& b, int range, Field& out)
    {
        const int kb = Field::kBlock;
        _a = &a;
        _b = &b;
        _bw = a.width / kb;
        _bh = a.height / kb;
        _words = a.Words();
        _range = range;
        const std::vector<V>& order = Field::SearchOrder(range);
        const size_t blocks = static_cast<size_t>(_bw) * _bh;
        _counts.resize(order.size() * blocks);
        _haveRow.assign(order.size() * _bh, 0);
        _indexOf.assign(static_cast<size_t>(2 * range + 1) * (2 * range + 1), -1);
        for (size_t i = 0; i < order.size(); i++)
            _indexOf[static_cast<size_t>(order[i].y + range) * (2 * range + 1) + (order[i].x + range)] = static_cast<int>(i);
        _order = &order;
        vectorsScored = 0;

        // Pass 1: per block the first vector of the order with the fewest mismatches, at most kAccept
        std::vector<int> best(blocks, Field::kAccept + 1), bestIndex(blocks, 0);
        std::vector<int> open(static_cast<size_t>(_bh), _bw);  // per block row: the blocks not matched exactly yet
        int rowsOpen = _bh;
        for (size_t vi = 0; vi < order.size() && rowsOpen > 0; vi++)
        {
            for (int j = 0; j < _bh; j++)
            {
                if (open[static_cast<size_t>(j)] == 0)
                    continue;
                const uint8_t* row = Row(static_cast<int>(vi), j);
                for (int i = 0; i < _bw; i++)
                {
                    const size_t block = static_cast<size_t>(j) * _bw + i;
                    if (best[block] == 0 || row[i] >= best[block])
                        continue;
                    best[block] = row[i];
                    bestIndex[block] = static_cast<int>(vi);
                    if (row[i] == 0 && --open[static_cast<size_t>(j)] == 0)
                        rowsOpen--;
                }
            }
        }
        std::vector<Field::Block> first(blocks);
        for (size_t block = 0; block < blocks; block++)
        {
            first[block].matched = best[block] <= Field::kAccept;
            first[block].v = first[block].matched ? order[static_cast<size_t>(bestIndex[block])] : V{};
        }

        // The refinement passes, as the reference's: zero, the common vectors, the eight neighbours'
        std::vector<Field::Block> prev = first, next(blocks);
        for (int pass = 0; pass < Field::kRefinePasses; pass++)
        {
            const std::vector<V> votes = Field::CommonVectors(prev, Field::kGlobalVectors, range);
            for (int j = 0; j < _bh; j++)
                for (int i = 0; i < _bw; i++)
                {
                    V cands[1 + Field::kGlobalVectors + 8];
                    int n = 0;
                    const auto add = [&](V v) {
                        for (int c = 0; c < n; c++)
                            if (cands[c] == v)
                                return;
                        cands[n++] = v;
                    };
                    add(V{});
                    for (const V& vote : votes)
                        add(vote);
                    for (int dj = -1; dj <= 1; dj++)
                        for (int di = -1; di <= 1; di++)
                        {
                            const int bi = i + di, bj = j + dj;
                            if ((di || dj) && bi >= 0 && bj >= 0 && bi < _bw && bj < _bh &&
                                prev[static_cast<size_t>(bj) * _bw + bi].matched)
                                add(prev[static_cast<size_t>(bj) * _bw + bi].v);
                        }
                    int bestCount = Field::kAccept + 1;
                    V bestV{};
                    for (int c = 0; c < n && bestCount > 0; c++)
                    {
                        const int bad = Row(IndexOf(cands[c]), j)[i];
                        if (bad < bestCount)
                        {
                            bestCount = bad;
                            bestV = cands[c];
                        }
                    }
                    Field::Block& block = next[static_cast<size_t>(j) * _bw + i];
                    if (bestCount <= Field::kAccept)
                        block = {bestV, true};
                    else
                        block = first[static_cast<size_t>(j) * _bw + i];
                }
            std::swap(prev, next);
        }
        out.Assign(_bw, _bh, std::move(prev), range);
    }

    int vectorsScored = 0;  ///< block rows scored, in whole pictures' worth (for the report)

private:
    int IndexOf(V v) const { return _indexOf[static_cast<size_t>(v.y + _range) * (2 * _range + 1) + (v.x + _range)]; }

    /// The mismatch counts of the vector `vi` for the blocks of the block row `j`, scored when first asked
    const uint8_t* Row(int vi, int j)
    {
        uint8_t* counts = _counts.data() + (static_cast<size_t>(vi) * _bh + j) * _bw;
        uint8_t& have = _haveRow[static_cast<size_t>(vi) * _bh + j];
        if (have)
            return counts;
        have = 1;
        vectorsScored++;
        const V v = (*_order)[static_cast<size_t>(vi)];
        // Warp(v, kOne / 2): the output's pixel reads a at -shift and b at v - shift
        const int sx = (smoothmotion::kOne / 2 * v.x + smoothmotion::kOne / 2) >> 8;
        const int sy = (smoothmotion::kOne / 2 * v.y + smoothmotion::kOne / 2) >> 8;
        const int ax = -sx, ay = -sy, bx = v.x - sx, by = v.y - sy;
        const int planes = _a->planes;
        uint64_t sums[16] = {};
        for (int r = 0; r < Field::kBlock; r++)
        {
            const int y = j * Field::kBlock + r;
            for (int w = 0; w < _words; w++)
            {
                uint64_t differ = 0;
                for (int p = 0; p < planes; p++)
                    differ |= Shifted(_a->Row(p, y + ay), w + 1, ax) ^ Shifted(_b->Row(p, y + by), w + 1, bx);
                sums[w] += ByteOnes(differ);
            }
        }
        for (int i = 0; i < _bw; i++)
            counts[i] = static_cast<uint8_t>(sums[i >> 3] >> (8 * (i & 7)));
        return counts;
    }

    const Planes* _a = nullptr;
    const Planes* _b = nullptr;
    const std::vector<V>* _order = nullptr;
    int _bw = 0, _bh = 0, _words = 0, _range = 0;
    std::vector<uint8_t> _counts, _haveRow;
    std::vector<int> _indexOf;
};
}  // namespace poc055
