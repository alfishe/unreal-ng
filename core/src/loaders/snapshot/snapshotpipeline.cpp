#include "snapshotpipeline.h"

namespace snapshot
{
bool Pipeline::Plan(Image& image, EmulatorContext* /*context*/, const Options& options, Report& report)
{
    report.format = image.format;
    report.machineHint = image.machineHint;
    report.warnings.insert(report.warnings.end(), image.warnings.begin(), image.warnings.end());

    // P1: nobody intervenes. The caller's policies (P2/P3), the machine's (P2) and the fit checks (P5) come here,
    // in this order (proposal 4.4); until they exist every load commits the way it always has
    if (options.commit.empty() || options.commit == "legacy")
    {
        report.commit = "legacy";
        report.verdicts.push_back(options.commit.empty() ? "no caller, machine or fit-check intervention: legacy commit"
                                                         : "caller asked for the legacy commit");
        return true;
    }

    report.commit = options.commit;
    report.Refuse("no snapshot commit policy named '" + options.commit + "'; known: legacy");
    report.verdicts.push_back("caller asked for '" + options.commit + "': unknown");
    return false;
}
}  // namespace snapshot
