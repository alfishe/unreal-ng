// Experiment 06: whole matchers - every breakpoint kind at once (code points, watchpoints, CPU-address ranges,
// physical ranges, slot-bound ranges, masked ports), on mixed sets the way a debugging session builds them,
// N = 0 to 10000, on the recorded traces and the remap-heavy one. The candidates combine the winners of 02-05.
//
//   threeloads   CPU-address ids painted per kind (64K x uint16), then the slot's page slice, then the slot's
//                slot-bound slice (pointers moved on remap); ports painted. Up to three loads on a miss
//   globalbits   a CPU-address bit filter per kind (8 KB, L1) that over-approximates: a physical breakpoint
//                marks its offsets in all four slots. Only on a set bit the three loads of threeloads
//   slotbits     per slot and kind a combined 2 KB bit slice = CPU-address bits of that slot | the page's
//                bits | the page's slot-bound bits, recombined on remap (3 x 256 words OR) and exact: one L1
//                bit test per access; the ids (three loads) only on a hit
//   threebits    no per-slot state to rebuild: the three bits (CPU address, the slot's page, slot-bound) read
//                and ORed before one branch, all L1-sized; a remap moves pointers only
//
// The ports part is the same in all three (05's winner, bit filter + painted ids).

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

template <typename F>
void ForEachPort(const Breakpoint& b, F&& f)
{
    const uint16_t want = b.start & b.mask;
    const uint16_t free = static_cast<uint16_t>(~b.mask);
    uint16_t sub = 0;
    do
    {
        f(static_cast<uint16_t>(want | sub));
        sub = static_cast<uint16_t>((sub - free) & free);
    } while (sub != 0);
}

/// Everything painted at set time; the matchers below differ in how the hot path reaches it
struct Store
{
    // CPU-address space (Z80): ids and bits per kind
    uint16_t z80Id[3][0x10000] = {};
    uint64_t z80Bits[3][1024] = {};
    // Physical and slot-bound slices (only pages / (page, slot) pairs that have breakpoints)
    std::unordered_map<PhysPage, std::unique_ptr<Slice>> phys[3];
    std::unordered_map<PhysPage, std::unique_ptr<BitSlice>> physBits[3];
    std::unordered_map<uint32_t, std::unique_ptr<Slice>> bound[3];
    std::unordered_map<uint32_t, std::unique_ptr<BitSlice>> boundBits[3];
    // The over-approximating CPU-address filter of globalbits (Z80 bits + every slot of every phys offset)
    uint64_t globalBits[3][1024] = {};
    // Ports
    uint16_t portId[2][0x10000] = {};
    uint64_t portBits[2][1024] = {};
    // "Any breakpoint of this kind?" - the gate every access passes first (BreakpointHotState::hasX)
    uint8_t has[3] = {};
    uint8_t hasPort[2] = {};
    // The slices again, as flat arrays by page (0x300 page ids) and by (page, slot): a remap indexes them
    // instead of hashing
    const Slice* physFlat[3][0x300] = {};
    const BitSlice* physBitsFlat[3][0x300] = {};
    const Slice* boundFlat[3][0x300 * 4] = {};
    const BitSlice* boundBitsFlat[3][0x300 * 4] = {};

