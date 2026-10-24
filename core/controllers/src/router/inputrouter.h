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
#include "haptics/surfacehaptics.h"
#include "hostbackend.h"
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
    /// An encoder's turn (control surfaces architecture.md §5): `value` is its steps, `rate` its speed in steps/s.
    /// A Delta action takes both; a Press action gets one press per step (the owner caps them at 16)
    bool turn = false;
    double rate = 0;
    /// The binding's `curve` object (the action's parameters, `ttd.seek_by`'s scrub curve); null when it has none
    JsonValue params{};
};

/// The speed of an encoder (control surfaces controls-and-feedback.md §2): the Stream Deck bridge's rule, retuned for
/// the Dial in S3: inst = |steps| / interval; rate = 0.5 x rate + 0.5 x inst; after kRestNs without a step rate =
/// 0.3 x inst. Bluetooth LE delivers reports in connection-interval bunches, so an interval counts at least kMinNs:
/// two reports of one bunch do not read as a spin of thousands of steps per second
class EncoderRate
{
public:
    static constexpr uint64_t kRestNs = 400'000'000;
    static constexpr uint64_t kMinNs = 7'500'000;  ///< the shortest BLE connection interval

    /// A turn of @p steps at @p nowNs: the speed in steps per second
    double Step(float steps, uint64_t nowNs);
    double Rate() const
    {
        return _rate;
    }

private:
    double _rate = 0;
    uint64_t _lastNs = 0;
    bool _moved = false;
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
    /// The device's instance mode, and in @p pinned the instance a pinned one drives
    InstanceMode GetInstanceMode(const HostDeviceId& device, std::string* pinned = nullptr) const;
    /// The connector a device's `auto:` targets drive (the player's connector); empty = none
    void SetPlayerConnector(const HostDeviceId& device, const std::string& connector);
    std::string PlayerConnector(const HostDeviceId& device) const;
    /// The connectors a player device's bindings drive outside the debug layer (an `auto:` target as its player
    /// connector), each once in binding order: the patch cords of the dialog (ui-ux.md §2.3). Empty for a debugger
    /// remote or a disabled device
    std::vector<std::string> Connectors(const HostDeviceId& device) const;
    /// The analog options (dead zone, saturation, on / off, trigger click / release) the device's current layers read
    /// @p control with ("lstick", "rt"): the first binding up that reads it, else the defaults with false. The test
    /// panel draws these thresholds (ui-ux.md §3.4)
    bool Analog(const HostDeviceId& device, const std::string& control, AnalogOptions& out) const;
    /// The device's default role (its family's: the device database's `role`), what Role answers until SetRole sets
    /// one (control surfaces Q-8: a Surface Dial starts as a debugger remote, a pad as a player)
    void SetDefaultRole(const HostDeviceId& device, DeviceRole role);
    /// SetRole chose the device's role (else it has its default)
    bool RoleSet(const HostDeviceId& device) const;
    /// A device's other id took over (architecture.md §5: a `gc:` device whose IOHID endpoint appeared): the settings
    /// chosen for @p from (role, instance mode, player connector) go to @p to unless @p to has its own
    void Promote(const HostDeviceId& from, const HostDeviceId& to);
    /// The instance whose window has focus: what follow-focus devices drive. Empty (the default) = every instance
    void SetFocus(const std::string& instance);
    /// The debugger window has the focus: every player device acts as a debugger remote (its `debug` layer alone,
    /// no layer key needed) until it is cleared. Either switch releases what is held
    void SetDebugFocus(bool on);
    /// The device drives that instance now (a player that follows the focus, is pinned to it, or drives all)
    bool Drives(const HostDeviceId& device, const std::string& instance) const;

    /// The layers a device has up, bottom to top ("" = play; a debugger remote has `debug` alone; above `debug` the
    /// context layer of its instance's context)
    std::vector<std::string> Layers(const HostDeviceId& device) const;
    /// The control drives a consuming binding of the device's current layers, or is a layer key: the window's
    /// keyboard keeps it from the machine (mapping-and-profiles.md §4.2)
    bool Consumes(const HostDeviceId& device, const std::string& control) const;

    /// endregion </Devices>

    /// region <Contexts and surface feedback (control surfaces profiles.md §1, architecture.md §9)>

    /// The context of an instance (`play`, `ttd`, `ttd.timecode`, `debug.disasm`): the owner pushes it from the
    /// core's ContextState. While a device's `debug` layer is up, the context layer of its instance's context (`@ttd`,
    /// or the longest `@<prefix>.*` match) is up above it; a change releases what is held, as a layer switch does
    void SetContext(const std::string& instance, const std::string& context);
    /// The context of the instance a device acts on (its pinned one, else the focused one); "" when none was pushed
    std::string Context(const HostDeviceId& device) const;
    /// What a scope's devices feel per layer (a debug profile's `haptics`); set and cleared with the bindings
    void SetSurfaceHaptics(const std::string& scope, SurfaceHaptics haptics);
    void ClearSurfaceHaptics(const std::string& scope);
    /// The feel of the device's top layer that has one; false when none of its layers has
    bool Feel(const HostDeviceId& device, LayerHaptics& out) const;
    /// Where steps / clicks requests go when a device's feel changes (a context change, a layer switch, new profiles,
    /// the device arriving), outside the lock as the actions. A device whose scope has surface haptics gets its top
    /// layer's steps, or clicks off where none of its layers has a feel (`play`: the knob is unbound)
    using OutputSink = std::function<void(const HostDeviceId&, const StepsOut&)>;
    void SetOutputSink(OutputSink sink);

