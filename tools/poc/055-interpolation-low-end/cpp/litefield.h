// POC 055, c-lite: a block field that is not searched anew for every pair.
//
// Motion lasts: the vectors of the last pair explain nearly all of this one. So a pair scores zero and the vectors
// kept from the pairs before (each for whole rows of blocks on the bit planes, bitfield.h), and only the block rows
// with a block still unexplained go on through the reference's search order, a budget of row scores a pair, each
// row from where it stopped. A vector that matches a block there joins the kept ones.
//
// Not the reference's field: a block takes the fewest mismatches among the vectors scored for its row (zero first,
// then the kept ones by their blocks, then the search), no refinement passes. A new motion is found over a few pairs
// (the search goes on where it stopped), its blocks unmatched meanwhile.
#pragma once

#include "emulator/video/smoothmotion.h"
#include "planes.h"

#include <chrono>
#include <algorithm>
#include <vector>

namespace poc055
{
/// A field's blocks, row by row. Not smoothmotion::MotionField: its Assign also ranks the common vectors (a sort with a
/// search of the order in every comparison), which c-lite does not use
struct Grid
{
    int bw = 0, bh = 0;
    std::vector<smoothmotion::MotionField::Block> blocks;
    const smoothmotion::MotionField::Block& At(int i, int j) const { return blocks[static_cast<size_t>(j) * bw + i]; }
    int BlocksWide() const { return bw; }
    int BlocksHigh() const { return bh; }
    void From(const smoothmotion::MotionField& field)
    {
        bw = field.BlocksWide();
        bh = field.BlocksHigh();
        blocks.resize(static_cast<size_t>(bw) * bh);
        for (int j = 0; j < bh; j++)
            for (int i = 0; i < bw; i++)
                blocks[static_cast<size_t>(j) * bw + i] = field.At(i, j);
    }
};

class LiteField
{
public:
    using Field = smoothmotion::MotionField;
    using V = Field::Vector;

    size_t keptLimit = 10;                 ///< the moving vectors kept from pair to pair (kKept: the default)
    static constexpr size_t kKept = 10;    ///< the moving vectors kept from pair to pair
    /// Of the vectors that fit a block equally, the slowest (the reference's tie rule): a periodic pattern fits every
    /// vector of its period's parity, and the eye sees the slowest. Every kept vector is then scored on the rows
    /// that move, not only until a row is explained
    bool slowestOfEquals = false;
    /// A row explained by a kept vector still tries the vectors before it in the search order (the shorter ones),
    /// once: a kept vector may be a look-alike of a slower one that was never met (a dotted line of period 8
    /// moving by -1 fits +7 as well). With slowestOfEquals
    bool verifySlower = false;
    /// Blocks that several vectors fit equally (a row of dots a dot's pitch apart looks still in its middle: zero
    /// and the row's motion tie) are not decided alone: 4-connected blocks with the same tie take what most of the
    /// decided blocks along their border say (the row's ends fit only its motion). The reference's regions, without
    /// its test that the neighbour's layer continues. A tie of kTied vectors or more is plain paper: left alone
    bool regions = false;
    /// A row whose search went through the whole order and left blocks open sleeps for kSleep pairs: content no
    /// vector explains (noise, shapes changing) is not searched again every pair. It wakes at once when more of its
    /// blocks open than were open when it fell asleep (kWake more), or when it is explained
    bool sleepRows = false;
    static constexpr int kSleep = 25, kWake = 2;
    /// With slowestOfEquals: a kept vector is scored on an explained row only where it was some block's vector at
    /// the last pair (that row and the ones next to it), and on every such row once in kRecheck pairs, row by row
    bool keptLocal = false;
    static constexpr int kRecheck = 4;
    /// A row score stops at the pixel row after which no block can equal its best any more (the counts only grow).
    /// One-colour blocks that zero explains are not asked: zero wins their every tie. The same field, sooner
    bool earlyOut = true;
    /// A search's vector is scored only in the words of a row that hold a block not explained exactly: the row's
    /// explained blocks meet it a pair later, as a kept vector
    bool searchOpenWords = false;
    long long usedAtRank[12] = {}, blocksAtRank[12] = {};  ///< pairs in which the kept vector of a rank won blocks, and how many
    long long countsNs = 0, regionsNs = 0, abandoned = 0, scored = 0, zeroScores = 0, keptScores = 0, keptOnOpen = 0,
              openBlocksSeen = 0, openRowsAfterZero = 0, openRowsAtEnd = 0, openBlocksAtEnd = 0, asleep = 0, keptCount = 0;  ///< time in the row scores' counts and in the regions, all pairs
    static constexpr int kTied = 6;
    int budgetOfPair = 288;                ///< row scores of the search a pair (a whole picture's rows 8 times)
    static constexpr int kDropAfter = 25;  ///< a kept vector no block took for this many pairs goes

