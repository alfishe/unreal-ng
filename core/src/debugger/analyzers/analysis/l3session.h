#pragma once

/// @file l3session.h
/// @brief L3 of the analysis layers per instance (docs/emulator/analysis/analysis-layers.md, "L3: review"): the
/// findings register of the last build (l3findings.h) with what the review decided, kept by each finding's key so a
/// rebuild keeps the decisions, the findings added since (notes, coverage, experiments), and the actions on it:
///
///   findings   [op kind status range limit format path claim evidence by title]  op list (default): the register
///              (format table, markdown: zx-reaper's re/findings.md, jsonl); op add: a note (claim, range, status,
///              evidence ';'-separated, by)
///   review     [op id verdict label title comment kind names by experiment ranges reason offset limit]  op next
///              (default): the first open item of the queue (l3review.h) with its fact sheet; list: the queue (one
///              reason, from an offset); show: one item's sheet; resolve: a decision (verdict ok: confirmed, wrong:
///              refuted, skip: out of the queue; label, title, comment go into the draft export; kind changes what
///              the draft makes of the finding's range; an entry candidate's ok splits the code entry), optionally
///              with an experiment whose result marks the finding; a naming batch (N3) takes names (JSON)
///   experiment [spec path parallel finding]  run experiments (l3experiment.h), a spec or an array of them as JSON
///              (spec) or a file (path); the results mark their findings observed / refuted
///   cover      [budget per_branch frames parallel limit ranges]  directed coverage (l3coverage.h): flip the branches
///              the session took one way only; the flips become coverage findings and draft notes
///
/// Experiments and coverage run synchronously on the calling thread and its pool of analysis instances (never the
/// source instance); a call answers when they are done.

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "debugger/analyzers/analysis/analysiscontrol.h"
#include "debugger/analyzers/analysis/l2export.h"
#include "debugger/analyzers/analysis/l3findings.h"
#include "debugger/analyzers/analysis/l3naming.h"

class Emulator;

namespace analysis
{

/// What the review decided about one finding (kept by Finding::Key)
struct Resolution
{
    uint16_t start = 0;
    bool statusSet = false;
    FindingStatus status = FindingStatus::Inferred;
    std::string label;
    std::string title;
    std::string comment;
    std::string verdict;
    std::string by;
    std::vector<std::string> evidence;  ///< appended (experiment results)
    std::string kind;                   ///< the region kind the review gave [start, kindEnd] (ExportNote::kind)
    uint16_t kindEnd = 0;
    bool entry = false;                 ///< an entry candidate accepted: a routine starts at start
};

class L3Sessions
{
public:
    static L3Sessions& Instance();

    /// The register of @p emulatorId's build @p job (made once per build), with the decisions and the added findings
    std::vector<Finding> Findings(const std::string& emulatorId, uint64_t job, const L1Result& l1, const L2Result& l2);
    /// Record a decision; false when no finding has that key
    void Resolve(const std::string& emulatorId, const std::string& key, const Resolution& resolution);
    void Add(const std::string& emulatorId, Finding finding);
    /// Drop the added findings of one kind and subject (a new coverage round replaces the last)
    void DropAdded(const std::string& emulatorId, FindingKind kind);
    void SetCoverageNotes(const std::string& emulatorId, std::map<uint16_t, ExportNote> notes);
    void MarkShown(const std::string& emulatorId, const std::string& key);
    bool Shown(const std::string& emulatorId, const std::string& key) const;
    /// The naming batches of build @p job (l3naming.h; made once per build)
    NamingResult Naming(const std::string& emulatorId, uint64_t job, const L1Result& l1, const L2Result& l2);
    /// Record the names of a batch (@p skipped: its subjects left unnamed)
    void Name(const std::string& emulatorId, const std::vector<GivenName>& names, const std::vector<uint16_t>& skipped);
    /// The names given, by address
    std::map<uint16_t, GivenName> Names(const std::string& emulatorId) const;
    /// Whether every subject of @p batch is named or skipped
    bool Named(const std::string& emulatorId, const NamingBatch& batch) const;
    /// The draft export's L3 notes: decisions with a label, title, comment, kind or entry, the names of the naming
    /// batches, and the coverage notes
    std::map<uint16_t, ExportNote> Notes(const std::string& emulatorId) const;
    void Forget(const std::string& emulatorId);

private:
    struct State
    {
        uint64_t job = 0;
        std::vector<Finding> base;
        std::vector<Finding> added;
        std::map<std::string, Resolution> resolutions;
        std::set<std::string> shown;
        std::map<uint16_t, ExportNote> coverageNotes;
        uint64_t namingJob = 0;
        bool namingMade = false;
        NamingResult naming;
        std::map<uint16_t, GivenName> names;
        std::set<uint16_t> skippedNames;
    };
    mutable std::mutex _mutex;
    std::map<std::string, State> _states;
};

/// findings, review, experiment, cover (Execute forwards them here once the options are checked)
Reply ExecuteReview(Emulator& emulator, const std::string& action, const Options& options);

}  // namespace analysis
