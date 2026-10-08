#include "unrealasm/dialect.h"

#include <algorithm>

#include "unrealasm/registry.h"

#include "dialects/alasm/alasmfrontend.h"
#include "dialects/common/z80.h"
#include "dialects/storm/stormfrontend.h"
#include "dialects/zxasm/zxasmfrontend.h"
#include "dialects/xas/xasfrontend.h"
#include "dialects/gens/gensfrontend.h"
#include "dialects/asm80/asm80frontend.h"
#include "dialects/pasmo/pasmofrontend.h"
#include "dialects/z88dk/z80asmfrontend.h"
#include "dialects/zasm/zasmfrontend.h"
#include "dialects/fantasm/fantasmfrontend.h"
#include "dialects/zmac/zmacfrontend.h"
#include "dialects/rasm/rasmfrontend.h"
#include "dialects/specasm/specasmfrontend.h"
#include "dialects/odin/odinfrontend.h"
#include "dialects/prometheus/prometheusfrontend.h"
#include "dialects/tasm/tasmfrontend.h"
#include "dialects/masm/masmfrontend.h"
#include "dialects/zeus/zeusfrontend.h"
#include "dialects/pasmo/pasmobackend.h"
#include "dialects/z88dk/z88dkbackend.h"
#include "dialects/sjasmplus/sjasmplusbackend.h"
#include "dialects/sjasmplus/sjasmplusfrontend.h"

