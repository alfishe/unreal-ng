#pragma once

/// @file protocolpads.h
/// @brief The pads that talk a protocol of their own over HID (haptics.md §9) as the OS-neutral part of a HID backend:
/// the DualSense, the DualSense Edge and the DualShock 4 (SonyPad, SonyProtocol), the Switch Pro Controller, the
/// Joy-Cons alone and as a pair, the halves of the charging grip, the Nintendo Switch Online pads (SwitchProPad,
/// SwitchProtocol, JoyConPairing), the Steam Controllers on their vendor interface (SteamPad, SteamProtocol: lizard mode
/// off, a dongle's slot shown while a pad is on it) and the GameCube adapter (GameCubeAdapter, GameCubeProtocol: each
/// port with a controller a device `<adapter id>:p1` .. `:p4`).
///
/// The backend gives it each endpoint of such a pad with the means to write an output report and to read a feature
/// report (called on the pad's PadPump thread), and every input report of it (on the backend's reading thread). It
/// reports the devices to the sink (a Joy-Con pair as one device, a grip half once its device info said which side it
/// is), their controls under the id the user sees, and answers the outputs (rumble with the HD frequencies, light,
/// player LEDs) and the battery from the pad's own report. The rules are the IOHID backend's (iohidbackend.cpp): the
/// lowest free player number on the player LEDs, a pair's halves rumble with their side's motor and show the pair's
/// number, a grip half that never says its side stays hidden.
///
/// The OS part stays in the backend: the device's id (architecture.md §5), its name, family and layout from the
/// database, the HidPad of the descriptor's reports (the Bluetooth short report 0x01 of a PlayStation pad, the simple
/// 0x3F of a Switch pad before its setup) and the writes. Used by the Windows Raw Input backend; tested with fake
/// writers on every OS.
///
/// Thread-safe: Add, Remove, Clear and Input on the backend's reading thread; the outputs and queries from any thread.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <array>

#include "hid/drivers/gamecubeadapter.h"
#include "hid/drivers/padprotocol.h"
#include "hid/drivers/sonypad.h"
#include "hid/drivers/steampad.h"
#include "hid/drivers/switchpro.h"
#include "hid/hidpad.h"
#include "hostbackend.h"
#include "joyconpairing.h"

namespace controllers
{

class ProtocolPads
{
public:
    struct Options
    {
        /// SwitchProtocol's wait before its first command (the tests make it 0)
        uint64_t switchStartDelayMs = SwitchProtocol::kStartDelayMs;
        /// The pumps' clock, steady milliseconds (empty: the steady clock)
        PadPump::Clock clock;
    };

    struct Endpoint
    {
        std::string key;      ///< the backend's, unique among the endpoints it gave
        HostDeviceInfo info;  ///< id (unique within the backend), name, family, layout, backend
        uint16_t vendor = 0;
        uint16_t product = 0;
        bool bluetooth = false;
        /// The device's own product string (the NES pads come as right Joy-Cons: "NES Controller", "HVC Controller")
        std::string productName;
        /// The OS puts a report id 0 before each report of a collection without report ids (Windows); dropped for
        /// the protocols without ids (the 2015 Steam Controller)
        bool zeroReportIdFirst = false;
        /// The descriptor's reports where they are not the protocol's full report; may be null
        std::unique_ptr<HidPad> hid;
        /// The writes on the pad's pump thread; both empty when the OS gave no handle for writing (no outputs, no setup)
        PadPump::Write write;
        PadPump::GetFeature getFeature;
        /// A feature report write (bytes[0] the report id, 0 for none), on the pump's thread: the Steam set-up
        PadPump::Write setFeature;
        /// Why there are no outputs when the writes are empty, for the user
        std::string outputsUnavailable;
    };

    /// A DualSense, DualSense Edge, DualShock 4, Switch Pro Controller, Joy-Con, a half of the charging grip, a
    /// Nintendo Switch Online pad, a Steam Controller (its vendor interface) or the GameCube adapter
    static bool Handles(uint16_t vendor, uint16_t product, const std::string& productName = {});
    /// Whether the top-level collection of usage page @p usagePage is the protocol's: a Steam Controller's pad is its
    /// vendor-page interface (0xFF00 and up; its lizard keyboard and mouse are not, a game pad interface of Valve's
    /// is a plain HID pad), any other device of Handles has one collection
    static bool TakesCollection(uint16_t vendor, uint16_t product, uint16_t usagePage,
                                const std::string& productName = {});
    /// A GameCube adapter's port as a device of its own: `<adapter id>:p1` .. `:p4` (@p port 0-3)
    static HostDeviceId PortId(const HostDeviceId& adapter, int port);
    /// The size of the protocol's full report in @p report: Windows hands every input report padded to the
    /// collection's longest one (HIDP_CAPS::InputReportByteLength: 547 bytes on a PlayStation pad over Bluetooth), and
    /// SonyPad takes its reports by their exact size (64 over USB, 78 over Bluetooth). @p size for anything else
    static size_t ReportSize(uint16_t vendor, uint16_t product, bool bluetooth, const uint8_t* report, size_t size);

