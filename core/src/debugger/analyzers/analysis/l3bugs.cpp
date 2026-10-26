#include "debugger/analyzers/analysis/l3bugs.h"

#include <algorithm>
#include <cstdio>
#include <set>

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
                const ByteUse* near = l1.UseAt(key);
                uint8_t v = 0;
                staticData = near != nullptr && (near->flags & (use::kRead | use::kWordRead)) != 0 &&
                             (near->flags & (use::kWrite | use::kBlockWrite)) == 0 &&
                             (near->lastReader == u->lastReader || near->firstReader == u->lastReader) &&
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
        return (u.flags & use::kSmc) != 0 && (u.flags & use::kOpcode) != 0 &&
               (u.flags & (use::kBlockWrite | use::kStackWrite)) != 0;
    };
    for (const auto& [lo, hi] : Runs(l1, hit))
    {
        const ByteUse* u = l1.UseAt(l1.StartKey(lo));
        Finding f = Bug("code-overwritten", lo, hi, 400,
                        "Code at " + Hex4(lo) + "-" + Hex4(hi) + " ran, then a block copy or the stack wrote over " +
                            "its opcodes: an overlay loaded on purpose, or a copy that runs too far");
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
    for (const Region& r : l2.store.Regions())
    {
        if (r.kind != RegionKind::PointerTable && r.kind != RegionKind::HandlerTable && r.kind != RegionKind::InitTable)
            continue;
        std::set<PhysKey> readers;
        for (PhysKey k = r.lo; k <= r.hi; ++k)
            if (const ByteUse* u = l1.UseAt(k))
            {
                if (u->firstReader != kNoKey)
                    readers.insert(u->firstReader);
                if (u->lastReader != kNoKey)
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
