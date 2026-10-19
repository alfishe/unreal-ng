// zxasm - the command-line tool of unreal-asm (decision D-12).
//
//   zxasm formats                                   list the codecs and the versions each one reads and writes
//   zxasm detect   <file>                           which codec reads it (scores, the choice or why none)
//   zxasm encoding <file>                           code page ranking, line ends, text score
//   zxasm decode   <file> [-o out] [--codec id] [--version v] [--codepage cp]
//   zxasm encode   <file> --codec id [--version v] [-o out] [--codepage cp] [--line-end lf|crlf|cr]
//   zxasm files    <image>                           list the files of a disk image (TRD, SCL, FDI, UDI, TD0, DSK ...:
//                                                   its TR-DOS files, through unreal-disk) or a tape image (TAP, TZX)
//   zxasm convert  <file> --to dialect [-o out] [--from dialect]  convert a source to another dialect (alasm ->
//                                                   sjasmplus, ...); --from names the dialect of a plain text file
//   zxasm convert  <image> --to dialect -o dir        the whole project (TRD, TAP or TZX): every source of the image converted together
//                                                   (INCLUDE wildcards resolved), INCBIN files extracted next to them
//   zxasm convert  <directory> --to dialect -o dir [--codec id]  the sources of a host directory as one project (a PC
//                                                   cross assembler's: ASM80): its files are named in lower case without
//                                                   extension (INCLUDE names them so), the other files are copied for INCBIN;
//                                                   --codec takes the files of the usual extensions detection is not sure of
//   zxasm convert  <main source> --project --to dialect -o dir  a host project: the main source, every file it
//                                                   INCLUDEs (and they in turn, each relative to its own folder) converted
//                                                   together, the files its INCBINs name copied beside them; for a retro
//                                                   target (TASM, ALASM, STORM ...) every file gets a TR-DOS name of 8
//                                                   characters, the INCLUDEs and INCBINs naming it so (listed)
//   zxasm convert  ... --out-codepage cp             write the converted sources in that code page (utf-8: readable
//                                                   comments); a line whose code, not its comment, has a non-ASCII byte
//                                                   is warned about, since its string would build other bytes
//   zxasm check    <file> [--codec id] [--version v] [--show]  decode, encode back: byte-exact? how many lines the
//                                                   canonical tokenizer alone reproduces (--show lists the others)
//   zxasm probe    <snapshot.sna|.z80|.szx>          the assemblers whose text is in the snapshot's memory
//   zxasm extract  <snapshot> [--assembler id] [--as text|file] [--to dialect] [-o out]  the text an assembler holds
//                                                   in a snapshot: UTF-8 text, its own file (-o *.$X: hobeta), or
//                                                   converted to a dialect
//   zxasm inject   <source> <target> [-o out] [--assembler id | --codec id [--version v]] [--from dialect]
//                  [--title NAME] [--name NAME.T] [--force] [--disk image]
//                                                   a source (any dialect with a frontend; a plain text is sjasmplus
//                                                   unless --from) written as an assembler's own file into the target:
//                                                   a snapshot (the text of the assembler waiting at its prompt there;
//                                                   --assembler when the probe finds none or two), a disk image of any
//                                                   format unreal-disk writes (TRD, SCL, FDI, UDI, TD0 ...: a file NAME.T
//                                                   on its TR-DOS file system, a blank disk when missing; --assembler or
//                                                   --codec says the format), or a hobeta file *.$X. The target is
//                                                   rewritten unless -o names another file. The source may be a file
//                                                   in an image: <image> --file NAME[.T]. A text file brings the files
//                                                   it INCLUDEs (beside it): into a snapshot each goes into an ALASM
//                                                   page of its own (INCLUDE "NAME",page), onto a disk each as a file;
//                                                   into a snapshot of XAS (one text) onto the disk image --disk names
//                                                   (a blank disk when missing), as XAS' LTEXT reads them
//
// Containers: a hobeta file (NAME.$A, ...) is unwrapped and its catalog fields used for detection; a file inside a
// TR-DOS image, an SCL archive or a tape is picked with --file NAME (or NAME.T for the type letter T). An encode output named *.$X is written
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

