#pragma once

/// @file l3coverage.h
/// @brief Directed coverage of L3 (docs/emulator/analysis/analysis-layers.md, "Directed coverage"): the conditional
/// branches that went only one way in the session (L1's BranchCondition), and an attempt to send each the other way
/// on the experiment runner (l3experiment.h), from the last frame the branch ran in:
///
///   - a branch on a memory byte: the byte is pinned (written before every instruction) to a value until the branch
///     runs once: the compared constant, its neighbours, $00, $01, $FF, $80;
///   - a branch on port $xxFE (a key test): each key of the half-rows the port's high byte selects is held;
///   - a branch on port $xx1F (a Kempston joystick): each direction and fire is held.
///
/// Branches on registers, constants or other ports are listed as skipped, with the reason. A branch flipped is a
/// coverage finding (observed) with what flipped it and the code it reached that L1 never saw run, and a note in the
/// draft ("needs poke $8100=$05", "needs key Q"). One round: attempts spread breadth first over the branches.

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/analyzers/analysis/l1model.h"
#include "debugger/analyzers/analysis/l2export.h"
#include "debugger/analyzers/analysis/l2model.h"
#include "emulator/state/statenode.h"

namespace analysis
{

struct TraceBundle;

struct CoverOptions
{
    uint32_t budget = 64;      ///< attempts in all
    uint32_t perBranch = 8;    ///< attempts per branch at most
    uint32_t frames = 50;      ///< frames an attempt runs (it stops early when the branch went the old way)
    uint32_t parallel = 0;     ///< workers (l3experiment.h)
    uint32_t limit = 200;      ///< branches tried at most (in address order)
    std::vector<ExportRange> ranges;  ///< branches in these Z80 ranges (empty: $4000-$FFFF)
};

struct CoverBranch
{
    uint16_t z80 = 0;
    uint8_t cond = 0;
    bool wantTaken = true;    ///< it was never taken (else: it was always taken)
    std::string tested;       ///< memory, port, register, constant, none
    uint16_t address = 0;     ///< the byte / port
    std::string status;       ///< flipped, not_flipped, not_reached, skipped, untried (the budget ran out)
    std::string reason;       ///< skipped: why
    std::string how;          ///< flipped: "poke $8100=$05", "key Q", "joystick fire"
    uint32_t attempts = 0;
    uint64_t fromFrame = 0;
    uint32_t newCode = 0;     ///< instructions L1 never saw run, reached after the flip
    std::vector<std::pair<uint16_t, uint16_t>> newRanges;
};

struct CoverResult
{
    std::vector<CoverBranch> branches;  ///< in address order
    uint32_t candidates = 0;
    uint32_t tried = 0;
    uint32_t flipped = 0;
    uint32_t attempts = 0;
    uint32_t newCode = 0;               ///< distinct new instructions over every flip
    uint32_t workers = 0;
    uint32_t peakConcurrency = 0;
    uint64_t micros = 0;
    std::string error;
};

CoverResult RunCoverage(const TraceBundle& bundle, const L1Result& l1, const CoverOptions& options);

StateNode CoverBranchNode(const CoverBranch& branch);
/// "$8123 JR NZ never taken: flipped by poke $8100=$05, 12 new instructions at $8130-$8150"
std::string CoverBranchLine(const CoverBranch& branch);

}  // namespace analysis
