#include "snapshotpipeline.h"

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

    // 3. The shared fit checks: P5

    // 4. Nobody intervened
    report.commit = "legacy";
    report.verdicts.push_back("no caller, machine or fit-check intervention: legacy commit");
    return {};
}
}  // namespace snapshot
