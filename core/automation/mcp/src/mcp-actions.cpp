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

    std::string ContextSummary(const Json::Value& reply)
    {
        std::ostringstream out;
        out << "context " << reply["context"].asString();
        if (reply["set"].asBool())
            out << " (set by " << reply["by"].asString() << ")";
        else
            out << " (the default: ttd with a time travel history, else play)";
        out << " on instance " << reply["instance"].asString() << "; set it with actions run ctx.set";
        return out.str();
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
        // ttd.seek_by: where it went from and to, and the marks crossed (a dial's feedback)
        if (result.isMember("marks") && result.isMember("from"))
        {
            out << "; frame " << result["from"].asUInt64() << " -> " << result["frame"].asUInt64();
            if (result.isMember("arrived_at"))
                out << " (arrived " << result["arrived_at"].get("frame", 0).asUInt64() << ")";
            out << "; marks:";
            if (result["marks"].empty())
                out << " none";
            for (const Json::Value& mark : result["marks"])
                out << " " << mark["kind"].asString() << "@" << mark["frame"].asUInt64();
        }
        // run.step_by: the steps done, and what stopped them
        if (result.isMember("steps") && result.isMember("direction"))
        {
            out << "; " << result["steps"].asInt() << " step(s) " << result["direction"].asString();
            if (result.isMember("stopped_by"))
                out << ", stopped by " << result["stopped_by"].asString();
        }
        // ctx.set: the context in force now
        if (result.isMember("context") && result.isMember("default"))
            out << "; context " << result["context"].asString() << " (was " << result["previous"].asString() << ")";
        if (reply.isMember("automation_session"))
            out << "\nnote: " << reply["automation_session"].get("advice", "").asString();
        return out.str();
    }
    std::string Str(const Json::Value& value)
    {
        if (value.isNull())
            return "-";
        if (value.isString())
            return value.asString();
        if (value.isIntegral())
            return std::to_string(value.asInt64());
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        return Json::writeString(writer, value);
    }

    /// "#1 PENTAGON 128K (<id>) in v1 Main window, follow [view]"
    std::string TargetLine(const Json::Value& target)
    {
        if (!target["instance"].isString())
            return "no machine (" + Str(target["pin"]) + ")";
        std::string line = "#" + Str(target["index"]) + " " + Str(target["model"]) + " " + Str(target["ram_kb"]) +
                           "K (" + target["instance"].asString() + ")";
        if (target["view"].isObject())
            line += " in " + Str(target["view"]["id"]) + " " + Str(target["view"]["name"]);
        return line + ", " + Str(target["pin"]) + " [" + Str(target["source"]) + "]";
    }

    /// The text of a surface verb's reply (an agent may see only this)
    std::string SurfaceSummary(const std::string& verb, const Json::Value& reply)
    {
        std::ostringstream out;
        if (verb == "views")
        {
            out << "followed: " << TargetLine(reply["followed"]) << " (what a WebAPI call without an id acts on)";
            if (reply["views"].empty())
                out << "\nno views (headless: the selected instance, else the sole one)";
            for (const Json::Value& view : reply["views"])
                out << "\n" << Str(view["position"]) << ". " << Str(view["id"]) << " " << Str(view["name"]) << " ["
                    << Str(view["owner"]) << "] active " << Str(view["active"]);
            return out.str();
        }
        if (verb == "sessions")
        {
            if (reply["sessions"].empty())
                return "no surface sessions";
            for (const Json::Value& session : reply["sessions"])
            {
                out << Str(session["id"]) << " " << Str(session["name"]) << " [" << Str(session["kind"]) << "] -> "
                    << TargetLine(session["target"]);
                if (session.isMember("notice"))
                    out << " (" << Str(session["notice"]) << ")";
                out << "\n";
            }
            std::string text = out.str();
            return text.substr(0, text.size() - 1);
        }
        if (verb == "target")
            return "target: " + TargetLine(reply["target"]);
        if (verb == "pin" || verb == "session_open" || verb == "session")
            return Str(reply["session"]["id"]) + " " + Str(reply["session"]["name"]) + " -> " +
                   TargetLine(reply["session"]["target"]);
        if (verb == "view_add" || verb == "view_update")
            return "view " + Str(reply["view"]["id"]) + " " + Str(reply["view"]["name"]) + " active " +
                   Str(reply["view"]["active"]) + "; followed: " + TargetLine(reply["followed"]);
        if (verb == "view_remove")
            return "removed " + Str(reply["removed"]) + "; followed: " + TargetLine(reply["followed"]);
        if (verb == "session_close")
            return "closed " + Str(reply["closed"]);
        if (verb == "readouts")
        {
            out << "readouts of " << Str(reply["instance"]) << ":";
            for (const std::string& name : reply["values"].getMemberNames())
                out << "\n" << name << " = " << Str(reply["values"][name]);
            if (!reply["unknown"].empty())
                out << "\nunknown: " << Str(reply["unknown"]);
            return out.str();
        }
        if (verb == "activity")
        {
            out << reply["entries"].size() << " entr" << (reply["entries"].size() == 1 ? "y" : "ies") << ", last "
                << Str(reply["last"]) << " (pass it as since)";
            if (reply["dropped"].asInt64() > 0)
                out << ", dropped " << Str(reply["dropped"]);
            for (const Json::Value& entry : reply["entries"])
                out << "\n" << Str(entry["seq"]) << " " << Str(entry["id"]) << " " << Str(entry["value"]) << " by "
                    << Str(entry["origin"]) << " (" << Str(entry["surface"]) << ") " << (entry["ok"].asBool() ? "ok" : Str(entry["error"]));
            return out.str();
        }
        return Str(reply);
    }

    /// The REST call of a surface verb: method, path, body
    bool SurfaceRoute(const std::string& verb, const Json::Value& args, std::string& method, std::string& path,
                      Json::Value& body, std::string& error)
    {
        const std::string session = EncodeSegment(args["session"].asString());
        const std::string view = EncodeSegment(args["view"].asString());
        auto query = [&args](std::initializer_list<const char*> names) {
            std::string out;
            for (const char* name : names)
            {
                const Json::Value& value = args[name];
                if (value.isNull())
                    continue;
                std::string text;
                if (value.isArray())
                    for (const Json::Value& item : value)
                        text += (text.empty() ? "" : ",") + item.asString();
                else
                    text = value.asString();
                out += std::string(out.empty() ? "?" : "&") + (std::string(name) == "target" ? "instance" : name) +
                       "=" + EncodeSegment(text);
            }
            return out;
        };
        body = Json::Value(Json::objectValue);
        method = "GET";
        if (verb == "views")
            path = "/api/v1/surfaces/views";
        else if (verb == "target")
            path = "/api/v1/surfaces/target" + query({"session"});
        else if (verb == "sessions")
            path = "/api/v1/surfaces/sessions";
        else if (verb == "readouts")
            path = "/api/v1/surfaces/readouts" + query({"target", "names"});
        else if (verb == "activity")
            path = "/api/v1/surfaces/activity" + query({"target", "since", "limit"});
        else if (verb == "session" || verb == "session_close" || verb == "pin")
        {
            if (session.empty())
            {
                error = "actions " + verb + ": session is required (actions sessions lists them)";
                return false;
            }
            method = verb == "session" ? "GET" : verb == "pin" ? "PUT" : "DELETE";
            path = "/api/v1/surfaces/sessions/" + session + (verb == "pin" ? "/pin" : "");
            if (verb == "pin")
                body["target"] = args.isMember("pin") ? args["pin"] : Json::Value("follow");
        }
        else if (verb == "session_open")
        {
            method = "POST";
            path = "/api/v1/surfaces/sessions";
            for (const char* name : {"name", "kind", "serial"})
                if (args.isMember(name))
                    body[name] = args[name];
            if (args.isMember("pin"))
                body["target"] = args["pin"];
        }
        else if (verb == "view_add")
        {
            method = "POST";
            path = "/api/v1/surfaces/views";
            for (const char* name : {"name", "active", "activate"})
                if (args.isMember(name))
                    body[name] = args[name];
        }
        else if (verb == "view_update" || verb == "view_remove")
        {
            if (view.empty())
            {
                error = "actions " + verb + ": view is required (actions views lists them)";
                return false;
            }
            method = verb == "view_update" ? "PUT" : "DELETE";
            path = "/api/v1/surfaces/views/" + view;
            for (const char* name : {"active", "activate"})
                if (args.isMember(name))
                    body[name] = args[name];
        }
        else
            return false;
        return true;
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
    schema["properties"]["action"]["enum"].append("context");
    for (const char* verb : {"views", "target", "sessions", "session", "pin", "readouts", "activity", "view_add",
                             "view_update", "view_remove", "session_open", "session_close"})
        schema["properties"]["action"]["enum"].append(verb);
    schema["properties"]["session"]["type"] = "string";
    schema["properties"]["session"]["description"] = "target / session / pin / session_close: a surface session id (s1)";
    schema["properties"]["pin"]["type"] = "string";
    schema["properties"]["pin"]["description"] =
        "pin, session_open: follow (the focused window's instance), view:<id> (that window's), instance:<id|index>";
    schema["properties"]["view"]["type"] = "string";
    schema["properties"]["view"]["description"] = "view_update / view_remove: a view id (v1)";
    schema["properties"]["name"]["type"] = "string";
    schema["properties"]["name"]["description"] = "view_add / session_open: its name";
    schema["properties"]["active"]["type"] = "string";
    schema["properties"]["active"]["description"] = "view_add / view_update: the view's active instance (id or index)";
    schema["properties"]["activate"]["type"] = "boolean";
    schema["properties"]["activate"]["description"] = "view_add / view_update: move the view to the front of the focus order";
    schema["properties"]["names"]["description"] =
        "readouts: names or prefixes (run.state, ttd.position, machine, media.*, audio.mute, debug.mode, toggle.*); "
        "default all";
    schema["properties"]["since"]["type"] = "integer";
    schema["properties"]["since"]["description"] = "activity: entries after this seq (the previous answer's last)";
    schema["properties"]["limit"]["type"] = "integer";
    schema["properties"]["id"]["type"] = "string";
    schema["properties"]["id"]["description"] =
        "run: the action id as list shows it (run.pause_toggle, run.step_in, run.step_by, ttd.seek_by, ttd.jog_frames, "
        "ctx.set, mach.nmi, ...)";
    schema["properties"]["value"]["description"] =
        "run: by the action's kind - press: omit (a press) or false (a release, for held actions); delta: signed "
        "steps (a number) or {delta, rate}; level: 0..1; text: a string";
    schema["properties"]["params"]["type"] = "object";
    schema["properties"]["params"]["description"] =
        "run: the action's parameters as list shows them in {} (input.key: key; media.open: path; feature.toggle: name; "
        "mach.start: model; run.frames: count; run.to_scanline: line; ttd.seek_by: unit frames | seconds, min, max, "
        "rate_lo, rate_hi, rate)";
    schema["required"].append("action");

    registry.Register(
        "actions",
        "Host actions: the ids a pad, the Surface Dial, a Stream Deck or a menu runs (run control, time travel, "
        "machine). list: every action with its kind (press, delta, level, text), the companion scope it needs, and "
        "whether it can run now or why not (needs a pause, no history, run-control held). run: one action by id on "
        "the target, with its value; it runs on the instance's actions worker, in order, and answers when done "
        "(a step answers pc, sp, state; ttd.seek_by the frames and the marks crossed: second, ten_seconds, bookmark, "
        "edge, barrier). context: the instance's surface context (play, ttd, ttd.timecode, ...; set it with run "
        "ctx.set). Surfaces: views (the windows in focus order and the followed instance, what a WebAPI call without "
        "an id acts on), target, sessions (each surface and the instance it drives), pin (session + pin: follow, "
        "view:<id>, instance:<id>), readouts (run.state, ttd.position, machine, media.*, audio.mute, debug.mode, "
        "toggle.*), activity (who ran which action, since a seq), view_add / view_update / view_remove and "
        "session_open / session_close (views and sessions of the automation plane: tests, REST surfaces). An action that changes the machine opens the implicit run-control session like the other tools; "
        "take control first for a longer sequence.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string action = args["action"].asString();
            {
                std::string method;
                std::string path;
                Json::Value body;
                std::string error;
                const bool explicitTarget = args["target"].isString() && args["target"].asString() != "auto";
                Json::Value routed = args;
                if (!explicitTarget)
                    routed.removeMember("target");
                if (SurfaceRoute(action, routed, method, path, body, error) || !error.empty())
                {
                    if (!error.empty())
                    {
                        done(ToolResult::Error(error));
                        return;
                    }
                    auto payload = std::make_shared<Json::Value>(body);
                    caller.Call(method, path, method == "GET" || method == "DELETE" ? nullptr : payload.get(),
                                [payload, action, done](int status, Json::Value reply) {
                                    if (status == 0)
                                    {
                                        done(ToolResult::Error("WebAPI unreachable — is the emulator running with "
                                                               "WebAPI enabled (port 8090)?"));
                                        return;
                                    }
                                    if (status < 200 || status >= 300 || !reply.get("ok", false).asBool())
                                    {
                                        done(ToolResult::Error("actions " + action + ": [" +
                                                               reply.get("error", "error").asString() + "] " +
                                                               reply.get("message", DescribeErrorBody(reply)).asString()));
                                        return;
                                    }
                                    const std::string text = SurfaceSummary(action, reply);
                                    done(ToolResult::Ok(text, std::move(reply)));
                                });
                    return;
                }
            }
            if (action != "list" && action != "run" && action != "context")
            {
                done(ToolResult::Error("actions: action is list, run, context, or a surface verb (views, target, "
                                       "sessions, session, pin, readouts, activity, view_add, view_update, "
                                       "view_remove, session_open, session_close)"));
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
                    const std::string text = action == "list"      ? ListSummary(reply)
                                             : action == "context" ? ContextSummary(reply)
                                                                   : RunSummary(reply);
                    done(ToolResult::Ok(text, std::move(reply)));
                };
                if (action == "list")
                {
                    caller.Call("GET", "/api/v1/controllers/actions?instance=" + EncodeSegment(id), nullptr, answer);
                    return;
                }
                if (action == "context")
                {
                    caller.Call("GET", "/api/v1/surfaces/context?instance=" + EncodeSegment(id), nullptr, answer);
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
