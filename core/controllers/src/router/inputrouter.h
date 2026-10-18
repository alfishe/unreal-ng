#pragma once

/// @file inputrouter.h
/// @brief Host control events -> retro control state per connector (architecture.md §7, mapping-and-profiles.md
/// §1-§3). Control events update each binding's held state when they arrive (any thread); the emulator samples
/// once per frame (late latch), where turbo, burst and tap count emulated frames, so a recording replays them
/// identically.
///
/// C2a: retro targets, OR-lists, sticks and the D-pad as directions, derived digital controls with hysteresis,
/// turbo / burst / autohold / tap, roles, instance assignment, a player's `auto` connector. Layers, chords and the
/// key / mouse / action targets are C2b; the profile store and field-by-field layering C2c (TODO.md).
///
/// Thread safety: one mutex, held briefly by events and samples (the seqlock state table of architecture.md §11 is
/// the C3 budget work, when real backends report at their rates).

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "emulator/io/controllers/controllertypes.h"
#include "hostdevice.h"
#include "router/binding.h"

namespace controllers
{

class InputRouter
{
public:
    /// region <Bindings>

    /// The bindings of a scope: a device id, "family:<family>", or "" for every device. The most specific scope
    /// that has bindings is used whole (C2c merges field by field). False with the reason for an invalid binding
    bool SetBindings(const std::string& scope, const std::vector<Binding>& bindings, std::string& error);
    void ClearBindings(const std::string& scope);

    /// endregion </Bindings>

    /// region <Devices>

    void OnDeviceAdded(const HostDeviceInfo& device);
    /// The device's held targets are released at the next sample
    void OnDeviceRemoved(const HostDeviceId& device);
    void OnControl(const ControlEvent& event);
    std::vector<HostDeviceInfo> Devices() const;

    /// A device's settings: kept by id across a replug (architecture.md §5, reconnect match)
    void SetRole(const HostDeviceId& device, DeviceRole role);
    DeviceRole Role(const HostDeviceId& device) const;
    void SetInstanceMode(const HostDeviceId& device, InstanceMode mode, const std::string& pinned = {});
    /// The connector a device's `auto:` targets drive (the player's connector); empty = none
    void SetPlayerConnector(const HostDeviceId& device, const std::string& connector);
    /// The instance whose window has focus: what follow-focus devices drive. Empty (the default) = every instance
    void SetFocus(const std::string& instance);

    /// endregion </Devices>

    /// region <Sampling>

    /// The emulator thread at a frame boundary: one sample per connector this instance's devices hold, and a zero
    /// once for a connector they released
    void Sample(const std::string& instance, uint64_t frame, std::vector<ConnectorSample>& out);
    /// The instance went away (its source was dropped)
    void ForgetInstance(const std::string& instance);

    /// endregion </Sampling>

private:
    enum class SourceKind : uint8_t
    {
        Button,          ///< pressed at 0.5
        Trigger,         ///< `lt`, `lt.click`: the click with hysteresis
        Axis,            ///< a single axis seen as a button: |value| with the stick's thresholds
        Stick,           ///< `lstick`: the directions
        StickDirection,  ///< `lstick.left`: one direction of the stick
        Dpad,            ///< `dpad.*`: the four buttons as directions
    };
    struct CompiledSource
    {
        SourceKind kind = SourceKind::Button;
        std::string control;  ///< the value read: the control, the trigger, or the stick's prefix
        uint16_t direction = 0;
    };
    struct CompiledBinding
    {
        Binding binding;
        std::vector<CompiledSource> sources;
        bool directionalTarget = false;  ///< the target is the four directions: directional sources keep theirs
    };

    /// Event side, per device and binding
    struct SourceState
    {
        uint16_t dirs = 0;  ///< a stick's or the D-pad's last directions (the hysteresis memory)
        bool on = false;
    };
    struct BindingState
    {
        std::vector<SourceState> sources;
        uint16_t bits = 0;       ///< the target bits held now
        uint16_t pressBits = 0;  ///< the bits at the last press (a press shorter than a frame still reaches it)
        uint32_t presses = 0;    ///< press edges so far
    };
    struct Device
    {
        HostDeviceInfo info;
        std::unordered_map<std::string, float> values;
        std::vector<BindingState> states;
    };
    struct Settings
    {
        DeviceRole role = DeviceRole::Player;
        InstanceMode mode = InstanceMode::FollowFocus;
        std::string pinned;
        std::string connector;
    };

    /// Sample side, per instance, device and binding: frames count here
    struct Phase
    {
        uint32_t presses = 0;
        uint64_t since = 0;  ///< the frame of the last press (turbo and burst phases start there)
        uint64_t until = 0;  ///< tap / burst end frame
        uint16_t tapBits = 0;
        bool latched = false;  ///< toggle / autohold
    };
    struct Instance
    {
        std::map<std::pair<HostDeviceId, size_t>, Phase> phases;
        std::set<std::string> held;  ///< connectors with bits at the last sample
    };

    static bool Compile(const Binding& binding, CompiledBinding& out, std::string& error);
    const std::vector<CompiledBinding>* BindingsForLocked(const Device& device) const;
    float Value(const Device& device, const std::string& control) const;
    void EvaluateLocked(Device& device);
    void ResetLocked();
    bool DrivesLocked(const HostDeviceId& device, const std::string& instance) const;
    static uint16_t Output(const CompiledBinding& compiled, const BindingState& state, Phase& phase, uint64_t frame);

    mutable std::mutex _mutex;
    std::map<std::string, std::vector<CompiledBinding>> _bindings;
    std::map<HostDeviceId, Device> _devices;
    std::map<HostDeviceId, Settings> _settings;
    std::map<std::string, Instance> _instances;
    std::string _focus;
};

}  // namespace controllers
