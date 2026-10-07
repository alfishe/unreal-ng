#pragma once

// Label values without running an assembler (symbols/formats.md §4.1): a pass over a project of sjasmplus sources that
// knows how many bytes every line takes and evaluates expressions as sjasmplus does, repeated until no label moves.
// No bytes are produced. A source of another dialect gets here through its sjasmplus conversion (symbols/fromsource.h),
// the conversion whose output assembles to what the original assembler built.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "unrealasm/diagnostics.h"
#include "unrealasm/dialect.h"

namespace unrealasm::layout
{
struct LayoutOptions
{
    /// The size of a file INCBIN names (the name as the source writes it); none = not found, reported and taken as empty
    std::function<std::optional<uint64_t>(const std::string& name)> fileSize;
    int maxPasses = 10;
};

enum class LabelUse : uint8_t
{
    Unknown,    ///< nothing follows it in its block
    Code,       ///< the next thing laid out is an instruction
    Data,       ///< DB, DW, DS, INCBIN ...
    Equ,
    Defl,       ///< DEFL / "=": the value the last pass ended with
};

struct Label
{
    std::string name;          ///< sjasmplus' full name: module.label, label.local
    int64_t value = 0;
    LabelUse use = LabelUse::Unknown;
    int page = -1;             ///< the page ORG named (ORG address,page); -1 = none
    std::string parent;        ///< a local label's label ("" = global)
    std::string module;
    std::string file;          ///< the project file defining it (as INCLUDE names it)
    uint32_t line = 0;         ///< 1-based line of that file
};

struct LayoutResult
{
    std::vector<Label> labels;   ///< each name once, in the order the source defines them
    Diagnostics diagnostics;
    int passes = 0;
    bool ok = false;             ///< every value resolved and no label moved in the last pass
};

/// Lays out a project of sjasmplus sources from its main file; the others are reached through INCLUDE (a project file
/// whose name, without its .asm, is the INCLUDEd name)
LayoutResult Layout(const std::vector<ProjectFile>& files, size_t main, const LayoutOptions& options = {});
}  // namespace unrealasm::layout
