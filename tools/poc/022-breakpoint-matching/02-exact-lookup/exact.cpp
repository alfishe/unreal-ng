// Experiment 02: single-address breakpoints (exec, read, write) - how the "maybe" of the filter is resolved and
// what the filter itself costs. Every candidate answers exactly (verified against the reference).
//
//   map2      today: byte flags[64K] -> unordered_map (page key, then wildcard key)
//   flat      byte flags[64K] -> flat open-addressing hash (one probe sequence, no node chasing)
//   sorted    byte flags[64K] -> sorted vector + binary search
//   bits      one bit per address per kind (3 x 8 KB, L1-sized) -> flat hash on a set bit
//   direct    uint16 id per address per kind (3 x 128 KB), no filter: the id is the answer
//   bitsdirect  bit filter (8 KB) first, the id table only on a set bit

#include <unordered_map>

#include "../common/harness.h"

using namespace poc;

namespace
{
inline int KindIndex(uint8_t kind)
{
    // exec 0, read (and fetch) 1, write 2; ports are not part of this experiment
    return kind == KExec ? 0 : (kind == KWrite ? 2 : (kind == KRead || kind == KFetch ? 1 : -1));
}
inline uint8_t KindWatch(int k) { return k == 0 ? WExec : (k == 1 ? WRead : WWrite); }

/// Today's resolution, for reference within this experiment
class Map2
{
public:
    Map2(const std::vector<Breakpoint>& set, const PhysPage*)
    {
        for (const Breakpoint& b : set)
            for (int k = 0; k < 3; k++)
                if (b.watch & KindWatch(k))
                {
                    _flags[b.start] |= static_cast<uint8_t>(1 << k);
                    _has[k] = 1;
                    _map[k].emplace(0xFFFF0000u | b.start, b.id);
                }
    }
    int Match(uint8_t kind, uint16_t addr, const PhysPage* slots)
    {
        const int k = KindIndex(kind);
        if (k < 0 || !_has[k] || !(_flags[addr] & (1 << k)))
            return kNone;
        auto it = _map[k].find((static_cast<uint32_t>(slots[addr >> 14]) << 16) | addr);
        if (it == _map[k].end())
            it = _map[k].find(0xFFFF0000u | addr);
        return it == _map[k].end() ? kNone : it->second;
    }
    void Remap(int, PhysPage, const PhysPage*) {}

private:
    uint8_t _has[3] = {};
    uint8_t _flags[0x10000] = {};
    std::unordered_map<uint32_t, uint16_t> _map[3];
};

/// Open addressing, power-of-two capacity, linear probing; key 0 is never used (addresses are stored + 1)
class FlatTable
{
public:
    void Build(const std::vector<std::pair<uint16_t, uint16_t>>& entries)
    {
        size_t cap = 16;
        while (cap < entries.size() * 2 + 1)
            cap <<= 1;
        _mask = cap - 1;
        _keys.assign(cap, 0);
        _ids.assign(cap, 0);
        for (auto [addr, id] : entries)
        {
            size_t i = Hash(addr) & _mask;
            while (_keys[i] && _keys[i] != static_cast<uint32_t>(addr) + 1)
                i = (i + 1) & _mask;
            if (!_keys[i])
            {
                _keys[i] = static_cast<uint32_t>(addr) + 1;
                _ids[i] = id;
            }
        }
    }
    int Find(uint16_t addr) const
    {
        size_t i = Hash(addr) & _mask;
        const uint32_t key = static_cast<uint32_t>(addr) + 1;
        while (_keys[i])
        {
            if (_keys[i] == key)
                return _ids[i];
            i = (i + 1) & _mask;
        }
        return kNone;
    }

private:
    static size_t Hash(uint16_t a) { return (a * 0x9E3779B1u) >> 8; }
    size_t _mask = 0;
    std::vector<uint32_t> _keys;
    std::vector<uint16_t> _ids;
};

class Flat
{
public:
    Flat(const std::vector<Breakpoint>& set, const PhysPage*)
    {
        std::vector<std::pair<uint16_t, uint16_t>> e[3];
        for (const Breakpoint& b : set)
            for (int k = 0; k < 3; k++)
                if (b.watch & KindWatch(k))
                {
                    _flags[b.start] |= static_cast<uint8_t>(1 << k);
                    _has[k] = 1;
                    e[k].push_back({b.start, b.id});
                }
        for (int k = 0; k < 3; k++)
            _table[k].Build(e[k]);
    }
    int Match(uint8_t kind, uint16_t addr, const PhysPage*)
    {
        const int k = KindIndex(kind);
        if (k < 0 || !_has[k] || !(_flags[addr] & (1 << k)))
            return kNone;
        return _table[k].Find(addr);
    }
    void Remap(int, PhysPage, const PhysPage*) {}

private:
    uint8_t _has[3] = {};
    uint8_t _flags[0x10000] = {};
    FlatTable _table[3];
};

class Sorted
{
public:
    Sorted(const std::vector<Breakpoint>& set, const PhysPage*)
    {
        for (const Breakpoint& b : set)
            for (int k = 0; k < 3; k++)
                if (b.watch & KindWatch(k))
                {
                    _flags[b.start] |= static_cast<uint8_t>(1 << k);
                    _has[k] = 1;
                    _v[k].push_back({b.start, b.id});
                }
        for (auto& v : _v)
            std::sort(v.begin(), v.end());
    }
    int Match(uint8_t kind, uint16_t addr, const PhysPage*)
    {
        const int k = KindIndex(kind);
        if (k < 0 || !_has[k] || !(_flags[addr] & (1 << k)))
            return kNone;
        auto it = std::lower_bound(_v[k].begin(), _v[k].end(), std::make_pair(addr, uint16_t(0)));
        return (it != _v[k].end() && it->first == addr) ? it->second : kNone;
    }
    void Remap(int, PhysPage, const PhysPage*) {}

private:
    uint8_t _has[3] = {};
    uint8_t _flags[0x10000] = {};
    std::vector<std::pair<uint16_t, uint16_t>> _v[3];
};

class Bits
{
public:
    Bits(const std::vector<Breakpoint>& set, const PhysPage*)
    {
        std::vector<std::pair<uint16_t, uint16_t>> e[3];
        for (const Breakpoint& b : set)
            for (int k = 0; k < 3; k++)
                if (b.watch & KindWatch(k))
                {
                    _bits[k][b.start >> 6] |= 1ull << (b.start & 63);
                    e[k].push_back({b.start, b.id});
                }
        for (int k = 0; k < 3; k++)
            _table[k].Build(e[k]);
    }
    int Match(uint8_t kind, uint16_t addr, const PhysPage*)
    {
        const int k = KindIndex(kind);
        if (k < 0 || !((_bits[k][addr >> 6] >> (addr & 63)) & 1))
            return kNone;
        return _table[k].Find(addr);
    }
    void Remap(int, PhysPage, const PhysPage*) {}

private:
    uint64_t _bits[3][1024] = {};
    FlatTable _table[3];
};

class Direct
{
public:
    Direct(const std::vector<Breakpoint>& set, const PhysPage*)
    {
        for (const Breakpoint& b : set)
            for (int k = 0; k < 3; k++)
                if ((b.watch & KindWatch(k)) && !_id[k][b.start])
                    _id[k][b.start] = b.id;
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
    uint16_t _id[3][0x10000] = {};  // 0 = none (ids start at 1)
};

class BitsDirect
{
public:
    BitsDirect(const std::vector<Breakpoint>& set, const PhysPage*)
    {
        for (const Breakpoint& b : set)
            for (int k = 0; k < 3; k++)
                if (b.watch & KindWatch(k))
                {
                    _bits[k][b.start >> 6] |= 1ull << (b.start & 63);
                    if (!_id[k][b.start])
                        _id[k][b.start] = b.id;
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
    for (auto shape : {SetSpec::ExecPoints, SetSpec::ReadWritePoints})
        for (const SetSpec& s : ScaleSpecs(shape, shape == SetSpec::ExecPoints ? "exec" : "rw"))
            specs.push_back(s);
    RegisterMatcher<Map2>("map2", specs);
    RegisterMatcher<Flat>("flat", specs);
    RegisterMatcher<Sorted>("sorted", specs);
    RegisterMatcher<Bits>("bits", specs);
    RegisterMatcher<Direct>("direct", specs);
    RegisterMatcher<BitsDirect>("bitsdirect", specs);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
