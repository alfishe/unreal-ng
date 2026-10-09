#include "asmcontrol.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "common/base64.h"
#include "common/filehelper.h"
#include "debugger/asm/diskfiles.h"
#include "unrealasm/codec.h"
#include "unrealasm/containers.h"
#include "unrealasm/dialect.h"
#include "unrealasm/registry.h"

/// region <Errors and the reply>

int AsmControlErrorHttpStatus(AsmControlError error)
{
    switch (error)
    {
        case AsmControlError::None: return 200;
        case AsmControlError::BadRequest: return 400;
        case AsmControlError::NotFound: return 404;
        case AsmControlError::Refused: return 409;
        case AsmControlError::Failed: return 422;
    }
    return 500;
}

const char* AsmControlErrorPhrase(AsmControlError error)
{
    switch (error)
    {
        case AsmControlError::None: return "";
        case AsmControlError::BadRequest: return "Bad Request";
        case AsmControlError::NotFound: return "Not Found";
        case AsmControlError::Refused: return "Conflict";
        case AsmControlError::Failed: return "Unprocessable Entity";
    }
    return "Internal Server Error";
}

StateNode AsmReply::ToValue() const
{
    if (Ok())
        return body;
    StateNode value = StateNode::Object();
    value["error"] = AsmControlErrorPhrase(error);
    value["message"] = message;
    for (const auto& [key, field] : body.members)
        if (key != "error" && key != "message")
            value[key] = field;
    return value;
}

/// endregion </Errors and the reply>

