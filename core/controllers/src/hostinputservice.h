#pragma once

/// @file hostinputservice.h
/// @brief The process-wide entry of the controllers library (architecture.md §2-§3): it runs the backends, receives
/// their devices and control events, routes them (InputRouter), and gives each emulator instance the
/// IHostInputSource its ControllerManager samples at every frame boundary.

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "devicedatabase.h"
#include "devicemapping.h"
#include "emulator/io/controllers/ihapticfeed.h"
#include "emulator/io/controllers/ihostinputsource.h"
#include "haptics/hapticmixer.h"
#include "haptics/hapticprofile.h"
#include "haptics/testeffects.h"
#include "hostbackend.h"
#include "router/inputrouter.h"

namespace controllers
{

class HostInputService : public IBackendSink
{
public:
    HostInputService();
    ~HostInputService() override;

    HostInputService(const HostInputService&) = delete;
    HostInputService& operator=(const HostInputService&) = delete;

    /// region <Backends>

    /// The native backends of this OS (BackendFactory), started; the ones that cannot run are listed by Probe
    void StartNative();
    /// Start @p backend and keep it; false with the reason when it cannot run (it is still listed by Probe)
    bool AddBackend(std::unique_ptr<IBackend> backend, std::string& reason);
    /// The backend of that name, or nullptr
    IBackend* FindBackend(const std::string& name) const;
    std::vector<BackendProbe> Probe() const;
    /// What the device cannot do here and how to fix it (IBackend::DeviceNotice); "" when nothing is missing
    std::string DeviceNotice(const HostDeviceId& device) const;
    /// The battery as the device's backend last saw it (IBackend::Battery); unknown for a device not connected
    BatteryState Battery(const HostDeviceId& device) const;
    /// The device has a light its backend can set
    bool HasLight(const HostDeviceId& device) const;
    /// Why the device's light cannot be set ("" when it can)
    std::string LightUnavailable(const HostDeviceId& device) const;
    /// Set the light's colour; never blocks. False when it has no light here
    bool SetLight(const HostDeviceId& device, const LightColor& color);
    /// The device's endpoints, the input one first (IBackend::Endpoints); a backend that lists none: the device as one
    /// endpoint with what the backend answers for it
    std::vector<EndpointInfo> Endpoints(const HostDeviceId& device) const;
    /// Why the device's raw reports cannot be shown; "" when RawReports answers them
    std::string RawReportsUnavailable(const HostDeviceId& device) const;
    /// The newest raw input reports (starts the backend's tap, ReportTap)
    std::vector<RawReport> RawReports(const HostDeviceId& device);
    /// Stop every backend: their devices are removed
    void Stop();

    /// endregion </Backends>

    InputRouter& Router()
    {
        return *_router;
    }

    /// The source an emulator instance's ControllerManager samples (ControllerManager::SetHostInputSource);
    /// @p instance is the emulator's id. Dropping the source forgets the instance
    std::shared_ptr<IHostInputSource> SourceFor(const std::string& instance);

    /// Changes when a device arrives or leaves (the dialog's refresh, ui-ux.md §7)
    uint64_t Revision() const
    {
        return _revision.load();
    }
    /// A device's last value of every control it reported (an encoder's: the sum of its steps since it connected):
    /// the dialog's live view and press-to-bind
    std::map<std::string, float> Live(const HostDeviceId& device) const;

    /// region <Unknown devices: control maps and the learn wizard (device-database.md §3-§4, ui-ux.md §3.5)>
    /// Give each connected device the `map` of its family in the active database (DeviceDatabase::Active: the built-in
    /// one with devices.user.json on top), after DeviceDatabase::SetUser changed it; a device that connects later gets
    /// it as it is added
    void RefreshUserMaps();
    /// What the database says about a connected device (its id's VID / PID, its name); step "unknown" without them
    DeviceMatch LookupDevice(const HostDeviceInfo& device) const;
    /// A device's control map: each raw control it names also reports as its canonical control. Empty: none
    void SetControlMap(const HostDeviceId& device, std::vector<MappedControl> map);
    std::vector<MappedControl> ControlMap(const HostDeviceId& device) const;

    /// The learn wizard on a device: its events answer the session's prompts and reach nothing else until EndLearn
    /// (the baseline is its Live values now). Replaces a session the device had
    void StartLearn(const HostDeviceId& device, LearnSession session);
    /// Run @p step on the device's session (Skip, Back, a read); false when it has none
    bool Learn(const HostDeviceId& device, const std::function<void(LearnSession&)>& step);
    /// The session ends; false when there was none
    bool EndLearn(const HostDeviceId& device);
    /// endregion </Unknown devices>

