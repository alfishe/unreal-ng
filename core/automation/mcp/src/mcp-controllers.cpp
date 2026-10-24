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
        {"output", {"device", "pattern", "steps", "clicks", "marks"}},
        {"light", {"device", "color"}},
        {"database", {"vidpid", "bus", "name", "family"}},
        {"database-save", {"text"}},
        {"database-delete", {"id"}},
        {"database-import", {"text", "save"}},
        {"player", {"device", "connector"}},
        {"probe", {"device"}},
        {"reports", {"device"}},
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
            {
                out << device["id"].asString() << ": " << device["name"].asString() << " ("
                    << device["family"].asString() << ")";
                // A control surface's role, the layers up and its context, the feel of its top layer
                if (device.isMember("role"))
                {
                    out << " - role " << device["role"].asString() << (device["role_set"].asBool() ? "" : " (default)")
                        << ", layers";
                    for (const Json::Value& layer : device["layers"])
                        out << " " << layer.asString();
                    if (!device["context"].asString().empty())
                        out << ", context " << device["context"].asString();
                }
                if (device.isMember("feel"))
                    out << ", feel " << Compact(device["feel"]);
                // An agent may see only the text: the battery, the light and what is missing go into it too
                if (device.isMember("battery"))
                    out << ", battery " << device["battery"]["text"].asString() << ", light "
                        << (device["light"]["available"].asBool() ? "settable" : "none");
                if (device.isMember("notice"))
                    out << "\n  notice: " << device["notice"].asString();
                out << "\n";
            }
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
        if (action == "probe")
        {
            for (const Json::Value& backend : reply["backends"])
                out << backend["name"].asString()
                    << (backend["running"].asBool() ? " ok, " + std::to_string(backend["devices"].asInt()) + " devices"
                                                    : ": " + backend["reason"].asString())
                    << "\n";
            for (const Json::Value& endpoint : reply["endpoints"])
            {
                out << "endpoint " << endpoint["backend"].asString() << " (" << endpoint["role"].asString() << "): "
                    << endpoint["name"].asString() << ", " << endpoint["controls"].asInt() << " controls via "
                    << endpoint["driver"].asString();
                for (const char* cap : {"rumble", "light", "battery"})
                    if (endpoint[cap].asBool())
                        out << ", " << cap;
                if (!endpoint["detail"].asString().empty())
                    out << ", " << endpoint["detail"].asString();
                out << "\n";
            }
            return out.str();
        }
        if (action == "reports")
        {
            out << reply["message"].asString() << "\n";
            for (const Json::Value& report : reply["reports"])
                out << report["hex"].asString() << " (" << report["size"].asInt() << " B)\n";
            return out.str();
        }
        if (action == "database-import")
        {
            out << reply["message"].asString() << "\n" << Compact(reply["entry"]);
            for (const Json::Value& skipped : reply["skipped"])
                out << "\nnot taken over: " << skipped.asString();
            return out.str();
        }
        if (action == "database")
        {
            // The match in one line (an agent may see only the text), then the user entries
            if (reply.isMember("match"))
            {
                const Json::Value& match = reply["match"];
                out << match["family"].asString() << ": " << match["name"].asString() << " (layout "
                    << match["layout"].asString() << ", images " << match["images"].asString();
                if (match.isMember("step"))
                    out << ", " << match["step"].asString() << " match, score " << match["score"].asInt();
                if (match["user"].asBool())
                    out << ", from devices.user.json";
                out << ")\n";
            }
            out << reply["families"].asInt() << " families; devices.user.json:";
            for (const Json::Value& id : reply["user"])
                out << " " << id.asString();
            if (reply["user"].empty())
                out << " no entries";
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
        "(keyboard-joystick:0); 'auto' for plug (the first free connector that takes the device); player: the "
        "connector the host pad's auto: targets drive ('auto' = the automatic choice)";
    schema["properties"]["device"]["type"] = "string";
    schema["properties"]["device"]["description"] =
        "plug / targets: the retro device (kempston-joystick, sinclair-joystick, cursor-joystick, keys-joystick, "
        "fuller-joystick, md6-pad); bindings / bind / unbind / haptics / rumble / output / light / player / probe / "
        "reports: a host device id (devices lists them)";
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
        "database-delete: the devices.user.json entry; "
        "scheme: the scheme the pad works as (scheme/kempston, scheme/sinclair-1, scheme/qaop-space; 'none' = the "
        "machine's default)";
    schema["properties"]["text"]["description"] =
        "save: the whole profile, YAML or JSON (schema 1); database-save: one devices.user.json entry as JSON "
        "({\"id\": \"xbox-elite-2\", \"images\": \"my-elite\"}: a known family changes the fields it names; a new "
        "one needs its match list) or a file's {\"families\": [...]}; database-import: a gamecontrollerdb.txt line "
        "or a RetroArch joypad autoconfig .cfg";
    schema["properties"]["save"]["description"] = "database-import: false = only answer the entry, do not save it";
    schema["properties"]["vidpid"]["description"] = "database: look a device up by its USB ids (045e:0b00)";
    schema["properties"]["bus"]["description"] = "database: with vidpid, usb or bt (some families match per bus)";
    schema["properties"]["name"]["description"] = "database: the device's product name (the name templates)";
    schema["properties"]["effect"]["description"] =
        "rumble: the test effect (identify by default; low, high, clicks, left-trigger, right-trigger, swell: haptics "
        "lists a device's)";
    schema["properties"]["pattern"]["description"] = "output: a haptic pattern (tick, double, buzz)";
    schema["properties"]["steps"]["description"] =
        "output: steps per turn (1..3600; the Dial's default 36), with clicks; lasts until its context or layers change";
    schema["properties"]["clicks"]["description"] = "output: on (a click at each step) or off (a free wheel)";
    schema["properties"]["marks"]["description"] =
        "output: a seek's marks, strongest first (second, ten_seconds, bookmark, edge, barrier): the pattern the "
        "device's top layer plays for them";
    schema["properties"]["color"]["description"] = "light: #RRGGBB, r,g,b (0..255 each) or off";
    schema["properties"]["control"]["description"] = "bind / unbind: south, lt.click, rstick.x, view+menu, dpad.*";
    schema["properties"]["spec"]["type"] = "object";
    schema["properties"]["spec"]["description"] =
        "bind: the binding ({\"key\": \"ENTER\"}, {\"retro\": \"auto:fire\"}, {\"mouse\": \"left\"}, "
        "{\"none\": true}; plus label, turbo, ...)";
    schema["properties"]["layer"]["description"] = "bindings / bind / unbind: play (default), debug, a shift layer";
    schema["properties"]["family"]["description"] =
        "bind / unbind / scheme: into user/<family> (xbox-elite-2, dualsense); database: look a family up";
    schema["properties"]["profile"]["description"] =
        "bind / unbind: into this profile (a shipped one is copied into the user's); default user/global";
    schema["required"].append("action");

    registry.Register(
        "controllers",
        "The machine's joystick connectors and the retro devices plugged into them. Actions: list, get, plug, unplug, "
        "options, targets (where a retro device can go, in order, with the default), auto (the automatic choice from "
        "the program's port reads: which joystick it polls; apply re-runs it), input (press / release / set controls "
        "on a connector, as joystick_input with connector). Plugs apply at the next frame boundary and wait for it "
        "unless async. Host actions (no target: the process's game controllers, the Qt client's): devices (each with "
        "its battery, whether its light can be set, a notice of what is missing and how to fix it), context "
        "(machine, applied profiles, the manual game profile), profiles, profile (as YAML), save, delete, apply (the "
        "game profile by hand, for a program started from the machine itself), bindings (effective, with the profile "
        "each came from), bind, unbind (in a user profile, saved and applied at once), scheme (how the pad works: "
        "Kempston, Sinclair 1, QAOP + Space; the scheme the user profile extends), haptics (whether the emulator "
        "drives a device's motors, else why; its test effects), rumble (play a test effect on a host device), output (a "
        "control surface such as the Surface Dial: a haptic pattern, its steps per turn and clicks, or a seek's marks "
        "as its context layer plays them), light (a pad's light colour: a DualSense / DualShock 4 light bar), database "
        "(look a device up by VID:PID or family: its family, layout, image set, match step; the devices.user.json "
        "entries), database-save / database-delete (an entry of devices.user.json: a user device, a new name, a "
        "photo's image set), database-import (a gamecontrollerdb line or a RetroArch autoconfig as a user entry "
        "matched by its VID:PID), player (the connector a host pad's auto: targets drive; connector=auto: back to "
        "the automatic choice, connector=none: no connector), probe (the backends; with device its endpoints: "
        "IOHID interfaces, a GameController companion, what each gives), reports (a device's newest raw HID input reports in hex; the tap runs while "
        "they are read). Options per action:" +
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
                if (action == "player" && !connector.empty())
                    (*body)["connector"] = connector;  // here the host pad's connector, not the machine target
                static const std::set<std::string> reads = {"devices", "context", "profiles", "profile", "bindings",
                                                            "haptics", "database", "probe", "reports"};
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
