// symconv - symbol files from any format to any format (unreal-asm symbol module, symbols/tdd.md §10, phase S4).
//
//   symconv formats                                  list the symbol codecs (id, family, what they hold, extensions)
//   symconv detect  <file>                           which codec reads it (scores, the choice or why none)
//   symconv <in> --to id [-o out] [--from id] [--pages fold|comment|drop]
//                                                    read <in> (format detected unless --from) and write it as `id`;
//                                                    renames by the target's name rules, page symbols by --pages
//                                                    (default fold); the report goes to stderr
//
// Without -o the result is written to stdout. Exit code 0 on success, 1 on errors, 2 on bad usage.

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "unrealasm/symbols/codec.h"

using namespace unrealasm;
using namespace unrealasm::symbols;

namespace
{
int Usage()
{
    std::cerr << "usage: symconv formats | detect <file> |\n"
                 "       <in> --to id [-o out] [--from id] [--pages fold|comment|drop]\n";
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
            const CodecInfo& info = codec->Info();
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
    std::string input;
    std::string to;
    std::string from;
    std::string output;
    SymbolEncodeOptions options;
    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::string& a = args[i];
        const bool hasValue = i + 1 < args.size();
        if (a == "--to" && hasValue)
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
    const SymbolEncodeResult encoded = target->Encode(decoded.file, options);
    Print(encoded.diagnostics);
    std::cerr << source->Info().id << " -> " << target->Info().id << ": " << count << " read, " << encoded.written << " written\n";
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
