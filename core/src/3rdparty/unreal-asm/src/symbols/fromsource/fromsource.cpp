#include "unrealasm/symbols/fromsource.h"

#include <map>
#include <set>

namespace unrealasm::symbols
{
namespace
{
SymbolKind KindOf(layout::LabelUse use)
{
    switch (use)
    {
        case layout::LabelUse::Code: return SymbolKind::Code;
        case layout::LabelUse::Data: return SymbolKind::Data;
        case layout::LabelUse::Equ:
        case layout::LabelUse::Defl: return SymbolKind::Const;
        case layout::LabelUse::Unknown: break;
    }
    return SymbolKind::Unknown;
}

/// The value where it is: a page ORG named for an address in the paged window (#C000-#FFFF), else the CPU view; a
/// value beyond 16 bits (a negative one too) is a constant
void Place(Symbol& s, const layout::Label& l)
{
    const uint32_t value = static_cast<uint32_t>(static_cast<uint64_t>(l.value));
    if (l.page >= 0 && value >= 0xC000 && value <= 0xFFFF)
    {
        s.location.space.kind = SpaceKind::Ram;
        s.location.space.page = static_cast<uint16_t>(l.page);
        s.location.offset = value & 0x3FFF;
        s.window = 3;
        return;
    }
    s.location.offset = value;
    if (value > 0xFFFF)
        s.location.space.kind = SpaceKind::Constant;
}

std::string LineText(const ProjectFile& file, uint32_t line)
{
    return line >= 1 && line <= file.document.lines.size() ? file.document.lines[line - 1].text : std::string();
}

/// The line as the source numbers it: the number a numbered format stores (GENS, ZEUS), else its position
uint32_t SourceLineNumber(const ProjectFile& file, uint32_t line)
{
    if (line >= 1 && line <= file.document.lines.size() && file.document.lines[line - 1].number >= 0)
        return static_cast<uint32_t>(file.document.lines[line - 1].number);
    return line;
}

/// A layout diagnostic ("file:line: message", the line of the converted text) on the source line it was written from
void PointToSource(Diagnostic& d, const std::vector<ProjectFile>& files, const std::vector<ProjectFile>& converted)
{
    for (size_t k = 0; k < converted.size() && k < files.size(); ++k)
    {
        const std::string prefix = converted[k].name + ":" + std::to_string(d.line) + ": ";
        if (d.message.rfind(prefix, 0) != 0 || d.line < 1 || d.line > converted[k].document.lines.size())
            continue;
        const uint32_t origin = converted[k].document.lines[d.line - 1].origin;
        if (origin == 0)
            return;
        d.line = SourceLineNumber(files[k], origin);
        d.message = files[k].name + ":" + std::to_string(d.line) + ": " + d.message.substr(prefix.size());
        return;
    }
}

void Keep(Diagnostics& into, const Diagnostics& from, Severity least)
{
    for (const Diagnostic& d : from)
        if (d.severity >= least)
            into.push_back(d);
}
}  // namespace

SourceSymbolsResult SymbolsFromProject(const std::vector<ProjectFile>& files, size_t main, const SourceSymbolsOptions& options)
{
    SourceSymbolsResult result;
    if (main >= files.size())
    {
        result.diagnostics.push_back({Severity::Error, 0, 0, "no main file"});
        return result;
    }
    const std::string dialect = files[main].document.dialect;
    const std::string importer = "source-" + (dialect.empty() ? std::string("sjasmplus") : dialect);
    result.set.id = files[main].name;
    result.set.title = files[main].name + " (" + importer + ")";
    result.set.origin = {"file", files[main].name, {}};

    const bool converted = !dialect.empty() && dialect != "sjasmplus";
    std::vector<ProjectFile> laidOut;
    std::vector<std::vector<LabelName>> written;
    Diagnostics conversion;
    if (converted)
    {
        ProjectResult project = ConvertProject(files, "sjasmplus");
        conversion = std::move(project.diagnostics);
        laidOut = std::move(project.files);
        written = std::move(project.labels);
    }
    else
        laidOut = files;
    layout::LayoutResult laid = layout::Layout(laidOut, main, options.layout);
    if (converted)
        for (Diagnostic& d : laid.diagnostics)
            PointToSource(d, files, laidOut);
    Keep(result.diagnostics, laid.diagnostics, Severity::Info);

    std::map<std::string, const layout::Label*> byName;
    std::set<std::string> reached = {files[main].name};
    for (const layout::Label& l : laid.labels)
    {
        byName[l.name] = &l;
        reached.insert(l.file);
    }
    // The conversion's warnings of the files this assembly takes (ConvertProject prefixes each with its file name)
    Diagnostics kept;
    for (const Diagnostic& d : conversion)
    {
        const size_t colon = d.message.find(": ");
        if (d.severity >= Severity::Warning && colon != std::string::npos && reached.count(d.message.substr(0, colon)))
            kept.push_back(d);
    }
    result.diagnostics.insert(result.diagnostics.begin(), kept.begin(), kept.end());
    auto make = [&](const layout::Label& l) {
        Symbol s;
        s.name = l.name;
        s.kind = l.parent.empty() ? KindOf(l.use) : SymbolKind::Local;
        s.parent = l.parent;
        s.module = l.module;
        Place(s, l);
        s.source.file = l.file;
        s.source.line = l.line;
        s.provenance.importer = importer;
        return s;
    };

    const bool complete = laid.ok;
    if (!converted || options.writtenNames)
    {
        for (const layout::Label& l : laid.labels)
        {
            Symbol s = make(l);
            for (const ProjectFile& f : converted ? std::vector<ProjectFile>{} : files)
                if (f.name == l.file)
                    s.provenance.raw = LineText(f, l.line);
            result.set.symbols.push_back(std::move(s));
        }
    }
    else
    {
        std::set<std::string> given;
        for (size_t k = 0; k < files.size() && k < written.size(); ++k)
            for (const LabelName& n : written[k])
            {
                if (n.inMacro || n.written.empty() || n.written.find_first_not_of("0123456789") == std::string::npos)
                    continue;   // macro body labels have one value per expansion; temporary labels none
                if (n.source.rfind("__UNREALASM_", 0) == 0)
                    continue;   // a value a frontend made for the conversion
                const auto found = byName.find(n.written);
                if (found == byName.end())
                {
                    // In a block an IF left out (the layout reports what it could not lay out)
                    if (reached.count(files[k].name))
                        result.diagnostics.push_back({Severity::Info, n.line, 0, files[k].name + ":" + std::to_string(n.line) + ": label " + n.source + " is not assembled"});
                    continue;
                }
                if (!given.insert(n.written).second)
                    continue;   // a file INCLUDEd twice defines its labels once
                Symbol s = make(*found->second);
                s.name = n.source;
                if (n.local)
                    s.kind = SymbolKind::Local;
                s.source.file = files[k].name;
                s.source.line = SourceLineNumber(files[k], n.line);
                s.provenance.raw = LineText(files[k], n.line);
                if (n.written != n.source)
                    s.provenance.type = "written as " + n.written;
                result.set.symbols.push_back(std::move(s));
            }
        if (options.generated)
            for (const layout::Label& l : laid.labels)
                if (!given.count(l.name))
                {
                    Symbol s = make(l);
                    s.provenance.type = "generated";
                    result.set.symbols.push_back(std::move(s));
                }
    }
    result.ok = complete;
    return result;
}

SourceSymbolsResult SymbolsFromSource(const SourceDocument& source, const SourceSymbolsOptions& options)
{
    return SymbolsFromProject({{source.name.empty() ? std::string("source") : source.name, source}}, 0, options);
}
}  // namespace unrealasm::symbols
