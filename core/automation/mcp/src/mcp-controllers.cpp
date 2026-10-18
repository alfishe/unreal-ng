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
    schema["properties"]["connector"]["type"] = "string";
    schema["properties"]["connector"]["description"] =
        "Connector selector: id (kempston, sinclair-1, keyjoy.1, next.left, fuller), alias (left, right), kind:index "
        "(keyboard-joystick:0); 'auto' for plug (the first free connector that takes the device)";
    schema["properties"]["device"]["type"] = "string";
    schema["properties"]["device"]["description"] =
        "plug / targets: the retro device (kempston-joystick, sinclair-joystick, cursor-joystick, keys-joystick, "
        "fuller-joystick, md6-pad)";
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
    const std::set<std::string> booleans = {"shift", "extended", "im2-leak", "async", "apply"};
    for (const std::string& option : options)
        schema["properties"][option]["type"] = booleans.count(option) ? "boolean" : option == "port" ? "integer" : "string";
    schema["properties"]["port"]["description"] = "sinclair-joystick: 1 (keys 6-0) or 2 (keys 1-5)";
    schema["properties"]["keys"]["description"] = "keys-joystick: 'up:q,down:a,left:o,right:p,fire:space'";
    schema["properties"]["apply"]["description"] = "auto: start the port-read watch again (it runs 30 s after a load)";
    for (const char* option : {"press", "release", "set"})
        schema["properties"][option]["description"] =
            "input: the controls ('up+fire'; right, left, down, up, fire, fire2, fire3, fire4; set 'none' = nothing held)";
    schema["required"].append("action");

    registry.Register(
        "controllers",
        "The machine's joystick connectors and the retro devices plugged into them. Actions: list, get, plug, unplug, "
        "options, targets (where a retro device can go, in order, with the default), auto (the automatic choice from "
        "the program's port reads: which joystick it polls; apply re-runs it), input (press / release / set controls "
        "on a connector, as joystick_input with connector). Plugs apply at the next frame boundary and wait for it "
        "unless async. Options per action:" + perVerb,
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string action = args["action"].asString();
            const std::string connector = args.isMember("connector") ? args["connector"].asString() : "";
            auto body = std::make_shared<Json::Value>(Json::objectValue);
            for (const std::string& name : args.getMemberNames())
                if (name != "action" && name != "connector" && name != "target")
                    (*body)[name] = args[name];

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
