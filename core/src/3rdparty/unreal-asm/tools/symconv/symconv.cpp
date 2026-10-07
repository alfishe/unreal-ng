// symconv - symbol files from any format to any format (unreal-asm symbol module, symbols/tdd.md §10, phase S4).
//
//   symconv formats                                  list the symbol codecs (id, family, what they hold, extensions)
//   symconv detect  <file>                           which codec reads it (scores, the choice or why none)
//   symconv <in> --to id [-o out] [--from id] [--pages fold|comment|drop]
//                                                    read <in> (format detected unless --from) and write it as `id`;
//                                                    renames by the target's name rules, page symbols by --pages
//                                                    (default fold); the report goes to stderr
//   symconv source <in> --to id [-o out] [--main NAME] [--generated] [--sjasmplus-names] [--pages ...]
//                                                    the labels a source defines, with their values (symbols/fromsource.h):
//                                                    <in> is a TR-DOS image (the project: its sources, and its other files
//                                                    for INCBIN; --main picks the source to assemble), a hobeta file or a
//                                                    text source; --sjasmplus-names gives every label under the name its
//                                                    sjasmplus conversion writes (to compare with sjasmplus --sym)
//   symconv live <page:file | dump>... --to id [-o out] [--pick N] [--pages ...]
//                                                    the label tables of assemblers in RAM (symbols/live.h): a file of
//                                                    16 KB pages, `3:ram3.bin` from page 3, a bare file from page 0
//                                                    (a 128K dump: pages 0-7); lists the tables found on stderr and
//                                                    writes the best (or the N-th) as `id`
//
// Without -o the result is written to stdout. Exit code 0 on success, 1 on errors, 2 on bad usage.

#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "unrealasm/containers.h"
#include "unrealasm/registry.h"
#include "unrealasm/symbols/codec.h"
#include "unrealasm/symbols/fromsource.h"
#include "unrealasm/symbols/live.h"

using namespace unrealasm;
using namespace unrealasm::symbols;

