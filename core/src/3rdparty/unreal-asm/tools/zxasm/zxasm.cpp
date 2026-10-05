// zxasm - the command-line tool of unreal-asm (decision D-12).
//
//   zxasm formats                                   list the codecs
//   zxasm detect   <file>                           which codec reads it (scores, the choice or why none)
//   zxasm encoding <file>                           code page ranking, line ends, text score
//   zxasm decode   <file> [-o out] [--codec id] [--codepage cp]
//   zxasm encode   <file> --codec id [-o out] [--codepage cp] [--line-end lf|crlf|cr]
//   zxasm files    <image.trd>                      list the files of a TR-DOS image
//
// Containers: a hobeta file (NAME.$A, ...) is unwrapped and its catalog fields used for detection; a file inside a
// TR-DOS image is picked with --file NAME (or NAME.T for the type letter T). An encode output named *.$X is written
// as a hobeta file of type X.
//
// Decoded text is written as UTF-8 (decision D-11). Exit code 0 on success, 1 on errors, 2 on bad usage.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "unrealasm/unrealasm.h"

using namespace unrealasm;

namespace
{
int Usage()
{
    std::cerr << "usage: zxasm formats | detect <file> | encoding <file> |\n"
                 "       decode <file> [-o out] [--codec id] [--codepage cp] |\n"
                 "       encode <file> --codec id [-o out] [--codepage cp] [--line-end lf|crlf|cr] |\n"
                 "       files <image.trd>      (decode/detect/encoding take --file NAME[.T] for a file in an image)\n";
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

bool WriteFile(const std::string& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

std::string Extension(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    return (dot == std::string::npos || (slash != std::string::npos && dot < slash)) ? std::string() : path.substr(dot + 1);
}

void PrintDiagnostics(const Diagnostics& diagnostics)
{
    for (const Diagnostic& d : diagnostics)
        std::cerr << (d.severity == Severity::Error ? "error" : d.severity == Severity::Warning ? "warning" : "info")
                  << (d.line ? " line " + std::to_string(d.line) : std::string()) << ": " << d.message << "\n";
}

struct Args
{
    std::string command, file, output, codec, codePage, lineEnd, inner;
};

bool Parse(int argc, char** argv, Args& args)
{
    if (argc < 2)
        return false;
    args.command = argv[1];
    for (int i = 2; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto value = [&](std::string& target) {
            if (i + 1 >= argc)
                return false;
            target = argv[++i];
            return true;
        };
        if (a == "-o" ? !value(args.output) : a == "--codec" ? !value(args.codec)
                                          : a == "--codepage" ? !value(args.codePage)
                                          : a == "--line-end" ? !value(args.lineEnd)
                                          : a == "--file" ? !value(args.inner) : false)
            return false;
        if (a != "-o" && a != "--codec" && a != "--codepage" && a != "--line-end" && a != "--file")
        {
            if (!args.file.empty())
                return false;
            args.file = a;
        }
    }
    return true;
}

std::string Lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/// Hobeta / TR-DOS image -> the file's bytes and catalog hints; false with a message when the container is broken or
/// the named file is missing. Plain host files pass through.
bool Unwrap(const Args& args, std::vector<uint8_t>& bytes, CatalogHints& hints)
{
    const std::string extension = Lower(Extension(args.file));
    containers::TrdosFile file;
    std::string error;
    if (extension == "trd")
    {
        std::vector<containers::TrdosFile> files;
        if (!containers::ReadTrd(bytes, files, error))
        {
            std::cerr << "zxasm: " << error << "\n";
            return false;
        }
        if (args.inner.empty())
            return true;   // the image itself (detect reports none; "files" lists it)
        const size_t dot = args.inner.find_last_of('.');
        const std::string name = dot == std::string::npos ? args.inner : args.inner.substr(0, dot);
        const char type = dot == std::string::npos ? 0 : args.inner[dot + 1];
        for (const auto& f : files)
            if (f.TrimmedName() == name && (type == 0 || f.type == type))
            {
                bytes = f.data;
                hints = f.Hints();
                return true;
            }
        std::cerr << "zxasm: no file " << args.inner << " in " << args.file << "\n";
        return false;
    }
    if (!extension.empty() && extension[0] == '$')
    {
        if (!containers::ReadHobeta(bytes, file, error))
        {
            std::cerr << "zxasm: " << error << "\n";
            return false;
        }
        bytes = file.data;
        hints = file.Hints();
    }
    return true;
}
}  // namespace

int main(int argc, char** argv)
{
    Args args;
    if (!Parse(argc, argv, args))
        return Usage();
    const CodecRegistry& registry = CodecRegistry::Builtin();

    if (args.command == "formats")
    {
        for (const auto& codec : registry.All())
            std::cout << codec->Info().id << "\t" << codec->Info().title << "\n";
        return 0;
    }
    if (args.file.empty())
        return Usage();
    std::vector<uint8_t> bytes;
    if (!ReadFile(args.file, bytes))
    {
        std::cerr << "zxasm: cannot read " << args.file << "\n";
        return 1;
    }
    CatalogHints hints;
    hints.extension = Extension(args.file);
    if (args.command == "files")
    {
        std::vector<containers::TrdosFile> files;
        std::string error;
        if (!containers::ReadTrd(bytes, files, error))
        {
            std::cerr << "zxasm: " << error << "\n";
            return 1;
        }
        for (const auto& f : files)
        {
            const DetectResult detected = registry.Detect(f.data, f.Hints());
            std::cout << f.TrimmedName() << "." << f.type << "\t" << f.start << "\t" << f.length << "\t"
                      << (detected.chosen ? detected.chosen->Info().id : "-") << "\n";
        }
        return 0;
    }
    if (args.command != "encode" && !Unwrap(args, bytes, hints))
        return 1;

    if (args.command == "detect")
    {
        const DetectResult detected = registry.Detect(bytes, hints);
        for (const DetectCandidate& c : detected.candidates)
            std::cout << c.codec->Info().id << "\t" << c.score << "\n";
        if (detected.chosen)
            std::cout << "chosen: " << detected.chosen->Info().id << "\n";
        else
            std::cout << "none: " << detected.reason << "\n";
        return detected.chosen ? 0 : 1;
    }
    if (args.command == "encoding")
    {
        for (const auto& guess : encoding::CodePageDetector().Rank(bytes))
            std::cout << encoding::CodePageName(guess.codePage) << "\t" << guess.confidence << "\n";
        std::cout << "line end: " << encoding::LineEndName(encoding::LineEndDetector().Detect(bytes)) << "\n";
        std::cout << "text score: " << encoding::TextBinaryDetector().TextScore(bytes) << "\n";
        return 0;
    }

    std::optional<encoding::CodePage> codePage;
    if (!args.codePage.empty())
    {
        encoding::CodePage parsed;
        if (!encoding::ParseCodePage(args.codePage, parsed))
        {
            std::cerr << "zxasm: unknown code page " << args.codePage << "\n";
            return 2;
        }
        codePage = parsed;
    }

    if (args.command == "decode")
    {
        const ISourceCodec* codec = nullptr;
        if (!args.codec.empty())
            codec = registry.Find(args.codec);
        else
        {
            const DetectResult detected = registry.Detect(bytes, hints);
            codec = detected.chosen;
            if (!codec)
            {
                std::cerr << "zxasm: " << detected.reason << " (use --codec)\n";
                return 1;
            }
        }
        if (!codec)
        {
            std::cerr << "zxasm: unknown codec " << args.codec << "\n";
            return 2;
        }
        DecodeOptions options;
        options.codePage = codePage;
        const DecodeResult decoded = codec->Decode(bytes, options);
        PrintDiagnostics(decoded.diagnostics);
        std::string text = decoded.document.Text();
        if (!decoded.document.lines.empty())
            text.push_back('\n');
        const std::vector<uint8_t> out(text.begin(), text.end());
        if (args.output.empty())
            std::cout << text;
        else if (!WriteFile(args.output, out))
        {
            std::cerr << "zxasm: cannot write " << args.output << "\n";
            return 1;
        }
        std::cerr << codec->Info().id << ": " << decoded.document.lines.size() << " line(s), "
                  << encoding::CodePageName(decoded.document.codePage) << "\n";
        return decoded.ok ? 0 : 1;
    }
    if (args.command == "encode")
    {
        const ISourceCodec* codec = registry.Find(args.codec);
        if (!codec)
        {
            std::cerr << "zxasm: encode needs --codec (zxasm formats lists them)\n";
            return 2;
        }
        const std::string text(bytes.begin(), bytes.end());
        SourceDocument document = SourceDocument::FromText(text, codec->Info().dialect);
        for (SourceLine& line : document.lines)
            if (!line.text.empty() && line.text.back() == '\r')
                line.text.pop_back();
        EncodeOptions options;
        options.codePage = codePage;
        if (!args.lineEnd.empty())
            options.lineEnd = args.lineEnd == "crlf" ? encoding::LineEnd::CrLf : args.lineEnd == "cr" ? encoding::LineEnd::Cr : encoding::LineEnd::Lf;
        EncodeResult encoded = codec->Encode(document, options);
        PrintDiagnostics(encoded.diagnostics);
        const std::string outExtension = Extension(args.output);
        if (outExtension.size() == 2 && outExtension[0] == '$')
        {
            // Hobeta output: name from the output file, type from the extension; TASM 3 sources carry its start
            containers::TrdosFile file;
            const size_t slash = args.output.find_last_of("/\\");
            file.name = args.output.substr(slash == std::string::npos ? 0 : slash + 1);
            file.name = file.name.substr(0, std::min<size_t>(file.name.size() - 3, 8));
            file.type = outExtension[1];
            file.start = codec->Info().id == "tasm3" ? 40872 : 0;
            file.length = static_cast<uint16_t>(encoded.bytes.size());
            file.data = encoded.bytes;
            encoded.bytes = containers::WriteHobeta(file);
        }
        if (args.output.empty())
            std::cout.write(reinterpret_cast<const char*>(encoded.bytes.data()), static_cast<std::streamsize>(encoded.bytes.size()));
        else if (!WriteFile(args.output, encoded.bytes))
        {
            std::cerr << "zxasm: cannot write " << args.output << "\n";
            return 1;
        }
        return encoded.ok ? 0 : 1;
    }
    return Usage();
}
