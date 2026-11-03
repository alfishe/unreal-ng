/// The code map detector: opcode and operand bytes the replay executed are code (observed); code reached from them
/// over a branch never taken, a call never made or the fall-through of a conditional never seen is decoded from the
/// memory the replay started from and claimed as inferred code, until it meets executed code, an unconditional
/// transfer, or a byte the code used as data. Code that was written while it was code is self-modifying. Routine
/// entries are L1's, plus the targets of calls in inferred code, plus the jump targets after an unconditional
/// transfer that only a tail jump or a backward branch reaches (JumpEntries), plus conditional tail calls
/// (TailEntries). Jump targets that look like a routine start but are not proven one become entry candidates for the
/// review queue (TailEntries, PrologueCandidates).
///
/// FinishUnused runs after every other detector, over the untouched bytes no claim explains (the stack counts as
/// untouched). A run of one value, 32 bytes or more, is a fill. A stretch between fills, 16 bytes or more, is code that
/// never ran when it decodes as Z80 (from one of its first four bytes, the start that lands most transfers) to 8
/// instructions or more, with no undefined ED opcode, and its JP / CALL / JR / DJNZ targets land, 9 in 10 of them and
/// 3 at least, on its own instruction starts (2 at least), in the ROM, or on executed code, unless a table entry (a
/// word holding its start, or the start the decode took, beside a word near it) points at it: that is a routine or records the session did not
/// reach. The fill is Unused; the code is inferred Code ("Code that never ran", from the start the decode took to its last
/// whole instruction, its instructions in the code map): code not yet run is code, not unused. A stretch a claimed handler table's entry points into is that routine: decoded from the
/// entry, it passes the same test and ends in an unconditional transfer (or runs into executed code), and it is
/// inferred Code, its handler entries routine entries; it ends at its first unconditional transfer that no branch or
/// call before it goes past. Bytes only the stack touched (PUSH / CALL / RST / an interrupt, POP / RET: no other read,
/// write or fetch) are the stack, Unused from the lowest pushed byte to the highest stack byte (gaps of up to 8
/// untouched bytes inside); bytes only popped (LD SP,rows / POP walks data) are no stack alone. An untouched run of 32 bytes or more, not one value, the same bytes as a range of
/// the ROM bank 0 holds at the start is a ROM copy (BASIC's user-defined graphics, the letters A-U of the ROM font,
/// left in RAM), the ROM range named: inferred Code when the whole copy decodes as code by the same test (a ROM routine
/// copied to be patched and called later), else Unused; the ROM's 8-byte grams are indexed once. Running
/// last, it never stands in the way of a claim a detector infers over untouched bytes (the frames of a sprite set the
/// replay did not draw, the records of a level it did not reach).

#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "debugger/analyzers/analysis/detectors/detectors.h"

