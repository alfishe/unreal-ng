#include "unrealasm/dialect.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <set>

#include "unrealasm/layout.h"
#include "unrealasm/symbols/fromsource.h"
#include "unrealasm/registry.h"
#include "dialects/sjasmplus/sjasmplusproject.h"

#include "dialects/alasm/alasmbackend.h"
#include "dialects/alasm/alasmfrontend.h"
#include "dialects/common/classicbackend.h"
#include "dialects/common/z80.h"
#include "dialects/storm/stormbackend.h"
#include "dialects/tasm/tasmbackend.h"
#include "dialects/zxasm/zxasmbackend.h"
#include "dialects/masm/masmbackend.h"
#include "dialects/gens/gensbackend.h"
#include "dialects/zeus/zeusbackend.h"
#include "dialects/lasergenius/lasergeniusbackend.h"
#include "dialects/prometheus/prometheusbackend.h"
#include "dialects/pasm/pasmbackend.h"
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
        r.Add(std::make_unique<dialects::LaserGeniusBackend>());
        r.Add(std::make_unique<dialects::PrometheusBackend>());
        r.Add(std::make_unique<dialects::PasmBackend>());
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
/// The size of the file an INCBIN names, for the layout: the one the caller gives (a catalog length), else the data
/// file's (which may hold a sector's slack)
std::function<std::optional<uint64_t>(const std::string&)> IncbinSize(const BackendOptions& options)
{
    if (options.fileSize)
        return options.fileSize;
    if (!options.dataFiles)
        return nullptr;
    return [&options](const std::string& name) -> std::optional<uint64_t> {
        const std::vector<uint8_t> bytes = options.dataFiles(name);
        return bytes.empty() ? std::nullopt : std::optional<uint64_t>(bytes.size());
    };
}

/// The labels of a sjasmplus project as its layout computes them, for a backend that folds with them (project: the
/// labels name their files)
std::map<std::string, SourceLabel> LabelValues(const std::vector<ProjectFile>& files, size_t main, const BackendOptions& options,
                                               bool project)
{
    std::map<std::string, SourceLabel> values;
    if (std::any_of(files.begin(), files.end(), [](const ProjectFile& f) { return f.document.dialect != "sjasmplus"; }))
    {
        // another dialect: its labels through its sjasmplus conversion's layout, under their names in the source (the
        // global ones, not a block's local labels), and its lines' addresses
        if (std::any_of(files.begin(), files.end(), [&](const ProjectFile& f) { return f.document.dialect != files[main].document.dialect; }))
            return values;
        symbols::SourceSymbolsOptions symbolOptions;
        symbolOptions.layout.fileSize = IncbinSize(options);
        const symbols::SourceSymbolsResult found = symbols::SymbolsFromProject(files, main, symbolOptions);
        for (const symbols::Symbol& symbol : found.set.symbols)
            if (symbol.parent.empty() && symbol.kind != symbols::SymbolKind::Local && !values.count(symbol.name))
                values[symbol.name] = {static_cast<int64_t>(symbol.location.offset), !project ? std::string() : symbol.source.file,
                                       symbol.source.line, false};
        // each source line's address, for $ there: __UNREALASM_S<file>_<line> (the file named only in a project)
        for (const layout::LineAddress& a : found.lines)
        {
            std::string file;
            if (project)
                for (const char c : a.file)
                    file.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
            values["__UNREALASM_S" + file + "_" + std::to_string(a.line)] = {a.address, !project ? std::string() : a.file, a.line, false};
        }
        return values;
    }
    layout::LayoutOptions layoutOptions;
    layoutOptions.fileSize = IncbinSize(options);
    const layout::LayoutResult laid = layout::Layout(files, main, layoutOptions);
    for (const layout::Label& l : laid.labels)
    {
        const SourceLabel value = {l.value, !project ? std::string() : l.file, l.line, l.use == layout::LabelUse::Defl};
        values[l.name] = value;
    }
    // the RELOCATE_TABLE words: __UNREALASM_RELOC_<k>
    for (size_t k = 0; k < laid.relocations.size(); ++k)
        values["__UNREALASM_RELOC_" + std::to_string(k)] = {laid.relocations[k], std::string(), 0, false};
    // each line's address, for $ there: __UNREALASM_L<file>_<line> (the file named only in a project)
    for (const layout::LineAddress& a : laid.lines)
    {
        std::string file;
        if (project)
            for (const char c : a.file)
                file.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
        const std::string key = a.bodyLine ? "__UNREALASM_M" + file + "_" + std::to_string(a.line) + "_" + std::to_string(a.bodyLine)
                                           : "__UNREALASM_L" + file + "_" + std::to_string(a.line);
        values[key] = {a.address, !project ? std::string() : a.file, a.line, false};
    }
    // how often each WHILE ran: __UNREALASM_W<file>_<line> (a target without a loop that tests writes a repeat count)
    for (const layout::WhileCount& w : laid.whileCounts)
    {
        std::string file;
        if (project)
            for (const char c : w.file)
                file.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
        values["__UNREALASM_W" + file + "_" + std::to_string(w.line)] = {w.count, !project ? std::string() : w.file, w.line, false};
    }
    return values;
}

/// The names an expression mentions
void Mentioned(const ir::Expr& e, std::set<std::string>& names)
{
    if (e.kind == ir::Expr::Kind::Symbol)
        names.insert(e.text);
    for (const ir::Expr& a : e.args)
        Mentioned(a, names);
}

