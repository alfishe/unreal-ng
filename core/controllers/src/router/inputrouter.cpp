#include "inputrouter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

#include "hostcontrols.h"

namespace controllers
{

namespace
{
constexpr uint16_t kDirections = kRight | kLeft | kDown | kUp;
constexpr float kButtonThreshold = 0.5f;
/// A value below it is released: a control held across a layer switch is seen again
constexpr float kReleased = 0.35f;

const char* const kDpadValues[] = {"dpad.up", "dpad.down", "dpad.left", "dpad.right"};

bool Contains(const std::vector<std::string>& names, const std::string& name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

bool IsLayerKey(const Binding& binding)
{
    return binding.target.kind == TargetKind::Layer;
}

/// Mark every control held now: a layer switch releases them (DT-5 rule 5)
template <typename Values, typename Gates>
void MarkHeldStale(const Values& values, Gates& gates, float released)
{
    for (const auto& [name, value] : values)
    {
        if (std::fabs(value) >= released)
            gates[name].stale = true;
    }
}

/// The targets a sample carries (the rest act on arrival)
bool Sampled(TargetKind kind)
{
    return kind == TargetKind::Retro || kind == TargetKind::Keys || kind == TargetKind::Macro ||
           kind == TargetKind::Mouse;
}
}  // namespace

double EncoderRate::Step(float steps, uint64_t nowNs)
{
    const double count = std::fabs(static_cast<double>(steps));
    const uint64_t interval = _moved && nowNs > _lastNs ? nowNs - _lastNs : kRestNs + 1;
    const double inst = count * 1e9 / static_cast<double>(std::max(interval, kMinNs));
    _rate = interval > kRestNs ? 0.3 * inst : 0.5 * _rate + 0.5 * inst;
    _lastNs = nowNs;
    _moved = true;
    return _rate;
}

InputRouter::InputRouter()
    : _clock([] {
          return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           std::chrono::steady_clock::now().time_since_epoch())
                                           .count());
      })
{
}

/// region <Bindings>

bool InputRouter::Compile(const Binding& binding, CompiledBinding& out, std::string& error)
{
    if (!ValidateBinding(binding, error))
        return false;
    // A named fresh one swapped in: gcc 15 (-O3) takes the destructor of an assigned temporary for a read of its
    // uninitialized strings (-Wmaybe-uninitialized)
    CompiledBinding fresh;
    fresh.binding = binding;
    std::swap(out, fresh);
    if (binding.target.kind == TargetKind::Retro)
    {
        out.mask = binding.target.retro.mask;
        out.directionalTarget = out.mask == kDirections;
    }
    for (const std::string& name : binding.sources)
    {
        CompiledSource source;
        ControlInfo info;
        if (name == "dpad.*")
            source.kind = SourceKind::Dpad;
        else if (!StickOf(name).empty())
        {
            source.kind = SourceKind::Stick;
            source.control = StickOf(name);
        }
        else if (ParseControl(name, info))
        {
            if (!info.base.empty() && info.direction != 0)
            {
                source.kind = SourceKind::StickDirection;
                source.control = info.base;
                source.direction = info.direction;
            }
            else if (!info.base.empty() || info.type == ControlType::Trigger)
            {
                source.kind = SourceKind::Trigger;
                source.control = info.base.empty() ? info.name : info.base;
            }
            else if (info.type == ControlType::Encoder)
            {
                source.kind = SourceKind::Encoder;
                source.control = info.name;
                out.encoder = static_cast<int>(out.sources.size());
            }
            else
            {
                source.kind = info.type == ControlType::Axis ? SourceKind::Axis : SourceKind::Button;
                source.control = info.name;
            }
        }

        switch (source.kind)
        {
            case SourceKind::Stick:
            case SourceKind::StickDirection:
                source.values = {source.control + ".x", source.control + ".y"};
                out.controls.insert(source.control);
                break;
            case SourceKind::Dpad:
                for (const char* value : kDpadValues)
                {
                    source.values.emplace_back(value);
                    out.controls.insert(value);
                }
                break;
            case SourceKind::Axis:
            {
                source.values = {source.control};
                // lstick.x is the stick for an override: a shift layer binding `lstick` takes it too
                const size_t dot = source.control.find('.');
                const std::string stick = dot == std::string::npos ? "" : source.control.substr(0, dot);
                out.controls.insert(stick == "lstick" || stick == "rstick" ? stick : source.control);
                break;
            }
            case SourceKind::Button:
            case SourceKind::Trigger:
            case SourceKind::Encoder:
                source.values = {source.control};
                out.controls.insert(source.control);
                break;
        }
        out.sources.push_back(source);
    }
    return true;
}

bool InputRouter::SetBindings(const std::string& scope, const std::vector<Binding>& bindings, std::string& error)
{
    CompiledSet set;
    set.bindings.resize(bindings.size());
    for (size_t i = 0; i < bindings.size(); i++)
    {
        if (!Compile(bindings[i], set.bindings[i], error))
        {
            error = "binding " + std::to_string(i) + ": " + error;
            return false;
        }
        if (!IsLayerKey(bindings[i]))
            set.liveLayers.insert(bindings[i].layer);
    }
    std::lock_guard<std::mutex> lock(_mutex);
    _bindings[scope] = std::move(set);
    ResetLocked();
    return true;
}

void InputRouter::ClearBindings(const std::string& scope)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _bindings.erase(scope);
    ResetLocked();
}

const InputRouter::CompiledSet* InputRouter::BindingsForLocked(const Device& device) const
{
    for (const std::string& scope : {device.info.id, "family:" + device.info.family, std::string{}})
    {
        const auto it = _bindings.find(scope);
        if (it != _bindings.end())
            return &it->second;
    }
    return nullptr;
}