#include "../common/diskfiles.h"
#include "unrealasm/sync/reader.h"
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
                 "       convert <file|dir|image> --to dialect [--to-version v] [--from dialect] [--z80n] [--project] [--out-codepage cp] [-o out]   (--z80n: ZX Spectrum Next sources; --project: a main source with its INCLUDEs and INCBINs into the directory -o)\n"
                 "       files <image.trd|.scl|.fdi|.udi|.td0|.tap|.tzx ...> (decode/detect/encoding/check take --file NAME[.T] for a file in an image) |\n"
                 "       probe <snapshot.sna|.z80|.szx> | extract <snapshot> [--assembler id] [--as text|file] [--to dialect] [-o out] |\n"
                 "       inject <source> <snapshot|disk image|file.$X> [-o out] [--assembler id | --codec id [--version v]]\n"
                 "              [--from dialect] [--title NAME] [--name NAME.T] [--force] [--disk image]\n";
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
    std::string command, file, output, codec, codePage, outCodePage, lineEnd, inner, version, to, from;
    std::string toVersion;   // convert: the target's version (its codec's version names: TASM "4.12", XAS "7.447")
    std::string target, assembler, title, name, as;   // inject / extract
    std::string disk;   // inject into a snapshot: the image taking the sources the assembler INCLUDEs from its disk (XAS)
    bool show = false;
    bool force = false;
    bool z80n = false;   // convert: the sources are for the ZX Spectrum Next (sjasmplus --zxnext)
    bool project = false;   // convert: the main source with what it INCLUDEs and INCBINs, into a directory
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
        if (a == "--project")
        {
            args.project = true;
            continue;
        }
        if (a == "--show")
        {
            args.show = true;
            continue;
        }
        if (a == "--force")
        {
            args.force = true;
            continue;
        }
        if (a == "--assembler" || a == "--title" || a == "--name" || a == "--as" || a == "--disk")
        {
            if (!value(a == "--assembler" ? args.assembler : a == "--title" ? args.title : a == "--name" ? args.name : a == "--disk" ? args.disk : args.as))
                return false;
            continue;
        }
        if (a == "-o" ? !value(args.output) : a == "--codec" ? !value(args.codec)
                                          : a == "--codepage" ? !value(args.codePage)
                                          : a == "--out-codepage" ? !value(args.outCodePage)
                                          : a == "--line-end" ? !value(args.lineEnd)
                                          : a == "--file" ? !value(args.inner)
                                          : a == "--version" ? !value(args.version)
                                          : a == "--to" ? !value(args.to)
                                          : a == "--to-version" ? !value(args.toVersion)
                                          : a == "--from" ? !value(args.from) : false)
            return false;
        if (a != "-o" && a != "--codec" && a != "--codepage" && a != "--out-codepage" && a != "--line-end" && a != "--file" && a != "--version" && a != "--to" && a != "--to-version" && a != "--from")
        {
            if (args.file.empty())
                args.file = a;
            else if (args.target.empty() && args.command == "inject")
                args.target = a;
            else
                return false;
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

/// An image of files: a disk image of any format unreal-disk reads (its TR-DOS files), or a tape (.tap / .tzx)
bool IsImage(const std::string& extension)
{
    return tools::IsImage(extension);
}

bool ReadImage(const std::string& path, const std::vector<uint8_t>& bytes, std::vector<containers::TrdosFile>& files, std::string& error)
{
    return tools::ReadImageFiles(path, bytes, files, error);
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
        if (!ReadImage(args.file, bytes, files, error))
        {
            std::cerr << "zxasm: " << error << "\n";
            return false;
        }
        if (args.inner.empty())
            return true;   // the image itself (detect reports none; "files" lists it)
        const size_t dot = args.inner.find_last_of('.');
        const std::string name = dot == std::string::npos ? args.inner : args.inner.substr(0, dot);
        const char type = dot == std::string::npos ? 0 : args.inner[dot + 1];
        const std::string innerExtension = dot == std::string::npos ? std::string() : Lower(args.inner.substr(dot + 1));   // +3DOS: NAME.TYP
        for (const auto& f : files)
            if (f.TrimmedName() == name && (type == 0 || (f.extension.empty() ? f.type == type : f.extension == innerExtension)))
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
    candidates.push_back({spec, '\0'});
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

/// convert --out-codepage cp: the encode options of the converted sources (none given: each keeps its own code page)
bool OutputEncodeOptions(const Args& args, EncodeOptions& options)
{
    if (args.outCodePage.empty())
        return true;
    encoding::CodePage parsed;
    if (!encoding::ParseCodePage(args.outCodePage, parsed))
    {
        std::cerr << "zxasm: unknown code page " << args.outCodePage << "\n";
        return false;
    }
    options.codePage = parsed;
    return true;
}

/// A source written in another code page: a non-ASCII byte in a line's code (a string, a character) builds other
/// bytes than the original; in a comment or in a DISPLAY message (the assembler's console) it does not matter
void WarnCodeNotAscii(const ProjectFile& file, const EncodeOptions& options)
{
    if (!options.codePage || *options.codePage == file.document.codePage)
        return;
    for (size_t n = 0; n < file.document.lines.size(); ++n)
    {
        const std::string& text = file.document.lines[n].text;
        const size_t first = text.find_first_not_of(" \t");
        if (first != std::string::npos && Lower(text.substr(first, 8)) == "display ")
            continue;
        char quote = 0;
        for (size_t k = 0; k < text.size(); ++k)
        {
            const char c = text[k];
            const bool shadowAf = c == '\'' && k >= 2 && std::toupper(static_cast<unsigned char>(text[k - 1])) == 'F' &&
                                  std::toupper(static_cast<unsigned char>(text[k - 2])) == 'A';   // EX AF,AF'
            if (shadowAf && !quote)
                continue;
            if (quote ? c == quote : (c == '"' || c == '\''))
                quote = quote ? 0 : c;
            else if (!quote && c == ';')
                break;
            if (static_cast<unsigned char>(c) >= 0x80)
            {
                std::cerr << "warning line " << n + 1 << ": " << file.name << ": a non-ASCII character outside a comment changes its bytes in "
                          << encoding::CodePageName(*options.codePage) << "\n";
                break;
            }
        }
    }
}

/// zxasm convert image.trd --to dialect -o dir
int ConvertImage(const Args& args, const std::vector<uint8_t>& image, const CodecRegistry& registry)
{
    std::vector<containers::TrdosFile> files;
    std::string error;
    if (!ReadImage(args.file, image, files, error) || args.output.empty() || args.to.empty())
    {
        std::cerr << "zxasm: " << (error.empty() ? "convert of an image needs --to and -o <directory>" : error) << "\n";
        return 2;
    }
    EncodeOptions encodeOptions;
    if (!OutputEncodeOptions(args, encodeOptions))
        return 2;
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
    // INCBIN takes the file up to its catalog length
    options.fileSize = [&files](const std::string& name) -> std::optional<uint64_t> {
        const containers::TrdosFile* file = FindBinary(files, name);
        return file ? std::optional<uint64_t>(file->data.size()) : std::nullopt;
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
        WarnCodeNotAscii(f, encodeOptions);
        if (target)
            out = target->Encode(f.document, encodeOptions).bytes;
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
    EncodeOptions encodeOptions;
    if (!OutputEncodeOptions(args, encodeOptions))
        return 2;
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
        if (codec == forced)
            options.subversion = args.version;   // --version: the files' version, not a guess (Primus' DISK is not ZEUS' DISP)
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
    encodeOptions.subversion = args.toVersion;
    BackendOptions options;
    options.z80n = args.z80n;
    options.targetVersion = args.toVersion;
    options.dataFiles = [&binaries](const std::string& name) {
        const auto found = binaries.find(Lower(name));
        return found == binaries.end() ? std::vector<uint8_t>{} : found->second;
    };
    const ProjectResult converted = ConvertProject(project, args.to, options);
    PrintDiagnostics(converted.diagnostics);
    const ISourceCodec* target = registry.Find(args.to) ? registry.Find(args.to) : registry.Find("text");
    for (ProjectFile f : converted.files)
    {
        // A sector slack INCBIN (TASM's INCBIN copies whole sectors): a host file stands for one on a fresh disk, the
        // rest of its last sector zeros; none when it ends on a sector's end
        for (SourceLine& line : f.document.lines)
        {
            const size_t slackAt = line.text.find(".slack\"");
            if (slackAt == std::string::npos || line.text.find("INCBIN \"") == std::string::npos || line.text.find(',', slackAt) != std::string::npos)
                continue;
            const size_t open = line.text.find('"');
            const std::string base = line.text.substr(open + 1, slackAt - open - 1);
            const auto found = binaries.find(Lower(base));
            const size_t length = found == binaries.end() ? 0 : (256 - found->second.size() % 256) % 256;
            if (length == 0)
                line.text = "; " + line.text.substr(line.text.find_first_not_of(' ')) + " (no sector slack)";
            else
            {
                if (args.to == "sjasmplus")
                    line.text += ",0,(#10000-__UNREALASM_INCBIN_P)<?" + std::to_string(length);
                binaries[Lower(base) + ".slack"] = std::vector<uint8_t>(length, 0);
            }
        }
        WarnCodeNotAscii(f, encodeOptions);
        const EncodeResult encoded = target->Encode(f.document, encodeOptions);
        for (Diagnostic d : encoded.diagnostics)
        {
            d.message = f.name + ": " + d.message;
            PrintDiagnostics({d});
        }
        if (!WriteFile(args.output + "/" + f.name + ".asm", encoded.bytes))
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

/// zxasm convert <main source> --project --to dialect -o dir: the main source and every file it INCLUDEs (HostProject:
/// each relative to the folder of the file that INCLUDEs it) converted together, the files their INCBINs name written
/// beside them. A retro target's disk has no folders and names of 8 characters: every file is renamed for it
/// (ProjectDiskNames), the INCLUDEs and INCBINs written with the new names, the renaming listed
int ConvertHostProject(const Args& args, const std::vector<uint8_t>& bytes, const CatalogHints& hints, const CodecRegistry& registry)
{
    if (args.output.empty() || args.to.empty())
    {
        std::cerr << "zxasm: convert --project needs --to and -o <directory>\n";
        return 2;
    }
    EncodeOptions encodeOptions;
    if (!OutputEncodeOptions(args, encodeOptions))
        return 2;
    encodeOptions.subversion = args.toVersion;
    const ISourceCodec* codec = args.codec.empty() ? registry.Detect(bytes, hints).chosen : registry.Find(args.codec);
    if (!codec)
    {
        std::cerr << "zxasm: convert needs a recognized source (or --codec)\n";
        return 2;
    }
    DecodeOptions decodeOptions;
    decodeOptions.catalog = hints;
    decodeOptions.subversion = args.version;
    DecodeResult decoded = codec->Decode(bytes, decodeOptions);
    if (!args.from.empty())
        decoded.document.dialect = args.from;
    const std::filesystem::path main(std::u8string(args.file.begin(), args.file.end()));
    const std::u8string mainName = main.stem().u8string();
    decoded.document.name = std::string(mainName.begin(), mainName.end());
    const std::filesystem::path folder = main.parent_path();
    const auto read = [&folder](std::string name) {
        std::vector<uint8_t> data;
        std::replace(name.begin(), name.end(), '\\', '/');   // sjasmplus takes \ for a folder separator on every system
        const std::u8string text = (folder / std::filesystem::path(std::u8string(name.begin(), name.end()))).u8string();
        ReadFile(std::string(text.begin(), text.end()), data);
        return data;
    };
    Diagnostics found;
    std::vector<std::string> folders;
    const std::vector<ProjectFile> project = HostProject(decoded.document, read, found, &folders);
    PrintDiagnostics(found);
    // the data files: each INCBIN's name relative to the folder of its file
    std::vector<std::string> dataNames;
    std::map<std::string, std::vector<uint8_t>> data;
    for (size_t k = 0; k < project.size(); ++k)
    {
        const ProjectFile& f = project[k];
        const IFrontend* frontend = DialectRegistry::Builtin().Frontend(f.document.dialect);
        if (!frontend)
            continue;
        for (const ir::Line& line : frontend->Parse(f.document).program.lines)
            for (const ir::Statement& s : line.statements)
                if (s.kind == ir::Statement::Kind::Directive && s.directive == ir::DirectiveKind::Incbin && !s.text.empty() && !data.count(s.text))
                {
                    std::vector<uint8_t> bytesOf = read((k < folders.size() ? folders[k] : std::string()) + s.text);
                    if (bytesOf.empty())
                    {
                        std::cerr << "warning line " << line.sourceLine << ": " << f.name << ": INCBIN \"" << s.text << "\" is not on the host\n";
                        continue;
                    }
                    dataNames.push_back(s.text);
                    data[s.text] = std::move(bytesOf);
                }
    }
    BackendOptions options;
    options.z80n = args.z80n;
    options.targetVersion = args.toVersion;
    options.dataFiles = [&data](const std::string& name) {
        const auto at = data.find(name);
        return at == data.end() ? std::vector<uint8_t>{} : at->second;
    };
    const IBackend* backend = DialectRegistry::Builtin().Backend(args.to);
    const bool disk = backend && backend->JoinsSjasmplusProject();
    if (disk)
        options.fileNames = ProjectDiskNames(project, dataNames);
    // A file the target's format or editor cannot hold whole (ALASM: 64K; TASM 4.12, STORM, XAS: their text memory):
    // the project written again with its long files cut into INCLUDEd parts, smaller until every one fits
    const ISourceCodec* target = registry.Find(args.to) ? registry.Find(args.to) : registry.Find("text");
    ProjectResult converted;
    for (const size_t lines : {size_t(0), size_t(4000), size_t(2000), size_t(1000), size_t(500)})
    {
        options.splitLines = lines;
        converted = ConvertProject(project, args.to, options);
        bool fits = true;
        for (const ProjectFile& f : converted.files)
            for (const Diagnostic& d : target->Encode(f.document, encodeOptions).diagnostics)
                fits = fits && d.message.find("longer than") == std::string::npos;
        if (fits || !disk)
            break;
        std::cerr << "info: a file is longer than " << target->Info().title << " holds: cut into parts of " << (lines ? lines / 2 : 4000) << " lines\n";
    }
    PrintDiagnostics(converted.diagnostics);
    const auto named = [&](const std::string& name) {
        const auto at = options.fileNames.find(name);
        return HostName(at == options.fileNames.end() ? name : at->second);
    };
    std::error_code made;
    const std::filesystem::path out(std::u8string(args.output.begin(), args.output.end()));
    std::filesystem::create_directories(out, made);
    const auto write = [&](const std::string& name, const std::vector<uint8_t>& content) {
        const std::filesystem::path path = out / std::filesystem::path(std::u8string(name.begin(), name.end()));
        std::filesystem::create_directories(path.parent_path(), made);
        const std::u8string text = path.u8string();
        if (WriteFile(std::string(text.begin(), text.end()), content))
            return true;
        std::cerr << "zxasm: cannot write " << std::string(text.begin(), text.end()) << "\n";
        return false;
    };
    bool failed = false;   // a codec could not hold a line (it is left out: an error)
    for (const ProjectFile& f : converted.files)
    {
        WarnCodeNotAscii(f, encodeOptions);
        // a project file keeps its folder where the target has folders (its INCLUDE names it so)
        std::string name = named(f.name);
        if (!disk)
            name = f.name + (Extension(f.name).empty() ? ".asm" : "");
        else
            name += ".asm";
        const EncodeResult encoded = target->Encode(f.document, encodeOptions);
        for (Diagnostic d : encoded.diagnostics)
        {
            d.message = f.name + ": " + d.message;
            PrintDiagnostics({d});
        }
        failed = failed || !encoded.ok || HasErrors(encoded.diagnostics);
        if (!write(name, encoded.bytes))
            return 1;
    }
    for (const std::string& d : dataNames)
        if (!write(disk ? named(d) : d, data[d]))
            return 1;
    if (disk)
        for (const auto& [from, to] : options.fileNames)
            if (from != to)
                std::cerr << "  " << from << " -> " << to << "\n";
    std::cerr << converted.files.size() << " source(s) and " << dataNames.size() << " data file(s) converted to " << args.output << "\n";
    return converted.ok && !failed ? 0 : 1;
}

// Snapshots and part B ---------------------------------------------------------------------------------------------

std::string Stem(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    const size_t dot = name.find('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

/// The snapshot `bytes` of `path` holds; false after a message
bool OpenSnapshot(const std::string& path, const std::vector<uint8_t>& bytes, containers::Snapshot& out)
{
    std::string error;
    if (containers::ReadSnapshot(bytes, Extension(path), out, error))
        return true;
    std::cerr << "zxasm: " << path << ": " << error << "\n";
    return false;
}

/// The assembler --assembler names, else the one the probe finds best; nullptr after a message
const sync::SyncDescriptor* ChooseAssembler(const sync::MachineView& view, const std::string& wanted)
{
    if (!wanted.empty())
    {
        if (const sync::SyncDescriptor* d = sync::FindDescriptor(wanted))
            return d;
        std::cerr << "zxasm: no assembler " << wanted << " (known:";
        for (const sync::SyncDescriptor& d : sync::Descriptors())
            std::cerr << " " << d.id;
        std::cerr << ")\n";
        return nullptr;
    }
    const std::vector<sync::ProbeCandidate> candidates = sync::Probe(view);
    if (candidates.empty())
    {
        std::cerr << "zxasm: no known assembler in the snapshot's memory (--assembler names one)\n";
        return nullptr;
    }
    if (candidates.size() > 1 && candidates[1].score == candidates[0].score)
    {
        std::cerr << "zxasm: " << candidates[0].descriptor->id << " and " << candidates[1].descriptor->id
                  << " identify alike: pick one with --assembler\n";
        return nullptr;
    }
    return candidates[0].descriptor;
}

/// A TR-DOS file of the assembler's format: the name and type the user gave (NAME.T), else `name` and `type`
containers::TrdosFile CatalogFile(const std::string& spec, const std::string& name, char type, const std::string& codec,
                                  const std::string& version, const std::vector<uint8_t>& bytes)
{
    const size_t dot = spec.rfind('.');
    return unrealasm::CatalogFile(spec.empty() ? name : dot == std::string::npos ? spec : spec.substr(0, dot),
                                  !spec.empty() && dot != std::string::npos && dot + 2 == spec.size() ? spec[dot + 1] : type, codec, version, bytes);
}

/// zxasm probe <snapshot>
int ProbeSnapshot(const Args& args, const std::vector<uint8_t>& bytes)
{
    containers::Snapshot snapshot;
    if (!OpenSnapshot(args.file, bytes, snapshot))
        return 1;
    const sync::MachineView view = sync::ViewOf(snapshot);
    std::cout << containers::SnapshotFormatName(snapshot.format) << "\t" << snapshot.machine << "\t" << snapshot.pages.size()
              << " RAM page(s)\n";
    const std::vector<sync::ProbeCandidate> candidates = sync::Probe(view);
    for (const sync::ProbeCandidate& c : candidates)
    {
        const sync::SyncText text = sync::ReadText(view, *c.descriptor);
        std::string why;
        const bool idle = sync::Idle(view, *c.descriptor, why);
        std::cout << c.descriptor->id << "\t" << c.score << "\t" << (text.ok ? text.name + ", " + std::to_string(text.file.size()) + " bytes" : text.state)
                  << "\t" << (idle ? "idle" : "busy: " + why) << "\n";
    }
    return candidates.empty() ? 1 : 0;
}

/// zxasm extract <snapshot> [--assembler id] [--as text|file] [--to dialect] [-o out]
int ExtractSnapshot(const Args& args, const std::vector<uint8_t>& bytes, const CodecRegistry& registry)
{
    containers::Snapshot snapshot;
    if (!OpenSnapshot(args.file, bytes, snapshot))
        return 1;
    const sync::MachineView view = sync::ViewOf(snapshot);
    const sync::SyncDescriptor* d = ChooseAssembler(view, args.assembler);
    if (!d)
        return 1;
    const sync::SyncText text = sync::ReadText(view, *d);
    PrintDiagnostics(text.diagnostics);
    if (!text.ok)
    {
        std::cerr << "zxasm: the text of " << d->title << " does not read (" << text.state << ")\n";
        return 1;
    }
    std::vector<uint8_t> out;
    if (args.as == "file")
    {
        out = text.file;
        const std::string outExtension = Extension(args.output);
        if (outExtension.size() == 2 && outExtension[0] == '$')
        {
            containers::TrdosFile file = CatalogFile("", text.name, outExtension[1], d->codec, d->version, text.file);
            out = containers::WriteHobeta(file);
        }
    }
    else if (args.as.empty() || args.as == "text")
    {
        const ISourceCodec* codec = registry.Find(d->codec);
        DecodeOptions options;
        options.subversion = d->version;
        DecodeResult decoded = codec->Decode(text.file, options);
        PrintDiagnostics(decoded.diagnostics);
        SourceDocument document = std::move(decoded.document);
        if (!args.to.empty())
        {
            const ConvertResult converted = Convert(document, args.to);
            PrintDiagnostics(converted.diagnostics);
            if (!converted.ok)
                return 1;
            document = converted.document;
        }
        if (!args.to.empty() && registry.Find(args.to))
            out = registry.Find(args.to)->Encode(document, {}).bytes;
        else
        {
            std::string plain = document.Text();
            if (!document.lines.empty())
                plain.push_back('\n');
            out.assign(plain.begin(), plain.end());
        }
    }
    else
    {
        std::cerr << "zxasm: --as is text or file\n";
        return 2;
    }
    std::cerr << d->id << ": " << text.name << ", " << text.file.size() << " bytes in page " << text.page
              << (text.changed ? " (changed since saved)" : "") << "\n";
    if (args.output.empty())
        std::cout.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    else if (!WriteFile(args.output, out))
    {
        std::cerr << "zxasm: cannot write " << args.output << "\n";
        return 1;
    }
    return 0;
}

/// zxasm inject <source> <target>: the source (already unwrapped into `bytes` with its catalog `hints`) as an
/// assembler's file in a snapshot, a disk image or a hobeta file
int Inject(const Args& args, const std::vector<uint8_t>& bytes, const CatalogHints& hints, const CodecRegistry& registry)
{
    if (args.target.empty())
        return Usage();
    // The source: a tokenized format by detection, else a text in --from's dialect (sjasmplus by default)
    SourceDocument document;
    const DetectResult detected = registry.Detect(bytes, hints);
    const bool tokenized = detected.chosen && detected.chosen->Info().id != "text";
    if (tokenized)
    {
        DecodeOptions options;
        options.catalog = hints;
        DecodeResult decoded = detected.chosen->Decode(bytes, options);
        PrintDiagnostics(decoded.diagnostics);
        document = std::move(decoded.document);
    }
    else
    {
        document = SourceDocument::FromText(std::string(bytes.begin(), bytes.end()), args.from.empty() ? "sjasmplus" : args.from);
        for (SourceLine& line : document.lines)
            if (!line.text.empty() && line.text.back() == '\r')
                line.text.pop_back();
    }
    if (!args.from.empty())
        document.dialect = args.from;
    document.name = !hints.name.empty() ? hints.name : Stem(args.file);
    for (char& c : document.name)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    // A text file on the host brings the sources it INCLUDEs (relative to its folder): the project
    std::vector<ProjectFile> project = {{document.name, document}};
    if (!tokenized && !IsImage(Lower(Extension(args.file))))
    {
        Diagnostics found;
        const std::filesystem::path folder = std::filesystem::path(std::u8string(args.file.begin(), args.file.end())).parent_path();
        project = HostProject(document, [&folder](const std::string& name) {
            std::vector<uint8_t> data;
            const std::filesystem::path path = folder / std::filesystem::path(std::u8string(name.begin(), name.end()));
            const std::u8string text = path.u8string();
            ReadFile(std::string(text.begin(), text.end()), data);
            return data;
        }, found);
        PrintDiagnostics(found);
    }

    const std::string targetExtension = Lower(Extension(args.target));
    const std::string output = args.output.empty() ? args.target : args.output;
    std::vector<uint8_t> target;
    const bool exists = ReadFile(args.target, target);

    if (containers::IsSnapshotExtension(targetExtension))
    {
        containers::Snapshot snapshot;
        if (!exists)
        {
            std::cerr << "zxasm: cannot read " << args.target << "\n";
            return 1;
        }
        if (!OpenSnapshot(args.target, target, snapshot))
            return 1;
        const sync::MachineView view = sync::ViewOf(snapshot);
        const sync::SyncDescriptor* d = ChooseAssembler(view, args.assembler);
        if (!d)
            return 1;
        const sync::SyncText current = sync::ReadText(view, *d);
        const std::string title = !args.title.empty() ? args.title : current.ok && !current.name.empty() ? current.name : document.name;
        sync::InjectOptions options;
        options.force = args.force;
        std::vector<uint8_t> out;
        const sync::ProjectInjectPlan plan = sync::InjectProjectIntoSnapshot(snapshot, *d, project, title, options, out);
        PrintDiagnostics(plan.diagnostics);
        if (!plan.ok)
        {
            std::cerr << "zxasm: " << plan.error << "\n";
            return 1;
        }
        if (!plan.diskFiles.empty() && args.disk.empty())
        {
            std::cerr << "zxasm: " << d->title << " reads the " << plan.diskFiles.size()
                      << " source(s) the main one INCLUDEs from its disk: --disk <image> takes them\n";
            return 1;
        }
        for (size_t k = 0; k < plan.diskFiles.size(); ++k)
        {
            std::string error;
            if (!tools::PutImageFile(args.disk, args.disk, plan.diskFiles[k], error))
            {
                std::cerr << "zxasm: " << args.disk << ": " << error << "\n";
                return 1;
            }
            std::cerr << d->id << ": " << plan.texts[k + 1].document.name << ", " << plan.texts[k + 1].document.lines.size() << " line(s), "
                      << plan.diskFiles[k].data.size() << " bytes as " << plan.diskFiles[k].FullName() << " -> " << args.disk
                      << " (INCLUDE " << project[k + 1].name << ")\n";
        }
        if (!WriteFile(output, out))
        {
            std::cerr << "zxasm: cannot write " << output << "\n";
            return 1;
        }
        for (size_t k = 0; k < plan.pages.size(); ++k)
            std::cerr << d->id << ": " << plan.texts[k].document.name << ", " << plan.texts[k].document.lines.size() << " line(s), "
                      << plan.texts[k].bytes.size() << " bytes "
                      << (plan.pages[k].page >= 0 ? "into page " + std::to_string(plan.pages[k].page) : std::string("into its memory"))
                      << (plan.pages[k].previous.ok && !plan.pages[k].previous.name.empty() ? " (replaced " + plan.pages[k].previous.name + ")" : std::string())
                      << (k == 0 ? " -> " + output : " (INCLUDE " + project[k].name + ")") << "\n";
        return 0;
    }

    // A file of the format: the assembler's (its codec, version and TR-DOS type) or a codec's
    std::string codec = args.codec, version = args.version;
    char type = 'C';
    if (!args.assembler.empty())
    {
        const sync::SyncDescriptor* d = sync::FindDescriptor(args.assembler);
        if (!d)
        {
            ChooseAssembler(sync::MachineView{}, args.assembler);   // says which ones are known
            return 1;
        }
        codec = d->codec;
        version = d->version;
        type = d->extension.empty() ? 'C' : d->extension[0];
    }
    if (codec.empty())
    {
        std::cerr << "zxasm: an inject into a disk or a file needs --assembler or --codec\n";
        return 2;
    }
    const std::string name = !args.title.empty() ? args.title : document.name;
    const bool image = IsImage(targetExtension) && targetExtension != "tap" && targetExtension != "tzx";
    if (project.size() > 1 && image)
    {
        // Every text of the project onto the disk, each INCLUDE naming its file there
        const std::vector<std::string> names = ProjectTextNames(project, name);
        std::map<std::string, std::string> includeAs;
        for (size_t k = 1; k < project.size(); ++k)
            includeAs[project[k].name] = "\"" + names[k] + "\"";
        const WriteProjectResult written = WriteProjectAs(project, codec, version, names, includeAs);
        PrintDiagnostics(written.diagnostics);
        if (!written.ok)
        {
            std::cerr << "zxasm: " << written.error << "\n";
            return 1;
        }
        std::string error;
        for (size_t k = 0; k < written.files.size(); ++k)
        {
            const WriteAsResult& text = written.files[k];
            const containers::TrdosFile file = CatalogFile(k == 0 ? args.name : std::string(), text.document.name, type, codec, version, text.bytes);
            if (!tools::PutImageFile(k == 0 ? args.target : output, output, file, error))
            {
                std::cerr << "zxasm: " << output << ": " << error << "\n";
                return 1;
            }
            std::cerr << codec << (version.empty() ? "" : " " + version) << ": " << file.TrimmedName() << "." << file.type << ", "
                      << text.document.lines.size() << " line(s), " << text.bytes.size() << " bytes -> " << output << "\n";
        }
        return 0;
    }
    if (project.size() > 1)
        std::cerr << "zxasm: a single file holds the main source only: its INCLUDEs stay for the assembler's disk\n";
    const WriteAsResult written = WriteAs(document, codec, version, name);
    PrintDiagnostics(written.diagnostics);
    if (!written.ok)
    {
        std::cerr << "zxasm: " << written.error << "\n";
        return 1;
    }
    const containers::TrdosFile file = CatalogFile(args.name, written.document.name, type, codec, version, written.bytes);
    std::string error;
    if (targetExtension.size() == 2 && targetExtension[0] == '$')
    {
        containers::TrdosFile hobeta = file;
        hobeta.type = Extension(args.target)[1];
        if (!WriteFile(output, containers::WriteHobeta(hobeta)))
        {
            std::cerr << "zxasm: cannot write " << output << "\n";
            return 1;
        }
    }
    else if (image)
    {
        // A disk image of any format: the file goes onto its TR-DOS file system, the image is saved in the format of -o
        if (!tools::PutImageFile(args.target, output, file, error))
        {
            std::cerr << "zxasm: " << args.target << ": " << error << "\n";
            return 1;
        }
    }
    else
    {
        std::cerr << "zxasm: inject writes into a snapshot (.sna .z80 .szx), a disk image (.trd .scl .fdi .udi .td0 ...) or a hobeta file (.$X)\n";
        return 2;
    }
    std::cerr << codec << (version.empty() ? "" : " " + version) << ": " << file.TrimmedName() << "." << file.type << ", "
              << written.document.lines.size() << " line(s), " << written.bytes.size() << " bytes -> " << output << "\n";
    return 0;
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
    if (args.command == "probe")
        return ProbeSnapshot(args, bytes);
    if (args.command == "extract" && containers::IsSnapshotExtension(Extension(args.file)))
        return ExtractSnapshot(args, bytes, registry);
    if (args.command == "inject")
    {
        if (!Unwrap(args, bytes, hints))
            return 1;
        return Inject(args, bytes, hints, registry);
    }
    if (args.command == "extract")
        args.command = "decode";   // a file in an image or a container: what decode does
    if (args.command == "files")
    {
        std::vector<containers::TrdosFile> files;
        std::string error;
        if (!ReadImage(args.file, bytes, files, error))
        {
            std::cerr << "zxasm: " << error << "\n";
            return 1;
        }
        for (const auto& f : files)
        {
            const DetectResult detected = registry.Detect(f.data, f.Hints());
            std::cout << f.FullName() << "\t" << f.start << "\t" << f.length << "\t"
                      << (detected.chosen ? detected.chosen->Info().id : "-") << "\n";
        }
        return 0;
    }
    if (args.command == "convert" && IsImage(Lower(Extension(args.file))) && args.inner.empty())
        return ConvertImage(args, bytes, registry);
    if (args.command == "convert" && args.project)
        return ConvertHostProject(args, bytes, hints, registry);
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
        convertOptions.targetVersion = args.toVersion;
        const ConvertResult converted = Convert(decoded.document, args.to, convertOptions);
        PrintDiagnostics(converted.diagnostics);
        EncodeOptions encodeOptions;
        if (!OutputEncodeOptions(args, encodeOptions))
            return 2;
        encodeOptions.subversion = args.toVersion;
        WarnCodeNotAscii({decoded.document.name, converted.document}, encodeOptions);
        // Written with the target's text codec when there is one (sjasmplus keeps the Spectrum code page for strings,
        // unless --out-codepage says otherwise)
        std::vector<uint8_t> out;
        bool encoded = true;
        if (const ISourceCodec* target = registry.Find(args.to) ? registry.Find(args.to) : registry.Find("text"))
        {
            // what the target's file cannot hold (a line left out) is said, and fails the run
            const EncodeResult written = target->Encode(converted.document, encodeOptions);
            PrintDiagnostics(written.diagnostics);
            encoded = !HasErrors(written.diagnostics);
            out = written.bytes;
        }
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
        return converted.ok && encoded ? 0 : 1;
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
