// Experiment 01: today's BreakpointManager hot path (master 7eca03436), replicated outside the core so it runs
// on the same traces and sets as the candidates.
//
//   HandlePCChange / HandleMemoryRead / HandleMemoryWrite:
//     hasX flag -> addressFlags[addr] bit -> MapZ80AddressToPhysicalPage -> unordered_map find(page key),
//     then find(wildcard key) -> active && type bit
//   HandlePortIn / HandlePortOut: hasX flag -> unordered_map find(port)   (no address filter)
//
// Today there are no ranges, physical (any-slot) breakpoints or port masks: a range is what a user can do
// now, one breakpoint per address, so the range sets are expanded into points here.

#include <unordered_map>

#include "../common/harness.h"

using namespace poc;

namespace
{
constexpr uint8_t kFilterExec = 1, kFilterRead = 2, kFilterWrite = 4;

struct Descriptor
{
    uint16_t id;
    uint8_t watch;
    bool active = true;
};

class Baseline
{
public:
    Baseline(const std::vector<Breakpoint>& set, const PhysPage*)
    {
        for (const Breakpoint& b : set)
        {
            if (b.space == Space::Port)
            {
                _ports.emplace(b.start, Descriptor{b.id, b.watch});
                _hasIn |= (b.watch & WIn) != 0;
                _hasOut |= (b.watch & WOut) != 0;
                continue;
            }
            for (uint32_t a = b.start; a <= b.end; a++)
            {
                // Slot-bound: key = type, page, CPU address (today's BRK_MATCH_BANK_ADDR)
                const uint32_t key = b.space == Space::Z80Slot ? (static_cast<uint32_t>(b.page) << 16) | a
                                                               : 0xFFFF0000u | a;
                _byAddress.emplace(key, Descriptor{b.id, b.watch});
                if (b.watch & WExec) { _hasExec = 1; _flags[a] |= kFilterExec; }
                if (b.watch & WRead) { _hasRead = 1; _flags[a] |= kFilterRead; }
                if (b.watch & WWrite) { _hasWrite = 1; _flags[a] |= kFilterWrite; }
            }
        }
    }

    int Match(uint8_t kind, uint16_t addr, const PhysPage* slots)
    {
        switch (kind)
        {
            case KExec: return Memory(_hasExec, kFilterExec, WExec, addr, slots);
            case KFetch:
            case KRead: return Memory(_hasRead, kFilterRead, WRead, addr, slots);
            case KWrite: return Memory(_hasWrite, kFilterWrite, WWrite, addr, slots);
            case KIn: return Port(_hasIn, WIn, addr);
            case KOut: return Port(_hasOut, WOut, addr);
            default: return kNone;
        }
    }

    void Remap(int, PhysPage, const PhysPage*) {}

private:
    int Memory(uint8_t has, uint8_t filter, uint8_t watch, uint16_t addr, const PhysPage* slots)
    {
        if (!has)
            return kNone;
        if (!(_flags[addr] & filter))
            return kNone;
        // FindAddressBreakpoint: the page through the slot table, then the page key, then the wildcard
        const uint32_t fullKey = (static_cast<uint32_t>(slots[addr >> 14]) << 16) | addr;
        auto it = _byAddress.find(fullKey);
        if (it == _byAddress.end())
            it = _byAddress.find(0xFFFF0000u | addr);
        if (it != _byAddress.end() && it->second.active && (it->second.watch & watch))
            return it->second.id;
        return kNone;
    }

    int Port(bool has, uint8_t watch, uint16_t port)
    {
        if (!has)
            return kNone;
        auto it = _ports.find(port);
        if (it != _ports.end() && it->second.active && (it->second.watch & watch))
            return it->second.id;
        return kNone;
    }

    uint8_t _hasExec = 0, _hasRead = 0, _hasWrite = 0;
    bool _hasIn = false, _hasOut = false;
    uint8_t _flags[0x10000] = {};
    std::unordered_map<uint32_t, Descriptor> _byAddress;
    std::unordered_map<uint16_t, Descriptor> _ports;
};

/// Today's model cannot hold two breakpoints of one key (the manager returns the existing id) nor ranges of
/// different watches over one address: the reference sees them all. Keep the sets the baseline can represent
/// exactly: points (one per address) and ranges expanded where they do not overlap another breakpoint.
std::vector<Breakpoint> Representable(std::vector<Breakpoint> set)
{
    std::vector<uint8_t> used(0x10000, 0);
    std::vector<uint8_t> usedPort(0x10000, 0);
    std::vector<Breakpoint> out;
    for (const Breakpoint& b : set)
    {
        if (b.space == Space::Phys || (b.space == Space::Port && b.mask != 0xFFFF))
            continue;  // not expressible today
        if (b.space == Space::Port)
        {
            if (!usedPort[b.start])
                out.push_back(b);  // one breakpoint per port key, as the manager keeps it
            usedPort[b.start] = 1;
            continue;
        }
        bool clash = false;
        for (uint32_t a = b.start; a <= b.end && !clash; a++)
            clash = used[a] != 0;
        if (clash)
            continue;
        for (uint32_t a = b.start; a <= b.end; a++)
            used[a] = 1;
        out.push_back(b);
    }
    return out;
}

}  // namespace

int main(int argc, char** argv)
{
    benchmark::Initialize(&argc, argv);
    RegisterFloor();
    std::vector<SetSpec> specs;
    for (auto shape : {SetSpec::ExecPoints, SetSpec::ReadWritePoints})
        for (const SetSpec& s : ScaleSpecs(shape, shape == SetSpec::ExecPoints ? "exec" : "rw"))
            specs.push_back(s);
    // Ranges as points today: 16-byte and 256-byte ranges, up to 100 of them (25600 points)
    for (uint16_t len : {16, 256})
        for (const SetSpec& s : ScaleSpecs(SetSpec::Ranges, len == 16 ? "range16" : "range256", len, {1, 10, 100}))
            specs.push_back(s);
    for (const SetSpec& s : ScaleSpecs(SetSpec::Ports, "port", 0, {1, 10, 100}))
        specs.push_back(s);

    // The reference is told the same filtered set, so "correct" is judged on what the baseline holds
    for (const std::string& path : TraceFiles())
        for (const SetSpec& spec : specs)
        {
            const Trace& t = GetTrace(path);
            std::vector<Breakpoint> set = Representable(MakeSet(spec, GetProfile(t)));
            for (Breakpoint& b : set)
                if (b.space == Space::Port)
                    b.mask = 0xFFFF;
            Verify<Baseline>("baseline", t, set, spec.count <= 100 ? t.events.size() : 300000);
            const std::string name = "baseline/" + spec.name + "/" + t.name;
            benchmark::RegisterBenchmark(name.c_str(), [&t, set](benchmark::State& st) { Replay<Baseline>(st, t, set); })
                ->Unit(benchmark::kMillisecond)
                ->MinTime(0.3);
        }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
