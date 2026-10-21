#include "mcp-actions.h"

#include <cctype>
#include <memory>
#include <sstream>
#include <string>

#include "mcp-tool-utils.h"
#include "target-resolver.h"

namespace mcp
{

namespace
{
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

    /// One line per action: "* id [kind] label" (* = can run now), else " ", the reason after it
    std::string ListSummary(const Json::Value& reply)
    {
        std::ostringstream out;
        std::string group;
        for (const Json::Value& action : reply["actions"])
        {
            if (action["group"].asString() != group)
            {
                group = action["group"].asString();
                out << group << ":\n";
            }
            out << (action["available"].asBool() ? "* " : "  ") << action["id"].asString() << " ["
                << action["kind"].asString() << (action["held"].asBool() ? ", held" : "")
                << (action["gui"].asBool() ? ", gui" : "") << "] " << action["label"].asString();
            if (action["scope"].isString())
                out << " (scope " << action["scope"].asString() << ")";
            if (action["params"].isString())
                out << " {" << action["params"].asString() << "}";
            if (!action["available"].asBool())
                out << " - " << action["error"].asString() << ": " << action["reason"].asString();
            out << "\n";
        }
        out << "(* = can run now; instance " << (reply["instance"].isString() ? reply["instance"].asString() : "none")
            << ")";
        return out.str();
    }

    std::string RunSummary(const Json::Value& reply)
    {
        std::ostringstream out;
        out << reply["action"].asString() << ": ok";
        if (reply["message"].isString() && !reply["message"].asString().empty())
            out << " - " << reply["message"].asString();
        const Json::Value& result = reply["result"];
        if (result.isMember("pc"))
            out << "; pc " << result["pc"].asInt() << ", sp " << result["sp"].asInt();
        if (result.isMember("state"))
            out << "; state " << result["state"].asString();
        if (reply.isMember("automation_session"))
            out << "\nnote: " << reply["automation_session"].get("advice", "").asString();
        return out.str();
    }
}  // namespace

void RegisterActions(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["description"] = "Emulator id, or 'auto'";
    schema["properties"]["action"]["type"] = "string";
    schema["properties"]["action"]["enum"].append("list");
    schema["properties"]["action"]["enum"].append("run");
    schema["properties"]["id"]["type"] = "string";
    schema["properties"]["id"]["description"] =
        "run: the action id as list shows it (run.pause_toggle, run.step_in, ttd.jog_frames, mach.nmi, ...)";
    schema["properties"]["value"]["description"] =
        "run: by the action's kind - press: omit (a press) or false (a release, for held actions); delta: signed "
        "steps (a number) or {delta, rate}; level: 0..1; text: a string";
    schema["properties"]["params"]["type"] = "object";
    schema["properties"]["params"]["description"] =
        "run: the action's parameters as list shows them in {} (input.key: key; media.open: path; feature.toggle: name; "
        "mach.start: model; run.frames: count; run.to_scanline: line)";
    schema["required"].append("action");

    registry.Register(
        "actions",
        "Host actions: the ids a pad, the Surface Dial, a Stream Deck or a menu runs (run control, time travel, "
        "machine). list: every action with its kind (press, delta, level, text), the companion scope it needs, and "
        "whether it can run now or why not (needs a pause, no history, run-control held). run: one action by id on "
        "the target, with its value; it runs on the instance's actions worker, in order, and answers when done "
        "(a step answers pc, sp, state). An action that changes the machine opens the implicit run-control session "
        "like the other tools; take control first for a longer sequence.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string action = args["action"].asString();
            if (action != "list" && action != "run")
            {
                done(ToolResult::Error("actions: action is list or run"));
                return;
            }
            if (action == "run" && args["id"].asString().empty())
            {
                done(ToolResult::Error("actions run: id is required (actions list shows them)"));
                return;
            }
            auto argsCopy = std::make_shared<Json::Value>(args);
            TargetResolver::ResolveFromArgs(args, caller, [action, argsCopy, &caller, done](bool ok, const std::string& idOrError) {
                if (!ok)
                {
                    done(ToolResult::Error(idOrError));
                    return;
                }
                const std::string& id = idOrError;
                auto answer = [action, done](int status, Json::Value reply) {
                    if (status == 0)
                    {
                        done(ToolResult::Error("WebAPI unreachable — is the emulator running with WebAPI enabled (port 8090)?"));
                        return;
                    }
                    if (status < 200 || status >= 300 || !reply.get("ok", false).asBool())
                    {
                        done(ToolResult::Error("actions " + action + ": [" + reply.get("error", "error").asString() +
                                               "] " + reply.get("message", DescribeErrorBody(reply)).asString()));
                        return;
                    }
                    const std::string text = action == "list" ? ListSummary(reply) : RunSummary(reply);
                    done(ToolResult::Ok(text, std::move(reply)));
                };
                if (action == "list")
                {
                    caller.Call("GET", "/api/v1/controllers/actions?instance=" + EncodeSegment(id), nullptr, answer);
                    return;
                }
                auto body = std::make_shared<Json::Value>(Json::objectValue);
                (*body)["instance"] = id;
                if (argsCopy->isMember("value"))
                    (*body)["value"] = (*argsCopy)["value"];
                if (argsCopy->isMember("params"))
                    (*body)["params"] = (*argsCopy)["params"];
                caller.Call("POST", "/api/v1/controllers/actions/" + EncodeSegment((*argsCopy)["id"].asString()),
                            body.get(), [body, answer](int status, Json::Value reply) { answer(status, std::move(reply)); });
            });
        });
}

} // namespace mcp
