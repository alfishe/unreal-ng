#include "mcp-controllers.h"

#include <cctype>
#include <memory>
#include <set>
#include <sstream>
#include <string>

#include "mcp-tool-utils.h"
#include "target-resolver.h"

namespace mcp
{

const std::vector<std::pair<std::string, std::vector<std::string>>>& ControllerToolActions()
{
    static const std::vector<std::pair<std::string, std::vector<std::string>>> actions = {
        {"list", {}},
        {"get", {}},
        {"plug", {"port", "shift", "keys", "extended", "im2-leak", "async"}},
        {"unplug", {"async"}},
        {"options", {"port", "shift", "keys", "extended", "im2-leak", "async"}},
        {"targets", {}},
        {"auto", {"apply"}},
        {"input", {"press", "release", "set"}},
    };
    return actions;
}

const std::vector<std::pair<std::string, std::vector<std::string>>>& HostControllerToolActions()
{
    static const std::vector<std::pair<std::string, std::vector<std::string>>> actions = {
        {"devices", {}},
        {"context", {}},
        {"profiles", {"set", "machine"}},
        {"profile", {"id"}},
        {"save", {"text"}},
        {"delete", {"id"}},
        {"apply", {"id"}},
        {"bindings", {"device", "layer"}},
        {"bind", {"control", "spec", "layer", "family", "device", "profile"}},
        {"unbind", {"control", "layer", "family", "device", "profile"}},
        {"scheme", {"id", "family", "device", "profile"}},
        {"haptics", {"device"}},
        {"rumble", {"device", "effect"}},
    };
    return actions;
}

namespace
{
    bool IsHostAction(const std::string& action)
    {
        for (const auto& [verb, options] : HostControllerToolActions())
            if (verb == action)
                return true;
        return false;
    }

    std::string Compact(const Json::Value& value)
    {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        return Json::writeString(builder, value);
    }

    std::string HostSummary(const std::string& action, const Json::Value& reply)
    {
        std::ostringstream out;
        if (action == "devices")
        {
            for (const Json::Value& device : reply["devices"])
                out << device["id"].asString() << ": " << device["name"].asString() << " ("
                    << device["family"].asString() << ")\n";
            if (reply["devices"].empty())
                out << "no host game controllers connected";
            return out.str();
        }
        if (action == "context" || action == "apply")
        {
            if (reply["message"].isString())
                out << reply["message"].asString() << "\n";
            out << "machine " << reply["machine"].asString() << "; game profile "
                << (reply["manual"].asString().empty() ? "by the loaded file" : reply["manual"].asString())
                << "; applied:";
            for (const Json::Value& id : reply["applied"])
                out << " " << id.asString();
            for (const Json::Value& warning : reply["warnings"])
                out << "\nwarning: " << warning.asString();
            return out.str();
        }
        if (action == "profiles")
        {
            for (const Json::Value& profile : reply["profiles"])
                out << (profile["applied"].asBool() ? "* " : "  ") << profile["id"].asString() << " - "
                    << profile["label"].asString() << " [" << profile["kind"].asString() << ", "
                    << profile["scope"].asString() << ", " << profile["specificity"].asString()
                    << (profile["loaded"].asBool() ? "" : ", not loaded") << "]\n";
            out << "(* = applies now)";
            return out.str();
        }
        if (action == "profile")
            return reply["yaml"].asString();
        if (action == "haptics")
        {
            for (const Json::Value& device : reply["devices"])
            {
                out << device["id"].asString() << ": " << device["name"].asString();
                if (!device["available"].asBool())
                {
                    out << " - off: " << device["reason"].asString() << "\n";
                    continue;
                }
                out << " - effects:";
                for (const Json::Value& effect : device["effects"])
                    out << " " << effect["id"].asString();
                out << "\n";
            }
            if (reply["devices"].empty())
                out << "no host game controllers connected";
            return out.str();
        }
        if (action == "bindings")
        {
            out << reply["scope"].asString() << ":";
            for (const Json::Value& binding : reply["bindings"])
                out << "\n" << binding["layer"].asString() << "." << binding["control"].asString() << ": "
                    << Compact(binding["spec"]) << " (" << binding["profile"].asString() << ")";
            return out.str();
        }
        return reply.get("message", action + ": ok").asString();
    }

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