    void Estimate(const Planes& a, const Planes& b, int range, Grid& out)
    {
        const int kb = Field::kBlock;
        _a = &a;
        _b = &b;
        _bw = a.width / kb;
        _bh = a.height / kb;
        _words = a.Words();
        const std::vector<V>& order = Field::SearchOrder(range);
        const size_t blocks = static_cast<size_t>(_bw) * _bh;
        if (_cursor.size() != static_cast<size_t>(_bh))
        {
            _cursor.assign(static_cast<size_t>(_bh), 1);
            _kept.clear();
        }
        std::vector<int> best(blocks, Field::kAccept + 1);
        std::vector<V> bestV(blocks);
        std::vector<int> open(static_cast<size_t>(_bh), _bw);
        rowScores = 0;
        _ties.assign(blocks, Tie{});
        if (regions || earlyOut)
            Plain();
        const auto score = [&](V v, int j, unsigned wordMask = ~0u) {
            uint8_t counts[64];
            const auto t0 = std::chrono::steady_clock::now();
            uint64_t bestW[16] = {}, askW[16] = {};
            if (earlyOut)
                for (int i = 0; i < _bw; i++)
                {
                    const size_t block = static_cast<size_t>(j) * _bw + i;
                    bestW[i >> 3] |= static_cast<uint64_t>(best[block]) << (8 * (i & 7));
                    if (!(_plain[block] && best[block] == 0 && bestV[block] == V{}))
                        askW[i >> 3] |= 0x80ull << (8 * (i & 7));
                }
            const bool whole = Counts(v, j, counts, wordMask, earlyOut ? bestW : nullptr, askW);
            countsNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
            rowScores++;
            scored++;
            if (!whole)
            {
                abandoned++;
                return false;
            }
            bool matchedOne = false;
            for (int i = 0; i < _bw; i++)
            {
                const size_t block = static_cast<size_t>(j) * _bw + i;
                if (counts[i] > best[block])
                    continue;
                Tie& tie = _ties[block];
                if (counts[i] < best[block])
                    tie.count = 0;
                if (tie.count < kTied)
                    tie.v[tie.count] = v;
                tie.count++;
                // of equals the slowest; of equally slow ones the earlier in the search order, as the reference's
                // search meets them (dots of period 8 moving by -4 fit +4 as well)
                if (counts[i] == best[block] &&
                    !(slowestOfEquals && (Length2(v) < Length2(bestV[block]) ||
                                          (Length2(v) == Length2(bestV[block]) && Before(v, bestV[block])))))
                    continue;
                matchedOne |= counts[i] <= Field::kAccept;
                if (counts[i] == 0 && best[block] != 0)
                    open[static_cast<size_t>(j)]--;
                best[block] = counts[i];
                bestV[block] = v;
            }
            return matchedOne;
        };
        // zero, then the kept vectors, on the rows not explained yet
        std::vector<uint8_t> moves(static_cast<size_t>(_bh), 0);
        for (int j = 0; j < _bh; j++)
        {
            score(V{}, j);
            zeroScores++;
            openRowsAfterZero += open[static_cast<size_t>(j)] > 0;
            moves[static_cast<size_t>(j)] = open[static_cast<size_t>(j)] > 0;
        }
        _pair++;
        for (Kept& kept : _kept)
            for (int j = 0; j < _bh; j++)
            {
                bool again = slowestOfEquals && moves[static_cast<size_t>(j)];
                if (again && keptLocal)
                    again = ((kept.rows >> j) & 1) || (_pair + j) % kRecheck == 0;
                if (open[static_cast<size_t>(j)] > 0 || again)
                {
                    keptScores++;
                    keptOnOpen += open[static_cast<size_t>(j)] > 0;
                    openBlocksSeen += open[static_cast<size_t>(j)];
                    score(kept.v, j);
                }
            }
        // the search, where rows are still open: each from its cursor, the budget shared row after row
        int budget = budgetOfPair;
        // how far a row's search goes: the whole order while a block is unexplained; with verifySlower up to the
        // longest of its blocks' vectors (0: a still row)
        const auto target = [&](int j) -> size_t {
            if (open[static_cast<size_t>(j)] > 0)
                return order.size();
            size_t far = 0;
            if (verifySlower)
                for (int i = 0; i < _bw; i++)
                {
                    const size_t block = static_cast<size_t>(j) * _bw + i;
                    if (best[block] <= Field::kAccept && !(bestV[block] == V{}))
                        far = std::max(far, IndexOf(order, bestV[block]) + 1);
                }
            return far;
        };
        if (_settled.size() != static_cast<size_t>(_bh))
            _settled.assign(static_cast<size_t>(_bh), 0);
        for (int j = 0; j < _bh; j++)
            if (_settled[static_cast<size_t>(j)] && open[static_cast<size_t>(j)] > 0)
                _cursor[static_cast<size_t>(j)] = 1;  // new content in an explained row: its search starts over
        if (_sleep.size() != static_cast<size_t>(_bh))
        {
            _sleep.assign(static_cast<size_t>(_bh), 0);
            _sleepOpen.assign(static_cast<size_t>(_bh), 0);
        }
        for (int j = 0; j < _bh; j++)
            if (_sleep[static_cast<size_t>(j)] > 0)
            {
                const int nowOpen = open[static_cast<size_t>(j)];
                if (nowOpen == 0 || nowOpen >= _sleepOpen[static_cast<size_t>(j)] + kWake)
                    _sleep[static_cast<size_t>(j)] = 0;
                else
                    _sleep[static_cast<size_t>(j)]--;
            }
        for (bool any = true; any && budget > 0;)
        {
            any = false;
            for (int j = 0; j < _bh && budget > 0; j++)
            {
                if (sleepRows && _sleep[static_cast<size_t>(j)] > 0)
                    continue;
                const size_t far = target(j);
                if (far == 0)
                {
                    _cursor[static_cast<size_t>(j)] = 1;  // explained: the next search of this row starts over
                    continue;
                }
                size_t& at = _cursor[static_cast<size_t>(j)];
                if (at >= far)
                    continue;  // the order tried as far as this row's content asks
                any = true;
                const V v = order[at++];
                if (IsKept(v))
                    continue;
                budget--;
                unsigned openWords = ~0u;
                if (searchOpenWords)
                {
                    openWords = 0;
                    for (int i = 0; i < _bw; i++)
                        if (best[static_cast<size_t>(j) * _bw + i] != 0)
                            openWords |= 1u << (i >> 3);
                }
                if (score(v, j, openWords) && _kept.size() < keptLimit * 2)
                    _kept.push_back({v, 0, 0});
            }
        }
        // rows whose content changed while the search ran: once the order is through, it starts over
        for (int j = 0; j < _bh; j++)
            if (_cursor[static_cast<size_t>(j)] >= order.size())
            {
                _cursor[static_cast<size_t>(j)] = 1;
                if (open[static_cast<size_t>(j)] > 0)
                {
                    _sleep[static_cast<size_t>(j)] = kSleep;
                    _sleepOpen[static_cast<size_t>(j)] = open[static_cast<size_t>(j)];
                }
            }
        for (int j = 0; j < _bh; j++)
        {
            _settled[static_cast<size_t>(j)] = open[static_cast<size_t>(j)] == 0;
            openRowsAtEnd += open[static_cast<size_t>(j)] > 0;
            openBlocksAtEnd += open[static_cast<size_t>(j)];
            asleep += _sleep[static_cast<size_t>(j)] > 0;
        }
        keptCount += static_cast<long long>(_kept.size());
        if (regions)
        {
            const auto t0 = std::chrono::steady_clock::now();
            Regions(best, bestV);
            regionsNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
        }
        std::vector<Field::Block> result(blocks);
        for (Kept& kept : _kept)
        {
            kept.blocks = 0;
            kept.rows = 0;
        }
        for (size_t block = 0; block < blocks; block++)
        {
            result[block].matched = best[block] <= Field::kAccept;
            result[block].v = result[block].matched ? bestV[block] : V{};
            if (result[block].matched && !(result[block].v == V{}))
                for (Kept& kept : _kept)
                    if (kept.v == result[block].v)
                    {
                        kept.blocks++;
                        const int j = static_cast<int>(block / static_cast<size_t>(_bw));
                        kept.rows |= (7ull << j) >> 1;
                    }
        }
        // the kept ones for the next pair: the most blocks first, the unused ones age out
        for (Kept& kept : _kept)
            kept.idle = kept.blocks ? 0 : kept.idle + 1;
        _kept.erase(std::remove_if(_kept.begin(), _kept.end(), [](const Kept& k) { return k.idle > kDropAfter; }),
                    _kept.end());
        std::stable_sort(_kept.begin(), _kept.end(), [](const Kept& p, const Kept& q) { return p.blocks > q.blocks; });
        // how many of the kept ones a pair uses: by the blocks they won, in the order they will be kept
        for (size_t c = 0; c < _kept.size() && c < 12; c++)
            if (_kept[c].blocks > 0)
            {
                usedAtRank[c]++;
                blocksAtRank[c] += _kept[c].blocks;
            }
        if (_kept.size() > keptLimit)
            _kept.resize(keptLimit);
        out.bw = _bw;
        out.bh = _bh;
        out.blocks = std::move(result);
    }

