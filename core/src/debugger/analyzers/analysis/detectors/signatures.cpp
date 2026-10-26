/// The signature detector: masked byte patterns from the catalog (signatures/), matched against the memory the
/// replay started from, with the catalog's check at the match. A match claims its bytes as the signature's kind and
/// names them; a code signature also names the routine entry there.

#include <string>

#include "debugger/analyzers/analysis/detectors/detectors.h"
#include "debugger/search/memorysearch.h"

namespace analysis
{

namespace
{

bool EnteredAt(const L1Result& l1, PhysKey key)
{
    for (const Routine& r : l1.routines)
        if (r.entry == key)
            return true;
    return false;
}

}  // namespace

void DetectSignatures(DetectorContext& context)
{
    const std::vector<Signature>& catalog =
        context.options.catalog != nullptr ? *context.options.catalog : signatures::Builtin();
    for (const Signature& sig : catalog)
    {
        std::vector<uint8_t> pattern, mask;
        std::string error;
        if (!MemorySearch::ParsePattern(sig.pattern, pattern, mask, error))
            continue;
        const uint16_t size = sig.size != 0 ? sig.size : static_cast<uint16_t>(pattern.size());

        std::vector<uint16_t> matches;
        if (sig.check == SignatureCheck::AtAddress)
        {
            std::vector<uint8_t> here(pattern.size());
            for (size_t i = 0; i < pattern.size(); ++i)
                here[i] = context.Start(static_cast<uint16_t>(sig.address + i));
            if (!MemorySearch::FindInBuffer(here.data(), here.size(), pattern, mask, 1).empty())
                matches.push_back(sig.address);
        }
        else
        {
            for (uint8_t bank = 0; bank < 4; ++bank)
            {
                std::vector<uint8_t> image(0x4000);
                for (uint32_t i = 0; i < 0x4000; ++i)
                    image[i] = context.Start(static_cast<uint16_t>((bank << 14) | i));
                for (const uint32_t at : MemorySearch::FindInBuffer(image.data(), image.size(), pattern, mask, 16))
                {
                    const auto z80 = static_cast<uint16_t>((bank << 14) | at);
                    if (sig.check == SignatureCheck::Entry && !EnteredAt(context.l1, context.Key(z80)))
                        continue;
                    matches.push_back(z80);
                }
            }
        }

        for (const uint16_t at : matches)
        {
            if (static_cast<uint32_t>(at) + size - 1 > 0xFFFF)
                continue;
            Region r;
            r.kind = sig.kind;
            r.detector = DetectorId::Signatures;
            r.status = RegionStatus::Observed;
            r.label = sig.label;
            r.title = sig.title;
            r.confidence = sig.check == SignatureCheck::Anywhere ? 800 : 950;
            r.evidence.push_back(Evidence{"pattern", at, 0, sig.id});
            const uint16_t hi = static_cast<uint16_t>(at + size - 1);
            r.users = context.UsersOf(at, hi);
            // One claim per 16 K bank: a signature over a bank boundary is split
            uint16_t lo = at;
            while (true)
            {
                const uint16_t bankEnd = static_cast<uint16_t>(lo | 0x3FFF);
                const uint16_t end = hi < bankEnd ? hi : bankEnd;
                context.Claim(r, lo, end);
                if (end == hi)
                    break;
                lo = static_cast<uint16_t>(end + 1);
            }
            if (sig.kind == RegionKind::Code && context.InScope(at))
                context.out.entries.emplace_back(at, sig.label);
        }
    }
}

}  // namespace analysis
