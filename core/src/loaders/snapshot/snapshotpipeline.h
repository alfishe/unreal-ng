#pragma once

/// @file snapshotpipeline.h
/// @brief The plan step of the snapshot pipeline (PLAN #84): between a format reader's staging and the commit, ask who
/// writes the machine and whether the load is allowed. P1 (this file): the road exists and the report is filled, the
/// answer is always today's commit ("legacy"). P2 adds the caller's and the machine's policies, P5 the fit checks.
/// Design: docs/inprogress/2026-10-02-snapshot-pipeline/proposal.md section 4.4.

#include <string>

#include "loaders/snapshot/snapshotimage.h"
#include "loaders/snapshot/snapshotreport.h"

class EmulatorContext;

namespace snapshot
{
/// What the caller asked for (P3 fills it from the `commit` option of every surface)
struct Options
{
    std::string commit;   ///< "" = let the plan decide, "legacy" = force today's commit, else a registered policy name
};

class Pipeline
{
public:
    /// Fill `report` from the image and decide. True = commit (with report.commit); false = refused (report says why).
    /// The image may be rewritten by a transform on the way (none yet)
    static bool Plan(Image& image, EmulatorContext* context, const Options& options, Report& report);
};
}  // namespace snapshot
