#pragma once

// Dialect conversion plugins (dialect-conversion.md §3, decision D-6: compiled in, one registry line each). A
// frontend parses a decoded source of its dialect into the IR, a backend writes the IR as a source of its dialect;
// Convert chains them. Each reports what it could not convert.

#include <functional>
#include <optional>
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

/// A label of the source with the value its assembler computes (the layout of the source, layout.h)
struct SourceLabel
{
    int64_t value = 0;
    std::string file;            ///< the project file defining it ("" = the single source)
    uint32_t line = 0;           ///< 1-based line of that file
    bool redefinable = false;    ///< assigned with DEFL / "=": the value changes while assembling, none is known
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
    /// The size of a file an INCBIN of the converted source names (none: dataFiles' size, which may hold a sector's
    /// slack): the layout of a converted sjasmplus project (byte operands to mark with low)
    std::function<std::optional<uint64_t>(const std::string& name)> fileSize;
    /// The sources are written for the ZX Spectrum Next (sjasmplus --zxnext): the Z80N mnemonics are instructions in every
    /// file, as when a file says DEVICE ZXSPECTRUMNEXT or OPT --zxnext
    bool z80n = false;
    /// The version of the target assembler the text is written for, in its codec's version names (ALASM "4.44",
    /// "5.07"); "" = the newest. Keywords follow it (ALASM IF / IF0, UNTIL / UNTIL0, DEFB in 3.8)
    std::string targetVersion;
    /// The source's labels by the names its layout gives them (sjasmplus: label, label.local, module.label), filled by
    /// Convert / ConvertProject for a backend that asks (IBackend::WantsLabelValues): a one-pass target folds to its
    /// value an expression it cannot compute (a later label in an operation other than + and -, an operator it lacks)
    std::map<std::string, SourceLabel> labelValues;
    /// A file of several assemblies (main sources INCLUDing it): the label values of the others, each an IF is decided
    /// by only when it gives every assembly the same branch
    std::vector<std::map<std::string, SourceLabel>> otherLabelValues;
    /// An INCLUDE of a project file (the IR's name for it) written with these operands instead: the name and the place
    /// the target finds the text in (ALASM `"NAME",4`: the text already in RAM page 4, WriteProjectAs)
    std::map<std::string, std::string> includeAs;
    /// The name each file an INCLUDE / INCBIN names gets on the target's disk (the IR's name to a TR-DOS name of up to 8
    /// characters): a host project's files renamed for a TR-DOS disk (ProjectDiskNames). A name not in it is written
    /// without its folders
    std::map<std::string, std::string> fileNames;
    /// A sjasmplus project written for a retro target: a file of more lines than this is cut into parts of about as
    /// many, each INCLUDEd where it stood (at a line outside any block); 0 = files as they are. For a target whose file
    /// cannot hold the whole text (ALASM's 64K)
    size_t splitLines = 0;
    /// The file is INCLUDEd more than once (ConvertProject fills it): a target that defines a macro once (sjasmplus)
    /// writes its definitions for the first time only
    bool includedRepeatedly = false;
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
    /// The backend folds expressions with the source's label values (BackendOptions::labelValues)
    virtual bool WantsLabelValues() const { return false; }
    /// The backend lowers sjasmplus' structure words (MODULE, STRUCT, DEFL, macros expanded at their calls): a sjasmplus
    /// project is written as sjasmplus assembles it, its INCLUDEs joined into one text, and cut into its files again
    /// (ConvertProject)
    virtual bool JoinsSjasmplusProject() const { return false; }
    /// The target reads an INCLUDE (in the version options.targetVersion names): without one a joined project stays one
    /// file, the INCLUDEd texts in it where their INCLUDEs were (ZEUS 1983, PROMETHEUS)
    virtual bool Includes(const BackendOptions&) const { return true; }
};

class DialectRegistry
{
public:
    static const DialectRegistry& Builtin();
    const IFrontend* Frontend(std::string_view dialect) const;
    const IBackend* Backend(std::string_view dialect) const;
    /// The dialects that can be read (a frontend) / written (a backend), in registration order
    std::vector<std::string> FrontendDialects() const;
    std::vector<std::string> BackendDialects() const;
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

/// A source as a file of another assembler (part B: what goes into its memory, a snapshot or a disk)
struct WriteAsResult
{
    std::vector<uint8_t> bytes;   ///< the file as the assembler saves it (its codec, its version)
    SourceDocument document;      ///< the text written, in the codec's dialect
    Diagnostics diagnostics;      ///< the conversion's and the codec's
    std::string error;            ///< why not, when !ok
    bool ok = false;
};

/// `source` (its dialect from source.dialect) written by `codec` in its `version` ("" the newest): converted to the
/// codec's dialect by its backend first when it is in another (the backend told `version`), named `name` (the
/// format's header keeps up to 8 characters; empty: the source's name)
WriteAsResult WriteAs(const SourceDocument& source, std::string_view codec, const std::string& version, const std::string& name = "");

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

/// A project on the host: `main` first, then every source it INCLUDEs and they in turn, each named as its INCLUDE
/// gives it in the IR (sjasmplus: without .asm) and decoded as text in main's dialect. `read` gets the path to open:
/// the name relative to the folder of the file that INCLUDEs it, with .asm back where the IR dropped it ("" = the
/// file is not there: a warning, the INCLUDE stays for the assembler's own disk). `folders` (when given) gets each file's
/// folder relative to main's ("" the same, else ending with '/'), where the names its INCBINs give are found
std::vector<ProjectFile> HostProject(const SourceDocument& main, const DataFileReader& read, Diagnostics& diagnostics,
                                     std::vector<std::string>* folders = nullptr);

/// The names a project's texts get in an assembler's headers and on a TR-DOS disk: files[0] `title`, each other its
/// file name without folder and extension, in upper case, 8 characters, made unique with ~2, ~3 ...
std::vector<std::string> ProjectTextNames(const std::vector<ProjectFile>& files, const std::string& title);

/// The names a host project's files get on a TR-DOS disk (BackendOptions::fileNames): each source (by its name in the
/// project, files[0] the main one) and each data file (as its INCBIN names it) by its file name without folders and
/// extensions, up to 8 characters, unique in any case (NAME~2, NAME~3 ...)
std::map<std::string, std::string> ProjectDiskNames(const std::vector<ProjectFile>& files, const std::vector<std::string>& dataFiles);

/// The catalog's start field each assembler writes for a saved source (the research documents of each codec)
uint16_t CatalogStart(std::string_view codec, const std::string& version, char type);

/// A source file as its assembler saves it to a TR-DOS disk: `name` (up to 8 characters, blank padded), `type`, the
/// start field CatalogStart gives, the length (XAS keeps 0 there: it reads whole sectors, its file list skips other files)
containers::TrdosFile CatalogFile(const std::string& name, char type, std::string_view codec, const std::string& version,
                                  const std::vector<uint8_t>& bytes);

struct WriteProjectResult
{
    std::vector<WriteAsResult> files;   ///< in the order of the project's files, each named as asked
    Diagnostics diagnostics;            ///< the conversion's, each prefixed with its file's name
    std::string error;
    bool ok = false;
};

/// A project (HostProject) written by `codec` as WriteAs writes one source: converted together (ConvertProject: the
/// labels of every file known in each), files[k] named names[k], each INCLUDE of a project file written as
/// includeAs gives it (the IR's name to the target's operands)
WriteProjectResult WriteProjectAs(const std::vector<ProjectFile>& files, std::string_view codec, const std::string& version,
                                  const std::vector<std::string>& names, const std::map<std::string, std::string>& includeAs = {});
}  // namespace unrealasm
