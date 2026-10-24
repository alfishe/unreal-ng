#include <emulator/emulator.h>
#include <emulator/io/controllers/controllercontrol.h>
#include <emulator/state/statenodejson.h>

#ifdef ENABLE_CONTROLLERS
#include "hostinputcontrol.h"
#endif

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "cli-processor.h"

/// region <Controllers>

// controllers [verb] ... : a thin adapter over ControllerControl (core/src/emulator/io/controllers/controllercontrol.h),
// the same verbs as WebAPI /api/v1/emulator/{id}/controllers and MCP `controllers` (controller manager surfaces.md §2).
// The host verbs (profiles, bindings, devices: the process's, no emulator) go to HostInputControl
// (core/controllers/src/hostinputcontrol.h), as WebAPI /api/v1/controllers/{verb} does

namespace
{
    const char* const kUsage =
        "controllers [list]                         - The machine's connectors and the devices they hold\n"
        "controllers get <connector>                - One connector, the controls it holds now\n"
        "controllers plug <connector|auto> <device> [opt=val ..] - e.g. plug auto kempston-joystick, plug auto "
        "sinclair-joystick port=2\n"
        "controllers unplug <connector>             - Free a connector\n"
        "controllers options <connector> opt=val .. - Change the options of the device it holds\n"
        "controllers targets <device>               - The connectors that take a device, the default first\n"
        "controllers auto [apply]                   - The port-read evidence and the joystick it points to\n"
        "Host (profiles and bindings of the host's pads, keyboard and mouse):\n"
        "controllers devices                        - The connected host devices: role, layers up, context, outputs, feel,\n"
        "                                             battery, light, what is missing (notice)\n"
        "controllers context                        - Machine, file hashes, manual game profile, devices\n"
        "controllers profiles [set=game|play|debug|scheme] [machine=next] - Every profile\n"
        "controllers profile <id>                   - One profile as YAML\n"
        "controllers save <file>                    - Save a profile file (YAML or JSON) into the user's profiles\n"
        "controllers delete <id>                    - Delete a user profile\n"
        "controllers apply <id|none>                - The game profile by hand (none: by the loaded file)\n"
        "controllers bindings [device=..] [layer=play|debug] - The effective bindings and their profiles\n"
        "controllers bind <control> field=val .. [family=..|device=..|profile=..] [layer=..] - e.g. bind south "
        "key=ENTER, bind west spec='{key: \"5\"}'\n"
        "controllers unbind <control> [family=..|device=..|profile=..] [layer=..]\n"
        "controllers scheme <scheme/kempston|scheme/qaop-space|none> [family=..|device=..|profile=..] - How the pad works\n"
        "controllers haptics [device]               - Which devices' motors the emulator drives, why not, their effects\n"
        "controllers rumble <device> [effect]       - Play a test effect (identify, low, high, clicks, left-trigger,\n"
        "                                             right-trigger, swell; default identify)\n"
        "controllers output <device> <tick|double|buzz> - A control surface's haptic pattern (the Surface Dial)\n"
        "controllers output <device> steps=36 clicks=on|off - Its steps per turn and a click at each (off: free wheel)\n"
        "controllers output <device> marks=second,edge - A seek's marks as the device's layer plays them\n"
        "controllers light <device> <#RRGGBB|r,g,b|off> - A pad's light colour (a DualSense / DualShock 4 light bar)\n"
        "controllers role <device> <player|debugger-remote|disabled> - What the device is for\n"
        "controllers pin <device> <follow-focus|all|pinned:<emulator id>> - Which instance it drives\n"
        "controllers identify <device>              - Rumble 300 ms and blink its light, to tell two pads apart\n"
        "controllers player <device> [connector|auto|none] - The connector the pad's auto: targets drive\n"
        "                                             (auto: the automatic choice again, none: no connector)\n"
        "controllers probe [device]                 - The backends and the devices each found; with a device its\n"
        "                                             endpoints and what each can do\n"
        "controllers reports <device>               - The newest raw input reports in hex (macOS IOHID)\n"
        "controllers database [vid:pid|SDL GUID|device|family] [bus=usb|bt] [name=..] [version=..] - The device\n"
        "                                             database: a device's family, layout, image set, map and match\n"
        "                                             step; with no key the devices.user.json entries\n"
        "controllers database_save <file>           - Put the entry (or the file's families) into devices.user.json\n"
        "controllers database_delete <id>           - Remove an entry of devices.user.json\n"
        "controllers database_import <file> [save=false] - A gamecontrollerdb.txt line or a RetroArch .cfg as a user\n"
        "                                             family with its control map (a connected pad takes it at once)\n"
        "controllers learn <device> [start|status|skip|back|finish|cancel] [controls=dpad.up,south,..] - The learn\n"
        "                                             wizard: press the prompted control on the device after each step\n"
        "controllers learn <device> sample <control> [value] - Feed a raw control (button.3, axis.1) as if pressed\n"
        "controllers aliases                        - Device aliases: old id -> new (a gc: pad matched to its HID device)\n"
        "controllers unalias <old id>               - Forget an alias: profiles naming the old id stop applying\n"
        "Flags: --json (the reply every surface returns), async (do not wait for the frame boundary)\n";

