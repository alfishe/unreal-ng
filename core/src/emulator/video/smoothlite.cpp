#include "smoothlite.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <vector>

namespace smoothmotion
{
namespace
{
using V = MotionField::Vector;

constexpr int kBlock = MotionField::kBlock;
constexpr int kMaxPlanes = 8;
constexpr int kMarginRows = 24;  ///< rows above and below a frame's planes: a read of a shifted row stays inside
constexpr int kMaxWords = LiteFlow::kMaxWidth / 64;
constexpr int kMaxBlocksWide = LiteFlow::kMaxWidth / kBlock;
constexpr uint64_t kLowBits = 0x0101010101010101ull;
constexpr uint64_t kHighBits = 0x8080808080808080ull;

/// The colours met, in the order met since the palette was last compacted: every frame's planes index the same list
struct Palette
{
    static constexpr unsigned kSlots = 1024;  ///< open addressing, a quarter full at most
    std::vector<uint32_t> colours;
    uint16_t slots[kSlots] = {};  ///< a colour's index + 1, 0: empty

    Palette() { colours.reserve(static_cast<size_t>(LiteFlow::kMaxColours)); }

    /// -1: full
    int Index(uint32_t colour)
    {
        unsigned slot = (colour * 0x9E3779B1u) >> 22;
        for (; slots[slot]; slot = (slot + 1) & (kSlots - 1))
            if (colours[slots[slot] - 1u] == colour)
                return slots[slot] - 1;
        if (colours.size() >= static_cast<size_t>(LiteFlow::kMaxColours))
            return -1;
        colours.push_back(colour);
        slots[slot] = static_cast<uint16_t>(colours.size());
        return static_cast<int>(colours.size()) - 1;
    }
    /// The planes `count` colours need
    static int PlanesFor(size_t count)
    {
        int planes = 1;
        while ((static_cast<size_t>(1) << planes) < count)
            planes++;
        return planes;
    }
    int Planes() const { return PlanesFor(colours.size()); }
    void Clear()
    {
        colours.clear();
        std::memset(slots, 0, sizeof(slots));
    }
};

/// A set of palette indices
struct Indices
{
    uint64_t bits[4] = {};
    void Add(int index) { bits[index >> 6] |= 1ull << (index & 63); }
    bool Has(int index) const { return (bits[index >> 6] >> (index & 63)) & 1; }
    void Unite(const Indices& o)
    {
        for (int i = 0; i < 4; i++)
            bits[i] |= o.bits[i];
    }
    int Count() const
    {
        int count = 0;
        for (uint64_t b : bits)
            count += std::popcount(b);
        return count;
    }
};

/// A frame as bit planes of its colours' indices.
///
/// A row is `stride` words: one margin word, the pixels from bit 0 of word 1 (pixel x is bit x & 63 of word
/// 1 + x / 64), margin to the end. kMarginRows rows above and below. The margins repeat the edge pixel, as
/// Picture::At clamps. All kMaxPlanes planes are there; the ones above a frame's highest index are zero, so a frame
/// packed while the palette was small stays right when it grows
struct Planes
{
    uint64_t id = 0;
    int width = 0, height = 0, stride = 0;
    uint64_t used = 0;   ///< when last found or packed (the slot least recently used is packed over)
    Indices colours;     ///< the indices the frame has
    std::vector<uint64_t> bits;

    bool Empty() const { return width == 0; }
    const uint64_t* Row(int plane, int y) const
    {
        return bits.data() + (static_cast<size_t>(plane) * (height + 2 * kMarginRows) + (y + kMarginRows)) * stride;
    }
    uint64_t* Row(int plane, int y)
    {
        return bits.data() + (static_cast<size_t>(plane) * (height + 2 * kMarginRows) + (y + kMarginRows)) * stride;
    }
    int Words() const { return (width + 63) / 64; }

    /// false: the palette is full
    bool Pack(const Picture& picture, Palette& palette)
    {
        width = picture.width;
        height = picture.height;
        stride = Words() + 2;
        colours = Indices{};
        // every word is written below: the pixels' words a word at a time, the margins after
        bits.resize(static_cast<size_t>(kMaxPlanes) * (height + 2 * kMarginRows) * stride);
        const int words = Words();
        // the last two colours met and their indices: ink and paper alternate
        uint32_t lastColour = picture.pixels[0];
        int index = palette.Index(lastColour);
        if (index < 0)
            return false;
        colours.Add(index);
        uint32_t otherColour = lastColour;
        int otherIndex = index;
        for (int y = 0; y < height; y++)
        {
            const uint32_t* row = picture.pixels + static_cast<size_t>(y) * width;
            uint64_t* out[kMaxPlanes];
            for (int p = 0; p < kMaxPlanes; p++)
                out[p] = Row(p, y);
            int first = 0;
            for (int j = 0; j < words; j++)
            {
                uint64_t word[kMaxPlanes] = {};
                const int end = std::min(width, 64 * j + 64);
                // eight pixels' indices in a word, then each plane's eight bits out of it with one multiply
                for (int x = 64 * j; x < end; x += 8)
                {
                    const int count = end - x < 8 ? end - x : 8;
                    uint64_t eight = 0;
                    // eight pixels of the last colour (paper, border): its index in every byte
                    uint32_t others = 0;
                    if (count == 8)
                        for (int i = 0; i < 8; i++)
                            others |= row[x + i] ^ lastColour;
                    if (count == 8 && others == 0)
                        eight = static_cast<uint64_t>(index) * kLowBits;
                    else
                        for (int i = 0; i < count; i++)
                        {
                            if (row[x + i] != lastColour)
                            {
                                std::swap(lastColour, otherColour);
                                std::swap(index, otherIndex);
                                if (row[x + i] != lastColour)
                                {
                                    lastColour = row[x + i];
                                    index = palette.Index(lastColour);
                                    if (index < 0)
                                        return false;
                                    colours.Add(index);
                                }
                            }
                            eight |= static_cast<uint64_t>(index) << (8 * i);
                        }
                    if (x == 0)
                        first = static_cast<int>(eight & 0xFF);
                    // the planes the eight indices have: as many as the highest's bits
                    uint64_t bytes = eight | (eight >> 32);
                    bytes |= bytes >> 16;
                    bytes |= bytes >> 8;
                    const int top = std::bit_width(static_cast<unsigned>(bytes & 0xFF));
                    for (int p = 0; p < top; p++)
                        word[p] |= ((((eight >> p) & kLowBits) * 0x0102040810204080ull) >> 56) << (x & 63);
                }
                for (int p = 0; p < kMaxPlanes; p++)
                    out[p][1 + j] = word[p];
            }
            // the margins: the edge pixels again
            const int last = index;
            for (int p = 0; p < kMaxPlanes; p++)
            {
                uint64_t* r = out[p];
                r[0] = (first & (1 << p)) ? ~0ull : 0;
                r[words + 1] = 0;
                if (last & (1 << p))
                {
                    const int tail = width & 63;
                    if (tail)
                        r[words] |= ~0ull << tail;
                    r[words + 1] = ~0ull;
                }
            }
        }
        for (int p = 0; p < kMaxPlanes; p++)
            for (int m = 1; m <= kMarginRows; m++)
            {
                std::memcpy(Row(p, -m), Row(p, 0), sizeof(uint64_t) * stride);
                std::memcpy(Row(p, height - 1 + m), Row(p, height - 1), sizeof(uint64_t) * stride);
            }
        return true;
    }