    explicit Store(const std::vector<Breakpoint>& set)
    {
        for (const Breakpoint& b : set)
        {
            if (b.space == Space::Port)
            {
                for (int d = 0; d < 2; d++)
                    if (b.watch & (d == 0 ? WIn : WOut))
                    {
                        hasPort[d] = 1;
                        ForEachPort(b, [&](uint16_t p) {
                            portBits[d][p >> 6] |= 1ull << (p & 63);
                            if (!portId[d][p])
                                portId[d][p] = b.id;
                        });
                    }
                continue;
            }
            for (int k = 0; k < 3; k++)
            {
                if (!(b.watch & KindWatch(k)))
                    continue;
                has[k] = 1;
                if (b.space == Space::Z80)
                {
                    for (uint32_t a = b.start; a <= b.end; a++)
                    {
                        Set(z80Bits[k], static_cast<uint16_t>(a));
                        Set(globalBits[k], static_cast<uint16_t>(a));
                        if (!z80Id[k][a])
                            z80Id[k][a] = b.id;
                    }
                    continue;
                }
                const bool isPhys = b.space == Space::Phys;
                const uint32_t key = isPhys ? b.page : (static_cast<uint32_t>(b.page) << 2) | (b.start >> 14);
                auto& slice = isPhys ? phys[k][b.page] : bound[k][key];
                auto& bits = isPhys ? physBits[k][b.page] : boundBits[k][key];
                if (!slice)
                {
                    slice = std::make_unique<Slice>();
                    slice->fill(0);
                    bits = std::make_unique<BitSlice>();
                    bits->fill(0);
                }
                for (uint32_t a = b.start; a <= b.end; a++)
                {
                    const uint16_t offset = static_cast<uint16_t>(a & 0x3FFF);
                    if (!(*slice)[offset])
                        (*slice)[offset] = b.id;
                    (*bits)[offset >> 6] |= 1ull << (offset & 63);
                    if (isPhys)
                        for (uint32_t s = 0; s < 4; s++)
                            Set(globalBits[k], static_cast<uint16_t>((s << 14) | offset));
                    else
                        Set(globalBits[k], static_cast<uint16_t>(a));
                }
            }
        }
        for (int k = 0; k < 3; k++)
        {
            for (auto& [page, slice] : phys[k])
                physFlat[k][page] = slice.get();
            for (auto& [page, bits] : physBits[k])
                physBitsFlat[k][page] = bits.get();
            for (auto& [key, slice] : bound[k])
                boundFlat[k][key] = slice.get();
            for (auto& [key, bits] : boundBits[k])
                boundBitsFlat[k][key] = bits.get();
        }
    }

    static void Set(uint64_t* bits, uint16_t a) { bits[a >> 6] |= 1ull << (a & 63); }

    template <typename Map, typename Key>
    static auto Find(const Map& m, Key key) -> decltype(m.begin()->second.get())
    {
        auto it = m.find(key);
        return it == m.end() ? nullptr : it->second.get();
    }

    int Port(uint8_t kind, uint16_t port) const
    {
        const int d = kind == KIn ? 0 : 1;
        if (!hasPort[d] || !((portBits[d][port >> 6] >> (port & 63)) & 1))
            return kNone;
        return portId[d][port];
    }
};

/// The per-slot pointers every candidate keeps (moved on remap)
struct SlotPointers
{
    const Slice* phys[3][4];
    const Slice* bound[3][4];
    const BitSlice* physBits[3][4];
    const BitSlice* boundBits[3][4];

    void Point(const Store& st, int slot, PhysPage page)
    {
        const uint32_t boundKey = (static_cast<uint32_t>(page) << 2) | static_cast<uint32_t>(slot);
        for (int k = 0; k < 3; k++)
        {
            const Slice* p = st.physFlat[k][page];
            const Slice* b = st.boundFlat[k][boundKey];
            const BitSlice* pb = st.physBitsFlat[k][page];
            const BitSlice* bb = st.boundBitsFlat[k][boundKey];
            phys[k][slot] = p ? p : &EmptySlice();
            bound[k][slot] = b ? b : &EmptySlice();
            physBits[k][slot] = pb ? pb : &EmptyBits();
            boundBits[k][slot] = bb ? bb : &EmptyBits();
        }
    }

    /// The three-load resolve: CPU-address id, the page's id, the slot-bound id
    int Resolve(const Store& st, int k, uint16_t addr) const
    {
        if (const uint16_t id = st.z80Id[k][addr])
            return id;
        const int s = addr >> 14;
        const uint16_t offset = addr & 0x3FFF;
        if (const uint16_t id = (*phys[k][s])[offset])
            return id;
        const uint16_t idb = (*bound[k][s])[offset];
        return idb ? idb : kNone;
    }
};

class ThreeLoads
{
public:
    ThreeLoads(const std::vector<Breakpoint>& set, const PhysPage* slots) : _st(set)
    {
        for (int s = 0; s < 4; s++)
            _p.Point(_st, s, slots[s]);
    }
    int Match(uint8_t kind, uint16_t addr, const PhysPage*)
    {
        if (kind == KIn || kind == KOut)
            return _st.Port(kind, addr);
        const int k = KindIndex(kind);
        return (k < 0 || !_st.has[k]) ? kNone : _p.Resolve(_st, k, addr);
    }
    void Remap(int slot, PhysPage page, const PhysPage*) { _p.Point(_st, slot, page); }

private:
    Store _st;
    SlotPointers _p;
};

