#pragma once

// Dialect conversion plugins (dialect-conversion.md §3, decision D-6: compiled in, one registry line each). A
// frontend parses a decoded source of its dialect into the IR, a backend writes the IR as a source of its dialect;
// Convert chains them. Each reports what it could not convert.

#include <functional>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "unrealasm/containers.h"
#include "unrealasm/diagnostics.h"
#include "unrealasm/document.h"
#include "unrealasm/ir.h"

namespace unrealasm
{
/// The bytes of a data file a source reads while it is assembled (ZX-ASM's LOADTAB table), by the name the source gives
/// (the drive dropped): the file's data and the rest of its last sector, as the assembler loads it; empty when the
/// project has no such file
using DataFileReader = std::function<std::vector<uint8_t>(const std::string& name)>;

struct FrontendResult
{
    ir::Program program;
    Diagnostics diagnostics;
};

struct BackendOptions
{
    bool hexDollar = false;   ///< write hex numbers as $C000 instead of #C000 (where the target accepts both)
    /// Macros defined in other files of the project and the parameters each declares (ConvertProject fills it)
    std::map<std::string, int> macroParams;
    /// Labels some file of the project tests with IFUSED / IFNUSED (ConvertProject fills it)
    std::set<std::string> ifUsedNames;
    /// The file being written as INCLUDE names it ("" = a single source): names a target must keep unique across the
    /// files of a project (z80asm's sections) are made from it
    std::string fileName;
    /// How often each name is defined in the files of the project (ConvertProject fills it; pasmo needs DEFL for a name
    /// defined twice, EQU for one defined once)
    std::map<std::string, int> definitions;
    /// The project's data files (ConvertProject hands it to the frontends; none: the sources alone)
    DataFileReader dataFiles;
    /// The sources are written for the ZX Spectrum Next (sjasmplus --zxnext): the Z80N mnemonics are instructions in every
    /// file, as when a file says DEVICE ZXSPECTRUMNEXT or OPT --zxnext
    bool z80n = false;
};

/// A label of the source and the name the backend wrote it under (a reserved word renamed, a LOCAL block's label made
/// unique): how a value found in the written text is given back to the source's label
struct LabelName
{
    uint32_t line = 0;           ///< the source line defining it (ir::Line::sourceLine)
    std::string source;          ///< as the source writes it
    std::string written;         ///< as the backend wrote it
    bool local = false;          ///< a label of a LOCAL block (ALASM, TASM's DEFMAC)
    bool inMacro = false;        ///< defined in a macro body: one per expansion, no single value
};

struct BackendResult
{
    SourceDocument document;
    Diagnostics diagnostics;
    std::map<std::string, int> macroParams;   ///< the macros this file defines
    std::set<std::string> ifUsedNames;        ///< the labels this file tests with IFUSED / IFNUSED
    std::map<std::string, int> definitions;   ///< how often this file defines each name
    std::vector<LabelName> labels;            ///< every label line of the program (backends that keep names fill it)
};

class IFrontend
{
public:
    virtual ~IFrontend() = default;
    virtual std::string_view Dialect() const = 0;
    virtual FrontendResult Parse(const SourceDocument& source) const = 0;
    /// The source as one file of a project: a dialect whose macros must be expanded at their calls (ZX-ASM) learns the
    /// macros the other files define (an INCLUDEd definitions file); the others parse the file alone
    virtual FrontendResult ParseInProject(const SourceDocument& source, const std::vector<const SourceDocument*>& project) const
    {
        (void)project;
        return Parse(source);
    }
    /// As ParseInProject, with the project's data files (a dialect that reads one while assembling)
    virtual FrontendResult ParseWithData(const SourceDocument& source, const std::vector<const SourceDocument*>& project,
                                         const DataFileReader& dataFiles) const
    {
        (void)dataFiles;
        return ParseInProject(source, project);
    }
};

class IBackend
{
public:
    virtual ~IBackend() = default;
    virtual std::string_view Dialect() const = 0;
    virtual BackendResult Write(const ir::Program& program, const BackendOptions& options) const = 0;
};

class DialectRegistry
{
public:
    static const DialectRegistry& Builtin();
    const IFrontend* Frontend(std::string_view dialect) const;
    const IBackend* Backend(std::string_view dialect) const;
    void Add(std::unique_ptr<IFrontend> frontend);
    void Add(std::unique_ptr<IBackend> backend);

private:
    std::vector<std::unique_ptr<IFrontend>> _frontends;
    std::vector<std::unique_ptr<IBackend>> _backends;
};

struct ConvertResult
{
    SourceDocument document;
    Diagnostics diagnostics;   ///< the frontend's and the backend's
    bool ok = false;
};

/// The source (its dialect from source.dialect) in the target dialect; ok = false when either side is missing
ConvertResult Convert(const SourceDocument& source, std::string_view targetDialect, const BackendOptions& options = {});

/// One source of a project (the files of a disk): its name as INCLUDE names it (no extension) and its document
struct ProjectFile
{
    std::string name;
    SourceDocument document;
};

struct ProjectResult
{
    std::vector<ProjectFile> files;   ///< converted, in the same order; names unchanged
    std::vector<std::vector<LabelName>> labels;   ///< per file (same order): its labels as the backend wrote them
    Diagnostics diagnostics;          ///< each prefixed with the file name
    bool ok = false;
};

/// Every source of a project in the target dialect: INCLUDE wildcards resolve against the project's names (the last
/// matching one, as ALASM does), macros defined in one file are known to the others
ProjectResult ConvertProject(const std::vector<ProjectFile>& files, std::string_view targetDialect, const BackendOptions& options = {});

/// The sources among the files of a TR-DOS image as one project, named as INCLUDE names them (a name saved again:
/// NAME~2, NAME~3 ...): every file the codec detection takes for a tokenized source, and every file such a source
/// INCLUDEs that detection could not tell (a source of two lines), read with the codec and version of the source that
/// INCLUDEs it
std::vector<ProjectFile> ImageProject(const std::vector<containers::TrdosFile>& files);
}  // namespace unrealasm