namespace
{
int Usage()
{
    std::cerr << "usage: symconv formats | detect <file> |\n"
                 "       <in> --to id [-o out] [--from id] [--pages fold|comment|drop] |\n"
                 "       source <in> --to id [-o out] [--main NAME] [--generated] [--sjasmplus-names] [--pages ...] |\n"
                 "       live <page:file | dump>... --to id [-o out] [--pick N] [--pages ...]\n";
    return 2;
}

bool ReadFile(const std::string& path, std::vector<uint8_t>& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

std::string Extension(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return {};
    std::string ext = path.substr(dot + 1);
    for (char& c : ext)
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return ext;
}

void Print(const Diagnostics& diagnostics)
{
    for (const Diagnostic& d : diagnostics)
    {
        const char* level = d.severity == Severity::Error ? "error" : d.severity == Severity::Warning ? "warning" : "note";
        std::cerr << level;
        if (d.line)
            std::cerr << " (line " << d.line << ")";
        std::cerr << ": " << d.message << "\n";
    }
}

/// ALASM's wildcards in an INCBIN name: * any run of characters, ? one (case-insensitive here: host names)
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

/// A source project read from a file: an image's sources (as `zxasm convert` takes them) and the sizes of all its files
/// under the names INCBIN gives them (NAME for type C, NAME.T, NAME.slack for the rest of the last sector)
struct Project
{
    std::vector<ProjectFile> sources;
    std::vector<std::pair<std::string, uint64_t>> sizes;

    std::optional<uint64_t> Size(const std::string& wanted) const
    {
        std::optional<uint64_t> found;
        for (const auto& [name, size] : sizes)
            if (Matches(wanted, name))
                found = size;   // the last match, as ALASM takes it
        return found;
    }
};

void AddSource(Project& project, std::string name, const std::vector<uint8_t>& bytes, const CatalogHints& hints, bool text)
{
    const CodecRegistry& registry = CodecRegistry::Builtin();
    const DetectResult detected = registry.Detect(bytes, hints);
    const ISourceCodec* codec = detected.chosen;
    if (text && (!codec || codec->Info().family != CodecFamily::Tokenized))
        codec = registry.Find("sjasmplus");   // a text source: sjasmplus' dialect
    if (!codec || (!text && codec->Info().family != CodecFamily::Tokenized))
        return;
    int copies = 1;
    for (const ProjectFile& earlier : project.sources)
        if (earlier.name == name || earlier.name.rfind(name + "~", 0) == 0)
            ++copies;
    if (copies > 1)
        name += "~" + std::to_string(copies);   // a name saved again: TR-DOS finds the first
    DecodeOptions options;
    options.catalog = hints;
    project.sources.push_back({name, codec->Decode(bytes, options).document});
}

bool ReadProject(const std::string& path, const std::vector<uint8_t>& bytes, Project& project)
{
    std::vector<containers::TrdosFile> files;
    std::string error;
    const std::string extension = Extension(path);
    const bool image = extension == "trd" && containers::ReadTrd(bytes, files, error);
    containers::TrdosFile one;
    if (!image && extension.size() == 2 && extension[0] == '$' && containers::ReadHobeta(bytes, one, error))
        files.push_back(one);
    if (files.empty())
    {
        // A text source: named as INCLUDE would name it (no folder, no .asm)
        const size_t slash = path.find_last_of("/\\");
        std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        if (name.size() > 4 && Extension(name) == "asm")
            name.resize(name.size() - 4);
        AddSource(project, name, bytes, {}, true);
        return !project.sources.empty();
    }
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
        AddSource(project, f.TrimmedName(), f.data, f.Hints(), false);
    }
    return !project.sources.empty();
}

int Write(const ISymbolCodec& target, const SymbolFile& file, const SymbolEncodeOptions& options, const std::string& from, size_t count,
          const std::string& output)
{
    const SymbolEncodeResult encoded = target.Encode(file, options);
    Print(encoded.diagnostics);
    std::cerr << from << " -> " << target.Info().id << ": " << count << " read, " << encoded.written << " written\n";
    if (!encoded.ok)
        return 1;
    if (output.empty())
    {
        std::cout.write(reinterpret_cast<const char*>(encoded.bytes.data()), static_cast<std::streamsize>(encoded.bytes.size()));
        return 0;
    }
    std::ofstream out(output, std::ios::binary);
    out.write(reinterpret_cast<const char*>(encoded.bytes.data()), static_cast<std::streamsize>(encoded.bytes.size()));
    if (!out)
    {
        std::cerr << "symconv: cannot write " << output << "\n";
        return 1;
    }
    return 0;
}

int Live(const std::vector<std::string>& args)
{
    std::string to, output;
    size_t pick = 0;
    SymbolEncodeOptions options;
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> files;   // first page, bytes
    for (size_t i = 1; i < args.size(); ++i)
    {
        const std::string& a = args[i];
        const bool hasValue = i + 1 < args.size();
        if (a == "--to" && hasValue)
            to = args[++i];
        else if (a == "-o" && hasValue)
            output = args[++i];
        else if (a == "--pick" && hasValue)
            pick = static_cast<size_t>(std::stoul(args[++i]));
        else if (a == "--pages" && hasValue)
        {
            if (!ParseUnrepresentable(args[++i], options.unrepresentable))
                return Usage();
        }
        else if (!a.empty() && a[0] != '-')
        {
            // page:file (digits before the colon), else a file from page 0
            uint16_t first = 0;
            std::string path = a;
            const size_t colon = a.find(':');
            if (colon != std::string::npos && colon > 0 && a.find_first_not_of("0123456789") == colon)
            {
                first = static_cast<uint16_t>(std::stoul(a.substr(0, colon)));
                path = a.substr(colon + 1);
            }
            std::vector<uint8_t> bytes;
            if (!ReadFile(path, bytes) || bytes.size() < 0x4000)
            {
                std::cerr << "symconv: cannot read 16 KB pages from " << path << "\n";
                return 1;
            }
            files.emplace_back(first, std::move(bytes));
        }
        else
            return Usage();
    }
    MemoryView memory;
    for (const auto& [first, bytes] : files)
        for (size_t at = 0; at + 0x4000 <= bytes.size(); at += 0x4000)
            memory.pages.push_back({static_cast<uint16_t>(first + at / 0x4000), std::span<const uint8_t>(bytes.data() + at, 0x4000)});
    if (to.empty() || memory.pages.empty())
        return Usage();
    const ISymbolCodec* target = SymbolCodecRegistry::Builtin().Find(to);
    if (!target)
    {
        std::cerr << "symconv: unknown format " << to << " (symconv formats lists them)\n";
        return 2;
    }
    const std::vector<LiveCandidate> found = FindLabelTables(memory);
    for (size_t k = 0; k < found.size(); ++k)
        std::cerr << k << "\t" << found[k].scanner << " " << found[k].version << "\tram" << found[k].page << "\t#" << std::hex << std::uppercase
                  << found[k].offset << std::dec << "\t" << found[k].count << " entries\tscore " << found[k].score << "\n";
    if (pick >= found.size())
    {
        std::cerr << "symconv: no label table" << (found.empty() ? "" : " with that number") << "\n";
        return 1;
    }
    const LiveReadResult r = ReadLabelTable(memory, found[pick]);
    Print(r.diagnostics);
    if (!r.ok)
        return 1;
    SymbolFile file;
    file.sets.push_back(r.set);
    return Write(*target, file, options, found[pick].scanner, r.set.symbols.size(), output);
}

const char* FamilyName(Family f)
{
    switch (f)
    {
        case Family::Text: return "text";
        case Family::Script: return "script";
        case Family::Native: return "native";
    }
    return "?";
}
}  // namespace

