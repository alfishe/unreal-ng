#include "debugger/analyzers/analysis/l3findings.h"

#include <algorithm>
#include <cstdio>

#include "debugger/analyzers/analysis/l2export.h"
#include "debugger/analyzers/analysis/l3bugs.h"
#include "emulator/state/statenodejson.h"

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

std::string Range(uint16_t start, uint16_t end)
{
    return start == end ? Hex4(start) : Hex4(start) + "-" + Hex4(end);
}

/// "12.3%"
std::string Percent(uint64_t part, uint64_t whole)
{
    if (whole == 0)
        return "0%";
    const uint64_t tenths = part * 1000 / whole;
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "%";
}

std::string EvidenceLine(const Evidence& ev)
{
    std::string line = ev.what;
    if (ev.z80 != 0)
        line += " " + Hex4(ev.z80);
    if (ev.count != 0)
        line += " x" + std::to_string(ev.count);
    if (!ev.detail.empty())
        line += ": " + ev.detail;
    return line;
}

/// Markdown table cell: no pipes, no line breaks; '$' before a digit stays (zx-reaper reads $XXXX refs)
std::string Cell(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
    {
        if (c == '|')
            out += "/";
        else if (c == '\n' || c == '\r')
            out += "; ";
        else
            out += c;
    }
    return out;
}

}  // namespace

const char* FindingKindName(FindingKind kind)
{
    switch (kind)
    {
        case FindingKind::Claim:
            return "claim";
        case FindingKind::Conflict:
            return "conflict";
        case FindingKind::Bug:
            return "bug";
        case FindingKind::Routine:
            return "routine";
        case FindingKind::Unknown:
            return "unknown";
        case FindingKind::Coverage:
            return "coverage";
        case FindingKind::Note:
            return "note";
        case FindingKind::Experiment:
            return "experiment";
        case FindingKind::Entry:
            return "entry";
    }
    return "claim";
}

bool ParseFindingKind(const std::string& text, FindingKind& kind)
{
    for (int k = 0; k <= static_cast<int>(FindingKind::Entry); ++k)
        if (text == FindingKindName(static_cast<FindingKind>(k)))
        {
            kind = static_cast<FindingKind>(k);
            return true;
        }
    return false;
}

const char* FindingStatusName(FindingStatus status)
{
    switch (status)
    {
        case FindingStatus::Observed:
            return "observed";
        case FindingStatus::Inferred:
            return "inferred";
        case FindingStatus::Refuted:
            return "refuted";
        case FindingStatus::Confirmed:
            return "confirmed";
    }
    return "inferred";
}

bool ParseFindingStatus(const std::string& text, FindingStatus& status)
{
    for (int s = 0; s <= static_cast<int>(FindingStatus::Confirmed); ++s)
        if (text == FindingStatusName(static_cast<FindingStatus>(s)))
        {
            status = static_cast<FindingStatus>(s);
            return true;
        }
    return false;
}

std::string Finding::Key() const
{
    return std::string(FindingKindName(kind)) + ":" + subject + ":" + Range(start, end) + ":" + source;
}

bool IsFallbackClaim(const Region& region)
{
    return region.kind == RegionKind::Untouched ||
           (region.detector == DetectorId::CodeMap && region.kind != RegionKind::Code);
}

std::vector<uint8_t> ClaimLevels(const L2Result& l2)
{
    std::vector<uint8_t> levels(0x10000, 0);
    for (const Region& r : l2.store.Regions())
    {
        const uint8_t level = IsFallbackClaim(r) ? 1 : 2;
        for (uint32_t a = r.z80; a <= r.Z80End() && a <= 0xFFFF; ++a)
            levels[a] = std::max(levels[a], level);
    }
    return levels;
}

