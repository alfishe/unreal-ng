#pragma once

/// @file inputrouter.h
/// @brief Host control events -> what the machine is given each frame: retro controls per connector, ZX keys, the
/// mouse (architecture.md §7, mapping-and-profiles.md §1-§3). Control events update each binding's held state when
/// they arrive (any thread) and run actions at once; the emulator samples once per frame (late latch), where turbo,
/// burst, tap and macros count emulated frames, so a recording replays them identically.
///
/// Layers (mapping-and-profiles.md §2, DT-5): `play` at the bottom, layer keys raise `debug` or a shift layer; the
/// top layer that binds a control wins, `debug` hides everything below it, a layer key acts only as the layer key,
/// and a layer switch releases every held target (a control held across it is ignored until released). Chords:
/// the members' own bindings wait for the chord window and never see a press that completed the chord.
/// The profile store and field-by-field layering are C2c (TODO.md).
///
/// Thread safety: one mutex, held briefly by events and samples (the seqlock state table of architecture.md §11 is
/// the C3 budget work, when real backends report at their rates).

#include <cstdint>
#include <functional>
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

/// An Action target pressed or released (architecture.md §7: run on arrival, never journaled)
struct ActionEvent
{
    HostDeviceId device;
    std::string action;
    bool pressed = false;
    /// 1 for a press; a `curve: scrub` binding's level (0..1, 0 = released); an encoder's signed steps
    float value = 0;
};

class InputRouter
{
public:
    /// Host time in ns (the chord window)
    using Clock = std::function<uint64_t()>;
    using ActionSink = std::function<void(const ActionEvent&)>;

    static constexpr uint64_t kChordWindowNs = 60'000'000;

    InputRouter();

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
    /// The device drives that instance now (a player that follows the focus, is pinned to it, or drives all)
    bool Drives(const HostDeviceId& device, const std::string& instance) const;

    /// The layers a device has up, bottom to top ("" = play; a debugger remote has `debug` alone)
    std::vector<std::string> Layers(const HostDeviceId& device) const;
    /// The control drives a consuming binding of the device's current layers, or is a layer key: the window's
    /// keyboard keeps it from the machine (mapping-and-profiles.md §4.2)
    bool Consumes(const HostDeviceId& device, const std::string& control) const;

    /// endregion </Devices>

    /// region <Actions and time>

    /// Where actions go: called outside the router's lock, on the thread of the event (or of the Sample / Poll that
    /// closed a chord window)
    void SetActionSink(ActionSink sink);
    /// The host clock (tests set their own); an event with hostTimeNs 0 takes its time
    void SetClock(Clock clock);
    /// Close the chord windows that ran out: Sample does it each frame, a paused machine needs a host timer
    void Poll();

    /// endregion </Actions and time>

    /// region <Sampling>

