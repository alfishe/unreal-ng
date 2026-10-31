/// The unpacker detector: a routine that wrote N bytes from M < N bytes it read (N >= 5/4 M), reading each input
/// byte about once per run (90 % of them: at most once per run, plus two reads by anything else, a copy or a
/// checksum), with at least two back references: block copies it ran from output it had written to further output
/// (an LZ match; a fill copies one byte along and has no input of its own). Its output must not be the display and
/// must be used afterwards: executed, or read by another routine. The input is claimed packed; the routine is named
/// UNPACK, the first routine entry in the output UNPACKED.

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "debugger/analyzers/analysis/detectors/media.h"

namespace analysis
{

namespace
{

constexpr uint32_t kMaxRoutine = 256;
constexpr uint32_t kMinOutput = 32;
constexpr uint32_t kMinInput = 8;

std::string Hex(uint16_t v)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", v);
    return text;
}

}  // namespace

void DetectUnpackers(DetectorContext& context)
{
    const L1Result& l1 = context.l1;
    const media::DisplayInfo display = media::FindDisplay(context);
    int named = 0;
    for (const Routine& routine : l1.routines)
    {
        if (routine.instructions > kMaxRoutine || routine.profile.entries == 0)
            continue;
        std::vector<uint8_t> output(0x10000, 0);
        uint32_t outputBytes = 0, displayBytes = 0;
        uint16_t outputLo = 0xFFFF;
        for (const auto& [lo, hi] : media::Runs(context, routine.profile.writes, 0))
            for (uint32_t a = lo; a <= hi; ++a)
            {
                output[a] = 1;
                ++outputBytes;
                displayBytes += display.At(a) != media::kNotDisplay ? 1 : 0;
                outputLo = std::min<uint16_t>(outputLo, static_cast<uint16_t>(a));
            }
        if (outputBytes < kMinOutput || displayBytes * 10 >= outputBytes)
            continue;
        std::vector<std::pair<uint16_t, uint16_t>> input;
        uint32_t inputBytes = 0, once = 0;
        for (const auto& [lo, hi] : media::Runs(context, routine.profile.reads, 0))
        {
            uint32_t a = lo;
            while (a <= hi)
            {
                if (output[a])
                {
                    ++a;
                    continue;
                }
                uint32_t b = a;
                while (b + 1 <= hi && !output[b + 1])
                    ++b;
                input.emplace_back(static_cast<uint16_t>(a), static_cast<uint16_t>(b));
                for (uint32_t k = a; k <= b; ++k)
                {
                    ++inputBytes;
                    once += context.Use(static_cast<uint16_t>(k)).reads <= routine.profile.entries + 2 ? 1 : 0;
                }
                a = b + 1;
            }
        }
        if (inputBytes < kMinInput || outputBytes * 4 < inputBytes * 5 || once * 10 < inputBytes * 9)
            continue;
        // Back references: copies by this routine from its output to its output, forwards
        std::set<uint16_t> backDst;
        for (const BlockCopy& c : l1.copies)
        {
            const Routine* owner = l1.RoutineOf(c.instruction);
            if (owner != &routine || c.length == 0)
                continue;
            const uint16_t src = c.dir > 0 ? c.src : static_cast<uint16_t>(c.src - (c.length - 1));
            const uint16_t dst = c.dir > 0 ? c.dst : static_cast<uint16_t>(c.dst - (c.length - 1));
            if (src >= dst || static_cast<uint32_t>(dst) + c.length - 1 > 0xFFFF)
                continue;
            bool inside = true;
            for (uint32_t k = 0; k < c.length && inside; ++k)
                inside = output[src + k] && output[dst + k];
            if (inside)
                backDst.insert(dst);
        }
        if (backDst.size() < 2)
            continue;
        // Used afterwards: executed, or read by another routine
        bool executed = false, readElsewhere = false;
        uint16_t firstEntry = 0;
        bool hasEntry = false;
        for (uint32_t a = 0; a <= 0xFFFF; ++a)
        {
            if (!output[a])
                continue;
            const ByteUse& u = context.Use(static_cast<uint16_t>(a));
            executed = executed || (u.flags & use::kOpcode) != 0;
            for (const PhysKey k : {u.firstReader, u.lastReader})
            {
                const Routine* r = l1.RoutineOf(k);
                readElsewhere = readElsewhere || (r != nullptr && r != &routine);
            }
            if (!hasEntry)
            {
                const Routine* r = l1.RoutineOf(context.Key(static_cast<uint16_t>(a)));
                if (r != nullptr && r->z80 == a)
                {
                    firstEntry = static_cast<uint16_t>(a);
                    hasEntry = true;
                }
            }
        }
        if (!executed && !readElsewhere)
            continue;
        for (const auto& [lo, hi] : input)
        {
            if (hi - lo + 1u < 4)
                continue;
            Region r;
            r.kind = RegionKind::Packed;
            r.detector = DetectorId::Unpackers;
            r.status = RegionStatus::Observed;
            r.geometry.count = static_cast<uint16_t>(std::min<uint32_t>(outputBytes, 0xFFFF));
            r.confidence = executed ? 850 : 750;
            r.title = "Packed data, unpacked by " + Hex(routine.z80) + " to " + Hex(outputLo) + " (" +
                      std::to_string(outputBytes) + " bytes from " + std::to_string(inputBytes) + ")";
            r.evidence.push_back(Evidence{"unpacker", routine.z80, routine.profile.entries,
                                          std::to_string(outputBytes) + " bytes written from " +
                                              std::to_string(inputBytes) + " read, each read once"});
            r.evidence.push_back(Evidence{"back references", *backDst.begin(), backDst.size(),
                                          "copies from the output to further output"});
            r.evidence.push_back(Evidence{executed ? "executed" : "read", outputLo, 0,
                                          executed ? "the output ran as code" : "another routine read the output"});
            r.users = context.UsersOf(lo, hi);
            context.Claim(std::move(r), lo, hi);
        }
        ++named;
        context.out.entries.emplace_back(routine.z80, named == 1 ? "UNPACK" : "UNPACK_" + Hex(routine.z80).substr(1));
        if (executed && hasEntry)
            context.out.entries.emplace_back(firstEntry,
                                             named == 1 ? "UNPACKED" : "UNPACKED_" + Hex(firstEntry).substr(1));
    }
}

}  // namespace analysis
