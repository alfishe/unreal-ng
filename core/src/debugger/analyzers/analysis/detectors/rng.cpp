/// The RNG detector: a variable (the variables detector's byte or word) that one short routine both reads and
/// writes, through anything but INC / DEC, with at least three distinct values, where that routine reads the
/// refresh register (LD A,R) or mixes the value with a shift or rotate and an XOR (xorshift, LFSR steps). A counter
/// (INC / DEC only) or a value another routine computes is not one. The routine is named RANDOM.

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>

#include "debugger/analyzers/analysis/detectors/detectors.h"

namespace analysis
{

namespace
{

constexpr uint32_t kMaxRoutine = 64;

std::string Hex(uint16_t v)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", v);
    return text;
}

}  // namespace

void DetectRng(DetectorContext& context)
{
    const L1Result& l1 = context.l1;
    std::set<uint32_t> named;
    struct Found
    {
        Region region;
        uint16_t lo = 0;
        uint16_t hi = 0;
    };
    std::vector<Found> found;
    for (const Region& v : context.out.store.Regions())
    {
        if (v.kind != RegionKind::Variable || v.detector != DetectorId::Variables || v.Size() > 2)
            continue;
        const Routine* routine = nullptr;
        bool sameRoutine = true, mixed = false;
        uint8_t distinct = 0;
        for (uint32_t i = 0; i < v.Size(); ++i)
        {
            const ByteUse& u = context.Use(static_cast<uint16_t>(v.z80 + i));
            if ((u.flags & use::kRead) == 0 || (u.flags & use::kWrite) == 0)
            {
                sameRoutine = false;
                break;
            }
            for (const PhysKey k : {u.firstReader, u.lastReader, u.firstWriter, u.lastWriter})
            {
                const Routine* r = l1.RoutineOf(k);
                if (r == nullptr || (routine != nullptr && r != routine))
                    sameRoutine = false;
                routine = r;
            }
            mixed = mixed || (u.flags & use::kOtherWrite) != 0;
            distinct = std::max(distinct, u.distinct);
        }
        if (!sameRoutine || routine == nullptr || !mixed || distinct < 3 || routine->instructions > kMaxRoutine)
            continue;
        const uint32_t index = l1.IndexOf(*routine);
        bool refresh = false, xorOp = false, shift = false;
        for (size_t i = 0; i < l1.instructions.size() && i < l1.instructionRoutine.size(); ++i)
        {
            if (l1.instructionRoutine[i] != index)
                continue;
            const Instruction& ins = l1.instructions[i];
            const OpInfo& op = DecodeOp(ins.bytes);
            refresh = refresh || (ins.bytes[0] == 0xED && ins.bytes[1] == 0x5F);
            xorOp = xorOp || (op.aluOp == 5 && ins.bytes[0] != 0xAF);  // XOR A only clears A
            shift = shift || op.cls == OpClass::Rotate;
        }
        if (!refresh && !(xorOp && shift))
            continue;
        Region r;
        r.kind = RegionKind::RngState;
        r.detector = DetectorId::Rng;
        r.status = RegionStatus::Observed;
        r.geometry.role = v.geometry.role;
        r.geometry.minValue = v.geometry.minValue;
        r.geometry.maxValue = v.geometry.maxValue;
        r.confidence = refresh ? 850 : 800;
        r.title = "Random number state, updated by " + Hex(routine->z80) +
                  (refresh ? " (the refresh register)" : " (shift and XOR)");
        r.evidence.push_back(Evidence{"updater", routine->z80, routine->profile.entries,
                                      refresh ? "reads R and writes the state back" : "shifts, XORs and writes back"});
        r.evidence.push_back(Evidence{"values", v.z80, distinct, "distinct values written (3 = three or more)"});
        r.users = v.users;
        found.push_back(Found{std::move(r), v.z80, v.Z80End()});
        if (named.insert(routine->z80).second)
            context.out.entries.emplace_back(routine->z80, named.size() == 1 ? "RANDOM" : "RANDOM_" + Hex(routine->z80).substr(1));
    }
    // Claimed once the store is not walked any more
    for (Found& f : found)
        context.Claim(std::move(f.region), f.lo, f.hi);
}

}  // namespace analysis