namespace analysis
{

namespace
{

constexpr uint32_t kMaxInferred = 32768;

struct Targets
{
    uint16_t to[2] = {0, 0};
    int count = 0;
    bool call = false;  ///< to[0] is a call / RST target
    bool stops = false; ///< no fall-through (JP, JR, RET, JP (rr), RETI, RETN)
};

Targets StaticTargets(const OpInfo& op, uint16_t z80, const uint8_t* b)
{
    Targets t;
    const auto next = static_cast<uint16_t>(z80 + op.length);
    const auto imm16 = static_cast<uint16_t>(b[op.immOffset & 3] | (b[(op.immOffset + 1) & 3] << 8));
    switch (op.flow)
    {
        case Flow::None:
        case Flow::Halt:
            break;
        case Flow::Jp:
            t.to[t.count++] = imm16;
            t.stops = op.cond == kAlways;
            break;
        case Flow::Jr:
        case Flow::Djnz:
            t.to[t.count++] = static_cast<uint16_t>(next + static_cast<int8_t>(b[1]));
            t.stops = op.flow == Flow::Jr && op.cond == kAlways;
            break;
        case Flow::Call:
            t.to[t.count++] = imm16;
            t.call = true;
            break;
        case Flow::Rst:
            t.to[t.count++] = op.rstTarget;
            t.call = true;
            break;
        case Flow::Ret:
            t.stops = op.cond == kAlways;
            break;
        case Flow::Reti:
        case Flow::Retn:
        case Flow::JpIndirect:
            t.stops = true;
            break;
    }
    return t;
}

/// Jump targets that start a routine of their own: the instruction before the target ends in an unconditional
/// transfer (or is not code), the target is reached by an unconditional JP / JR (a tail jump) or by a branch from
/// after it (a backward branch from a later routine), and no branch from the code before it jumps over it (the
/// head of a loop entered at its test is no entry). A forward conditional skip over a RET stays inside its routine.
std::vector<uint16_t> JumpEntries(const DetectorContext& context, const std::map<uint16_t, CodeLine>& lines,
                                  const std::vector<std::pair<uint16_t, std::string>>& known)
{
    constexpr uint32_t kWindow = 512;  ///< how far back the jump-over check looks
    struct Branch
    {
        uint16_t from;
        uint16_t to;
    };
    std::vector<Branch> branches;
    std::set<uint16_t> candidates;
    for (const auto& [z80, line] : lines)
    {
        const OpInfo& op = DecodeOp(line.bytes);
        if (op.flow != Flow::Jp && op.flow != Flow::Jr && op.flow != Flow::Djnz)
            continue;
        const Targets t = StaticTargets(op, z80, line.bytes);
        if (t.count == 0)
            continue;
        const uint16_t to = t.to[0];
        branches.push_back(Branch{z80, to});
        if (!context.InScope(to) || lines.count(to) == 0 || (!t.stops && to > z80))
            continue;
        // The instruction ending right before the target
        bool fallsIn = false;
        auto before = lines.lower_bound(to);
        if (before != lines.begin())
        {
            --before;
            if (static_cast<uint32_t>(before->first) + before->second.length == to)
                fallsIn = !StaticTargets(DecodeOp(before->second.bytes), before->first, before->second.bytes).stops;
        }
        if (!fallsIn)
            candidates.insert(to);
    }
    std::set<uint16_t> entries;
    for (const auto& e : known)
        entries.insert(e.first);
    std::vector<uint16_t> found;
    for (const uint16_t to : candidates)
    {
        if (entries.count(to) != 0)
            continue;
        const auto next = entries.upper_bound(to);
        const uint32_t limit = next != entries.end() ? *next : std::min<uint32_t>(to + kWindow, 0x10000);
        const bool over = std::any_of(branches.begin(), branches.end(), [&](const Branch& b) {
            return b.from < to && b.from + kWindow >= to && b.to > to && b.to < limit;
        });
        if (!over)
            found.push_back(to);
    }
    return found;
}

/// The static branches (JP / JR / DJNZ / CALL / RST with a target) of the code map: target -> sources
std::map<uint16_t, std::vector<uint16_t>> BranchSources(const std::map<uint16_t, CodeLine>& lines)
{
    std::map<uint16_t, std::vector<uint16_t>> sources;
    for (const auto& [z80, line] : lines)
    {
        const Targets t = StaticTargets(DecodeOp(line.bytes), z80, line.bytes);
        for (int i = 0; i < t.count; ++i)
            sources[t.to[i]].push_back(z80);
    }
    return sources;
}

/// The instruction that ends right at @p z80 (nullptr: none, or not contiguous)
const CodeLine* LineBefore(const std::map<uint16_t, CodeLine>& lines, uint16_t z80)
{
    auto before = lines.lower_bound(z80);
    if (before == lines.begin())
        return nullptr;
    --before;
    return static_cast<uint32_t>(before->first) + before->second.length == z80 ? &before->second : nullptr;
}

/// The code a tail starting at @p start runs before it leaves for a known entry: every instruction reached from
/// @p start (fall-through, jumps; a call returns to the next instruction) below @p limit. Closed when it never
/// returns (no RET / RETI / RETN / JP (rr)), never jumps below @p start or past @p limit to a non-entry, and no
/// branch from outside it lands inside it except at @p start.
struct Tail
{
    std::set<uint16_t> body;
    bool closed = false;
    std::string why;  ///< what broke it
};

Tail TailFrom(const std::map<uint16_t, CodeLine>& lines, const std::map<uint16_t, std::vector<uint16_t>>& sources,
              const std::set<uint16_t>& entries, uint16_t start, uint32_t limit)
{
    constexpr size_t kMaxBody = 4096;
    Tail tail;
    std::vector<uint16_t> work{start};
    while (!work.empty())
    {
        uint32_t at = work.back();
        work.pop_back();
        while (at < limit && tail.body.count(static_cast<uint16_t>(at)) == 0)
        {
            const auto it = lines.find(static_cast<uint16_t>(at));
            if (it == lines.end())
            {
                tail.why = "runs into bytes that are not code";
                return tail;
            }
            if (at != start && entries.count(static_cast<uint16_t>(at)) != 0)
                break;  // falls into a routine
            if (tail.body.size() >= kMaxBody)
            {
                tail.why = "too long";
                return tail;
            }
            tail.body.insert(static_cast<uint16_t>(at));
            const CodeLine& line = it->second;
            const OpInfo& op = DecodeOp(line.bytes);
            if (op.flow == Flow::Ret || op.flow == Flow::Reti || op.flow == Flow::Retn || op.flow == Flow::JpIndirect)
            {
                tail.why = "returns";
                return tail;
            }
            const Targets t = StaticTargets(op, line.z80, line.bytes);
            if (!t.call)
                for (int i = 0; i < t.count; ++i)
                {
                    const uint16_t to = t.to[i];
                    if (entries.count(to) != 0 && to != start)
                        continue;  // leaves for a routine
                    if (to < start || to >= limit)
                    {
                        tail.why = to < start ? "jumps back into the code before it" : "jumps past the next routine";
                        return tail;
                    }
                    work.push_back(to);
                }
            if (t.stops)
                break;
            at += line.length;
        }
    }
    for (const uint16_t a : tail.body)
    {
        if (a == start)
            continue;
        const auto it = sources.find(a);
        if (it == sources.end())
            continue;
        for (const uint16_t from : it->second)
            if (tail.body.count(from) == 0)
            {
                tail.why = "entered from outside it inside it";
                return tail;
            }
    }
    tail.closed = true;
    return tail;
}

/// Conditional tail calls: the target T of a conditional JP whose code never comes back starts a routine of its own
/// when the instruction before T is an unconditional JP to a known entry (the code before leaves for good), the JP is
/// the only way into T, and T's code is closed (TailFrom). A forward skip over a JP whose code returns or rejoins the
/// code before it stays inside its routine. Closed tails that miss the first two conditions are entry candidates.
std::vector<uint16_t> TailEntries(const std::map<uint16_t, CodeLine>& lines,
                                  const std::map<uint16_t, std::vector<uint16_t>>& sources,
                                  const std::set<uint16_t>& entries, const DetectorContext& context,
                                  std::vector<EntryCandidate>& candidates)
{
    std::vector<uint16_t> found;
    for (const auto& [to, from] : sources)
    {
        if (!context.InScope(to) || entries.count(to) != 0 || lines.count(to) == 0)
            continue;
        bool forwardJp = false;
        for (const uint16_t s : from)
        {
            const CodeLine& line = lines.at(s);
            const OpInfo& op = DecodeOp(line.bytes);
            forwardJp = forwardJp || (op.flow == Flow::Jp && op.cond != kAlways && s < to);
        }
        if (!forwardJp)
            continue;
        const CodeLine* before = LineBefore(lines, to);
        if (before == nullptr)
            continue;
        const OpInfo& beforeOp = DecodeOp(before->bytes);
        const Targets beforeTargets = StaticTargets(beforeOp, before->z80, before->bytes);
        if (!beforeTargets.stops)
            continue;  // falls in: a skip inside the routine
        const auto next = entries.upper_bound(to);
        const uint32_t limit = next != entries.end() ? *next : 0x10000u;
        const Tail tail = TailFrom(lines, sources, entries, to, limit);
        if (!tail.closed)
            continue;
        const bool tailJump = beforeOp.flow == Flow::Jp && beforeOp.cond == kAlways && beforeTargets.count == 1 &&
                              entries.count(beforeTargets.to[0]) != 0;
        bool single = from.size() == 1;
        for (const uint16_t s : from)
            single = single && DecodeOp(lines.at(s).bytes).flow == Flow::Jp;
        if (tailJump && single)
        {
            found.push_back(to);
            continue;
        }
        EntryCandidate c;
        c.z80 = to;
        c.rule = "tail";
        c.sources = from;
        c.why = "Code entered only by jumps (" + std::to_string(from.size()) +
                (from.size() == 1 ? " site" : " sites") + ") that never returns nor rejoins the code before it (" +
                std::to_string(tail.body.size()) + " instructions)" +
                (tailJump ? "" : "; the instruction before it is no tail jump to a routine") + ".";
        candidates.push_back(std::move(c));
    }
    return found;
}

/// Prologues: the straight run of code (no branch in it, none into it) that falls into a known entry and starts at a
/// target of jumps from two or more sites is shared start-up code of that entry. Whether it is a routine of its own
/// is a reading of the program, not a fact: an entry candidate.
void PrologueCandidates(const std::map<uint16_t, CodeLine>& lines,
                        const std::map<uint16_t, std::vector<uint16_t>>& sources, const std::set<uint16_t>& entries,
                        const DetectorContext& context, std::vector<EntryCandidate>& candidates)
{
    constexpr int kMaxRun = 64;
    for (const uint16_t e : entries)
    {
        const CodeLine* line = LineBefore(lines, e);
        uint16_t start = e;
        for (int n = 0; line != nullptr && n < kMaxRun; ++n)
        {
            const OpInfo& op = DecodeOp(line->bytes);
            const Targets t = StaticTargets(op, line->z80, line->bytes);
            const bool selfLoop = t.count == 1 && !t.call && t.to[0] == line->z80;
            if ((op.flow != Flow::None && op.flow != Flow::Halt && !selfLoop) || entries.count(line->z80) != 0)
                break;
            start = line->z80;
            const auto it = sources.find(line->z80);
            if (it != sources.end() && std::any_of(it->second.begin(), it->second.end(),
                                                   [&](uint16_t s) { return s != line->z80; }))
                break;
            line = LineBefore(lines, line->z80);
        }
        if (start == e || !context.InScope(start))
            continue;
        const auto it = sources.find(start);
        if (it == sources.end())
            continue;
        std::set<uint16_t> sites;
        for (const uint16_t s : it->second)
            if (s != start)
                sites.insert(s);
        if (sites.size() < 2)
            continue;
        EntryCandidate c;
        c.z80 = start;
        c.rule = "prologue";
        c.sources.assign(sites.begin(), sites.end());
        char text[160];
        std::snprintf(text, sizeof(text),
                      "Straight code from %u jump sites that runs into the routine at $%04X (%u bytes before it).",
                      static_cast<unsigned>(sites.size()), e, static_cast<unsigned>(e - start));
        c.why = text;
        candidates.push_back(std::move(c));
    }
}

}  // namespace

void DetectCodeMap(DetectorContext& context)
{
    const L1Result& l1 = context.l1;
    L2Result& out = context.out;

    // Executed instructions, in the start's Z80 view
    std::map<uint16_t, CodeLine> lines;
    std::vector<uint8_t> isCode(0x10000, 0);  // 1 executed, 2 inferred
    uint32_t unmapped = 0;
    for (const Instruction& ins : l1.instructions)
    {
        uint16_t z80 = 0;
        if (!context.Z80Of(ins.key, z80))
        {
            ++unmapped;
            continue;
        }
        if (!context.InScope(z80))
            continue;
        CodeLine line;
        line.key = ins.key;
        line.z80 = z80;
        line.length = ins.length;
        std::copy(std::begin(ins.bytes), std::end(ins.bytes), std::begin(line.bytes));
        line.executed = true;
        line.smc = ins.selfModified;
        for (uint8_t i = 0; i < ins.length; ++i)
        {
            const ByteUse& u = context.Use(static_cast<uint16_t>(z80 + i));
            if ((u.flags & use::kSmc) != 0 && !u.overlaid)  // an overlay is not a patch of this code
                line.smc = true;
        }
        lines.emplace(z80, line);
        for (uint8_t i = 0; i < ins.length; ++i)
            isCode[static_cast<uint16_t>(z80 + i)] = 1;
    }
    if (unmapped != 0)
        out.notes.push_back(std::to_string(unmapped) +
                            " executed instructions are in pages not mapped at the start (not in the Z80 view)");

    // Static descent from the executed code into what the replay did not take
    std::set<uint16_t> callTargets;
    std::map<uint16_t, uint16_t> inferredFrom;  // inferred instruction -> the instruction whose branch reached it
    std::deque<std::pair<uint16_t, uint16_t>> work;  // (target, from)
    const auto push = [&](const CodeLine& line) {
        const OpInfo& op = DecodeOp(line.bytes);
        const Targets t = StaticTargets(op, line.z80, line.bytes);
        for (int i = 0; i < t.count; ++i)
        {
            work.emplace_back(t.to[i], line.z80);
            if (t.call && i == 0)
                callTargets.insert(t.to[i]);
        }
        if (!t.stops)
            work.emplace_back(static_cast<uint16_t>(line.z80 + line.length), line.z80);
    };
    for (const auto& [z80, line] : lines)
        push(line);
    uint32_t inferred = 0;
    while (!work.empty() && inferred < kMaxInferred)
    {
        auto [at, from] = work.front();
        work.pop_front();
        while (inferred < kMaxInferred)
        {
            if (!context.InScope(at) || isCode[at] != 0)
                break;
            uint8_t bytes[4];
            for (int i = 0; i < 4; ++i)
                bytes[i] = context.Start(static_cast<uint16_t>(at + i));
            const OpInfo& op = DecodeOp(bytes);
            bool clash = false;
            for (uint8_t i = 0; i < op.length && !clash; ++i)
            {
                const auto a = static_cast<uint16_t>(at + i);
                clash = isCode[a] != 0 || !context.InScope(a) || DetectorContext::Data(context.Use(a));
            }
            // A run of filler (three equal $00 / $FF bytes) is not code
            if (!clash && (bytes[0] == 0x00 || bytes[0] == 0xFF) && bytes[1] == bytes[0] && bytes[2] == bytes[0])
                clash = true;
            if (clash)
                break;
            CodeLine line;
            line.key = context.Key(at);
            line.z80 = at;
            line.length = op.length;
            std::copy(std::begin(bytes), std::end(bytes), std::begin(line.bytes));
            for (uint8_t i = op.length; i < 4; ++i)
                line.bytes[i] = 0;
            line.executed = false;
            lines.emplace(at, line);
            inferredFrom.emplace(at, from);
            for (uint8_t i = 0; i < op.length; ++i)
                isCode[static_cast<uint16_t>(at + i)] = 2;
            ++inferred;
            const Targets t = StaticTargets(op, at, line.bytes);
            for (int i = 0; i < t.count; ++i)
            {
                work.emplace_back(t.to[i], at);
                if (t.call && i == 0)
                    callTargets.insert(t.to[i]);
            }
            if (t.stops)
                break;
            from = at;
            at = static_cast<uint16_t>(at + op.length);
        }
    }

    // Entries: L1's routines, then the call targets that are code
    for (const Routine& r : l1.routines)
    {
        uint16_t z80 = 0;
        if (context.Z80Of(r.entry, z80) && context.InScope(z80))
            out.entries.emplace_back(z80, std::string());
    }
    for (const uint16_t t : callTargets)
        if (lines.count(t) != 0)
            out.entries.emplace_back(t, std::string());
    for (const uint16_t t : JumpEntries(context, lines, out.entries))
        out.entries.emplace_back(t, std::string());
    {
        const std::map<uint16_t, std::vector<uint16_t>> sources = BranchSources(lines);
        std::set<uint16_t> known;
        for (const auto& e : out.entries)
            known.insert(e.first);
        std::vector<EntryCandidate> candidates;
        for (const uint16_t t : TailEntries(lines, sources, known, context, candidates))
            out.entries.emplace_back(t, std::string());
        for (const auto& e : out.entries)
            known.insert(e.first);
        PrologueCandidates(lines, sources, known, context, candidates);
        std::sort(candidates.begin(), candidates.end(),
                  [](const EntryCandidate& a, const EntryCandidate& b) {
                      return a.z80 != b.z80 ? a.z80 < b.z80 : a.rule > b.rule;  // tail before prologue
                  });
        for (EntryCandidate& c : candidates)
            if (known.count(c.z80) == 0 &&
                (out.entryCandidates.empty() || out.entryCandidates.back().z80 != c.z80))
                out.entryCandidates.push_back(std::move(c));
    }
    for (auto& [z80, line] : lines)
        line.entry = std::any_of(out.entries.begin(), out.entries.end(),
                                 [z = z80](const auto& e) { return e.first == z; });

    // Regions: runs of code bytes of one status
    out.code.clear();
    out.code.reserve(lines.size());
    for (const auto& [z80, line] : lines)
        out.code.push_back(line);
    std::sort(out.code.begin(), out.code.end(), [](const CodeLine& a, const CodeLine& b) { return a.key < b.key; });

    uint32_t a = 0;
    while (a <= 0xFFFF)
    {
        if (isCode[a] == 0)
        {
            ++a;
            continue;
        }
        const uint8_t status = isCode[a];
        uint32_t b = a;
        while (b + 1 <= 0xFFFF && isCode[b + 1] == status && ((b + 1) >> 14) == (a >> 14))
            ++b;
        Region r;
        r.kind = RegionKind::Code;
        r.detector = DetectorId::CodeMap;
        uint64_t executions = 0;
        uint32_t instructions = 0, smc = 0;
        uint16_t firstFrom = 0;
        bool haveFrom = false;
        for (auto it = lines.lower_bound(static_cast<uint16_t>(a)); it != lines.end() && it->first <= b; ++it)
        {
            ++instructions;
            smc += it->second.smc ? 1 : 0;
            if (status == 1)
            {
                if (const Instruction* ins = l1.InstructionAt(it->second.key))
                    executions += ins->executions;
            }
            else if (!haveFrom)
            {
                const auto from = inferredFrom.find(it->first);
                if (from != inferredFrom.end())
                {
                    firstFrom = from->second;
                    haveFrom = true;
                }
            }
        }
        if (status == 1)
        {
            r.status = RegionStatus::Observed;
            r.confidence = confidence::kMax;
            r.evidence.push_back(Evidence{"executed", static_cast<uint16_t>(a), executions,
                                          std::to_string(instructions) + " instructions"});
        }
        else
        {
            r.status = RegionStatus::Inferred;
            r.confidence = 500;
            r.evidence.push_back(Evidence{"branch", firstFrom, instructions,
                                          "decoded from a branch, call or fall-through the replay did not take"});
        }
        if (smc != 0)
            r.evidence.push_back(Evidence{"self-modifying", static_cast<uint16_t>(a), smc, "written while code"});
        r.users = context.UsersOf(static_cast<uint16_t>(a), static_cast<uint16_t>(b));
        context.Claim(std::move(r), static_cast<uint16_t>(a), static_cast<uint16_t>(b));
        a = b + 1;
    }

    // The rest: bytes only read or written are data, bytes nothing touched are unknown (the fallbacks the later
    // detectors refine; the stack counts as untouched). ROM bytes only written are a sink: the writes are lost and
    // say nothing of the bytes (a clip pointer aimed at the ROM)
    const auto romPage = [&](uint16_t page) {
        for (const auto& [number, name] : context.l1.pageNames)
            if (number == page)
                return name.rfind("ROM", 0) == 0;
        return false;
    };
    const auto classOf = [&](uint32_t at) -> int {
        const auto z80 = static_cast<uint16_t>(at);
        if (!context.InScope(z80) || isCode[at] != 0)
            return 0;
        const ByteUse& u = context.Use(z80);
        if (!DetectorContext::Data(u))
            return 2;
        return (u.flags & use::kRead) == 0 && romPage(KeyPage(context.Key(z80))) ? 3 : 1;
    };
    a = 0;
    while (a <= 0xFFFF)
    {
        const int cls = classOf(a);
        if (cls == 0)
        {
            ++a;
            continue;
        }
        uint32_t b = a;
        while (b + 1 <= 0xFFFF && ((b + 1) >> 14) == (a >> 14) && classOf(b + 1) == cls)
            ++b;
        Region r;
        r.detector = DetectorId::CodeMap;
        r.status = RegionStatus::Observed;
        if (cls != 2)
        {
            uint64_t reads = 0, writes = 0;
            for (uint32_t i = a; i <= b; ++i)
            {
                const ByteUse& u = context.Use(static_cast<uint16_t>(i));
                reads += u.reads;
                writes += u.writes;
            }
            r.kind = RegionKind::Data;
            r.confidence = 700;
            r.evidence.push_back(Evidence{"data", static_cast<uint16_t>(a), reads + writes,
                                          std::to_string(reads) + " reads, " + std::to_string(writes) + " writes"});
            if (cls == 3)
            {
                r.title = kRomWriteSink;
                r.evidence.push_back(Evidence{kRomWriteSink, static_cast<uint16_t>(a), writes,
                                              "written only, in ROM: the writes are lost"});
            }
            r.users = context.UsersOf(static_cast<uint16_t>(a), static_cast<uint16_t>(b));
        }
        else
        {
            r.kind = RegionKind::Untouched;
            r.confidence = 100;
            r.evidence.push_back(Evidence{"untouched", static_cast<uint16_t>(a), b - a + 1, "no access in the replay"});
        }
        context.Claim(std::move(r), static_cast<uint16_t>(a), static_cast<uint16_t>(b));
        a = b + 1;
    }
}

namespace
{

constexpr uint32_t kMinFill = 32;
constexpr uint32_t kMinLeftover = 16;
constexpr uint32_t kMinLeftoverInstructions = 8;
constexpr uint32_t kMinLeftoverTransfers = 3;
constexpr uint32_t kMinLeftoverInside = 2;
constexpr uint32_t kTableNeighbourSpan = 0x400;
constexpr uint32_t kStackGap = 8;         ///< untouched bytes a stack may hold between two stack bytes
constexpr uint32_t kMinRomCopy = 32;
constexpr uint32_t kRomGram = 8;
constexpr size_t kRomGramCandidates = 16;  ///< ROM offsets kept per gram (the first ones)
constexpr uint32_t kMinRomCopyValues = 4;  ///< distinct values a ROM copy holds (fewer: a fill, or almost one)

/// Touched only by the stack: pushed or popped, never read, written or fetched otherwise
bool StackOnly(const ByteUse& u)
{
    return (u.flags & (use::kStackRead | use::kStackWrite)) != 0 && !DetectorContext::Touched(u);
}

/// Pushed (PUSH / CALL / RST / an interrupt), and touched by nothing else than the stack: where a stack is. Bytes
/// only popped are not one alone (LD SP,rows / POP DE walks data: sprite rows, a list)
bool StackPushed(const ByteUse& u)
{
    return (u.flags & use::kStackWrite) != 0 && StackOnly(u);
}

/// The bytes bank 0 holds at the start when it is a ROM (empty: no ROM there, or a page L1 did not name), with its
/// 8-byte grams indexed (grams of one value left out: those are fills)
struct RomIndex
{
    std::vector<uint8_t> bytes;
    std::unordered_map<uint64_t, std::vector<uint16_t>> grams;