    /// Every index i turned into map[i] (the palette compacted). `spread`: a byte's bits, one a byte
    void Remap(const uint8_t* map, const uint64_t* spread)
    {
        const size_t words = static_cast<size_t>(height + 2 * kMarginRows) * stride;
        uint64_t* plane[kMaxPlanes];
        for (int p = 0; p < kMaxPlanes; p++)
            plane[p] = bits.data() + p * words;
        for (size_t k = 0; k < words; k++)
        {
            uint64_t old[kMaxPlanes];
            bool flat = true;
            unsigned flatIndex = 0;
            for (int p = 0; p < kMaxPlanes; p++)
            {
                old[p] = plane[p][k];
                flat &= old[p] == 0 || old[p] == ~0ull;
                flatIndex |= static_cast<unsigned>(old[p] & 1) << p;
            }
            if (flat)
            {
                for (int p = 0; p < kMaxPlanes; p++)
                    plane[p][k] = ((map[flatIndex] >> p) & 1) ? ~0ull : 0;
                continue;
            }
            uint64_t now[kMaxPlanes] = {};
            for (int byte = 0; byte < 8; byte++)
            {
                uint64_t index = 0;
                for (int p = 0; p < kMaxPlanes; p++)
                    index |= spread[(old[p] >> (8 * byte)) & 0xFF] << p;
                uint64_t mapped = 0;
                for (int i = 0; i < 8; i++)
                    mapped |= static_cast<uint64_t>(map[(index >> (8 * i)) & 0xFF]) << (8 * i);
                for (int p = 0; p < kMaxPlanes; p++)
                    now[p] |= ((((mapped >> p) & kLowBits) * 0x0102040810204080ull) >> 56) << (8 * byte);
            }
            for (int p = 0; p < kMaxPlanes; p++)
                plane[p][k] = now[p];
        }
        Indices mapped;
        for (int i = 0; i < LiteFlow::kMaxColours; i++)
            if (colours.Has(i))
                mapped.Add(map[i]);
        colours = mapped;
    }
};

/// Word `j` of a row read `dx` pixels to the right: bit i is the row's pixel 64 (j - 1) + i + dx. |dx| < 64
inline uint64_t Shifted(const uint64_t* row, int j, int dx)
{
    if (dx == 0)
        return row[j];
    if (dx > 0)
        return (row[j] >> dx) | (row[j + 1] << (64 - dx));
    return (row[j] << -dx) | (row[j - 1] >> (64 + dx));
}

/// `words` words of a row from word 1, read `dx` pixels to the right
inline void ShiftedRow(const uint64_t* row, int dx, int words, uint64_t* out)
{
    if (dx == 0)
        for (int w = 0; w < words; w++)
            out[w] = row[w + 1];
    else if (dx > 0)
        for (int w = 0; w < words; w++)
            out[w] = (row[w + 1] >> dx) | (row[w + 2] << (64 - dx));
    else
        for (int w = 0; w < words; w++)
            out[w] = (row[w + 1] << -dx) | (row[w] >> (64 + dx));
}

/// The ones of each byte of `word`, in that byte (8 at most)
inline uint64_t ByteOnes(uint64_t word)
{
    word = word - ((word >> 1) & 0x5555555555555555ull);
    word = (word & 0x3333333333333333ull) + ((word >> 2) & 0x3333333333333333ull);
    return (word + (word >> 4)) & 0x0F0F0F0F0F0F0F0Full;
}

/// The bytes of `word` that are zero, all ones; the others zero
inline uint64_t ZeroBytes(uint64_t word)
{
    const uint64_t nonZero = (((word & ~kHighBits) + ~kHighBits) | word) & kHighBits;
    return ((nonZero ^ kHighBits) >> 7) * 0xFF;
}

inline int Length2(V v)
{
    return v.x * v.x + v.y * v.y;
}
/// Of two vectors of one length, the one MotionField::SearchOrder has first: row by row
inline bool Before(V u, V v)
{
    return u.y != v.y ? u.y < v.y : u.x < v.x;
}

/// The block field kept from pair to pair
class KeptField
{
public:
    std::vector<MotionField::Block> blocks;  ///< of the last pair, row by row
    int blocksWide = 0, blocksHigh = 0;
    uint64_t rowScores = 0;  ///< all pairs

    void Reset()
    {
        _cursor.clear();
        _settled.clear();
        _kept.clear();
    }

    void Estimate(const Planes& a, const Planes& b, int planes)
    {
        _a = &a;
        _b = &b;
        _planes = planes;
        _bw = a.width / kBlock;
        _bh = a.height / kBlock;
        _words = a.Words();
        const std::vector<V>& order = MotionField::SearchOrder(MotionField::kRangeFrames);
        const size_t count = static_cast<size_t>(_bw) * _bh;
        if (_cursor.size() != static_cast<size_t>(_bh) || blocksWide != _bw)
        {
            _cursor.assign(static_cast<size_t>(_bh), 1);
            _settled.assign(static_cast<size_t>(_bh), 0);
            _kept.clear();
        }
        _best.assign(count, MotionField::kAccept + 1);
        _bestV.assign(count, V{});
        _open.assign(static_cast<size_t>(_bh), _bw);
        _ties.assign(count, Tie{});
        Plain();

        // zero, then the kept vectors on every row that moves: of the vectors that fit a block equally the slowest
        // wins, so a row a kept vector explains still meets the slower ones
        _moves.assign(static_cast<size_t>(_bh), 0);
        for (int j = 0; j < _bh; j++)
        {
            Score(V{}, j);
            _moves[static_cast<size_t>(j)] = _open[static_cast<size_t>(j)] > 0;
        }
        for (size_t k = 0; k < _kept.size(); k++)
            for (int j = 0; j < _bh; j++)
                if (_moves[static_cast<size_t>(j)])
                    Score(_kept[k].v, j);

        // the search, on the rows with a block not explained exactly: each from its cursor, the budget shared row
        // after row
        for (int j = 0; j < _bh; j++)
            if (_settled[static_cast<size_t>(j)] && _open[static_cast<size_t>(j)] > 0)
                _cursor[static_cast<size_t>(j)] = 1;  // new content in an explained row: its search starts over
        int budget = LiteFlow::kBudget;
        for (bool any = true; any && budget > 0;)
        {
            any = false;
            for (int j = 0; j < _bh && budget > 0; j++)
            {
                size_t& at = _cursor[static_cast<size_t>(j)];
                if (_open[static_cast<size_t>(j)] == 0)
                {
                    at = 1;  // explained: the next search of this row starts over
                    continue;
                }
                if (at >= order.size())
                    continue;  // the whole order tried for this row's content
                any = true;
                const V v = order[at++];
                if (IsKept(v))
                    continue;
                budget--;
                if (Score(v, j) && _kept.size() < LiteFlow::kKept * 2)
                    _kept.push_back({v, 0, 0});
            }
        }
        // rows whose content changed while the search ran: once the order is through, it starts over
        for (int j = 0; j < _bh; j++)
        {
            if (_cursor[static_cast<size_t>(j)] >= order.size())
                _cursor[static_cast<size_t>(j)] = 1;
            _settled[static_cast<size_t>(j)] = _open[static_cast<size_t>(j)] == 0;
        }
        Regions();

        blocks.resize(count);
        blocksWide = _bw;
        blocksHigh = _bh;
        for (Kept& kept : _kept)
            kept.blocks = 0;
        for (size_t block = 0; block < count; block++)
        {
            blocks[block].matched = _best[block] <= MotionField::kAccept;
            blocks[block].v = blocks[block].matched ? _bestV[block] : V{};
            if (blocks[block].matched && !(blocks[block].v == V{}))
                for (Kept& kept : _kept)
                    if (kept.v == blocks[block].v)
                        kept.blocks++;
        }
        // the kept ones for the next pair: the most blocks first, the unused ones age out
        for (Kept& kept : _kept)
            kept.idle = kept.blocks ? 0 : kept.idle + 1;
        _kept.erase(std::remove_if(_kept.begin(), _kept.end(),
                                   [](const Kept& k) { return k.idle > LiteFlow::kDropAfter; }),
                    _kept.end());
        std::stable_sort(_kept.begin(), _kept.end(), [](const Kept& p, const Kept& q) { return p.blocks > q.blocks; });
        if (_kept.size() > LiteFlow::kKept)
            _kept.resize(LiteFlow::kKept);
    }

private:
    struct Kept
    {
        V v;
        int blocks = 0, idle = 0;
    };

