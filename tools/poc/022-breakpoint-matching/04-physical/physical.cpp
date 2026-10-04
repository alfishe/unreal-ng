// Experiment 04: breakpoints on physical memory - a page and offsets that match through whatever slot shows
// the page (design F4), and slot-bound ones that match only while the page is in one slot (F3). The page of
// an access is the slot table's entry, as Memory::MapZ80AddressToPhysicalPage gives it; paging moves pages
// between slots at run time (the remap events).
//
//   hashkey   bit filter over CPU addresses (an offset marked in all four slots, the page may be anywhere),
//             then a flat hash on (page, offset) over the expanded points
//   pagemap   the same filter, then unordered_map page -> 16K id slice, then the slice
//   slices    per page with breakpoints a 16K id slice; per slot a pointer to the slice of the page it shows
//             (or an empty slice), moved on remap: one dependent load. Slot-bound ones in a second slice per
//             (page, slot) - two loads
//   merged    per slot one 16K id table = page slice + slot-bound slice, rebuilt on remap: one load, remap
//             costs a 32 KB rebuild
//   bitslices a 2 KB bit slice per page next to the id slice, per-slot pointers to both: the bit (L1) first,
//             the ids only on a set bit
//
// Besides the recorded traces, "remapheavy" is the Dizzy trace with slot 3 switched every 64 events (the worst
// case for remap-time work; ATM / TS-Conf code pages far more often than a 128K game).

#include <array>
#include <unordered_map>

#include "../common/harness.h"

using namespace poc;

