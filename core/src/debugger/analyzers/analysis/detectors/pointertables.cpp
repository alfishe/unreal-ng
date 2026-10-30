/// The pointer / handler table detector: words in memory whose values the code used as addresses (L1's word
/// pointers: LD E,(HL) / INC HL / LD D,(HL), LD rr,(nn), then an access through the pair, a JP (rr) or a PUSH +
/// RET), read through a register (an indexed table, not a variable), two or more entries a word apart. A table whose
/// words were jumped to is a handler table. The table grows over the neighbouring words that point at code (handler
/// tables) or into the range the used entries pointed into (pointer tables): those entries are inferred.

#include <algorithm>
#include <cstdio>

#include "debugger/analyzers/analysis/detectors/detectors.h"

namespace analysis
{

namespace
{

constexpr uint32_t kMaxGap = 8;     ///< bytes between two used entries of one table
constexpr uint32_t kMaxGrow = 64;   ///< inferred entries added on either side

}  // namespace

void DetectPointerTables(DetectorContext& context)
{
    const std::vector<uint8_t> claimed = context.ClaimedMap();
    struct Entry
    {
        const PointerUse* use;
        uint16_t z80;
    };
    std::vector<Entry> entries;
    for (const PointerUse& p : context.l1.pointers)
    {
        if (p.form != PointerForm::Word)
            continue;
        uint16_t z80 = 0;
        if (!context.Z80Of(p.origin, z80) || !context.InScope(z80))
            continue;
        const ByteUse& u = context.Use(z80);
        if ((u.flags & use::kIndexed) == 0 || claimed[z80] != 0)
            continue;
        entries.push_back(Entry{&p, z80});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.z80 < b.z80; });

    size_t i = 0;
    while (i < entries.size())
    {
        size_t j = i;
        while (j + 1 < entries.size() && uint32_t(entries[j + 1].z80 - entries[j].z80) <= kMaxGap &&
               (entries[j + 1].z80 - entries[i].z80) % 2 == 0 && (entries[j + 1].z80 >> 14) == (entries[i].z80 >> 14))
            ++j;
        bool code = false;
        uint16_t minValue = 0xFFFF, maxValue = 0;
        uint64_t uses = 0;
        for (size_t k = i; k <= j; ++k)
        {
            code |= entries[k].use->code;
            minValue = std::min(minValue, entries[k].use->minValue);
            maxValue = std::max(maxValue, entries[k].use->maxValue);
            uses += entries[k].use->uses;
        }
        uint32_t lo = entries[i].z80;
        uint32_t hi = entries[j].z80 + 1u;
        const auto fits = [&](uint32_t at) {
            if (at > 0xFFFE || !context.InScope(static_cast<uint16_t>(at)) ||
                !context.InScope(static_cast<uint16_t>(at + 1)) || claimed[at] != 0 || claimed[at + 1] != 0 ||
                (at >> 14) != (lo >> 14) || ((at + 1) >> 14) != (lo >> 14))
                return false;
            const auto value =
                static_cast<uint16_t>(context.Start(static_cast<uint16_t>(at)) |
                                      (context.Start(static_cast<uint16_t>(at + 1)) << 8));
            if (code)
                return context.out.CodeAt(context.Key(value)) != nullptr;
            const uint32_t span = std::max<uint32_t>(maxValue - minValue, 16);
            return value + span >= minValue && value <= maxValue + span;
        };
        uint32_t grownBefore = 0, grownAfter = 0;
        while (grownBefore < kMaxGrow && lo >= 2 && fits(lo - 2))
        {
            lo -= 2;
            ++grownBefore;
        }
        while (grownAfter < kMaxGrow && fits(hi + 1))
        {
            hi += 2;
            ++grownAfter;
        }
        const size_t used = j - i + 1;
        if (used >= 2 || grownBefore + grownAfter >= 1)
        {
            Region r;
            r.kind = code ? RegionKind::HandlerTable : RegionKind::PointerTable;
            r.detector = DetectorId::PointerTables;
            r.status = grownBefore + grownAfter == 0 ? RegionStatus::Observed : RegionStatus::Inferred;
            r.geometry.stride = 2;
            r.geometry.count = static_cast<uint16_t>((hi - lo + 1) / 2);
            r.geometry.width = r.geometry.count;
            r.confidence = confidence::FromCount(code ? 600 : 500, 950, used, 2);
            for (size_t k = i; k <= j; ++k)
                r.evidence.push_back(Evidence{"pointer", entries[k].z80, entries[k].use->uses,
                                              std::string(code ? "jumped to " : "addressed through, first at ") +
                                                  "$" + [&] {
                                                      char hex[8];
                                                      std::snprintf(hex, sizeof(hex), "%04X",
                                                                    entries[k].use->useZ80);
                                                      return std::string(hex);
                                                  }()});
            if (grownBefore + grownAfter != 0)
                r.evidence.push_back(Evidence{"neighbours", static_cast<uint16_t>(lo), grownBefore + grownAfter,
                                              code ? "words pointing at code" : "words pointing into the same range"});
            r.users = context.UsersOf(static_cast<uint16_t>(lo), static_cast<uint16_t>(hi));
            if (code)
                for (uint32_t at = lo; at + 1 <= hi; at += 2)
                {
                    const auto value =
                        static_cast<uint16_t>(context.Start(static_cast<uint16_t>(at)) |
                                              (context.Start(static_cast<uint16_t>(at + 1)) << 8));
                    if (context.out.CodeAt(context.Key(value)) != nullptr && context.InScope(value))
                        context.out.entries.emplace_back(value, std::string());
                }
            (void)uses;
            context.Claim(std::move(r), static_cast<uint16_t>(lo), static_cast<uint16_t>(hi));
        }
        i = j + 1;
    }
}

}  // namespace analysis