int main(int argc, char** argv)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty())
        return Usage();
    const SymbolCodecRegistry& registry = SymbolCodecRegistry::Builtin();
    if (args[0] == "formats")
    {
        for (const auto& codec : registry.All())
        {
            const symbols::CodecInfo& info = codec->Info();
            std::cout << info.id << "\t" << FamilyName(info.family) << "\t" << (info.pages ? "pages" : "no pages") << "\t";
            for (size_t i = 0; i < info.extensions.size(); ++i)
                std::cout << (i ? "," : "") << info.extensions[i];
            std::cout << "\t" << info.title << "\n";
        }
        return 0;
    }
    if (args[0] == "detect")
    {
        if (args.size() != 2)
            return Usage();
        std::vector<uint8_t> bytes;
        if (!ReadFile(args[1], bytes))
        {
            std::cerr << "symconv: cannot read " << args[1] << "\n";
            return 1;
        }
        const SymbolDetectResult d = registry.Detect(bytes, Extension(args[1]));
        for (const SymbolDetectCandidate& c : d.candidates)
            std::cout << c.codec->Info().id << "\t" << c.score << "\n";
        std::cout << (d.chosen ? "chosen: " + d.chosen->Info().id : d.reason) << "\n";
        return d.chosen ? 0 : 1;
    }
    if (args[0] == "live")
        return Live(args);
    std::string input;
    std::string to;
    std::string from;
    std::string output;
    std::string main;
    SymbolEncodeOptions options;
    SourceSymbolsOptions sourceOptions;
    const bool fromSource = args[0] == "source";
    for (size_t i = fromSource ? 1 : 0; i < args.size(); ++i)
    {
        const std::string& a = args[i];
        const bool hasValue = i + 1 < args.size();
        if (fromSource && a == "--main" && hasValue)
            main = args[++i];
        else if (fromSource && a == "--generated")
            sourceOptions.generated = true;
        else if (fromSource && a == "--sjasmplus-names")
            sourceOptions.writtenNames = true;
        else if (a == "--to" && hasValue)
            to = args[++i];
        else if (a == "--from" && hasValue)
            from = args[++i];
        else if (a == "-o" && hasValue)
            output = args[++i];
        else if (a == "--pages" && hasValue)
        {
            if (!ParseUnrepresentable(args[++i], options.unrepresentable))
                return Usage();
        }
        else if (!a.empty() && a[0] != '-' && input.empty())
            input = a;
        else
            return Usage();
    }
    if (input.empty() || to.empty())
        return Usage();
    const ISymbolCodec* target = registry.Find(to);
    if (!target)
    {
        std::cerr << "symconv: unknown format " << to << " (symconv formats lists them)\n";
        return 2;
    }
    std::vector<uint8_t> bytes;
    if (!ReadFile(input, bytes))
    {
        std::cerr << "symconv: cannot read " << input << "\n";
        return 1;
    }
    if (fromSource)
    {
        Project project;
        if (!ReadProject(input, bytes, project))
        {
            std::cerr << "symconv: no assembler source in " << input << "\n";
            return 1;
        }
        size_t at = 0;
        if (!main.empty())
        {
            at = project.sources.size();
            for (size_t k = 0; k < project.sources.size(); ++k)
                if (project.sources[k].name == main)
                    at = k;
            if (at == project.sources.size())
            {
                std::cerr << "symconv: no source " << main << " in " << input << "\n";
                return 1;
            }
        }
        else if (project.sources.size() > 1)
        {
            std::cerr << "symconv: " << input << " holds several sources, pick one with --main:";
            for (const ProjectFile& f : project.sources)
                std::cerr << " " << f.name;
            std::cerr << "\n";
            return 2;
        }
        sourceOptions.layout.fileSize = [&project](const std::string& name) { return project.Size(name); };
        const SourceSymbolsResult r = SymbolsFromProject(project.sources, at, sourceOptions);
        Print(r.diagnostics);
        SymbolFile file;
        file.sets.push_back(r.set);
        const int written = Write(*target, file, options, r.set.symbols.empty() ? std::string("source") : r.set.symbols[0].provenance.importer,
                                  r.set.symbols.size(), output);
        return written ? written : (r.ok ? 0 : 1);
    }
    const ISymbolCodec* source = from.empty() ? nullptr : registry.Find(from);
    if (!from.empty() && !source)
    {
        std::cerr << "symconv: unknown format " << from << "\n";
        return 2;
    }
    if (!source)
    {
        const SymbolDetectResult d = registry.Detect(bytes, Extension(input));
        if (!d.chosen)
        {
            std::cerr << "symconv: " << d.reason << " (use --from)\n";
            return 1;
        }
        source = d.chosen;
    }
    const SymbolDecodeResult decoded = source->Decode(bytes);
    Print(decoded.diagnostics);
    if (!decoded.ok)
        return 1;
    size_t count = 0;
    for (const SymbolSet& set : decoded.file.sets)
        count += set.symbols.size();
    return Write(*target, decoded.file, options, source->Info().id, count, output);
}
