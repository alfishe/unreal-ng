/// The variables detector: data bytes and words the code addressed directly ((nn), LD rr,(nn), a word used as a
/// pointer, an LD rr,nn address used unchanged) that no other claim explains (buffers aside: a buffer holds variables). The role comes from the use:
/// written only by INC / DEC is a counter, two values a flag, a word whose value addressed memory a pointer, read
/// or written as a pair a word, anything else a value. Each gets a label from its role and address.
///
/// Then two record claims over the variables (fields from the variables; the export writes the variables one by
/// one and lists these in the fields JSON and the buffer field lists):
///  - strided variables: four or more one-byte variables a constant step apart (2..64) are the first bytes of an
///    array of records of that step; the array takes the whole records after the last one when the unclaimed gap up
///    to the next claim is exactly one or two steps;
///  - variable groups: adjacent variables that share a routine reading or writing them, routines that touch more
///    than an eighth of the surroundings (the enclosing buffer, else 512 bytes) aside (an init copy, a sweep), are
///    one record of their total size.

#include <algorithm>
#include <cstdio>
#include <set>

#include "debugger/analyzers/analysis/detectors/detectors.h"

namespace analysis
{

namespace
{

bool Explained(const DetectorContext& context, uint16_t z80)
{
    for (const Region* r : context.out.store.ClaimsAt(context.Key(z80)))
        switch (r->kind)
        {
            case RegionKind::Data:
            case RegionKind::Untouched:
            case RegionKind::Buffer:
                break;
            default:
                if (r->detector != DetectorId::CodeMap || r->kind == RegionKind::Code)
                    return true;
        }
    return false;
}

const char* RolePrefix(VarRole role)
{
    switch (role)
    {
        case VarRole::Counter:
            return "ctr";
        case VarRole::Flag:
            return "flag";
        case VarRole::Pointer:
            return "ptr";
        case VarRole::Word:
            return "word";
        default:
            return "var";
    }
}

struct Var
{
    uint16_t lo = 0;
    uint16_t hi = 0;
    VarRole role = VarRole::None;
    uint8_t minValue = 0;
    uint8_t maxValue = 0;
};

constexpr size_t kMinStrided = 4;
constexpr uint16_t kMaxStep = 64;
constexpr uint32_t kWindow = 512;  ///< the surroundings of a variable outside a buffer

Field FieldOf(const Var& v, uint16_t base)
{
    Field f;
    f.offset = static_cast<uint16_t>(v.lo - base);
    f.size = static_cast<uint16_t>(v.hi - v.lo + 1);
    f.name = VarRoleName(v.role);
    f.minValue = v.minValue;
    f.maxValue = v.maxValue;
    f.word = f.size == 2;
    return f;
}

std::string Hex(uint16_t value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", value);
    return text;
}

/// Four or more one-byte variables a constant step apart: an array of records
void StridedVariables(DetectorContext& context, const std::vector<Var>& vars, std::vector<uint8_t>& inArray)
{
    // Claimed bytes, the buffers aside (a buffer holds records and variables)
    std::vector<uint8_t> claimed(0x10000, 0);
    for (const Region& r : context.out.store.Regions())
    {
        uint16_t z80 = 0;
        if (r.kind == RegionKind::Untouched || r.kind == RegionKind::Buffer ||
            (r.detector == DetectorId::CodeMap && r.kind != RegionKind::Code) || !context.Z80Of(r.lo, z80))
            continue;
        for (uint32_t k = 0; k < r.Size() && z80 + k <= 0xFFFF; ++k)
            claimed[z80 + k] = 1;
    }
    size_t i = 0;
    while (i + 1 < vars.size())
    {
        if (vars[i].lo != vars[i].hi)
        {
            ++i;
            continue;
        }
        const uint32_t step = static_cast<uint32_t>(vars[i + 1].lo) - vars[i].lo;
        size_t j = i;
        while (j + 1 < vars.size() && vars[j + 1].lo == vars[j + 1].hi &&
               static_cast<uint32_t>(vars[j + 1].lo) - vars[j].lo == step && (vars[j + 1].lo >> 14) == (vars[i].lo >> 14))
            ++j;
        if (step < 2 || step > kMaxStep || j - i + 1 < kMinStrided)
        {
            i = std::max(j, i + 1);
            continue;
        }
        // The records end at the last variable's record; whole unclaimed records up to the next claim join
        uint32_t end = vars[j].lo + step - 1;
        bool clear = end <= 0xFFFF;
        for (uint32_t a = vars[j].lo + 1; clear && a <= end; ++a)
            clear = context.InScope(static_cast<uint16_t>(a)) && claimed[a] == 0;
        if (!clear)
        {
            i = j + 1;
            continue;
        }
        uint32_t gap = 0;
        while (end + gap + 1 <= 0xFFFF && gap <= 2 * step && context.InScope(static_cast<uint16_t>(end + gap + 1)) &&
               claimed[end + gap + 1] == 0)
            ++gap;
        const bool bounded = end + gap + 1 > 0xFFFF || !context.InScope(static_cast<uint16_t>(end + gap + 1)) ||
                             claimed[end + gap + 1] != 0;
        uint32_t extra = 0;
        if (bounded && gap != 0 && gap % step == 0 && gap / step <= 2)
            extra = gap / step;
        end += extra * step;
        if ((end >> 14) != (vars[i].lo >> 14u))
        {
            i = j + 1;
            continue;
        }
        Region r;
        r.kind = RegionKind::RecordArray;
        r.detector = DetectorId::Variables;
        r.status = extra == 0 ? RegionStatus::Observed : RegionStatus::Inferred;
        r.geometry.stride = static_cast<uint16_t>(step);
        r.geometry.count = static_cast<uint16_t>(j - i + 1 + extra);
        r.geometry.fields.push_back(FieldOf(vars[i], vars[i].lo));
        r.geometry.fields.push_back(Field{1, static_cast<uint16_t>(step - 1), "bytes", 0, 0, false});
        r.confidence = confidence::FromCount(400, 800, j - i + 1, kMinStrided);
        r.evidence.push_back(Evidence{"step", vars[i].lo, j - i + 1,
                                      std::to_string(j - i + 1) + " variables " + std::to_string(step) +
                                          " bytes apart"});
        if (extra != 0)
            r.evidence.push_back(Evidence{"gap", static_cast<uint16_t>(vars[j].lo + step), extra,
                                          "unclaimed whole records up to the next claim"});
        r.users = context.UsersOf(vars[i].lo, static_cast<uint16_t>(end));
        const uint16_t lo = vars[i].lo;
        if (context.Claim(std::move(r), lo, static_cast<uint16_t>(end)))
            for (size_t k = i; k <= j; ++k)
                inArray[k] = 1;
        i = j + 1;
    }
}

/// Adjacent variables sharing a routine (broad routines aside): one record
void VariableGroups(DetectorContext& context, const std::vector<Var>& vars, const std::vector<uint8_t>& inArray)
{
    const L1Result& l1 = context.l1;
    // The routines touching each variable, from their read / write profiles
    const auto touches = [](const Routine& routine, PhysKey lo, PhysKey hi) {
        uint32_t bytes = 0;
        for (const std::vector<Interval>* list : {&routine.profile.reads, &routine.profile.writes})
            for (const Interval& iv : *list)
            {
                const PhysKey a = std::max(iv.lo, lo);
                const PhysKey b = std::min(iv.hi, hi);
                if (a <= b)
                    bytes += b - a + 1;
            }
        return bytes;
    };
    std::vector<std::set<uint32_t>> users(vars.size());
    for (size_t k = 0; k < vars.size(); ++k)
    {
        if (inArray[k] != 0)
            continue;
        // The surroundings: the enclosing buffer, else a window around the variable (one page)
        PhysKey lo = context.Key(vars[k].lo), hi = context.Key(vars[k].hi);
        PhysKey wlo = 0, whi = 0;
        bool box = false;
        for (const Region* r : context.out.store.ClaimsAt(lo))
            if (r->kind == RegionKind::Buffer)
            {
                wlo = r->lo;
                whi = r->hi;
                box = true;
            }
        if (!box)
        {
            const uint32_t z = vars[k].lo;
            const uint32_t from = std::max<uint32_t>(z >= kWindow / 2 ? z - kWindow / 2 : 0, z & 0xC000u);
            const uint32_t to = std::min<uint32_t>(from + kWindow - 1, (z & 0xC000u) + 0x3FFFu);
            wlo = context.Key(static_cast<uint16_t>(from));
            whi = context.Key(static_cast<uint16_t>(to));
        }
        for (uint32_t ri = 0; ri < l1.routines.size(); ++ri)
        {
            const Routine& routine = l1.routines[ri];
            if (touches(routine, lo, hi) == 0)
                continue;
            if (static_cast<uint64_t>(touches(routine, wlo, whi)) * 8 > static_cast<uint64_t>(whi - wlo + 1))
                continue;  // broad: an init copy, a sweep over the surroundings
            users[k].insert(ri);
        }
    }
    size_t i = 0;
    while (i < vars.size())
    {
        if (inArray[i] != 0 || users[i].empty())
        {
            ++i;
            continue;
        }
        std::set<uint32_t> shared = users[i];
        size_t j = i;
        while (j + 1 < vars.size() && inArray[j + 1] == 0 && static_cast<uint32_t>(vars[j].hi) + 1 == vars[j + 1].lo &&
               (vars[j + 1].lo >> 14) == (vars[i].lo >> 14) &&
               std::any_of(users[j + 1].begin(), users[j + 1].end(), [&](uint32_t u) { return shared.count(u) != 0; }))
        {
            ++j;
            shared.insert(users[j].begin(), users[j].end());
        }
        if (j > i)
        {
            Region r;
            r.kind = RegionKind::RecordArray;
            r.detector = DetectorId::Variables;
            r.status = RegionStatus::Observed;
            r.geometry.count = 1;
            r.geometry.stride = static_cast<uint16_t>(vars[j].hi - vars[i].lo + 1);
            for (size_t k = i; k <= j; ++k)
                r.geometry.fields.push_back(FieldOf(vars[k], vars[i].lo));
            std::string names;
            for (const uint32_t u : shared)
                if (names.size() < 60)
                    names += (names.empty() ? "" : " ") + Hex(l1.routines[u].z80);
            r.confidence = confidence::FromCount(400, 800, j - i + 1, 2);
            r.evidence.push_back(Evidence{"users", vars[i].lo, j - i + 1,
                                          std::to_string(j - i + 1) + " adjacent variables used by " + names});
            r.title = "Variables used together";
            r.users = context.UsersOf(vars[i].lo, vars[j].hi);
            context.Claim(std::move(r), vars[i].lo, vars[j].hi);
        }
        i = j + 1;
    }
}

}  // namespace

void DetectVariables(DetectorContext& context)
{
    // An address the code formed with LD rr,nn and used unchanged (LD HL,nn / INC (HL)) is as direct as (nn)
    std::vector<uint8_t> fixed(0x10000, 0);
    for (const PointerUse& p : context.l1.pointers)
    {
        // A block instruction (LDIR, CPIR, ...) moves the pair on: its first access is no variable
        const bool block = context.Start(p.useZ80) == 0xED &&
                           (context.Start(static_cast<uint16_t>(p.useZ80 + 1)) & 0xE4) == 0xA0;
        if (p.form == PointerForm::Immediate && p.minDelta == 0 && p.maxDelta == 0 && p.uses != 0 && !block)
            fixed[p.value] = 1;
    }
    const auto direct = [&fixed](uint16_t z80, const ByteUse& u) {
        return ((u.flags & (use::kAbsolute | use::kWordRead | use::kWordWrite | use::kPointer)) != 0 ||
                (fixed[z80] != 0 && (u.flags & (use::kRead | use::kWrite)) != 0)) &&
               !DetectorContext::Code(u);
    };
    std::vector<Var> vars;
    uint32_t a = 0;
    while (a <= 0xFFFF)
    {
        const auto z80 = static_cast<uint16_t>(a);
        const ByteUse& u = context.Use(z80);
        if (!context.InScope(z80) || !direct(z80, u) || Explained(context, z80))
        {
            ++a;
            continue;
        }
        Region r;
        r.kind = RegionKind::Variable;
        r.detector = DetectorId::Variables;
        r.status = RegionStatus::Observed;
        uint16_t hi = z80;
        const auto next = static_cast<uint16_t>(z80 + 1);
        const ByteUse& v = context.Use(next);
        const bool pairUse = (u.flags & (use::kWordRead | use::kWordWrite | use::kPointer)) != 0 &&
                             (v.flags & (use::kWordRead | use::kWordWrite)) != 0 && (next >> 14) == (z80 >> 14) &&
                             context.InScope(next) && !Explained(context, next) && !DetectorContext::Code(v);
        if (pairUse)
        {
            hi = next;
            r.geometry.role = (u.flags & use::kPointer) != 0 ? VarRole::Pointer : VarRole::Word;
            r.evidence.push_back(Evidence{(u.flags & use::kPointer) != 0 ? "pointer" : "word",
                                          static_cast<uint16_t>(context.RoutineEntry(u.firstReader != kNoKey
                                                                                         ? u.firstReader
                                                                                         : u.firstWriter) &
                                                                0xFFFF),
                                          static_cast<uint64_t>(u.reads) + u.writes, "read or written as a pair"});
        }
        else
        {
            const uint8_t start = context.Start(z80);
            if (u.writes != 0 && (u.flags & use::kIncDec) != 0 && (u.flags & use::kOtherWrite) == 0)
                r.geometry.role = VarRole::Counter;
            else if (u.writes != 0 && u.distinct <= 2 &&
                     (start == u.minWritten || start == u.maxWritten || u.distinct == 2))
                r.geometry.role = VarRole::Flag;
            else
                r.geometry.role = VarRole::Value;
            r.geometry.minValue = u.writes != 0 ? std::min(u.minWritten, start) : start;
            r.geometry.maxValue = u.writes != 0 ? std::max(u.maxWritten, start) : start;
            const PhysKey user = u.firstWriter != kNoKey ? u.firstWriter : u.firstReader;
            r.evidence.push_back(Evidence{u.writes != 0 ? "writer" : "reader",
                                          static_cast<uint16_t>(context.RoutineEntry(user) & 0xFFFF),
                                          static_cast<uint64_t>(u.reads) + u.writes,
                                          std::to_string(u.reads) + " reads, " + std::to_string(u.writes) +
                                              " writes" + (u.writes != 0 ? ", " + std::to_string(u.distinct) +
                                                                               (u.distinct >= 3 ? "+" : "") + " values"
                                                                         : std::string())});
        }
        r.confidence = (u.flags & use::kAbsolute) != 0 || fixed[z80] != 0 ? 800 : 650;
        char label[16];
        std::snprintf(label, sizeof(label), "%s_%04X", RolePrefix(r.geometry.role), z80);
        r.label = label;
        r.users = context.UsersOf(z80, hi);
        const Var var{z80, hi, r.geometry.role, r.geometry.minValue, r.geometry.maxValue};
        if (context.Claim(std::move(r), z80, hi))
            vars.push_back(var);
        a = static_cast<uint32_t>(hi) + 1;
    }

    std::vector<uint8_t> inArray(vars.size(), 0);
    StridedVariables(context, vars, inArray);
    VariableGroups(context, vars, inArray);
}

}  // namespace analysis
