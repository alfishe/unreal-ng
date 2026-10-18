#include "inputrouter.h"

#include <cmath>

#include "hostcontrols.h"

namespace controllers
{

namespace
{
constexpr uint16_t kDirections = kRight | kLeft | kDown | kUp;
constexpr float kButtonThreshold = 0.5f;
}  // namespace

/// region <Bindings>

bool InputRouter::Compile(const Binding& binding, CompiledBinding& out, std::string& error)
{
    if (!ValidateBinding(binding, error))
        return false;
    out = CompiledBinding{};
    out.binding = binding;
    out.directionalTarget = binding.target.mask == kDirections;
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
            else
            {
                source.kind = info.type == ControlType::Axis ? SourceKind::Axis : SourceKind::Button;
                source.control = info.name;
            }
        }
        out.sources.push_back(source);
    }
    return true;
}

bool InputRouter::SetBindings(const std::string& scope, const std::vector<Binding>& bindings, std::string& error)
{
    std::vector<CompiledBinding> compiled(bindings.size());
    for (size_t i = 0; i < bindings.size(); i++)
    {
        if (!Compile(bindings[i], compiled[i], error))
        {
            error = "binding " + std::to_string(i) + ": " + error;
            return false;
        }
    }
    std::lock_guard<std::mutex> lock(_mutex);
    _bindings[scope] = std::move(compiled);
    ResetLocked();
    return true;
}

void InputRouter::ClearBindings(const std::string& scope)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _bindings.erase(scope);
    ResetLocked();
}

const std::vector<InputRouter::CompiledBinding>* InputRouter::BindingsForLocked(const Device& device) const
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
    // The bindings changed: the held states start over from the controls' values, the turbo phases from scratch
    for (auto& [id, device] : _devices)
    {
        device.states.clear();
        EvaluateLocked(device);
    }
    for (auto& [name, instance] : _instances)
        instance.phases.clear();
}

/// endregion </Bindings>

/// region <Devices>

void InputRouter::OnDeviceAdded(const HostDeviceInfo& info)
{
    std::lock_guard<std::mutex> lock(_mutex);
    Device& device = _devices[info.id];
    device = Device{};
    device.info = info;
    EvaluateLocked(device);
}

void InputRouter::OnDeviceRemoved(const HostDeviceId& id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _devices.erase(id);
    for (auto& [name, instance] : _instances)
    {
        for (auto it = instance.phases.begin(); it != instance.phases.end();)
            it = it->first.first == id ? instance.phases.erase(it) : std::next(it);
    }
}

void InputRouter::OnControl(const ControlEvent& event)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _devices.find(event.device);
    if (it == _devices.end())
        return;
    ControlInfo info;
    if (!ParseControl(event.control, info))
        return;
    it->second.values[info.name] = event.value;
    EvaluateLocked(it->second);
}

std::vector<HostDeviceInfo> InputRouter::Devices() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<HostDeviceInfo> list;
    for (const auto& [id, device] : _devices)
        list.push_back(device.info);
    return list;
}

void InputRouter::SetRole(const HostDeviceId& device, DeviceRole role)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _settings[device].role = role;
}

DeviceRole InputRouter::Role(const HostDeviceId& device) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _settings.find(device);
    return it == _settings.end() ? DeviceRole::Player : it->second.role;
}

void InputRouter::SetInstanceMode(const HostDeviceId& device, InstanceMode mode, const std::string& pinned)
{
    std::lock_guard<std::mutex> lock(_mutex);
    Settings& settings = _settings[device];
    settings.mode = mode;
    settings.pinned = mode == InstanceMode::Pinned ? pinned : std::string{};
}

void InputRouter::SetPlayerConnector(const HostDeviceId& device, const std::string& connector)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _settings[device].connector = connector;
}

void InputRouter::SetFocus(const std::string& instance)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _focus = instance;
}

float InputRouter::Value(const Device& device, const std::string& control) const
{
    const auto it = device.values.find(control);
    return it == device.values.end() ? 0.0f : it->second;
}

