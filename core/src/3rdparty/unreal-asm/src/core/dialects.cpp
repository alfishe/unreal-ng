#include "unrealasm/dialect.h"

#include <algorithm>
#include <cctype>

#include "unrealasm/layout.h"
#include "unrealasm/registry.h"
#include "dialects/sjasmplus/sjasmplusproject.h"

#include "dialects/alasm/alasmbackend.h"
#include "dialects/alasm/alasmfrontend.h"
#include "dialects/common/z80.h"
#include "dialects/storm/stormbackend.h"
#include "dialects/tasm/tasmbackend.h"
#include "dialects/zxasm/zxasmbackend.h"
#include "dialects/masm/masmbackend.h"
#include "dialects/gens/gensbackend.h"
#include "dialects/zeus/zeusbackend.h"
#include "dialects/storm/stormfrontend.h"
#include "dialects/zxasm/zxasmfrontend.h"
#include "dialects/xas/xasbackend.h"
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
#include "dialects/lasergenius/lasergeniusfrontend.h"
#include "dialects/pasm/pasmfrontend.h"
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
        r.Add(std::make_unique<dialects::LaserGeniusFrontend>());
        r.Add(std::make_unique<dialects::PasmFrontend>());
        r.Add(std::make_unique<dialects::SjasmplusFrontend>());
        r.Add(std::make_unique<dialects::SjasmplusBackend>());
        r.Add(std::make_unique<dialects::PasmoBackend>());
        r.Add(std::make_unique<dialects::Z88dkBackend>());
        r.Add(std::make_unique<dialects::AlasmBackend>());
        r.Add(std::make_unique<dialects::XasBackend>());
        r.Add(std::make_unique<dialects::StormBackend>());
        r.Add(std::make_unique<dialects::TasmBackend>());
        r.Add(std::make_unique<dialects::ZxasmBackend>());
        r.Add(std::make_unique<dialects::MasmBackend>());
        r.Add(std::make_unique<dialects::GensBackend>());
        r.Add(std::make_unique<dialects::ZeusBackend>());
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

std::vector<std::string> DialectRegistry::FrontendDialects() const
{
    std::vector<std::string> out;
    for (const auto& f : _frontends)
        out.emplace_back(f->Dialect());
    return out;
}

std::vector<std::string> DialectRegistry::BackendDialects() const
{
    std::vector<std::string> out;
    for (const auto& b : _backends)
        out.emplace_back(b->Dialect());
    return out;
}

void DialectRegistry::Add(std::unique_ptr<IFrontend> frontend)
{
    _frontends.push_back(std::move(frontend));
}

void DialectRegistry::Add(std::unique_ptr<IBackend> backend)
{
    _backends.push_back(std::move(backend));
}