    std::string Lines(const std::string& text, const char* newline)
    {
        std::string out;
        for (char c : text)
        {
            if (c == '\n')
                out += newline;
            else
                out += c;
        }
        return out;
    }

    std::string Text(const StateNode* node)
    {
        if (node == nullptr || node->kind == StateNode::Kind::Null)
            return "-";
        if (node->kind == StateNode::Kind::String)
            return node->s;
        return StateNodeToJsonText(*node);
    }

    /// A connector line: id, kind, the device, the alias
    std::string ConnectorLine(const StateNode& connector)
    {
        std::string line = "  " + Text(connector.find("id"));
        line.resize(std::max<size_t>(line.size() + 1, 20), ' ');
        line += Text(connector.find("kind"));
        line.resize(std::max<size_t>(line.size() + 1, 40), ' ');
        line += Text(connector.find("device"));
        const StateNode* alias = connector.find("alias");
        if (alias != nullptr && alias->kind == StateNode::Kind::String)
            line += "  (" + alias->s + ")";
        return line + "\n";
    }

#ifdef ENABLE_CONTROLLERS
    /// A host verb: positional id (profile, delete, apply, database_delete, unalias), control (bind, unbind), file
    /// (save, database_save, database_import), a key (database: vid:pid, an SDL GUID, a device or a family) or device
    /// (haptics, rumble, then its effect; output, then its pattern; light, then its colour; role / pin, then the role /
    /// pin; identify; player, then the connector; probe; reports; learn, then the step, control, value); for bind, the
    /// options that are not the verb's become the binding spec (key=ENTER
    /// label=Fire)
    std::string HandleHostVerb(const std::string& verb, const std::vector<std::string>& words,
                               const std::map<std::string, std::string>& options, bool json)
    {
        using namespace controllers;
        HostRequest request;
        request.verb = verb;
        const std::vector<std::string>& known = HostInputControl::OptionsFor(verb);
        JsonValue spec = JsonValue::Object();
        for (const auto& [name, value] : options)
        {
            if (std::find(known.begin(), known.end(), name) != known.end() || verb != "bind")
                request.options[name] = value;
            else
                spec.Set(name, JsonValue::String(value));
        }
        if (words.size() > 1)
        {
            if (verb == "bind" || verb == "unbind")
                request.options["control"] = words[1];
            else if (verb == "role" || verb == "pin" || verb == "identify" || verb == "learn")
            {
                request.options["device"] = words[1];
                if ((verb == "role" || verb == "pin") && words.size() > 2)
                    request.options[verb] = words[2];
                if (verb == "learn" && words.size() > 2)
                    request.options["step"] = words[2];
                if (verb == "learn" && words.size() > 3)
                    request.options["control"] = words[3];
                if (verb == "learn" && words.size() > 4)
                    request.options["value"] = words[4];
            }
            else if (verb == "player" || verb == "probe" || verb == "reports")
            {
                request.options["device"] = words[1];
                if (verb == "player" && words.size() > 2)
                    request.options["connector"] = words[2];
            }
            else if (verb == "haptics" || verb == "rumble" || verb == "output" || verb == "light")
            {
                request.options["device"] = words[1];
                if (verb == "rumble" && words.size() > 2)
                    request.options["effect"] = words[2];
                if (verb == "output" && words.size() > 2)
                    request.options["pattern"] = words[2];
                if (verb == "light" && words.size() > 2)
                    request.options["color"] = words[2];
            }
            else if (verb == "database")
            {
                // 32 hex digits: an SDL GUID; vid:pid; another id with a colon: a connected device; else a family
                const std::string& key = words[1];
                const bool hex = std::all_of(key.begin(), key.end(), [](unsigned char c) { return std::isxdigit(c); });
                if (key.size() == 32 && hex)
                    request.options["guid"] = key;
                else if (key.size() == 9 && key[4] == ':')
                    request.options["vidpid"] = key;
                else
                    request.options[key.find(':') != std::string::npos ? "device" : "family"] = key;
            }
            else if (verb == "save" || verb == "database_save" || verb == "database_import")
            {
                std::ifstream file(words[1], std::ios::binary);
                if (!file)
                    return "Error: cannot read '" + words[1] + "'\n";
                std::ostringstream text;
                text << file.rdbuf();
                request.options["text"] = text.str();
            }
            else
                request.options["id"] = words[1];
        }
        if (verb == "bind" && request.options.count("spec") == 0 && !spec.GetMembers().empty())
            request.options["spec"] = spec.Dump();

        const HostReply reply = HostInputControl::Process(request);
        if (json)
            return reply.ToJson() + "\n";
        if (!reply.Ok())
            return "Error: " + reply.message + "\n";
        std::string text;
        if (verb == "profile" && reply.body.Find("yaml") != nullptr)
            return reply.body.Find("yaml")->AsString();
        if (const JsonValue* bindings = reply.body.Find("bindings"))
        {
            for (const JsonValue& binding : bindings->Items())
            {
                std::string line = "  " + binding.Find("layer")->AsString() + "." + binding.Find("control")->AsString();
                line.resize(std::max<size_t>(line.size() + 1, 24), ' ');
                line += binding.Find("spec")->Dump();
                line.resize(std::max<size_t>(line.size() + 1, 60), ' ');
                text += line + binding.Find("profile")->AsString() + "\n";
            }
            return "Bindings (control, spec, profile):\n" + text;
        }
        if (verb == "haptics")
        {
            for (const JsonValue& device : reply.body.Find("devices")->Items())
            {
                text += "  " + device.Find("id")->AsString() + "  " + device.Find("name")->AsString() + "\n";
                if (!device.Find("available")->AsBool())
                {
                    text += "    off: " + device.Find("reason")->AsString() + "\n";
                    continue;
                }
                std::string effects;
                for (const JsonValue& effect : device.Find("effects")->Items())
                    effects += (effects.empty() ? "" : ", ") + effect.Find("id")->AsString();
                text += "    effects: " + effects + "\n";
            }
            return "Haptics (device, the effects rumble plays or why not):\n" + (text.empty() ? "  none\n" : text);
        }
        if (verb == "probe")
        {
            for (const JsonValue& backend : reply.body.Find("backends")->Items())
            {
                const JsonValue* reason = backend.Find("reason");
                text += "  " + backend.Find("name")->AsString() + ": " +
                        (backend.Find("running")->AsBool() ? std::string("running")
                                                           : "not running: " + (reason ? reason->AsString() : "")) +
                        "\n";
                for (const JsonValue& device : backend.Find("found")->Items())
                {
                    const JsonValue* match = device.Find("match");
                    text += "    " + device.Find("id")->AsString() + "  " + device.Find("name")->AsString() + " (" +
                            device.Find("family")->AsString() + ", database " + match->Find("step")->AsString() + " " +
                            std::to_string(static_cast<int>(match->Find("score")->AsNumber())) + ")\n";
                    if (const JsonValue* notice = device.Find("notice"))
                        text += "      notice: " + notice->AsString() + "\n";
                }
            }
            if (const JsonValue* endpoints = reply.body.Find("endpoints"))
            {
                text += "Endpoints of " + reply.body.Find("device")->AsString() + ":\n";
                for (const JsonValue& endpoint : endpoints->Items())
                {
                    std::string caps;
                    for (const char* cap : {"rumble", "light", "battery"})
                        if (endpoint.Find(cap)->AsBool())
                            caps += std::string(caps.empty() ? "" : ", ") + cap;
                    text += "  " + endpoint.Find("backend")->AsString() + "  " + endpoint.Find("role")->AsString() +
                            "  " + endpoint.Find("name")->AsString() + "  " +
                            std::to_string(static_cast<int>(endpoint.Find("controls")->AsNumber())) + " controls (" +
                            endpoint.Find("driver")->AsString() + ")" + (caps.empty() ? "" : "; " + caps) +
                            (endpoint.Find("detail")->AsString().empty() ? "" : "; " + endpoint.Find("detail")->AsString()) +
                            "\n";
                }
            }
            return "Backends:\n" + (text.empty() ? "  none\n" : text) + "User devices: " +
                   reply.body.Find("user_devices")->AsString() + "\n";
        }
        if (verb == "aliases")
        {
            for (const JsonValue& alias : reply.body.Find("aliases")->Items())
            {
                text += "  " + alias.Find("from")->AsString() + " -> " + alias.Find("to")->AsString();
                if (alias.Find("connected")->AsBool())
                    text += "  (" + alias.Find("name")->AsString() + ", connected)";
                text += "\n";
            }
            return "Device aliases (old id -> new id):\n" + (text.empty() ? "  none\n" : text);
        }
        if (verb == "devices")
        {
            for (const JsonValue& device : reply.body.Find("devices")->Items())
            {
                text += "  " + device.Find("id")->AsString() + "  " + device.Find("name")->AsString() + " (" +
                        device.Find("family")->AsString() + ")\n";
                if (const JsonValue* role = device.Find("role"))
                {
                    std::string layers;
                    for (const JsonValue& layer : device.Find("layers")->Items())
                        layers += (layers.empty() ? "" : ", ") + layer.AsString();
                    text += "    role " + role->AsString() + (device.Find("role_set")->AsBool() ? "" : " (default)") +
                            "; layers " + (layers.empty() ? "none" : layers);
                    if (const JsonValue* pin = device.Find("pin"); pin != nullptr && pin->AsString() != "follow-focus")
                        text += "; pin " + pin->AsString();
                    if (!device.Find("context")->AsString().empty())
                        text += "; context " + device.Find("context")->AsString();
                    text += "\n";
                }
                if (const JsonValue* feel = device.Find("feel"))
                    text += "    feel " + feel->Dump() + "\n";
                if (const JsonValue* battery = device.Find("battery"))
                {
                    const JsonValue* light = device.Find("light");
                    text += "    battery " + battery->Find("text")->AsString() + "; light " +
                            (light != nullptr && light->Find("available")->AsBool() ? "settable" : "none") + "\n";
                }
                if (const JsonValue* map = device.Find("map"))
                    text += "    map " + map->Dump() + "\n";
                if (const JsonValue* notice = device.Find("notice"))
                    text += "    notice: " + notice->AsString() + "\n";
            }
            return "Host devices:\n" + (text.empty() ? "  none\n" : text);
        }
        if (verb == "database")
        {
            if (const JsonValue* match = reply.body.Find("match"))
            {
                text += "  " + reply.message + "\n  " + match->Find("family")->AsString() + ": " + match->Find("name")->AsString() +
                        "\n    layout " + match->Find("layout")->AsString() + ", images " +
                        match->Find("images")->AsString();
                if (const JsonValue* step = match->Find("step"))
                    text += ", " + step->AsString() + " match, score " +
                            std::to_string(static_cast<int>(match->Find("score")->AsNumber()));
                text += match->Find("user")->AsBool() ? ", from devices.user.json\n" : "\n";
                if (const JsonValue* map = match->Find("map"))
                    text += "    map " + map->Dump() + "\n";
            }
            std::string user;
            for (const JsonValue& id : reply.body.Find("user")->Items())
                user += (user.empty() ? "" : ", ") + id.AsString();
            return "Device database (" + std::to_string(static_cast<int>(reply.body.Find("families")->AsNumber())) +
                   " families):\n" + text + "  devices.user.json: " + (user.empty() ? "no entries" : user) + "\n";
        }
        if (verb == "reports")
        {
            for (const JsonValue& report : reply.body.Find("reports")->Items())
                text += "  " + report.Find("hex")->AsString() + "  (" +
                        std::to_string(static_cast<int>(report.Find("size")->AsNumber())) + " B)\n";
            return reply.message + ":\n" + (text.empty() ? "  none yet: read again while pressing a control\n" : text);
        }
        if (const JsonValue* profiles = reply.body.Find("profiles"))
        {
            for (const JsonValue& profile : profiles->Items())
            {
                std::string line = "  " + profile.Find("id")->AsString();
                line.resize(std::max<size_t>(line.size() + 1, 40), ' ');
                if (const JsonValue* label = profile.Find("label"))
                    line += label->AsString();
                text += line + "\n";
            }
            return "Profiles (id, label):\n" + text;
        }
        text = "OK";
        if (!reply.message.empty())
            text += ": " + reply.message;
        text += "\n";
        for (const auto& [key, field] : reply.body.GetMembers())
            text += "  " + key + ": " + (field.IsString() ? field.AsString() : field.Dump()) + "\n";
        return text;
    }
#endif
}  // namespace