namespace
{
using namespace unrealasm;

AsmReply Fail(AsmControlError error, std::string message, StateNode body = StateNode::Object())
{
    AsmReply reply;
    reply.error = error;
    reply.message = std::move(message);
    reply.body = std::move(body);
    return reply;
}

std::string Option(const AsmRequest& request, const std::string& name)
{
    const auto it = request.options.find(name);
    return it != request.options.end() ? it->second : std::string();
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string Extension(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? std::string() : path.substr(dot + 1);
}

bool ReadHostFile(const std::string& path, std::vector<uint8_t>& out)
{
    std::ifstream file(FileHelper::ToFsPath(path), std::ios::binary);
    if (!file.is_open())
        return false;
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

bool WriteHostFile(const std::string& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream file(FileHelper::ToFsPath(path), std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

const char* FamilyName(CodecFamily family)
{
    return family == CodecFamily::Tokenized ? "tokenized" : "text";
}

StateNode DiagnosticsValue(const Diagnostics& diagnostics)
{
    StateNode list = StateNode::Array();
    for (const Diagnostic& d : diagnostics)
    {
        StateNode item = StateNode::Object();
        item["severity"] = d.severity == Severity::Error ? "error" : d.severity == Severity::Warning ? "warning" : "info";
        item["line"] = static_cast<unsigned>(d.line);
        item["message"] = d.message;
        list.items.push_back(std::move(item));
    }
    return list;
}

/// The catalog start a file of a format needs to be listed and recognized (the same rule as zxasm's hobeta output)
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
        return 0x5341;   // "AS": XAS lists only files that carry it
    return 0;
}

struct Source
{
    std::vector<uint8_t> bytes;
    CatalogHints hints;
    std::string name;   ///< as the reply names it
};

/// The source a request names (path, disk:A/NAME.T, or data + name); false with the failure reply
bool LoadSource(EmulatorContext* context, const AsmRequest& request, Source& out, AsmReply& failure)
{
    const std::string path = Option(request, "path");
    const bool upload = request.options.count("data") > 0;
    if (path.empty() == !upload)
    {
        failure = Fail(AsmControlError::BadRequest, "'" + request.verb + "' needs a path (a host file or disk:A/NAME.T) or data (base64), not both");
        return false;
    }
    std::string error;
    if (upload)
    {
        if (!base64::Decode(Option(request, "data"), out.bytes))
        {
            failure = Fail(AsmControlError::BadRequest, "'data' is no base64");
            return false;
        }
        out.name = Option(request, "name").empty() ? std::string("upload") : Option(request, "name");
    }
    else if (DiskFileRef ref; ParseDiskFileRef(path, ref))
    {
        if (!context)
        {
            failure = Fail(AsmControlError::BadRequest, "disk files need an emulator instance");
            return false;
        }
        containers::TrdosFile file;
        if (!ReadDiskFile(context, ref, file, error))
        {
            failure = Fail(AsmControlError::NotFound, error);
            return false;
        }
        out.bytes = file.data;
        out.hints = file.Hints();
        out.name = path;
        return true;
    }
    else
    {
        if (!ReadHostFile(path, out.bytes))
        {
            failure = Fail(AsmControlError::NotFound, "cannot read " + path);
            return false;
        }
        out.name = path;
    }
    // A host file or an upload: an image with `file` inside it, a hobeta file, a +3DOS file, else the bytes as they are
    const std::string extension = Lower(Extension(out.name));
    out.hints.extension = Extension(out.name);
    const std::string inner = Option(request, "file");
    if (extension == "trd" || extension == "tap" || extension == "tzx")
    {
        if (inner.empty())
        {
            failure = Fail(AsmControlError::BadRequest, "an image needs 'file' (NAME.T inside it); 'files' lists a disk in a drive");
            return false;
        }
        std::vector<containers::TrdosFile> files;
        const bool read = extension == "trd" ? containers::ReadTrd(out.bytes, files, error) : containers::ReadTape(out.bytes, files, error);
        if (!read)
        {
            failure = Fail(AsmControlError::Failed, error);
            return false;
        }
        const size_t dot = inner.find_last_of('.');
        const std::string name = dot == std::string::npos ? inner : inner.substr(0, dot);
        const char type = dot == std::string::npos || dot + 1 >= inner.size() ? 0 : inner[dot + 1];
        const auto found = std::find_if(files.rbegin(), files.rend(), [&](const containers::TrdosFile& f) {
            return f.TrimmedName() == name && (type == 0 || f.type == type);
        });
        if (found == files.rend())
        {
            failure = Fail(AsmControlError::NotFound, "no file " + inner + " in " + out.name);
            return false;
        }
        out.bytes = found->data;
        out.hints = found->Hints();
        out.name += ":" + inner;
        return true;
    }
    if (!inner.empty())
    {
        failure = Fail(AsmControlError::BadRequest, "'file' applies to an image (.trd, .tap, .tzx)");
        return false;
    }
    if (out.bytes.size() >= 128 && std::equal(out.bytes.begin(), out.bytes.begin() + 8, "PLUS3DOS"))
    {
        containers::Plus3dosFile plus3;
        if (!containers::ReadPlus3dos(out.bytes, plus3, error))
        {
            failure = Fail(AsmControlError::Failed, error);
            return false;
        }
        out.bytes = plus3.data;
    }
    else if (!extension.empty() && extension[0] == '$')
    {
        containers::TrdosFile file;
        if (!containers::ReadHobeta(out.bytes, file, error))
        {
            failure = Fail(AsmControlError::Failed, error);
            return false;
        }
        out.bytes = file.data;
        out.hints = file.Hints();
    }
    return true;
}

/// Writes to a host file or disk:A/NAME.T (start: the catalog's start field); false with the failure reply
bool WriteOutput(EmulatorContext* context, const std::string& output, uint16_t start, const std::vector<uint8_t>& bytes, AsmReply& failure)
{
    std::string error;
    if (DiskFileRef ref; ParseDiskFileRef(output, ref))
    {
        if (!context)
        {
            failure = Fail(AsmControlError::BadRequest, "disk files need an emulator instance");
            return false;
        }
        if (!WriteDiskFile(context, ref, start, bytes, error))
        {
            const bool refused = error.find("write-protected") != std::string::npos || error.find("free sectors") != std::string::npos ||
                                 error.find("catalog is full") != std::string::npos;
            failure = Fail(refused ? AsmControlError::Refused : AsmControlError::NotFound, error);
            return false;
        }
        return true;
    }
    if (!WriteHostFile(output, bytes))
    {
        failure = Fail(AsmControlError::NotFound, "cannot write " + output);
        return false;
    }
    return true;
}

const ISourceCodec* CodecFor(const Source& source, const std::string& name, DetectResult& detected)
{
    const CodecRegistry& registry = CodecRegistry::Builtin();
    if (!name.empty())
        return registry.Find(name);
    detected = registry.Detect(source.bytes, source.hints);
    return detected.chosen;
}

bool ParseCodePageOption(const AsmRequest& request, std::optional<encoding::CodePage>& out, AsmReply& failure)
{
    if (!request.options.count("codepage"))
        return true;
    encoding::CodePage page;
    if (!encoding::ParseCodePage(Option(request, "codepage"), page))
    {
        failure = Fail(AsmControlError::BadRequest, "unknown code page '" + Option(request, "codepage") + "' (cp866, koi8-r, cp1251, zx, utf-8, ...)");
        return false;
    }
    out = page;
    return true;
}
}  // namespace

/// region <Construction and dispatch>

AsmControl::AsmControl(EmulatorContext* context) : _context(context) {}

const std::vector<std::string>& AsmControl::Verbs()
{
    static const std::vector<std::string> verbs = {"formats", "dialects", "files", "detect", "decode", "encode", "convert"};
    return verbs;
}

const std::vector<std::string>& AsmControl::OptionsFor(const std::string& verb)
{
    static const std::map<std::string, std::vector<std::string>> options = {
        {"formats", {}},
        {"dialects", {}},
        {"files", {"drive"}},
        {"detect", {"path", "data", "name", "file"}},
        {"decode", {"path", "data", "name", "file", "codec", "version", "codepage", "output"}},
        {"encode", {"text", "input", "codec", "version", "codepage", "lineend", "output", "start"}},
        {"convert", {"path", "data", "name", "file", "to", "codec", "version", "from", "z80n", "output"}},
    };
    static const std::vector<std::string> none;
    const auto it = options.find(verb);
    return it != options.end() ? it->second : none;
}

AsmReply AsmControl::Execute(const AsmRequest& request)
{
    std::string verb = Lower(request.verb);
    const auto& verbs = Verbs();
    if (std::find(verbs.begin(), verbs.end(), verb) == verbs.end())
    {
        std::string known;
        for (const std::string& v : verbs)
            known += (known.empty() ? "" : ", ") + v;
        return Fail(AsmControlError::BadRequest, "unknown assembler-source verb '" + request.verb + "' (verbs: " + known + ")");
    }
    const auto& allowed = OptionsFor(verb);
    for (const auto& [name, value] : request.options)
    {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
        {
            std::string list;
            for (const std::string& o : allowed)
                list += (list.empty() ? "" : ", ") + o;
            return Fail(AsmControlError::BadRequest,
                        "'" + verb + "' has no option '" + name + "'" + (list.empty() ? std::string(" (it takes none)") : " (options: " + list + ")"));
        }
    }
    AsmRequest normalized = request;
    normalized.verb = verb;
    if (verb == "formats")
        return Formats();
    if (verb == "dialects")
        return Dialects();
    if (verb == "files")
        return Files(normalized);
    if (verb == "detect")
        return Detect(normalized);
    if (verb == "decode")
        return Decode(normalized);
    if (verb == "encode")
        return Encode(normalized);
    return Convert(normalized);
}

/// endregion </Construction and dispatch>

/// region <Verbs>

AsmReply AsmControl::Formats()
{
    AsmReply reply;
    reply.body["formats"] = StateNode::Array();
    for (const auto& codec : CodecRegistry::Builtin().All())
    {
        const CodecInfo& info = codec->Info();
        StateNode item = StateNode::Object();
        item["id"] = info.id;
        item["title"] = info.title;
        item["dialect"] = info.dialect;
        item["family"] = FamilyName(info.family);
        item["versions"] = StateNode::Array();
        for (const Subversion& v : info.subversions)
        {
            StateNode version = StateNode::Object();
            version["id"] = v.id;
            version["title"] = v.title;
            item["versions"].items.push_back(std::move(version));
        }
        reply.body["formats"].items.push_back(std::move(item));
    }
    return reply;
}

AsmReply AsmControl::Dialects()
{
    AsmReply reply;
    reply.body["read"] = StateNode::Array();
    reply.body["write"] = StateNode::Array();
    for (const std::string& d : DialectRegistry::Builtin().FrontendDialects())
        reply.body["read"].items.emplace_back(d);
    for (const std::string& d : DialectRegistry::Builtin().BackendDialects())
        reply.body["write"].items.emplace_back(d);
    return reply;
}

AsmReply AsmControl::Files(const AsmRequest& request)
{
    if (!_context)
        return Fail(AsmControlError::BadRequest, "'files' needs an emulator instance");
    std::string drive = Option(request, "drive");
    if (drive.empty())
        drive = "A";
    const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(drive[0])));
    if (drive.size() != 1 || letter < 'A' || letter > 'D')
        return Fail(AsmControlError::BadRequest, "'drive' is A, B, C or D: '" + drive + "'");
    std::vector<containers::TrdosFile> files;
    std::string error;
    if (!ReadDiskFiles(_context, static_cast<uint8_t>(letter - 'A'), files, error))
        return Fail(AsmControlError::NotFound, error);
    AsmReply reply;
    reply.body["drive"] = std::string(1, letter);
    reply.body["files"] = StateNode::Array();
    const CodecRegistry& registry = CodecRegistry::Builtin();
    for (const containers::TrdosFile& f : files)
    {
        StateNode item = StateNode::Object();
        item["name"] = f.TrimmedName();
        item["type"] = std::string(1, f.type);
        item["start"] = static_cast<unsigned>(f.start);
        item["length"] = static_cast<unsigned>(f.length);
        item["sectors"] = static_cast<unsigned>(f.sectors);
        item["path"] = std::string("disk:") + letter + "/" + f.TrimmedName() + "." + f.type;
        // An empty file is no source of any format
        const DetectResult detected = f.data.empty() ? DetectResult{} : registry.Detect(f.data, f.Hints());
        item["format"] = detected.chosen ? StateNode(detected.chosen->Info().id) : StateNode();
        reply.body["files"].items.push_back(std::move(item));
    }
    return reply;
}

AsmReply AsmControl::Detect(const AsmRequest& request)
{
    Source source;
    AsmReply failure;
    if (!LoadSource(_context, request, source, failure))
        return failure;
    const DetectResult detected = CodecRegistry::Builtin().Detect(source.bytes, source.hints);
    AsmReply reply;
    reply.body["source"] = source.name;
    reply.body["candidates"] = StateNode::Array();
    for (const DetectCandidate& c : detected.candidates)
    {
        StateNode item = StateNode::Object();
        item["format"] = c.codec->Info().id;
        item["score"] = c.score;
        reply.body["candidates"].items.push_back(std::move(item));
    }
    reply.body["format"] = detected.chosen ? StateNode(detected.chosen->Info().id) : StateNode();
    reply.body["reason"] = detected.reason;
    return reply;
}

AsmReply AsmControl::Decode(const AsmRequest& request)
{
    Source source;
    AsmReply failure;
    if (!LoadSource(_context, request, source, failure))
        return failure;
    DetectResult detected;
    const std::string name = Option(request, "codec");
    const ISourceCodec* codec = CodecFor(source, name, detected);
    if (!codec)
        return name.empty() ? Fail(AsmControlError::Failed, "no format recognized: " + detected.reason + " (give 'codec'; 'formats' lists them)")
                            : Fail(AsmControlError::NotFound, "unknown format '" + name + "' ('formats' lists them)");
    DecodeOptions options;
    options.catalog = source.hints;
    options.subversion = Option(request, "version");
    if (!ParseCodePageOption(request, options.codePage, failure))
        return failure;
    const DecodeResult decoded = codec->Decode(source.bytes, options);

    StateNode body = StateNode::Object();
    body["source"] = source.name;
    body["format"] = codec->Info().id;
    body["version"] = decoded.document.subversion;
    body["versions"] = StateNode::Array();
    for (const std::string& v : decoded.subversions)
        body["versions"].items.emplace_back(v);
    body["codepage"] = std::string(encoding::CodePageName(decoded.document.codePage));
    body["lines"] = static_cast<uint64_t>(decoded.document.lines.size());
    body["diagnostics"] = DiagnosticsValue(decoded.diagnostics);
    if (!decoded.ok)
        return Fail(AsmControlError::Failed, "the bytes are no " + codec->Info().id + " source", std::move(body));
    std::string text = decoded.document.Text();
    if (!decoded.document.lines.empty())
        text.push_back('\n');
    const std::string output = Option(request, "output");
    if (!output.empty())
    {
        if (!WriteOutput(_context, output, 0, std::vector<uint8_t>(text.begin(), text.end()), failure))
            return failure;
        body["output"] = output;
    }
    else
        body["text"] = text;
    AsmReply reply;
    reply.body = std::move(body);
    return reply;
}

AsmReply AsmControl::Encode(const AsmRequest& request)
{
    const CodecRegistry& registry = CodecRegistry::Builtin();
    const std::string name = Option(request, "codec");
    if (name.empty())
        return Fail(AsmControlError::BadRequest, "'encode' needs a codec ('formats' lists them)");
    const ISourceCodec* codec = registry.Find(name);
    if (!codec)
        return Fail(AsmControlError::NotFound, "unknown format '" + name + "' ('formats' lists them)");
    const bool hasText = request.options.count("text") > 0;
    const std::string input = Option(request, "input");
    if (hasText == !input.empty())
        return Fail(AsmControlError::BadRequest, "'encode' needs text or input (a host file of UTF-8 text), not both");
    std::string text = Option(request, "text");
    if (!input.empty())
    {
        std::vector<uint8_t> bytes;
        if (!ReadHostFile(input, bytes))
            return Fail(AsmControlError::NotFound, "cannot read " + input);
        text.assign(bytes.begin(), bytes.end());
    }
    if (!text.empty() && text.back() == '\n')
        text.pop_back();
    SourceDocument document = SourceDocument::FromText(text, codec->Info().dialect);
    for (SourceLine& line : document.lines)
        if (!line.text.empty() && line.text.back() == '\r')
            line.text.pop_back();
    const std::string output = Option(request, "output");
    DiskFileRef ref;
    const bool toDisk = ParseDiskFileRef(output, ref);
    document.name = toDisk ? ref.name : std::filesystem::path(output).stem().string();
    EncodeOptions options;
    options.subversion = Option(request, "version");
    AsmReply failure;
    if (!ParseCodePageOption(request, options.codePage, failure))
        return failure;
    if (request.options.count("lineend"))
    {
        const std::string end = Lower(Option(request, "lineend"));
        if (end != "lf" && end != "crlf" && end != "cr")
            return Fail(AsmControlError::BadRequest, "'lineend' is lf, crlf or cr: '" + end + "'");
        options.lineEnd = end == "crlf" ? encoding::LineEnd::CrLf : end == "cr" ? encoding::LineEnd::Cr : encoding::LineEnd::Lf;
    }
    const EncodeResult encoded = codec->Encode(document, options);
    StateNode body = StateNode::Object();
    body["format"] = codec->Info().id;
    const std::string version = !options.subversion.empty() ? options.subversion
                                : codec->Info().subversions.empty() ? std::string() : codec->Info().subversions.back().id;
    body["version"] = version;
    body["lines"] = static_cast<uint64_t>(document.lines.size());
    body["bytes"] = static_cast<uint64_t>(encoded.bytes.size());
    body["diagnostics"] = DiagnosticsValue(encoded.diagnostics);
    if (!encoded.ok)
        return Fail(AsmControlError::Failed, "the text does not encode as " + codec->Info().id, std::move(body));
    if (!output.empty())
    {
        int64_t start = toDisk ? CatalogStart(codec->Info().id, version, ref.type) : 0;
        if (request.options.count("start"))
        {
            char* end = nullptr;
            const std::string value = Option(request, "start");
            start = std::strtoll(value.c_str(), &end, 0);
            if (!end || *end || start < 0 || start > 0xFFFF)
                return Fail(AsmControlError::BadRequest, "'start' is no number 0-65535: '" + value + "'");
        }
        if (!WriteOutput(_context, output, static_cast<uint16_t>(start), encoded.bytes, failure))
            return failure;
        body["output"] = output;
        if (toDisk)
            body["start"] = static_cast<unsigned>(start);
    }
    else
        body["data"] = base64::Encode(encoded.bytes);
    AsmReply reply;
    reply.body = std::move(body);
    return reply;
}

AsmReply AsmControl::Convert(const AsmRequest& request)
{
    const std::string to = Option(request, "to");
    if (to.empty())
        return Fail(AsmControlError::BadRequest, "'convert' needs 'to' (a dialect; 'dialects' lists them)");
    if (!DialectRegistry::Builtin().Backend(to))
        return Fail(AsmControlError::NotFound, "no dialect '" + to + "' to write ('dialects' lists them)");
    Source source;
    AsmReply failure;
    if (!LoadSource(_context, request, source, failure))
        return failure;
    DetectResult detected;
    const std::string name = Option(request, "codec");
    const ISourceCodec* codec = CodecFor(source, name, detected);
    if (!codec)
        return name.empty() ? Fail(AsmControlError::Failed, "no format recognized: " + detected.reason + " (give 'codec')")
                            : Fail(AsmControlError::NotFound, "unknown format '" + name + "'");
    DecodeOptions options;
    options.catalog = source.hints;
    options.subversion = Option(request, "version");
    DecodeResult decoded = codec->Decode(source.bytes, options);
    if (!Option(request, "from").empty())
        decoded.document.dialect = Option(request, "from");
    BackendOptions backend;
    const std::string z80n = Lower(Option(request, "z80n"));
    backend.z80n = request.options.count("z80n") && (z80n.empty() || z80n == "true" || z80n == "1" || z80n == "on");
    const ConvertResult converted = unrealasm::Convert(decoded.document, to, backend);

    StateNode body = StateNode::Object();
    body["source"] = source.name;
    body["format"] = codec->Info().id;
    body["version"] = decoded.document.subversion;
    body["from"] = decoded.document.dialect;
    body["to"] = to;
    body["lines"] = static_cast<uint64_t>(converted.document.lines.size());
    Diagnostics diagnostics = decoded.diagnostics;
    diagnostics.insert(diagnostics.end(), converted.diagnostics.begin(), converted.diagnostics.end());
    body["diagnostics"] = DiagnosticsValue(diagnostics);
    if (!converted.ok)
        return Fail(AsmControlError::Failed, "the conversion from " + decoded.document.dialect + " to " + to + " failed", std::move(body));
    // Written with the target's text codec when there is one (sjasmplus keeps the Spectrum code page for strings)
    std::vector<uint8_t> out;
    const CodecRegistry& registry = CodecRegistry::Builtin();
    if (const ISourceCodec* target = registry.Find(to) ? registry.Find(to) : registry.Find("text"))
        out = target->Encode(converted.document, {}).bytes;
    const std::string output = Option(request, "output");
    if (!output.empty())
    {
        if (!WriteOutput(_context, output, 0, out, failure))
            return failure;
        body["output"] = output;
    }
    else
    {
        std::string text = converted.document.Text();
        if (!converted.document.lines.empty())
            text.push_back('\n');
        body["text"] = text;
    }
    AsmReply reply;
    reply.body = std::move(body);
    return reply;
}

/// endregion </Verbs>