/// The names a macro argument's text mentions
void MentionedInText(const std::string& text, std::set<std::string>& names)
{
    for (size_t k = 0; k < text.size();)
    {
        const auto word = [&](size_t at) {
            const unsigned char c = static_cast<unsigned char>(text[at]);
            return std::isalnum(c) || c == '_' || c == '.' || c >= 0x80;
        };
        if (!word(k))
        {
            ++k;
            continue;
        }
        size_t end = k;
        while (end < text.size() && word(end))
            ++end;
        if (!std::isdigit(static_cast<unsigned char>(text[k])))
            names.insert(text.substr(k, end - k));
        k = end;
    }
}

/// Constants a main source defines before its first line (name EQU 1 / 0)
void DefineFirst(ir::Program& program, const std::vector<std::pair<std::string, bool>>& defined)
{
    std::vector<ir::Line> first;
    for (const auto& [name, value] : defined)
    {
        ir::Line line;
        line.sourceLine = program.lines.empty() ? 0 : program.lines.front().sourceLine;
        line.label = name;
        ir::Statement equ;
        equ.kind = ir::Statement::Kind::Directive;
        equ.directive = ir::DirectiveKind::Equ;
        equ.args = {ir::Expr::Number(value ? 1 : 0)};
        line.statements.push_back(std::move(equ));
        first.push_back(std::move(line));
    }
    program.lines.insert(program.lines.begin(), first.begin(), first.end());
}

/// ZX-ASM's IFUSED X / IFNUSED X (X used and not defined so far) decided for a target that has no such test, as an IF
/// of 1 or 0. X is used when the code an assembly assembles mentions it, not a block it leaves out: per assembly (a
/// main source with the files it INCLUDEs, in assembly order), from "nothing used" until the answers stay. A test that
/// two assemblies answer differently, each one way, tests a constant every main source reaching it defines first
/// (__UNREALASM_IFU<n> EQU 1 / 0); one that two INCLUDEs of a file in one assembly answer differently stays a test
void DecideIfUsed(std::vector<ir::Program>& programs, const std::vector<std::string>& names, const std::vector<bool>& included)
{
    using Answers = std::map<const ir::Statement*, std::optional<bool>>;   // nullopt: answered both ways
    bool any = false;
    for (const ir::Program& p : programs)
        for (const ir::Line& l : p.lines)
            for (const ir::Statement& s : l.statements)
                any = any || (s.kind == ir::Statement::Kind::Directive && s.directive == ir::DirectiveKind::IfUsed);
    if (!any)
        return;
    const auto negated = [](const ir::Statement& s) { return !s.params.empty() && s.params[0] == "not"; };
    const auto merge = [](Answers& into, const ir::Statement* s, std::optional<bool> v) {
        const auto [at, fresh] = into.emplace(s, v);
        if (!fresh && at->second != v)
            at->second = std::nullopt;
    };
    Answers project;
    struct Assembly
    {
        size_t main = 0;
        Answers answers;
        std::set<size_t> files;   // the files it reaches
    };
    std::vector<Assembly> assemblies;
    for (size_t main = 0; main < programs.size(); ++main)
    {
        if (included[main])
            continue;
        Answers answers;
        std::set<size_t> reached;
        for (int round = 0; round < 32; ++round)
        {
            struct Open
            {
                const ir::Statement* test = nullptr;   // nullptr: an IF of another kind (both parts count)
                bool inElse = false;
            };
            std::set<std::string> used, defined;
            std::vector<std::pair<const ir::Statement*, bool>> tests;   // each test reached, X defined before it
            std::vector<size_t> active;
            const auto answer = [&](const ir::Statement* s) -> std::optional<bool> {
                const auto at = answers.find(s);
                return at == answers.end() ? std::optional<bool>(negated(*s)) : at->second;
            };
            std::function<void(size_t)> walk = [&](size_t file) {
                if (std::count(active.begin(), active.end(), file) || active.size() > 32)
                    return;
                active.push_back(file);
                reached.insert(file);
                std::vector<Open> open;
                const auto taken = [&] {
                    for (const Open& o : open)
                        if (o.test)
                            if (const auto a = answer(o.test); a && *a == o.inElse)
                                return false;
                    return true;
                };
                for (const ir::Line& l : programs[file].lines)
                {
                    if (!l.label.empty() && taken())
                        defined.insert(l.label);
                    for (const ir::Statement& s : l.statements)
                    {
                        if (s.kind == ir::Statement::Kind::Directive)
                        {
                            if (s.directive == ir::DirectiveKind::IfUsed || s.directive == ir::DirectiveKind::If)
                            {
                                const bool test = s.directive == ir::DirectiveKind::IfUsed;
                                if (test && taken())
                                    tests.push_back({&s, defined.count(s.text) > 0});
                                if (!test && taken())
                                    for (const ir::Expr& a : s.args)
                                        Mentioned(a, used);
                                open.push_back({test ? &s : nullptr, false});
                                continue;
                            }
                            if (s.directive == ir::DirectiveKind::Else && !open.empty())
                            {
                                open.back().inElse = true;
                                continue;
                            }
                            if (s.directive == ir::DirectiveKind::EndIf && !open.empty())
                            {
                                open.pop_back();
                                continue;
                            }
                        }
                        if (!taken())
                            continue;
                        for (const ir::Expr& a : s.args)
                            Mentioned(a, used);
                        for (const ir::Operand& o : s.operands)
                            Mentioned(o.expr, used);
                        if (s.kind == ir::Statement::Kind::MacroCall)
                            for (const std::string& p : s.params)
                                MentionedInText(p, used);
                        if (s.kind == ir::Statement::Kind::Directive && s.directive == ir::DirectiveKind::Include)
                            for (size_t n = 0; n < programs.size(); ++n)
                                if (n != file && names[n] == s.text)
                                {
                                    walk(n);
                                    break;
                                }
                    }
                }
                active.pop_back();
            };
            walk(main);
            Answers next;
            for (const auto& [s, before] : tests)
                merge(next, s, (used.count(s->text) > 0 && !before) != negated(*s));
            if (next == answers)
                break;
            answers = std::move(next);
        }
        for (const auto& [s, v] : answers)
            merge(project, s, v);
        assemblies.push_back({main, std::move(answers), std::move(reached)});
    }
    std::map<size_t, std::vector<std::pair<std::string, bool>>> constants;   // a main source's: name, value
    int flags = 0;
    for (size_t file = 0; file < programs.size(); ++file)
        for (ir::Line& l : programs[file].lines)
            for (ir::Statement& s : l.statements)
                if (const auto at = project.find(&s); at != project.end())
                {
                    std::optional<ir::Expr> value;
                    if (at->second)
                        value = ir::Expr::Number(*at->second ? 1 : 0);
                    else
                    {
                        bool each = true;   // every assembly answers it one way
                        for (const Assembly& a : assemblies)
                            if (const auto own = a.answers.find(&s); own != a.answers.end() && !own->second)
                                each = false;
                        if (!each)
                            continue;
                        const std::string name = "__UNREALASM_IFU" + std::to_string(++flags);
                        for (const Assembly& a : assemblies)
                            if (a.files.count(file))
                            {
                                const auto own = a.answers.find(&s);   // not reached: in a block left out, either value
                                constants[a.main].push_back({name, own != a.answers.end() && *own->second});
                            }
                        value = ir::Expr::Make(ir::Expr::Kind::Symbol);
                        value->text = name;
                    }
                    const std::string test = (negated(s) ? "IFNUSED " : "IFUSED ") + s.text;
                    s.directive = ir::DirectiveKind::If;
                    s.args = {*value};
                    s.params.clear();
                    s.text.clear();
                    if (!l.hasComment)
                    {
                        l.comment = " " + test + (at->second ? " (decided when converting)" : " (decided per main source)");
                        l.hasComment = true;
                    }
                }
    for (const auto& [main, defined] : constants)
        DefineFirst(programs[main], defined);
}

