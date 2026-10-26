#pragma once

/// @file l3review.h
/// @brief The review queue of L3 (docs/emulator/analysis/analysis-layers.md, "Review queue"): of the findings register
/// (l3findings.h), only the findings that need a decision, in a fixed order, at most a cap of them plus one summary
/// item when there are more:
///
///   1. candidate bugs (every one);
///   2. conflicts between two specific claims (not the code map's fallbacks) whose confidences are within
///      kConflictGap of each other;
///   3. unnamed hot routines;
///   4. specific claims below confidence::kReview (700);
///   5. unknown areas (over 16 bytes no detector explains).
///
/// A finding resolved (a verdict, or a label / title / comment given) leaves the queue. The thresholds were tuned on
/// the Manic Miner session (baselines/score-p5.md) and are frozen: JSW and a 128K game are the hold-outs.
///
/// @code
///   analysis::ReviewQueue queue = analysis::SelectReview(findings, l2, options);
///   std::string sheet = analysis::ReviewSheet(findings[queue.items[0].finding], queue.items[0], l1, l2, tools);
/// @endcode

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "debugger/analyzers/analysis/l2export.h"
#include "debugger/analyzers/analysis/l3findings.h"
#include "emulator/state/statenode.h"

namespace analysis
{

constexpr uint32_t kReviewCap = 40;
/// Conflicts: the two claims' confidences at most this far apart (a wider gap: the stronger one stands)
constexpr uint16_t kConflictGap = 300;
/// The fact sheet of one item, at most
constexpr size_t kReviewSheetBytes = 1024;

struct ReviewOptions
{
    std::vector<ExportRange> ranges;  ///< findings overlapping these Z80 ranges (empty: $4000-$FFFF)
    uint32_t limit = kReviewCap;
};

struct ReviewItem
{
    size_t finding = 0;   ///< index into the findings given
    std::string id;       ///< the finding's id
    std::string reason;   ///< bug, conflict, unnamed-hot, low-confidence, unknown
    uint32_t priority = 0;
};

struct ReviewQueue
{
    std::vector<ReviewItem> items;  ///< at most the limit, in the fixed order
    uint32_t total = 0;             ///< items before the cap
    uint32_t resolved = 0;          ///< findings in range a decision removed
    std::string summary;            ///< non-empty when the cap cut the queue: what is left, by reason
    uint32_t byReason[5] = {};      ///< before the cap: bug, conflict, unnamed-hot, low-confidence, unknown
};

ReviewQueue SelectReview(const std::vector<Finding>& findings, const L2Result& l2, const ReviewOptions& options);

/// What a sheet needs from the instance: an instruction's text, a routine's L1 fact sheet
struct ReviewTools
{
    std::function<std::string(const uint8_t* bytes, uint16_t z80)> disassemble;
    std::function<std::string(uint16_t z80)> routineSheet;
};

/// The item's fact sheet, at most kReviewSheetBytes: the finding, its evidence, the bytes, the code, how to resolve
std::string ReviewSheet(const Finding& finding, const ReviewItem& item, const L1Result& l1, const L2Result& l2,
                        const ReviewTools& tools);

StateNode ReviewItemNode(const Finding& finding, const ReviewItem& item, const std::string& state);

}  // namespace analysis