    /// The vectors with a block's fewest mismatches (past kTied: counted, not kept)
    struct Tie
    {
        V v[LiteFlow::kTied];
        int count = 0;
        bool Has(V u) const
        {
            for (int i = 0; i < std::min(count, LiteFlow::kTied); i++)
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

    bool IsKept(V v) const
    {
        for (const Kept& kept : _kept)
            if (kept.v == v)
                return true;
        return false;
    }

    /// _plain: the blocks of one colour in the first frame. Such a block fits every vector and says nothing: no member
    /// of a region and no voter (paper round a row of dots would join the row's region and outvote its ends)
    void Plain()
    {
        _plain.assign(static_cast<size_t>(_bw) * _bh, 0);
        for (int j = 0; j < _bh; j++)
        {
            uint64_t mixed[kMaxWords];
            std::fill_n(mixed, _words, 0);
            for (int p = 0; p < _planes; p++)
                for (int w = 0; w < _words; w++)
                {
                    uint64_t all = ~0ull, some = 0;
                    for (int r = 0; r < kBlock; r++)
                    {
                        const uint64_t row = _a->Row(p, j * kBlock + r)[w + 1];
                        all &= row;
                        some |= row;
                    }
                    // a block's byte: every bit of the plane the same over its 8 rows, and the same along the row
                    uint64_t differs = all ^ some;
                    differs |= some ^ ((some & kLowBits) * 0xFF);
                    mixed[w] |= differs;
                }
            for (int i = 0; i < _bw; i++)
                _plain[static_cast<size_t>(j) * _bw + i] = ((mixed[i >> 3] >> (8 * (i & 7))) & 0xFF) == 0;
        }
    }

    /// The mismatch counts of `v` for the blocks of the block row `j`. false: stopped at the pixel row after which no
    /// asked block could equal its best any more (the counts only grow), `counts` not written
    bool Counts(V v, int j, uint8_t* counts, const uint64_t* bestW, const uint64_t* askW) const
    {
        const int sx = (kOne / 2 * v.x + kOne / 2) >> 8;
        const int sy = (kOne / 2 * v.y + kOne / 2) >> 8;
        const int ax = -sx, ay = -sy, bx = v.x - sx, by = v.y - sy;
        // the arrays are kMaxWords long, only _words of them are used: zeroing the rest costs more than the counts
        uint64_t sums[kMaxWords];
        std::fill_n(sums, _words, 0);
        for (int r = 0; r < kBlock; r++)
        {
            const int y = j * kBlock + r;
            uint64_t differ[kMaxWords], ra[kMaxWords], rb[kMaxWords];
            std::fill_n(differ, _words, 0);
            for (int p = 0; p < _planes; p++)
            {
                ShiftedRow(_a->Row(p, y + ay), ax, _words, ra);
                ShiftedRow(_b->Row(p, y + by), bx, _words, rb);
                for (int w = 0; w < _words; w++)
                    differ[w] |= ra[w] ^ rb[w];
            }
            for (int w = 0; w < _words; w++)
                sums[w] += ByteOnes(differ[w]);
            if (r + 1 < kBlock)
            {
                // a byte of (best | 0x80) - sum keeps its top bit where the sum is no more than the best (both
                // under 128, so no borrow between the bytes)
                uint64_t alive = 0;
                for (int w = 0; w < _words; w++)
                    alive |= ((bestW[w] | kHighBits) - sums[w]) & askW[w];
                if (!alive)
                    return false;
            }
        }
        for (int i = 0; i < _bw; i++)
            counts[i] = static_cast<uint8_t>(sums[i >> 3] >> (8 * (i & 7)));
        return true;
    }

    /// `v` for the block row `j`: the blocks it fits no worse than their best take it (of equals the slowest, of
    /// equally slow ones the first in the search order). true: it matched a block better than what the block had
    bool Score(V v, int j)
    {
        uint8_t counts[kMaxBlocksWide];
        uint64_t bestW[kMaxWords], askW[kMaxWords];
        std::fill_n(bestW, _words, 0);
        std::fill_n(askW, _words, 0);
        for (int i = 0; i < _bw; i++)
        {
            const size_t block = static_cast<size_t>(j) * _bw + i;
            bestW[i >> 3] |= static_cast<uint64_t>(_best[block]) << (8 * (i & 7));
            // a one-colour block zero explains is not asked: zero wins its every tie
            if (!(_plain[block] && _best[block] == 0 && _bestV[block] == V{}))
                askW[i >> 3] |= 0x80ull << (8 * (i & 7));
        }
        rowScores++;
        if (!Counts(v, j, counts, bestW, askW))
            return false;
        bool matchedOne = false;
        for (int i = 0; i < _bw; i++)
        {
            const size_t block = static_cast<size_t>(j) * _bw + i;
            if (counts[i] > _best[block])
                continue;
            Tie& tie = _ties[block];
            if (counts[i] < _best[block])
                tie.count = 0;
            if (tie.count < LiteFlow::kTied)
                tie.v[tie.count] = v;
            tie.count++;
            if (counts[i] == _best[block] &&
                !(Length2(v) < Length2(_bestV[block]) ||
                  (Length2(v) == Length2(_bestV[block]) && Before(v, _bestV[block]))))
                continue;
            matchedOne |= counts[i] <= MotionField::kAccept;
            if (counts[i] == 0 && _best[block] != 0)
                _open[static_cast<size_t>(j)]--;
            _best[block] = counts[i];
            _bestV[block] = v;
        }
        return matchedOne;
    }

    /// Blocks that several vectors fit equally (a row of dots a dot's pitch apart looks still in its middle: zero
    /// and the row's motion tie) are not decided alone: 4-connected blocks with the same tie take what most of the
    /// decided blocks along their border say (the row's ends fit only its motion)
    void Regions()
    {
        const size_t count = _best.size();
        const auto tied = [&](size_t b) {
            return !_plain[b] && _best[b] <= MotionField::kAccept && _ties[b].count > 1 &&
                   _ties[b].count < LiteFlow::kTied;
        };
        const auto decided = [&](size_t b) {
            return !_plain[b] && _best[b] <= MotionField::kAccept && _ties[b].count == 1;
        };
        _regionOf.assign(count, -1);
        _before = _bestV;  // the votes are of the blocks as decided alone
        for (size_t seed = 0; seed < count; seed++)
        {
            if (_regionOf[seed] >= 0 || !tied(seed))
                continue;
            _members.clear();
            _stack.assign(1, seed);
            _regionOf[seed] = static_cast<int>(seed);
            int votes[LiteFlow::kTied] = {};
            const Tie& tie = _ties[seed];
            while (!_stack.empty())
            {
                const size_t b = _stack.back();
                _stack.pop_back();
                _members.push_back(b);
                const int i = static_cast<int>(b % static_cast<size_t>(_bw));
                const int j = static_cast<int>(b / static_cast<size_t>(_bw));
                const int di[4] = {-1, 1, 0, 0}, dj[4] = {0, 0, -1, 1};
                for (int d = 0; d < 4; d++)
                {
                    const int ni = i + di[d], nj = j + dj[d];
                    if (ni < 0 || nj < 0 || ni >= _bw || nj >= _bh)
                        continue;
                    const size_t nb = static_cast<size_t>(nj) * _bw + ni;
                    if (tied(nb) && _ties[nb].Same(tie))
                    {
                        if (_regionOf[nb] < 0)
                        {
                            _regionOf[nb] = static_cast<int>(seed);
                            _stack.push_back(nb);
                        }
                    }
                    else if (decided(nb))
                        for (int t = 0; t < tie.count; t++)
                            if (tie.v[t] == _before[nb])
                                votes[t]++;
                }
            }
            int winner = -1;
            for (int t = 0; t < tie.count; t++)
                if (votes[t] > 0 &&
                    (winner < 0 || votes[t] > votes[winner] ||
                     (votes[t] == votes[winner] &&
                      (Length2(tie.v[t]) < Length2(tie.v[winner]) ||
                       (Length2(tie.v[t]) == Length2(tie.v[winner]) && Before(tie.v[t], tie.v[winner]))))))
                    winner = t;
            if (winner >= 0)
                for (size_t b : _members)
                    _bestV[b] = tie.v[winner];
        }
    }

    const Planes* _a = nullptr;
    const Planes* _b = nullptr;
    int _planes = 0, _bw = 0, _bh = 0, _words = 0;
    std::vector<size_t> _cursor;     ///< per block row: where its search of the order stands
    std::vector<uint8_t> _settled;   ///< per block row: every block explained exactly at the last pair
    std::vector<Kept> _kept;
    // of the pair being estimated
    std::vector<int> _best, _open;
    std::vector<V> _bestV, _before;
    std::vector<uint8_t> _plain, _moves;
    std::vector<Tie> _ties;
    std::vector<int> _regionOf;
    std::vector<size_t> _stack, _members;
};

}  // namespace

struct LiteFlow::Impl
{
    Stats stats;
    Palette palette;
    /// Two pairs and the frame before each: a pair asked again finds its frames
    static constexpr size_t kCached = 4;
    Planes cached[kCached];
    uint64_t clock = 0;  ///< Planes::used
    KeptField field;
    int planes = 0;  ///< the palette's, for the pair being drawn