void InputRouter::ResetLocked()
{
    // The bindings changed: the held states start over from the controls' values, the layers from `play`, the
    // turbo phases from scratch. Actions of the old bindings are not released: their bindings are gone
    std::vector<ActionEvent> dropped;
    for (auto& [id, device] : _devices)
    {
        device.states.clear();
        device.gates.clear();
        device.layers.clear();
        EvaluateLocked(device, Now(), dropped);
    }
    for (auto& [name, instance] : _instances)
        instance.phases.clear();
}

/// endregion </Bindings>

/// region <Devices>

void InputRouter::OnDeviceAdded(const HostDeviceInfo& info)
{
    std::vector<ActionEvent> actions;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        Device& device = _devices[info.id];
        device = Device{};
        device.info = info;
        EvaluateLocked(device, Now(), actions);
    }
    Dispatch(actions);
}

void InputRouter::OnDeviceRemoved(const HostDeviceId& id)
{
    std::vector<ActionEvent> actions;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto it = _devices.find(id);
        if (it == _devices.end())
            return;
        // A held action is released with the device
        const CompiledSet* set = BindingsForLocked(it->second);
        for (size_t i = 0; set && i < it->second.states.size() && i < set->bindings.size(); i++)
        {
            const Target& target = set->bindings[i].binding.target;
            if (target.kind == TargetKind::Action && it->second.states[i].bits != 0)
                actions.push_back({id, target.name, false});
        }
        _devices.erase(it);
        for (auto& [name, instance] : _instances)
        {
            for (auto phase = instance.phases.begin(); phase != instance.phases.end();)
                phase = phase->first.first == id ? instance.phases.erase(phase) : std::next(phase);
        }
    }
    Dispatch(actions);
}

void InputRouter::OnControl(const ControlEvent& event)
{
    std::vector<ActionEvent> actions;
    std::function<void()> wake;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto it = _devices.find(event.device);
        if (it == _devices.end())
            return;
        ControlInfo info;
        if (!ParseControl(event.control, info))
            return;
        const uint64_t now = event.hostTimeNs != 0 ? event.hostTimeNs : Now();
        if (info.type == ControlType::Encoder)
            TurnLocked(it->second, info.name, event.value, now, actions);
        else
        {
            it->second.values[info.name] = event.value;
            EvaluateLocked(it->second, now, actions);
        }
        if (_wake && NextDeadlineLocked() != UINT64_MAX)
            wake = _wake;
    }
    Dispatch(actions);
    if (wake)
        wake();  // a chord window or a repeat waits: the host timer polls at its deadline
}

std::vector<HostDeviceInfo> InputRouter::Devices() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<HostDeviceInfo> list;
    for (const auto& [id, device] : _devices)
        list.push_back(device.info);
    return list;
}

void InputRouter::SetRole(const HostDeviceId& id, DeviceRole role)
{
    std::vector<ActionEvent> actions;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _settings[id].role = role;
        _settings[id].roleSet = true;
        const auto it = _devices.find(id);
        if (it != _devices.end())
        {
            // Another role has other layers: what was held is released
            it->second.layers.clear();
            it->second.epoch++;
            EvaluateLocked(it->second, Now(), actions);
        }
    }
    Dispatch(actions);
}

bool InputRouter::Drives(const HostDeviceId& device, const std::string& instance) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return DrivesLocked(device, instance);
}

DeviceRole InputRouter::Role(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return RoleLocked(device);
}

DeviceRole InputRouter::RoleLocked(const HostDeviceId& device) const
{
    const auto it = _settings.find(device);
    if (it == _settings.end())
        return DeviceRole::Player;
    return it->second.roleSet ? it->second.role : it->second.defaultRole;
}

void InputRouter::SetDefaultRole(const HostDeviceId& id, DeviceRole role)
{
    std::vector<ActionEvent> actions;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const DeviceRole before = RoleLocked(id);
        _settings[id].defaultRole = role;
        const auto it = _devices.find(id);
        if (it != _devices.end() && RoleLocked(id) != before)
        {
            it->second.layers.clear();
            it->second.epoch++;
            EvaluateLocked(it->second, Now(), actions);
        }
    }
    Dispatch(actions);
}

bool InputRouter::RoleSet(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _settings.find(device);
    return it != _settings.end() && it->second.roleSet;
}

void InputRouter::SetInstanceMode(const HostDeviceId& device, InstanceMode mode, const std::string& pinned)
{
    std::lock_guard<std::mutex> lock(_mutex);
    Settings& settings = _settings[device];
    settings.mode = mode;
    settings.pinned = mode == InstanceMode::Pinned ? pinned : std::string{};
}

InstanceMode InputRouter::GetInstanceMode(const HostDeviceId& device, std::string* pinned) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _settings.find(device);
    if (pinned != nullptr)
        *pinned = it != _settings.end() ? it->second.pinned : std::string{};
    return it != _settings.end() ? it->second.mode : InstanceMode::FollowFocus;
}

void InputRouter::Promote(const HostDeviceId& from, const HostDeviceId& to)
{
    std::vector<ActionEvent> actions;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto source = _settings.find(from);
        if (source == _settings.end() || from == to)
            return;
        Settings& target = _settings[to];
        const bool chosen = target.roleSet || target.mode != InstanceMode::FollowFocus || !target.connector.empty();
        if (chosen)
            return;
        const DeviceRole before = RoleLocked(to);
        target.roleSet = source->second.roleSet;
        target.role = source->second.role;
        target.mode = source->second.mode;
        target.pinned = source->second.pinned;
        target.connector = source->second.connector;
        const auto it = _devices.find(to);
        if (it != _devices.end() && RoleLocked(to) != before)
        {
            it->second.layers.clear();
            it->second.epoch++;
            EvaluateLocked(it->second, Now(), actions);
        }
    }
    Dispatch(actions);
}

void InputRouter::SetPlayerConnector(const HostDeviceId& device, const std::string& connector)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _settings[device].connector = connector;
}

std::string InputRouter::PlayerConnector(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _settings.find(device);
    return it != _settings.end() ? it->second.connector : std::string{};
}