namespace
{
inline int KindIndex(uint8_t kind)
{
    return kind == KExec ? 0 : (kind == KWrite ? 2 : (kind == KRead || kind == KFetch ? 1 : -1));
}
inline uint8_t KindWatch(int k) { return k == 0 ? WExec : (k == 1 ? WRead : WWrite); }

using Slice = std::array<uint16_t, 0x4000>;
using BitSlice = std::array<uint64_t, 0x4000 / 64>;

/// The flat hash of experiment 02, keyed by a 32-bit (page << 14 | offset)
class FlatTable
{
public:
    void Build(const std::vector<std::pair<uint32_t, uint16_t>>& entries)
    {
        size_t cap = 16;
        while (cap < entries.size() * 2 + 1)
            cap <<= 1;
        _mask = cap - 1;
        _keys.assign(cap, 0);
        _ids.assign(cap, 0);
        for (auto [key, id] : entries)
        {
            size_t i = Hash(key) & _mask;
            while (_keys[i] && _keys[i] != key + 1)
                i = (i + 1) & _mask;
            if (!_keys[i])
            {
                _keys[i] = key + 1;
                _ids[i] = id;
            }
        }
    }
    int Find(uint32_t key) const
    {
        size_t i = Hash(key) & _mask;
        while (_keys[i])
        {
            if (_keys[i] == key + 1)
                return _ids[i];
            i = (i + 1) & _mask;
        }
        return kNone;
    }

private:
    static size_t Hash(uint32_t a) { return (a * 0x9E3779B1u) >> 7; }
    size_t _mask = 0;
    std::vector<uint32_t> _keys;
    std::vector<uint16_t> _ids;
};

class HashKey
{
public:
    HashKey(const std::vector<Breakpoint>& set, const PhysPage*)
    {
        std::vector<std::pair<uint32_t, uint16_t>> phys[3], bound[3];
        for (const Breakpoint& b : set)
            for (int k = 0; k < 3; k++)
            {
                if (!(b.watch & KindWatch(k)))
                    continue;
                for (uint32_t a = b.start; a <= b.end; a++)
                {
                    if (b.space == Space::Phys)
                    {
                        for (uint32_t s = 0; s < 4; s++)
                            Mark(k, static_cast<uint16_t>((s << 14) | a));
                        phys[k].push_back({(static_cast<uint32_t>(b.page) << 14) | a, b.id});
                    }
                    else  // Z80Slot: the CPU address itself, the page checked through the key
                    {
                        Mark(k, static_cast<uint16_t>(a));
                        bound[k].push_back({(static_cast<uint32_t>(b.page) << 16) | a, b.id});
                    }
                }
            }
        for (int k = 0; k < 3; k++)
        {
            _phys[k].Build(phys[k]);
            _bound[k].Build(bound[k]);
        }
    }
    int Match(uint8_t kind, uint16_t addr, const PhysPage* slots)
    {
        const int k = KindIndex(kind);
        if (k < 0 || !((_bits[k][addr >> 6] >> (addr & 63)) & 1))
            return kNone;
        const PhysPage page = slots[addr >> 14];
        const int id = _phys[k].Find((static_cast<uint32_t>(page) << 14) | (addr & 0x3FFF));
        return id != kNone ? id : _bound[k].Find((static_cast<uint32_t>(page) << 16) | addr);
    }
    void Remap(int, PhysPage, const PhysPage*) {}

private:
    void Mark(int k, uint16_t a) { _bits[k][a >> 6] |= 1ull << (a & 63); }
    uint64_t _bits[3][1024] = {};
    FlatTable _phys[3], _bound[3];
};

class PageMap
{
public:
    PageMap(const std::vector<Breakpoint>& set, const PhysPage*)
    {
        for (const Breakpoint& b : set)
            for (int k = 0; k < 3; k++)
            {
                if (!(b.watch & KindWatch(k)))
                    continue;
                auto& slice = b.space == Space::Phys ? _phys[k][b.page] : _bound[k][(static_cast<uint32_t>(b.page) << 2) | (b.start >> 14)];
                if (slice.empty())
                    slice.assign(0x4000, 0);
                for (uint32_t a = b.start; a <= b.end; a++)
                {
                    const uint16_t offset = static_cast<uint16_t>(a & 0x3FFF);
                    if (!slice[offset])
                        slice[offset] = b.id;
                    if (b.space == Space::Phys)
                        for (uint32_t s = 0; s < 4; s++)
                            Mark(k, static_cast<uint16_t>((s << 14) | offset));
                    else
                        Mark(k, static_cast<uint16_t>(a));
                }
            }
    }
    int Match(uint8_t kind, uint16_t addr, const PhysPage* slots)
    {
        const int k = KindIndex(kind);
        if (k < 0 || !((_bits[k][addr >> 6] >> (addr & 63)) & 1))
            return kNone;
        const PhysPage page = slots[addr >> 14];
        auto it = _phys[k].find(page);
        if (it != _phys[k].end() && it->second[addr & 0x3FFF])
            return it->second[addr & 0x3FFF];
        auto jt = _bound[k].find((static_cast<uint32_t>(page) << 2) | (addr >> 14));
        if (jt != _bound[k].end() && jt->second[addr & 0x3FFF])
            return jt->second[addr & 0x3FFF];
        return kNone;
    }
    void Remap(int, PhysPage, const PhysPage*) {}

private:
    void Mark(int k, uint16_t a) { _bits[k][a >> 6] |= 1ull << (a & 63); }
    uint64_t _bits[3][1024] = {};
    std::unordered_map<uint32_t, std::vector<uint16_t>> _phys[3], _bound[3];
};

/// Shared by slices / merged / bitslices: the slices per page and per (page, slot)
struct SliceStore
{
    std::unordered_map<PhysPage, std::unique_ptr<Slice>> phys[3];
    std::unordered_map<uint32_t, std::unique_ptr<Slice>> bound[3];  // page << 2 | slot
    std::unordered_map<PhysPage, std::unique_ptr<BitSlice>> physBits[3];
    std::unordered_map<uint32_t, std::unique_ptr<BitSlice>> boundBits[3];

