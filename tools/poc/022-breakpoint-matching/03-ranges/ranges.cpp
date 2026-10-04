// Experiment 03: address-range breakpoints (CPU addresses, any page) - read / write ranges of 16, 256 and
// 4096 bytes, 1 to 10000 of them, overlapping as the generator places them.
//
//   linear       has-flag, then every range of the kind compared (what a naive interval list does)
//   segments     the ranges flattened into disjoint covered segments (sweep at set time), binary search
//   bitsseg      one bit per address per kind (8 KB, painted at set time) first, segments only on a set bit
//   painted      uint16 id per address per kind (128 KB, painted): one load is the answer
//   bitspainted  the bit filter first, the id table only on a set bit

#include "../common/harness.h"

using namespace poc;

namespace
{
inline int KindIndex(uint8_t kind)
{
    return kind == KExec ? 0 : (kind == KWrite ? 2 : (kind == KRead || kind == KFetch ? 1 : -1));
}
inline uint8_t KindWatch(int k) { return k == 0 ? WExec : (k == 1 ? WRead : WWrite); }

struct Range
{
    uint16_t start, end, id;
};

class Linear
{
public:
    Linear(const std::vector<Breakpoint>& set, const PhysPage*)
    {
        for (const Breakpoint& b : set)
            for (int k = 0; k < 3; k++)
                if (b.watch & KindWatch(k))
                    _r[k].push_back({b.start, b.end, b.id});
    }
    int Match(uint8_t kind, uint16_t addr, const PhysPage*)
    {
        const int k = KindIndex(kind);
        if (k < 0)
            return kNone;
        for (const Range& r : _r[k])
            if (addr >= r.start && addr <= r.end)
                return r.id;
        return kNone;
    }
    void Remap(int, PhysPage, const PhysPage*) {}

private:
    std::vector<Range> _r[3];
};

/// Disjoint covered segments with the id of one range covering each: built by painting a scratch table and
/// collecting runs (set time is not measured, and the sweep is O(64K))
std::vector<Range> Segments(const std::vector<Range>& ranges)
{
    std::vector<uint16_t> owner(0x10000, 0);
    for (const Range& r : ranges)
        for (uint32_t a = r.start; a <= r.end; a++)
            if (!owner[a])
                owner[a] = r.id;
    std::vector<Range> seg;
    for (uint32_t a = 0; a < 0x10000;)
    {
        if (!owner[a])
        {
            a++;
            continue;
        }
        const uint32_t s = a;
        while (a < 0x10000 && owner[a])
            a++;
        seg.push_back({static_cast<uint16_t>(s), static_cast<uint16_t>(a - 1), owner[s]});
    }
    return seg;
}

inline int FindSegment(const std::vector<Range>& seg, uint16_t addr)
{
    // the last segment starting at or before addr
    size_t lo = 0, hi = seg.size();
    while (lo < hi)
    {
        const size_t mid = (lo + hi) / 2;
        if (seg[mid].start <= addr)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == 0)
        return kNone;
    const Range& r = seg[lo - 1];
    return addr <= r.end ? r.id : kNone;
}

class SegmentsMatcher
{
public:
    SegmentsMatcher(const std::vector<Breakpoint>& set, const PhysPage*)
    {
        std::vector<Range> r[3];
        for (const Breakpoint& b : set)
            for (int k = 0; k < 3; k++)
                if (b.watch & KindWatch(k))
                    r[k].push_back({b.start, b.end, b.id});
        for (int k = 0; k < 3; k++)
            _seg[k] = Segments(r[k]);
    }
    int Match(uint8_t kind, uint16_t addr, const PhysPage*)
    {
        const int k = KindIndex(kind);
        if (k < 0 || _seg[k].empty())
            return kNone;
        return FindSegment(_seg[k], addr);
    }
    void Remap(int, PhysPage, const PhysPage*) {}

private:
    std::vector<Range> _seg[3];
};

class BitsSeg
{
public:
    BitsSeg(const std::vector<Breakpoint>& set, const PhysPage*)
    {
        std::vector<Range> r[3];
        for (const Breakpoint& b : set)
            for (int k = 0; k < 3; k++)
                if (b.watch & KindWatch(k))
                {
                    r[k].push_back({b.start, b.end, b.id});
                    for (uint32_t a = b.start; a <= b.end; a++)
                        _bits[k][a >> 6] |= 1ull << (a & 63);
                }
        for (int k = 0; k < 3; k++)
            _seg[k] = Segments(r[k]);
    }
    int Match(uint8_t kind, uint16_t addr, const PhysPage*)
    {
        const int k = KindIndex(kind);
        if (k < 0 || !((_bits[k][addr >> 6] >> (addr & 63)) & 1))
            return kNone;
        return FindSegment(_seg[k], addr);
    }
    void Remap(int, PhysPage, const PhysPage*) {}

private:
    uint64_t _bits[3][1024] = {};
    std::vector<Range> _seg[3];
};

class Painted
{
public:
    Painted(const std::vector<Breakpoint>& set, const PhysPage*)
    {
        for (const Breakpoint& b : set)
            for (int k = 0; k < 3; k++)
                if (b.watch & KindWatch(k))
                    for (uint32_t a = b.start; a <= b.end; a++)
                        if (!_id[k][a])
                            _id[k][a] = b.id;
    }
    int Match(uint8_t kind, uint16_t addr, const PhysPage*)
    {
        const int k = KindIndex(kind);
        if (k < 0)
            return kNone;
        const uint16_t id = _id[k][addr];
        return id ? id : kNone;
    }
    void Remap(int, PhysPage, const PhysPage*) {}

private:
    uint16_t _id[3][0x10000] = {};
};

class BitsPainted
{
public:
    BitsPainted(const std::vector<Breakpoint>& set, const PhysPage*)
    {
        for (const Breakpoint& b : set)
            for (int k = 0; k < 3; k++)
                if (b.watch & KindWatch(k))
                    for (uint32_t a = b.start; a <= b.end; a++)
                    {
                        _bits[k][a >> 6] |= 1ull << (a & 63);
                        if (!_id[k][a])
                            _id[k][a] = b.id;
                    }
    }
    int Match(uint8_t kind, uint16_t addr, const PhysPage*)
    {
        const int k = KindIndex(kind);
        if (k < 0 || !((_bits[k][addr >> 6] >> (addr & 63)) & 1))
            return kNone;
        return _id[k][addr];
    }
    void Remap(int, PhysPage, const PhysPage*) {}

private:
    uint64_t _bits[3][1024] = {};
    uint16_t _id[3][0x10000] = {};
};
}  // namespace

int main(int argc, char** argv)
{
    benchmark::Initialize(&argc, argv);
    RegisterFloor();
    std::vector<SetSpec> specs;
    for (uint16_t len : {16, 256, 4096})
    {
        const std::string tag = "range" + std::to_string(len);
        for (const SetSpec& s : ScaleSpecs(SetSpec::Ranges, tag.c_str(), len, {1, 10, 100, 1000, 10000}))
            specs.push_back(s);
    }
    RegisterMatcher<Linear>("linear", specs, 50000);
    RegisterMatcher<SegmentsMatcher>("segments", specs);
    RegisterMatcher<BitsSeg>("bitsseg", specs);
    RegisterMatcher<Painted>("painted", specs);
    RegisterMatcher<BitsPainted>("bitspainted", specs);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