void CLIProcessor::HandleControllers(const ClientSession& session, const std::vector<std::string>& args)
{
    using namespace controllers;
    if (!args.empty() && (args[0] == "help" || args[0] == "--help"))
    {
        session.SendResponse(Lines(kUsage, NEWLINE));
        return;
    }
    bool json = false;
    std::vector<std::string> words;
    std::map<std::string, std::string> options;
    for (const std::string& arg : args)
    {
        if (arg == "--json")
            json = true;
        else if (arg.find('=') != std::string::npos)
            options[arg.substr(0, arg.find('='))] = arg.substr(arg.find('=') + 1);
        else if (!words.empty() && (arg == "async" || arg == "apply" || arg == "shift"))  // not the verb `apply`
            options[arg] = "";
        else
            words.push_back(arg);
    }
#ifdef ENABLE_CONTROLLERS
    if (!words.empty())
    {
        const auto& hostVerbs = HostInputControl::Verbs();
        if (std::find(hostVerbs.begin(), hostVerbs.end(), words[0]) != hostVerbs.end())
        {
            session.SendResponse(Lines(HandleHostVerb(words[0], words, options, json), NEWLINE));
            return;
        }
    }
#endif
    ControllerRequest request;
    request.verb = words.empty() ? "list" : words[0];
    const auto& verbs = ControllerControl::Verbs();
    if (std::find(verbs.begin(), verbs.end(), request.verb) == verbs.end())
    {
        session.SendResponse(Lines("Error: unknown verb '" + request.verb + "'\n" + kUsage, NEWLINE));
        return;
    }
    if (request.verb == "targets")
    {
        if (words.size() > 1)
            request.device = words[1];
    }
    else
    {
        if (words.size() > 1)
            request.selector = words[1];
        if (words.size() > 2)
            request.device = words[2];
    }
    request.options = options;

    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }
    const ControllerReply reply = ControllerControl(emulator->GetContext()).Execute(request);
    if (json)
    {
        session.SendResponse(reply.ToJson() + NEWLINE);
        return;
    }
    if (!reply.result.Ok())
    {
        session.SendResponse("Error: " + reply.result.message + NEWLINE);
        return;
    }
    std::string text;
    const StateNode value = reply.ToValue();
    if (const StateNode* connectors = value.find("connectors"))
    {
        text += "Connectors (id, kind, device):\n";
        for (const StateNode& connector : connectors->items)
            text += ConnectorLine(connector);
    }
    else
    {
        text += "OK";
        if (!reply.connector.empty())
            text += " " + reply.connector;
        if (reply.pending)
            text += " (pending: applies at the frame boundary)";
        if (!reply.result.message.empty())
            text += ": " + reply.result.message;
        text += "\n";
        for (const auto& [key, field] : reply.body.members)
            text += "  " + key + ": " + Text(&field) + "\n";
    }
    session.SendResponse(Lines(text, NEWLINE));
}

/// endregion </Controllers>
