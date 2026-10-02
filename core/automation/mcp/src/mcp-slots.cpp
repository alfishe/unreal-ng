#include "mcp-slots.h"

#include <cctype>
#include <memory>
#include <set>
#include <sstream>
#include <string>

#include "mcp-tool-utils.h"
#include "target-resolver.h"

namespace mcp
{

const std::vector<std::string>& TapeExtensions()
{
    static const std::vector<std::string> extensions = {"tap", "spc", "sta", "ltp", "zxt", "tzx"};
    return extensions;
}

const std::vector<std::pair<std::string, std::vector<std::string>>>& MediaToolActions()
{
    static const std::vector<std::string> insertOptions = {"access", "format", "fs", "codepage", "free", "wp", "kind", "device",
                                                           "save", "export", "discard", "end_recording", "async", "immediate"};
    static const std::vector<std::pair<std::string, std::vector<std::string>>> actions = {
        {"list", {}},
        {"info", {}},
        {"formats", {"kind"}},
        {"targets", {}},
        {"insert", insertOptions},
        {"eject", {"save", "export", "discard", "end_recording", "async"}},
        {"swap", insertOptions},
        {"save", {"retarget"}},
        {"export", {}},
        {"discard", {"async"}},
        {"rescan", {"async"}},
        {"create", {"format", "cylinders", "sides", "size", "save", "export", "discard", "end_recording", "async"}},
        {"protect", {"on"}},
    };
    return actions;
}

namespace
{
    /// Percent-encode a selector for the URL path ("tag:sd+neogs", "b:")
    std::string EncodeSegment(const std::string& text)
    {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        for (unsigned char c : text)
        {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                out.push_back(static_cast<char>(c));
            else
            {
                out.push_back('%');
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 0x0F]);
            }
        }
        return out;
    }

    std::string Summary(const std::string& action, const Json::Value& reply)
    {
        std::ostringstream out;
        if (action == "list")
        {
            for (const Json::Value& slot : reply["slots"])
            {
                out << slot["id"].asString();
                if (slot["aliases"].size() > 0)
                    out << " (" << slot["aliases"][0].asString() << ")";
                out << ": " << slot["state"].asString();
                if (slot["medium"].isObject())
                {
                    out << " " << slot["medium"]["source"].asString() << " [" << slot["medium"]["format"].asString() << ", "
                        << slot["medium"]["access"].asString();
                    if (slot["medium"]["dirty"].asBool())
                        out << ", unsaved " << slot["medium"]["changes"].asString();
                    out << "]";
                }
                out << "\n";
            }
            for (const Json::Value& slot : reply["detached"])
                out << "detached " << slot["id"].asString() << ": " << slot["medium"]["source"].asString() << "\n";
            out << "revision " << reply["revision"].asUInt64();
            return out.str();
        }
        if (action == "info")
        {
            const Json::Value& slot = reply["info"];
            out << slot["id"].asString() << ": " << slot["state"].asString();
            if (slot["medium"].isObject())
                out << " " << slot["medium"]["source"].asString() << " [" << slot["medium"]["format"].asString() << "]";
            return out.str();
        }
        if (action == "formats")
            return "Accepted formats per kind (structuredContent.formats)";
        if (action == "targets")
        {
            const Json::Value& file = reply["file"];
            out << "file: ";
            if (file["kinds"].empty())
                out << "unknown";
            for (Json::ArrayIndex i = 0; i < file["kinds"].size(); i++)
                out << (i ? " " : "") << file["kinds"][i].asString();
            if (!file["format"].asString().empty())
                out << " (" << file["format"].asString() << ")";
            if (file["evidence"].size() > 0)
                out << " - " << file["evidence"][0].asString();
            if (reply["refusal"].isString())
                out << "\nrefused: " << reply["refusal"].asString();
            const int chosen = reply["default"].isInt() ? reply["default"].asInt() : -1;
            for (Json::ArrayIndex i = 0; i < reply["targets"].size(); i++)
            {
                const Json::Value& target = reply["targets"][i];
                out << "\n" << (static_cast<int>(i) == chosen ? "* " : "  ")
                    << (target["slot"].isString() ? target["slot"].asString() : target["action"].asString()) << " - "
                    << target["label"].asString();
                if (target["occupiedBy"].isString())
                    out << ", replaces " << target["occupiedBy"].asString();
                if (target["dirty"].asBool())
                    out << " (unsaved writes!)";
                if (target["autostart"].asBool())
                    out << ", autostart";
            }
            if (chosen < 0 && reply["targets"].size() > 1)
                out << "\nseveral targets: ask the user, then insert with the chosen slot";
            return out.str();
        }
        out << action << " " << reply["slot"].asString() << ": ok";
        if (reply["pending"].asBool())
            out << " (pending: applied at the next frame boundary)";
        for (const Json::Value& line : reply["report"])
            out << "\nnote: " << line.asString();
        return out.str();
    }
}  // namespace

