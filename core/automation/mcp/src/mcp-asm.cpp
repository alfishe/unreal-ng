// MCP tool: asm_source
//
// Actions and their WebAPI mappings (core/src/debugger/asm/asmcontrol.h implements them):
//   formats  → GET  /api/v1/asm/formats
//   dialects → GET  /api/v1/asm/dialects
//   files    → GET  /asm/files?drive=
//   detect   → POST /asm/detect  {path | data + name, file?}
//   decode   → POST /asm/decode  {path | data + name, file?, codec?, version?, codepage?, output?}
//   encode   → POST /asm/encode  {text | input, codec, version?, codepage?, lineend?, output?, start?}
//   convert  → POST /asm/convert {path | data + name, file?, to, codec?, version?, from?, z80n?, output?}
//   sync_status  → GET  /asm/sync?assembler=        the assembler running in the machine and its text (asm-synchronizer)
//   sync_probe   → POST /asm/sync/probe
//   sync_extract → POST /asm/sync/extract {assembler?, as?, to?, codepage?, output?}
//
// Drogon-free; all calls go through the loopback IApiCaller.

#include "mcp-asm.h"

#include "mcp-tool-utils.h"

namespace mcp
{

namespace
{

void RegisterAsmSourceImpl(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    schema["properties"]["action"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* action : {"formats", "dialects", "files", "detect", "decode", "encode", "convert", "sync_status", "sync_probe", "sync_extract"})
        schema["properties"]["action"]["enum"].append(action);
    schema["properties"]["action"]["description"] =
        "Assembler source operation: the tokenized formats of ZX Spectrum assemblers (ALASM, TASM, ZX-ASM, STORM, MASM, GENS, ZEUS, "
        "XAS, Laser Genius, ...) and text dialects; decode to text, encode text, convert between dialects (to sjasmplus, pasmo, z88dk). sync_*: the source an "
        "assembler running in the machine holds in RAM (ALASM 5.09 / 4.44, TASM 4.12), read as its own SAVE would write it.";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    const auto text = [&](const char* name, const char* description) {
        schema["properties"][name]["type"] = "string";
        schema["properties"][name]["description"] = description;
    };
    text("path", "detect / decode / convert: a host file (a .trd / .tap / .tzx image with 'file'; a hobeta $X file) or disk:A/NAME.T");
    text("data", "detect / decode / convert: the file as base64 instead of a path");
    text("name", "with data: the file name (its extension helps detection)");
    text("file", "NAME.T inside an image given as path");
    text("codec", "a format id (formats); decode / convert: default detected; encode: required");
    text("version", "a version of the format (formats lists them); default detected / the newest");
    text("codepage", "text formats: cp866, koi8-r, cp1251, zx, utf-8 ...");
    text("lineend", "encode of a text format: lf, crlf, cr");
    text("text", "encode: the source text (UTF-8)");
    text("input", "encode: a host file of UTF-8 text instead of text");
    text("output", "a host file or disk:A/NAME.T to write to; without it the reply carries the text / the bytes as base64");
    text("to", "convert: the target dialect (dialects)");
    text("from", "convert: the source dialect when the codec does not tell (the text codec)");
    text("drive", "files: A-D (default A)");
    text("assembler", "sync_*: the assembler when two identify (alasm-5.09, alasm-4.44, tasm-4.12)");
    text("as", "sync_extract: text (default), file (the assembler's own format) or dialect (with to)");
    schema["properties"]["start"]["type"] = "integer";
    schema["properties"]["start"]["description"] = "encode to a disk: the catalog start field (default: what the format needs)";
    schema["properties"]["z80n"]["type"] = "boolean";
    schema["properties"]["z80n"]["description"] = "convert: the ZX Spectrum Next instructions";
    schema["required"].append("action");

    registry.Register(
        "asm_source",
        "Sources of ZX Spectrum assemblers: list formats and dialects, the files of a disk in a drive (with their formats), detect a "
        "file's format, decode it to text, encode text in a format (to a host file or onto the disk), convert a source to another "
        "dialect (sjasmplus, pasmo, z88dk ...).",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string action = args["action"].asString();
            if (action == "formats" || action == "dialects")
            {
                ForwardCall("GET", "/api/v1/asm/" + action, nullptr, caller, action == "formats" ? "Assembler source formats" : "Dialects", done);
                return;
            }
            if (action == "files")
            {
                const std::string drive = args.isMember("drive") ? args["drive"].asString() : std::string();
                ResolveAndForward(args, "GET", "/asm/files" + (drive.empty() ? std::string() : "?drive=" + UrlEncodeSegment(drive)), nullptr,
                                  caller, "Disk files", done);
                return;
            }
            if (action == "detect" || action == "decode" || action == "encode" || action == "convert")
            {
                Json::Value body(Json::objectValue);
                for (const char* name : {"path", "data", "name", "file", "codec", "version", "codepage", "lineend", "text", "input", "output",
                                         "to", "from", "start", "z80n"})
                    if (args.isMember(name) && !args[name].isNull())
                        body[name] = args[name].isBool() ? Json::Value(args[name].asBool() ? "true" : "false") : Json::Value(args[name].asString());
                ResolveAndForward(args, "POST", "/asm/" + action, &body, caller, "Assembler source " + action, done);
                return;
            }
            if (action == "sync_status")
            {
                const std::string assembler = args.isMember("assembler") ? args["assembler"].asString() : std::string();
                ResolveAndForward(args, "GET", "/asm/sync" + (assembler.empty() ? std::string() : "?assembler=" + UrlEncodeSegment(assembler)),
                                  nullptr, caller, "Assembler in RAM", done);
                return;
            }
            if (action == "sync_probe" || action == "sync_extract")
            {
                Json::Value body(Json::objectValue);
                for (const char* name : {"assembler", "as", "to", "codepage", "output"})
                    if (args.isMember(name) && !args[name].isNull())
                        body[name] = args[name].asString();
                ResolveAndForward(args, "POST", "/asm/sync/" + action.substr(5), &body, caller, "Assembler in RAM: " + action.substr(5), done);
                return;
            }
            done(ToolResult::Error("Unknown action '" + action +
                                   "'. Valid: formats, dialects, files, detect, decode, encode, convert, sync_status, sync_probe, sync_extract"));
        });
}

} // namespace

void RegisterAsmSource(ToolRegistry& registry)
{
    RegisterAsmSourceImpl(registry);
}

} // namespace mcp
