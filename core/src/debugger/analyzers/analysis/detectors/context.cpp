#include <algorithm>

#include "debugger/analyzers/analysis/detectors/detectors.h"

namespace analysis
{

DetectorContext::DetectorContext(const L1Result& l1In, L2Result& outIn, const DetectorOptions& optionsIn)
    : l1(l1In), out(outIn), options(optionsIn)
{
}

bool DetectorContext::Z80Of(PhysKey key, uint16_t& z80) const
{
    for (uint8_t bank = 0; bank < 4; ++bank)
        if (l1.startBanks[bank] == KeyPage(key))
        {
            z80 = static_cast<uint16_t>((bank << 14) | KeyOffset(key));
            return true;
        }
    return false;
}

uint8_t DetectorContext::Start(uint16_t z80) const
{
    uint8_t value = 0;
    l1.StartByte(Key(z80), value);
    return value;
}

uint8_t DetectorContext::End(uint16_t z80) const
{
    uint8_t value = 0;
    if (!l1.EndByte(Key(z80), value))
        l1.StartByte(Key(z80), value);
    return value;
}

const ByteUse& DetectorContext::Use(uint16_t z80) const
{
    const ByteUse* u = l1.UseAt(Key(z80));
    return u != nullptr ? *u : _none;
}

uint32_t DetectorContext::RoutineEntry(PhysKey instruction) const
{
    if (instruction == kNoKey)
        return 0xFFFFFFFFu;
    const Routine* r = l1.RoutineOf(instruction);
    return r != nullptr ? r->z80 : 0xFFFFFFFFu;
}

bool DetectorContext::Claim(Region region, uint16_t lo, uint16_t hi)
{
    if (hi < lo || !InScope(lo) || !InScope(hi) || (lo >> 14) != (hi >> 14))
        return false;
    region.lo = Key(lo);
    region.hi = Key(hi);
    region.z80 = lo;
    return out.store.Add(std::move(region));
}

std::vector<uint8_t> DetectorContext::ClaimedMap() const
{
    std::vector<uint8_t> map(0x10000, 0);
    for (const Region& r : out.store.Regions())
    {
        // The code map's fallback claims (plain data, untouched) do not stop later detectors
        if (r.kind == RegionKind::Untouched || (r.detector == DetectorId::CodeMap && r.kind != RegionKind::Code))
            continue;
        uint16_t z80 = 0;
        if (!Z80Of(r.lo, z80))
            continue;
        for (uint32_t i = 0; i < r.Size() && z80 + i <= 0xFFFF; ++i)
            map[z80 + i] = 1;
    }
    return map;
}

std::vector<uint16_t> DetectorContext::UsersOf(uint16_t lo, uint16_t hi) const
{
    std::vector<uint16_t> users;
    for (uint32_t a = lo; a <= hi; ++a)
    {
        const ByteUse& u = Use(static_cast<uint16_t>(a));
        for (const PhysKey k : {u.firstReader, u.lastReader, u.firstWriter, u.lastWriter})
        {
            const uint32_t entry = RoutineEntry(k);
            if (entry != 0xFFFFFFFFu && std::find(users.begin(), users.end(), entry) == users.end())
                users.push_back(static_cast<uint16_t>(entry));
        }
        if (users.size() >= 16)
            break;
    }
    std::sort(users.begin(), users.end());
    return users;
}

L2Result RunDetectors(const L1Result& l1, const DetectorOptions& options)
{
    L2Result out;
    DetectorContext context(l1, out, options);
    DetectSignatures(context);
    DetectCodeMap(context);
    DetectText(context);
    DetectInitTables(context);
    // Word pointers before records: a table whose words the code used as addresses is a pointer table even where
    // its walk also looks like an array of 2-byte records
    DetectPointerTables(context);
    DetectRecords(context);
    DetectVariables(context);
    FinishBufferFields(context);
    DetectGraphics(context);
    DetectLevelMaps(context);
    DetectScreens(context);
    DetectRng(context);
    DetectUnpackers(context);
    DetectMusic(context);
    out.store.Finish();
    // One name per entry: the first detector's non-empty one (signatures before roles); empty: the export's sub_XXXX
    std::stable_sort(out.entries.begin(), out.entries.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::pair<uint16_t, std::string>> merged;
    for (const auto& e : out.entries)
    {
        if (!merged.empty() && merged.back().first == e.first)
        {
            if (merged.back().second.empty())
                merged.back().second = e.second;
            continue;
        }
        merged.push_back(e);
    }
    out.entries = std::move(merged);
    return out;
}

}  // namespace analysis
