// Experiment 05: port breakpoints with masks (design F7: (port & mask) == (value & mask); a Spectrum decodes
// ports partly, #FE answers every even port). Port accesses are a small share of the trace (well under 1%),
// so the question is mostly what the check costs when it is armed and how it scales.
//
//   map        today: has-flag -> unordered_map on the exact port (no masks: masked sets are expanded into
//              every matching port first, which is what a user would have to do)
//   buckets    one group per distinct mask: a flat bit set of (port & mask) values; checks = distinct masks
//   painted    uint16 id per port per direction (2 x 128 KB), masks expanded at set time: one load
//   bitspainted  a bit per port per direction (2 x 8 KB) first, the id table only on a set bit

#include <unordered_map>

#include "../common/harness.h"

using namespace poc;

namespace
{
inline int Dir(uint8_t kind) { return kind == KIn ? 0 : (kind == KOut ? 1 : -1); }
inline uint8_t DirWatch(int d) { return d == 0 ? WIn : WOut; }

/// Every port a masked breakpoint matches
template <typename F>
void ForEachPort(const Breakpoint& b, F&& f)
{
    const uint16_t want = b.start & b.mask;
    const uint16_t free = static_cast<uint16_t>(~b.mask);
    // enumerate the subsets of the free bits
    uint16_t sub = 0;
    do
    {
        f(static_cast<uint16_t>(want | sub));
        sub = static_cast<uint16_t>((sub - free) & free);
    } while (sub != 0);
}

class Map
{
public:
    Map(const std::vector<Breakpoint>& set, const PhysPage*)
    {
        for (const Breakpoint& b : set)
            for (int d = 0; d < 2; d++)
                if (b.watch & DirWatch(d))
                {
                    _has[d] = true;
                    ForEachPort(b, [&](uint16_t p) { _m[d].emplace(p, b.id); });
                }
    }
    int Match(uint8_t kind, uint16_t port, const PhysPage*)
    {
        const int d = Dir(kind);
        if (d < 0 || !_has[d])
            return kNone;
        auto it = _m[d].find(port);
        return it == _m[d].end() ? kNone : it->second;
    }
    void Remap(int, PhysPage, const PhysPage*) {}

private:
    bool _has[2] = {};
    std::unordered_map<uint16_t, uint16_t> _m[2];
};

class Buckets
{
public:
    Buckets(const std::vector<Breakpoint>& set, const PhysPage*)
    {
        for (const Breakpoint& b : set)
            for (int d = 0; d < 2; d++)
                if (b.watch & DirWatch(d))
                {
                    Bucket* bucket = nullptr;
                    for (Bucket& g : _b[d])
                        if (g.mask == b.mask)
                            bucket = &g;
                    if (!bucket)
                    {
                        _b[d].push_back(Bucket{b.mask, {}, std::vector<uint16_t>(0x10000, 0)});
                        bucket = &_b[d].back();
                    }
                    const uint16_t v = b.start & b.mask;
                    bucket->bits[v >> 6] |= 1ull << (v & 63);
                    if (!bucket->ids[v])
                        bucket->ids[v] = b.id;
                }
    }
    int Match(uint8_t kind, uint16_t port, const PhysPage*)
    {
        const int d = Dir(kind);
        if (d < 0)
            return kNone;
        for (const Bucket& g : _b[d])
        {
            const uint16_t v = port & g.mask;
            if ((g.bits[v >> 6] >> (v & 63)) & 1)
                return g.ids[v];
        }
        return kNone;
    }
    void Remap(int, PhysPage, const PhysPage*) {}

private:
    struct Bucket
    {
        uint16_t mask;
        uint64_t bits[1024];
        std::vector<uint16_t> ids;
    };
    std::vector<Bucket> _b[2];
};

class Painted
{
public:
    Painted(const std::vector<Breakpoint>& set, const PhysPage*)
    {
        for (const Breakpoint& b : set)
            for (int d = 0; d < 2; d++)
                if (b.watch & DirWatch(d))
                    ForEachPort(b, [&](uint16_t p) {
                        if (!_id[d][p])
                            _id[d][p] = b.id;
                    });
    }
    int Match(uint8_t kind, uint16_t port, const PhysPage*)
    {
        const int d = Dir(kind);
        if (d < 0)
            return kNone;
        const uint16_t id = _id[d][port];
        return id ? id : kNone;
    }
    void Remap(int, PhysPage, const PhysPage*) {}

private:
    uint16_t _id[2][0x10000] = {};
};

class BitsPainted
{
public:
    BitsPainted(const std::vector<Breakpoint>& set, const PhysPage*)
    {
        for (const Breakpoint& b : set)
            for (int d = 0; d < 2; d++)
                if (b.watch & DirWatch(d))
                    ForEachPort(b, [&](uint16_t p) {
                        _bits[d][p >> 6] |= 1ull << (p & 63);
                        if (!_id[d][p])
                            _id[d][p] = b.id;
                    });
    }
    int Match(uint8_t kind, uint16_t port, const PhysPage*)
    {
        const int d = Dir(kind);
        if (d < 0 || !((_bits[d][port >> 6] >> (port & 63)) & 1))
            return kNone;
        return _id[d][port];
    }
    void Remap(int, PhysPage, const PhysPage*) {}

private:
    uint64_t _bits[2][1024] = {};
    uint16_t _id[2][0x10000] = {};
};
}  // namespace

int main(int argc, char** argv)
{
    benchmark::Initialize(&argc, argv);
    RegisterFloor();
    const std::vector<SetSpec> specs = ScaleSpecs(SetSpec::Ports, "port", 0, {1, 10, 100, 1000});
    RegisterMatcher<Map>("map", specs);
    RegisterMatcher<Buckets>("buckets", specs);
    RegisterMatcher<Painted>("painted", specs);
    RegisterMatcher<BitsPainted>("bitspainted", specs);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
