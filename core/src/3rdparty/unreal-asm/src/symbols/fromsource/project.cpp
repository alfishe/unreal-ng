#include "unrealasm/symbols/fromsource.h"

#include <string_view>

#include "unrealasm/registry.h"

namespace unrealasm::symbols
{
namespace
{
/// ALASM's wildcards in an INCBIN name: * any run of characters, ? one (case-insensitive: host names)
bool Matches(std::string_view pattern, std::string_view name)
{
    auto lower = [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); };
    if (pattern.empty())
        return name.empty();
    if (pattern[0] == '*')
    {
        for (size_t k = 0; k <= name.size(); ++k)
            if (Matches(pattern.substr(1), name.substr(k)))
                return true;
        return false;
    }
    return !name.empty() && (pattern[0] == '?' || lower(pattern[0]) == lower(name[0])) && Matches(pattern.substr(1), name.substr(1));
}

std::string Lower(std::string text)
{
    for (char& c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return text;
}
}  // namespace

std::optional<uint64_t> SourceProject::Size(const std::string& wanted) const
{
    std::optional<uint64_t> found;
    for (const auto& [name, size] : sizes)
        if (Matches(wanted, name))
            found = size;
    return found;
}

SourceProject ProjectFromFiles(const std::vector<containers::TrdosFile>& files)
{
    SourceProject project;
    for (const containers::TrdosFile& f : files)
    {
        const std::string name = f.TrimmedName() + (f.type == 'C' ? std::string() : std::string(".") + f.type);
        project.sizes.emplace_back(name, f.data.size());
        project.sizes.emplace_back(f.TrimmedName() + "." + std::string(1, f.type), f.data.size());
        project.sizes.emplace_back(name + ".slack", f.tail.size());
        // A three-letter extension: the type and the two bytes of the start address ("Font4_3.fn1")
        const char second = static_cast<char>(f.start & 0xFF), third = static_cast<char>(f.start >> 8);
        if (second > ' ' && second < 0x7F && third > ' ' && third < 0x7F)
            project.sizes.emplace_back(f.TrimmedName() + "." + std::string{f.type, second, third}, f.data.size());
    }
    // The sources, and the files they INCLUDE that detection could not tell (read like their includer)
    project.sources = ImageProject(files);
    return project;
}

SourceProject ProjectFromText(const std::string& path, const std::vector<uint8_t>& bytes)
{
    SourceProject project;
    const size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    if (name.size() > 4 && Lower(name.substr(name.size() - 4)) == ".asm")
        name.resize(name.size() - 4);
    const CodecRegistry& registry = CodecRegistry::Builtin();
    const DetectResult detected = registry.Detect(bytes, {});
    const ISourceCodec* codec = detected.chosen;
    if (!codec || codec->Info().family != CodecFamily::Tokenized)
        codec = registry.Find("sjasmplus");
    if (codec)
        project.sources.push_back({name, codec->Decode(bytes, {}).document});
    return project;
}

size_t FindMainSource(const SourceProject& project, const std::string& main, std::string& error)
{
    if (project.sources.empty())
    {
        error = "no assembler source";
        return 0;
    }
    if (!main.empty())
    {
        for (size_t k = 0; k < project.sources.size(); ++k)
            if (project.sources[k].name == main)
                return k;
        error = "no source " + main;
        return project.sources.size();
    }
    if (project.sources.size() == 1)
        return 0;
    error = "several sources, pick one as main:";
    for (const ProjectFile& f : project.sources)
        error += " " + f.name;
    return project.sources.size();
}

SourceSymbolsResult SymbolsFromSourceProject(const SourceProject& project, size_t main, SourceSymbolsOptions options)
{
    options.layout.fileSize = [&project](const std::string& name) { return project.Size(name); };
    return SymbolsFromProject(project.sources, main, options);
}
}  // namespace unrealasm::symbols
