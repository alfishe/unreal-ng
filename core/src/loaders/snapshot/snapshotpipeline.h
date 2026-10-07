#pragma once

/// @file snapshotpipeline.h
/// @brief The plan step of the snapshot pipeline (PLAN #84): between a format reader's staging and the commit, ask who
/// writes the machine and whether the load is allowed. The order (proposal 4.4):
///   1. the caller named a commit        -> "legacy", or that registered policy (it may still Refuse); unknown = refused
///   2. the machine has a policy         -> its verdict: Take, Decline (go on) or Refuse
///   3. the shared fit checks            -> the memory the snapshot carries must exist on the machine (P5)
///   4. nobody intervened                -> the legacy commit, the code that commits today
/// Design: docs/inprogress/2026-10-02-snapshot-pipeline/proposal.md section 4.4.

#include <functional>
#include <string>

#include "loaders/snapshot/snapshotimage.h"
#include "loaders/snapshot/snapshotpolicy.h"
#include "loaders/snapshot/snapshotreport.h"

class EmulatorContext;

namespace snapshot
{
/// What the caller asked for (P3 fills it from the `commit` option of every surface)
struct Options
{
    std::string commit;   ///< "" = let the plan decide, "legacy" = force today's commit, else a registered policy name
    /// Called by the plan the moment it decides the snapshot WILL be committed (a Take or a Legacy decision, never a refusal),
    /// before the loader writes anything. The emulator ends the TTD recording session here, so a load that is refused (a model
    /// that does not fit, a bank the machine lacks, a Sprinter outside a ZX mode) never ends it
    std::function<void()> beforeCommit;
};

/// The plan's answer
struct Decision
{
    enum class Action : uint8_t
    {
        Legacy,   ///< the loader commits the way it always has
        Take,     ///< `policy` commits
        Refuse,   ///< nothing is written; the report says why
    };

    Action action = Action::Legacy;
    ISnapshotCommitPolicy* policy = nullptr;   ///< Action::Take only

    bool Proceeds() const { return action != Action::Refuse; }
    /// Run the policy's commit (Action::Take); records the failure in the report
    bool Commit(const Image& image, EmulatorContext& context, Report& report) const;
};

class Pipeline
{
public:
    /// Fill `report` from the image and decide. The image is not changed (no transform verdict yet)
    /// A decision that proceeds (Take, Legacy) also calls options.beforeCommit, once, before the caller writes anything
    static Decision Plan(const Image& image, EmulatorContext* context, const Options& options, Report& report);

private:
    static Decision PlanImpl(const Image& image, EmulatorContext* context, const Options& options, Report& report);
};
}  // namespace snapshot