/// An IF of a file several assemblies INCLUDE that their layouts answer differently, each one way: it tests a
/// constant every main source reaching it defines first (__UNREALASM_IFL<n> EQU 1 / 0), as an IFUSED test does
/// (LDLD060 in Info Guide #10: IF high rd&#40, rd in conlt60 for UNRLT60 and in const55 for mkunr060)
void DecideIfPerMain(std::vector<ir::Program>& programs, const std::vector<std::vector<size_t>>& unitOf,
                     const std::function<const std::map<std::string, SourceLabel>&(size_t)>& valuesOf)
{
    std::map<size_t, std::vector<std::pair<std::string, bool>>> constants;   // a main source's: name, value
    int flags = 0;
    for (size_t file = 0; file < programs.size(); ++file)
    {
        std::vector<size_t> mains;
        for (size_t main = 0; main < unitOf.size(); ++main)
            if (std::find(unitOf[main].begin(), unitOf[main].end(), file) != unitOf[main].end())
                mains.push_back(main);
        if (mains.size() < 2)
            continue;
        bool inMacro = false;
        for (ir::Line& l : programs[file].lines)
            for (ir::Statement& s : l.statements)
            {
                if (s.kind != ir::Statement::Kind::Directive)
                    continue;
                if (s.directive == ir::DirectiveKind::Macro || s.directive == ir::DirectiveKind::EndMacro)
                    inMacro = s.directive == ir::DirectiveKind::Macro;
                if (inMacro || s.directive != ir::DirectiveKind::If || s.args.size() != 1)
                    continue;
                std::vector<std::pair<size_t, bool>> answers;
                bool each = true;
                for (const size_t main : mains)
                {
                    const auto v = dialects::LayoutValue(s.args[0], valuesOf(main));
                    each = each && v.has_value();
                    if (v)
                        answers.push_back({main, *v != 0});
                }
                if (!each || std::all_of(answers.begin(), answers.end(), [&](const auto& a) { return a.second == answers[0].second; }))
                    continue;
                const std::string name = "__UNREALASM_IFL" + std::to_string(++flags);
                for (const auto& [main, value] : answers)
                    constants[main].push_back({name, value});
                ir::Expr test = ir::Expr::Make(ir::Expr::Kind::Symbol);
                test.text = name;
                s.args = {test};
                if (!l.hasComment)
                {
                    l.comment = " the condition decided per main source";
                    l.hasComment = true;
                }
            }
    }
    for (const auto& [main, defined] : constants)
        DefineFirst(programs[main], defined);
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
    if (targetDialect != "sjasmplus" && targetDialect != "zxasm")
    {
        std::vector<ir::Program> one;
        one.push_back(std::move(parsed.program));
        DecideIfUsed(one, {source.name}, {false});
        parsed.program = std::move(one[0]);
    }
    BackendOptions withValues = options;
    if (backend->WantsLabelValues() && options.labelValues.empty())
        withValues.labelValues = LabelValues({{source.name, source}}, 0, options, false);
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

namespace
{
constexpr std::string_view kFileMark = "UNREALASM@FILE:";   // a comment line opening the text of an INCLUDEd file
constexpr std::string_view kEndMark = "UNREALASM@END:";     // ... and closing it

std::string Trimmed(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t");
    return first == std::string::npos ? std::string() : text.substr(first);
}

std::string LowerText(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/// A file name without its folders and its last extension
std::string Stem(const std::string& name)
{
    const size_t slash = name.find_last_of("/\\");
    std::string base = slash == std::string::npos ? name : name.substr(slash + 1);
    const size_t dot = base.rfind('.');
    return dot == std::string::npos || dot == 0 ? base : base.substr(0, dot);
}

/// The project file an INCLUDE names: the name as it is given, else the same in another case, else the same file name
/// without folders and extension (a folder converted as a project names its files so)
std::optional<size_t> IncludedFile(const std::vector<ProjectFile>& files, const std::string& text)
{
    for (size_t k = 0; k < files.size(); ++k)
        if (files[k].name == text)
            return k;
    for (size_t k = 0; k < files.size(); ++k)
        if (LowerText(files[k].name) == LowerText(text))
            return k;
    for (size_t k = 0; k < files.size(); ++k)
        if (LowerText(Stem(files[k].name)) == LowerText(Stem(text)))
            return k;
    return std::nullopt;
}

/// A sjasmplus project as its assembler reads it: every main source (one no other file INCLUDEs) with the text of each
/// file it INCLUDEs put in after the INCLUDE line, between marks; converted as one source, then cut into its files
class ProjectJoiner
{
public:
    ProjectJoiner(const std::vector<ProjectFile>& files, const IBackend& backend, const BackendOptions& options)
        : _files(files), _backend(backend), _options(options)
    {
    }

    ProjectResult Run()
    {
        const IFrontend* frontend = DialectRegistry::Builtin().Frontend("sjasmplus");
        // the INCLUDE lines of each file (outside macro bodies, which sjasmplus reads where they are expanded)
        _includes.resize(_files.size());
        _safe.resize(_files.size());
        std::vector<bool> included(_files.size(), false);
        for (size_t k = 0; k < _files.size(); ++k)
        {
            int depth = 0;
            int blocks = 0;   // any block open (MACRO, DUP, WHILE, IF..., STRUCT, LUA, DISP): no cut inside one
            for (const ir::Line& l : frontend->Parse(_files[k].document).program.lines)
            {
                for (const ir::Statement& s : l.statements)
                {
                    const std::string word = s.kind == ir::Statement::Kind::Directive && s.directive == ir::DirectiveKind::Other
                                                 ? dialects::z80::Upper(Trimmed(s.text).substr(0, Trimmed(s.text).find_first_of(" \t")))
                                                 : std::string();
                    if (s.kind == ir::Statement::Kind::Directive)
                    {
                        const auto d = s.directive;
                        blocks += d == ir::DirectiveKind::Macro || d == ir::DirectiveKind::Repeat || d == ir::DirectiveKind::While ||
                                  d == ir::DirectiveKind::If || d == ir::DirectiveKind::Disp || word == "IFDEF" || word == "IFNDEF" ||
                                  word == "IFUSED" || word == "IFNUSED" || word == "STRUCT" || word == "LUA";
                        blocks -= d == ir::DirectiveKind::EndMacro || d == ir::DirectiveKind::EndRepeat || d == ir::DirectiveKind::EndWhile ||
                                  d == ir::DirectiveKind::EndIf || d == ir::DirectiveKind::Ent || word == "ENDS" || word == "ENDLUA";
                    }
                    if (s.kind != ir::Statement::Kind::Directive)
                        continue;
                    depth += s.directive == ir::DirectiveKind::Macro;
                    depth -= s.directive == ir::DirectiveKind::EndMacro;
                    if (s.directive != ir::DirectiveKind::Include || depth > 0)
                        continue;
                    if (const auto n = IncludedFile(_files, s.text); n && *n != k)
                    {
                        _includes[k][l.sourceLine] = *n;
                        included[*n] = true;
                    }
                }
                if (blocks <= 0)
                    _safe[k].insert(l.sourceLine);
                blocks = std::max(0, blocks);
            }
        }
        _written.resize(_files.size());
        _labels.resize(_files.size());
        for (size_t main = 0; main < _files.size(); ++main)
            if (!included[main])
                Program(main);
        // a file joined into a main one (a target without INCLUDE): a note in its place
        for (const size_t k : _inlined)
            if (!_written[k])
            {
                SourceDocument note;
                note.name = _files[k].name;
                note.dialect = std::string(_backend.Dialect());
                note.lines.push_back({";in the file INCLUDing it", {}, -1});   // short: PROMETHEUS' line holds 63 bytes
                _written[k] = std::move(note);
                _labels[k] = {};
            }
        // files only a cycle of INCLUDEs reaches: each as a main of its own
        for (size_t k = 0; k < _files.size(); ++k)
            if (!_written[k])
                Program(k);
        for (size_t k = 0; k < _files.size(); ++k)
        {
            _result.files.push_back({_files[k].name, std::move(*_written[k])});
            _result.labels.push_back(std::move(_labels[k]));
        }
        for (Copy& c : _copies)
        {
            _result.files.push_back({c.name, std::move(c.document)});
            _result.labels.push_back(std::move(c.labels));
        }
        _result.ok = !HasErrors(_result.diagnostics);
        return std::move(_result);
    }

private:
    struct Region
    {
        size_t file = 0;
        uint32_t includeLine = 0;   // the joined text's line of the INCLUDE that brings it in (0: the main source)
        std::string name;           // the name its INCLUDE is written with (a copy: name~2)
        bool part = false;          // a part a long file was cut into (BackendOptions::splitLines)
    };
    bool _inlineIncludes = false;   // the target has no INCLUDE: the INCLUDEd texts stay in the main file
    std::set<size_t> _inlined;      // the files written into a main file so

    struct Copy
    {
        size_t file = 0;
        std::string name;
        SourceDocument document;
        std::vector<LabelName> labels;
    };

    void Join(size_t k, std::vector<size_t>& stack)
    {
        const SourceDocument& d = _files[k].document;
        size_t since = 0;                // lines since the file's last cut
        std::optional<size_t> part;      // the part being written (a region of the file's own)
        for (size_t i = 0; i < d.lines.size(); ++i)
        {
            _joined.lines.push_back(d.lines[i]);
            _origin.push_back({k, static_cast<uint32_t>(i + 1)});
            ++since;
            const auto found = _includes[k].find(static_cast<uint32_t>(i + 1));
            if (found != _includes[k].end() && std::find(stack.begin(), stack.end(), found->second) == stack.end() && _inlineIncludes)
            {
                // a target without INCLUDE: the text stays here, the INCLUDE a comment
                _joined.lines.back().text = ";" + _joined.lines.back().text;
                _inlined.insert(found->second);
                stack.push_back(found->second);
                Join(found->second, stack);
                stack.pop_back();
                continue;
            }
            if (found != _includes[k].end() && std::find(stack.begin(), stack.end(), found->second) == stack.end())
            {
                const size_t id = _regions.size();
                _regions.push_back({found->second, static_cast<uint32_t>(_joined.lines.size()), _files[found->second].name});
                Mark(std::string(kFileMark) + std::to_string(id), found->second);
                stack.push_back(found->second);
                Join(found->second, stack);
                stack.pop_back();
                Mark(std::string(kEndMark) + std::to_string(id), k);
            }
            // a long file cut at a line outside any block: the rest goes on in a part INCLUDEd from the file
            if (_writing && _writing->splitLines > 0 && since >= _writing->splitLines && i + 1 < d.lines.size() && _safe[k].count(static_cast<uint32_t>(i + 1)))
            {
                if (part)
                    Mark(std::string(kEndMark) + std::to_string(*part), k);
                const std::string name = CopyName(k, *_writing);
                SourceLine include;
                include.text = "\tINCLUDE \"" + name + "\"";
                _joined.lines.push_back(include);
                _origin.push_back({k, static_cast<uint32_t>(i + 1)});
                part = _regions.size();
                _regions.push_back({k, static_cast<uint32_t>(_joined.lines.size()), name, true});
                Mark(std::string(kFileMark) + std::to_string(*part), k);
                since = 0;
            }
        }
        if (part)
            Mark(std::string(kEndMark) + std::to_string(*part), k);
    }

    /// The INCLUDE that brings a region in names it as the region is named
    static void Rename(ir::Program& program, const Region& region)
    {
        for (ir::Line& l : program.lines)
            if (l.sourceLine == region.includeLine)
                for (ir::Statement& s : l.statements)
                    if (s.kind == ir::Statement::Kind::Directive && s.directive == ir::DirectiveKind::Include)
                        s.text = region.name;
    }

    void Mark(const std::string& text, size_t file)
    {
        SourceLine line;
        line.text = ";" + text;
        _joined.lines.push_back(line);
        _origin.push_back({file, 0});
    }

    /// The region a mark line opens or closes (-1: none)
    static int MarkOf(const std::string& text, std::string_view mark)
    {
        const size_t at = text.find(mark);
        if (at == std::string::npos)
            return -1;
        size_t end = at + mark.size();
        int id = 0;
        bool digits = false;
        while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])))
        {
            id = id * 10 + (text[end++] - '0');
            digits = true;
        }
        return digits ? id : -1;
    }

    void Program(size_t main)
    {
        _joined = SourceDocument();
        _joined.name = _files[main].name;
        _joined.dialect = "sjasmplus";
        _joined.z80n = _files[main].document.z80n;
        _origin.clear();
        _regions = {{main, 0, _files[main].name}};
        BackendOptions options = _options;
        options.fileName.clear();
        _writing = &options;   // the parts' names go into its fileNames
        _inlineIncludes = !_backend.Includes(options);
        std::vector<size_t> stack = {main};
        Join(main, stack);
        for (const auto& f : _files)
            _joined.z80n = _joined.z80n || f.document.z80n;

        const IFrontend* frontend = DialectRegistry::Builtin().Frontend("sjasmplus");
        FrontendResult parsed = frontend->ParseWithData(_joined, {}, _options.dataFiles);
        // each INCLUDE joined names its file as the project does (fileNames knows it so)
        for (size_t r = 1; r < _regions.size(); ++r)
            Rename(parsed.program, _regions[r]);
        if (_backend.WantsLabelValues() && options.labelValues.empty())
        {
            // laid out as one source (the INCLUDE lines left out: their text follows them)
            SourceDocument laid = _joined;
            for (size_t r = 1; r < _regions.size(); ++r)
                laid.lines[_regions[r].includeLine - 1].text = ";" + laid.lines[_regions[r].includeLine - 1].text;
            options.labelValues = LabelValues({{_joined.name, laid}}, 0, options, false);
        }
        BackendResult written = _backend.Write(parsed.program, options);
        std::vector<std::vector<SourceLine>> texts;
        std::vector<std::vector<LabelName>> labels;
        Cut(written, texts, labels);
        // a file brought in twice and written otherwise the second time (another module): a copy of its own, its
        // INCLUDE naming the copy
        bool renamed = false;
        for (size_t r = 1; r < _regions.size(); ++r)
        {
            if (_regions[r].part)
                continue;
            const size_t file = _regions[r].file;
            std::vector<std::pair<std::string, const std::vector<SourceLine>*>> versions;   // the names it has, their texts
            if (_written[file])
                versions.push_back({_files[file].name, &_written[file]->lines});
            for (const Copy& c : _copies)
                if (c.file == file)
                    versions.push_back({c.name, &c.document.lines});
            for (size_t e = 1; e < r; ++e)
                if (_regions[e].file == file && !_regions[e].part)
                    versions.push_back({_regions[e].name, &texts[e]});
            if (versions.empty())
                continue;
            const auto same = std::find_if(versions.begin(), versions.end(), [&](const auto& v) { return SameLines(*v.second, texts[r]); });
            if (same != versions.end())
                _regions[r].name = same->first;
            else
                _regions[r].name = CopyName(file, options);
            if (_regions[r].name == _files[file].name)
                continue;
            Rename(parsed.program, _regions[r]);
            renamed = true;
        }
        if (renamed)
        {
            written = _backend.Write(parsed.program, options);
            texts.clear();
            labels.clear();
            Cut(written, texts, labels);
        }
        for (const Diagnostic& d : parsed.diagnostics)
            Note(d);
        for (const Diagnostic& d : written.diagnostics)
            Note(d);
        for (size_t r = 0; r < _regions.size(); ++r)
        {
            const size_t file = _regions[r].file;
            SourceDocument document;
            document.name = _regions[r].name;
            document.dialect = written.document.dialect;
            document.format = written.document.format;
            document.subversion = written.document.subversion;
            document.codePage = written.document.codePage;
            document.lineEnd = written.document.lineEnd;
            document.attrs = written.document.attrs;
            document.lines = std::move(texts[r]);
            if (_regions[r].name != _files[file].name)
            {
                if (std::none_of(_copies.begin(), _copies.end(), [&](const Copy& c) { return c.name == _regions[r].name; }))
                    _copies.push_back({file, _regions[r].name, std::move(document), std::move(labels[r])});
            }
            else if (!_written[file])
            {
                _written[file] = std::move(document);
                _labels[file] = std::move(labels[r]);
            }
        }
    }

    /// A new name for another text of a file: NAME~2, NAME~3 ... (NAME2, NAME3 on the disk, when the project's files are
    /// renamed for one: the copy's name is its disk name)
    std::string CopyName(size_t file, BackendOptions& options) const
    {
        const auto disk = options.fileNames.find(_files[file].name);
        const std::string base = disk != options.fileNames.end() ? disk->second : _files[file].name;
        std::set<std::string> taken;
        for (const auto& [from, to] : options.fileNames)
            taken.insert(LowerText(to));
        for (const ProjectFile& f : _files)
            taken.insert(LowerText(f.name));
        for (const Copy& c : _copies)
            taken.insert(LowerText(c.name));
        for (const Region& r : _regions)
            taken.insert(LowerText(r.name));
        std::string name;
        for (int n = 2; name.empty() || taken.count(LowerText(name)); ++n)
            name = disk != options.fileNames.end() ? base.substr(0, 8 - std::to_string(n).size()) + std::to_string(n)
                                                   : base + "~" + std::to_string(n);
        if (disk != options.fileNames.end())
            options.fileNames[name] = name;
        return name;
    }

    static bool SameLines(const std::vector<SourceLine>& a, const std::vector<SourceLine>& b)
    {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const SourceLine& x, const SourceLine& y) { return x.text == y.text; });
    }

    /// The written text cut at the marks into the regions' texts, each line's origin a line of its own file
    void Cut(const BackendResult& written, std::vector<std::vector<SourceLine>>& texts, std::vector<std::vector<LabelName>>& labels) const
    {
        texts.assign(_regions.size(), {});
        labels.assign(_regions.size(), {});
        std::vector<size_t> open = {0};
        for (const SourceLine& line : written.document.lines)
        {
            if (const int id = MarkOf(line.text, kFileMark); id >= 0 && static_cast<size_t>(id) < _regions.size())
            {
                open.push_back(static_cast<size_t>(id));
                continue;
            }
            if (const int id = MarkOf(line.text, kEndMark); id >= 0 && open.size() > 1)
            {
                open.pop_back();
                continue;
            }
            SourceLine kept = line;
            kept.origin = line.origin > 0 && line.origin <= _origin.size() ? _origin[line.origin - 1].second : 0;
            texts[open.back()].push_back(std::move(kept));
        }
        for (LabelName l : written.labels)
        {
            if (l.line == 0 || l.line > _origin.size())
                continue;
            const auto [file, line] = _origin[l.line - 1];
            l.line = line;
            for (size_t r = 0; r < _regions.size(); ++r)
                if (_regions[r].file == file)
                {
                    labels[r].push_back(l);
                    break;
                }
        }
    }

    /// A diagnostic of the joined text, told by its file and line
    void Note(Diagnostic d)
    {
        size_t file = _regions[0].file;
        if (d.line > 0 && d.line <= _origin.size())
        {
            file = _origin[d.line - 1].first;
            d.line = _origin[d.line - 1].second;
        }
        d.message = _files[file].name + ": " + d.message;
        if (std::none_of(_result.diagnostics.begin(), _result.diagnostics.end(),
                         [&](const Diagnostic& e) { return e.line == d.line && e.message == d.message; }))
            _result.diagnostics.push_back(std::move(d));
    }

    const std::vector<ProjectFile>& _files;
    const IBackend& _backend;
    const BackendOptions& _options;
    std::vector<std::map<uint32_t, size_t>> _includes;   // per file: the line of an INCLUDE -> the file it brings in
    std::vector<std::set<uint32_t>> _safe;               // per file: the lines after which no block is open (a cut may go there)
    BackendOptions* _writing = nullptr;                  // the options of the program being written
    SourceDocument _joined;
    std::vector<std::pair<size_t, uint32_t>> _origin;    // per joined line: its file and line (0: a mark)
    std::vector<Region> _regions;
    std::vector<std::optional<SourceDocument>> _written;
    std::vector<std::vector<LabelName>> _labels;
    std::vector<Copy> _copies;
    ProjectResult _result;
};
}  // namespace

