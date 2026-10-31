#pragma once

/// @file l3bugs.h
/// @brief Candidate bugs of L3 (docs/emulator/analysis/analysis-layers.md, "Candidate bugs"): rules over L1's per-byte
/// uses, the routines' stack balance and the L2 claims. Each hit is a finding of kind bug, status inferred, with the
/// instructions it names; the review queue shows every one. The rules, in their fixed order:
///
///   read-unwritten   data reads of RAM bytes the replay never wrote, in a zero-filled run no claim explains, by a
///                    reader that reads no other never-written value nearby (not a walk over static data)
///   stack-over-code  stack writes onto bytes that ran as code (the stack grew into the program)
///   code-overwritten an opcode byte written after it ran by anything but an operand patch (a block copy, a stray write)
///   stack-imbalance  a routine whose frames were left without a return, or returned to another address than pushed
///   index-past-table a table's own readers also read the bytes just before or after it (an index out of range)
///   counter-wrap     a byte INC / DEC wrote that took both $00 and $FF (it wraps)

#include <vector>

#include "debugger/analyzers/analysis/l3findings.h"

namespace analysis
{

/// The bug rules' hits (no ids yet), rule by rule, each in address order
std::vector<Finding> CandidateBugs(const L1Result& l1, const L2Result& l2);

}  // namespace analysis
