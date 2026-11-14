#pragma once

/// @file l3experimentbranch.h
/// @brief Experiments kept as TTD branches (TTD branches surfaces.md §5.3, owner 2026-11-13): an experiment runs on
/// analysis instances (l3experiment.h) and leaves the source alone; one asked for with keep_branch (or every one, or
/// the observed ones, with the action's keep_branches) is then replayed in the source instance's TTD session as a
/// branch a human can open: `branch-create` at the experiment's start frame with its pokes and registers as tool
/// edits, its input as the input script, its frames as the run, created by `analyzer` under one run id
/// (`branch-prune {run}` removes them), and back where the machine stood. The memory checks' bytes are read on the
/// branch and compared with the experiment's: `reproduced`.
///
/// Not replayable: a coverage experiment's pins (a byte held before every instruction) - refused with the reason. A
/// medium changed after the start frame is not decided for the user: no branch, the reason says so.

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/analyzers/analysis/l3experiment.h"

class Emulator;

namespace analysis
{

enum class KeepBranches : uint8_t
{
    Asked,     ///< only the specs with keep_branch (default)
    Observed,  ///< and every observed one
    All,       ///< every experiment that ran
};

/// "observed" / "all" / "asked" (empty: Asked); false for another word
bool ParseKeepBranches(const std::string& text, KeepBranches& mode);

/// The run id the batch's branches share: "experiment-<n>", n counting up in the process
std::string NextExperimentRun();

/// The input script of branch-create for @p spec's input, up to @p frames ("0:q:down,10:q:up,5:joy.fire:down")
std::string ExperimentInputScript(const ExperimentSpec& spec, uint32_t frames);

/// Replay the chosen experiments of @p batch (matching @p specs by index) as branches of @p source's TTD session
/// under @p run; each result's `kept` says what happened. Returns how many branches were created
uint32_t KeepExperimentBranches(Emulator& source, const std::vector<ExperimentSpec>& specs, ExperimentBatch& batch,
                                KeepBranches mode, const std::string& run);

}  // namespace analysis