    /// A pair estimated: what its pictures are drawn from
    struct Pair
    {
        bool valid = false;
        uint64_t a = 0, b = 0;
        uint64_t before = 0;  ///< the id of the frame before a, 0: none
        int width = 0, height = 0, blocksHigh = 0;
        std::vector<V> vectors;                ///< the moving ones, most blocks first
        std::vector<uint64_t> own, nearby;     ///< per vector, block row and word: the pixels of the blocks it may take
        /// per block row, words + 2 (a margin word each side, the edge block again): the pixels of the blocks the
        /// field matched still
        std::vector<uint64_t> stillBlocks;
        /// per block row and word: the pixels unlike in a and b on some row of the block row (the next pair's
        /// `unlike`); empty: not counted
        std::vector<uint64_t> changed;
        std::vector<std::pair<V, size_t>> confirmedBy;  ///< a moving vector confirmed, and the block that did
        /// per vector, row and word: the pixels it carries their colour, a(x) to b(x + v) or b(x) from a(x - v) (by
        /// Credible, on the block rows it counted: those Draw asks and more)
        std::vector<uint64_t> carries;
        std::vector<uint64_t> same;  ///< per row and word: the pixels alike in a and b (by Credible; empty: none)
    };
    /// The last two pairs estimated; `current` the one drawn last. A pair asked again is drawn as it was the first
    /// time (the kept field moves on only with a pair new to it)
    Pair pairs[2];
    int current = 0;
    uint64_t refusedA = 0, refusedB = 0;  ///< the ids of the last pair refused for its colours

    // Confirm's: per block row and word, computed when first asked
    std::vector<uint64_t> free, unlike;  ///< the detailed blocks; the pixels where a and the frame before differ
    std::vector<uint8_t> freeKnown, unlikeKnown, confirmed;
    // Credible's
    std::vector<int> explains, unexplained, roundRow;
    std::vector<uint64_t> any;
    std::vector<uint8_t> candidate;
    std::vector<std::pair<V, int>> allVectors;  ///< Prepare's
    Palette scratch;      ///< FitsWith's
    uint64_t spread[256];  ///< a byte's bits, one a byte

    Impl()
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

    void Forget()
    {
        for (Planes& p : cached)
            p = Planes{};
        palette.Clear();
        field.Reset();
        for (Pair& p : pairs)
            p.valid = false;
        refusedA = refusedB = 0;
    }

    /// The slot empty (its words kept for the next frame)
    static void Drop(Planes& slot)
    {
        slot.id = 0;
        slot.width = slot.height = slot.stride = 0;
        slot.colours = Indices{};
    }
    size_t SlotOf(const Planes* planes) const { return static_cast<size_t>(planes - cached); }