void RegisterMediaSlots(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["description"] = "Emulator id, or 'auto'";
    schema["properties"]["action"]["type"] = "string";
    for (const auto& [verb, verbOptions] : MediaToolActions())
        schema["properties"]["action"]["enum"].append(verb);
    schema["properties"]["slot"]["type"] = "string";
    schema["properties"]["slot"]["description"] =
        "Slot selector: id (fdd.b, sd.zc), alias (A, B, b:, sd, tape, hd), kind:index (floppy:1), tag query "
        "(tag:sd+neogs); 'auto' for insert (chosen from the file's content)";
    schema["properties"]["path"]["type"] = "string";
    schema["properties"]["path"]["description"] =
        "insert / swap: a file or a folder on the emulator host; save / export: the target file; targets: the file "
        "or folder to place";

    // Every option any verb takes, typed; MediaControl checks which verb takes which
    const std::set<std::string> booleans = {"save", "discard", "wp", "on", "retarget", "end_recording", "async", "immediate"};
    const std::set<std::string> integers = {"free", "cylinders", "sides", "size"};
    std::set<std::string> options;
    std::string perVerb;
    for (const auto& [verb, verbOptions] : MediaToolActions())
    {
        std::string list;
        for (const std::string& option : verbOptions)
        {
            options.insert(option);
            list += (list.empty() ? "" : ", ") + option;
        }
        perVerb += "\n" + verb + ": " + (list.empty() ? "-" : list);
    }
    for (const std::string& option : options)
        schema["properties"][option]["type"] = booleans.count(option) ? "boolean" : integers.count(option) ? "integer" : "string";
    schema["properties"]["access"]["enum"].append("readonly");
    schema["properties"]["access"]["enum"].append("session");
    schema["properties"]["access"]["enum"].append("writethrough");
    schema["properties"]["export"]["description"] = "Disposition: write a dirty medium to this new file before it leaves";
    schema["properties"]["kind"]["description"] = "formats: filter; insert auto: the kind a folder becomes (floppy, block)";
    schema["properties"]["device"]["type"] = "string";
    schema["properties"]["device"]["description"] =
        "insert / swap on an IDE unit: disk or cdrom - swap the unit's drive first (the unit must be empty)";
    schema["required"].append("action");

    registry.Register(
        "media",
        "The machine's media slots (floppy drives, tape, IDE hard disks and CD-ROM, SD cards). Actions: list, info, "
        "formats, targets (where a file can go: what it is, the slots that take it in order, the default, or why "
        "nothing does), insert, swap, eject, save, export, discard, rescan, create, protect. Operations are synchronous (the reply "
        "comes when the medium is in or out; async:true returns at once). A dirty medium leaves its slot only with "
        "save:true, export:'<path>' or discard:true. The reply's 'revision' increases with every change. "
        "Options per action:" + perVerb,
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string action = args["action"].asString();
            const std::string slot = args.isMember("slot") ? args["slot"].asString() : "";

            auto body = std::make_shared<Json::Value>(Json::objectValue);
            for (const std::string& name : args.getMemberNames())
            {
                if (name != "action" && name != "slot" && name != "target")
                    (*body)[name] = args[name];
            }

            TargetResolver::ResolveFromArgs(args, caller, [action, slot, body, &caller, done](bool ok, const std::string& idOrError) {
                if (!ok)
                {
                    done(ToolResult::Error(idOrError));
                    return;
                }
                const std::string& id = idOrError;
                std::string method = "POST";
                std::string path;
                const Json::Value* payload = body.get();
                if (action == "list")
                {
                    method = "GET";
                    path = Endpoint(id, "/media");
                    payload = nullptr;
                }
                else if (action == "formats")
                {
                    method = "GET";
                    path = Endpoint(id, "/media/formats");
                    if (body->isMember("kind"))
                        path += "?kind=" + EncodeSegment((*body)["kind"].asString());
                    payload = nullptr;
                }
                else if (action == "targets")
                {
                    method = "GET";
                    path = Endpoint(id, "/media/targets?path=" + EncodeSegment((*body).get("path", "").asString()));
                    payload = nullptr;
                }
                else if (action == "info")
                {
                    method = "GET";
                    path = Endpoint(id, "/media/" + EncodeSegment(slot));
                    payload = nullptr;
                }
                else
                {
                    path = Endpoint(id, "/media/" + EncodeSegment(slot) + "/" + action);
                }

                caller.Call(method, path, payload, [action, body, done](int status, Json::Value reply) {
                    if (status == 0)
                    {
                        done(ToolResult::Error("WebAPI unreachable — is the emulator running with WebAPI enabled (port 8090)?"));
                        return;
                    }
                    if (status < 200 || status >= 300 || !reply.get("ok", false).asBool())
                    {
                        done(ToolResult::Error("media " + action + ": [" + reply.get("error", "error").asString() + "] " +
                                               reply.get("message", DescribeErrorBody(reply)).asString()));
                        return;
                    }
                    done(ToolResult::Ok(Summary(action, reply), std::move(reply)));
                });
            });
        });
}

} // namespace mcp