    std::string Connector(const Json::Value& connector)
    {
        std::ostringstream out;
        out << connector["id"].asString();
        if (connector["alias"].isString())
            out << " (" << connector["alias"].asString() << ")";
        out << ": " << (connector["device"].isString() ? connector["device"].asString() : "empty");
        if (connector["fixed"].asBool())
            out << ", fixed";
        if (connector["held"].size() > 0)
        {
            out << ", held";
            for (const Json::Value& control : connector["held"])
                out << " " << control.asString();
        }
        return out.str();
    }

    std::string Summary(const std::string& action, const Json::Value& reply)
    {
        std::ostringstream out;
        if (action == "list")
        {
            for (const Json::Value& connector : reply["connectors"])
                out << Connector(connector) << "\n";
            out << "revision " << reply["revision"].asUInt64();
            return out.str();
        }
        if (action == "get")
            return Connector(reply["info"]);
        if (action == "targets")
        {
            const int chosen = reply["default"].isInt() ? reply["default"].asInt() : -1;
            for (Json::ArrayIndex i = 0; i < reply["targets"].size(); i++)
            {
                const Json::Value& target = reply["targets"][i];
                out << (i ? "\n" : "") << (static_cast<int>(i) == chosen ? "* " : "  ") << target["connector"].asString();
                if (target["occupiedBy"].isString())
                    out << ", replaces " << target["occupiedBy"].asString();
                if (target["refusal"].isString())
                    out << " - refused: " << target["refusal"].asString();
            }
            return out.str();
        }
        if (action == "auto")
        {
            // One line with the decision: an agent may see only the text
            const Json::Value& decision = reply["decision"];
            const Json::Value& evidence = reply["evidence"];
            if (decision["decisive"].asBool())
                out << decision["label"].asString() << " (port-read evidence: " << decision["reason"].asString() << ")";
            else if (decision["keyboard"].asBool())
                out << "the program reads the whole keyboard (" << decision["reason"].asString() << ")";
            else
                out << "no decision yet";
            out << "; watch " << (evidence["active"].asBool() ? "running" : "stopped") << ", "
                << evidence["frames_observed"].asUInt64() << " frames observed";
            return out.str();
        }
        if (action == "input")
        {
            out << reply["connector"].asString() << ": held";
            for (const Json::Value& control : reply["held"])
                out << " " << control.asString();
            if (reply["held"].empty())
                out << " nothing";
            return out.str();
        }
        out << action << " " << reply["connector"].asString() << ": ok";
        if (reply["pending"].asBool())
            out << " (pending: applied at the next frame boundary)";
        return out.str();
    }
}  // namespace

void RegisterControllers(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["description"] = "Emulator id, or 'auto'";
    schema["properties"]["action"]["type"] = "string";
    for (const auto& [verb, verbOptions] : ControllerToolActions())
        schema["properties"]["action"]["enum"].append(verb);
    for (const auto& [verb, verbOptions] : HostControllerToolActions())
        schema["properties"]["action"]["enum"].append(verb);
    schema["properties"]["connector"]["type"] = "string";
    schema["properties"]["connector"]["description"] =
        "Connector selector: id (kempston, sinclair-1, keyjoy.1, next.left, fuller), alias (left, right), kind:index "
        "(keyboard-joystick:0); 'auto' for plug (the first free connector that takes the device)";
    schema["properties"]["device"]["type"] = "string";
    schema["properties"]["device"]["description"] =
        "plug / targets: the retro device (kempston-joystick, sinclair-joystick, cursor-joystick, keys-joystick, "
        "fuller-joystick, md6-pad); bindings / bind / unbind / haptics / rumble: a host device id (devices lists them)";
    std::string perVerb;
    std::set<std::string> options;
    for (const auto& [verb, verbOptions] : ControllerToolActions())
    {
        std::string list;
        for (const std::string& option : verbOptions)
        {
            options.insert(option);
            list += (list.empty() ? "" : ", ") + option;
        }
        perVerb += "\n" + verb + ": " + (list.empty() ? "-" : list);
    }
    for (const auto& [verb, verbOptions] : HostControllerToolActions())
    {
        std::string list;
        for (const std::string& option : verbOptions)
        {
            if (option != "device")
                options.insert(option);
            list += (list.empty() ? "" : ", ") + option;
        }
        perVerb += "\n" + verb + " (host): " + (list.empty() ? "-" : list);
    }
    const std::set<std::string> booleans = {"shift", "extended", "im2-leak", "async", "apply"};
    for (const std::string& option : options)
        schema["properties"][option]["type"] = booleans.count(option) ? "boolean" : option == "port" ? "integer" : "string";
    schema["properties"]["port"]["description"] = "sinclair-joystick: 1 (keys 6-0) or 2 (keys 1-5)";
    schema["properties"]["keys"]["description"] = "keys-joystick: 'up:q,down:a,left:o,right:p,fire:space'";
    schema["properties"]["apply"]["description"] = "auto: start the port-read watch again (it runs 30 s after a load)";
    for (const char* option : {"press", "release", "set"})
        schema["properties"][option]["description"] =
            "input: the controls ('up+fire'; right, left, down, up, fire, fire2, fire3, fire4; set 'none' = nothing held)";
    schema["properties"]["set"]["description"] = "profiles: all (default), play, debug, scheme, game";
    schema["properties"]["machine"]["description"] = "profiles: only those of a machine model (next, pentagon)";
    schema["properties"]["id"]["description"] =
        "profile / delete / apply: a profile id (builtin/game/vradarks-revenge); apply 'none' = by the loaded file; "
        "scheme: the scheme the pad works as (scheme/kempston, scheme/sinclair-1, scheme/qaop-space; 'none' = the "
        "machine's default)";
    schema["properties"]["text"]["description"] = "save: the whole profile, YAML or JSON (schema 1)";
    schema["properties"]["effect"]["description"] =
        "rumble: the test effect (identify by default; low, high, clicks, left-trigger, right-trigger, swell: haptics "
        "lists a device's)";
    schema["properties"]["control"]["description"] = "bind / unbind: south, lt.click, rstick.x, view+menu, dpad.*";
    schema["properties"]["spec"]["type"] = "object";
    schema["properties"]["spec"]["description"] =
        "bind: the binding ({\"key\": \"ENTER\"}, {\"retro\": \"auto:fire\"}, {\"mouse\": \"left\"}, "
        "{\"none\": true}; plus label, turbo, ...)";
    schema["properties"]["layer"]["description"] = "bindings / bind / unbind: play (default), debug, a shift layer";
    schema["properties"]["family"]["description"] =
        "bind / unbind / scheme: into user/<family> (xbox-elite-2, dualsense)";
    schema["properties"]["profile"]["description"] =
        "bind / unbind: into this profile (a shipped one is copied into the user's); default user/global";
    schema["required"].append("action");

    registry.Register(
        "controllers",
        "The machine's joystick connectors and the retro devices plugged into them. Actions: list, get, plug, unplug, "
        "options, targets (where a retro device can go, in order, with the default), auto (the automatic choice from "
        "the program's port reads: which joystick it polls; apply re-runs it), input (press / release / set controls "
        "on a connector, as joystick_input with connector). Plugs apply at the next frame boundary and wait for it "
        "unless async. Host actions (no target: the process's game controllers, the Qt client's): devices, context "
        "(machine, applied profiles, the manual game profile), profiles, profile (as YAML), save, delete, apply (the "
        "game profile by hand, for a program started from the machine itself), bindings (effective, with the profile "
        "each came from), bind, unbind (in a user profile, saved and applied at once), scheme (how the pad works: "
        "Kempston, Sinclair 1, QAOP + Space; the scheme the user profile extends), haptics (whether the emulator "
        "drives a device's motors, else why; its test effects), rumble (play a test effect on a host device). Options "
        "per action:" +
            perVerb,
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string action = args["action"].asString();
            const std::string connector = args.isMember("connector") ? args["connector"].asString() : "";
            auto body = std::make_shared<Json::Value>(Json::objectValue);
            for (const std::string& name : args.getMemberNames())
                if (name != "action" && name != "connector" && name != "target")
                    (*body)[name] = args[name];

            if (IsHostAction(action))
            {
                if ((*body).isMember("spec") && (*body)["spec"].isObject())
                    (*body)["spec"] = Compact((*body)["spec"]);
                static const std::set<std::string> reads = {"devices", "context", "profiles", "profile", "bindings",
                                                            "haptics"};
                const bool read = reads.count(action) != 0;
                std::string path = "/api/v1/controllers/" + action;
                if (read)
                {
                    std::string query;
                    for (const std::string& name : (*body).getMemberNames())
                        query += (query.empty() ? "?" : "&") + name + "=" +
                                 EncodeSegment((*body)[name].isString() ? (*body)[name].asString()
                                                                         : Compact((*body)[name]));
                    path += query;
                }
                caller.Call(read ? "GET" : "POST", path, read ? nullptr : body.get(),
                            [action, done](int status, Json::Value reply) {
                                if (status == 0)
                                {
                                    done(ToolResult::Error("WebAPI unreachable — is the emulator running with WebAPI "
                                                           "enabled (port 8090)?"));
                                    return;
                                }
                                if (status < 200 || status >= 300 || !reply.get("ok", false).asBool())
                                {
                                    done(ToolResult::Error("controllers " + action + ": [" +
                                                           reply.get("error", "error").asString() + "] " +
                                                           reply.get("message", DescribeErrorBody(reply)).asString()));
                                    return;
                                }
                                const std::string text = HostSummary(action, reply);
                                done(ToolResult::Ok(text, std::move(reply)));
                            });
                return;
            }

            TargetResolver::ResolveFromArgs(args, caller, [action, connector, body, &caller, done](bool ok, const std::string& idOrError) {
                if (!ok)
                {
                    done(ToolResult::Error(idOrError));
                    return;
                }
                const std::string& id = idOrError;
                std::string method = "GET";
                std::string path;
                const Json::Value* payload = nullptr;
                if (action == "list")
                    path = Endpoint(id, "/controllers");
                else if (action == "get")
                    path = Endpoint(id, "/controllers/" + EncodeSegment(connector));
                else if (action == "targets")
                    path = Endpoint(id, "/controllers/targets?device=" + EncodeSegment((*body).get("device", "").asString()));
                else if (action == "auto")
                {
                    if ((*body).get("apply", false).asBool())
                    {
                        method = "POST";
                        path = Endpoint(id, "/controllers/auto/apply");
                    }
                    else
                        path = Endpoint(id, "/controllers/auto");
                }
                else
                {
                    method = "POST";
                    path = Endpoint(id, "/controllers/" + EncodeSegment(connector) + "/" + action);
                    payload = body.get();
                }

                caller.Call(method, path, payload, [action, done](int status, Json::Value reply) {
                    if (status == 0)
                    {
                        done(ToolResult::Error("WebAPI unreachable — is the emulator running with WebAPI enabled (port 8090)?"));
                        return;
                    }
                    if (status < 200 || status >= 300 || !reply.get("ok", false).asBool())
                    {
                        done(ToolResult::Error("controllers " + action + ": [" + reply.get("error", "error").asString() + "] " +
                                               reply.get("message", DescribeErrorBody(reply)).asString()));
                        return;
                    }
                    const std::string text = Summary(action, reply);
                    done(ToolResult::Ok(text, std::move(reply)));
                });
            });
        });
}

} // namespace mcp