    int rowScores = 0;  ///< of the last pair

private:
    struct Kept
    {
        V v;
        int blocks = 0, idle = 0;
        uint64_t rows = 0;  ///< block rows where it was a block's vector at the last pair, and the rows next to them
    };

    struct Tie
    {
        V v[kTied];
        int count = 0;  ///< the vectors with the block's fewest mismatches (past kTied: counted, not kept)
        bool Has(V u) const
        {
            for (int i = 0; i < std::min(count, kTied); i++)
                if (v[i] == u)
                    return true;
            return false;
        }
        bool Same(const Tie& o) const
        {
            if (count != o.count)
                return false;
            for (int i = 0; i < count; i++)
                if (!o.Has(v[i]))
                    return false;
            return true;
        }
    };

    /// _plain: the blocks of one colour in the first frame. Such a block fits every vector and says nothing: no member
    /// of a region and no voter (paper round a row of dots would join the row's region and outvote its ends)
    void Plain()
    {
        _plain.assign(static_cast<size_t>(_bw) * _bh, 0);
        for (int j = 0; j < _bh; j++)
        {
            uint64_t mixed[16] = {};
            for (int p = 0; p < _a->planes; p++)
                for (int w = 0; w < _words; w++)
                {
                    uint64_t all = ~0ull, some = 0;
                    for (int r = 0; r < Field::kBlock; r++)
                    {
                        const uint64_t row = _a->Row(p, j * Field::kBlock + r)[w + 1];
                        all &= row;
                        some |= row;
                    }
                    // a block's byte: every bit of the plane the same over its 8 rows, and the same along the row
                    uint64_t differs = all ^ some;
                    const uint64_t ones = some & 0x0101010101010101ull;
                    differs |= some ^ (ones * 0xFF);
                    mixed[w] |= differs;
                }
            for (int i = 0; i < _bw; i++)
                _plain[static_cast<size_t>(j) * _bw + i] = ((mixed[i >> 3] >> (8 * (i & 7))) & 0xFF) == 0;
        }
    }