class GlobalBits
{
public:
    GlobalBits(const std::vector<Breakpoint>& set, const PhysPage* slots) : _st(set)
    {
        for (int s = 0; s < 4; s++)
            _p.Point(_st, s, slots[s]);
    }
    int Match(uint8_t kind, uint16_t addr, const PhysPage*)
    {
        if (kind == KIn || kind == KOut)
            return _st.Port(kind, addr);
        const int k = KindIndex(kind);
        if (k < 0 || !_st.has[k] || !((_st.globalBits[k][addr >> 6] >> (addr & 63)) & 1))
            return kNone;
        return _p.Resolve(_st, k, addr);
    }
    void Remap(int slot, PhysPage page, const PhysPage*) { _p.Point(_st, slot, page); }

private:
    Store _st;
    SlotPointers _p;
};

class SlotBits
{
public:
    SlotBits(const std::vector<Breakpoint>& set, const PhysPage* slots) : _st(set)
    {
        for (int s = 0; s < 4; s++)
            Remap(s, slots[s], slots);
    }
    int Match(uint8_t kind, uint16_t addr, const PhysPage*)
    {
        if (kind == KIn || kind == KOut)
            return _st.Port(kind, addr);
        const int k = KindIndex(kind);
        if (k < 0 || !_st.has[k] || !((_bits[k][addr >> 6] >> (addr & 63)) & 1))
            return kNone;
        return _p.Resolve(_st, k, addr);
    }
    void Remap(int slot, PhysPage page, const PhysPage*)
    {
        _p.Point(_st, slot, page);
        // The slot's quarter of the combined filter: 256 words per kind; a page without slices copies the
        // CPU-address bits only, and a kind without breakpoints is skipped
        for (int k = 0; k < 3; k++)
        {
            if (!_st.has[k])
                continue;
            const uint64_t* z = &_st.z80Bits[k][slot * 256];
            uint64_t* out = &_bits[k][slot * 256];
            const BitSlice* pb = _p.physBits[k][slot];
            const BitSlice* bb = _p.boundBits[k][slot];
            if (pb == &EmptyBits() && bb == &EmptyBits())
            {
                std::copy(z, z + 256, out);
                continue;
            }
            for (int i = 0; i < 256; i++)
                out[i] = z[i] | (*pb)[i] | (*bb)[i];
        }
    }

private:
    Store _st;
    SlotPointers _p;
    uint64_t _bits[3][1024] = {};  // 8 KB per kind, exact for the current mapping
};

/// No per-slot filter state to rebuild: three bit tests ORed before the branch - the CPU-address bit, the
/// bit of the page the slot shows, the slot-bound bit - all L1-sized; a remap only moves pointers
class ThreeBits
{
public:
    ThreeBits(const std::vector<Breakpoint>& set, const PhysPage* slots) : _st(set)
    {
        for (int s = 0; s < 4; s++)
            _p.Point(_st, s, slots[s]);
    }
    int Match(uint8_t kind, uint16_t addr, const PhysPage*)
    {
        if (kind == KIn || kind == KOut)
            return _st.Port(kind, addr);
        const int k = KindIndex(kind);
        if (k < 0 || !_st.has[k])
            return kNone;
        const int s = addr >> 14;
        const uint16_t offset = addr & 0x3FFF;
        const uint64_t any = (_st.z80Bits[k][addr >> 6] | (*_p.physBits[k][s])[offset >> 6] |
                              (*_p.boundBits[k][s])[offset >> 6]) >> (addr & 63);
        if (!(any & 1))
            return kNone;
        return _p.Resolve(_st, k, addr);
    }
    void Remap(int slot, PhysPage page, const PhysPage*) { _p.Point(_st, slot, page); }

private:
    Store _st;
    SlotPointers _p;
};

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
                    continue;
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
    std::vector<const Trace*> traces = AllTraces();
    if (const Trace* heavy = RemapHeavyTrace())
        traces.push_back(heavy);
    RegisterFloor(traces);
    std::vector<SetSpec> specs = ScaleSpecs(SetSpec::Mixed, "mixed", 64);
    RegisterMatcher<ThreeLoads>("threeloads", specs, 300000, traces);
    RegisterMatcher<GlobalBits>("globalbits", specs, 300000, traces);
    RegisterMatcher<SlotBits>("slotbits", specs, 300000, traces);
    RegisterMatcher<ThreeBits>("threebits", specs, 300000, traces);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
