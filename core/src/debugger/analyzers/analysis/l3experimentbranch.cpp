#include "debugger/analyzers/analysis/l3experimentbranch.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>

#include "debugger/ttd/ttdcontrol.h"
#include "emulator/emulator.h"

namespace analysis
{

namespace
{

std::string Hex(uint32_t value, int digits)
{
    char text[8];
    std::snprintf(text, sizeof(text), "#%0*X", digits, value);
    return text;
}

/// A tag the branch table takes (1..31 of A-Z a-z 0-9 _ . -), or empty
std::string TagOf(const std::string& text)
{
    std::string tag;
    for (const char c : text)
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-')
            tag += c;
    return tag.size() > 31 ? tag.substr(0, 31) : tag;
}

bool Chosen(const ExperimentSpec& spec, const ExperimentResult& result, KeepBranches mode)
{
    if (spec.keepBranch)
        return true;
    if (mode == KeepBranches::All)
        return true;
    return mode == KeepBranches::Observed && result.verdict == "observed";
}

void KeepOne(Emulator& source, const ExperimentSpec& spec, ExperimentResult& result, const std::string& run)
{
    // The note first: the result's line before it names a branch
    std::string note = (spec.claim.empty() ? std::string() : spec.claim + "\n") + ExperimentResultLine(result);
    if (note.size() > 4000)
        note = note.substr(0, 4000) + " ...";
    ExperimentResult::KeptBranch& kept = result.kept;
    kept.asked = true;
    kept.run = run;
    if (result.verdict == "error")
    {
        kept.error = "the experiment did not run";
        return;
    }
    if (!spec.pins.empty())
    {
        kept.error = "a coverage experiment holds a byte before every instruction: no branch can replay that";
        return;
    }

    std::map<std::string, std::string> options;
    options["frame"] = std::to_string(result.fromFrame);
    options["tinframe"] = "0";
    options["name"] = !spec.branchName.empty() ? spec.branchName
                                               : "exp " + (spec.id.empty() ? std::string("?") : spec.id) + " (" + run + ")";
    options["reason"] = "experiment " + spec.id + ": " + result.verdict;
    options["note"] = note;
    std::string tags = "analyzer,experiment";
    if (const std::string finding = TagOf(result.finding); !finding.empty())
        tags += "," + finding;
    options["tags"] = tags;
    options["run"] = run;
    options["created_by"] = "analyzer";
    options["go_back"] = "true";
    options["media"] = "ask";  // a medium that cannot go back is the user's decision, not the analyzer's
    if (!spec.pokes.empty())
    {
        std::string pokes;
        for (const auto& [address, value] : spec.pokes)
            pokes += (pokes.empty() ? "" : ",") + Hex(address, 4) + "=" + std::to_string(value);
        options["poke"] = pokes;
    }
    if (!spec.regs.empty())
    {
        std::string regs;
        for (const auto& [name, value] : spec.regs)
            regs += (regs.empty() ? "" : ",") + name + "=" + std::to_string(value);
        options["regs"] = regs;
    }
    if (result.framesRun > 0)
    {
        options["run_frames"] = std::to_string(result.framesRun);
        const std::string script = ExperimentInputScript(spec, result.framesRun);
        if (!script.empty())
            options["input"] = script;
    }
    std::string reads;
    for (const auto& [address, value] : result.endMemory)
    {
        (void)value;
        reads += (reads.empty() ? "" : ",") + Hex(address, 4);
    }
    if (!reads.empty())
        options["read"] = reads;

    ttd::TTDControl control(source.GetContext());
    const ttd::TTDReply reply = control.Execute({"branch-create", options});
    if (!reply.Ok())
    {
        kept.error = reply.message;
        return;
    }
    if (const StateNode* decision = reply.body.find("needs_decision"); decision && decision->b)
    {
        kept.error = "a medium changed after frame " + std::to_string(result.fromFrame) +
                     ": create the branch with ttd branch-create and a media choice";
        return;
    }
    const StateNode* branch = reply.body.find("branch");
    if (branch == nullptr)
    {
        kept.error = "branch-create answered without the branch";
        return;
    }
    kept.created = true;
    if (const StateNode* id = branch->find("id"))
        kept.id = static_cast<uint32_t>(id->i);
    if (const StateNode* guid = branch->find("guid"))
        kept.guid = guid->s;
    if (const StateNode* name = branch->find("name"))
        kept.name = name->s;

    // The memory checks' bytes on the branch against the experiment's
    if (result.endMemory.empty())
        return;
    const StateNode* readNodes = reply.body.find("reads");
    kept.reproduced = 1;
    for (size_t i = 0; i < result.endMemory.size(); ++i)
    {
        const StateNode* item = readNodes && i < readNodes->items.size() ? &readNodes->items[i] : nullptr;
        const StateNode* bytes = item ? item->find("bytes") : nullptr;
        if (bytes == nullptr || bytes->items.empty())
        {
            kept.reproduced = -1;
            kept.mismatch = "the branch's bytes could not be read";
            return;
        }
        const auto onBranch = static_cast<uint8_t>(bytes->items[0].i);
        if (onBranch != result.endMemory[i].second)
        {
            kept.reproduced = 0;
            kept.mismatch += (kept.mismatch.empty() ? "" : ", ") + Hex(result.endMemory[i].first, 4) + ": " +
                             Hex(result.endMemory[i].second, 2) + " in the experiment, " + Hex(onBranch, 2) +
                             " on the branch";
        }
    }
}

}  // namespace

bool ParseKeepBranches(const std::string& text, KeepBranches& mode)
{
    if (text.empty() || text == "asked")
        mode = KeepBranches::Asked;
    else if (text == "observed")
        mode = KeepBranches::Observed;
    else if (text == "all")
        mode = KeepBranches::All;
    else
        return false;
    return true;
}

std::string NextExperimentRun()
{
    static std::atomic<uint32_t> counter{0};
    return "experiment-" + std::to_string(++counter);
}

std::string ExperimentInputScript(const ExperimentSpec& spec, uint32_t frames)
{
    std::string script;
    for (const ExperimentInput& in : spec.input)
    {
        if (in.frame >= frames)
            continue;
        std::string item = std::to_string(in.frame) + ":";
        switch (in.type)
        {
            case ExperimentInput::Type::ReleaseAll:
                item += "release_all";
                break;
            case ExperimentInput::Type::Key:
                item += in.name + (in.press ? ":down" : ":up");
                break;
            case ExperimentInput::Type::Joystick:
            {
                // "joystick fire" -> "joy.fire"
                const size_t space = in.name.find(' ');
                item += "joy." + (space == std::string::npos ? in.name : in.name.substr(space + 1)) +
                        (in.press ? ":down" : ":up");
                break;
            }
        }
        script += (script.empty() ? "" : ",") + item;
    }
    return script;
}

uint32_t KeepExperimentBranches(Emulator& source, const std::vector<ExperimentSpec>& specs, ExperimentBatch& batch,
                                KeepBranches mode, const std::string& run)
{
    uint32_t created = 0;
    for (size_t i = 0; i < batch.results.size() && i < specs.size(); ++i)
    {
        if (!Chosen(specs[i], batch.results[i], mode))
            continue;
        KeepOne(source, specs[i], batch.results[i], run);
        created += batch.results[i].kept.created ? 1 : 0;
    }
    return created;
}

}  // namespace analysis