namespace
{
/// The labels of a sjasmplus project as its layout computes them, for a backend that folds with them
std::map<std::string, SourceLabel> LabelValues(const std::vector<ProjectFile>& files, size_t main, const BackendOptions& options)
{
    std::map<std::string, SourceLabel> values;
    for (const ProjectFile& f : files)
        if (f.document.dialect != "sjasmplus")
            return values;   // another dialect's layout goes through its conversion (symbols/fromsource.h): not here yet
    layout::LayoutOptions layoutOptions;
    if (options.dataFiles)
        layoutOptions.fileSize = [&options](const std::string& name) -> std::optional<uint64_t> {
            const std::vector<uint8_t> bytes = options.dataFiles(name);
            return bytes.empty() ? std::nullopt : std::optional<uint64_t>(bytes.size());
        };
    // Every line without a label gets one (local, so sjasmplus' .labels keep their parent): its address is $ there
    std::vector<ProjectFile> labelled = files;
    const IFrontend* frontend = DialectRegistry::Builtin().Frontend("sjasmplus");
    for (ProjectFile& f : labelled)
    {
        std::string file;
        if (files.size() > 1)
            for (const char c : f.name)
                file.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
        const ir::Program program = frontend->Parse(f.document).program;
        int depth = 0;   // inside MACRO, DUP / REPT, WHILE: $ is not one address
        bool parent = false;   // a label without a dot came: before it a .label would itself become the parent
        for (const ir::Line& l : program.lines)
        {
            if (!l.label.empty() && l.label[0] != '.')
                parent = true;
            int opens = 0, closes = 0;
            for (const ir::Statement& s : l.statements)
                if (s.kind == ir::Statement::Kind::Directive)
                {
                    opens += s.directive == ir::DirectiveKind::Macro || s.directive == ir::DirectiveKind::Repeat || s.directive == ir::DirectiveKind::While;
                    closes += s.directive == ir::DirectiveKind::EndMacro || s.directive == ir::DirectiveKind::EndRepeat || s.directive == ir::DirectiveKind::EndWhile;
                }
            const size_t index = l.sourceLine - 1;
            if (parent && depth == 0 && opens == 0 && l.label.empty() && !l.statements.empty() && index < f.document.lines.size())
            {
                std::string& text = f.document.lines[index].text;
                if (!text.empty() && (text[0] == ' ' || text[0] == '\t'))
                    text = ".__UNREALASM_L" + file + "_" + std::to_string(l.sourceLine) + text;
            }
            depth += opens - closes;
        }
    }
    const layout::LayoutResult laid = layout::Layout(labelled, main, layoutOptions);
    for (const layout::Label& l : laid.labels)
        values[l.name] = {l.value, files.size() == 1 ? std::string() : l.file, l.line, l.use == layout::LabelUse::Defl};
    return values;
}
}  // namespace

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
    BackendOptions withValues = options;
    if (backend->WantsLabelValues() && options.labelValues.empty())
        withValues.labelValues = LabelValues({{source.name, source}}, 0, options);
    BackendResult written = backend->Write(parsed.program, withValues);
    result.document = std::move(written.document);
    result.document.name = source.name;
    result.diagnostics = std::move(parsed.diagnostics);
    result.diagnostics.insert(result.diagnostics.end(), written.diagnostics.begin(), written.diagnostics.end());
    if (targetDialect == "sjasmplus" && source.dialect != "sjasmplus")
    {
        // As a project of one file (ConvertProject)
        std::vector<ProjectFile> one = {{source.name, std::move(result.document)}};
        dialects::FinishSjasmplusProject(one, {false}, options, result.diagnostics);
        result.document = std::move(one[0].document);
    }
    result.ok = !HasErrors(result.diagnostics);
    return result;
}
WriteAsResult WriteAs(const SourceDocument& source, std::string_view codecId, const std::string& version, const std::string& name)
{
    WriteAsResult result;
    const ISourceCodec* codec = CodecRegistry::Builtin().Find(codecId);
    if (!codec)
    {
        result.error = "no codec " + std::string(codecId);
        return result;
    }
    const std::string dialect = codec->Info().dialect.empty() ? std::string(codecId) : codec->Info().dialect;
    SourceDocument document = source;
    if (document.dialect != dialect)
    {
        if (!DialectRegistry::Builtin().Backend(dialect))
        {
            result.error = "no backend writes " + codec->Info().title + "'s dialect (" + dialect + ") yet";
            return result;
        }
        BackendOptions options;
        options.targetVersion = version;
        ConvertResult converted = Convert(source, dialect, options);
        result.diagnostics = std::move(converted.diagnostics);
        if (!converted.ok)
        {
            result.error = "the conversion from " + source.dialect + " to " + dialect + " failed";
            return result;
        }
        document = std::move(converted.document);
    }
    document.name = (name.empty() ? source.name : name).substr(0, 8);
    document.format.clear();
    EncodeOptions encode;
    encode.subversion = version;
    EncodeResult encoded = codec->Encode(document, encode);
    result.diagnostics.insert(result.diagnostics.end(), encoded.diagnostics.begin(), encoded.diagnostics.end());
    if (!encoded.ok)
    {
        result.error = codec->Info().title + "'s format cannot hold this text";
        return result;
    }
    result.bytes = std::move(encoded.bytes);
    result.document = std::move(document);
    result.ok = true;
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
    if (backend->WantsLabelValues() && options.labelValues.empty())
        for (size_t main = 0; main < programs.size(); ++main)
            if (!included[main])
            {
                withMacros.labelValues = LabelValues(files, main, options);
                break;
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
    // A conversion to sjasmplus: what needs the whole written project (the byte operands the source's assembler
    // truncates without a word, the object saver, ORG pages)
    bool converted = false;
    for (const ProjectFile& f : files)
        converted = converted || f.document.dialect != "sjasmplus";
    if (targetDialect == "sjasmplus" && converted)
        dialects::FinishSjasmplusProject(result.files, included, options, result.diagnostics);
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
        // Tokenized sources; of the text ones only Power Assembler's, which it keeps as CODE files on the disk and the
        // detection knows by what only PASM writes (other text files on a disk are documents)
        const DetectResult detected = registry.Detect(files[k].data, files[k].Hints());
        if (detected.chosen && (detected.chosen->Info().family == CodecFamily::Tokenized || detected.chosen->Info().id == "pasm"))
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

std::vector<ProjectFile> HostProject(const SourceDocument& main, const DataFileReader& read, Diagnostics& diagnostics)
{
    std::vector<ProjectFile> project = {{main.name, main}};
    std::vector<std::string> folders = {""};   // each file's folder, relative to main's
    const CodecRegistry& registry = CodecRegistry::Builtin();
    const ISourceCodec* codec = registry.Find(main.dialect);
    if (!codec || codec->Info().family != CodecFamily::Text)
        codec = registry.Find("text");
    const IFrontend* frontend = DialectRegistry::Builtin().Frontend(main.dialect);
    if (!frontend || !codec)
        return project;
    for (size_t k = 0; k < project.size(); ++k)
    {
        const ir::Program program = frontend->Parse(project[k].document).program;
        for (const ir::Line& line : program.lines)
            for (const ir::Statement& s : line.statements)
            {
                if (s.kind != ir::Statement::Kind::Directive || s.directive != ir::DirectiveKind::Include || s.text.empty())
                    continue;
                if (std::any_of(project.begin(), project.end(), [&](const ProjectFile& f) { return &f != &project[0] && f.name == s.text; }))
                    continue;
                const bool verbatim = std::find(s.params.begin(), s.params.end(), "verbatim") != s.params.end();
                const bool absolute = s.text[0] == '/' || s.text[0] == '\\' || (s.text.size() > 1 && s.text[1] == ':');
                const std::string path = (absolute ? std::string() : folders[k]) + s.text + (verbatim ? "" : ".asm");
                const std::vector<uint8_t> bytes = read(path);
                if (bytes.empty())
                {
                    diagnostics.push_back({Severity::Warning, line.sourceLine, 0,
                                           project[k].name + ": INCLUDE \"" + path + "\" is not on the host: the assembler reads it from its disk"});
                    continue;
                }
                DecodeResult decoded = codec->Decode(bytes, {});
                decoded.document.dialect = main.dialect;
                decoded.document.name = s.text;
                const size_t slash = path.find_last_of("/\\");
                folders.push_back(slash == std::string::npos ? std::string() : path.substr(0, slash + 1));
                project.push_back({s.text, std::move(decoded.document)});
            }
    }
    return project;
}

std::vector<std::string> ProjectTextNames(const std::vector<ProjectFile>& files, const std::string& title)
{
    const auto upper = [](std::string text) {
        for (char& c : text)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return text;
    };
    std::vector<std::string> names;
    for (size_t k = 0; k < files.size(); ++k)
    {
        if (k == 0)
        {
            names.push_back(title.substr(0, 8));
            continue;
        }
        const size_t slash = files[k].name.find_last_of("/\\");
        std::string name = files[k].name.substr(slash == std::string::npos ? 0 : slash + 1);
        name = upper(name.substr(0, name.find('.'))).substr(0, 8);
        if (name.empty())
            name = "TEXT";
        const std::string base = name;
        for (int n = 2; std::any_of(names.begin(), names.end(), [&](const std::string& taken) { return upper(taken) == name; }); ++n)
            name = base.substr(0, 7 - std::to_string(n).size()) + "~" + std::to_string(n);
        names.push_back(name);
    }
    return names;
}

WriteProjectResult WriteProjectAs(const std::vector<ProjectFile>& files, std::string_view codecId, const std::string& version,
                                  const std::vector<std::string>& names, const std::map<std::string, std::string>& includeAs)
{
    WriteProjectResult result;
    const ISourceCodec* codec = CodecRegistry::Builtin().Find(codecId);
    if (!codec)
    {
        result.error = "no codec " + std::string(codecId);
        return result;
    }
    if (names.size() != files.size())
    {
        result.error = "a name for each file of the project";
        return result;
    }
    const std::string dialect = codec->Info().dialect.empty() ? std::string(codecId) : codec->Info().dialect;
    std::vector<SourceDocument> documents;
    if (std::all_of(files.begin(), files.end(), [&](const ProjectFile& f) { return f.document.dialect == dialect; }))
        for (const ProjectFile& f : files)
            documents.push_back(f.document);
    else
    {
        if (!DialectRegistry::Builtin().Backend(dialect))
        {
            result.error = "no backend writes " + codec->Info().title + "'s dialect (" + dialect + ") yet";
            return result;
        }
        BackendOptions options;
        options.targetVersion = version;
        options.includeAs = includeAs;
        ProjectResult converted = ConvertProject(files, dialect, options);
        result.diagnostics = std::move(converted.diagnostics);
        if (!converted.ok)
        {
            result.error = "the conversion of the project to " + dialect + " failed";
            return result;
        }
        for (ProjectFile& f : converted.files)
            documents.push_back(std::move(f.document));
    }
    for (size_t k = 0; k < documents.size(); ++k)
    {
        WriteAsResult written;
        written.document = std::move(documents[k]);
        written.document.name = names[k].substr(0, 8);
        written.document.format.clear();
        EncodeOptions encode;
        encode.subversion = version;
        EncodeResult encoded = codec->Encode(written.document, encode);
        for (Diagnostic d : encoded.diagnostics)
            result.diagnostics.push_back({d.severity, d.line, d.byteOffset, files[k].name + ": " + d.message});
        if (!encoded.ok)
        {
            result.error = files[k].name + ": " + codec->Info().title + "'s format cannot hold this text";
            return result;
        }
        written.bytes = std::move(encoded.bytes);
        written.ok = true;
        result.files.push_back(std::move(written));
    }
    result.ok = true;
    return result;
}
}  // namespace unrealasm