std::vector<std::string> InputRouter::Connectors(const HostDeviceId& id) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<std::string> connectors;
    const auto it = _devices.find(id);
    if (it == _devices.end() || RoleLocked(id) != DeviceRole::Player)
        return connectors;
    const CompiledSet* set = BindingsForLocked(it->second);
    if (set == nullptr)
        return connectors;
    const auto settings = _settings.find(id);
    const std::string player = settings == _settings.end() ? std::string{} : settings->second.connector;
    for (const CompiledBinding& compiled : set->bindings)
    {
        const Target& target = compiled.binding.target;
        if (target.kind != TargetKind::Retro || compiled.binding.layer == kDebugLayer)
            continue;
        const std::string& connector = target.retro.connector == "auto" ? player : target.retro.connector;
        if (!connector.empty() && std::find(connectors.begin(), connectors.end(), connector) == connectors.end())
            connectors.push_back(connector);
    }
    return connectors;
}

bool InputRouter::Analog(const HostDeviceId& id, const std::string& control, AnalogOptions& out) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    out = AnalogOptions{};
    const auto it = _devices.find(id);
    if (it == _devices.end())
        return false;
    const CompiledSet* set = BindingsForLocked(it->second);
    if (set == nullptr)
        return false;
    const std::vector<std::string> layers = ActiveLayersLocked(it->second);
    // Top layer first: what a press reads now
    for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer)
        for (const CompiledBinding& compiled : set->bindings)
        {
            if (compiled.binding.layer != *layer)
                continue;
            for (const CompiledSource& source : compiled.sources)
                if (source.control == control || source.control.rfind(control + ".", 0) == 0)
                {
                    out = compiled.binding.options.analog;
                    return true;
                }
        }
    return false;
}

void InputRouter::SetFocus(const std::string& instance)
{
    std::vector<ActionEvent> actions;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (instance == _focus)
            return;
        // Another instance may have another context: its context layers
        std::map<HostDeviceId, std::vector<std::string>> before;
        for (const auto& [id, device] : _devices)
            before[id] = ActiveLayersLocked(device);
        _focus = instance;
        const uint64_t now = Now();
        for (auto& [id, device] : _devices)
        {
            if (ActiveLayersLocked(device) == before[id])
                continue;
            device.epoch++;
            MarkHeldStale(device.values, device.gates, kReleased);
            EvaluateLocked(device, now, actions);
        }
    }
    Dispatch(actions);
}

void InputRouter::SetDebugFocus(bool on)
{
    std::vector<ActionEvent> actions;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (on == _debugFocus)
            return;
        _debugFocus = on;
        const uint64_t now = Now();
        for (auto& [id, device] : _devices)
        {
            if (RoleLocked(id) != DeviceRole::Player)
                continue;
            // Other layers: what was held is released, a control held across the switch waits for its release
            device.layers.clear();
            device.epoch++;
            MarkHeldStale(device.values, device.gates, kReleased);
            EvaluateLocked(device, now, actions);
        }
    }
    Dispatch(actions);
}

std::vector<std::string> InputRouter::Layers(const HostDeviceId& id) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _devices.find(id);
    if (it == _devices.end())
        return {};
    return ActiveLayersLocked(it->second);
}

bool InputRouter::Consumes(const HostDeviceId& id, const std::string& control) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _devices.find(id);
    if (it == _devices.end() || RoleLocked(id) == DeviceRole::Disabled)
        return false;
    const Device& device = it->second;
    const CompiledSet* set = BindingsForLocked(device);
    if (set == nullptr || device.states.size() != set->bindings.size())
        return false;
    const std::vector<std::string> active = ActiveLayersLocked(device);
    for (size_t i = 0; i < set->bindings.size(); i++)
    {
        const CompiledBinding& compiled = set->bindings[i];
        const Binding& binding = compiled.binding;
        // A layer key is the layer's alone; a binding only while its layer acts
        const bool layerKey = IsLayerKey(binding) && set->liveLayers.count(binding.target.name) != 0 &&
                              (binding.layer == kPlayLayer || Contains(active, binding.layer));
        if (!layerKey && !(device.states[i].active && binding.options.consume))
            continue;
        for (const CompiledSource& source : compiled.sources)
        {
            if (Contains(source.values, control))
                return true;
        }
    }
    return false;
}

float InputRouter::Value(const Device& device, const std::string& control)
{
    const auto it = device.values.find(control);
    if (it != device.values.end())
        return it->second;
    // A device whose trigger is only a switch may send `lt.click` alone (older layouts, a driver's digital L2):
    // `lt` and the `lt.click` derived from it read that switch at full travel
    if (control == "lt" || control == "rt")
    {
        const auto click = device.values.find(control + ".click");
        if (click != device.values.end())
            return click->second;
    }
    return 0.0f;
}

std::vector<std::string> InputRouter::ActiveLayersLocked(const Device& device) const
{
    const DeviceRole role = RoleLocked(device.info.id);
    if (role == DeviceRole::Disabled)
        return {};
    std::vector<std::string> layers = {kPlayLayer};
    if (RemoteLocked(device))
        layers = {kDebugLayer};
    else
    {
        for (const LayerEntry& entry : device.layers)
        {
            // `debug` is opaque: what it does not bind does nothing while it is up
            if (entry.name == kDebugLayer)
                layers.clear();
            layers.push_back(entry.name);
        }
    }
    // The context layer sits right above `debug` (profiles.md §1): a shift layer raised over debug stays on top
    const auto debug = std::find(layers.begin(), layers.end(), std::string(kDebugLayer));
    if (debug != layers.end())
    {
        const std::string context = ContextLayerLocked(device);
        if (!context.empty())
            layers.insert(debug + 1, context);
    }
    return layers;
}

const std::string& InputRouter::InstanceLocked(const HostDeviceId& device) const
{
    const auto it = _settings.find(device);
    if (it != _settings.end() && it->second.mode == InstanceMode::Pinned)
        return it->second.pinned;
    return _focus;
}

