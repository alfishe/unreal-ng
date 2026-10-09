#pragma once

// Symbols from assembler sources (symbols/formats.md §4.1, symbols/tdd.md §4.2): the labels a project's sources
// define, with their values, kinds and defining lines. A sjasmplus project is laid out as it is; a project of another
// dialect (TASM, ALASM, STORM, ZX-ASM) through its sjasmplus conversion, whose labels are given back their names in
// the source. The values are what the original assembler would compute: the conversion assembles to the bytes it
// built (research-*-to-sjasmplus.md), and layout.h lays the converted text out as sjasmplus does.

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "unrealasm/containers.h"
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

/// A project read from files: its sources (ImageProject: as `zxasm convert` takes an image) and the sizes of all its
/// files under the names INCBIN gives them (NAME for type C, NAME.T, NAME.slack for the rest of the last sector)
struct SourceProject
{
    std::vector<ProjectFile> sources;
    std::vector<std::pair<std::string, uint64_t>> sizes;

    /// The size of the file an INCBIN name (ALASM's * and ? wildcards) gives; the last match, as ALASM takes it
    std::optional<uint64_t> Size(const std::string& wanted) const;
};

/// The project of an image's files (a TR-DOS disk, a tape, one hobeta file)
SourceProject ProjectFromFiles(const std::vector<containers::TrdosFile>& files);
/// A text file as the project's one source: a tokenized format when detection says so, else sjasmplus' dialect; named
/// as INCLUDE would name it (no folder, no .asm)
SourceProject ProjectFromText(const std::string& path, const std::vector<uint8_t>& bytes);
/// The index of the source named `main`, or of the only one when `main` is empty; sources.size() with the reason
/// (no such source; several, listed)
size_t FindMainSource(const SourceProject& project, const std::string& main, std::string& error);
/// SymbolsFromProject with the project's file sizes for INCBIN
SourceSymbolsResult SymbolsFromSourceProject(const SourceProject& project, size_t main, SourceSymbolsOptions options = {});
}  // namespace unrealasm::symbols
