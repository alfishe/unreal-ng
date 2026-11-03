/// The pointer / handler table detector: words in memory whose values the code used as addresses (L1's word
/// pointers: LD E,(HL) / INC HL / LD D,(HL), LD rr,(nn), then an access through the pair, a JP (rr) or a PUSH +
/// RET; a POP from a table SP walks), read through a register (an indexed table, not a variable; SP counts), two
/// or more entries a word apart. A table whose words were jumped to is a handler table. The table grows over the
/// neighbouring words that point at code (handler tables) or into the range the used entries pointed into (pointer
/// tables): those entries are inferred. A handler table also grows over the words whose value lies inside the hull
/// of the targets jumped to through its dispatch (the JP (rr) that used its words) and whose target is the start of an
/// executed instruction or a byte nothing touched (not an operand, not data): a handler that did not run in this
/// session. Two used entries of one dispatch further apart than kMaxGap are one
/// table when every word between them passes that test (a 2-D table whose rows the session took only in part).

#include <algorithm>
#include <cstdio>

#include "debugger/analyzers/analysis/detectors/detectors.h"

namespace analysis
{

namespace
{

constexpr uint32_t kMaxGap = 8;     ///< bytes between two used entries of one table
constexpr uint32_t kMaxGrow = 64;   ///< inferred entries added on either side, or between two used entries

/// The targets jumped to through one dispatch site (the JP (rr) that used the words): the lowest and the highest
struct Hull
{
    uint16_t site = 0;
    uint16_t lo = 0xFFFF;
    uint16_t hi = 0;
};

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
        if ((u.flags & (use::kIndexed | use::kStackRead)) == 0 || claimed[z80] != 0)
            continue;
        entries.push_back(Entry{&p, z80});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.z80 < b.z80; });

    // Per dispatch site, the hull of the targets jumped to through it
    std::vector<Hull> hulls;
    for (const Entry& e : entries)
    {
        if (!e.use->code)
            continue;
        auto it = std::find_if(hulls.begin(), hulls.end(), [&](const Hull& h) { return h.site == e.use->useZ80; });
        if (it == hulls.end())
            it = hulls.insert(hulls.end(), Hull{e.use->useZ80});
        it->lo = std::min(it->lo, e.use->minValue);
        it->hi = std::max(it->hi, e.use->maxValue);
    }
    const auto hullOf = [&](const Entry& e) -> const Hull* {
        if (!e.use->code)
            return nullptr;
        for (const Hull& h : hulls)
            if (h.site == e.use->useZ80)
                return &h;
        return nullptr;
    };
    const auto wordAt = [&](uint32_t at) {
        return static_cast<uint16_t>(context.Start(static_cast<uint16_t>(at)) |
                                     (context.Start(static_cast<uint16_t>(at + 1)) << 8));
    };
    // A free word of the table's page that is not code itself, whose value lies inside the hull and points at the
    // start of an executed instruction or at a byte nothing touched (not into an instruction, not at data)
    const auto inHull = [&](uint32_t at, const Hull& hull, uint32_t page) {
        if (at > 0xFFFE || !context.InScope(static_cast<uint16_t>(at)) ||
            !context.InScope(static_cast<uint16_t>(at + 1)) || claimed[at] != 0 || claimed[at + 1] != 0 ||
            (at >> 14) != page || ((at + 1) >> 14) != page)
            return false;
        if (DetectorContext::Code(context.Use(static_cast<uint16_t>(at))) ||
            DetectorContext::Code(context.Use(static_cast<uint16_t>(at + 1))))
            return false;
        const uint16_t value = wordAt(at);
        if (value < hull.lo || value > hull.hi)
            return false;
        const ByteUse& target = context.Use(value);
        if (DetectorContext::Code(target))
            return context.out.CodeAt(context.Key(value)) != nullptr;
        return !DetectorContext::Touched(target);
    };

    size_t i = 0;
    while (i < entries.size())
    {
        const uint32_t page = entries[i].z80 >> 14;
        size_t j = i;
        uint32_t bridged = 0;  // words between two used entries of one dispatch more than kMaxGap apart
        while (j + 1 < entries.size())
        {
            const Entry& a = entries[j];
            const Entry& b = entries[j + 1];
            const uint32_t gap = static_cast<uint32_t>(b.z80 - a.z80);
            if ((b.z80 - entries[i].z80) % 2 != 0 || (b.z80 >> 14) != page)
                break;
            if (gap <= kMaxGap)
            {
                ++j;
                continue;
            }
            // Further apart: one table when both were jumped through one dispatch and every word between them
            // could be one of its entries
            const Hull* hull = hullOf(a);
            if (hull == nullptr || hullOf(b) != hull || gap / 2 - 1 > kMaxGrow)
                break;
            uint32_t at = a.z80 + 2u;
            while (at < b.z80 && inHull(at, *hull, page))
                at += 2;
            if (at < b.z80)
                break;
            bridged += gap / 2 - 1;
            ++j;
        }
        bool code = false;
        uint16_t minValue = 0xFFFF, maxValue = 0;
        uint64_t uses = 0;
        const Hull* hull = nullptr;
        bool oneSite = true;
        for (size_t k = i; k <= j; ++k)
        {
            code |= entries[k].use->code;
            minValue = std::min(minValue, entries[k].use->minValue);
            maxValue = std::max(maxValue, entries[k].use->maxValue);
            uses += entries[k].use->uses;
            if (const Hull* h = hullOf(entries[k]))
            {
                oneSite = oneSite && (hull == nullptr || hull == h);
                hull = h;
            }
        }
        if (!oneSite)
            hull = nullptr;  // two dispatches: no hull speaks for the table
        uint32_t lo = entries[i].z80;
        uint32_t hi = entries[j].z80 + 1u;
        const auto fits = [&](uint32_t at) {
            if (at > 0xFFFE || !context.InScope(static_cast<uint16_t>(at)) ||
                !context.InScope(static_cast<uint16_t>(at + 1)) || claimed[at] != 0 || claimed[at + 1] != 0 ||
                (at >> 14) != (lo >> 14) || ((at + 1) >> 14) != (lo >> 14))
                return false;
            const uint16_t value = wordAt(at);
            if (code)
                return context.out.CodeAt(context.Key(value)) != nullptr ||
                       (hull != nullptr && inHull(at, *hull, lo >> 14));
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
            r.status =
                grownBefore + grownAfter + bridged == 0 ? RegionStatus::Observed : RegionStatus::Inferred;
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
                                              code ? (hull != nullptr
                                                          ? "words pointing at code or inside the targets jumped to"
                                                          : "words pointing at code")
                                                   : "words pointing into the same range"});
            if (bridged != 0)
                r.evidence.push_back(Evidence{"between", entries[i].z80, bridged,
                                              "words between used entries, pointing inside the targets jumped to"});
            r.users = context.UsersOf(static_cast<uint16_t>(lo), static_cast<uint16_t>(hi));
            if (code)
                for (uint32_t at = lo; at + 1 <= hi; at += 2)
                {
                    const uint16_t value = wordAt(at);
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