const SurfaceHaptics* InputRouter::SurfaceHapticsForLocked(const Device& device) const
{
    for (const std::string& scope : {device.info.id, "family:" + device.info.family, std::string{}})
    {
        const auto it = _surfaceHaptics.find(scope);
        if (it != _surfaceHaptics.end())
            return &it->second;
    }
    return nullptr;
}

std::string InputRouter::ContextLayerLocked(const Device& device) const
{
    const auto context = _contexts.find(InstanceLocked(device.info.id));
    if (context == _contexts.end() || context->second.empty())
        return {};
    std::string best;
    int length = -1;
    auto consider = [&](const std::string& layer) {
        const int match = ContextLayerMatch(layer, context->second);
        if (match > length)
        {
            length = match;
            best = layer;
        }
    };
    if (const CompiledSet* set = BindingsForLocked(device))
        for (const std::string& layer : set->liveLayers)
            consider(layer);
    if (const SurfaceHaptics* haptics = SurfaceHapticsForLocked(device))
        for (const auto& [layer, feel] : haptics->layers)
            consider(layer);
    return best;
}

bool InputRouter::RemoteLocked(const Device& device) const
{
    const DeviceRole role = RoleLocked(device.info.id);
    return role == DeviceRole::DebuggerRemote || (_debugFocus && role == DeviceRole::Player);
}

bool InputRouter::RaisedLocked(const Device& device, const std::string& layer) const
{
    if (IsContextLayer(layer))
        return layer == ContextLayerLocked(device) && RaisedLocked(device, kDebugLayer);
    if (layer == kDebugLayer && RemoteLocked(device))
        return true;
    return layer == kPlayLayer || std::any_of(device.layers.begin(), device.layers.end(),
                                              [&layer](const LayerEntry& entry) { return entry.name == layer; });
}

uint16_t InputRouter::SourceBits(const CompiledBinding& compiled, BindingState& state, const ValueOf& value)
{
    const AnalogOptions& analog = compiled.binding.options.analog;
    if (state.sources.size() != compiled.sources.size())
        state.sources.assign(compiled.sources.size(), SourceState{});

    uint16_t bits = 0;
    bool all = true;
    for (size_t j = 0; j < compiled.sources.size(); j++)
    {
        const CompiledSource& source = compiled.sources[j];
        SourceState& held = state.sources[j];
        bool directional = false;
        switch (source.kind)
        {
            case SourceKind::Button:
                held.on = value(source.control) >= kButtonThreshold;
                break;
            case SourceKind::Trigger:
                held.on = TriggerClick(value(source.control), held.on, analog);
                break;
            case SourceKind::Axis:
            {
                const float deflection = std::fabs(value(source.control));
                held.on = deflection >= (held.on ? analog.off : analog.on);
                break;
            }
            case SourceKind::Stick:
            case SourceKind::StickDirection:
                held.dirs = StickDirections(value(source.values[0]), value(source.values[1]), held.dirs, analog);
                held.on = source.kind == SourceKind::Stick ? held.dirs != 0 : (held.dirs & source.direction) != 0;
                directional = source.kind == SourceKind::Stick;
                break;
            case SourceKind::Dpad:
                held.dirs = static_cast<uint16_t>((value("dpad.up") >= kButtonThreshold ? kUp : 0) |
                                                  (value("dpad.down") >= kButtonThreshold ? kDown : 0) |
                                                  (value("dpad.left") >= kButtonThreshold ? kLeft : 0) |
                                                  (value("dpad.right") >= kButtonThreshold ? kRight : 0));
                held.on = held.dirs != 0;
                directional = true;
                break;
            case SourceKind::Encoder:
                held.on = false;  // steps, never held: TurnLocked
                break;
        }
        all = all && held.on;
        if (directional && compiled.directionalTarget)
            bits |= held.dirs;
        else if (held.on)
            bits |= compiled.mask;
    }
    if (compiled.binding.chord)
        return all ? compiled.mask : uint16_t{0};
    return bits;
}

void InputRouter::Settle(BindingState& state, uint16_t bits)
{
    if (bits != 0 && state.bits == 0)
    {
        state.presses++;
        state.pressBits = bits;
    }
    state.bits = bits;
}

bool InputRouter::UpdateLayersLocked(Device& device, const CompiledSet& set, const ValueOf& raw)
{
    bool changed = false;
    const bool player = RoleLocked(device.info.id) == DeviceRole::Player;
    for (size_t i = 0; i < set.bindings.size(); i++)
    {
        const CompiledBinding& compiled = set.bindings[i];
        const Binding& binding = compiled.binding;
        BindingState& state = device.states[i];
        // A key to a layer with no bindings is an ordinary button; a debugger remote has no layer keys
        if (!IsLayerKey(binding) || set.liveLayers.count(binding.target.name) == 0 || !player)
            continue;
        // A layer key acts from its layer while that is up, `debug` or not: the key that raised it must drop it
        const bool up = RaisedLocked(device, binding.layer);
        const bool on = up && (binding.chord ? state.chordOn : SourceBits(compiled, state, raw) != 0);
        const bool edge = on && !state.layerOn;
        state.layerOn = on;
        state.active = up;

        const auto raised = std::find_if(device.layers.begin(), device.layers.end(),
                                         [&](const LayerEntry& e) { return e.binding == i; });
        const bool isRaised = raised != device.layers.end();
        bool raise = false;
        bool drop = false;
        switch (binding.target.layerMode)
        {
            case LayerMode::Hold:
                raise = on && !isRaised;
                drop = !on && isRaised;
                break;
            case LayerMode::Toggle:
                raise = edge && !isRaised;
                drop = edge && isRaised;
                break;
            case LayerMode::Oneshot:
                raise = edge && !isRaised;
                break;
        }
        if (raise)
            device.layers.push_back({binding.target.name, i, binding.target.layerMode, false});
        else if (drop)
            device.layers.erase(raised);
        changed = changed || raise || drop;
    }
    return changed;
}