void InputRouter::EvaluateLocked(Device& device)
{
    const std::vector<CompiledBinding>* bindings = BindingsForLocked(device);
    if (bindings == nullptr)
    {
        device.states.clear();
        return;
    }
    if (device.states.size() != bindings->size())
        device.states.assign(bindings->size(), BindingState{});

    for (size_t i = 0; i < bindings->size(); i++)
    {
        const CompiledBinding& compiled = (*bindings)[i];
        const AnalogOptions& analog = compiled.binding.options.analog;
        BindingState& state = device.states[i];
        if (state.sources.size() != compiled.sources.size())
            state.sources.assign(compiled.sources.size(), SourceState{});

        uint16_t bits = 0;
        for (size_t j = 0; j < compiled.sources.size(); j++)
        {
            const CompiledSource& source = compiled.sources[j];
            SourceState& held = state.sources[j];
            bool directional = false;
            switch (source.kind)
            {
                case SourceKind::Button:
                    held.on = Value(device, source.control) >= kButtonThreshold;
                    break;
                case SourceKind::Trigger:
                    held.on = TriggerClick(Value(device, source.control), held.on, analog);
                    break;
                case SourceKind::Axis:
                {
                    const float deflection = std::fabs(Value(device, source.control));
                    held.on = deflection >= (held.on ? analog.off : analog.on);
                    break;
                }
                case SourceKind::Stick:
                case SourceKind::StickDirection:
                    held.dirs = StickDirections(Value(device, source.control + ".x"),
                                                Value(device, source.control + ".y"), held.dirs, analog);
                    held.on = source.kind == SourceKind::Stick ? held.dirs != 0 : (held.dirs & source.direction) != 0;
                    directional = source.kind == SourceKind::Stick;
                    break;
                case SourceKind::Dpad:
                    held.dirs = static_cast<uint16_t>(
                        (Value(device, "dpad.up") >= kButtonThreshold ? kUp : 0) |
                        (Value(device, "dpad.down") >= kButtonThreshold ? kDown : 0) |
                        (Value(device, "dpad.left") >= kButtonThreshold ? kLeft : 0) |
                        (Value(device, "dpad.right") >= kButtonThreshold ? kRight : 0));
                    held.on = held.dirs != 0;
                    directional = true;
                    break;
            }
            if (directional && compiled.directionalTarget)
                bits |= held.dirs;
            else if (held.on)
                bits |= compiled.binding.target.mask;
        }

        if (bits != 0 && state.bits == 0)
        {
            state.presses++;
            state.pressBits = bits;
        }
        state.bits = bits;
    }
}

bool InputRouter::DrivesLocked(const HostDeviceId& device, const std::string& instance) const
{
    const auto it = _settings.find(device);
    if (it == _settings.end())
        return _focus.empty() || _focus == instance;
    const Settings& settings = it->second;
    if (settings.role != DeviceRole::Player)
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

/// region <Sampling>

uint16_t InputRouter::Output(const CompiledBinding& compiled, const BindingState& state, Phase& phase, uint64_t frame)
{
    const BindingOptions& options = compiled.binding.options;
    const uint16_t mask = compiled.binding.target.mask;
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
            return bits;
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

void InputRouter::Sample(const std::string& instanceName, uint64_t frame, std::vector<ConnectorSample>& out)
{
    std::lock_guard<std::mutex> lock(_mutex);
    Instance& instance = _instances[instanceName];
    std::map<std::string, uint16_t> masks;
    for (auto& [id, device] : _devices)
    {
        if (!DrivesLocked(id, instanceName))
            continue;
        const std::vector<CompiledBinding>* bindings = BindingsForLocked(device);
        if (bindings == nullptr || device.states.size() != bindings->size())
            continue;
        const auto settings = _settings.find(id);
        const std::string player = settings == _settings.end() ? std::string{} : settings->second.connector;
        for (size_t i = 0; i < bindings->size(); i++)
        {
            const CompiledBinding& compiled = (*bindings)[i];
            const uint16_t bits = Output(compiled, device.states[i], instance.phases[{id, i}], frame);
            const std::string& connector =
                compiled.binding.target.connector == "auto" ? player : compiled.binding.target.connector;
            if (connector.empty())
                continue;
            masks[connector] |= bits;
        }
    }

    std::set<std::string> held;
    for (const auto& [connector, bits] : masks)
    {
        if (bits != 0)
        {
            out.push_back({connector, bits});
            held.insert(connector);
        }
    }
    for (const std::string& connector : instance.held)
    {
        if (held.count(connector) == 0)
            out.push_back({connector, 0});
    }
    instance.held = std::move(held);
}

void InputRouter::ForgetInstance(const std::string& instance)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _instances.erase(instance);
}

/// endregion </Sampling>

}  // namespace controllers