std::vector<Finding> BaseFindings(const L1Result& l1, const L2Result& l2)
{
    std::vector<Finding> out;
    const std::vector<Region>& regions = l2.store.Regions();

    // Claims, in the store's order: claim i is F(i + 1)
    for (size_t i = 0; i < regions.size(); ++i)
    {
        const Region& r = regions[i];
        Finding f;
        f.kind = FindingKind::Claim;
        f.status = r.status == RegionStatus::Observed ? FindingStatus::Observed : FindingStatus::Inferred;
        f.start = r.z80;
        f.end = r.Z80End();
        f.subject = RegionKindName(r.kind);
        f.source = DetectorName(r.detector);
        f.confidence = r.confidence;
        f.claim = Range(f.start, f.end) + " is " + RegionKindName(r.kind) + ": " + RegionTitle(r);
        if (!r.label.empty())
            f.claim += " (" + r.label + ")";
        for (const Evidence& ev : r.evidence)
        {
            if (f.evidence.size() == 8)
                break;
            f.evidence.push_back(EvidenceLine(ev));
        }
        for (const uint16_t user : r.users)
        {
            if (f.refs.size() == 8)
                break;
            f.refs.push_back(Hex4(user));
        }
        f.region = static_cast<int32_t>(i);
        out.push_back(std::move(f));
    }

    // Conflicts
    for (const RegionStore::Conflict& c : l2.store.Conflicts())
    {
        const Region& a = regions[c.a];
        const Region& b = regions[c.b];
        Finding f;
        f.kind = FindingKind::Conflict;
        f.status = FindingStatus::Inferred;
        f.start = std::max(a.z80, b.z80);
        f.end = std::min(a.Z80End(), b.Z80End());
        f.subject = std::string(RegionKindName(a.kind)) + "/" + RegionKindName(b.kind);
        f.source = std::string(DetectorName(a.detector)) + "/" + DetectorName(b.detector);
        f.confidence = std::min(a.confidence, b.confidence);
        f.claim = Range(f.start, f.end) + " is claimed " + RegionKindName(a.kind) + " (" + DetectorName(a.detector) +
                  ", " + std::to_string(a.confidence) + ") and " + RegionKindName(b.kind) + " (" +
                  DetectorName(b.detector) + ", " + std::to_string(b.confidence) + "): they cannot both hold";
        f.evidence.push_back("F" + std::to_string(c.a + 1) + ": " + RegionTitle(a));
        f.evidence.push_back("F" + std::to_string(c.b + 1) + ": " + RegionTitle(b));
        f.refs.push_back("F" + std::to_string(c.a + 1));
        f.refs.push_back("F" + std::to_string(c.b + 1));
        f.region = static_cast<int32_t>(c.a);
        f.other = static_cast<int32_t>(c.b);
        out.push_back(std::move(f));
    }

    // Candidate bugs
    for (Finding& f : CandidateBugs(l1, l2))
        out.push_back(std::move(f));

    // Unnamed hot routines
    const uint64_t totalT = l1.replay.tstates;
    const uint64_t frames = std::max<uint64_t>(1, l1.replay.frames);
    for (const Routine& r : l1.routines)
    {
        std::string name;
        for (const auto& [z80, entryName] : l2.entries)
            if (z80 == r.z80)
                name = entryName;
        if (!name.empty() && name.rfind("sub_", 0) != 0)
            continue;
        if (r.z80 < 0x4000)
            continue;  // ROM routines have the signature catalog
        const RoutineProfile& p = r.profile;
        const bool hot = (totalT != 0 && p.tstatesInclusive * 1000 >= totalT * kHotPermille) ||
                         p.entries >= frames * kHotEntriesPerFrame;
        if (!hot)
            continue;
        Finding f;
        f.kind = FindingKind::Routine;
        f.status = FindingStatus::Observed;
        f.start = r.z80;
        for (const uint32_t b : r.blocks)
            f.end = std::max(f.end, l1.blocks[b].z80End);
        if (f.end < f.start)
            f.end = f.start;
        f.subject = "unnamed-hot";
        f.source = "l1";
        f.confidence = 500;
        f.at = r.z80;
        f.claim = "The routine at " + Hex4(r.z80) + " has no name: " + Percent(p.tstatesInclusive, totalT) +
                  " of the time with its callees, entered " + std::to_string(p.entries) + " times in " +
                  std::to_string(p.activeFrames) + " of " + std::to_string(frames) + " frames";
        f.evidence.push_back(std::to_string(r.instructions) + " instructions, " + EntryKindNames(r.kinds) +
                             ", frames " + std::to_string(p.firstFrame) + ".." + std::to_string(p.lastFrame));
        out.push_back(std::move(f));
    }

    // Unknown areas: runs only fallback claims cover
    const std::vector<uint8_t> levels = ClaimLevels(l2);
    uint32_t a = 0;
    while (a <= 0xFFFF)
    {
        if (levels[a] != 1)
        {
            ++a;
            continue;
        }
        uint32_t b = a;
        while (b + 1 <= 0xFFFF && levels[b + 1] == 1 && ((b + 1) >> 14) == (a >> 14))
            ++b;
        if (b - a + 1 >= kUnknownMinBytes)
        {
            uint64_t reads = 0, writes = 0, touched = 0;
            for (uint32_t i = a; i <= b; ++i)
                if (const ByteUse* u = l1.UseAt(l1.StartKey(static_cast<uint16_t>(i))))
                {
                    reads += u->reads;
                    writes += u->writes;
                    touched += (u->flags != 0) ? 1 : 0;
                }
            Finding f;
            f.kind = FindingKind::Unknown;
            f.status = FindingStatus::Inferred;
            f.start = static_cast<uint16_t>(a);
            f.end = static_cast<uint16_t>(b);
            f.subject = touched == 0 ? "untouched" : "data";
            f.source = "code_map";
            f.confidence = touched == 0 ? 100 : 300;
            f.claim = Range(f.start, f.end) + " (" + std::to_string(b - a + 1) + " bytes): no detector explains it" +
                      (touched == 0 ? ", nothing accessed it in the session"
                                    : ", " + std::to_string(touched) + " bytes accessed as plain data");
            f.evidence.push_back(std::to_string(reads) + " reads, " + std::to_string(writes) + " writes");
            out.push_back(std::move(f));
        }
        a = b + 1;
    }

    // Entry candidates: jump targets that may start a routine of their own
    for (const EntryCandidate& c : l2.entryCandidates)
    {
        Finding f;
        f.kind = FindingKind::Entry;
        f.status = FindingStatus::Inferred;
        f.start = f.end = c.z80;
        f.subject = c.rule;
        f.source = "code_map";
        f.confidence = 400;
        f.at = c.z80;
        f.claim = Hex4(c.z80) + " may start a routine of its own (" + c.rule + "): " + c.why;
        std::string sites;
        for (size_t i = 0; i < c.sources.size() && i < 8; ++i)
            sites += (i ? " " : "") + Hex4(c.sources[i]);
        f.evidence.push_back("jumped to from " + sites);
        for (size_t i = 0; i < c.sources.size() && f.refs.size() < 8; ++i)
            f.refs.push_back(Hex4(c.sources[i]));
        out.push_back(std::move(f));
    }

    for (size_t i = 0; i < out.size(); ++i)
        out[i].id = "F" + std::to_string(i + 1);
    return out;
}

