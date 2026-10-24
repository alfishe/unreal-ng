#include "controllercontrol.h"

#ifdef ENABLE_CONTROLLERS
#include "profiles/autodecision.h"
#endif

#include <algorithm>
#include <cctype>

#include "emulator/emulatorcontext.h"
#include "emulator/state/statenodejson.h"

namespace controllers
{

namespace
{
constexpr const char* kControlNames[kOverlayControls] = {"right", "left",  "down",  "up",
                                                         "fire",  "fire2", "fire3", "fire4"};

std::string Lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string Trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

std::string Join(const std::vector<std::string>& items)
{
    std::string text;
    for (const std::string& item : items)
        text += (text.empty() ? "" : ", ") + item;
    return text;
}

ControllerReply Fail(ControllerError error, const std::string& message)
{
    ControllerReply reply;
    reply.result = ControllerResult::Fail(error, message);
    return reply;
}

bool ParseBool(const std::string& text, bool& value)
{
    const std::string v = Lower(Trim(text));
    if (v.empty() || v == "1" || v == "true" || v == "yes" || v == "on")
        value = true;
    else if (v == "0" || v == "false" || v == "no" || v == "off")
        value = false;
    else
        return false;
    return true;
}

/// "up+fire", "up,fire", "up fire" -> bits; b5..b7 are the Kempston names of fire2..fire4. False with the name
bool ParseControls(const std::string& text, uint16_t& bits, std::string& error)
{
    bits = 0;
    std::string name;
    auto flush = [&]() {
        if (name.empty())
            return true;
        const std::string lower = Lower(name);
        name.clear();
        static const std::pair<const char*, const char*> kAliases[] = {{"b5", "fire2"}, {"b6", "fire3"}, {"b7", "fire4"}};
        std::string control = lower;
        for (const auto& [alias, real] : kAliases)
            if (control == alias)
                control = real;
        for (unsigned bit = 0; bit < kOverlayControls; bit++)
        {
            if (control == kControlNames[bit])
            {
                bits |= static_cast<uint16_t>(1u << bit);
                return true;
            }
        }
        error = "unknown control '" + lower + "' (controls: right, left, down, up, fire, fire2, fire3, fire4)";
        return false;
    };
    if (Lower(Trim(text)) == "none")
        return true;  // `set none`: nothing held
    for (char c : text)
    {
        if (c == '+' || c == ',' || c == ' ')
        {
            if (!flush())
                return false;
        }
        else
            name += c;
    }
    return flush();
}

/// The controls held, by name ("up", "fire")
StateNode HeldControls(ConnectorKind kind, uint16_t state)
{
    StateNode held = StateNode::Array();
    // The Next pad's bits 4-11 are its own buttons (PadState); only the shared directions are named here
    const unsigned named = kind == ConnectorKind::NextDb9 ? 4 : kOverlayControls;
    for (unsigned bit = 0; bit < named; bit++)
    {
        if (state & (1u << bit))
            held.push(kControlNames[bit]);
    }
    return held;
}
}  // namespace

/// region <ControllerReply>

StateNode ControllerReply::ToValue() const
{
    StateNode value = StateNode::Object();
    value["ok"] = result.Ok();
    if (!result.Ok())
    {
        value["error"] = ControllerErrorCode(result.error);
        value["message"] = result.message;
    }
    if (!connector.empty())
        value["connector"] = connector;
    value["pending"] = pending;
    value["revision"] = revision;
    for (const auto& [key, field] : body.members)
        value[key] = field;
    return value;
}

std::string ControllerReply::ToJson() const
{
    return StateNodeToJsonText(ToValue());
}

/// endregion </ControllerReply>

ControllerControl::ControllerControl(EmulatorContext* context)
    : _manager(context ? context->pControllerManager : nullptr)
{
}

const std::vector<std::string>& ControllerControl::Verbs()
{
    static const std::vector<std::string> verbs = {"list",    "get",     "plug", "unplug",
                                                   "options", "targets", "auto", "input"};
    return verbs;
}

const std::vector<std::string>& ControllerControl::OptionsFor(const std::string& verb)
{
    static const std::map<std::string, std::vector<std::string>> options = {
        {"list", {}},
        {"get", {}},
        {"plug", {"port", "shift", "keys", "extended", "im2-leak", "async"}},
        {"unplug", {"async"}},
        {"options", {"port", "shift", "keys", "extended", "im2-leak", "async"}},
        {"targets", {}},
        {"auto", {"apply"}},
        {"input", {"press", "release", "set"}},
    };
    static const std::vector<std::string> none;
    auto it = options.find(verb);
    return it == options.end() ? none : it->second;
}

ControllerReply ControllerControl::Execute(const ControllerRequest& request)
{
    if (!_manager)
        return Fail(ControllerError::BadRequest, "this emulator has no controller manager");
    ControllerReply reply = Run(request);
    reply.revision = _manager->Revision();  // on failures too: the client's view stays current
    return reply;
}

ControllerReply ControllerControl::Run(const ControllerRequest& request)
{
    const std::string verb = Lower(Trim(request.verb));
    const auto& verbs = Verbs();
    if (std::find(verbs.begin(), verbs.end(), verb) == verbs.end())
        return Fail(ControllerError::BadRequest,
                    "unknown controller verb '" + request.verb + "' (verbs: " + Join(verbs) + ")");

    // Every option name is checked, so a typo fails the same way on every surface
    const auto& allowed = OptionsFor(verb);
    for (const auto& [name, value] : request.options)
    {
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
        {
            return Fail(ControllerError::BadRequest,
                        "unknown option '" + name + "' for " + verb +
                            (allowed.empty() ? " (it takes none)" : " (options: " + Join(allowed) + ")"));
        }
    }

    if (verb == "list")
        return List();
    if (verb == "auto")
        return Auto(request);
    if (verb == "get")
        return Get(request);
    if (verb == "plug")
        return Plug(request);
    if (verb == "unplug")
        return Unplug(request);
    if (verb == "options")
        return Options(request);
    if (verb == "input")
        return Input(request);
    return Targets(request);
}

ControllerResult ControllerControl::Resolve(const std::string& selector, size_t& index) const
{
    const int found = _manager->Find(selector);
    if (found >= 0)
    {
        index = static_cast<size_t>(found);
        return ControllerResult::Success();
    }
    std::vector<std::string> ids;
    for (const Connector& connector : _manager->Connectors())
        ids.push_back(connector.id);
    return ControllerResult::Fail(
        ControllerError::UnknownConnector,
        "no connector '" + Trim(selector) + "' on this machine (connectors: " + Join(ids) + ")");
}

StateNode ControllerControl::ConnectorValue(const Connector& connector, uint16_t state)
{
    StateNode value = StateNode::Object();
    value["id"] = connector.id;
    value["alias"] = connector.alias.empty() ? StateNode() : StateNode(connector.alias);
    value["kind"] = ConnectorKindName(connector.kind);
    value["index"] = connector.kindIndex;
    value["fixed"] = connector.fixed;
    value["device"] =
        connector.device == RetroDeviceKind::None ? StateNode() : StateNode(RetroDeviceKindName(connector.device));
    StateNode accepts = StateNode::Array();
    for (RetroDeviceKind kind : connector.accepts)
        accepts.push(RetroDeviceKindName(kind));
    value["accepts"] = accepts;

    StateNode options = StateNode::Object();
    switch (connector.device)
    {
        case RetroDeviceKind::SinclairJoystick:
            options["port"] = static_cast<unsigned>(connector.options.sinclairPort);
            break;
        case RetroDeviceKind::CursorJoystick:
            options["shift"] = connector.options.cursorShift;
            break;
        case RetroDeviceKind::KeysJoystick:
            options["keys"] = ControllerManager::KeysSpec(connector.options.keys);
            break;
        case RetroDeviceKind::KempstonJoystick:
            options["extended"] = connector.options.extended;
            options["im2-leak"] = connector.options.im2Leak;
            break;
        default:
            break;
    }
    value["options"] = options;
    value["state"] = static_cast<unsigned>(state);
    value["held"] = HeldControls(connector.kind, state);
    return value;
}

ControllerResult ControllerControl::ParseOptions(RetroDeviceKind device,
                                                 const std::map<std::string, std::string>& options,
                                                 OverlayOptions& base)
{
    for (const auto& [name, text] : options)
    {
        if (name == "async")
            continue;  // the request's, not the device's
        const char* deviceName = RetroDeviceKindName(device);
        if (name == "port" && device == RetroDeviceKind::SinclairJoystick)
        {
            const std::string v = Trim(text);
            if (v != "1" && v != "2")
                return ControllerResult::Fail(ControllerError::BadRequest, "port '" + text + "': expected 1 or 2");
            base.sinclairPort = static_cast<uint8_t>(v[0] - '0');
        }
        else if (name == "shift" && device == RetroDeviceKind::CursorJoystick)
        {
            if (!ParseBool(text, base.cursorShift))
                return ControllerResult::Fail(ControllerError::BadRequest, "shift '" + text + "': expected on or off");
        }
        else if (name == "keys" && device == RetroDeviceKind::KeysJoystick)
        {
            std::array<ZXKeysEnum, kOverlayControls> keys = base.keys;
            if (!ControllerManager::ParseKeys(text, keys))
            {
                return ControllerResult::Fail(ControllerError::BadRequest,
                                              "keys '" + text + "': expected control:key pairs, controls " +
                                                  "right, left, down, up, fire, fire2, fire3, fire4");
            }
            base.keys = keys;
        }
        else if ((name == "extended" || name == "im2-leak") && device == RetroDeviceKind::KempstonJoystick)
        {
            bool& field = name == "extended" ? base.extended : base.im2Leak;
            if (!ParseBool(text, field))
                return ControllerResult::Fail(ControllerError::BadRequest,
                                              name + " '" + text + "': expected on or off");
        }
        else
        {
            return ControllerResult::Fail(ControllerError::BadRequest,
                                          std::string("option '") + name + "' does not apply to " + deviceName);
        }
    }
    return ControllerResult::Success();
}

/// region <Verbs>

ControllerReply ControllerControl::List()
{
    ControllerReply reply;
    StateNode connectors = StateNode::Array();
    const std::vector<Connector> all = _manager->Connectors();
    for (size_t i = 0; i < all.size(); i++)
        connectors.push(ConnectorValue(all[i], _manager->State(i)));
    reply.body["connectors"] = connectors;
    return reply;
}

ControllerReply ControllerControl::Get(const ControllerRequest& request)
{
    ControllerReply reply;
    size_t index = 0;
    reply.result = Resolve(request.selector, index);
    if (!reply.result.Ok())
        return reply;
    const Connector connector = _manager->At(index);
    reply.connector = connector.id;
    reply.body["info"] = ConnectorValue(connector, _manager->State(index));
    return reply;
}

std::vector<ControllerControl::Target> ControllerControl::TargetsFor(RetroDeviceKind device, int& defaultTarget) const
{
    std::vector<Target> targets;
    std::vector<Target> occupied;
    for (const Connector& connector : _manager->Connectors())
    {
        if (std::find(connector.accepts.begin(), connector.accepts.end(), device) == connector.accepts.end())
            continue;
        Target target;
        target.index = static_cast<size_t>(_manager->Find(connector.id));
        if (connector.fixed)
            target.refusal = connector.id + " is part of the board";
        // Free sockets first, then the ones whose device would be replaced
        (connector.device == RetroDeviceKind::None || connector.fixed ? targets : occupied).push_back(target);
    }
    targets.insert(targets.end(), occupied.begin(), occupied.end());

    defaultTarget = -1;
    for (size_t i = 0; i < targets.size(); i++)
    {
        if (targets[i].refusal.empty())
        {
            defaultTarget = static_cast<int>(i);
            break;
        }
    }
    return targets;
}

ControllerReply ControllerControl::Auto(const ControllerRequest& request)
{
    if (request.options.count("apply") != 0)
        _manager->WatchPortReads(true);
    const PortReadEvidence evidence = _manager->PortReads();
    ControllerReply reply;
    StateNode watch = StateNode::Object();
    watch["active"] = evidence.active;
    watch["frames_observed"] = static_cast<uint64_t>(evidence.framesObserved);
    watch["window"] = static_cast<uint64_t>(evidence.window);
    StateNode classes = StateNode::Object();
    for (size_t index = 0; index < kPortReadClasses; ++index)
    {
        if (evidence.framesWithReads[index] == 0)
            continue;
        StateNode entry = StateNode::Object();
        entry["frames"] = static_cast<uint64_t>(evidence.framesWithReads[index]);
        entry["reads_per_frame"] = static_cast<double>(evidence.readsPerFrame[index]);
        classes[PortReadClassName(static_cast<PortReadClass>(index))] = entry;
    }
    watch["classes"] = classes;
    // row-bits.<half-row>: the bits the code tested after its reads (the sampled AND n / BIT b / shift into carry)
    StateNode rowBits = StateNode::Object();
    for (unsigned row = 0; row < evidence.rowBits.size(); ++row)
    {
        const auto cls = static_cast<PortReadClass>(static_cast<unsigned>(PortReadClass::Row0) + row);
        if (evidence.RowBits(cls) == 0)
            continue;
        StateNode bits = StateNode::Array();
        for (unsigned bit = 0; bit < 8; ++bit)
            if ((evidence.RowBits(cls) >> bit) & 1u)
                bits.push(static_cast<uint64_t>(bit));
        rowBits[PortReadClassName(cls)] = bits;
    }
    watch["row_bits"] = rowBits;
    reply.body["evidence"] = watch;
#ifdef ENABLE_CONTROLLERS
    // The library's heuristic (mapping-and-profiles.md §6.1 step 2): what the Qt host would plug
    const controllers::AutoDecision decision = controllers::DecideFromPortReads(evidence);
    StateNode value = StateNode::Object();
    value["decisive"] = decision.decisive;
    value["keyboard"] = decision.keyboard;
    value["device"] = decision.device.empty() ? StateNode() : StateNode(decision.device);
    value["connector"] = decision.connector.empty() ? StateNode() : StateNode(decision.connector);
    value["label"] = decision.label;
    value["reason"] = decision.reason;
    if (!decision.hint.empty())
        value["hint"] = decision.hint;
    reply.body["decision"] = value;
#endif
    return reply;
}

ControllerReply ControllerControl::Input(const ControllerRequest& request)
{
    ControllerReply reply;
    size_t index = 0;
    reply.result = Resolve(request.selector, index);
    if (!reply.result.Ok())
        return reply;
    const Connector connector = _manager->At(index);
    reply.connector = connector.id;
    if (connector.device == RetroDeviceKind::None)
    {
        reply.result = ControllerResult::Fail(ControllerError::BadRequest, "connector '" + connector.id + "' is empty");
        return reply;
    }
    uint16_t mask = _manager->State(index);
    bool any = false;
    for (const char* option : {"set", "press", "release"})
    {
        auto it = request.options.find(option);
        if (it == request.options.end())
            continue;
        any = true;
        uint16_t bits = 0;
        std::string error;
        if (!ParseControls(it->second, bits, error))
        {
            reply.result = ControllerResult::Fail(ControllerError::BadRequest, std::string(option) + ": " + error);
            return reply;
        }
        if (std::string(option) == "set")
            mask = bits;  // the Next pad keeps its own buttons above the named ones
        else if (std::string(option) == "press")
            mask = static_cast<uint16_t>(mask | bits);
        else
            mask = static_cast<uint16_t>(mask & ~bits);
    }
    if (!any)
    {
        reply.result = ControllerResult::Fail(ControllerError::BadRequest, "input takes press, release or set");
        return reply;
    }
    if (!_manager->Submit(index, mask))
    {
        reply.result =
            ControllerResult::Fail(ControllerError::OwnedByReplay, "input refused: TTD replay or RZX playback owns it");
        return reply;
    }
    reply.body["held"] = HeldControls(connector.kind, mask);
    return reply;
}

ControllerReply ControllerControl::Targets(const ControllerRequest& request)
{
    RetroDeviceKind device = RetroDeviceKind::None;
    if (!RetroDeviceKindFromName(Lower(Trim(request.device)), device) || device == RetroDeviceKind::None)
        return Fail(ControllerError::BadRequest, "targets needs a retro device ('" + request.device + "' is not one)");

    int defaultTarget = -1;
    const std::vector<Target> targets = TargetsFor(device, defaultTarget);
    ControllerReply reply;
    StateNode list = StateNode::Array();
    for (const Target& target : targets)
    {
        const Connector connector = _manager->At(target.index);
        StateNode t = StateNode::Object();
        t["connector"] = connector.id;
        t["occupiedBy"] =
            connector.device == RetroDeviceKind::None ? StateNode() : StateNode(RetroDeviceKindName(connector.device));
        t["refusal"] = target.refusal.empty() ? StateNode() : StateNode(target.refusal);
        list.push(t);
    }
    reply.body["targets"] = list;
    reply.body["default"] = defaultTarget < 0 ? StateNode() : StateNode(static_cast<int64_t>(defaultTarget));
    // A refusal is an answer, not an error: the reply is ok
    std::string refusal;
    if (targets.empty())
        refusal = std::string("no connector of this machine takes ") + RetroDeviceKindName(device);
    else if (defaultTarget < 0)
        refusal = targets.front().refusal;
    reply.body["refusal"] = refusal.empty() ? StateNode() : StateNode(refusal);
    if (defaultTarget >= 0)
        reply.connector = _manager->At(targets[static_cast<size_t>(defaultTarget)].index).id;
    return reply;
}

void ControllerControl::Finish(ControllerReply& reply, const std::map<std::string, std::string>& options)
{
    if (!reply.result.Ok())
        return;
    bool async = false;
    if (auto it = options.find("async"); it != options.end() && !ParseBool(it->second, async))
    {
        reply.result =
            ControllerResult::Fail(ControllerError::BadRequest, "async '" + it->second + "': expected on or off");
        return;
    }
    // While TTD records a plug waits for the frame boundary
    reply.pending = async ? _manager->HasPending() : !_manager->WaitApplied(kSyncTimeoutMs);
}

ControllerReply ControllerControl::Plug(const ControllerRequest& request)
{
    RetroDeviceKind device = RetroDeviceKind::None;
    if (!RetroDeviceKindFromName(Lower(Trim(request.device)), device) || device == RetroDeviceKind::None)
    {
        return Fail(ControllerError::BadRequest,
                    "plug needs a retro device ('" + request.device +
                        "' is not one: kempston-joystick, md6-pad, sinclair-joystick, cursor-joystick, keys-joystick, " +
                        "fuller-joystick)");
    }

    size_t index = 0;
    if (Lower(Trim(request.selector)) == "auto")
    {
        int defaultTarget = -1;
        const std::vector<Target> targets = TargetsFor(device, defaultTarget);
        if (defaultTarget < 0)
        {
            return Fail(ControllerError::Incompatible,
                        targets.empty()
                            ? std::string("no connector of this machine takes ") + RetroDeviceKindName(device)
                            : targets.front().refusal);
        }
        index = targets[static_cast<size_t>(defaultTarget)].index;
    }
    else if (ControllerResult r = Resolve(request.selector, index); !r.Ok())
    {
        ControllerReply reply;
        reply.result = r;
        return reply;
    }

    OverlayOptions options = ControllerManager::DefaultOptions(device);
    if (ControllerResult r = ParseOptions(device, request.options, options); !r.Ok())
        return Fail(r.error, r.message);

    ControllerReply reply;
    reply.result = _manager->Plug(index, device, options);
    Finish(reply, request.options);
    const Connector connector = _manager->At(index);
    reply.connector = connector.id;
    if (reply.result.Ok())
        reply.body["info"] = ConnectorValue(connector, _manager->State(index));
    return reply;
}

ControllerReply ControllerControl::Unplug(const ControllerRequest& request)
{
    ControllerReply reply;
    size_t index = 0;
    reply.result = Resolve(request.selector, index);
    if (!reply.result.Ok())
        return reply;
    reply.result = _manager->Unplug(index);
    Finish(reply, request.options);
    const Connector connector = _manager->At(index);
    reply.connector = connector.id;
    if (reply.result.Ok())
        reply.body["info"] = ConnectorValue(connector, _manager->State(index));
    return reply;
}

ControllerReply ControllerControl::Options(const ControllerRequest& request)
{
    ControllerReply reply;
    size_t index = 0;
    reply.result = Resolve(request.selector, index);
    if (!reply.result.Ok())
        return reply;
    const Connector connector = _manager->At(index);
    reply.connector = connector.id;
    if (connector.device == RetroDeviceKind::None)
    {
        reply.result =
            ControllerResult::Fail(ControllerError::BadRequest, connector.id + " is empty: plug a device first");
        return reply;
    }

    OverlayOptions options = connector.options;
    reply.result = ParseOptions(connector.device, request.options, options);
    if (!reply.result.Ok())
        return reply;
    // The same device with new options: a re-plug (it releases what the device held)
    reply.result = _manager->Plug(index, connector.device, options);
    Finish(reply, request.options);
    if (reply.result.Ok())
        reply.body["info"] = ConnectorValue(_manager->At(index), _manager->State(index));
    return reply;
}

/// endregion </Verbs>

}  // namespace controllers