namespace unrealasm
{
const DialectRegistry& DialectRegistry::Builtin()
{
    static const DialectRegistry registry = [] {
        DialectRegistry r;
        // One line per plugin (decision D-6)
        r.Add(std::make_unique<dialects::AlasmFrontend>());
        r.Add(std::make_unique<dialects::TasmFrontend>());
        r.Add(std::make_unique<dialects::StormFrontend>());
        r.Add(std::make_unique<dialects::ZxasmFrontend>());
        r.Add(std::make_unique<dialects::MasmFrontend>());
        r.Add(std::make_unique<dialects::XasFrontend>());
        r.Add(std::make_unique<dialects::ZeusFrontend>());
        r.Add(std::make_unique<dialects::GensFrontend>());
        r.Add(std::make_unique<dialects::Asm80Frontend>());
        r.Add(std::make_unique<dialects::PasmoFrontend>());
        r.Add(std::make_unique<dialects::Z80asmFrontend>());
        r.Add(std::make_unique<dialects::ZasmFrontend>());
        r.Add(std::make_unique<dialects::FantasmFrontend>());
        r.Add(std::make_unique<dialects::ZmacFrontend>());
        r.Add(std::make_unique<dialects::RasmFrontend>());
        r.Add(std::make_unique<dialects::SpecasmFrontend>());
        r.Add(std::make_unique<dialects::OdinFrontend>());
        r.Add(std::make_unique<dialects::PrometheusFrontend>());
        r.Add(std::make_unique<dialects::SjasmplusFrontend>());
        r.Add(std::make_unique<dialects::SjasmplusBackend>());
        r.Add(std::make_unique<dialects::PasmoBackend>());
        r.Add(std::make_unique<dialects::Z88dkBackend>());
        return r;
    }();
    return registry;
}

const IFrontend* DialectRegistry::Frontend(std::string_view dialect) const
{
    for (const auto& f : _frontends)
        if (f->Dialect() == dialect)
            return f.get();
    return nullptr;
}

const IBackend* DialectRegistry::Backend(std::string_view dialect) const
{
    for (const auto& b : _backends)
        if (b->Dialect() == dialect)
            return b.get();
    return nullptr;
}

void DialectRegistry::Add(std::unique_ptr<IFrontend> frontend)
{
    _frontends.push_back(std::move(frontend));
}

void DialectRegistry::Add(std::unique_ptr<IBackend> backend)
{
    _backends.push_back(std::move(backend));
}

ConvertResult Convert(const SourceDocument& source, std::string_view targetDialect, const BackendOptions& options)
{
    ConvertResult result;
    const DialectRegistry& registry = DialectRegistry::Builtin();
    const IFrontend* frontend = registry.Frontend(source.dialect);
    const IBackend* backend = registry.Backend(targetDialect);
    if (!frontend || !backend)
    {
        result.diagnostics.push_back({Severity::Error, 0, 0,
                                      !frontend ? "no frontend for dialect '" + source.dialect + "'" : "no backend for dialect '" + std::string(targetDialect) + "'"});
        return result;
    }
    SourceDocument next;
    if (options.z80n && !source.z80n)
    {
        next = source;
        next.z80n = true;
    }
    FrontendResult parsed = frontend->Parse(options.z80n && !source.z80n ? next : source);
    BackendResult written = backend->Write(parsed.program, options);
    result.document = std::move(written.document);
    result.document.name = source.name;
    result.diagnostics = std::move(parsed.diagnostics);
    result.diagnostics.insert(result.diagnostics.end(), written.diagnostics.begin(), written.diagnostics.end());
    result.ok = !HasErrors(result.diagnostics);
    return result;
}
namespace
{
/// ALASM wildcards: * any run of characters, ? one character
bool Matches(std::string_view pattern, std::string_view name)
{
    if (pattern.empty())
        return name.empty();
    if (pattern[0] == '*')
    {
        for (size_t k = 0; k <= name.size(); ++k)
            if (Matches(pattern.substr(1), name.substr(k)))
                return true;
        return false;
    }
    return !name.empty() && (pattern[0] == '?' || pattern[0] == name[0]) && Matches(pattern.substr(1), name.substr(1));
}
}  // namespace

ProjectResult ConvertProject(const std::vector<ProjectFile>& files, std::string_view targetDialect, const BackendOptions& options)
{
    ProjectResult result;
    const DialectRegistry& registry = DialectRegistry::Builtin();
    const IBackend* backend = registry.Backend(targetDialect);
    std::vector<ir::Program> programs;
    std::vector<Diagnostics> notes(files.size());
    // The documents under their project names (INCLUDE names them so)
    std::vector<SourceDocument> named;
    for (const ProjectFile& f : files)
    {
        named.push_back(f.document);
        named.back().name = f.name;
    }
    // A sjasmplus project that enables the Z80N in one file (DEVICE ZXSPECTRUMNEXT, OPT --zxnext) has it on in all of them
    bool projectZ80n = options.z80n;
    for (const ProjectFile& f : files)
        if (f.document.dialect == "sjasmplus")
            for (const SourceLine& line : f.document.lines)
                if (dialects::z80::EnablesZ80n(line.text))
                    projectZ80n = true;
    if (projectZ80n)
        for (SourceDocument& d : named)
            d.z80n = true;
    for (size_t k = 0; k < files.size(); ++k)
    {
        const IFrontend* frontend = registry.Frontend(files[k].document.dialect);
        if (!frontend || !backend)
        {
            result.diagnostics.push_back({Severity::Error, 0, 0, files[k].name + ": no frontend or backend for this pair"});
            programs.emplace_back();
            continue;
        }
        std::vector<const SourceDocument*> others;
        for (size_t n = 0; n < files.size(); ++n)
            if (n != k)
                others.push_back(&named[n]);
        FrontendResult parsed = frontend->ParseWithData(projectZ80n ? named[k] : files[k].document, others, options.dataFiles);
        notes[k] = std::move(parsed.diagnostics);
        // INCLUDE "pattern": the last project file whose name matches
        for (ir::Line& line : parsed.program.lines)
            for (ir::Statement& s : line.statements)
                if (s.kind == ir::Statement::Kind::Directive && s.directive == ir::DirectiveKind::Include)
                {
                    std::string pattern = s.text;
                    const size_t colon = pattern.find(':');
                    if (colon != std::string::npos)
                        pattern = pattern.substr(colon + 1);
                    if (pattern.find_first_of("*?") == std::string::npos && !pattern.empty())
                        continue;
                    std::string found;
                    for (const ProjectFile& f : files)
                        if (pattern.empty() || Matches(pattern, f.name))
                            found = f.name;
                    if (!found.empty())
                        s.text = found;
                }
        programs.push_back(std::move(parsed.program));
    }
    if (!backend)
        return result;
    // First pass: the macros every file defines; second pass: write with all of them known
    BackendOptions withMacros = options;
    std::vector<std::map<std::string, int>> definitions;
    for (const ir::Program& program : programs)
    {
        BackendResult written = backend->Write(program, options);
        withMacros.macroParams.insert(written.macroParams.begin(), written.macroParams.end());
        withMacros.ifUsedNames.insert(written.ifUsedNames.begin(), written.ifUsedNames.end());
        definitions.push_back(written.definitions);
    }
    // Definitions are counted per assembly: a main source (one no file INCLUDEs) with everything it INCLUDEs. A file
    // gets the largest count of the assemblies it belongs to (two programs defining the same name apart do not add up)
    std::vector<std::vector<size_t>> includes(programs.size());
    std::vector<bool> included(programs.size(), false);
    for (size_t k = 0; k < programs.size(); ++k)
        for (const ir::Line& line : programs[k].lines)
            for (const ir::Statement& s : line.statements)
                if (s.kind == ir::Statement::Kind::Directive && s.directive == ir::DirectiveKind::Include)
                    for (size_t n = 0; n < files.size(); ++n)
                        if (n != k && files[n].name == s.text)
                        {
                            includes[k].push_back(n);
                            included[n] = true;
                        }
    std::vector<std::map<std::string, int>> unitDefinitions(programs.size());
    for (size_t main = 0; main < programs.size(); ++main)
    {
        if (included[main])
            continue;
        std::vector<size_t> unit = {main};
        std::vector<bool> seen(programs.size(), false);
        seen[main] = true;
        for (size_t at = 0; at < unit.size(); ++at)
            for (const size_t n : includes[unit[at]])
                if (!seen[n])
                {
                    seen[n] = true;
                    unit.push_back(n);
                }
        std::map<std::string, int> counts;
        for (const size_t n : unit)
            for (const auto& [name, count] : definitions[n])
                counts[name] += count;
        for (const size_t n : unit)
            for (const auto& [name, count] : counts)
                unitDefinitions[n][name] = std::max(unitDefinitions[n][name], count);
    }
    for (size_t k = 0; k < programs.size(); ++k)
    {
        withMacros.definitions = unitDefinitions[k];
        withMacros.fileName = files[k].name;
        BackendResult written = backend->Write(programs[k], withMacros);
        written.document.name = files[k].name;
        result.files.push_back({files[k].name, std::move(written.document)});
        result.labels.push_back(std::move(written.labels));
        for (Diagnostic d : notes[k])
            result.diagnostics.push_back({d.severity, d.line, d.byteOffset, files[k].name + ": " + d.message});
        for (Diagnostic d : written.diagnostics)
            result.diagnostics.push_back({d.severity, d.line, d.byteOffset, files[k].name + ": " + d.message});
    }
    result.ok = !HasErrors(result.diagnostics);
    return result;
}
std::vector<ProjectFile> ImageProject(const std::vector<containers::TrdosFile>& files)
{
    const CodecRegistry& registry = CodecRegistry::Builtin();
    std::vector<ProjectFile> project;
    std::vector<size_t> taken;   // the index in `files` of each project file
    auto add = [&](size_t k, const ISourceCodec& codec, const std::string& subversion) {
        const containers::TrdosFile& f = files[k];
        DecodeOptions options;
        options.catalog = f.Hints();
        options.subversion = subversion;
        // A name saved again (another catalog entry): TR-DOS finds the first one, so it keeps the name; the later ones
        // are NAME~2, NAME~3 ...
        std::string name = f.TrimmedName();
        int copies = 1;
        for (const ProjectFile& earlier : project)
            if (earlier.name == name || earlier.name.rfind(name + "~", 0) == 0)
                ++copies;
        if (copies > 1)
            name += "~" + std::to_string(copies);
        project.push_back({name, codec.Decode(f.data, options).document});
        taken.push_back(k);
    };
    for (size_t k = 0; k < files.size(); ++k)
    {
        const DetectResult detected = registry.Detect(files[k].data, files[k].Hints());
        if (detected.chosen && detected.chosen->Info().family == CodecFamily::Tokenized)
            add(k, *detected.chosen, std::string());
    }
    // The files the sources INCLUDE that detection left out, read like their includer (until none is left)
    for (size_t at = 0; at < project.size(); ++at)
    {
        const IFrontend* frontend = DialectRegistry::Builtin().Frontend(project[at].document.dialect);
        const ISourceCodec* codec = registry.Find(project[at].document.format);
        if (!frontend || !codec)
            continue;
        const std::string subversion = project[at].document.subversion;
        const FrontendResult parsed = frontend->Parse(project[at].document);
        for (const ir::Line& line : parsed.program.lines)
            for (const ir::Statement& s : line.statements)
            {
                if (s.kind != ir::Statement::Kind::Directive || s.directive != ir::DirectiveKind::Include)
                    continue;
                std::string wanted = s.text;
                const size_t colon = wanted.find(':');
                if (colon != std::string::npos)
                    wanted = wanted.substr(colon + 1);   // a drive (ALASM's "A:name")
                if (wanted.empty() || wanted.find_first_of("*?") != std::string::npos)
                    continue;
                for (size_t k = 0; k < files.size(); ++k)
                    if (files[k].TrimmedName() == wanted && std::find(taken.begin(), taken.end(), k) == taken.end())
                    {
                        add(k, *codec, subversion);
                        break;
                    }
            }
    }
    return project;
}
}  // namespace unrealasm