void InputRouter::EvaluateLocked(Device& device, uint64_t now, std::vector<ActionEvent>& actions)
{
    const CompiledSet* set = BindingsForLocked(device);
    if (set == nullptr)
    {
        device.states.clear();
        device.layers.clear();
        FeelLocked(device);
        return;
    }
    const std::vector<CompiledBinding>& bindings = set->bindings;
    if (device.states.size() != bindings.size())
        device.states.assign(bindings.size(), BindingState{});
    const ValueOf raw = [&device](const std::string& name) { return Value(device, name); };

    // A control held across a layer switch is seen again once released
    for (auto& [name, gate] : device.gates)
    {
        if (gate.stale && std::fabs(Value(device, name)) < kReleased)
            gate.stale = false;
    }

    // Chord members: a press alone opens the window, a release inside it gives the press to the member's own
    // bindings at once (a tap still works, one release later). A button held with an encoder waits until released:
    // a turn meanwhile takes its press (TurnLocked)
    std::map<std::string, bool> members;  // -> held with an encoder
    const std::vector<std::string> up = ActiveLayersLocked(device);
    for (const CompiledBinding& compiled : bindings)
    {
        const bool turn = compiled.encoder >= 0;
        if (!compiled.binding.chord || !(turn ? Contains(up, compiled.binding.layer)
                                              : RaisedLocked(device, compiled.binding.layer)))
            continue;
        for (const CompiledSource& source : compiled.sources)
            if (source.kind != SourceKind::Encoder)
                members[source.control] = members[source.control] || turn;
    }
    for (const auto& [member, turn] : members)
    {
        Gate& gate = device.gates[member];
        const bool pressed = Value(device, member) >= kButtonThreshold;
        if (pressed && !gate.pressed && !gate.consumed)
        {
            gate.pending = true;
            gate.until = turn ? UINT64_MAX : now + kChordWindowNs;
        }
        if (!pressed && gate.pressed)
        {
            gate.owed = gate.pending;
            gate.pending = false;
            gate.consumed = false;
        }
        gate.pressed = pressed;
        if (gate.pending && now >= gate.until)
            gate.pending = false;
    }
    for (size_t i = 0; i < bindings.size(); i++)
    {
        const CompiledBinding& compiled = bindings[i];
        if (!compiled.binding.chord || compiled.encoder >= 0)
            continue;
        BindingState& state = device.states[i];
        bool all = true;
        bool fresh = true;
        for (const CompiledSource& source : compiled.sources)
        {
            const Gate& gate = device.gates[source.control];
            all = all && gate.pressed;
            fresh = fresh && gate.pending;
        }
        if (state.chordOn)
            state.chordOn = all;
        else if (all && fresh && RaisedLocked(device, compiled.binding.layer))
        {
            state.chordOn = true;
            for (const CompiledSource& source : compiled.sources)
            {
                Gate& gate = device.gates[source.control];
                gate.pending = false;
                gate.consumed = true;
            }
        }
    }

    // Layer keys; a switch releases what is held (DT-5 rule 5)
    if (UpdateLayersLocked(device, *set, raw))
    {
        device.epoch++;
        MarkHeldStale(device.values, device.gates, kReleased);
    }

    // What the layer keys take: their controls reach no other binding
    const std::vector<std::string> active = ActiveLayersLocked(device);
    std::set<std::string> layerKeyValues;
    for (size_t i = 0; i < bindings.size(); i++)
    {
        const CompiledBinding& compiled = bindings[i];
        if (IsLayerKey(compiled.binding) && !compiled.binding.chord && device.states[i].active)
        {
            for (const CompiledSource& source : compiled.sources)
                layerKeyValues.insert(source.values.begin(), source.values.end());
        }
    }
    bool anyOwed = false;
    const ValueOf effective = [&](const std::string& name) {
        if (layerKeyValues.count(name) != 0)
            return 0.0f;
        const auto gate = device.gates.find(name);
        if (gate != device.gates.end() && (gate->second.pending || gate->second.consumed || gate->second.stale))
            return 0.0f;
        return Value(device, name);
    };
    const ValueOf owed = [&](const std::string& name) {
        const auto gate = device.gates.find(name);
        if (gate != device.gates.end() && gate->second.owed)
            return 1.0f;
        return effective(name);
    };
    for (const auto& [name, gate] : device.gates)
        anyOwed = anyOwed || gate.owed;

    auto layerIndex = [&active](const std::string& layer) {
        const auto it = std::find(active.begin(), active.end(), layer);
        return it == active.end() ? -1 : static_cast<int>(it - active.begin());
    };
    for (size_t i = 0; i < bindings.size(); i++)
    {
        const CompiledBinding& compiled = bindings[i];
        const Binding& binding = compiled.binding;
        if (IsLayerKey(binding))
            continue;
        BindingState& state = device.states[i];
        const int level = layerIndex(binding.layer);
        bool active = level >= 0;
        // A higher layer binding the same control overrides this one
        for (size_t j = 0; active && j < bindings.size(); j++)
        {
            const CompiledBinding& other = bindings[j];
            if (j == i || IsLayerKey(other.binding) || layerIndex(other.binding.layer) <= level)
                continue;
            active = std::none_of(compiled.controls.begin(), compiled.controls.end(),
                                  [&other](const std::string& c) { return other.controls.count(c) != 0; });
        }
        const uint16_t was = state.bits;
        const uint32_t presses = state.presses;
        state.active = active;
        state.epoch = device.epoch;
        if (compiled.encoder >= 0)
        {
            state.bits = 0;  // it acts when the encoder turns
            continue;
        }
        if (!active)
            state.bits = 0;
        else if (binding.chord)
            Settle(state, state.chordOn ? compiled.mask : uint16_t{0});
        else
        {
            // A member released inside its chord window: the press, then the release
            if (anyOwed)
                Settle(state, SourceBits(compiled, state, owed));
            Settle(state, SourceBits(compiled, state, effective));
        }

        if (binding.target.kind == TargetKind::Action && binding.options.level)
        {
            // The scrub curve's input: the level, each time it changes
            float level = 0;
            if (active)
                for (const CompiledSource& source : compiled.sources)
                    for (const std::string& value : source.values)
                        level = std::max(level, std::min(1.0f, std::fabs(effective(value))));
            if (level != state.level)
            {
                state.level = level;
                actions.push_back({device.info.id, binding.target.name, level > 0, level});
            }
        }
        else if (binding.target.kind == TargetKind::Action)
        {
            const bool pressed = state.presses != presses;
            if (pressed)
            {
                actions.push_back({device.info.id, binding.target.name, true, 1});
                // `repeat`: held, it runs again after the delay (ExpireLocked)
                state.repeating = binding.options.repeatDelay != 0;
                state.repeatAt = now + binding.options.repeatDelay * kRepeatFrameNs;
            }
            if ((was != 0 || pressed) && state.bits == 0)
                actions.push_back({device.info.id, binding.target.name, false, 0});
            if (state.bits == 0)
                state.repeating = false;
        }
        // A oneshot layer is used once one of its controls was pressed
        if (state.bits != 0)
        {
            for (LayerEntry& entry : device.layers)
            {
                if (entry.mode == LayerMode::Oneshot && entry.name == binding.layer)
                    entry.used = true;
            }
        }
    }
    for (auto& [name, gate] : device.gates)
        gate.owed = false;

    // A used oneshot layer drops once its controls are released
    const size_t layersBefore = device.layers.size();
    device.layers.erase(std::remove_if(device.layers.begin(), device.layers.end(),
                                       [&](const LayerEntry& entry) {
                                           if (entry.mode != LayerMode::Oneshot || !entry.used)
                                               return false;
                                           for (size_t i = 0; i < bindings.size(); i++)
                                           {
                                               if (bindings[i].binding.layer == entry.name &&
                                                   device.states[i].bits != 0)
                                                   return false;
                                           }
                                           return true;
                                       }),
                        device.layers.end());
    if (device.layers.size() != layersBefore)
    {
        device.epoch++;
        MarkHeldStale(device.values, device.gates, kReleased);
        // Once more with the layers left: the bindings it hid act again (the layers only shrink: this ends)
        EvaluateLocked(device, now, actions);
        return;
    }
    FeelLocked(device);
}