    /// The palette cut to the colours of the cached frames in `keep` (a bit a slot), in their order; their planes
    /// re-indexed, the other slots dropped. The pictures are the same: they ask only which pixels are equal
    void Compact(unsigned keep)
    {
        Indices live;
        for (size_t i = 0; i < kCached; i++)
            if ((keep >> i) & 1)
                live.Unite(cached[i].colours);
            else
                Drop(cached[i]);
        uint8_t map[kMaxColours] = {};
        uint32_t colours[kMaxColours];
        int count = 0;
        for (size_t i = 0; i < palette.colours.size(); i++)
            if (live.Has(static_cast<int>(i)))
            {
                map[i] = static_cast<uint8_t>(count);
                colours[count++] = palette.colours[i];
            }
        palette.Clear();
        for (int i = 0; i < count; i++)
            palette.Index(colours[i]);
        for (size_t i = 0; i < kCached; i++)
            if (((keep >> i) & 1) && !cached[i].Empty())
                cached[i].Remap(map, spread);
        stats.palettes++;
    }

    /// Whether `picture`'s colours and those of `keep` (null: none) are kMaxColours at most
    bool FitsWith(const Picture& picture, const Planes* keep)
    {
        scratch.Clear();
        if (keep)
            for (size_t i = 0; i < palette.colours.size(); i++)
                if (keep->colours.Has(static_cast<int>(i)))
                    scratch.Index(palette.colours[i]);
        const size_t pixels = static_cast<size_t>(picture.width) * picture.height;
        uint32_t last = picture.pixels[0];
        if (scratch.Index(last) < 0)
            return false;
        for (size_t p = 1; p < pixels; p++)
            if (picture.pixels[p] != last)
            {
                last = picture.pixels[p];
                if (scratch.Index(last) < 0)
                    return false;
            }
        return true;
    }

    /// The planes of the frame before the pair's `a` when still kept, else nullptr
    const Planes* Before(const Pair& pair, const Picture& a)
    {
        if (!pair.before)
            return nullptr;
        for (Planes& kept : cached)
            if (kept.id == pair.before && kept.width == a.width && kept.height == a.height)
            {
                kept.used = ++clock;
                return &kept;
            }
        return nullptr;
    }

    /// The planes of `picture`, packed now unless kept by its id, never over `keep`. The palette is compacted when it
    /// holds two planes' worth more than the frames kept need, or fills. nullptr: more than kMaxColours colours in
    /// the picture and `keep`
    Planes* PlanesOf(const Picture& picture, const Planes* keep)
    {
        if (picture.id)
            for (Planes& p : cached)
                if (p.id == picture.id && p.width == picture.width && p.height == picture.height)
                {
                    p.used = ++clock;
                    return &p;
                }
        Planes* slot = nullptr;
        for (Planes& p : cached)
            if (&p != keep && (!slot || p.used < slot->used))
                slot = &p;
        Drop(*slot);
        const unsigned others = ((1u << kCached) - 1) & ~(1u << SlotOf(slot));
        if (palette.colours.size() > 16)
        {
            Indices live;
            for (const Planes& p : cached)
                live.Unite(p.colours);
            if (palette.Planes() >= Palette::PlanesFor(static_cast<size_t>(live.Count())) + 2)
                Compact(others);
        }
        bool packed = slot->Pack(picture, palette);
        if (!packed)
        {
            Drop(*slot);
            Compact(others);
            packed = slot->Pack(picture, palette);
        }
        if (!packed)
        {
            // the frames kept but `keep` go only for a picture that fits without them
            Drop(*slot);
            if (!FitsWith(picture, keep))
                return nullptr;
            Compact(keep ? 1u << SlotOf(keep) : 0u);
            packed = slot->Pack(picture, palette);
        }
        if (!packed)
        {
            Drop(*slot);
            return nullptr;
        }
        slot->id = picture.id;
        slot->used = ++clock;
        stats.packed++;
        return slot;
    }

    /// The pair's vectors and the masks of the blocks each may take. `last`: the pair before, ending at a
    void Prepare(Pair& pair, const Pair* last, const Planes* before, const Planes& a, const Planes& b)
    {
        field.Estimate(a, b, planes);
        stats.pairs++;
        const int bw = field.blocksWide, bh = field.blocksHigh, words = a.Words();
        pair.width = a.width;
        pair.height = a.height;
        pair.blocksHigh = bh;
        std::vector<uint64_t>& stillBlocks = pair.stillBlocks;
        stillBlocks.assign(static_cast<size_t>(bh) * (words + 2), 0);
        for (int j = 0; j < bh; j++)
        {
            uint64_t* row = stillBlocks.data() + static_cast<size_t>(j) * (words + 2);
            const auto still = [&](int i) {
                const MotionField::Block& block = field.blocks[static_cast<size_t>(j) * bw + i];
                return block.matched && block.v == V{};
            };
            for (int i = 0; i < bw; i++)
                if (still(i))
                    row[1 + (i >> 3)] |= 0xFFull << (8 * (i & 7));
            if (still(0))
                row[0] = ~0ull;
            if (still(bw - 1))
            {
                if (const int tail = (bw * kBlock) & 63)
                    row[words] |= ~0ull << tail;
                row[words + 1] = ~0ull;
            }
        }
        // the moving vectors by their blocks, most first (equals: the first met)
        int16_t indexOf[64][64];  // by the vector's low bits: a field's range is under 32
        std::memset(indexOf, -1, sizeof(indexOf));
        std::vector<std::pair<V, int>>& all = allVectors;
        all.clear();
        for (const MotionField::Block& block : field.blocks)
        {
            if (!block.matched || block.v == V{})
                continue;
            int16_t& index = indexOf[block.v.y & 63][block.v.x & 63];
            if (index < 0)
            {
                index = static_cast<int16_t>(all.size());
                all.push_back({block.v, 1});
            }
            else
                all[static_cast<size_t>(index)].second++;
        }
        std::stable_sort(all.begin(), all.end(), [](const auto& p, const auto& q) { return p.second > q.second; });
        std::vector<V>& vectors = pair.vectors;
        vectors.clear();
        for (size_t c = 0; c < all.size() && c < static_cast<size_t>(kVectors); c++)
            vectors.push_back(all[c].first);

        const size_t per = static_cast<size_t>(bh) * words;
        std::vector<uint64_t>& own = pair.own;
        std::vector<uint64_t>& nearby = pair.nearby;
        own.assign(vectors.size() * per, 0);
        nearby.assign(vectors.size() * per, 0);
        pair.changed.clear();
        pair.same.clear();
        pair.confirmedBy.clear();
        if (vectors.empty())
            return;  // nothing moves: still or the nearer frame, pixel by pixel
        // own: one pass over the blocks; nearby: own spread a block each way, by words
        for (int j = 0; j < bh; j++)
            for (int i = 0; i < bw; i++)
            {
                const MotionField::Block& block = field.blocks[static_cast<size_t>(j) * bw + i];
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
        uint64_t wide[kMaxWords];
        for (size_t c = 0; c < vectors.size(); c++)
        {
            uint64_t* nearOf = nearby.data() + c * per;
            const uint64_t* ownOf = own.data() + c * per;
            if (c < static_cast<size_t>(kEverywhere))
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
                    uint64_t spreadW = row[w] | (row[w] << 8) | (row[w] >> 8);
                    if (w > 0)
                        spreadW |= row[w - 1] >> 56;
                    if (w + 1 < words)
                        spreadW |= row[w + 1] << 56;
                    wide[w] = w == words - 1 ? spreadW & tail : spreadW;
                    some |= spreadW != 0;
                }
                if (!some)
                    continue;
                for (int dj = -1; dj <= 1; dj++)
                    if (j + dj >= 0 && j + dj < bh)
                        for (int w = 0; w < words; w++)
                            nearOf[static_cast<size_t>(j + dj) * words + w] |= wide[w];
            }
        }
        // which vectors are confirmed asks only the blocks the field matched to them: `own` as it is now
        Confirm(pair, last, before, a, b);
        Credible(pair, a, b);
        // not confirmed: the vector moves only the detailed blocks
        for (size_t c = 0; c < vectors.size(); c++)
        {
            if (confirmed[c])
                continue;
            uint64_t* ownOf = own.data() + c * per;
            uint64_t* nearOf = nearby.data() + c * per;
            for (size_t k = 0; k < per; k++)
                if (ownOf[k] | nearOf[k])
                {
                    const uint64_t detailed = Free(a, k, bw, words);
                    ownOf[k] &= detailed;
                    nearOf[k] &= detailed;
                }
        }
    }