    explicit SliceStore(const std::vector<Breakpoint>& set)
    {
        for (const Breakpoint& b : set)
            for (int k = 0; k < 3; k++)
            {
                if (!(b.watch & KindWatch(k)))
                    continue;
                const bool isPhys = b.space == Space::Phys;
                const uint32_t key = isPhys ? b.page : (static_cast<uint32_t>(b.page) << 2) | (b.start >> 14);
                auto& slot = isPhys ? phys[k][b.page] : bound[k][key];
                auto& bits = isPhys ? physBits[k][b.page] : boundBits[k][key];
                if (!slot)
                {
                    slot = std::make_unique<Slice>();
                    slot->fill(0);
                    bits = std::make_unique<BitSlice>();
                    bits->fill(0);
                }
                for (uint32_t a = b.start; a <= b.end; a++)
                {
                    const uint16_t offset = static_cast<uint16_t>(a & 0x3FFF);
                    if (!(*slot)[offset])
                        (*slot)[offset] = b.id;
                    (*bits)[offset >> 6] |= 1ull << (offset & 63);
                }
            }
    }
    const Slice* Phys(int k, PhysPage page) const
    {
        auto it = phys[k].find(page);
        return it == phys[k].end() ? nullptr : it->second.get();
    }
    const Slice* Bound(int k, PhysPage page, int slot) const
    {
        auto it = bound[k].find((static_cast<uint32_t>(page) << 2) | static_cast<uint32_t>(slot));
        return it == bound[k].end() ? nullptr : it->second.get();
    }
    const BitSlice* PhysBits(int k, PhysPage page) const
    {
        auto it = physBits[k].find(page);
        return it == physBits[k].end() ? nullptr : it->second.get();
    }
    const BitSlice* BoundBits(int k, PhysPage page, int slot) const
    {
        auto it = boundBits[k].find((static_cast<uint32_t>(page) << 2) | static_cast<uint32_t>(slot));
        return it == boundBits[k].end() ? nullptr : it->second.get();
    }
};

const Slice& EmptySlice()
{
    static Slice empty{};
    return empty;
}
const BitSlice& EmptyBits()
{
    static BitSlice empty{};
    return empty;
}

class Slices
{
public:
    Slices(const std::vector<Breakpoint>& set, const PhysPage* slots) : _store(set)
    {
        for (int s = 0; s < 4; s++)
            Remap(s, slots[s], slots);
    }
    int Match(uint8_t kind, uint16_t addr, const PhysPage*)
    {
        const int k = KindIndex(kind);
        if (k < 0)
            return kNone;
        const int s = addr >> 14;
        const uint16_t offset = addr & 0x3FFF;
        const uint16_t id = (*_phys[k][s])[offset];
        if (id)
            return id;
        const uint16_t idb = (*_bound[k][s])[offset];
        return idb ? idb : kNone;
    }
    void Remap(int slot, PhysPage page, const PhysPage*)
    {
        for (int k = 0; k < 3; k++)
        {
            const Slice* p = _store.Phys(k, page);
            const Slice* b = _store.Bound(k, page, slot);
            _phys[k][slot] = p ? p : &EmptySlice();
            _bound[k][slot] = b ? b : &EmptySlice();
        }
    }

private:
    SliceStore _store;
    const Slice* _phys[3][4];
    const Slice* _bound[3][4];
};

class Merged
{
public:
    Merged(const std::vector<Breakpoint>& set, const PhysPage* slots) : _store(set)
    {
        for (int s = 0; s < 4; s++)
            Remap(s, slots[s], slots);
    }
    int Match(uint8_t kind, uint16_t addr, const PhysPage*)
    {
        const int k = KindIndex(kind);
        if (k < 0)
            return kNone;
        const uint16_t id = _eff[k][addr >> 14][addr & 0x3FFF];
        return id ? id : kNone;
    }
    void Remap(int slot, PhysPage page, const PhysPage*)
    {
        for (int k = 0; k < 3; k++)
        {
            const Slice* p = _store.Phys(k, page);
            const Slice* b = _store.Bound(k, page, slot);
            Slice& eff = _eff[k][slot];
            if (p)
                eff = *p;
            else
                eff.fill(0);
            if (b)
                for (size_t i = 0; i < eff.size(); i++)
                    if (!eff[i])
                        eff[i] = (*b)[i];
        }
    }

private:
    SliceStore _store;
    Slice _eff[3][4];
};

class BitSlices
{
public:
    BitSlices(const std::vector<Breakpoint>& set, const PhysPage* slots) : _store(set)
    {
        for (int s = 0; s < 4; s++)
            Remap(s, slots[s], slots);
    }
    int Match(uint8_t kind, uint16_t addr, const PhysPage*)
    {
        const int k = KindIndex(kind);
        if (k < 0)
            return kNone;
        const int s = addr >> 14;
        const uint16_t offset = addr & 0x3FFF;
        if (((*_physBits[k][s])[offset >> 6] >> (offset & 63)) & 1)
            return (*_phys[k][s])[offset];
        if (((*_boundBits[k][s])[offset >> 6] >> (offset & 63)) & 1)
            return (*_bound[k][s])[offset];
        return kNone;
    }
    void Remap(int slot, PhysPage page, const PhysPage*)
    {
        for (int k = 0; k < 3; k++)
        {
            const Slice* p = _store.Phys(k, page);
            const Slice* b = _store.Bound(k, page, slot);
            const BitSlice* pb = _store.PhysBits(k, page);
            const BitSlice* bb = _store.BoundBits(k, page, slot);
            _phys[k][slot] = p ? p : &EmptySlice();
            _bound[k][slot] = b ? b : &EmptySlice();
            _physBits[k][slot] = pb ? pb : &EmptyBits();
            _boundBits[k][slot] = bb ? bb : &EmptyBits();
        }
    }

private:
    SliceStore _store;
    const Slice* _phys[3][4];
    const Slice* _bound[3][4];
    const BitSlice* _physBits[3][4];
    const BitSlice* _boundBits[3][4];
};

/// Dizzy with slot 3 switched among RAM pages 0, 1, 3, 4, 6, 7 every 64 events
const Trace* RemapHeavyTrace()
{
    for (const Trace* base : AllTraces())
        if (base->name.find("game-dizzy") != std::string::npos)
        {
            static Trace heavy;
            heavy.name = "remapheavy-dizzy.trace";
            std::copy(base->initialSlots, base->initialSlots + 4, heavy.initialSlots);
            static const uint8_t pages[] = {0, 1, 3, 4, 6, 7};
            size_t n = 0;
            for (const Event& e : base->events)
            {
                if (e.kind == KRemap && e.addr == 3)
                    continue;  // the game's own slot-3 switches give way to the synthetic ones
                if (++n % 64 == 0)
                    heavy.events.push_back({3, MakePhys(kRam, pages[(n / 64) % 6]), KRemap});
                heavy.events.push_back(e);
            }
            return &heavy;
        }
    return nullptr;
}
}  // namespace

int main(int argc, char** argv)
{
    benchmark::Initialize(&argc, argv);
    std::vector<SetSpec> specs;
    for (auto shape : {SetSpec::PhysRanges, SetSpec::SlotRanges})
        for (uint16_t len : {1, 256})
        {
            const std::string tag = std::string(shape == SetSpec::PhysRanges ? "phys" : "slot") + std::to_string(len);
            for (const SetSpec& s : ScaleSpecs(shape, tag.c_str(), len, {1, 10, 100, 1000}))
                specs.push_back(s);
        }
    std::vector<const Trace*> traces = AllTraces();
    if (const Trace* heavy = RemapHeavyTrace())
        traces.push_back(heavy);
    RegisterFloor(traces);
    RegisterMatcher<HashKey>("hashkey", specs, 300000, traces);
    RegisterMatcher<PageMap>("pagemap", specs, 300000, traces);
    RegisterMatcher<Slices>("slices", specs, 300000, traces);
    RegisterMatcher<Merged>("merged", specs, 300000, traces);
    RegisterMatcher<BitSlices>("bitslices", specs, 300000, traces);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