void InputRouter::FeelLocked(Device& device)
{
    const SurfaceHaptics* haptics = SurfaceHapticsForLocked(device);
    if (haptics == nullptr && !device.feelSent)
        return;  // nothing to say to it
    // The top layer with a feel; none (play, a disabled device): clicks off, the steps as they were
    StepsOut feel{device.feelSent ? device.feel.perTurn : StepsOut{}.perTurn, false};
    const std::vector<std::string> active = ActiveLayersLocked(device);
    for (auto layer = active.rbegin(); haptics != nullptr && layer != active.rend(); ++layer)
        if (const LayerHaptics* found = haptics->Find(*layer))
        {
            feel = found->steps;
            break;
        }
    if (device.feelSent && feel == device.feel)
        return;
    device.feelSent = true;
    device.feel = feel;
    _feelQueue.emplace_back(device.info.id, feel);
}

void InputRouter::TurnLocked(Device& device, const std::string& control, float steps, uint64_t now,
                             std::vector<ActionEvent>& actions)
{
    const CompiledSet* set = BindingsForLocked(device);
    if (steps == 0 || set == nullptr)
        return;
    if (device.states.size() != set->bindings.size())
        EvaluateLocked(device, now, actions);
    int best = -1;
    size_t most = 0;
    for (size_t i = 0; i < set->bindings.size() && i < device.states.size(); i++)
    {
        const CompiledBinding& compiled = set->bindings[i];
        if (compiled.encoder < 0 || !device.states[i].active ||
            compiled.sources[static_cast<size_t>(compiled.encoder)].control != control)
            continue;
        bool held = true;
        const size_t buttons = compiled.sources.size() - 1;
        for (const CompiledSource& source : compiled.sources)
            if (source.kind != SourceKind::Encoder)
                held = held && Value(device, source.control) >= kButtonThreshold;
        if (held && (best < 0 || buttons > most))
        {
            best = static_cast<int>(i);
            most = buttons;
        }
    }
    // The speed counts every turn, bound or not: a binding that comes into force mid-spin starts at the right speed
    const double rate = device.rates[control].Step(steps, now);
    if (best < 0)
        return;
    const CompiledBinding& chosen = set->bindings[static_cast<size_t>(best)];
    // The buttons held for the turn: their own bindings never see this press
    for (const CompiledSource& source : chosen.sources)
    {
        if (source.kind == SourceKind::Encoder)
            continue;
        Gate& gate = device.gates[source.control];
        gate.pending = false;
        gate.consumed = true;
    }
    ActionEvent event{device.info.id, chosen.binding.target.name, true, steps};
    event.turn = true;
    event.rate = rate;
    event.params = chosen.binding.options.params;
    actions.push_back(std::move(event));
}

void InputRouter::ExpireLocked(uint64_t now, std::vector<ActionEvent>& actions)
{
    for (auto& [id, device] : _devices)
    {
        const bool due = std::any_of(device.gates.begin(), device.gates.end(), [now](const auto& entry) {
            return entry.second.pending && now >= entry.second.until;
        });
        if (due)
            EvaluateLocked(device, now, actions);
        // The held actions' repeats: one run per deadline, a late poll does not catch up in a burst
        const CompiledSet* set = BindingsForLocked(device);
        if (set == nullptr || device.states.size() != set->bindings.size())
            continue;
        for (size_t i = 0; i < device.states.size(); i++)
        {
            BindingState& state = device.states[i];
            if (!state.repeating || now < state.repeatAt)
                continue;
            const Binding& binding = set->bindings[i].binding;
            if (!state.active || state.bits == 0 || binding.options.repeatRate == 0)
            {
                state.repeating = false;
                continue;
            }
            actions.push_back({device.info.id, binding.target.name, true, 1});
            const uint64_t rate = binding.options.repeatRate * kRepeatFrameNs;
            state.repeatAt += rate;
            if (state.repeatAt <= now)
                state.repeatAt = now + rate;
        }
    }
}

