#include "debugger/analyzers/analysis/l3review.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

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

const char* const kReasons[] = {"bug", "conflict", "unnamed-hot", "low-confidence", "unknown"};

bool Overlaps(const std::vector<ExportRange>& ranges, const Finding& f)
{
    if (ranges.empty())
        return f.end >= 0x4000;
    for (const ExportRange& r : ranges)
        if (f.end >= r.start && f.start <= r.end)
            return true;
    return false;
}

bool Decided(const Finding& f)
{
    return !f.verdict.empty() || !f.label.empty() || !f.title.empty() || !f.comment.empty() ||
           f.status == FindingStatus::Confirmed || f.status == FindingStatus::Refuted;
}

/// The priority of a finding in the queue (-1: it needs no decision)
int Priority(const Finding& f, const L2Result& l2)
{
    const std::vector<Region>& regions = l2.store.Regions();
    switch (f.kind)
    {
        case FindingKind::Bug:
            return 0;
        case FindingKind::Conflict:
        {
            if (f.region < 0 || f.other < 0 || static_cast<size_t>(f.other) >= regions.size())
                return -1;
            const Region& a = regions[static_cast<size_t>(f.region)];
            const Region& b = regions[static_cast<size_t>(f.other)];
            if (IsFallbackClaim(a) || IsFallbackClaim(b))
                return -1;
            const int gap = std::abs(static_cast<int>(a.confidence) - static_cast<int>(b.confidence));
            return gap <= kConflictGap ? 1 : -1;
        }
        case FindingKind::Routine:
            return 2;
        case FindingKind::Claim:
        {
            if (f.region < 0 || static_cast<size_t>(f.region) >= regions.size())
                return -1;
            const Region& r = regions[static_cast<size_t>(f.region)];
            if (IsFallbackClaim(r) || r.confidence >= confidence::kReview)
                return -1;
            return 3;
        }
        case FindingKind::Unknown:
            return 4;
        default:
            return -1;
    }
}

std::string Clip(std::string text, size_t max)
{
    if (text.size() <= max)
        return text;
    text.resize(max > 4 ? max - 4 : 0);
    return text + " ...";
}

}  // namespace

ReviewQueue SelectReview(const std::vector<Finding>& findings, const L2Result& l2, const ReviewOptions& options)
{
    ReviewQueue queue;
    std::vector<ReviewItem> all;
    for (size_t i = 0; i < findings.size(); ++i)
    {
        const Finding& f = findings[i];
        if (!Overlaps(options.ranges, f))
            continue;
        const int priority = Priority(f, l2);
        if (priority < 0)
            continue;
        if (Decided(f))
        {
            ++queue.resolved;
            continue;
        }
        ReviewItem item;
        item.finding = i;
        item.id = f.id;
        item.priority = static_cast<uint32_t>(priority);
        item.reason = kReasons[priority];
        ++queue.byReason[priority];
        all.push_back(item);
    }
    std::stable_sort(all.begin(), all.end(), [&](const ReviewItem& a, const ReviewItem& b) {
        if (a.priority != b.priority)
            return a.priority < b.priority;
        const Finding& fa = findings[a.finding];
        const Finding& fb = findings[b.finding];
        if (fa.start != fb.start)
            return fa.start < fb.start;
        return a.finding < b.finding;
    });
    queue.total = static_cast<uint32_t>(all.size());
    if (all.size() > options.limit)
    {
        uint32_t left[5] = {};
        for (size_t i = options.limit; i < all.size(); ++i)
            ++left[all[i].priority];
        std::string parts;
        for (int p = 0; p < 5; ++p)
            if (left[p] != 0)
                parts += (parts.empty() ? "" : ", ") + std::to_string(left[p]) + " " + kReasons[p];
        queue.summary = std::to_string(all.size() - options.limit) + " more items (" + parts +
                        "): resolve the shown ones first, or narrow the ranges";
        all.resize(options.limit);
    }
    queue.items = std::move(all);
    return queue;
}

std::string ReviewSheet(const Finding& f, const ReviewItem& item, const L1Result& l1, const L2Result& l2,
                        const ReviewTools& tools)
{
    (void)l2;
    std::string sheet = f.id + " " + FindingKindName(f.kind) + " " + Hex4(f.start) +
                        (f.end != f.start ? "-" + Hex4(f.end) : std::string()) + ", " + FindingStatusName(f.status) +
                        ", confidence " + std::to_string(f.confidence) + " (" + item.reason + ")\n";
    sheet += Clip(f.claim, 300) + "\n";
    for (size_t i = 0; i < f.evidence.size() && i < 4; ++i)
        sheet += "  - " + Clip(f.evidence[i], 110) + "\n";

    // The bytes at the start (the start's image)
    std::string bytes;
    const uint32_t count = std::min<uint32_t>(16, static_cast<uint32_t>(f.end - f.start) + 1u);
    for (uint32_t i = 0; i < count; ++i)
    {
        uint8_t v = 0;
        if (!l1.StartByte(l1.StartKey(static_cast<uint16_t>(f.start + i)), v))
            break;
        char hex[4];
        std::snprintf(hex, sizeof(hex), "%02X", v);
        bytes += (bytes.empty() ? "" : " ") + std::string(hex);
    }
    if (!bytes.empty())
        sheet += "bytes " + Hex4(f.start) + ": " + bytes + "\n";

    // The code: a routine's L1 sheet, or a few instructions at the start (or at the instruction a bug names)
    if (f.kind == FindingKind::Routine && tools.routineSheet)
    {
        const std::string routine = tools.routineSheet(f.at);
        const size_t room = sheet.size() < kReviewSheetBytes - 160 ? kReviewSheetBytes - 160 - sheet.size() : 0;
        sheet += Clip(routine, room) + (routine.empty() || routine.back() == '\n' ? "" : "\n");
    }
    else if (tools.disassemble)
    {
        const uint16_t at = f.at != 0 ? f.at : f.start;
        const Instruction* ins = l1.InstructionAt(l1.StartKey(at));
        uint16_t pc = at;
        for (int line = 0; line < 4 && ins != nullptr; ++line)
        {
            sheet += "  " + Hex4(pc) + "  " + tools.disassemble(ins->bytes, pc) + "\n";
            pc = static_cast<uint16_t>(pc + ins->length);
            ins = l1.InstructionAt(l1.StartKey(pc));
        }
    }
    const std::string resolve = "resolve: analysis review op=resolve id=" + f.id +
                                " verdict=ok|wrong|skip [label=Name title=\"...\" comment=\"...\"] or attach an "
                                "experiment (analysis experiment, finding=" + f.id + ")\n";
    if (sheet.size() + resolve.size() > kReviewSheetBytes)
        sheet = Clip(sheet, kReviewSheetBytes - resolve.size() - 1) + "\n";
    return sheet + resolve;
}

StateNode ReviewItemNode(const Finding& finding, const ReviewItem& item, const std::string& state)
{
    StateNode node = StateNode::Object();
    node["id"] = item.id;
    node["reason"] = item.reason;
    node["state"] = state;
    node["kind"] = FindingKindName(finding.kind);
    node["start"] = Hex4(finding.start);
    node["end"] = Hex4(finding.end);
    node["confidence"] = static_cast<uint64_t>(finding.confidence);
    node["claim"] = finding.claim;
    return node;
}

}  // namespace analysis