    /// The tied regions' vectors by their decided neighbours' votes
    void Regions(const std::vector<int>& best, std::vector<V>& bestV)
    {
        const size_t blocks = best.size();
        const std::vector<uint8_t>& plain = _plain;
        const auto tied = [&](size_t b) {
            return !plain[b] && best[b] <= Field::kAccept && _ties[b].count > 1 && _ties[b].count < kTied;
        };
        const auto decided = [&](size_t b) {
            return !plain[b] && best[b] <= Field::kAccept && _ties[b].count == 1;
        };
        std::vector<int> regionOf(blocks, -1);
        std::vector<size_t> stack, members;
        const std::vector<V> before = bestV;  // the votes are of the blocks as decided alone
        for (size_t seed = 0; seed < blocks; seed++)
        {
            if (regionOf[seed] >= 0 || !tied(seed))
                continue;
            members.clear();
            stack.assign(1, seed);
            regionOf[seed] = static_cast<int>(seed);
            V voted[kTied];
            int votes[kTied] = {};
            const Tie& tie = _ties[seed];
            for (int i = 0; i < tie.count; i++)
                voted[i] = tie.v[i];
            while (!stack.empty())
            {
                const size_t b = stack.back();
                stack.pop_back();
                members.push_back(b);
                const int i = static_cast<int>(b % _bw), j = static_cast<int>(b / _bw);
                const int di[4] = {-1, 1, 0, 0}, dj[4] = {0, 0, -1, 1};
                for (int d = 0; d < 4; d++)
                {
                    const int ni = i + di[d], nj = j + dj[d];
                    if (ni < 0 || nj < 0 || ni >= _bw || nj >= _bh)
                        continue;
                    const size_t nb = static_cast<size_t>(nj) * _bw + ni;
                    if (tied(nb) && _ties[nb].Same(tie))
                    {
                        if (regionOf[nb] < 0)
                        {
                            regionOf[nb] = static_cast<int>(seed);
                            stack.push_back(nb);
                        }
                    }
                    else if (decided(nb))
                        for (int t = 0; t < tie.count; t++)
                            if (voted[t] == before[nb])
                                votes[t]++;
                }
            }
            int winner = -1;
            for (int t = 0; t < tie.count; t++)
                if (votes[t] > 0 && (winner < 0 || votes[t] > votes[winner] ||
                                     (votes[t] == votes[winner] &&
                                      (Length2(voted[t]) < Length2(voted[winner]) ||
                                       (Length2(voted[t]) == Length2(voted[winner]) && Before(voted[t], voted[winner]))))))
                    winner = t;
            if (winner >= 0)
                for (size_t b : members)
                    bestV[b] = voted[winner];
        }
    }
    static int Length2(V v) { return v.x * v.x + v.y * v.y; }
    /// Of two vectors of one length, the one Field::SearchOrder has first: row by row
    static bool Before(V u, V v) { return u.y != v.y ? u.y < v.y : u.x < v.x; }