ProjectResult ConvertProject(const std::vector<ProjectFile>& files, std::string_view targetDialect, const BackendOptions& options)
{
    if (const IBackend* backend = DialectRegistry::Builtin().Backend(targetDialect); backend && backend->JoinsSjasmplusProject() && !files.empty() &&
        std::all_of(files.begin(), files.end(), [](const ProjectFile& f) { return f.document.dialect == "sjasmplus"; }))
        return ProjectJoiner(files, *backend, options).Run();
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
    std::vector<std::vector<size_t>> includes(programs.size());
    std::vector<bool> included(programs.size(), false);
    std::vector<int> includeCount(programs.size(), 0);   // the INCLUDEs naming each file
    for (size_t k = 0; k < programs.size(); ++k)
        for (const ir::Line& line : programs[k].lines)
            for (const ir::Statement& s : line.statements)
                if (s.kind == ir::Statement::Kind::Directive && s.directive == ir::DirectiveKind::Include)
                    for (size_t n = 0; n < files.size(); ++n)
                        if (n != k && files[n].name == s.text)
                        {
                            includes[k].push_back(n);
                            included[n] = true;
                            ++includeCount[n];
                        }
    // before the first pass: the constants of IFUSED tests decided per main source are counted as definitions
    if (targetDialect != "sjasmplus" && targetDialect != "zxasm")
    {
        std::vector<std::string> names;
        for (const ProjectFile& f : files)
            names.push_back(f.name);
        DecideIfUsed(programs, names, included);
    }
    // the main source of the first assembly each file belongs to: its labels are that assembly's layout (a disk of
    // several programs: each one's own values)
    std::vector<size_t> mainOf(programs.size(), programs.size());
    std::vector<std::vector<size_t>> unitOf(programs.size());   // a main source's files: it and what it includes
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
        for (const size_t n : unit)
            if (mainOf[n] == programs.size())
                mainOf[n] = main;
        unitOf[main] = std::move(unit);
    }
    std::map<size_t, std::map<std::string, SourceLabel>> unitValues;
    const auto valuesOf = [&](size_t main) -> const std::map<std::string, SourceLabel>& {
        if (!unitValues.count(main))
        {
            // laid out from the assembly's own files only: each one over the whole disk is quadratic
            std::vector<ProjectFile> own;
            for (const size_t n : unitOf[main])
                own.push_back(files[n]);
            unitValues[main] = own.empty() ? LabelValues(files, main, options, files.size() > 1)
                                           : LabelValues(own, 0, options, files.size() > 1);
        }
        return unitValues[main];
    };
    if (backend->WantsLabelValues() && options.labelValues.empty())
        DecideIfPerMain(programs, unitOf, valuesOf);
    // the options a file is written with: its assembly's label values for a backend that folds with them
    const auto optionsFor = [&](size_t k, BackendOptions written) {
        if (backend->WantsLabelValues() && options.labelValues.empty())
        {
            size_t main = mainOf[k];
            if (main == programs.size())   // only in a cycle of INCLUDEs: the first main source's, as before
                main = static_cast<size_t>(std::find(included.begin(), included.end(), false) - included.begin());
            if (main < programs.size())
            {
                written.labelValues = valuesOf(main);
                // a file of several assemblies: the others' values too (LDLD060 in Info Guide #10 tests tcrc, 1 in
                // mkunr060 and 0 in UNRLT60)
                for (size_t other = 0; other < programs.size(); ++other)
                    if (other != main && !included[other] && std::find(unitOf[other].begin(), unitOf[other].end(), k) != unitOf[other].end())
                        written.otherLabelValues.push_back(valuesOf(other));
            }
        }
        return written;
    };
    // First pass: the macros every file defines (and its definitions, the IFs the layout decides taking one branch);
    // second pass: write with all of them known
    BackendOptions withMacros = options;
    std::vector<std::map<std::string, int>> definitions;
    for (size_t k = 0; k < programs.size(); ++k)
    {
        const ir::Program& program = programs[k];
        BackendResult written = backend->Write(program, optionsFor(k, options));
        withMacros.macroParams.insert(written.macroParams.begin(), written.macroParams.end());
        withMacros.ifUsedNames.insert(written.ifUsedNames.begin(), written.ifUsedNames.end());
        definitions.push_back(written.definitions);
    }
    // Definitions are counted per assembly: a main source (one no file INCLUDEs) with everything it INCLUDEs. A file
    // gets the largest count of the assemblies it belongs to (two programs defining the same name apart do not add up)
    std::vector<std::map<std::string, int>> unitDefinitions(programs.size());
    for (size_t main = 0; main < programs.size(); ++main)
    {
        if (included[main])
            continue;
        const std::vector<size_t>& unit = unitOf[main];
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
        withMacros = optionsFor(k, std::move(withMacros));
        withMacros.definitions = unitDefinitions[k];
        withMacros.fileName = files[k].name;
        withMacros.includedRepeatedly = includeCount[k] > 1;
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
    auto add = [&](size_t k, const ISourceCodec& codec, const std::string& subversion, const std::string& dialect = {}) {
        const containers::TrdosFile& f = files[k];
        DecodeOptions options;
        options.catalog = f.Hints();
        options.subversion = subversion;
        options.dialect = dialect;
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
    // A text file is the text copy of a source of the disk when nine of ten of its code lines (comments and blanks
    // aside) are that source's and it reads in its dialect without a warning: isosurf.a, lorenz1k.a (the CR texts of
    // the ALASM sources isosurf.H, LORENZ1K.H under ZX-ASM 3.10+ catalog entries), megalz d.a (DEC40.H with its
    // description). Other text files are documents
    auto code = [](const std::string& t) {
        std::string out;
        bool quote = false;
        for (const char c : t)
        {
            if (c == '"')
                quote = !quote;
            if (c == ';' && !quote)
                break;
            if (c != ' ' && c != '\t')
                out.push_back(c);
            else if (!out.empty() && out.back() != ' ')
                out.push_back(' ');
        }
        while (!out.empty() && out.back() == ' ')
            out.pop_back();
        return out;
    };
    const size_t sources = project.size();
    const ISourceCodec* text = registry.Find("text");
    for (size_t k = 0; k < files.size() && text; ++k)
    {
        if (std::find(taken.begin(), taken.end(), k) != taken.end())
            continue;
        const DetectResult detected = registry.Detect(files[k].data, files[k].Hints());
        if (!detected.chosen || detected.chosen->Info().family != CodecFamily::Text)
            continue;
        for (size_t n = 0; n < sources; ++n)
        {
            const std::string& dialect = project[n].document.dialect;
            const IFrontend* frontend = DialectRegistry::Builtin().Frontend(dialect);
            if (!frontend)
                continue;
            DecodeOptions options;
            options.catalog = files[k].Hints();
            options.dialect = dialect;
            const SourceDocument copy = text->Decode(files[k].data, options).document;
            std::multiset<std::string> own;
            for (const SourceLine& l : project[n].document.lines)
                own.insert(code(l.text));
            size_t lines = 0, same = 0;
            for (const SourceLine& l : copy.lines)
            {
                const std::string t = code(l.text);
                if (t.empty())
                    continue;
                ++lines;
                if (const auto at = own.find(t); at != own.end())
                {
                    ++same;
                    own.erase(at);
                }
            }
            if (lines == 0 || same * 10 < lines * 9)
                continue;
            const FrontendResult parsed = frontend->Parse(copy);
            const bool clean = std::none_of(parsed.diagnostics.begin(), parsed.diagnostics.end(),
                                            [](const Diagnostic& d) { return d.severity >= Severity::Warning; });
            if (clean)
            {
                add(k, *text, std::string(), dialect);
                break;
            }
        }
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

std::vector<ProjectFile> HostProject(const SourceDocument& main, const DataFileReader& read, Diagnostics& diagnostics,
                                     std::vector<std::string>* foldersOut)
{
    std::vector<ProjectFile> project = {{main.name, main}};
    std::vector<std::string> folders = {""};   // each file's folder, relative to main's
    const CodecRegistry& registry = CodecRegistry::Builtin();
    const ISourceCodec* codec = registry.Find(main.dialect);
    if (!codec || codec->Info().family != CodecFamily::Text)
        codec = registry.Find("text");
    const IFrontend* frontend = DialectRegistry::Builtin().Frontend(main.dialect);
    if (foldersOut)
        *foldersOut = folders;
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
    if (foldersOut)
        *foldersOut = folders;
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

std::map<std::string, std::string> ProjectDiskNames(const std::vector<ProjectFile>& files, const std::vector<std::string>& dataFiles)
{
    std::map<std::string, std::string> names;
    std::set<std::string> taken;   // in lower case: TR-DOS tells the case, a host's file system may not
    const auto give = [&](const std::string& name) {
        if (names.count(name))
            return;
        const size_t slash = name.find_last_of("/\\");
        std::string base = name.substr(slash == std::string::npos ? 0 : slash + 1);
        base = base.substr(0, base.find('.'));
        std::string clean;
        for (const char c : base)
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
                clean.push_back(c);
        clean = clean.empty() ? std::string("FILE") : clean.substr(0, 8);
        // made unique with a number (letters and digits only: some targets' fonts lack ~)
        std::string chosen = clean;
        for (int n = 2; taken.count(LowerText(chosen)); ++n)
            chosen = clean.substr(0, 8 - std::to_string(n).size()) + std::to_string(n);
        taken.insert(LowerText(chosen));
        names[name] = chosen;
    };
    for (const ProjectFile& f : files)
        give(f.name);
    for (const std::string& d : dataFiles)
        give(d);
    return names;
}

uint16_t CatalogStart(std::string_view codec, const std::string& version, char type)
{
    if (codec == "tasm")
        return version == "2.0" ? 38750 : version == "3" ? 39221 : version == "4.0" ? 40872 : 0;
    if (codec == "storm")
        return version == "1.0" ? 0xC003 : 0xC00B;
    if (codec == "zxasm")
    {
        if (type == 'a')
            return 0x6D73;   // extension "sm"
        if (type == 'z')
            return 0x7361;   // extension "as"
        return version == "2" ? 0xA1DF : 35151;
    }
    if (codec == "xas")
        return 0x5341;   // "AS": XAS lists only files that carry it (research-xas.md)
    return 0;
}

containers::TrdosFile CatalogFile(const std::string& name, char type, std::string_view codec, const std::string& version,
                                  const std::vector<uint8_t>& bytes)
{
    containers::TrdosFile file;
    file.name = name.substr(0, 8);
    file.name.resize(8, ' ');
    file.type = type;
    file.start = CatalogStart(codec, version, type);
    file.length = codec == "xas" ? 0 : static_cast<uint16_t>(bytes.size());
    file.data = bytes;
    return file;
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
