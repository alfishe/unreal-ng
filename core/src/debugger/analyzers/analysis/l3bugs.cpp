#include "debugger/analyzers/analysis/l3bugs.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace analysis
{

namespace
{

std::string Hex4(uint32_t value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", value & 0xFFFF);
    return text;
}

std::string Hex2(uint32_t value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%02X", value & 0xFF);
    return text;
}

/// The Z80 address an instruction ran at (0 with no instruction)
uint16_t Z80Of(const L1Result& l1, PhysKey key)
{
    if (key == kNoKey)
        return 0;
    const Instruction* ins = l1.InstructionAt(key);
    return ins != nullptr ? ins->z80 : 0;
}

Finding Bug(const char* rule, uint16_t start, uint16_t end, uint16_t confidence, std::string claim)
{
    Finding f;
    f.kind = FindingKind::Bug;
    f.status = FindingStatus::Inferred;
    f.subject = rule;
    f.source = "bugs";
    f.start = start;
    f.end = end;
    f.confidence = confidence;
    f.claim = std::move(claim);
    return f;
}

/// Runs of Z80 addresses (start paging) where @p hit holds, at most @p gap bytes apart merged
template <typename Hit>
std::vector<std::pair<uint16_t, uint16_t>> Runs(const L1Result& l1, Hit hit, uint32_t gap = 0)
{
    std::vector<std::pair<uint16_t, uint16_t>> runs;
    for (uint32_t a = 0; a <= 0xFFFF; ++a)
    {
        const PhysKey key = l1.StartKey(static_cast<uint16_t>(a));
        const ByteUse* u = l1.UseAt(key);
        if (u == nullptr || !hit(static_cast<uint16_t>(a), key, *u))
            continue;
        if (!runs.empty() && a - runs.back().second <= gap + 1u && (a >> 14) == (runs.back().first >> 14u))
            runs.back().second = static_cast<uint16_t>(a);
        else
            runs.emplace_back(static_cast<uint16_t>(a), static_cast<uint16_t>(a));
    }
    return runs;
}

bool IsRam(const L1Result& l1, uint16_t z80)
{
    const uint16_t page = l1.startBanks[z80 >> 14];
    for (const auto& [index, name] : l1.pageNames)
        if (index == page)
            return name.rfind("RAM", 0) == 0;
    return z80 >= 0x4000;
}

/// read-unwritten: how far around a run the reader's other reads are looked at (static data or not)
constexpr uint32_t kStaticWindow = 256;

void ReadUnwritten(const L1Result& l1, const L2Result& l2, std::vector<Finding>& out)
{
    const std::vector<uint8_t> claimed = ClaimLevels(l2);
    const auto hit = [&](uint16_t z80, PhysKey key, const ByteUse& u) {
        if ((u.flags & (use::kRead | use::kWordRead)) == 0 || (u.flags & (use::kWrite | use::kBlockWrite)) != 0 ||
            (u.flags & (use::kOpcode | use::kOperand)) != 0 || z80 < 0x5B00 || !IsRam(l1, z80))
            return false;
        uint8_t value = 0;
        if (!l1.StartByte(key, value) || value != 0)
            return false;
        return claimed[z80] != 2;
    };
    for (const auto& [lo, hi] : Runs(l1, hit))
    {
        // A zero byte read but never written is every cleared variable: only runs, with zeros around them, count
        uint32_t zeros = 0;
        for (uint32_t a = lo >= 8 ? lo - 8u : 0; a <= std::min<uint32_t>(0xFFFF, hi + 8u); ++a)
        {
            uint8_t v = 1;
            if (l1.StartByte(l1.StartKey(static_cast<uint16_t>(a)), v) && v == 0)
                ++zeros;
        }
        if (hi - lo + 1u < 2 || zeros < (hi - lo + 1u) + 12u)
            continue;
        const ByteUse* u = l1.UseAt(l1.StartKey(lo));
        // The reader walks static data (it also reads bytes nothing wrote that hold other values nearby): the zeros
        // are values of a table, not an uninitialised area
        bool staticData = false;
        if (u != nullptr && u->lastReader != kNoKey)
            for (uint32_t a = lo >= kStaticWindow ? lo - kStaticWindow : 0;
                 a <= std::min<uint32_t>(0xFFFF, hi + kStaticWindow) && !staticData; ++a)
            {
                const PhysKey key = l1.StartKey(static_cast<uint16_t>(a));
                const ByteUse* nearby = l1.UseAt(key);
                uint8_t v = 0;
                staticData = nearby != nullptr && (nearby->flags & (use::kRead | use::kWordRead)) != 0 &&
                             (nearby->flags & (use::kWrite | use::kBlockWrite)) == 0 &&
                             (nearby->lastReader == u->lastReader || nearby->firstReader == u->lastReader) &&
                             l1.StartByte(key, v) && v != 0;
            }
        if (staticData)
            continue;
        Finding f = Bug("read-unwritten", lo, hi, 300,
                        Hex4(lo) + "-" + Hex4(hi) + " is read but never written in the session, and it holds zeros: " +
                            "an uninitialised area, or one only a part of the game not played fills");
        if (u != nullptr && u->lastReader != kNoKey)
        {
            f.at = Z80Of(l1, u->lastReader);
            f.evidence.push_back("read by " + Hex4(f.at) + (u->multiReader ? " (and others)" : "") + ", " +
                                 std::to_string(u->reads) + " reads of " + Hex4(lo));
            f.refs.push_back(Hex4(f.at));
        }
        out.push_back(std::move(f));
    }
}

void StackOverCode(const L1Result& l1, std::vector<Finding>& out)
{
    const auto hit = [](uint16_t, PhysKey, const ByteUse& u) {
        return (u.flags & use::kStackWrite) != 0 && (u.flags & (use::kOpcode | use::kOperand)) != 0;
    };
    for (const auto& [lo, hi] : Runs(l1, hit))
    {
        const ByteUse* u = l1.UseAt(l1.StartKey(lo));
        Finding f = Bug("stack-over-code", lo, hi, 600,
                        "The stack wrote over " + Hex4(lo) + "-" + Hex4(hi) + ", bytes that ran as code: the stack " +
                            "grew into the program (or the code there is a stack-borne copy)");
        if (u != nullptr && u->lastWriter != kNoKey)
        {
            f.at = Z80Of(l1, u->lastWriter);
            f.evidence.push_back("pushed by " + Hex4(f.at) + ", frames " + std::to_string(u->firstFrame) + ".." +
                                 std::to_string(u->lastFrame));
            f.refs.push_back(Hex4(f.at));
        }
        out.push_back(std::move(f));
    }
}

void CodeOverwritten(const L1Result& l1, std::vector<Finding>& out)
{
    const auto hit = [](uint16_t, PhysKey, const ByteUse& u) {
        // Overwritten by a block copy or the stack, or by anything at all and never run again (a buffer laid over
        // code that ran once): an operand patch of code that runs again is not an overlay
        return (u.flags & use::kSmc) != 0 && (u.flags & (use::kOpcode | use::kOperand)) != 0 &&
               ((u.flags & (use::kBlockWrite | use::kStackWrite)) != 0 || u.overlaid);
    };
    // One overlay is one finding: the code it overwrote has data and code not run between its runs
    constexpr uint32_t kOverlayGap = 32;
    for (const auto& [lo, hi] : Runs(l1, hit, kOverlayGap))
    {
        const ByteUse* u = l1.UseAt(l1.StartKey(lo));
        Finding f = Bug("code-overwritten", lo, hi, 400,
                        "Code at " + Hex4(lo) + "-" + Hex4(hi) + " ran, then was written over and did not run "
                            "again (or a block copy or the stack wrote over its opcodes): an overlay loaded on purpose, a "
                            "buffer laid over code that ran once, or a copy that runs too far");
        if (u != nullptr && u->lastWriter != kNoKey)
        {
            f.at = Z80Of(l1, u->lastWriter);
            f.evidence.push_back("written by " + Hex4(f.at) + ", " + std::to_string(u->writes) + " writes");
            f.refs.push_back(Hex4(f.at));
        }
        out.push_back(std::move(f));
    }
}

void StackImbalance(const L1Result& l1, std::vector<Finding>& out)
{
    for (const Routine& r : l1.routines)
    {
        const RoutineProfile& p = r.profile;
        if (p.abandoned == 0 && p.patchedReturns == 0)
            continue;
        std::string claim = "The routine at " + Hex4(r.z80) + " ";
        if (p.patchedReturns != 0)
            claim += "returned " + std::to_string(p.patchedReturns) + " times to another address than its caller " +
                     "pushed";
        if (p.abandoned != 0)
            claim += std::string(p.patchedReturns != 0 ? ", and " : "") + "was left " +
                     std::to_string(p.abandoned) + " times without a return (the stack pointer moved above its " +
                     "return address)";
        claim += ": a stack reset on purpose (a restart, a death), or a push / pop imbalance";
        Finding f = Bug("stack-imbalance", r.z80, r.z80, p.patchedReturns != 0 ? 400 : 300, claim);
        f.at = r.z80;
        f.evidence.push_back("entered " + std::to_string(p.entries) + " times, abandoned " + std::to_string(p.abandoned) +
                             ", patched returns " + std::to_string(p.patchedReturns));
        out.push_back(std::move(f));
    }
}

void IndexPastTable(const L1Result& l1, const L2Result& l2, std::vector<Finding>& out)
{
    // Only accesses that read through an index (L1 indexes: a value turned into an offset) can run past a table; a
    // walk one byte after another (a list, a mask blit, a block copy) through a larger structure reads around a
    // table L2 found inside it, which says nothing about an index
    std::set<PhysKey> indexed;
    std::vector<const Routine*> indexRoutine;  // the routine of each index read's instruction, looked up once
    indexRoutine.reserve(l1.indexes.size());
    for (const IndexUse& x : l1.indexes)
    {
        indexed.insert(x.instruction);
        indexRoutine.push_back(l1.RoutineOf(x.instruction));
    }
    for (const Region& r : l2.store.Regions())
    {
        if (r.kind != RegionKind::PointerTable && r.kind != RegionKind::HandlerTable && r.kind != RegionKind::InitTable)
            continue;
        // A reader whose index reads run on both sides of the table addresses a larger structure the table lies in (a
        // mask laid over a list, a record's field L2 took for a table): it says nothing about an index running off the
        // table's end. The structure is what the reader's routine read through its indexes in one connected stretch
        // from the table: the other fields of a record (read by the routine's next instructions), the same
        // instruction through other selectors. Reads of the same routine that do not touch that stretch (one
        // selector byte, a level number, indexing other tables far away) say nothing about this one
        std::map<PhysKey, std::pair<uint32_t, uint32_t>> span;
        const auto structure = [&](PhysKey k) {
            if (const auto it = span.find(k); it != span.end())
                return it->second;
            const Routine* routine = l1.RoutineOf(k);
            std::vector<std::pair<uint32_t, uint32_t>> reads;  // the routine's index reads (the reader's alone: none)
            uint32_t lo = 0x10000, hi = 0;
            for (size_t i = 0; i < l1.indexes.size(); ++i)
            {
                const IndexUse& x = l1.indexes[i];
                const bool own = x.instruction == k;
                if (!own && (routine == nullptr || indexRoutine[i] != routine))
                    continue;
                reads.emplace_back(x.lo, x.hi);
                if (own && x.hi >= r.z80 && x.lo <= r.Z80End())
                {
                    lo = std::min<uint32_t>(lo, x.lo);
                    hi = std::max<uint32_t>(hi, x.hi);
                }
            }
            for (bool grew = lo <= hi; grew;)
            {
                grew = false;
                for (const auto& [a, b] : reads)
                    if (a <= hi + 1 && b + 1 >= lo && (a < lo || b > hi))
                    {
                        lo = std::min(lo, a);
                        hi = std::max(hi, b);
                        grew = true;
                    }
            }
            return span[k] = {lo, hi};
        };
        const auto counts = [&](PhysKey k) {
            if (k == kNoKey || indexed.count(k) == 0)
                return false;
            const auto [lo, hi] = structure(k);
            return lo > hi || !(lo < r.z80 && hi > r.Z80End());
        };
        std::set<PhysKey> readers;
        for (PhysKey k = r.lo; k <= r.hi; ++k)
            if (const ByteUse* u = l1.UseAt(k))
            {
                if (counts(u->firstReader))
                    readers.insert(u->firstReader);
                if (counts(u->lastReader))
                    readers.insert(u->lastReader);
            }
        if (readers.empty())
            continue;
        std::vector<uint16_t> outside;
        PhysKey reader = kNoKey;
        const auto check = [&](int64_t z80) {
            if (z80 < 0 || z80 > 0xFFFF)
                return;
            const PhysKey key = l1.StartKey(static_cast<uint16_t>(z80));
            const ByteUse* u = l1.UseAt(key);
            if (u == nullptr || (u->flags & use::kRead) == 0)
                return;
            for (const PhysKey k : {u->firstReader, u->lastReader})
                if (k != kNoKey && readers.count(k) != 0)
                {
                    outside.push_back(static_cast<uint16_t>(z80));
                    reader = k;
                    return;
                }
        };
        for (int64_t d = 1; d <= 16; ++d)
        {
            check(static_cast<int64_t>(r.Z80End()) + d);
            check(static_cast<int64_t>(r.z80) - d);
        }
        if (outside.empty())
            continue;
        std::sort(outside.begin(), outside.end());
        Finding f = Bug("index-past-table", outside.front(), outside.back(), 350,
                        "The readers of the " + std::string(RegionKindName(r.kind)) + " at " + Hex4(r.z80) + "-" +
                            Hex4(r.Z80End()) + " also read " + std::to_string(outside.size()) +
                            " bytes outside it: an index out of range, or a table longer than L2 measured");
        f.at = Z80Of(l1, reader);
        f.evidence.push_back("reader " + Hex4(f.at) + " read " + Hex4(outside.front()) +
                             (outside.size() > 1 ? ".." + Hex4(outside.back()) : std::string()));
        f.refs.push_back(Hex4(r.z80));
        f.refs.push_back(Hex4(f.at));
        out.push_back(std::move(f));
    }
}

void CounterWrap(const L1Result& l1, std::vector<Finding>& out)
{
    const auto hit = [](uint16_t z80, PhysKey, const ByteUse& u) {
        return z80 >= 0x5B00 && (u.flags & use::kIncDec) != 0 && (u.flags & use::kOtherWrite) == 0 &&
               u.distinct >= 3 && u.minWritten == 0x00 && u.maxWritten == 0xFF;
    };
    for (const auto& [lo, hi] : Runs(l1, hit))
        for (uint32_t a = lo; a <= hi; ++a)
        {
            const ByteUse* u = l1.UseAt(l1.StartKey(static_cast<uint16_t>(a)));
            Finding f = Bug("counter-wrap", static_cast<uint16_t>(a), static_cast<uint16_t>(a), 250,
                            "The counter at " + Hex4(a) + " is changed only by INC / DEC and took both " + Hex2(0) +
                                " and " + Hex2(0xFF) + ": it wraps (a timer on purpose, or a count that runs over)");
            if (u != nullptr && u->lastWriter != kNoKey)
            {
                f.at = Z80Of(l1, u->lastWriter);
                f.evidence.push_back("written by " + Hex4(f.at) + (u->multiWriter ? " (and others)" : "") + ", " +
                                     std::to_string(u->writes) + " writes");
                f.refs.push_back(Hex4(f.at));
            }
            out.push_back(std::move(f));
        }
}

}  // namespace

std::vector<Finding> CandidateBugs(const L1Result& l1, const L2Result& l2)
{
    std::vector<Finding> out;
    ReadUnwritten(l1, l2, out);
    StackOverCode(l1, out);
    CodeOverwritten(l1, out);
    StackImbalance(l1, out);
    IndexPastTable(l1, l2, out);
    CounterWrap(l1, out);
    return out;
}

}  // namespace analysis