    static size_t IndexOf(const std::vector<V>& order, V v)
    {
        return static_cast<size_t>(std::find(order.begin(), order.end(), v) - order.begin());
    }

    bool IsKept(V v) const
    {
        for (const Kept& kept : _kept)
            if (kept.v == v)
                return true;
        return false;
    }

    /// The mismatch counts of `v` for the blocks of the block row `j` (bitfield.h's Row)
    bool Counts(V v, int j, uint8_t* counts, unsigned wordMask, const uint64_t* bestW, const uint64_t* askW) const
    {
        const int sx = (smoothmotion::kOne / 2 * v.x + smoothmotion::kOne / 2) >> 8;
        const int sy = (smoothmotion::kOne / 2 * v.y + smoothmotion::kOne / 2) >> 8;
        const int ax = -sx, ay = -sy, bx = v.x - sx, by = v.y - sy;
        const int planes = _a->planes;
        uint64_t sums[16] = {};
        for (int r = 0; r < Field::kBlock; r++)
        {
            const int y = j * Field::kBlock + r;
            uint64_t differ[16] = {}, ra[16], rb[16];
            if (wordMask != ~0u)
            {
                // a search's score: only the words with a block still open
                for (int w = 0; w < _words; w++)
                    if ((wordMask >> w) & 1)
                        for (int p = 0; p < planes; p++)
                            differ[w] |= Shifted(_a->Row(p, y + ay), w + 1, ax) ^ Shifted(_b->Row(p, y + by), w + 1, bx);
            }
            else
                for (int p = 0; p < planes; p++)
                {
                    ShiftedRow(_a->Row(p, y + ay), ax, _words, ra);
                    ShiftedRow(_b->Row(p, y + by), bx, _words, rb);
                    for (int w = 0; w < _words; w++)
                        differ[w] |= ra[w] ^ rb[w];
                }
            for (int w = 0; w < _words; w++)
                sums[w] += ByteOnes(differ[w]);
            if (bestW && r + 1 < Field::kBlock)
            {
                // a byte of (best | 0x80) - sum keeps its top bit where the sum is no more than the best (both
                // under 128, so no borrow between the bytes)
                uint64_t alive = 0;
                for (int w = 0; w < _words; w++)
                    alive |= ((bestW[w] | 0x8080808080808080ull) - sums[w]) & askW[w];
                if (!alive)
                    return false;
            }
        }
        for (int i = 0; i < _bw; i++)
            counts[i] = (wordMask >> (i >> 3)) & 1 ? static_cast<uint8_t>(sums[i >> 3] >> (8 * (i & 7))) : 255;
        return true;
    }

    const Planes* _a = nullptr;
    const Planes* _b = nullptr;
    int _bw = 0, _bh = 0, _words = 0;
    std::vector<size_t> _cursor;  ///< per block row: where its search of the order stands
    std::vector<uint8_t> _plain;  ///< per block, of the pair being estimated
    std::vector<int> _sleep, _sleepOpen;  ///< per block row: pairs left to sleep, the open blocks when it fell asleep
    long _pair = 0;
    std::vector<Tie> _ties;  ///< per block, of the pair being estimated
    std::vector<uint8_t> _settled;  ///< per block row: every block explained exactly at the last pair
    std::vector<Kept> _kept;
};
}  // namespace poc055
