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
///   5. unknown areas (over 16 bytes no detector explains);
///   6. entry candidates (a jump target that may start a routine of its own: L2Result::entryCandidates);
///   7. naming batches (l3naming.h): draft labels named after their address, up to kNameBatchSubjects an item.
///
/// A finding resolved (a verdict, or a label / title / comment given) leaves the queue; a naming batch leaves it when
/// each of its subjects is named or skipped. The thresholds of 1-5 were tuned on the Manic Miner session
/// (baselines/score-p5.md) and are frozen: JSW and a 128K game are the hold-outs. 6 and 7 come after them, so the
/// first items of the queue stay what they were; `reason` picks one reason, `offset` pages through the queue.
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
#include "debugger/analyzers/analysis/l3naming.h"
#include "emulator/state/statenode.h"

namespace analysis
{

constexpr uint32_t kReviewCap = 40;
/// Conflicts: the two claims' confidences at most this far apart (a wider gap: the stronger one stands)
constexpr uint16_t kConflictGap = 300;
/// The fact sheet of one item, at most
constexpr size_t kReviewSheetBytes = 1024;

/// The reasons in queue order: bug, conflict, unnamed-hot, low-confidence, unknown, entry, name
constexpr int kReviewReasons = 7;
const char* ReviewReasonName(int priority);
/// -1: not a reason
int ParseReviewReason(const std::string& text);

struct ReviewOptions
{
    std::vector<ExportRange> ranges;  ///< findings overlapping these Z80 ranges (empty: $4000-$FFFF)
    uint32_t limit = kReviewCap;
    uint32_t offset = 0;              ///< items of the queue skipped before the shown ones
    int reason = -1;                  ///< only this reason (ParseReviewReason); -1: all
    /// The naming batches of the build and whether one is decided (every subject named or skipped)
    const std::vector<NamingBatch>* names = nullptr;
    std::function<bool(const NamingBatch&)> named;
};

struct ReviewItem
{
    size_t finding = 0;   ///< index into the findings given (a naming batch: into the batches)
    std::string id;       ///< the finding's id, or the batch's (N3)
    std::string reason;   ///< bug, conflict, unnamed-hot, low-confidence, unknown, entry, name
    uint32_t priority = 0;
    bool batch = false;   ///< a naming batch
};

struct ReviewQueue
{
    std::vector<ReviewItem> items;  ///< at most the limit, in the fixed order
    uint32_t total = 0;             ///< items before the cap
    uint32_t resolved = 0;          ///< findings in range a decision removed
    std::string summary;            ///< non-empty when the cap cut the queue: what is left, by reason
    uint32_t byReason[kReviewReasons] = {};  ///< before the reason filter and the cap, in queue order
    uint32_t offset = 0;            ///< items skipped before the shown ones
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
StateNode NamingItemNode(const NamingBatch& batch, const ReviewItem& item, const std::string& state);
/// The list line of a naming batch ("4 draft labels $9A10-$9B00 (code): sub_9A10 L9A20 ...")
std::string NamingItemClaim(const NamingBatch& batch);

}  // namespace analysis