    /// endregion </Contexts and surface feedback>

    /// region <Actions and time>

    /// Where actions go: called outside the router's lock, on the thread of the event (or of the Sample / Poll that
    /// closed a chord window)
    void SetActionSink(ActionSink sink);
    /// The host clock (tests set their own); an event with hostTimeNs 0 takes its time
    void SetClock(Clock clock);
    /// Close the chord windows that ran out and run the held actions' repeats that are due: Sample does it each
    /// frame; a paused machine needs a host timer (HostInputService runs one on the wake sink and NextDeadline)
    void Poll();
    /// The host time (ns, the router's clock) Poll has something to do at: a chord window's end, a repeat;
    /// UINT64_MAX when nothing waits
    uint64_t NextDeadline() const;
    /// The router's clock (ns)
    uint64_t NowNs() const;
    /// Called (outside the lock, on the event's thread) when an event left something for Poll to do later
    void SetWakeSink(std::function<void()> wake);
    /// A frame of host time: an action's `repeat` counts in these
    static constexpr uint64_t kRepeatFrameNs = 20'000'000;

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
        bool repeating = false;  ///< a held action with `repeat`: it runs again at repeatAt
        uint64_t repeatAt = 0;   ///< host ns
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
        std::map<std::string, EncoderRate> rates;  ///< per encoder control
        bool feelSent = false;     ///< the output sink got its steps
        StepsOut feel;             ///< what it got
    };
    struct Settings
    {
        bool roleSet = false;  ///< SetRole chose it; else defaultRole
        DeviceRole role = DeviceRole::Player;
        DeviceRole defaultRole = DeviceRole::Player;
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
    /// `play`, or a layer a layer key raised (`debug` may hide it), or the context layer above a raised `debug`
    bool RaisedLocked(const Device& device, const std::string& layer) const;
    /// The context layer the device's bindings or haptics name that its instance's context selects; "" none
    std::string ContextLayerLocked(const Device& device) const;
    /// The instance the device acts on: its pinned one, else the focused one
    const std::string& InstanceLocked(const HostDeviceId& device) const;
    const SurfaceHaptics* SurfaceHapticsForLocked(const Device& device) const;
    /// Queue a steps request when the device's feel changed (Dispatch sends it)
    void FeelLocked(Device& device);
    /// A debugger remote by its role, or a player while the debugger window has the focus
    bool RemoteLocked(const Device& device) const;
    /// The bits of a binding's sources as @p value gives them (the hysteresis memory advances)
    static uint16_t SourceBits(const CompiledBinding& compiled, BindingState& state, const ValueOf& value);
    static void Settle(BindingState& state, uint16_t bits);
    void EvaluateLocked(Device& device, uint64_t now, std::vector<ActionEvent>& actions);
    /// An encoder turned @p steps: the action of its binding whose buttons are held (the most of them wins)
    void TurnLocked(Device& device, const std::string& control, float steps, uint64_t now,
                    std::vector<ActionEvent>& actions);
    bool UpdateLayersLocked(Device& device, const CompiledSet& set, const ValueOf& raw);
    void ExpireLocked(uint64_t now, std::vector<ActionEvent>& actions);
    uint64_t NextDeadlineLocked() const;
    void ResetLocked();
    bool DrivesLocked(const HostDeviceId& device, const std::string& instance) const;
    static uint16_t Output(const CompiledBinding& compiled, const BindingState& state, Phase& phase, uint64_t frame);
    void Dispatch(const std::vector<ActionEvent>& actions) const;
    uint64_t Now() const;

    mutable std::mutex _mutex;
    std::map<std::string, CompiledSet> _bindings;
    std::map<std::string, SurfaceHaptics> _surfaceHaptics;  ///< by scope, as _bindings
    std::map<std::string, std::string> _contexts;           ///< instance -> context
    mutable std::vector<std::pair<HostDeviceId, StepsOut>> _feelQueue;  ///< under _mutex; Dispatch sends them
    OutputSink _outputs;
    std::map<HostDeviceId, Device> _devices;
    std::map<HostDeviceId, Settings> _settings;
    std::map<std::string, Instance> _instances;
    std::string _focus;
    bool _debugFocus = false;
    Clock _clock;
    ActionSink _actions;
    std::function<void()> _wake;
};

}  // namespace controllers
