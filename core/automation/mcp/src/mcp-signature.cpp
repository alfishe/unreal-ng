// MCP tool: signature_info
//
// Actions and their WebAPI mappings (core/swsignature/src/signaturecontrol.h implements them):
//   file    → GET /api/v1/signature?path=&details=&external=
//   compare → GET /api/v1/signature/compare?path=&with=
//   media   → GET /signature?details=   (the machine's tape and floppy slots)
//
// Drogon-free; all calls go through the loopback IApiCaller. The text carries every signature (an agent may see only
// the text of an answer).

#include "mcp-signature.h"

#include <sstream>

#include "mcp-tool-utils.h"

namespace mcp
{

std::string SignatureText(const Json::Value& signature)
{
    std::ostringstream out;
    out << signature["name"].asString() << ": " << signature["kind"].asString() << " " << signature["format"].asString()
        << ", " << signature["size"].asUInt64() << " bytes\nsha256 " << signature["sha256"].asString();
    for (const char* key : {"sha1", "md5", "machine_hint"})
        if (signature.isMember(key))
            out << "\n" << key << " " << signature[key].asString();
    out << "\ncontent " << (signature["content"].asString().empty() ? "none" : signature["content"].asString());
    out << "\nkeys (a game profile's match.signatures):";
    for (const Json::Value& key : signature["keys"])
        out << " " << key.asString();
    if (!signature["file_set"].empty())
    {
        out << "\nfile set (match.file_set):";
        for (const Json::Value& token : signature["file_set"])
            out << " " << token.asString();
    }
    if (signature.isMember("memory_pages"))
        out << "\nmemory pages " << signature["memory_pages"].asUInt64();
    for (const Json::Value& file : signature["files"])
        out << "\n  " << file["name"].asString() << "." << file["type"].asString() << " start " << file["start"].asUInt()
            << " length " << file["length"].asUInt() << " " << file["token"].asString();
    for (const Json::Value& block : signature["blocks"])
        out << "\n  block " << block["index"].asUInt64() << " flag " << block["flag"].asUInt() << " "
            << block["length"].asUInt64() << " bytes " << block["sha256"].asString();
    for (const Json::Value& warning : signature["warnings"])
        out << "\nwarning: " << warning.asString();
    return out.str();
}

namespace
{

std::string Query(const Json::Value& args, std::initializer_list<const char*> names)
{
    std::string query;
    for (const char* name : names)
    {
        if (!args.isMember(name) || args[name].isNull())
            continue;
        const std::string value = args[name].isBool() ? (args[name].asBool() ? "true" : "false") : args[name].asString();
        query += (query.empty() ? "?" : "&") + std::string(name) + "=" + UrlEncodeSegment(value);
    }
    return query;
}

/// The reply as text: every signature, or the comparison
ToolCallback Described(const std::string& action, ToolCallback done)
{
    return [action, done](ToolResult result) {
        if (result.isError || !result.structured.isObject())
        {
            done(std::move(result));
            return;
        }
        const Json::Value& body = result.structured;
        std::ostringstream out;
        if (action == "file")
            out << SignatureText(body["signature"]);
        else if (action == "compare")
            out << body["match"]["level"].asString() << " (score " << body["match"]["score"].asDouble()
                << "): " << body["match"]["reason"].asString();
        else
        {
            for (const Json::Value& item : body["media"])
            {
                out << item["slot"].asString() << ": " << item["source"].asString()
                    << (item["dirty"].asBool() ? " (written since inserted)" : "") << "\n";
                out << (item.isMember("signature") ? SignatureText(item["signature"]) : item["error"].asString())
                    << "\n";
            }
            if (body["media"].empty())
                out << "no tape or disk loaded";
        }
        result.text = out.str();
        done(std::move(result));
    };
}

void RegisterSignatureInfoImpl(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    schema["properties"]["action"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* action : {"file", "media", "compare"})
        schema["properties"]["action"]["enum"].append(action);
    schema["properties"]["action"]["description"] =
        "file: the signatures of a host file; media: of the tapes and disks loaded in the machine (their source "
        "files); compare: a known title (path) against a candidate (with)";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["target"]["description"] = "media: emulator id, or 'auto'";
    schema["properties"]["path"]["type"] = "string";
    schema["properties"]["path"]["description"] = "file / compare: a host file (TAP, TZX, TRD, SCL, FDI, UDI, TD0, SNA, Z80, SZX, any)";
    schema["properties"]["with"]["type"] = "string";
    schema["properties"]["with"]["description"] = "compare: the candidate file";
    schema["properties"]["details"]["type"] = "boolean";
    schema["properties"]["details"]["description"] = "file / media: the tape blocks and disk files with their hashes";
    schema["properties"]["external"]["type"] = "boolean";
    schema["properties"]["external"]["description"] = "file / media: SHA-1 and MD5 too (catalogue lookups)";
    schema["required"].append("action");

    registry.Register(
        "signature_info",
        "Media signatures that recognise a ZX Spectrum title after it was re-packed, renamed or saved to: the exact "
        "SHA-256 (file:sha256:), the tape content over the data payloads (tape:sha256:, the same for TAP and TZX), the "
        "TR-DOS file set (trdset:sha256:, the same for TRD, SCL, FDI, UDI, TD0; file_set tokens for the fuzzy rule: "
        "Jaccard >= 0.8 and no code file missing) and snapshot memory (mem:sha256:, >= 90 % equal RAM pages). keys is "
        "what a game profile's match.signatures holds; the controllers tool's match action gives the profile they "
        "choose.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string action = args["action"].asString();
            if (action == "file")
                ForwardCall("GET", "/api/v1/signature" + Query(args, {"path", "details", "external"}), nullptr, caller,
                            "Signatures", Described(action, done));
            else if (action == "compare")
                ForwardCall("GET", "/api/v1/signature/compare" + Query(args, {"path", "with"}), nullptr, caller,
                            "Comparison", Described(action, done));
            else if (action == "media")
                ResolveAndForward(args, "GET", "/signature" + Query(args, {"details", "external"}), nullptr, caller,
                                  "Media signatures", Described(action, done));
            else
                done(ToolResult::Error("signature_info: unknown action '" + action + "' (file, media, compare)"));
        });
}

}  // namespace

void RegisterSignatureInfo(ToolRegistry& registry)
{
    RegisterSignatureInfoImpl(registry);
}

} // namespace mcp
