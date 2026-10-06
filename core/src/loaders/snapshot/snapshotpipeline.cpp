#include "snapshotpipeline.h"

#include <algorithm>

#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"

namespace snapshot
{
namespace
{
/// One policy's verdict into the report and the decision; `asked` says who asked ("caller", "machine")
Decision Ask(ISnapshotCommitPolicy& policy, const char* asked, const Image& image, EmulatorContext& context,
             Report& report, bool mustTake)
{
    const Verdict verdict = policy.Examine(image, context);
    const char* names[] = {"declined", "takes it", "refuses"};
    report.verdicts.push_back(std::string(asked) + " policy '" + policy.Name() + "' " +
                              names[static_cast<int>(verdict.kind)] + (verdict.reason.empty() ? "" : ": " + verdict.reason));

    Decision decision;
    switch (verdict.kind)
    {
        case Verdict::Kind::Take:
            decision.action = Decision::Action::Take;
            decision.policy = &policy;
            report.commit = policy.Name();
            break;
        case Verdict::Kind::Refuse:
            decision.action = Decision::Action::Refuse;
            report.commit = policy.Name();
            report.Refuse(verdict.reason, verdict.needs);
            break;
        case Verdict::Kind::Decline:
            // A commit the caller asked for by name that does not apply is an error, not a silent fallback
            if (mustTake)
            {
                decision.action = Decision::Action::Refuse;
                report.commit = policy.Name();
                report.Refuse("the '" + policy.Name() + "' commit does not apply to this snapshot" +
                              (verdict.reason.empty() ? "" : ": " + verdict.reason));
            }
            break;
    }
    return decision;
}

/// The shared fit check (proposal 4.5): does the machine have the memory the snapshot carries? A refusal names the bank
/// and what would work; nothing is written. SPG (physical addresses), SZX (its own exact model check) and the Sprinter
/// (its policy) are not judged here
bool DoesNotFit(const Image& image, const EmulatorContext& context, Report& report)
{
    // SPG has its own machine (TS-Conf); SZX refuses any model but its own with both named (LoaderSZX::Commit)
    if (image.memoryModel == MemoryModel::Physical || image.banks.empty() || image.format == "szx")
        return false;
    uint16_t highest = 0;
    for (const auto& bank : image.banks)
        highest = std::max(highest, bank.first);

    const CONFIG& config = context.config;
    const std::string madeOn = image.machineHint.empty() || image.machineHint == "unknown" ? std::string("a 128K machine")
                                                                                          : image.machineHint;
    if (config.mem_model == MM_SPECTRUM48)
    {
        // A 48K has banks 5, 2, 0 only and no paging: a 48K file is all it can hold
        if (image.memoryModel == MemoryModel::Mem48k)
            return false;
        report.Refuse("this is a 128K snapshot (it holds Spectrum bank " + std::to_string(highest) + ", made on " + madeOn +
                          "); a ZX Spectrum 48K has no such memory. Load it on a 128K machine or a Pentagon",
                      "model:128K");
        return true;
    }
    const unsigned banks = config.ramsize >> 4;   // 16 KB pages
    if (highest < banks)
        return false;
    report.Refuse("the snapshot holds Spectrum bank " + std::to_string(highest) + " (made on " + madeOn + "); this machine has " +
                      std::to_string(banks) + " banks (" + std::to_string(config.ramsize) + " KB of RAM). Load it on a machine with at least " +
                      std::to_string((highest + 1) * 16) + " KB",
                  "ram:" + std::to_string((highest + 1) * 16) + "K");
    return true;
}
}  // namespace

bool Decision::Commit(const Image& image, EmulatorContext& context, Report& report) const
{
    if (action != Action::Take || !policy)
        return false;
    if (policy->Commit(image, context, report))
        return true;
    if (!report.refused)
        report.Refuse("the '" + policy->Name() + "' commit failed");
    return false;
}

Decision Pipeline::Plan(const Image& image, EmulatorContext* context, const Options& options, Report& report)
{
    report.format = image.format;
    report.machineHint = image.machineHint;
    report.warnings.insert(report.warnings.end(), image.warnings.begin(), image.warnings.end());

    // 0. A snapshot no machine can take (a SAM Coupe's, a Z80 with a ROM block): refused before anything is written
    if (!image.unsupported.empty())
    {
        report.commit = "none";
        report.verdicts.push_back("unsupported: " + image.unsupported);
        report.Refuse("this snapshot cannot be loaded: " + image.unsupported, "format:unsupported");
        return {Decision::Action::Refuse, nullptr};
    }

    // 1. The caller's choice
    if (options.commit == "legacy")
    {
        report.commit = "legacy";
        report.verdicts.push_back("caller asked for the legacy commit");
        return {};
    }
    if (!options.commit.empty())
    {
        ISnapshotCommitPolicy* named = SnapshotPolicies::Find(options.commit);
        if (!named)
        {
            std::string known;
            for (const std::string& name : SnapshotPolicies::Names())
                known += (known.empty() ? "" : ", ") + name;
            report.commit = options.commit;
            report.verdicts.push_back("caller asked for '" + options.commit + "': unknown");
            report.Refuse("no snapshot commit policy named '" + options.commit + "'; known: " + known);
            return {Decision::Action::Refuse, nullptr};
        }
        if (!context)
        {
            report.Refuse("no machine to commit into");
            return {Decision::Action::Refuse, nullptr};
        }
        return Ask(*named, "caller's", image, *context, report, true);
    }

    // 2. The machine's own policy
    if (context && context->pPortDecoder)
    {
        if (ISnapshotCommitPolicy* machine = context->pPortDecoder->GetSnapshotPolicy())
        {
            const Decision decision = Ask(*machine, "machine's", image, *context, report, false);
            if (decision.action != Decision::Action::Legacy)
                return decision;
        }
    }

    // 3. The shared fit checks
    if (context && DoesNotFit(image, *context, report))
    {
        report.commit = "none";
        report.verdicts.push_back("fit check: the snapshot does not fit this machine");
        return {Decision::Action::Refuse, nullptr};
    }

    // 4. Nobody intervened
    report.commit = "legacy";
    report.verdicts.push_back("no caller, machine or fit-check intervention: legacy commit");
    return {};
}
}  // namespace snapshot
