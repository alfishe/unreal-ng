#pragma once

// Symbols from assembler sources (symbols/formats.md §4.1, symbols/tdd.md §4.2): the labels a project's sources
// define, with their values, kinds and defining lines. A sjasmplus project is laid out as it is; a project of another
// dialect (TASM, ALASM, STORM, ZX-ASM) through its sjasmplus conversion, whose labels are given back their names in
// the source. The values are what the original assembler would compute: the conversion assembles to the bytes it
// built (research-*-to-sjasmplus.md), and layout.h lays the converted text out as sjasmplus does.

#include <string>
#include <vector>

#include "unrealasm/diagnostics.h"
#include "unrealasm/dialect.h"
#include "unrealasm/layout.h"
#include "unrealasm/symbols/symbol.h"

namespace unrealasm::symbols
{
struct SourceSymbolsOptions
{
    layout::LayoutOptions layout;
    /// Also the labels the conversion adds or a macro defines once per expansion (sjasmplus names); off: only the
    /// source's own labels
    bool generated = false;
    /// Every label of the laid-out sjasmplus text under the name that text gives it (block labels with their suffix,
    /// renames, what the conversion adds): to compare with what sjasmplus --sym writes for the same text
    bool writtenNames = false;
};

struct SourceSymbolsResult
{
    SymbolSet set;
    Diagnostics diagnostics;   ///< the conversion's warnings and errors, the layout's, and the labels left without a value
    bool ok = false;           ///< laid out completely: every label of the result has its value
};

/// The labels of a project (its files as INCLUDE names them, every file of one dialect) assembled from `main`
SourceSymbolsResult SymbolsFromProject(const std::vector<ProjectFile>& files, size_t main, const SourceSymbolsOptions& options = {});

/// The labels of one source that includes nothing
SourceSymbolsResult SymbolsFromSource(const SourceDocument& source, const SourceSymbolsOptions& options = {});
}  // namespace unrealasm::symbols
