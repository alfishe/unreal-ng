#pragma once

/// @file l3naming.h
/// @brief Naming batches of the L3 review (docs/emulator/analysis/analysis-layers.md, "Naming"): the draft names
/// what L2 could not name after its kind and address (sub_9A10, var_9000, L9A20); the review gives them names in
/// batches. One batch covers up to kNameBatchSubjects related draft labels (one entry and the points inside it, or
/// neighbouring entries of one class: code or data, never mixed), each with a compact fact sheet: callers, callees,
/// accesses and a four-line disassembly head for code, the value class and range, the accesses and the users for
/// data. The batches come from the draft plan without the review's notes, so one build gives the same batches (ids
/// N1..) on every compiler.
///
/// Subjects: every label of the draft named after its address (a kind prefix and the address in hex), the points
/// inside entries (loop labels JR / DJNZ reach included) and plain data the code uses, except untouched and unused
/// entries, code entries no instruction refers to that L2 did not take for routine entries (the continuation of a
/// routine after a status change), and graphics inside the copy source of a buffer no instruction refers to (one of
/// a set the program copies as a whole).
///
/// @code
///   analysis::DraftPlan plan = analysis::MakePlan(l1, l2, analysis::ExportOptions{});
///   std::vector<analysis::NamingBatch> batches = analysis::NamingBatches(l1, l2, plan);
///   std::string sheet = analysis::NamingSheet(batches[0], l1, l2, plan, disassemble);
/// @endcode

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "debugger/analyzers/analysis/l1model.h"
#include "debugger/analyzers/analysis/l2export.h"
#include "debugger/analyzers/analysis/l2model.h"

namespace analysis
{

/// Subjects a batch, at most
constexpr size_t kNameBatchSubjects = 10;
/// A batch's sheet, at most
constexpr size_t kNameSheetBytes = 2048;
/// Batches of one build, at most: over it the batches naming routine and data entries are kept first (the subjects
/// left out are counted in the queue's summary)
constexpr size_t kNameBatchCap = 40;

struct NamingSubject
{
    uint16_t z80 = 0;
    std::string label;   ///< the draft's (sub_9A10)
    bool code = false;
    bool entry = false;  ///< an entry's start (false: a point inside one)
    uint16_t entryStart = 0;
};

struct NamingBatch
{
    std::string id;  ///< "N3"
    uint16_t start = 0;
    uint16_t end = 0;  ///< inclusive: the first subject's entry start to the last subject's entry end
    bool code = false;
    std::vector<NamingSubject> subjects;
};

struct NamingResult
{
    std::vector<NamingBatch> batches;  ///< at most kNameBatchCap
    uint32_t subjects = 0;             ///< all subjects, the capped ones included
    uint32_t capped = 0;               ///< subjects in no batch (past the cap)
};

/// Whether @p label is a draft name made of a kind prefix and @p z80 in hex (sub_9A10, L9A20)
bool IsAddressName(const std::string& label, uint16_t z80);

/// The naming batches of one build (@p plan: MakePlan without the review's notes)
NamingResult NamingBatches(const L1Result& l1, const L2Result& l2, const DraftPlan& plan);

/// The batch's sheet, at most kNameSheetBytes: one block a subject, then how to resolve it
std::string NamingSheet(const NamingBatch& batch, const L1Result& l1, const L2Result& l2, const DraftPlan& plan,
                        const std::function<std::string(const uint8_t* bytes, uint16_t z80)>& disassemble);

/// One name the review gave
struct GivenName
{
    uint16_t z80 = 0;
    std::string label;
    std::string title;
};

/// Parse the names of a batch's resolve: a JSON object {"$9000": "PlayerY", "$9A10": {"label": "...", "title":
/// "..."}} or an array [{"at": "$9000", "label": "...", "title": "..."}]; addresses as $XXXX, 0xXXXX, XXXX (hex)
bool ParseGivenNames(const std::string& json, std::vector<GivenName>& names, std::string& error);

}  // namespace analysis
