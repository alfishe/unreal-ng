#pragma once

/// @file l3findings.h
/// @brief The findings register of L3 (docs/emulator/analysis/analysis-layers.md, "L3: review"): every L2 claim, every
/// conflict between two claims, the candidate bugs (l3bugs.h), the unnamed hot routines and the areas nothing claimed,
/// as findings with their evidence, a status (observed / inferred / refuted / confirmed), a deterministic id and a
/// fixed order. The review queue (l3review.h) picks the ones that need a decision; resolving one, an experiment
/// (l3experiment.h) or the coverage driver (l3coverage.h) writes back into the register and into the draft export.
///
/// The base register comes from one build: claims in the store's order, then conflicts, candidate bugs, hot routines,
/// unknown areas and entry candidates, each group in address order; ids F1.. in that order, so one recording gives the same ids on
/// every compiler. What the review adds (notes, coverage, experiments) follows, numbered on.
///
/// @code
///   std::vector<analysis::Finding> base = analysis::BaseFindings(l1, l2);
///   std::string md = analysis::FindingsMarkdown(base, "Game");   // zx-reaper re/findings.md
/// @endcode

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/analyzers/analysis/l1model.h"
#include "debugger/analyzers/analysis/l2model.h"
#include "emulator/state/statenode.h"

namespace analysis
{

enum class FindingKind : uint8_t
{
    Claim,       ///< an L2 region
    Conflict,    ///< two claims on one byte that cannot both hold
    Bug,         ///< a candidate bug (l3bugs.h)
    Routine,     ///< a routine L2 could not name that takes a large share of the time or runs every frame
    Unknown,     ///< a run of bytes only the code map's fallback claims cover (plain data, untouched)
    Coverage,    ///< a branch never taken in the session, and what flipped it (l3coverage.h)
    Note,        ///< added by an agent or a human
    Experiment,  ///< an experiment's result with no finding of its own
    Entry,       ///< a jump target that may start a routine of its own (L2Result::entryCandidates)
};
const char* FindingKindName(FindingKind kind);
bool ParseFindingKind(const std::string& text, FindingKind& kind);

enum class FindingStatus : uint8_t
{
    Observed,   ///< seen in the replay or in an experiment
    Inferred,   ///< derived, not seen directly
    Refuted,    ///< an experiment or a reviewer showed it wrong
    Confirmed,  ///< a reviewer accepted it
};
const char* FindingStatusName(FindingStatus status);
bool ParseFindingStatus(const std::string& text, FindingStatus& status);

struct Finding
{
    std::string id;                      ///< "F12"
    FindingKind kind = FindingKind::Claim;
    FindingStatus status = FindingStatus::Inferred;
    uint16_t start = 0;                  ///< the Z80 range it is about (the start's paging)
    uint16_t end = 0;                    ///< inclusive
    std::string subject;                 ///< claims: the region kind; bugs: the rule; others: what it is about
    std::string source;                  ///< the detector, "bugs", "l1", "code_map", "coverage", "experiment", "agent"
    uint16_t confidence = 0;             ///< 0..1000
    std::string claim;                   ///< one sentence
    std::vector<std::string> evidence;
    std::vector<std::string> refs;       ///< other findings ("F3") and addresses ("$92BA")
    std::string label;                   ///< what the review named it
    std::string title;
    std::string comment;
    std::string verdict;                 ///< "", ok, wrong, skip, observed, refuted
    std::string by;                      ///< who decided: agent, human, experiment, coverage
    int32_t region = -1;                 ///< claims: the region's index in the store; conflicts: the first claim
    int32_t other = -1;                  ///< conflicts: the second claim
    uint16_t at = 0;                     ///< the instruction / routine it names (0: none)

    /// The key a resolution is kept under across rebuilds (kind, range, subject, source)
    std::string Key() const;
};

/// Unknown areas: runs longer than this many bytes
constexpr uint32_t kUnknownMinBytes = 17;
/// Hot routines: at least this share of the replay's T-states (callees included), in thousandths ...
constexpr uint64_t kHotPermille = 10;
/// ... or entered at least once a frame on average
constexpr uint64_t kHotEntriesPerFrame = 1;

/// Whether a claim is one of the code map's fallbacks (plain data, untouched): it names no structure
bool IsFallbackClaim(const Region& region);

/// Per Z80 address (the start's paging): 0 no claim, 1 only fallback claims, 2 a specific claim
std::vector<uint8_t> ClaimLevels(const L2Result& l2);

/// The base register of one build (ids F1.. in the fixed order)
std::vector<Finding> BaseFindings(const L1Result& l1, const L2Result& l2);

StateNode FindingNode(const Finding& finding);
/// "F12  inferred  8000-80FF claim sprite  graphics  650  <claim>"
std::string FindingRow(const Finding& finding);
/// zx-reaper's re/findings.md: a header and `| ID | Status | Claim | Evidence | Refs |` rows (confirmed is written
/// observed, the template's statuses)
std::string FindingsMarkdown(const std::vector<Finding>& findings, const std::string& title);
/// One JSON object a line
std::string FindingsJsonl(const std::vector<Finding>& findings);

}  // namespace analysis