    static uint64_t Gram(const uint8_t* at)
    {
        uint64_t g = 0;
        for (uint32_t i = 0; i < kRomGram; ++i)
            g = (g << 8) | at[i];
        return g;
    }
    static bool OneValue(const uint8_t* at)
    {
        for (uint32_t i = 1; i < kRomGram; ++i)
            if (at[i] != at[0])
                return false;
        return true;
    }

    void Build(const DetectorContext& context)
    {
        const uint16_t page = context.l1.startBanks[0];
        bool rom = false;
        for (const auto& [number, name] : context.l1.pageNames)
            if (number == page)
                rom = name.rfind("ROM", 0) == 0;
        uint8_t probe = 0;
        if (!rom || !context.l1.StartByte(MakeKey(page, 0), probe))
            return;
        bytes.resize(0x4000);
        for (uint32_t i = 0; i < 0x4000; ++i)
            bytes[i] = context.Start(static_cast<uint16_t>(i));
        grams.reserve(0x4000);
        for (uint32_t i = 0; i + kRomGram <= bytes.size(); ++i)
        {
            if (OneValue(&bytes[i]))
                continue;
            std::vector<uint16_t>& at = grams[Gram(&bytes[i])];
            if (at.size() < kRomGramCandidates)
                at.push_back(static_cast<uint16_t>(i));
        }
    }
};

/// ED xx with no instruction behind it (a NOP on the Z80, never in code a person or a compiler wrote)
bool UndefinedEd(uint8_t b0, uint8_t b1)
{
    if (b0 != 0xED)
        return false;
    if (b1 < 0x40 || b1 >= 0xC0 || (b1 >= 0x80 && b1 < 0xA0))
        return true;
    return b1 >= 0xA0 && (b1 & 0x04) != 0;  // A4-A7, AC-AF, B4-B7, BC-BF
}

struct Leftover
{
    uint32_t instructions = 0;
    uint32_t transfers = 0;
    uint32_t landed = 0;  ///< on its own instruction starts, in the ROM or on executed code
    uint32_t inside = 0;  ///< on its own instruction starts
    uint32_t end = 0;     ///< the last byte decoded
    uint32_t stop = 0;    ///< the last byte of the last unconditional transfer (0: none)
    /// The last byte of the first unconditional transfer no branch or call before it (or its own) goes past, to a byte
    /// up to the end of the stretch: where a routine entered at the start ends (0: none)
    uint32_t firstStop = 0;
    bool undefined = false;
};

/// The leftover test: enough instructions and transfers, most of them landing, some on its own instructions
bool CodeLike(const Leftover& l)
{
    return !l.undefined && l.instructions >= kMinLeftoverInstructions && l.transfers >= kMinLeftoverTransfers &&
           l.inside >= kMinLeftoverInside && l.landed * 10 >= l.transfers * 9;
}

/// Decode [from, hi] as straight code and count where its transfers land
Leftover DecodeLeftover(const DetectorContext& context, uint32_t from, uint32_t lo, uint32_t hi)
{
    Leftover out;
    std::set<uint32_t> starts;
    std::vector<std::pair<uint32_t, bool>> targets;  // target, absolute
    uint32_t reach = 0;  // the furthest target ahead, up to hi, of the transfers decoded so far
    for (uint32_t at = from; at <= hi;)
    {
        uint8_t b[4];
        for (uint32_t i = 0; i < 4; ++i)
            b[i] = context.Start(static_cast<uint16_t>(at + i));
        const OpInfo& op = DecodeOp(b);
        if (at + op.length - 1 > hi)
            break;
        starts.insert(at);
        ++out.instructions;
        out.undefined = out.undefined || UndefinedEd(b[0], b[1]) ||
                        ((b[0] == 0xDD || b[0] == 0xFD) && UndefinedEd(b[1], b[2]));
        const Targets t = StaticTargets(op, static_cast<uint16_t>(at), b);
        if (op.flow == Flow::Jp || op.flow == Flow::Call || op.flow == Flow::Jr || op.flow == Flow::Djnz)
            for (int i = 0; i < t.count; ++i)
            {
                targets.emplace_back(t.to[i], op.flow == Flow::Jp || op.flow == Flow::Call);
                if (t.to[i] > at && t.to[i] <= hi)
                    reach = std::max<uint32_t>(reach, t.to[i]);
            }
        out.end = at + op.length - 1;
        if (t.stops)
        {
            out.stop = out.end;
            if (out.firstStop == 0 && reach <= out.end)
                out.firstStop = out.end;
        }
        at += op.length;
    }
    for (const auto& [to, absolute] : targets)
    {
        ++out.transfers;
        if (to >= lo && to <= hi)
        {
            if (starts.count(to) != 0)
            {
                ++out.landed;
                ++out.inside;
            }
            continue;
        }
        const ByteUse& u = context.Use(static_cast<uint16_t>(to));
        if ((absolute && to < 0x4000) || (u.flags & use::kOpcode) != 0)
            ++out.landed;
    }
    return out;
}

/// A word in the image holds `start` beside another word near it (a handler or record table entry): bytes reached
/// through such a table are a routine or records that did not run in this session, not leftovers
bool TableEntryPoint(const DetectorContext& context, uint32_t start)
{
    const auto word = [&](uint32_t at) {
        return static_cast<uint32_t>(context.Start(static_cast<uint16_t>(at))) |
               (static_cast<uint32_t>(context.Start(static_cast<uint16_t>(at + 1))) << 8);
    };
    for (uint32_t at = 0x4000; at + 1 <= 0xFFFF; ++at)
    {
        if (word(at) != start)
            continue;
        for (const int step : {-4, -2, 2, 4})
        {
            const int64_t near = static_cast<int64_t>(at) + step;
            if (near < 0x4000 || near + 1 > 0xFFFF)
                continue;
            const uint32_t other = word(static_cast<uint32_t>(near));
            if ((other > start ? other - start : start - other) < kTableNeighbourSpan)
                return true;
        }
    }
    return false;
}

}  // namespace

void FinishUnused(DetectorContext& context)
{
    const std::vector<uint8_t> claimed = context.ClaimedMap();
    const auto free = [&](uint32_t at) {
        const auto z80 = static_cast<uint16_t>(at);
        if (!context.InScope(z80) || claimed[at] != 0)
            return false;
        return !DetectorContext::Touched(context.Use(z80));
    };
    const auto claim = [&](uint32_t lo, uint32_t hi, const std::string& title, const Evidence& evidence,
                           uint16_t confidence) {
        Region r;
        r.kind = RegionKind::Unused;
        r.detector = DetectorId::CodeMap;
        r.status = RegionStatus::Inferred;
        r.title = title;
        r.confidence = confidence;
        r.evidence.push_back(evidence);
        r.evidence.push_back(Evidence{"untouched", static_cast<uint16_t>(lo), hi - lo + 1, "no access in the replay"});
        context.Claim(std::move(r), static_cast<uint16_t>(lo), static_cast<uint16_t>(hi));
    };
    // The entries of the handler tables claimed so far: an untouched stretch one of them points into holds a handler
    // the session did not reach
    std::vector<uint8_t> handlerEntry(0x10000, 0);
    for (const Region& r : context.out.store.Regions())
        if (r.kind == RegionKind::HandlerTable)
            for (uint32_t at = r.z80; at + 1 <= r.Z80End(); at += 2)
                handlerEntry[static_cast<size_t>(context.Start(static_cast<uint16_t>(at))) |
                             (static_cast<size_t>(context.Start(static_cast<uint16_t>(at + 1))) << 8)] = 1;
    std::vector<CodeLine> inferredLines;  // the instructions of code that never ran, added to the code map at the end
    // [from, end] as inferred code the session did not run (code not yet run is code, not unused): the region and its
    // instructions, the handler entries among them routine entries
    const auto claimCode = [&](uint32_t from, uint32_t end, const std::string& title, const Evidence& evidence,
                               uint16_t confidence) {
        Region r;
        r.kind = RegionKind::Code;
        r.detector = DetectorId::CodeMap;
        r.status = RegionStatus::Inferred;
        r.title = title;
        r.confidence = confidence;
        r.evidence.push_back(evidence);
        r.evidence.push_back(
            Evidence{"untouched", static_cast<uint16_t>(from), end - from + 1, "no access in the replay"});
        if (!context.Claim(std::move(r), static_cast<uint16_t>(from), static_cast<uint16_t>(end)))
            return false;
        for (uint32_t at = from; at <= end;)
        {
            CodeLine line;
            line.key = context.Key(static_cast<uint16_t>(at));
            line.z80 = static_cast<uint16_t>(at);
            for (uint32_t i = 0; i < 4; ++i)
                line.bytes[i] = context.Start(static_cast<uint16_t>(at + i));
            line.length = DecodeOp(line.bytes).length;
            for (uint32_t i = line.length; i < 4; ++i)
                line.bytes[i] = 0;
            line.entry = handlerEntry[at] != 0;
            if (line.entry)
                context.out.entries.emplace_back(line.z80, std::string());
            inferredLines.push_back(line);
            at += line.length;
        }
        return true;
    };
    const auto unrun = [&](uint32_t lo, uint32_t hi) {
        if (hi - lo + 1 < kMinLeftover)
            return;
        Leftover best;
        uint32_t bestFrom = lo;
        for (uint32_t from = lo; from < lo + 4; ++from)
        {
            const Leftover l = DecodeLeftover(context, from, lo, hi);
            if (!l.undefined && l.landed > best.landed)
            {
                best = l;
                bestFrom = from;
            }
        }
        if (!CodeLike(best))
            return;
        // A table entry pointing at the stretch, or at the start the decode took (a byte or more of something else
        // before the routine)
        if (TableEntryPoint(context, lo) || (bestFrom != lo && TableEntryPoint(context, bestFrom)))
            return;
        // The decode from its start to its last whole instruction: the bytes before and after stay untouched
        claimCode(bestFrom, best.end, "Code that never ran, " + std::to_string(best.end - bestFrom + 1) + " bytes",
                  Evidence{"leftover-code", static_cast<uint16_t>(bestFrom), best.instructions,
                           std::to_string(best.instructions) + " instructions decode; " + std::to_string(best.landed) +
                               " of " + std::to_string(best.transfers) + " calls and jumps land on its own instructions (" +
                               std::to_string(best.inside) + "), in the ROM or on executed code"},
                  confidence::FromCount(500, 850, best.landed, 8));
    };
    // [entry, hi] from a handler table entry: inferred code up to its first unconditional transfer that no branch or
    // call before it goes past (or up to executed code right after the stretch) when that decodes as code by the
    // leftover test: the bytes after it are not the handler's. The last byte claimed, 0: none
    const auto handler = [&](uint32_t entry, uint32_t hi) -> uint32_t {
        const Leftover whole = DecodeLeftover(context, entry, entry, hi);
        uint32_t end = whole.firstStop != 0 ? whole.firstStop : whole.stop;
        if (whole.instructions != 0 && whole.end == hi && hi < 0xFFFF &&
            DetectorContext::Code(context.Use(static_cast<uint16_t>(hi + 1))))
            end = hi;  // it runs on into executed code
        if (end == 0 || end - entry + 1 < kMinLeftover)
            return 0;
        const Leftover l = end == whole.end ? whole : DecodeLeftover(context, entry, entry, end);
        if (!CodeLike(l))
            return 0;
        if (!claimCode(entry, end, std::string(),
                       Evidence{"handler-entry", static_cast<uint16_t>(entry), l.instructions,
                                "a handler table entry points here; " + std::to_string(l.instructions) +
                                    " instructions decode, " + std::to_string(l.landed) + " of " +
                                    std::to_string(l.transfers) + " calls and jumps land on its own instructions (" +
                                    std::to_string(l.inside) + "), in the ROM or on executed code"},
                       confidence::FromCount(500, 850, l.landed, 8)))
            return 0;
        return end;
    };
    const auto leftover = [&](uint32_t lo, uint32_t hi) {
        if (hi - lo + 1 < kMinLeftover)
            return;
        // Handlers from table entries first; the bytes before and after each tried as leftovers
        uint32_t rest = lo;
        for (uint32_t at = lo; at <= hi; ++at)
        {
            if (handlerEntry[at] == 0)
                continue;
            const uint32_t end = handler(at, hi);
            if (end == 0)
                continue;
            if (at > rest)
                unrun(rest, at - 1);
            rest = end + 1;
            at = end;
        }
        if (rest <= hi)
            unrun(rest, hi);
    };
    // Fills of one value in [a, b]; the stretches between them tried as code
    const auto fills = [&](uint32_t a, uint32_t b) {
        uint32_t rest = a;
        for (uint32_t s = a; s <= b;)
        {
            const uint8_t v = context.Start(static_cast<uint16_t>(s));
            uint32_t e = s;
            while (e + 1 <= b && context.Start(static_cast<uint16_t>(e + 1)) == v)
                ++e;
            if (e - s + 1 >= kMinFill)
            {
                if (s > rest)
                    leftover(rest, s - 1);
                char title[64];
                std::snprintf(title, sizeof(title), "Fill, %u bytes of $%02X", static_cast<unsigned>(e - s + 1), v);
                claim(s, e, title,
                      Evidence{"fill", static_cast<uint16_t>(s), e - s + 1, "one value, never read or written"},
                      confidence::FromCount(500, 850, e - s + 1, 256));
                rest = e + 1;
            }
            s = e + 1;
        }
        if (rest <= b)
            leftover(rest, b);
    };
    // ROM copies in [a, b]: the longest match of the ROM from each byte (its first 8 bytes a gram of the ROM), 32
    // bytes or more and not almost one value; the bytes between them go to the fills
    RomIndex rom;
    bool romBuilt = false;
    const auto romCopies = [&](uint32_t a, uint32_t b) {
        if (b - a + 1 >= kMinRomCopy && !romBuilt)
        {
            rom.Build(context);
            romBuilt = true;
        }
        if (b - a + 1 < kMinRomCopy || rom.bytes.empty())
        {
            fills(a, b);
            return;
        }
        uint32_t rest = a;
        for (uint32_t s = a; s + kMinRomCopy - 1 <= b;)
        {
            uint8_t here[kRomGram];
            for (uint32_t i = 0; i < kRomGram; ++i)
                here[i] = context.Start(static_cast<uint16_t>(s + i));
            const auto found = RomIndex::OneValue(here) ? rom.grams.end() : rom.grams.find(RomIndex::Gram(here));
            uint32_t best = 0, bestAt = 0;
            if (found != rom.grams.end())
                for (const uint16_t at : found->second)
                {
                    uint32_t n = 0;
                    while (s + n <= b && at + n < rom.bytes.size() &&
                           context.Start(static_cast<uint16_t>(s + n)) == rom.bytes[at + n])
                        ++n;
                    if (n > best)
                    {
                        best = n;
                        bestAt = at;
                    }
                }
            uint32_t values = 0, longest = 0;
            uint32_t fillEnd = 0;  // the end of the first run of one value of kMinFill bytes or more (in the match)
            if (best >= kMinRomCopy)
            {
                bool seen[256] = {};
                for (uint32_t i = 0, run = 0; i < best; ++i)
                {
                    const uint8_t v = rom.bytes[bestAt + i];
                    values += seen[v] ? 0 : 1;
                    seen[v] = true;
                    run = i != 0 && v == rom.bytes[bestAt + i - 1] ? run + 1 : 1;
                    longest = std::max(longest, run);
                    if (longest >= kMinFill && fillEnd == 0 &&
                        (i + 1 == best || rom.bytes[bestAt + i + 1] != v))
                        fillEnd = i;
                }
            }
            if (best < kMinRomCopy || values < kMinRomCopyValues || longest >= kMinFill)
            {
                // A match that runs into a fill: every start before the fill's end matches into it again (a long
                // stretch the same as the ROM would be matched once per byte); go on after it
                s = longest >= kMinFill ? s + fillEnd + 1 : s + 1;
                continue;
            }
            if (s > rest)
                fills(rest, s - 1);
            const uint32_t e = s + best - 1;
            char range[32];
            std::snprintf(range, sizeof(range), "$%04X-$%04X", static_cast<unsigned>(bestAt),
                          static_cast<unsigned>(bestAt + best - 1));
            const Evidence copied{"rom-copy", static_cast<uint16_t>(bestAt), best,
                                  std::string("the same bytes as the ROM at ") + range + ", never read or written"};
            // A copy of a ROM routine (a loader the program patches and calls later) is code the session did not run;
            // a copy of the ROM's data (its letters, BASIC's UDG) is a leftover
            const Leftover asCode = DecodeLeftover(context, s, s, e);
            if (!CodeLike(asCode) || asCode.end != e ||
                !claimCode(s, e, "ROM copy, code, " + std::to_string(best) + " bytes of " + range, copied,
                           confidence::FromCount(500, 850, best, 64)))
                claim(s, e, "ROM copy, " + std::to_string(best) + " bytes of " + range, copied,
                      confidence::FromCount(500, 850, best, 64));
            rest = e + 1;
            s = e + 1;
        }
        if (rest <= b)
            fills(rest, b);
    };
    // The stack: [lo, hi] from the lowest byte a push wrote (nothing else touched it) to the highest stack byte
    const auto claimStack = [&](uint32_t lo, uint32_t hi) {
        uint64_t bytes = 0;
        for (uint32_t i = lo; i <= hi; ++i)
            bytes += StackOnly(context.Use(static_cast<uint16_t>(i))) ? 1 : 0;
        Region r;
        r.kind = RegionKind::Unused;
        r.detector = DetectorId::CodeMap;
        r.status = RegionStatus::Observed;
        r.title = "Stack, " + std::to_string(hi - lo + 1) + " bytes";
        r.confidence = confidence::FromCount(500, 850, bytes, 8);
        r.evidence.push_back(Evidence{"stack", static_cast<uint16_t>(lo), bytes,
                                      std::to_string(bytes) +
                                          " bytes only pushed and popped (PUSH / CALL / RST / an interrupt, POP / RET), "
                                          "no other access"});
        context.Claim(std::move(r), static_cast<uint16_t>(lo), static_cast<uint16_t>(hi));
    };
    // The ROM's bytes are the signatures', the display file is the screen's
    uint32_t a = 0x5B00;
    while (a <= 0xFFFF)
    {
        if (!free(a))
        {
            ++a;
            continue;
        }
        uint32_t b = a;
        while (b + 1 <= 0xFFFF && ((b + 1) >> 14) == (a >> 14) && free(b + 1))
            ++b;
        // The stack first, then ROM copies, fills and leftovers in the rest
        uint32_t rest = a;
        for (uint32_t s = a; s <= b;)
        {
            if (!StackPushed(context.Use(static_cast<uint16_t>(s))))
            {
                ++s;
                continue;
            }
            uint32_t e = s;
            for (uint32_t t = s + 1; t <= b && t <= e + kStackGap + 1; ++t)
                if (StackOnly(context.Use(static_cast<uint16_t>(t))))
                    e = t;
            if (s > rest)
                romCopies(rest, s - 1);
            claimStack(s, e);
            rest = e + 1;
            s = e + 1;
        }
        if (rest <= b)
            romCopies(rest, b);
        a = b + 1;
    }
    if (!inferredLines.empty())
    {
        context.out.code.insert(context.out.code.end(), inferredLines.begin(), inferredLines.end());
        std::sort(context.out.code.begin(), context.out.code.end(),
                  [](const CodeLine& x, const CodeLine& y) { return x.key < y.key; });
    }
}

}  // namespace analysis