    explicit ProtocolPads(IBackendSink& sink);
    ProtocolPads(IBackendSink& sink, Options options);
    /// The pumps stop their motors; nothing is reported (Clear does that)
    ~ProtocolPads();
    ProtocolPads(const ProtocolPads&) = delete;
    ProtocolPads& operator=(const ProtocolPads&) = delete;

    /// An endpoint arrived: reported as a device (a Joy-Con straight into a pair with one of the other side that
    /// waits; a grip half once it said its side). False when no driver here takes its vendor / product
    bool Add(Endpoint endpoint);
    void Remove(const std::string& key);
    /// Every device gone, each reported once
    void Clear();
    /// An input report of endpoint @p key: the protocol sees it first (subcommand replies), then the full report or
    /// the descriptor's
    void Input(const std::string& key, const uint8_t* report, size_t size, uint64_t timeNs = 0);

    /// A device this part reports (a pad, a Joy-Con alone, a pair)
    bool Has(const HostDeviceId& device) const;
    /// The ids of every endpoint and pair: what the backend's unique ids must not take
    bool IdTaken(const HostDeviceId& id) const;
    size_t DeviceCount() const;

    /// region <Outputs and battery (IBackend's calls for the devices of Has)>
    uint8_t RumbleActuators(const HostDeviceId& device) const;
    std::string RumbleUnavailable(const HostDeviceId& device) const;
    bool SetRumble(const HostDeviceId& device, const RumbleLevels& levels);
    bool HasLight(const HostDeviceId& device) const;
    std::string LightUnavailable(const HostDeviceId& device) const;
    bool SetLight(const HostDeviceId& device, const LightColor& color);
    BatteryState Battery(const HostDeviceId& device) const;
    /// The number the player LEDs show, 0 none (the tests)
    int Player(const HostDeviceId& device) const;
    /// endregion </Outputs and battery>

private:
    struct Device
    {
        std::string key;
        HostDeviceId id;
        /// The id its events go out under: its own, or its Joy-Con pair's (written on the reading thread under
        /// _mutex, read there without it)
        HostDeviceId reported;
        HostDeviceInfo single;  ///< its own device, for when a Joy-Con is alone again
        bool bluetooth = false;
        uint16_t vendor = 0;
        uint16_t product = 0;
        std::unique_ptr<HidPad> hid;
        std::unique_ptr<SonyPad> sony;
        std::unique_ptr<SwitchProPad> switchPro;
        std::unique_ptr<SteamPad> steam;
        std::unique_ptr<GameCubeAdapter> gameCube;
        std::array<bool, GameCubeAdapter::kPorts> portShown{};  ///< under _mutex: the adapter's ports reported
        /// A dongle's slot with no pad on it, or the GameCube adapter itself: not reported (under _mutex; written on
        /// the reading thread)
        bool slotPending = false;
        bool zeroReportIdFirst = false;  ///< drop the 0 the OS put first (Endpoint::zeroReportIdFirst)
        std::string noRumble;            ///< why a device with writes has no motors (NSO pads, Steam, GameCube)
        int joyCon = 0;      ///< 1 a left Joy-Con, 2 a right one
        bool shown = false;  ///< under _mutex: its own device is reported (a pad, a Joy-Con alone)
        /// A grip half waiting for its device info: not reported, its input dropped
        std::atomic<bool> gripPending{false};
        /// The device info's type (SwitchProtocol::TypeSink, from the pump's thread); -1 not yet
        std::shared_ptr<std::atomic<int>> gripType = std::make_shared<std::atomic<int>>(-1);
        std::unique_ptr<PadPump> pump;
        bool light = false;  ///< the pump sets a light bar colour (the PlayStation pads)
        int player = 0;
        uint8_t actuators = 0;
        std::string outputsUnavailable;
        BatteryState battery;      ///< under _mutex
        BatteryState lastBattery;  ///< the reading thread's copy: battery is written under the lock on a change only
    };
    struct Events
    {
        std::vector<HostDeviceId> removed;
        std::vector<HostDeviceInfo> added;
    };

    Device* FindLocked(const HostDeviceId& id) const;
    /// The GameCube adapter that has @p id as a port with a controller in it
    Device* PortLocked(const HostDeviceId& id) const;
    void OnGameCube(Device& device, const uint8_t* report, size_t size, uint64_t timeNs);
    void OnSteam(Device& device, const uint8_t* report, size_t size, uint64_t timeNs);
    Device* JoyConLocked(const HostDeviceId& id) const;
    /// Both halves of a pair; nullptr, nullptr for any other id
    std::pair<Device*, Device*> PairLocked(const HostDeviceId& device) const;
    void ApplyJoyConChangeLocked(const JoyConPairing::Change& change, const HostDeviceId& gone, Events& out);
    void ResolveGrip(Device& device);
    void Report(const Events& events);

    IBackendSink& _sink;
    Options _options;
    mutable std::mutex _mutex;
    std::map<std::string, std::unique_ptr<Device>> _devices;  ///< under _mutex; changed on the reading thread only
    JoyConPairing _joyCons;                                   ///< under _mutex
};

}  // namespace controllers
