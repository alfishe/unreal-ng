/// Save areas (part of the records detector's buffers, run before the init tables): the bytes block copies (LDIR,
/// LDI chains) save from one place to put them back there later. A copy from X to Y whose bytes a copy by another
/// instruction takes from Y back to X, with the same length, is a save of X's bytes in Y (the strips under a sprite
/// saved before it is drawn, then restored; a window's background). The slots of one saving instruction and one
/// restoring one, packed back to back (a pointer moving through the area) or one slot used again and again, are one
/// save area: a Buffer claim over the slots seen, titled with what it saves. Its bytes are written before every read;
/// the parts the copies wrote least are no table a routine built (the init tables detector sees them claimed). A
/// saved byte is read only to put it back: an area some other instruction read (a score the game draws and compares,
/// saved into a table and restored from it) is what was saved, not the save. A saved byte is written by block copies
/// only too: each save X -> Y restored Y -> X is also a "save" Y -> X restored X -> Y, and X is the live data, which
/// code other than the copies writes (the sprite drawn over the background between the save and the restore).

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include "debugger/analyzers/analysis/detectors/detectors.h"

namespace analysis
{

namespace
{

constexpr uint64_t kMinRestores = 2;  ///< restores seen: one save put back once is no pattern

struct Slot
{
    uint32_t lo = 0;
    uint32_t hi = 0;  ///< inclusive
};

std::string Hex(uint32_t v)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", v & 0xFFFFu);
    return text;
}

}  // namespace

void DetectSaveAreas(DetectorContext& context)
{
    // The copies by their lowest source and destination bytes, and the runs of each
    std::map<std::tuple<uint16_t, uint16_t, uint32_t, uint16_t>, uint64_t> runs;  // src, dst, length, copier
    std::map<std::tuple<uint16_t, uint16_t, uint32_t>, std::set<uint16_t>> copiers;
    for (const BlockCopy& c : context.l1.copies)
    {
        if (c.length == 0 || c.length > 0x4000)
            continue;
        const auto src = static_cast<uint16_t>(c.dir > 0 ? c.src : c.src - (c.length - 1));
        const auto dst = static_cast<uint16_t>(c.dir > 0 ? c.dst : c.dst - (c.length - 1));
        runs[{src, dst, c.length, c.z80}] += c.count;
        copiers[{src, dst, c.length}].insert(c.z80);
    }

    // A save X -> Y put back by another instruction Y -> X
    struct Group
    {
        std::vector<Slot> slots;
        uint32_t fromLo = 0xFFFFFFFFu;
        uint32_t fromHi = 0;
        uint64_t saves = 0;
        uint64_t restores = 0;
    };
    std::map<std::pair<uint16_t, uint16_t>, Group> groups;  // saving, restoring instruction
    for (const auto& [key, n] : runs)
    {
        const auto& [src, dst, length, saver] = key;
        const auto back = copiers.find({dst, src, length});
        if (back == copiers.end() || src == dst)
            continue;
        for (const uint16_t restorer : back->second)
        {
            if (restorer == saver)
                continue;
            Group& g = groups[{saver, restorer}];
            g.slots.push_back(Slot{dst, static_cast<uint32_t>(dst) + length - 1});
            g.fromLo = std::min<uint32_t>(g.fromLo, src);
            g.fromHi = std::max<uint32_t>(g.fromHi, static_cast<uint32_t>(src) + length - 1);
            g.saves += n;
            g.restores += runs[{dst, src, length, restorer}];
        }
    }

    const std::vector<uint8_t> claimed = context.ClaimedMap();
    // The instructions at the readers' places, by key (LDI / LDD / LDIR / LDDR: a block copy, any LDI of a chain)
    std::map<PhysKey, uint16_t> places;
    const auto blockCopy = [&](PhysKey reader) {
        if (places.empty())
            for (uint32_t a = 0; a <= 0xFFFF; ++a)
                if (context.InScope(static_cast<uint16_t>(a)))
                    places.emplace(context.Key(static_cast<uint16_t>(a)), static_cast<uint16_t>(a));
        const auto it = places.find(reader);
        if (it == places.end() || it->second == 0xFFFF)
            return false;
        const uint8_t op = context.Start(static_cast<uint16_t>(it->second + 1));
        return context.Start(it->second) == 0xED && (op == 0xA0 || op == 0xA8 || op == 0xB0 || op == 0xB8);
    };
    // Read and written by block copies only. A write by anything but a block instruction sets kOtherWrite or
    // kIncDec, and every access through (nn) or a register pair sets kAbsolute or kIndexed (a block instruction sets
    // neither): with no such write, kAbsolute / kIndexed is a read by code other than a block instruction, wherever it
    // falls between the first and the last reader. The first and the last reader must be copies (LDI / LDD / LDIR /
    // LDDR, not CPI or OUTI); a CPI / OUTI read between them is not seen (a known limit: L1 keeps no bit for it)
    const auto byCopiesOnly = [&](const Slot& area) {
        for (uint32_t a = area.lo; a <= area.hi; ++a)
        {
            const ByteUse& u = context.Use(static_cast<uint16_t>(a));
            if ((u.flags & (use::kOtherWrite | use::kIncDec | use::kIndexed | use::kAbsolute)) != 0)
                return false;
            for (const PhysKey reader : {u.firstReader, u.lastReader})
                if (reader != kNoKey && !blockCopy(reader))
                    return false;
        }
        return true;
    };
    for (auto& [instructions, g] : groups)
    {
        if (g.restores < kMinRestores)
            continue;
        // The slots packed back to back (or overlapping) are one area
        std::sort(g.slots.begin(), g.slots.end(), [](const Slot& a, const Slot& b) { return a.lo < b.lo; });
        std::vector<Slot> areas;
        for (const Slot& s : g.slots)
            if (!areas.empty() && s.lo <= areas.back().hi + 1)
                areas.back().hi = std::max(areas.back().hi, s.hi);
            else
                areas.push_back(s);
        for (const Slot& area : areas)
        {
            if (area.hi > 0xFFFF || (area.lo >> 14) != (area.hi >> 14) ||
                !context.InScope(static_cast<uint16_t>(area.lo)))
                continue;
            // The save area overlaps what X is: a scroll, not a save
            if (area.lo <= g.fromHi && g.fromLo <= area.hi)
                continue;
            bool free = true;
            for (uint32_t a = area.lo; a <= area.hi && free; ++a)
                free = claimed[a] == 0;
            if (!free || !byCopiesOnly(area))
                continue;
            Region r;
            r.kind = RegionKind::Buffer;
            r.detector = DetectorId::Records;
            r.status = RegionStatus::Observed;
            r.title = "Save area of " + Hex(g.fromLo) + "-" + Hex(g.fromHi) + ", " +
                      std::to_string(area.hi - area.lo + 1) + " bytes: copied here by " + Hex(instructions.first) +
                      " and back by " + Hex(instructions.second);
            r.confidence = confidence::FromCount(700, 950, g.restores, 2);
            r.evidence.push_back(Evidence{"save", instructions.first, g.saves,
                                          "copies of " + Hex(g.fromLo) + "-" + Hex(g.fromHi) + " bytes into the area"});
            r.evidence.push_back(Evidence{"restore", instructions.second, g.restores,
                                          "copies of the same lengths back to the places they came from"});
            r.users = context.UsersOf(static_cast<uint16_t>(area.lo), static_cast<uint16_t>(area.hi));
            context.Claim(std::move(r), static_cast<uint16_t>(area.lo), static_cast<uint16_t>(area.hi));
        }
    }
}

}  // namespace analysis
