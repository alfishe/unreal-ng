// zxasm - the command-line tool of unreal-asm (decision D-12).
//
//   zxasm formats                                   list the codecs and the versions each one reads and writes
//   zxasm detect   <file>                           which codec reads it (scores, the choice or why none)
//   zxasm encoding <file>                           code page ranking, line ends, text score
//   zxasm decode   <file> [-o out] [--codec id] [--version v] [--codepage cp]
//   zxasm encode   <file> --codec id [--version v] [-o out] [--codepage cp] [--line-end lf|crlf|cr]
//   zxasm files    <image.trd|.tap|.tzx>             list the files of a TR-DOS disk or tape image
//   zxasm convert  <file> --to dialect [-o out] [--from dialect]  convert a source to another dialect (alasm ->
//                                                   sjasmplus, ...); --from names the dialect of a plain text file
//   zxasm convert  <image> --to dialect -o dir        the whole project (TRD, TAP or TZX): every source of the image converted together
//                                                   (INCLUDE wildcards resolved), INCBIN files extracted next to them
//   zxasm convert  <directory> --to dialect -o dir [--codec id]  the sources of a host directory as one project (a PC
//                                                   cross assembler's: ASM80): its files are named in lower case without
//                                                   extension (INCLUDE names them so), the other files are copied for INCBIN;
//                                                   --codec takes the files of the usual extensions detection is not sure of
//   zxasm check    <file> [--codec id] [--version v] [--show]  decode, encode back: byte-exact? how many lines the
//                                                   canonical tokenizer alone reproduces (--show lists the others)
//
// Containers: a hobeta file (NAME.$A, ...) is unwrapped and its catalog fields used for detection; a file inside a
// TR-DOS image is picked with --file NAME (or NAME.T for the type letter T). An encode output named *.$X is written
// as a hobeta file of type X.
//
// --version: decode reads the file as that version of its format (default: detected, printed on stderr); encode writes
// that version (default: the newest).
//
// Decoded text is written as UTF-8 (decision D-11). Exit code 0 on success, 1 on errors, 2 on bad usage.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "unrealasm/unrealasm.h"

using namespace unrealasm;

