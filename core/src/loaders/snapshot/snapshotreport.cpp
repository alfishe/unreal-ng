#include "snapshotreport.h"

#include <sstream>

namespace snapshot
{
const char* ToText(Outcome outcome)
{
    switch (outcome)
    {
        case Outcome::Applied: return "applied";
        case Outcome::Approximated: return "approximated";
        case Outcome::Ignored: return "ignored";
        case Outcome::Refused: return "refused";
        case Outcome::Unknown: return "unknown";
    }
    return "?";
}

void Report::Add(std::string item, Outcome outcome, std::string note)
{
    items.push_back(ReportItem{std::move(item), outcome, std::move(note)});
}

void Report::Refuse(std::string why, std::string needsHint)
{
    refused = true;
    reason = std::move(why);
    needs = std::move(needsHint);
}

std::string Report::ToText() const
{
    std::ostringstream out;
    out << format << " snapshot";
    if (!machineHint.empty())
        out << " (made on " << machineHint << ")";
    out << ": ";
    if (refused)
        out << "refused - " << reason;
    else
        out << "loaded by the " << commit << " commit";
    out << '\n';
    for (const std::string& v : verdicts)
        out << "  verdict: " << v << '\n';
    for (const ReportItem& item : items)
    {
        out << "  " << item.item << ": " << snapshot::ToText(item.outcome);
        if (!item.note.empty())
            out << " - " << item.note;
        out << '\n';
    }
    for (const std::string& w : warnings)
        out << "  warning: " << w << '\n';
    return out.str();
}

StateNode Report::ToStateNode() const
{
    StateNode n = StateNode::Object();
    n["format"] = format;
    n["machine_hint"] = machineHint;
    n["commit"] = commit;
    n["refused"] = refused;
    if (refused)
    {
        n["reason"] = reason;
        if (!needs.empty())
            n["needs"] = needs;
    }
    StateNode verdictList = StateNode::Array();
    for (const std::string& v : verdicts)
        verdictList.push(v);
    n["verdicts"] = verdictList;
    StateNode itemList = StateNode::Array();
    for (const ReportItem& item : items)
    {
        StateNode i = StateNode::Object();
        i["item"] = item.item;
        i["outcome"] = snapshot::ToText(item.outcome);
        if (!item.note.empty())
            i["note"] = item.note;
        itemList.push(i);
    }
    n["items"] = itemList;
    StateNode warningList = StateNode::Array();
    for (const std::string& w : warnings)
        warningList.push(w);
    n["warnings"] = warningList;
    return n;
}
}  // namespace snapshot