    /// The detailed blocks of block row and word `k` (Confirm)
    uint64_t Free(const Planes& a, size_t k, int bw, int words)
    {
        if (freeKnown[k])
            return free[k];
        const int j = static_cast<int>(k / static_cast<size_t>(words)), w = static_cast<int>(k % words);
        uint64_t across = 0, down = 0;
        for (int r = 0; r < kBlock; r++)
        {
            const int y = j * kBlock + r;
            uint64_t right = 0, below = 0;
            for (int q = 0; q < planes; q++)
            {
                const uint64_t* row = a.Row(q, y);
                right |= row[w + 1] ^ Shifted(row, w + 1, 1);
                if (r + 1 < kBlock)
                    below |= row[w + 1] ^ a.Row(q, y + 1)[w + 1];
            }
            // within the block: a byte's last pixel is not compared with the next block's first
            across += ByteOnes(right & ~kHighBits);
            down += ByteOnes(below);
        }
        uint64_t detailed = 0;
        for (int i = w * 8; i < std::min(bw, w * 8 + 8); i++)
        {
            const int shift = 8 * (i & 7);
            if (static_cast<int>((across >> shift) & 0xFF) >= LiteFlow::kDetail &&
                static_cast<int>((down >> shift) & 0xFF) >= LiteFlow::kDetail)
                detailed |= 0xFFull << shift;
        }
        freeKnown[k] = 1;
        free[k] = detailed;
        return detailed;
    }

    /// The pixels of block row and word `k` where a and `before` differ in place, on some row of the block row
    uint64_t Unlike(const Planes& before, const Planes& a, size_t k, int words)
    {
        if (unlikeKnown[k])
            return unlike[k];
        const int j = static_cast<int>(k / static_cast<size_t>(words)), w = static_cast<int>(k % words);
        uint64_t changed = 0;
        for (int r = 0; r < kBlock; r++)
            for (int q = 0; q < planes; q++)
                changed |= a.Row(q, j * kBlock + r)[w + 1] ^ before.Row(q, j * kBlock + r)[w + 1];
        unlikeKnown[k] = 1;
        unlike[k] = changed;
        return changed;
    }

    /// The blocks of `matched` (block row and word `k`) that confirm `v`: detailed, or carried exactly to b by it
    /// and come exactly from `before` (by it, or in place)
    uint64_t Confirming(V v, uint64_t matched, const Planes* before, const Planes& a, const Planes& b, size_t k,
                        int bw, int words)
    {
        const uint64_t detailed = matched & Free(a, k, bw, words);
        if (detailed || !before)
            return detailed;
        const int j = static_cast<int>(k / static_cast<size_t>(words)), w = static_cast<int>(k % words);
        uint64_t carryBad = 0;
        for (int r = 0; r < kBlock; r++)
        {
            const int y = j * kBlock + r;
            for (int q = 0; q < planes; q++)
                carryBad |= a.Row(q, y)[w + 1] ^ Shifted(b.Row(q, y + v.y), w + 1, v.x);
        }
        const uint64_t carried = matched & ZeroBytes(carryBad);
        if (!carried)
            return 0;
        if (const uint64_t held = carried & ZeroBytes(Unlike(*before, a, k, words)))
            return held;
        uint64_t goBad = 0;
        for (int r = 0; r < kBlock; r++)
        {
            const int y = j * kBlock + r;
            for (int q = 0; q < planes; q++)
                goBad |= a.Row(q, y)[w + 1] ^ Shifted(before->Row(q, y - v.y), w + 1, -v.x);
        }
        return carried & ZeroBytes(goBad);
    }

    /// `confirmed`: the moving vectors that move more than the detailed blocks, as LayerFlow's: a vector is confirmed
    /// by a block the field matched to it that is detailed (its pixels unlike their right and lower neighbours at
    /// least kDetail times each: too much to match by chance) or whose pixels it carries exactly to b and that came
    /// exactly from `before`, the frame before a: by the same vector (the motion going on) or in place (a held frame,
    /// a motion starting from rest). The field matches a block within MotionField::kAccept, and a line of one color
    /// across the block holds 8 values, not 64: content new every frame (a tape loader's lines) matches some vector
    /// by chance in every pair, never exactly over three frames. No frame before (the first pair, a seek): detail
    /// alone confirms. Any block that confirms will do: the one that did at the last pair is asked first
    void Confirm(Pair& pair, const Pair* last, const Planes* before, const Planes& a, const Planes& b)
    {
        const int bw = field.blocksWide, bh = field.blocksHigh, words = a.Words();
        const size_t per = static_cast<size_t>(bh) * words;
        free.resize(per);
        freeKnown.assign(per, 0);
        unlike.resize(per);
        if (last && !last->changed.empty())
        {
            // the last pair was (before, a): what changed there is what changed here
            unlike = last->changed;
            unlikeKnown.assign(per, 1);
        }
        else
            unlikeKnown.assign(per, 0);
        const size_t n = pair.vectors.size();
        confirmed.assign(n, 0);
        for (size_t c = 0; c < n; c++)
        {
            const V v = pair.vectors[c];
            const uint64_t* ownOf = pair.own.data() + c * per;
            size_t by = per;
            uint64_t blocks = 0;
            if (last)
                for (const auto& [u, block] : last->confirmedBy)
                    if (u == v)
                    {
                        const size_t i = block % static_cast<size_t>(bw);
                        const size_t k = block / static_cast<size_t>(bw) * static_cast<size_t>(words) + i / 8;
                        const uint64_t byte = 0xFFull << (8 * (i % 8));
                        if (ownOf[k] & byte)
                        {
                            blocks = Confirming(v, ownOf[k] & byte, before, a, b, k, bw, words);
                            by = k;
                        }
                    }
            for (size_t k = 0; k < per && !blocks; k++)
                if (ownOf[k])
                {
                    blocks = Confirming(v, ownOf[k], before, a, b, k, bw, words);
                    by = k;
                }
            if (!blocks)
                continue;
            confirmed[c] = 1;
            const size_t j = by / static_cast<size_t>(words), w = by % static_cast<size_t>(words);
            pair.confirmedBy.push_back(
                {v, j * static_cast<size_t>(bw) + w * 8 + static_cast<size_t>(std::countr_zero(blocks)) / 8});
        }
    }