uint64_t InputRouter::NextDeadlineLocked() const
{
    uint64_t next = UINT64_MAX;
    for (const auto& [id, device] : _devices)
    {
        for (const auto& [name, gate] : device.gates)
            if (gate.pending)
                next = std::min(next, gate.until);
        for (const BindingState& state : device.states)
            if (state.repeating)
                next = std::min(next, state.repeatAt);
    }
    return next;
}

uint64_t InputRouter::NextDeadline() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return NextDeadlineLocked();
}

uint64_t InputRouter::NowNs() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return Now();
}

void InputRouter::SetWakeSink(std::function<void()> wake)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _wake = std::move(wake);
}

bool InputRouter::DrivesLocked(const HostDeviceId& device, const std::string& instance) const
{
    const auto it = _settings.find(device);
    if (it == _settings.end())
        return _focus.empty() || _focus == instance;
    const Settings& settings = it->second;
    if (RoleLocked(device) != DeviceRole::Player)
        return false;
    switch (settings.mode)
    {
        case InstanceMode::All:
            return true;
        case InstanceMode::Pinned:
            return settings.pinned == instance;
        case InstanceMode::FollowFocus:
            break;
    }
    return _focus.empty() || _focus == instance;
}

/// endregion </Devices>

/// region <Actions and time>

void InputRouter::SetActionSink(ActionSink sink)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _actions = std::move(sink);
}

void InputRouter::SetClock(Clock clock)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _clock = std::move(clock);
}

void InputRouter::Poll()
{
    std::vector<ActionEvent> actions;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ExpireLocked(Now(), actions);
    }
    Dispatch(actions);
}

uint64_t InputRouter::Now() const
{
    return _clock ? _clock() : 0;
}

void InputRouter::Dispatch(const std::vector<ActionEvent>& actions) const
{
    ActionSink sink;
    OutputSink outputs;
    std::vector<std::pair<HostDeviceId, StepsOut>> feel;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        sink = _actions;
        outputs = _outputs;
        feel.swap(_feelQueue);
    }
    if (outputs)
        for (const auto& [device, steps] : feel)
            outputs(device, steps);
    if (!sink)
        return;
    for (const ActionEvent& action : actions)
        sink(action);
}

void InputRouter::SetOutputSink(OutputSink sink)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _outputs = std::move(sink);
}

/// endregion </Actions and time>

/// region <Contexts and surface feedback>

void InputRouter::SetContext(const std::string& instance, const std::string& context)
{
    std::vector<ActionEvent> actions;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto it = _contexts.find(instance);
        if (it != _contexts.end() && it->second == context)
            return;
        std::map<HostDeviceId, std::vector<std::string>> before;
        for (const auto& [id, device] : _devices)
            before[id] = ActiveLayersLocked(device);
        _contexts[instance] = context;
        const uint64_t now = Now();
        for (auto& [id, device] : _devices)
        {
            if (ActiveLayersLocked(device) == before[id])
                continue;
            // Another context layer: what was held is released, a control held across it waits for its release
            device.epoch++;
            MarkHeldStale(device.values, device.gates, kReleased);
            EvaluateLocked(device, now, actions);
        }
    }
    Dispatch(actions);
}

std::string InputRouter::Context(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _contexts.find(InstanceLocked(device));
    return it == _contexts.end() ? std::string{} : it->second;
}

void InputRouter::SetSurfaceHaptics(const std::string& scope, SurfaceHaptics haptics)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _surfaceHaptics[scope] = std::move(haptics);
        for (auto& [id, device] : _devices)
            FeelLocked(device);
    }
    Dispatch({});
}

void InputRouter::ClearSurfaceHaptics(const std::string& scope)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_surfaceHaptics.erase(scope) == 0)
            return;
        for (auto& [id, device] : _devices)
            FeelLocked(device);
    }
    Dispatch({});
}

bool InputRouter::Feel(const HostDeviceId& id, LayerHaptics& out) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _devices.find(id);
    if (it == _devices.end())
        return false;
    const SurfaceHaptics* haptics = SurfaceHapticsForLocked(it->second);
    if (haptics == nullptr)
        return false;
    const std::vector<std::string> active = ActiveLayersLocked(it->second);
    for (auto layer = active.rbegin(); layer != active.rend(); ++layer)
        if (const LayerHaptics* found = haptics->Find(*layer))
        {
            out = *found;
            return true;
        }
    return false;
}

/// endregion </Contexts and surface feedback>

/// region <Actions and time, continued>

/// endregion </Actions and time>

/// region <Sampling>

uint16_t InputRouter::Output(const CompiledBinding& compiled, const BindingState& state, Phase& phase, uint64_t frame)
{
    const BindingOptions& options = compiled.binding.options;
    const uint16_t mask = compiled.mask;
    // A press since the last sample, even one released already: the game still sees it for a frame
    const bool edge = state.presses != phase.presses;
    phase.presses = state.presses;
    if (frame < phase.since)  // a seek went back past the press
        phase.since = phase.until = frame;
    const uint16_t bits = state.bits != 0 ? state.bits : (edge ? state.pressBits : uint16_t{0});
    const unsigned period = options.on + options.off;
    auto pulse = [&]() { return period != 0 && (frame - phase.since) % period < options.on; };

    if (options.press == PressMode::Tap)
    {
        if (edge)
        {
            phase.since = frame;
            phase.until = frame + options.frames;
            phase.tapBits = state.pressBits;
        }
        return frame < phase.until ? phase.tapBits : uint16_t{0};
    }

    switch (options.turbo)
    {
        case Turbo::Off:
            if (options.repeatDelay == 0)
                return bits;
            // `repeat`: held, released for a frame after the delay and then every `rate` frames
            if (edge)
                phase.since = frame;
            if (state.bits == 0 || frame - phase.since < options.repeatDelay)
                return bits;
            return (frame - phase.since - options.repeatDelay) % options.repeatRate == 0 ? uint16_t{0} : bits;
        case Turbo::Classic:
            if (edge)
                phase.since = frame;
            return bits != 0 && pulse() ? bits : uint16_t{0};
        case Turbo::Toggle:
            if (edge)
            {
                phase.latched = !phase.latched;
                phase.since = frame;
            }
            return phase.latched && pulse() ? mask : uint16_t{0};
        case Turbo::Hold:
            if (edge)
                phase.latched = !phase.latched;
            return phase.latched ? mask : uint16_t{0};
        case Turbo::Burst:
            if (edge)
            {
                phase.since = frame;
                phase.until = frame + static_cast<uint64_t>(options.count) * period;
            }
            return frame < phase.until && pulse() ? mask : uint16_t{0};
    }
    return bits;
}