StateNode FindingNode(const Finding& f)
{
    StateNode node = StateNode::Object();
    node["id"] = f.id;
    node["kind"] = FindingKindName(f.kind);
    node["status"] = FindingStatusName(f.status);
    node["start"] = Hex4(f.start);
    node["end"] = Hex4(f.end);
    node["subject"] = f.subject;
    node["source"] = f.source;
    node["confidence"] = static_cast<uint64_t>(f.confidence);
    node["claim"] = f.claim;
    StateNode evidence = StateNode::Array();
    for (const std::string& e : f.evidence)
        evidence.push(e);
    node["evidence"] = evidence;
    StateNode refs = StateNode::Array();
    for (const std::string& r : f.refs)
        refs.push(r);
    node["refs"] = refs;
    if (!f.label.empty())
        node["label"] = f.label;
    if (!f.title.empty())
        node["title"] = f.title;
    if (!f.comment.empty())
        node["comment"] = f.comment;
    if (!f.verdict.empty())
        node["verdict"] = f.verdict;
    if (!f.by.empty())
        node["by"] = f.by;
    return node;
}

std::string FindingRow(const Finding& f)
{
    char head[96];
    std::snprintf(head, sizeof(head), "%-5s %-9s %04X-%04X %-10s %-14s %4u ", f.id.c_str(), FindingStatusName(f.status),
                  f.start, f.end, FindingKindName(f.kind), f.subject.substr(0, 14).c_str(),
                  static_cast<unsigned>(f.confidence));
    std::string row = head + f.claim;
    if (!f.label.empty())
        row += " [" + f.label + "]";
    return row;
}

std::string FindingsMarkdown(const std::vector<Finding>& findings, const std::string& title)
{
    std::string out = "# Findings\n\n";
    if (!title.empty())
        out += title + "\n\n";
    out += "Every claim about the game with its status and evidence. `observed`: seen in the simulator, a byte check "
           "or a test; `inferred`: read from code, not run; `unknown`: open; `refuted`: shown wrong (keep it). Only "
           "observed evidence can make a claim observed. Edit through `tools/zxq.py finding`.\n\n"
           "| ID | Status | Claim | Evidence | Refs |\n| --- | --- | --- | --- | --- |\n";
    for (const Finding& f : findings)
    {
        const char* status = f.status == FindingStatus::Confirmed ? "observed" : FindingStatusName(f.status);
        std::string evidence;
        for (const std::string& e : f.evidence)
            evidence += (evidence.empty() ? "" : "; ") + e;
        if (!f.comment.empty())
            evidence += (evidence.empty() ? "" : "; ") + ("review: " + f.comment);
        std::string refs;
        for (const std::string& r : f.refs)
            if (!r.empty() && r[0] == '$')
                refs += (refs.empty() ? "" : " ") + r;
        if (refs.empty())
            refs = Hex4(f.start);
        std::string claim = f.claim;
        if (!f.label.empty())
            claim += " (" + f.label + (f.title.empty() ? "" : ": " + f.title) + ")";
        out += "| " + f.id + " | " + status + " | " + Cell(claim) + " | " + Cell(evidence.empty() ? "-" : evidence) +
               " | " + Cell(refs) + " |\n";
    }
    return out;
}

std::string FindingsJsonl(const std::vector<Finding>& findings)
{
    std::string out;
    for (const Finding& f : findings)
        out += StateNodeToJsonText(FindingNode(f)) + "\n";
    return out;
}

}  // namespace analysis
