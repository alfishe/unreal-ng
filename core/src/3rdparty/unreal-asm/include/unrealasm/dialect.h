#pragma once

// Dialect conversion plugins (dialect-conversion.md §3, decision D-6: compiled in, one registry line each). A
// frontend parses a decoded source of its dialect into the IR, a backend writes the IR as a source of its dialect;
// Convert chains them. Each reports what it could not convert.

#include <map>
#include <set>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "unrealasm/diagnostics.h"
#include "unrealasm/document.h"
#include "unrealasm/ir.h"

namespace unrealasm
{
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
};

struct BackendResult
{
    SourceDocument document;
    Diagnostics diagnostics;
    std::map<std::string, int> macroParams;   ///< the macros this file defines
    std::set<std::string> ifUsedNames;        ///< the labels this file tests with IFUSED / IFNUSED
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
    Diagnostics diagnostics;          ///< each prefixed with the file name
    bool ok = false;
};

/// Every source of a project in the target dialect: INCLUDE wildcards resolve against the project's names (the last
/// matching one, as ALASM does), macros defined in one file are known to the others
ProjectResult ConvertProject(const std::vector<ProjectFile>& files, std::string_view targetDialect, const BackendOptions& options = {});
}  // namespace unrealasm