namespace
{
int Usage()
{
    std::cerr << "usage: zxasm formats | detect <file> | encoding <file> |\n"
                 "       decode <file> [-o out] [--codec id] [--version v] [--codepage cp] |\n"
                 "       encode <file> --codec id [--version v] [-o out] [--codepage cp] [--line-end lf|crlf|cr] |\n"
                 "       check <file> [--codec id] [--version v] |\n"
                 "       convert <file|dir|image> --to dialect [--from dialect] [--z80n] [-o out]   (--z80n: ZX Spectrum Next sources)\n"
                 "       files <image.trd|.tap|.tzx> (decode/detect/encoding/check take --file NAME[.T] for a file in an image)\n";
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
    std::string command, file, output, codec, codePage, lineEnd, inner, version, to, from;
    bool show = false;
    bool z80n = false;   // convert: the sources are for the ZX Spectrum Next (sjasmplus --zxnext)
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
        if (a == "--z80n")
        {
            args.z80n = true;
            continue;
        }
        if (a == "--show")
        {
            args.show = true;
            continue;
        }
        if (a == "-o" ? !value(args.output) : a == "--codec" ? !value(args.codec)
                                          : a == "--codepage" ? !value(args.codePage)
                                          : a == "--line-end" ? !value(args.lineEnd)
                                          : a == "--file" ? !value(args.inner)
                                          : a == "--version" ? !value(args.version)
                                          : a == "--to" ? !value(args.to)
                                          : a == "--from" ? !value(args.from) : false)
            return false;
        if (a != "-o" && a != "--codec" && a != "--codepage" && a != "--line-end" && a != "--file" && a != "--version" && a != "--to" && a != "--from")
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

/// The catalog's start field each assembler writes for a saved source (the research documents of each codec)
uint16_t CatalogStart(const std::string& codec, const std::string& version, char type)
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

/// An image of files: a TR-DOS disk (.trd) or a tape (.tap / .tzx)
bool IsImage(const std::string& extension)
{
    return extension == "trd" || extension == "tap" || extension == "tzx";
}

bool ReadImage(const std::string& extension, std::span<const uint8_t> bytes, std::vector<containers::TrdosFile>& files, std::string& error)
{
    return extension == "trd" ? containers::ReadTrd(bytes, files, error) : containers::ReadTape(bytes, files, error);
}

/// Hobeta / TR-DOS image / tape image -> the file's bytes and catalog hints; false with a message when the container is broken or
/// the named file is missing. Plain host files pass through.
bool Unwrap(const Args& args, std::vector<uint8_t>& bytes, CatalogHints& hints)
{
    const std::string extension = Lower(Extension(args.file));
    containers::TrdosFile file;
    std::string error;
    if (IsImage(extension))
    {
        std::vector<containers::TrdosFile> files;
        if (!ReadImage(extension, bytes, files, error))
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
    if (bytes.size() >= 128 && std::equal(bytes.begin(), bytes.begin() + 8, "PLUS3DOS"))
    {
        // a +3DOS file (the Next's .god, .asm ... saved by NextZXOS): the data after the 128-byte header
        containers::Plus3dosFile plus3;
        if (!containers::ReadPlus3dos(bytes, plus3, error))
        {
            std::cerr << "zxasm: " << error << "\n";
            return false;
        }
        bytes = plus3.data;
        return true;
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
/// A TR-DOS name pattern: ? is any one character, * the rest (ALASM and TASM accept them in INCBIN / INCLUDE)
bool NameMatches(const std::string& pattern, const std::string& name)
{
    size_t k = 0;
    for (; k < pattern.size(); ++k)
    {
        if (pattern[k] == '*')
            return true;
        if (k >= name.size() || (pattern[k] != '?' && pattern[k] != name[k]))
            return false;
    }
    return k == name.size();
}

/// The image's file an INCBIN names: "NAME", "NAME.T" (T the type), a name holding a dot itself ("PIC.SCR"), ZAsm's
/// "NAME.Txx", with
/// wildcards; without a type the code file (type C) is meant, as TASM and ALASM read it. An exact name is the first
/// entry TR-DOS finds; a wildcard the last fitting one (ALASM help, Work)
const containers::TrdosFile* FindBinary(const std::vector<containers::TrdosFile>& files, const std::string& spec)
{
    std::vector<std::pair<std::string, char>> candidates;
    const size_t dot = spec.rfind('.');
    if (dot != std::string::npos && dot + 2 == spec.size())
        candidates.push_back({spec.substr(0, dot), spec[dot + 1]});
    candidates.push_back({spec, 0});
    // ZAsm shows a name with the type letter and the two bytes of the catalog's start as its extension ("FONT.fn1")
    if (dot != std::string::npos && dot > 0 && dot + 4 == spec.size())
        candidates.push_back({spec.substr(0, dot), spec[dot + 1]});
    for (const auto& [pattern, type] : candidates)
    {
        const bool wildcard = pattern.find_first_of("*?") != std::string::npos;
        const containers::TrdosFile* found = nullptr;
        for (const auto& file : files)
        {
            if (!NameMatches(pattern, file.TrimmedName()) || (type != 0 && file.type != type))
                continue;
            const bool better = !found || (type == 0 && file.type == 'C' && found->type != 'C') ||
                                (wildcard && !(type == 0 && found->type == 'C' && file.type != 'C'));
            if (better)
                found = &file;
        }
        if (found)
            return found;
    }
    return nullptr;
}

/// Where a converted line includes a binary file: INCBIN "..." (sjasmplus, pasmo) or BINARY "..." (z80asm), both 8
/// characters up to the name
size_t FindIncbin(const std::string& text)
{
    const size_t incbin = text.find("INCBIN \"");
    return incbin != std::string::npos ? incbin : text.find("BINARY \"");
}

/// A TR-DOS name as a host file name: TR-DOS allows / \ : and the like ("SIN64/FF"), a file system and an INCBIN
/// path do not
std::string HostName(std::string name)
{
    for (char& c : name)
        if (c == '/' || c == '\\' || c == ':' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    return name;
}

/// zxasm convert image.trd --to dialect -o dir
int ConvertImage(const Args& args, const std::vector<uint8_t>& image, const CodecRegistry& registry)
{
    std::vector<containers::TrdosFile> files;
    std::string error;
    if (!ReadImage(Lower(Extension(args.file)), image, files, error) || args.output.empty() || args.to.empty())
    {
        std::cerr << "zxasm: " << (error.empty() ? "convert of an image needs --to and -o <directory>" : error) << "\n";
        return 2;
    }
    // The output directory is made when missing (a UTF-8 name: std::u8string keeps it intact on Windows)
    std::error_code made;
    std::filesystem::create_directories(std::filesystem::path(std::u8string(args.output.begin(), args.output.end())), made);
    if (made)
    {
        std::cerr << "zxasm: cannot make " << args.output << ": " << made.message() << "\n";
        return 1;
    }
    // The sources, and the files they INCLUDE that detection could not tell (read like their includer)
    const std::vector<ProjectFile> project = ImageProject(files);
    // Data files a source reads while assembling (ZX-ASM's LOADTAB table) come from the image as the assembler loads
    // them: the data and the rest of the last sector
    BackendOptions options;
    options.z80n = args.z80n;
    options.dataFiles = [&files](const std::string& name) {
        const containers::TrdosFile* file = FindBinary(files, name);
        std::vector<uint8_t> bytes;
        if (file)
        {
            bytes = file->data;
            bytes.insert(bytes.end(), file->tail.begin(), file->tail.end());
        }
        return bytes;
    };
    const ProjectResult converted = ConvertProject(project, args.to, options);
    PrintDiagnostics(converted.diagnostics);
    // A dialect without its own codec (pasmo) is a text file in the document's code page (texts are program bytes)
    const ISourceCodec* target = registry.Find(args.to) ? registry.Find(args.to) : registry.Find("text");
    std::string extracted;
    std::map<std::string, std::string> imageNames;   // host file name -> the name in the image
    for (ProjectFile f : converted.files)
    {
        // An INCBIN with wildcards names the file it found (the converted source then assembles anywhere). A sector
        // slack INCBIN gets its length, limited to the end of memory; an empty slack is left out
        for (SourceLine& line : f.document.lines)
        {
            const size_t slackAt = line.text.find(".slack\"");
            if (slackAt != std::string::npos && line.text.find("INCBIN \"") != std::string::npos && line.text.find(',', slackAt) == std::string::npos)
            {
                const size_t open = line.text.find('"');
                const containers::TrdosFile* base = FindBinary(files, line.text.substr(open + 1, slackAt - open - 1));
                const size_t length = base ? base->tail.size() : 0;
                if (length == 0)
                    line.text = "; " + line.text.substr(line.text.find_first_not_of(' ')) + " (no sector slack)";
                else if (args.to == "sjasmplus")   // sjasmplus' INCBIN takes an offset and a length; pasmo's the whole file
                    line.text += ",0,(#10000-__UNREALASM_INCBIN_P)<?" + std::to_string(length);
            }
        }
        for (SourceLine& line : f.document.lines)
        {
            const size_t at = FindIncbin(line.text);
            if (at == std::string::npos)
                continue;
            const size_t close = line.text.find('"', at + 8);
            std::string wanted = line.text.substr(at + 8, close - at - 8);
            const bool slack = wanted.size() > 6 && wanted.compare(wanted.size() - 6, 6, ".slack") == 0;
            if (slack)
                wanted.resize(wanted.size() - 6);
            std::string resolved = wanted;
            if (wanted.find_first_of("*?") != std::string::npos)
                if (const containers::TrdosFile* found = FindBinary(files, wanted))
                    resolved = found->TrimmedName() + (found->type == 'C' ? std::string() : std::string(".") + found->type);
            const std::string host = HostName(resolved);
            imageNames[host] = resolved;
            if (host != wanted)
                line.text.replace(at + 8, wanted.size(), host);
        }
        // A target whose INCBIN takes the whole file (pasmo) marks a part as "; unreal-asm: slice offset[,length]": the
        // part goes to its own file "name.offset-length"
        for (SourceLine& line : f.document.lines)
        {
            const size_t marker = line.text.find("; unreal-asm: slice ");
            const size_t at = FindIncbin(line.text);
            if (marker == std::string::npos || at == std::string::npos)
                continue;
            const size_t close = line.text.find('"', at + 8);
            const std::string name = line.text.substr(at + 8, close - at - 8);
            const std::string spec = line.text.substr(marker + 20);
            const size_t offset = std::stoul(spec);
            const containers::TrdosFile* found = FindBinary(files, name);
            if (!found)
                continue;
            const size_t comma = spec.find(',');
            const size_t length = comma == std::string::npos ? found->data.size() - std::min(offset, found->data.size()) : std::stoul(spec.substr(comma + 1));
            const std::string part = name + "." + std::to_string(offset) + "-" + std::to_string(length);
            std::vector<uint8_t> bytes;
            for (size_t k = offset; k < offset + length && k < found->data.size(); ++k)
                bytes.push_back(found->data[k]);
            WriteFile(args.output + "/" + HostName(part), bytes);
            line.text = line.text.substr(0, at + 8) + HostName(part) + line.text.substr(close, marker - close);
            while (!line.text.empty() && line.text.back() == ' ')
                line.text.pop_back();
        }
        std::vector<uint8_t> out;
        if (target)
            out = target->Encode(f.document, {}).bytes;
        else
        {
            const std::string text = f.document.Text() + "\n";
            out.assign(text.begin(), text.end());
        }
        if (!WriteFile(args.output + "/" + f.name + ".asm", out))
        {
            std::cerr << "zxasm: cannot write " << args.output << "/" << f.name << ".asm\n";
            return 1;
        }
        // INCBIN "name" / "name.T": the file of the image, written under the name the source uses
        for (const SourceLine& line : f.document.lines)
        {
            const size_t at = FindIncbin(line.text);
            if (at == std::string::npos)
                continue;
            const size_t close = line.text.find('"', at + 8);
            const std::string wanted = line.text.substr(at + 8, close - at - 8);
            // "<file>.slack": the rest of the file's last sector (TASM's INCBIN copies whole sectors)
            const bool slack = wanted.size() > 6 && wanted.compare(wanted.size() - 6, 6, ".slack") == 0;
            const std::string host = slack ? wanted.substr(0, wanted.size() - 6) : wanted;
            const auto inImage = imageNames.find(host);
            const containers::TrdosFile* found = FindBinary(files, inImage != imageNames.end() ? inImage->second : host);
            if (found)
            {
                WriteFile(args.output + "/" + wanted, slack ? found->tail : found->data);
                if (extracted.find("|" + wanted + "|") == std::string::npos)
                    extracted += "|" + wanted + "|";
            }
        }
    }
    std::cerr << converted.files.size() << " source(s) converted to " << args.output << "\n";
    return converted.ok ? 0 : 1;
}
/// zxasm convert <directory> --to dialect -o dir: the text sources of a host directory as one project (DOS names: the
/// sources go by their lower-case name without extension, binaries by their lower-case name)
int ConvertDirectory(const Args& args, const CodecRegistry& registry)
{
    if (args.output.empty() || args.to.empty())
    {
        std::cerr << "zxasm: convert of a directory needs --to and -o <directory>\n";
        return 2;
    }
    const ISourceCodec* forced = args.codec.empty() ? nullptr : registry.Find(args.codec);
    if (!args.codec.empty() && !forced)
    {
        std::cerr << "zxasm: unknown codec " << args.codec << "\n";
        return 2;
    }
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(std::u8string(args.file.begin(), args.file.end()))))
        if (entry.is_regular_file())
            paths.push_back(entry.path());
    std::sort(paths.begin(), paths.end());
    std::vector<ProjectFile> project;
    std::map<std::string, std::vector<uint8_t>> binaries;   // lower-case name -> bytes
    for (const std::filesystem::path& path : paths)
    {
        std::vector<uint8_t> bytes;
        const std::u8string u8 = path.u8string();
        const std::string name(u8.begin(), u8.end());
        if (!ReadFile(name, bytes))
            continue;
        CatalogHints hints;
        hints.extension = Extension(name);
        const std::string ext = Lower(hints.extension);
        const ISourceCodec* codec = nullptr;
        if (forced && (forced->Detect(bytes, hints) > 0 || ext == "a80" || ext == "sym" || ext == "asm" || ext == "s" || ext == "z80"))
            codec = forced;
        else if (!forced)
        {
            const DetectResult detected = registry.Detect(bytes, hints);
            if (detected.chosen && detected.chosen->Info().id != "text")
                codec = detected.chosen;
        }
        const std::u8string fileU8 = path.filename().u8string();
        const std::string file = Lower(std::string(fileU8.begin(), fileU8.end()));
        if (!codec)
        {
            binaries[file] = bytes;
            continue;
        }
        DecodeOptions options;
        options.catalog = hints;
        DecodeResult decoded = codec->Decode(bytes, options);
        if (!decoded.ok)
        {
            binaries[file] = bytes;
            continue;
        }
        std::string stem = file.substr(0, file.find_last_of('.') == std::string::npos ? file.size() : file.find_last_of('.'));
        decoded.document.name = stem;
        project.push_back({stem, std::move(decoded.document)});
    }
    std::error_code made;
    std::filesystem::create_directories(std::filesystem::path(std::u8string(args.output.begin(), args.output.end())), made);
    BackendOptions options;
    options.z80n = args.z80n;
    options.dataFiles = [&binaries](const std::string& name) {
        const auto found = binaries.find(Lower(name));
        return found == binaries.end() ? std::vector<uint8_t>{} : found->second;
    };
    const ProjectResult converted = ConvertProject(project, args.to, options);
    PrintDiagnostics(converted.diagnostics);
    const ISourceCodec* target = registry.Find(args.to) ? registry.Find(args.to) : registry.Find("text");
    for (const ProjectFile& f : converted.files)
    {
        if (!WriteFile(args.output + "/" + f.name + ".asm", target->Encode(f.document, {}).bytes))
        {
            std::cerr << "zxasm: cannot write " << args.output << "/" << f.name << ".asm\n";
            return 1;
        }
        // INCBIN "name": the directory's file of that name in any case, written under the name the source uses
        for (const SourceLine& line : f.document.lines)
        {
            const size_t at = FindIncbin(line.text);
            if (at == std::string::npos)
                continue;
            const size_t close = line.text.find('"', at + 8);
            const std::string wanted = line.text.substr(at + 8, close - at - 8);
            const auto found = binaries.find(Lower(wanted));
            if (found != binaries.end())
                WriteFile(args.output + "/" + wanted, found->second);
            else
                std::cerr << "warning: " << f.name << ": INCBIN \"" << wanted << "\" is not in " << args.file << "\n";
        }
    }
    std::cerr << converted.files.size() << " source(s) converted to " << args.output << "\n";
    return converted.ok ? 0 : 1;
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
        {
            std::cout << codec->Info().id << "\t" << codec->Info().title << "\n";
            for (const Subversion& v : codec->Info().subversions)
                std::cout << "\t--version " << v.id << "\t" << v.title << "\n";
        }
        return 0;
    }
    if (args.file.empty())
        return Usage();
    if (args.command == "convert" && std::filesystem::is_directory(std::filesystem::path(std::u8string(args.file.begin(), args.file.end()))))
        return ConvertDirectory(args, registry);
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
        if (!ReadImage(Lower(Extension(args.file)), bytes, files, error))
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
    if (args.command == "convert" && IsImage(Lower(Extension(args.file))) && args.inner.empty())
        return ConvertImage(args, bytes, registry);
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

    if (args.command == "convert")
    {
        const DetectResult detected = args.codec.empty() ? registry.Detect(bytes, hints) : DetectResult{};
        const ISourceCodec* codec = args.codec.empty() ? detected.chosen : registry.Find(args.codec);
        if (!codec || args.to.empty())
        {
            std::cerr << "zxasm: convert needs a recognized source (or --codec) and --to dialect\n";
            return 2;
        }
        DecodeOptions options;
        options.catalog = hints;
        options.codePage = codePage;
        options.subversion = args.version;
        DecodeResult decoded = codec->Decode(bytes, options);
        if (!args.from.empty())
            decoded.document.dialect = args.from;
        BackendOptions convertOptions;
        convertOptions.z80n = args.z80n;
        const ConvertResult converted = Convert(decoded.document, args.to, convertOptions);
        PrintDiagnostics(converted.diagnostics);
        // Written with the target's text codec when there is one (sjasmplus keeps the Spectrum code page for strings)
        std::vector<uint8_t> out;
        if (const ISourceCodec* target = registry.Find(args.to) ? registry.Find(args.to) : registry.Find("text"))
            out = target->Encode(converted.document, {}).bytes;
        else
        {
            const std::string text = converted.document.Text() + "\n";
            out.assign(text.begin(), text.end());
        }
        if (args.output.empty())
            std::cout.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
        else if (!WriteFile(args.output, out))
        {
            std::cerr << "zxasm: cannot write " << args.output << "\n";
            return 1;
        }
        return converted.ok ? 0 : 1;
    }
    if (args.command == "decode" || args.command == "check")
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
        options.subversion = args.version;
        options.catalog = hints;
        const DecodeResult decoded = codec->Decode(bytes, options);
        PrintDiagnostics(decoded.diagnostics);
        if (args.command == "check")
        {
            const EncodeResult exact = codec->Encode(decoded.document, {});
            // Per line: the line alone, once with its kept bytes and once without (file-level data kept in both)
            size_t canonical = 0;
            SourceDocument one = decoded.document;
            for (const SourceLine& line : decoded.document.lines)
            {
                one.lines = {line};
                const EncodeResult kept = codec->Encode(one, {});
                one.lines[0].attrs = {};
                const bool equal = codec->Encode(one, {}).bytes == kept.bytes;
                canonical += equal;
                if (!equal && args.show)
                    std::cout << "  canonical differs: " << line.text << "\n";
            }
            const SourceDocument& plain = decoded.document;
            // A format that reads the sector slack (XAS) writes it back too
            std::vector<uint8_t> sectors = bytes;
            sectors.insert(sectors.end(), hints.slack.begin(), hints.slack.end());
            const bool same = exact.bytes == bytes || exact.bytes == sectors;
            std::string range;
            for (const std::string& v : decoded.subversions)
                range += (range.empty() ? "" : ",") + v;
            std::cout << args.file << (args.inner.empty() ? "" : ":" + args.inner) << "\t" << codec->Info().id << "\t"
                      << decoded.document.subversion << "\t[" << range << "]\t" << decoded.document.lines.size() << " lines\t"
                      << (same ? "byte-exact" : "DIFFERS") << "\tcanonical " << canonical << "/" << plain.lines.size() << "\n";
            return decoded.ok && same ? 0 : 1;
        }
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
        std::cerr << codec->Info().id << (decoded.document.subversion.empty() ? "" : " " + decoded.document.subversion) << ": "
                  << decoded.document.lines.size() << " line(s), " << encoding::CodePageName(decoded.document.codePage) << "\n";
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
        const std::string outName = args.output.substr(args.output.find_last_of("/\\") == std::string::npos ? 0 : args.output.find_last_of("/\\") + 1);
        document.name = outName.substr(0, outName.find('.'));
        EncodeOptions options;
        options.codePage = codePage;
        options.subversion = args.version;
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
            const std::string version = !args.version.empty() ? args.version
                                        : codec->Info().subversions.empty() ? std::string() : codec->Info().subversions.back().id;
            file.start = CatalogStart(codec->Info().id, version, file.type);
            // XAS keeps 0 in the length field (it reads whole sectors): its file list skips other files
            file.length = codec->Info().id == "xas" ? 0 : static_cast<uint16_t>(encoded.bytes.size());
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