void InputRouter::Sample(const std::string& instanceName, uint64_t frame, HostSample& out)
{
    std::vector<ActionEvent> actions;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ExpireLocked(Now(), actions);

        Instance& instance = _instances[instanceName];
        std::map<std::string, uint16_t> masks;
        std::set<uint8_t> keys;
        auto hold = [&keys](const std::vector<uint8_t>& held) { keys.insert(held.begin(), held.end()); };
        int dx = 0;
        int dy = 0;
        for (auto& [id, device] : _devices)
        {
            if (!DrivesLocked(id, instanceName))
                continue;
            const CompiledSet* set = BindingsForLocked(device);
            if (set == nullptr || device.states.size() != set->bindings.size())
                continue;
            const auto settings = _settings.find(id);
            const std::string player = settings == _settings.end() ? std::string{} : settings->second.connector;
            for (size_t i = 0; i < set->bindings.size(); i++)
            {
                const CompiledBinding& compiled = set->bindings[i];
                const Target& target = compiled.binding.target;
                if (!Sampled(target.kind))
                    continue;
                const BindingState& state = device.states[i];
                Phase& phase = instance.phases[{id, i}];
                if (!state.active || phase.epoch != state.epoch)
                {
                    // A layer switch released it: autohold, toggles, taps and macros end with the layer
                    phase.epoch = state.epoch;
                    phase.latched = false;
                    phase.until = 0;
                    phase.remainder = 0.0f;
                    if (!state.active)
                    {
                        phase.presses = state.presses;
                        continue;
                    }
                }

                switch (target.kind)
                {
                    case TargetKind::Retro:
                    {
                        const uint16_t bits = Output(compiled, state, phase, frame);
                        const std::string& connector =
                            target.retro.connector == "auto" ? player : target.retro.connector;
                        if (!connector.empty())
                            masks[connector] |= bits;
                        break;
                    }
                    case TargetKind::Keys:
                        if (Output(compiled, state, phase, frame) != 0)
                            hold(target.keys);
                        break;
                    case TargetKind::Macro:
                    {
                        // Each step held one frame and released one, so the ROM's scan sees every key; a press
                        // while it runs is ignored
                        const bool edge = state.presses != phase.presses;
                        phase.presses = state.presses;
                        if (edge && frame >= phase.until)
                        {
                            phase.since = frame;
                            phase.until = frame + 2 * target.steps.size();
                        }
                        if (frame < phase.until && frame >= phase.since && (frame - phase.since) % 2 == 0)
                            hold(target.steps[(frame - phase.since) / 2]);
                        break;
                    }
                    case TargetKind::Mouse:
                        switch (target.mouse)
                        {
                            case MouseControl::Left:
                            case MouseControl::Right:
                            case MouseControl::Middle:
                                if (Output(compiled, state, phase, frame) != 0)
                                    out.mouse.buttons |= static_cast<uint8_t>(1u << static_cast<unsigned>(target.mouse));
                                break;
                            case MouseControl::Wheel:
                                if (state.presses != phase.presses)
                                    out.mouse.wheel += static_cast<int>(std::lround(target.scale));
                                phase.presses = state.presses;
                                break;
                            case MouseControl::Dx:
                            case MouseControl::Dy:
                            {
                                float amount = 0.0f;
                                for (size_t j = 0; j < compiled.sources.size(); j++)
                                {
                                    const CompiledSource& source = compiled.sources[j];
                                    if (source.kind == SourceKind::Axis)
                                    {
                                        const auto gate = device.gates.find(source.control);
                                        const bool gated = gate != device.gates.end() && gate->second.stale;
                                        amount += gated ? 0.0f
                                                        : MouseCurve(Value(device, source.control),
                                                                     compiled.binding.options.analog);
                                    }
                                    else if (j < state.sources.size() && state.sources[j].on)
                                        amount += 1.0f;
                                }
                                phase.remainder += amount * target.scale;
                                const int counts = static_cast<int>(phase.remainder);
                                phase.remainder -= static_cast<float>(counts);
                                // The profile's dy is + down; the machine's mouse + up
                                if (target.mouse == MouseControl::Dx)
                                    dx += counts;
                                else
                                    dy -= counts;
                                break;
                            }
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        std::set<std::string> held;
        for (const auto& [connector, bits] : masks)
        {
            if (bits != 0)
            {
                out.connectors.push_back({connector, bits});
                held.insert(connector);
            }
        }
        for (const std::string& connector : instance.held)
        {
            if (held.count(connector) == 0)
                out.connectors.push_back({connector, 0});
        }
        instance.held = std::move(held);
        out.keys.assign(keys.begin(), keys.end());
        out.mouse.dx += dx;
        out.mouse.dy += dy;
    }
    Dispatch(actions);
}

void InputRouter::ForgetInstance(const std::string& instance)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _instances.erase(instance);
}

/// endregion </Sampling>

}  // namespace controllers