    /// The emulator thread at a frame boundary: one connector sample per connector this instance's devices hold
    /// (and a zero once for a connector they released), the ZX keys and the mouse they drive
    void Sample(const std::string& instance, uint64_t frame, HostSample& out);
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
        Encoder,         ///< `knob`: steps, no state; the binding's other sources are buttons held while it turns
    };
    struct CompiledSource
    {
        SourceKind kind = SourceKind::Button;
        std::string control;  ///< the value read: the control, the trigger, or the stick's prefix
        uint16_t direction = 0;
        std::vector<std::string> values;  ///< every value it reads
    };
    struct CompiledBinding
    {
        Binding binding;
        std::vector<CompiledSource> sources;
        bool directionalTarget = false;  ///< the target is the four directions: directional sources keep theirs
        uint16_t mask = 1;               ///< the output when on: the retro bits, 1 for the other targets
        int encoder = -1;                ///< the encoder source: its steps run the action while the rest is held
        std::set<std::string> controls;  ///< what a higher layer overrides it by (a stick, a button)
    };
    struct CompiledSet
    {
        std::vector<CompiledBinding> bindings;
        std::set<std::string> liveLayers;  ///< the layers with bindings of their own: a key to any other is inert
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
        uint32_t epoch = 0;      ///< the device's layer switches when it was evaluated
        bool active = false;     ///< its layer is up and no higher layer overrides it
        bool chordOn = false;
        bool layerOn = false;    ///< a layer key's last state
        float level = 0;         ///< a `curve: scrub` action's last level
    };
    /// What the router does to one value of a device besides reading it
    struct Gate
    {
        bool pressed = false;   ///< a chord member's last digital state
        /// a chord member pressed alone: its own bindings wait for the window (a button held with an encoder: until
        /// released, and a turn while held takes the press)
        bool pending = false;
        uint64_t until = 0;
        bool consumed = false;  ///< it completed a chord: its own bindings never see this press
        bool owed = false;      ///< released inside the window: its own bindings get the press now
        bool stale = false;     ///< held across a layer switch: ignored until released
    };
    struct LayerEntry
    {
        std::string name;
        size_t binding = 0;  ///< the layer key that raised it
        LayerMode mode = LayerMode::Hold;
        bool used = false;   ///< oneshot: a control of the layer was pressed
    };
    struct Device
    {
        HostDeviceInfo info;
        std::unordered_map<std::string, float> values;
        std::vector<BindingState> states;
        std::map<std::string, Gate> gates;
        std::vector<LayerEntry> layers;  ///< above `play`, bottom to top
        uint32_t epoch = 0;
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
        uint32_t epoch = 0;
        uint64_t since = 0;  ///< the frame of the last press (turbo, burst and macro phases start there)
        uint64_t until = 0;  ///< tap / burst / macro end frame
        uint16_t tapBits = 0;
        bool latched = false;    ///< toggle / autohold
        float remainder = 0.0f;  ///< a mouse axis: the counts not sent yet
    };
    struct Instance
    {
        std::map<std::pair<HostDeviceId, size_t>, Phase> phases;
        std::set<std::string> held;  ///< connectors with bits at the last sample
    };

    using ValueOf = std::function<float(const std::string&)>;

    static bool Compile(const Binding& binding, CompiledBinding& out, std::string& error);
    const CompiledSet* BindingsForLocked(const Device& device) const;
    static float Value(const Device& device, const std::string& control);
    DeviceRole RoleLocked(const HostDeviceId& device) const;
    /// The layers whose bindings act, bottom to top: `debug` hides what is below it
    std::vector<std::string> ActiveLayersLocked(const Device& device) const;
    /// `play`, or a layer a layer key raised (`debug` may hide it)
    static bool RaisedLocked(const Device& device, const std::string& layer);
    /// The bits of a binding's sources as @p value gives them (the hysteresis memory advances)
    static uint16_t SourceBits(const CompiledBinding& compiled, BindingState& state, const ValueOf& value);
    static void Settle(BindingState& state, uint16_t bits);
    void EvaluateLocked(Device& device, uint64_t now, std::vector<ActionEvent>& actions);
    /// An encoder turned @p steps: the action of its binding whose buttons are held (the most of them wins)
    void TurnLocked(Device& device, const std::string& control, float steps, uint64_t now,
                    std::vector<ActionEvent>& actions);
    bool UpdateLayersLocked(Device& device, const CompiledSet& set, const ValueOf& raw);
    void ExpireLocked(uint64_t now, std::vector<ActionEvent>& actions);
    void ResetLocked();
    bool DrivesLocked(const HostDeviceId& device, const std::string& instance) const;
    static uint16_t Output(const CompiledBinding& compiled, const BindingState& state, Phase& phase, uint64_t frame);
    void Dispatch(const std::vector<ActionEvent>& actions) const;
    uint64_t Now() const;

    mutable std::mutex _mutex;
    std::map<std::string, CompiledSet> _bindings;
    std::map<HostDeviceId, Device> _devices;
    std::map<HostDeviceId, Settings> _settings;
    std::map<std::string, Instance> _instances;
    std::string _focus;
    Clock _clock;
    ActionSink _actions;
};

}  // namespace controllers