    /// region <Haptics (haptics.md §3; host output only, H-4)>
    /// The device has motors its backend can drive
    bool HasRumble(const HostDeviceId& device) const;
    /// Why the device cannot be tested for haptics ("" when it can): no motors, or none its backend drives yet
    std::string HapticsUnavailable(const HostDeviceId& device) const;
    /// Play an effect on a device (ignored while the gate is closed)
    void PlayEffect(const HostDeviceId& device, const HapticEffect& effect);
    /// Play a test effect (haptics.md §10): a clicker's clicks straight to its backend, an envelope as PlayEffect
    void PlayTest(const HostDeviceId& device, const TestEffect& test);
    /// What Identify did
    struct Identified
    {
        bool rumble = false;  ///< the identify test effect played (300 ms on both motors, two clicks on a clicker)
        bool light = false;   ///< the light blinked (white for 300 ms, then back to the colour it had)
    };
    /// Identify (ui-ux.md §3.7): a 300 ms pulse on both motors (two clicks on a clicker) and a blink of the light where
    /// the device has them, so the user sees which pad is which
    Identified Identify(const HostDeviceId& device);
    static constexpr int kIdentifyMs = 300;
    /// How long Identify's light stays white (kIdentifyMs; a test shortens it to see the light go back at once)
    void SetIdentifyBlinkMs(int ms)
    {
        _identifyBlinkMs.store(ms > 0 ? ms : kIdentifyMs);
    }
    /// H-5: the host closes it on pause, seek, replay, turbo and focus loss; closing stops every motor
    void SetHapticsGate(bool open);
    void SetHapticsStrength(float strength);
    /// The generic sound haptics of one instance (haptics.md §4) for its ControllerManager::SetHapticFeed: they
    /// play on the devices with motors that drive the instance
    std::shared_ptr<IHapticFeed> HapticFeedFor(const std::string& instance);
    /// RumbleActuator bits of @p device (0: no motors)
    uint8_t RumbleActuators(const HostDeviceId& device) const;
    /// A game profile's haptics (haptics.md §7) for @p instance: its sources and rules instead of the generic filters
    std::shared_ptr<IHapticFeed> HapticFeedFor(const std::string& instance, const HapticProfile& profile,
                                               const std::string& name);
    HapticMixer& Haptics()
    {
        return *_haptics;
    }
    /// endregion </Haptics>

    /// region <Surface outputs (control surfaces controls-and-feedback.md §3, architecture.md §9)>
    /// OutputKind bits of @p device (0: none)
    uint8_t Outputs(const HostDeviceId& device) const;
    /// Play a pattern / set the steps on a surface; never blocks (the device's writer queues it). False for a device
    /// without that output. The router's feel changes (context, layers, profiles) come here by themselves
    bool Output(const HostDeviceId& device, const HapticOut& out);
    bool Output(const HostDeviceId& device, const StepsOut& out);

    /// What PlayMarks did
    struct MarksPlayed
    {
        bool played = false;
        std::string mark;     ///< the mark that played (the strongest one the layer has a pattern for)
        HapticPattern pattern = HapticPattern::Tick;
        std::string reason;   ///< why nothing played
    };
    /// A seek's marks (strongest first, as `ttd.seek_by` returns them) felt on the device that moved: the pattern its
    /// top layer's feel names for the strongest of them, unless a mark played less than `min_interval_ms` ago (an
    /// `edge` or a `barrier` always plays). @p nowMs 0: the steady clock
    MarksPlayed PlayMarks(const HostDeviceId& device, const std::vector<std::string>& marks, uint64_t nowMs = 0);
    /// endregion </Surface outputs>

    /// The promotions since the last call (old id, new id; OnDevicePromoted): the profile store's owner records each
    /// as an alias (ProfileStore::AddAlias) and keeps it (architecture.md §5). Revision counts them as a change
    std::vector<std::pair<HostDeviceId, HostDeviceId>> TakePromotions();

    /// region <IBackendSink>
    void OnDeviceAdded(const HostDeviceInfo& device) override;
    void OnDeviceRemoved(const HostDeviceId& device) override;
    void OnDevicePromoted(const HostDeviceId& from, const HostDeviceId& to) override;
    void OnControl(const ControlEvent& event) override;
    /// endregion </IBackendSink>

private:
    struct Entry
    {
        std::unique_ptr<IBackend> backend;
        bool running = false;
        std::string reason;
    };

    IBackend* BackendOfLocked(const HostDeviceId& device) const;
    /// The router's host timer: started by the first wake, it polls at each NextDeadline (a chord window's end, a
    /// held action's repeat) while no frame samples the router (a paused machine) and sleeps when nothing waits
    void WakeTicker();
    /// The user family's map for @p device when it has one (under _mapMutex)
    void ApplyUserMapLocked(const HostDeviceInfo& device);
    void TickerLoop();
    void StopTicker();
    /// The devices that drive @p instance and have motors, asked at every haptic frame
    std::function<std::vector<HostDeviceId>()> TargetsFor(const std::string& instance);


    mutable std::mutex _mutex;
    std::mutex _hapticsWrite;  ///< held across a backend's SetRumble; Stop takes it before it drops the backends
    std::vector<Entry> _backends;
    std::shared_ptr<InputRouter> _router;
    std::atomic<uint64_t> _revision{0};
    mutable std::mutex _liveMutex;
    std::map<HostDeviceId, std::map<std::string, float>> _live;
    std::map<HostDeviceId, std::string> _deviceBackend;  ///< under _mutex: the backend that reported a device
    std::vector<std::pair<HostDeviceId, HostDeviceId>> _promotions;  ///< under _mutex: for TakePromotions
    std::mutex _marksMutex;
    MarkLimiter _marks;  ///< under _marksMutex
    mutable std::mutex _mapMutex;
    std::map<HostDeviceId, std::vector<MappedControl>> _maps;          ///< under _mapMutex
    std::map<HostDeviceId, LearnSession> _learn;                       ///< under _mapMutex
    std::map<HostDeviceId, HostDeviceInfo> _connected;                 ///< under _mapMutex: for the user maps
    std::map<HostDeviceId, LightColor> _lights;                        ///< under _mutex: the colour each light has
    /// under _tickMutex: the lights Identify blinked, back to their colour at the steady time (ns)
    std::map<HostDeviceId, std::pair<uint64_t, LightColor>> _blinks;
    std::atomic<int> _identifyBlinkMs{kIdentifyMs};
    std::mutex _tickMutex;
    std::condition_variable _tickCv;
    std::thread _ticker;  ///< under _tickMutex until it is joined
    bool _tickStop = false;
    bool _tickWake = false;
    /// Last member: destroyed first, while the backends it writes to still exist
    std::unique_ptr<HapticMixer> _haptics;
};

}  // namespace controllers
