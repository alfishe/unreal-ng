/// The code map detector: opcode and operand bytes the replay executed are code (observed); code reached from them
/// over a branch never taken, a call never made or the fall-through of a conditional never seen is decoded from the
/// memory the replay started from and claimed as inferred code, until it meets executed code, an unconditional
/// transfer, or a byte the code used as data. Code that was written while it was code is self-modifying. Routine
/// entries are L1's, plus the targets of calls in inferred code, plus the jump targets after an unconditional
/// transfer that only a tail jump or a backward branch reaches (JumpEntries), plus conditional tail calls
/// (TailEntries). Jump targets that look like a routine start but are not proven one become entry candidates for the
/// review queue (TailEntries, PrologueCandidates).

#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <set>

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
            if ((context.Use(static_cast<uint16_t>(z80 + i)).flags & use::kSmc) != 0)
                line.smc = true;
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
    // detectors refine; the stack counts as untouched)
    const auto classOf = [&](uint32_t at) -> int {
        const auto z80 = static_cast<uint16_t>(at);
        if (!context.InScope(z80) || isCode[at] != 0)
            return 0;
        return DetectorContext::Data(context.Use(z80)) ? 1 : 2;
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
        if (cls == 1)
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

}  // namespace analysis