    /// `own` and `nearby` cut to the blocks where their vector is credible: in the 3 x 3 blocks round a block, the
    /// changed pixels the vector carries both ways (a(y) to b(y + v), b(y) from a(y - v)) are some, and no fewer
    /// than the changes no vector explains there. Colour changing in place fits a moving vector by chance and
    /// explains nothing. The pair's `changed` on the way
    void Credible(Pair& pair, const Planes& a, const Planes& b)
    {
        const std::vector<V>& vectors = pair.vectors;
        std::vector<uint64_t>& own = pair.own;
        std::vector<uint64_t>& nearby = pair.nearby;
        const int words = a.Words(), bw = field.blocksWide, bh = field.blocksHigh;
        const int width = a.width, height = a.height;
        const size_t blocks = static_cast<size_t>(bw) * bh;
        const size_t n = vectors.size();
        const size_t per = static_cast<size_t>(bh) * words;
        explains.assign(n * blocks, 0);
        unexplained.assign(blocks, 0);
        any.assign(static_cast<size_t>(height) * words, 0);
        candidate.resize(static_cast<size_t>(bh));
        // a and b alike, every row: the still pixels; per block row the changed ones (the next pair's `unlike`)
        std::vector<uint64_t>& same = pair.same;
        same.resize(static_cast<size_t>(height) * words);
        pair.changed.assign(per, 0);
        for (int y = 0; y < height; y++)
            for (int w = 0; w < words; w++)
            {
                uint64_t alike = ~0ull;
                for (int p = 0; p < planes; p++)
                    alike &= ~(a.Row(p, y)[w + 1] ^ b.Row(p, y)[w + 1]);
                same[static_cast<size_t>(y) * words + w] = alike;
                pair.changed[static_cast<size_t>(y / kBlock) * words + w] |= ~alike;
            }
        // Draw's too: where a vector carries a pixel's colour, the rows counted (a superset of those Draw asks)
        pair.carries.resize(n * height * words);
        for (size_t c = 0; c < n; c++)
        {
            const V v = vectors[c];
            // a vector is counted only in the block rows where its support may be asked: a candidate there or next
            // to it
            for (int j = 0; j < bh; j++)
            {
                uint64_t some = 0;
                for (int w = 0; w < words; w++)
                    some |= own[c * per + static_cast<size_t>(j) * words + w] |
                            nearby[c * per + static_cast<size_t>(j) * words + w];
                candidate[static_cast<size_t>(j)] = some != 0;
            }
            for (int j = 0; j < bh; j++)
            {
                if (!candidate[static_cast<size_t>(j)] && !(j > 0 && candidate[static_cast<size_t>(j) - 1]) &&
                    !(j + 1 < bh && candidate[static_cast<size_t>(j) + 1]))
                    continue;
                uint64_t sums[kMaxWords];
                std::fill_n(sums, words, 0);
                for (int r = 0; r < kBlock; r++)
                {
                    const int y = j * kBlock + r;
                    for (int w = 0; w < words; w++)
                    {
                        const int jj = w + 1;
                        uint64_t onward = ~0ull, from = ~0ull;
                        for (int p = 0; p < planes; p++)
                        {
                            onward &= ~(a.Row(p, y)[jj] ^ Shifted(b.Row(p, y + v.y), jj, v.x));
                            from &= ~(b.Row(p, y)[jj] ^ Shifted(a.Row(p, y - v.y), jj, -v.x));
                        }
                        pair.carries[(c * height + y) * words + w] = onward | from;
                        const uint64_t explained = onward & from & ~same[static_cast<size_t>(y) * words + w];
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
            uint64_t sums[kMaxWords];
            std::fill_n(sums, words, 0);
            for (int r = 0; r < kBlock; r++)
            {
                const int y = j * kBlock + r;
                for (int w = 0; w < words; w++)
                {
                    uint64_t changed = ~same[static_cast<size_t>(y) * words + w] & ~any[static_cast<size_t>(y) * words + w];
                    if (w == words - 1 && (width & 63))
                        changed &= (1ull << (width & 63)) - 1;  // the margin past the picture is not a change
                    sums[w] += ByteOnes(changed);
                }
            }
            for (int i = 0; i < bw; i++)
                unexplained[static_cast<size_t>(j) * bw + i] = static_cast<int>((sums[i >> 3] >> (8 * (i & 7))) & 0xFF);
        }
        // 3 x 3 sums, in place: along the rows, then down the columns
        const auto round = [&](int* counts) {
            roundRow.resize(blocks);
            int* across = roundRow.data();
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
                    nearby[c * per + static_cast<size_t>(j) * words + (i >> 3)] &= byte;
                }
        }
    }

    /// `before`: the frame before a (null: none kept), for the carried step's still samples
    void Draw(const Pair& pair, const Planes* before, const Planes& a, const Planes& b, int w, uint32_t* out) const
    {
        const std::vector<V>& vectors = pair.vectors;
        const std::vector<uint64_t>& own = pair.own;
        const std::vector<uint64_t>& nearby = pair.nearby;
        const std::vector<uint64_t>& stillBlocks = pair.stillBlocks;
        const Planes& nearer = w < kOne / 2 ? a : b;
        const int width = a.width, height = a.height, words = a.Words(), bh = pair.blocksHigh;
        const size_t per = static_cast<size_t>(bh) * words;
        const int n = static_cast<int>(vectors.size());
        int sx[kVectors], sy[kVectors];
        for (int c = 0; c < n; c++)
        {
            sx[c] = (w * vectors[static_cast<size_t>(c)].x + kOne / 2) >> 8;
            sy[c] = (w * vectors[static_cast<size_t>(c)].y + kOne / 2) >> 8;
        }
        const uint32_t* colours = palette.colours.data();
        for (int y = 0; y < height; y++)
        {
            const size_t blockRow = static_cast<size_t>(y / kBlock) * words;
            for (int j = 0; j < words; j++)
            {
                const int jj = j + 1;
                uint64_t fit[kVectors];
                uint64_t res[kMaxPlanes] = {};
                uint64_t taken = 0;
                // per vector: a(x - s) against b(x - s + v), and the vector carries the pixel's own colour in one
                // frame: a(x) to b(x + v), or b(x) from a(x - v). A still pixel over a moving layer keeps its place
                for (int c = 0; c < n; c++)
                {
                    const V v = vectors[static_cast<size_t>(c)];
                    // a fit is asked only under the vector's blocks (nearby holds own)
                    if (!nearby[static_cast<size_t>(c) * per + blockRow + j])
                    {
                        fit[c] = 0;
                        continue;
                    }
                    uint64_t eq = ~0ull;
                    for (int p = 0; p < planes; p++)
                        eq &= ~(Shifted(a.Row(p, y - sy[c]), jj, -sx[c]) ^
                                Shifted(b.Row(p, y - sy[c] + v.y), jj, -sx[c] + v.x));
                    fit[c] = eq & pair.carries[(static_cast<size_t>(c) * height + y) * words + j];
                }
                const auto take = [&](uint64_t mask, int c) {
                    if (!mask)
                        return;
                    taken |= mask;
                    for (int p = 0; p < planes; p++)
                        res[p] |= mask & Shifted(a.Row(p, y - sy[c]), jj, -sx[c]);
                };
                // the block's own vector
                for (int c = 0; c < n; c++)
                    take(fit[c] & own[static_cast<size_t>(c) * per + blockRow + j] & ~taken, c);
                // still
                {
                    uint64_t same = ~0ull;
                    if (!pair.same.empty())
                        same = pair.same[static_cast<size_t>(y) * words + j];
                    else
                        for (int p = 0; p < planes; p++)
                            same &= ~(a.Row(p, y)[jj] ^ b.Row(p, y)[jj]);
                    const uint64_t mask = same & ~taken;
                    taken |= mask;
                    for (int p = 0; p < planes; p++)
                        res[p] |= mask & a.Row(p, y)[jj];
                }
                // a vector of the blocks round it
                for (int c = 0; c < n; c++)
                    take(fit[c] & nearby[static_cast<size_t>(c) * per + blockRow + j] & ~taken, c);
                // nothing fits in a block that moves: the nearer frame moved as the block is (content coming in at a
                // layer's edge, a shape changing on the move), not the nearer frame where it stood. From the other
                // frame where the nearer one's sample stood still over the three frames in a block the field matched
                // still, and the other's did not (another layer, still, the moving one goes under: stripes going down
                // under the paper's edge)
                for (int c = 0; c < n; c++)
                {
                    const uint64_t mask = own[static_cast<size_t>(c) * per + blockRow + j] & ~taken;
                    if (!mask)
                        continue;
                    taken |= mask;
                    const V v = vectors[static_cast<size_t>(c)];
                    uint64_t fromA[kMaxPlanes], fromB[kMaxPlanes];
                    // the still blocks' row of a sample's row, read clamped
                    const auto stillRow = [&](int row) {
                        return stillBlocks.data() +
                               static_cast<size_t>(std::clamp(row, 0, height - 1) / kBlock) * (words + 2);
                    };
                    uint64_t stillA = before ? Shifted(stillRow(y - sy[c]), jj, -sx[c]) : 0;
                    uint64_t stillB = before ? Shifted(stillRow(y - sy[c] + v.y), jj, -sx[c] + v.x) : 0;
                    for (int q = 0; q < planes; q++)
                    {
                        fromA[q] = Shifted(a.Row(q, y - sy[c]), jj, -sx[c]);
                        fromB[q] = Shifted(b.Row(q, y - sy[c] + v.y), jj, -sx[c] + v.x);
                        if (before)
                        {
                            stillA &= ~(fromA[q] ^ Shifted(b.Row(q, y - sy[c]), jj, -sx[c])) &
                                      ~(fromA[q] ^ Shifted(before->Row(q, y - sy[c]), jj, -sx[c]));
                            stillB &= ~(fromB[q] ^ Shifted(a.Row(q, y - sy[c] + v.y), jj, -sx[c] + v.x)) &
                                      ~(fromB[q] ^ Shifted(before->Row(q, y - sy[c] + v.y), jj, -sx[c] + v.x));
                        }
                    }
                    const uint64_t useA = w < kOne / 2 ? ~(stillA & ~stillB) : stillB & ~stillA;
                    for (int q = 0; q < planes; q++)
                        res[q] |= mask & ((useA & fromA[q]) | (~useA & fromB[q]));
                }
                // nothing fits: the nearer frame
                for (int p = 0; p < planes; p++)
                    res[p] |= ~taken & nearer.Row(p, y)[jj];

                // the indices, eight pixels at a time, and their colours
                uint32_t* line = out + static_cast<size_t>(y) * width + static_cast<size_t>(j) * 64;
                const int count = std::min(64, width - j * 64);
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
    }
};

LiteFlow::LiteFlow() : _impl(std::make_unique<Impl>()) {}

LiteFlow::~LiteFlow() = default;

bool LiteFlow::Fits(int width, int height)
{
    return width >= kBlock && height >= kBlock && width <= kMaxWidth && width % kBlock == 0 && height % kBlock == 0;
}

void LiteFlow::Reset()
{
    _impl->Forget();
}

const LiteFlow::Stats& LiteFlow::GetStats() const
{
    _impl->stats.rowScores = _impl->field.rowScores;
    return _impl->stats;
}

bool LiteFlow::Compose(const Picture& a, const Picture& b, int w, uint32_t* out)
{
    Impl& impl = *_impl;
    if (!out || !a.pixels || !b.pixels || a.width != b.width || a.height != b.height || !Fits(a.width, a.height) ||
        (a.id && b.id && impl.refusedA == a.id && impl.refusedB == b.id))
    {
        impl.stats.refused++;
        return false;
    }
    Planes* pa = impl.PlanesOf(a, nullptr);
    Planes* pb = pa ? impl.PlanesOf(b, pa) : nullptr;
    if (!pa || !pb)
    {
        // over kMaxColours colours in the two: not asked again
        impl.refusedA = a.id;
        impl.refusedB = b.id;
        impl.stats.refused++;
        return false;
    }
    impl.planes = impl.palette.Planes();
    using Pair = Impl::Pair;
    const auto isPair = [&](const Pair& p) {
        return p.valid && p.a == a.id && p.b == b.id && p.width == a.width && p.height == a.height;
    };
    Pair* pair = nullptr;
    if (a.id && b.id)
        for (int i : {impl.current, 1 - impl.current})
            if (isPair(impl.pairs[i]))
            {
                pair = &impl.pairs[i];
                impl.current = i;
                break;
            }
    if (!pair)
    {
        // the frame before a: the pair before's first when that pair ended at a
        const Pair& last = impl.pairs[impl.current];
        const bool follows = last.valid && a.id && last.b == a.id && last.width == a.width && last.height == a.height;
        impl.current = 1 - impl.current;
        pair = &impl.pairs[impl.current];
        pair->valid = false;
        pair->a = a.id;
        pair->b = b.id;
        pair->before = follows ? last.a : 0;
        impl.Prepare(*pair, follows ? &last : nullptr, impl.Before(*pair, a), *pa, *pb);
        pair->valid = true;
    }
    impl.Draw(*pair, impl.Before(*pair, a), *pa, *pb, std::clamp(w, 0, kOne), out);
    return true;
}

}  // namespace smoothmotion
