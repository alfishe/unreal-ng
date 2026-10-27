#pragma once

/// @file controllermanager.h
/// @brief The machine's connectors and the retro devices plugged into them (controller manager C1,
/// docs/inprogress/2026-10-17-controller-manager/retro-devices.md).
///
/// A connector is to a retro device what a media slot is to a medium: a stable id, an alias, a kind:index
/// selector, the devices it accepts. The manager builds the list for the machine (Rebuild), plugs devices, takes
/// the host's samples at the frame boundary and routes every change through TimeTravelManager::SubmitLiveInput:
/// Kempston as Joystick, the Next pads as NextPad, the keyboard joysticks as ConnectorState. The keyboard joysticks
/// are an overlay on the matrix (Keyboard::SetOverlay), ANDed into the half-row reads like a stick wired in
/// parallel with the keys.
///
/// What is configuration and what is state: plugs (which device, its options) are configuration, as media are.
/// Refused while the journal owns input. While TTD records they wait for the frame boundary (ApplyPending, before
/// the checkpoint), so the checkpoint holds them and the configuration fingerprint (`controllers.<connector>`) gives
/// the recording a ConfigChange cut there: a replay crossing that frame takes the checkpoint, a seek before it
/// unplugs again. TTD v1 keeps no configuration changes: there plugs are refused while it records. The keyboard
/// joysticks' devices, options and held controls are the blob of TTDDeviceType::Controllers, on every machine.

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "controllertypes.h"
#include "emulator/io/controllers/ihapticfeed.h"
#include "debugger/ttd/engine/ttdconfigfingerprint.h"
#include "debugger/ttd/ttdinputjournal.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/io/keyboard/keyboard.h"
#include "portreadwatch.h"

class BreakpointConditionProgram;
class EmulatorContext;

namespace controllers
{

class IHostInputSource;
class IHapticFeed;
class HapticProbes;

/// Options of a connector's device (each device reads its own, the others are ignored)
struct OverlayOptions
{
    uint8_t sinclairPort = 1;  ///< sinclair-joystick: 1 (6-0) or 2 (1-5)
    bool cursorShift = false;  ///< cursor-joystick: also hold CAPS SHIFT while a direction is held
    /// keys-joystick: the key of each control bit (kRight ... kFire4); ZXKEY_NONE = unbound
    std::array<ZXKeysEnum, kOverlayControls> keys{};
    /// kempston-joystick: bits 5-7 carry fire 2-4 (an extended interface, a Sega pad); off = they read 0 (the
    /// original interface grounds D5-D7)
    bool extended = true;
    /// kempston-joystick: a cheap interface that decodes A5 = 0 without /RD answers the interrupt acknowledge too:
    /// with A5 of PC low the stick byte is the IM 2 vector (Joystick::Im2Vector)
    bool im2Leak = false;
};

struct Connector
{
    std::string id;     ///< kempston, next.left, next.right, sinclair-1, sinclair-2, fuller, keyjoy.1, keyjoy.2
    std::string alias;  ///< left / right on the Next, "" otherwise
    ConnectorKind kind = ConnectorKind::KeyboardJoystick;
    unsigned kindIndex = 0;  ///< index among the connectors of its kind (the kind:index selector)
    std::vector<RetroDeviceKind> accepts;
    bool fixed = false;  ///< the device is part of the board: cannot be unplugged or replaced
    RetroDeviceKind device = RetroDeviceKind::None;
    OverlayOptions options;
    uint16_t mask = 0;  ///< keyboard joysticks and card sockets: the controls held (the others read their device)
    const void* owner = nullptr;  ///< the card that brought the connector (ICard); null for the machine's own
};

class ControllerManager : public ttd::TTDSerializable
{
public:
    explicit ControllerManager(EmulatorContext* context);
    ~ControllerManager() override;

    /// region <Connectors>
    /// The connectors of the machine as it is now (model, port decoder). A connector whose id survives keeps its
    /// plug and options; the others are dropped. Called when the machine is built or reconfigured
    void Rebuild();
    size_t Count() const;
    /// A copy of the connector (an empty id when there is none): the list may be rebuilt from another thread
    Connector At(size_t index) const;
    std::vector<Connector> Connectors() const;
    /// id, alias or kind:index (kempston:0, next-db9:1, keyboard-joystick:0), case-insensitive; -1 when none
    int Find(const std::string& selector) const;
    /// Bumped on every plug, unplug and rebuild
    uint64_t Revision() const;
    /// endregion </Connectors>

    /// region <Plugs (configuration)>
    /// Plug @p device into the connector: fails when the connector does not accept it, is fixed, or replay owns
    /// input (TTD v1: also while it records). While TTD records the plug waits for the frame boundary (HasPending).
    /// Replacing a device releases what the old one held
    ControllerResult Plug(size_t index, RetroDeviceKind device, const OverlayOptions& options);
    ControllerResult Unplug(size_t index);
    /// MainLoop, at the frame boundary before the checkpoint: the plugs made while TTD records land here
    void ApplyPending();
    /// Plugs waiting for the frame boundary
    bool HasPending() const;
    /// Wait until the pending plugs land; false on timeout, and at once when no frame runs (paused)
    bool WaitApplied(uint32_t timeoutMs);
    /// "up:q,down:a,left:o,right:p,fire:space" -> keys; false when an entry does not parse (the rest is applied)
    static bool ParseKeys(const std::string& spec, std::array<ZXKeysEnum, kOverlayControls>& keys);
    /// The reverse of ParseKeys: the bound controls in bit order, lower case ("right:p,left:o,...")
    static std::string KeysSpec(const std::array<ZXKeysEnum, kOverlayControls>& keys);
    static OverlayOptions DefaultOptions(RetroDeviceKind device);
    /// endregion </Plugs>

    /// region <Input>
    /// The controls held on the connector now (Kempston: the joystick byte; Next: the pad's 12 bits)
    uint16_t State(size_t index) const;
    /// The controls held on the connector a card brought (ICard::Connectors): what the card's port answers from.
    /// 0 when the card has none
    uint16_t OwnedState(const void* owner) const;
    /// Live input: the whole state of a connector, through TimeTravelManager::SubmitLiveInput (applied directly
    /// without TTD). False when refused (the journal owns input) or the connector has no device
    bool Submit(size_t index, uint16_t mask);
    /// The TTD apply point of ConnectorState (live and replay): a keyboard joystick's or card socket's mask. False
    /// for other kinds
    bool ApplyConnectorState(size_t index, uint16_t mask);
    /// The TTD apply point of HostKeys (live and replay): the ZX keys host controllers hold on one half-row
    /// (active high), on the matrix overlay beside the keyboard joysticks. False for a row past 7
    bool ApplyHostKeys(uint8_t row, uint8_t bits);
    /// The host key overlay's half-row (active high)
    uint8_t HostKeys(uint8_t row) const;
    /// The host's devices: sampled at every frame boundary (OnFrame). nullptr detaches
    void SetHostInputSource(std::shared_ptr<IHostInputSource> source);
    /// The host's haptics (haptics.md §3): a summary of the frame's sound goes to it at every live frame boundary;
    /// nullptr (the default) costs one pointer check per frame
    void SetHapticFeed(std::shared_ptr<IHapticFeed> feed);
    /// The machine starts or ends replaying (a TTD seek, scrub, step back or history replay, RZX playback): the feed
    /// is told at once (IHapticFeed::OnHapticHold), not at the next live frame. The TTD controllers call it when they
    /// enter and leave replay mode, OnFrame when replay takes input or gives it back. Any thread
    void UpdateHapticHold();
    /// The game's events as the haptic feed reports them (gameevents.h): the game.events monitor. Any thread
    const GameEventLog& GameEvents() const
    {
        return _gameEvents;
    }
    bool HapticHeld() const
    {
        return _hapticHeld.load(std::memory_order_acquire);
    }
    /// MainLoop, after the frame's checkpoint: sample the host, submit what changed. While the journal owns
    /// input the samples are dropped; when it gives input back, the next sample is submitted whole (a seek
    /// restored the devices: a held button stays held, a released one is released)
    void OnFrame(uint64_t frame);
    /// endregion </Input>

    /// region <Port-read evidence (retro-devices.md §6)>
    /// Start (from zero) or stop the port-read watch; applied at the next frame boundary on the machine's thread.
    /// A media load starts it; it counts from the end of a tape's playing and stops itself after kPortReadWatchFrames
    void WatchPortReads(bool on);
    /// What the watch saw over its last window (active false when it does not run)
    PortReadEvidence PortReads() const;
    static constexpr uint32_t kPortReadWatchFrames = 1500;  ///< 30 s of play at 50 Hz
    /// endregion </Port-read evidence>

    /// The keyboard-joystick connectors that have a device
    bool HasOverlayDevices() const;
    /// `controllers.<connector>` per plugged keyboard joystick (its device and options): a plug while recording
    /// changes the fingerprint, and the recording gets a ConfigChange cut at that boundary
    void AddTtdFingerprint(ttd::TTDConfigFingerprint& fp) const;

    /// region <TTD (TTDDeviceType::Controllers)>
    /// Blob v3: version, the count of journaled connectors (keyboard joysticks and card sockets, in list order), then
    /// each one's device, Sinclair port, Cursor shift, eight keys (u8 each) and mask (u16 little-endian), then the
    /// host key overlay (8 half-rows, active high). v2 (no overlay) and v1 (masks only) still load
    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override
    {
        return "Controllers";
    }
    ttd::TTDDeviceType TTDType() const override
    {
        return ttd::TTDDeviceType::Controllers;
    }
    uint64_t TTDHashState() const override;
    /// endregion </TTD>

private:
    bool InputOwnedByReplay() const;
    int FindLocked(const std::string& selector) const;
    /// The journal event that carries @p mask to the connector's device; false when it has none
    bool EventFor(size_t index, uint16_t mask, ttd::TTDInputEvent& ev) const;
    /// Refused, or (defer) to wait for the frame boundary
    ControllerResult PlugRefused(bool& defer) const;
    /// Check a plug (None = unplug) against the connector; the lock is held
    ControllerResult CheckPlugLocked(size_t index, RetroDeviceKind device) const;
    /// Plug now; the lock is held
    void PlugLocked(size_t index, RetroDeviceKind device, const OverlayOptions& options);
    bool FramesRun() const;
    /// The overlay of every keyboard joystick, pushed to the keyboard
    void UpdateOverlay();
    /// The Kempston connector's options, pushed to the Joystick device
    void UpdateKempston();
    /// The connectors whose controls the manager holds and journals as ConnectorState
    size_t JournaledCount() const;
    /// NC_CONTROLLER_CONNECTOR_CHANGED for a change made under the lock (connector "" = the list was rebuilt)
    void QueuePostLocked(const std::string& connector, const std::string& device);
    /// Post what was queued; the lock must not be held (observers may call back)
    void FlushPosts();

    EmulatorContext* _context = nullptr;
    /// Guards the connectors: plugs come from control threads, input and TTD from the machine's thread. Never held
    /// across SubmitLiveInput (it may apply on this thread and come back through ApplyConnectorState)
    mutable std::mutex _mutex;
    std::vector<Connector> _connectors;
    uint64_t _revision = 0;
    struct PendingPlug
    {
        std::string connector;
        RetroDeviceKind device = RetroDeviceKind::None;  ///< None: unplug
        OverlayOptions options;
    };
    std::vector<PendingPlug> _pending;  ///< plugs made while TTD records, applied at the frame boundary
    std::condition_variable _applied;
    struct QueuedPost
    {
        std::string connector;
        std::string device;
        uint64_t revision = 0;
    };
    std::vector<QueuedPost> _posts;  ///< guarded by _mutex

    std::mutex _sourceMutex;
    std::shared_ptr<IHostInputSource> _source;
    std::shared_ptr<IHapticFeed> _hapticFeed;  ///< under _sourceMutex
    std::shared_ptr<IHapticFeed> _hapticFed;   ///< the feed the last frame went to: a new one starts from fresh counters
    std::vector<HapticWatch> _hapticWatches;   ///< the bytes _hapticFed reads (emulator thread)
    size_t _hapticStreams = 0;                 ///< the sound's haptic taps _hapticFed asked for
    std::unique_ptr<HapticProbes> _hapticProbes;  ///< its breakpoint probes (emulator thread)
    /// Its FrameConditions, compiled on this machine; null: did not compile or failed once (never holds again)
    std::vector<std::shared_ptr<const BreakpointConditionProgram>> _hapticConditions;
    std::vector<int> _hapticConditionEvents;   ///< per condition, its game event (the error goes there); -1: none
    std::atomic<bool> _hapticHeld{false};      ///< replay or seek: the feed was told (UpdateHapticHold)
    std::mutex _holdMutex;                     ///< UpdateHapticHold from the control thread and the frame boundary
    GameEventLog _gameEvents;                  ///< what _hapticFed reports
    bool _gameEventsOn = false;                ///< _hapticFed defined events (emulator thread)
    void FeedHaptics(uint64_t frame);
    void AttachHapticFeed(const std::shared_ptr<IHapticFeed>& feed);
    void DetachHapticFeed();
    void EvaluateHapticConditions(HapticFrame& summary);
    /// Per connector: the host's last submitted sample (-1 = none yet, submit the next one whole)
    std::vector<int32_t> _lastSample;
    bool _wasOwned = false;
    HostSample _sample;  // reused per frame
    std::array<uint8_t, 8> _hostKeys{};  ///< the host key overlay, active high (journaled, in the blob); _mutex
    uint8_t _mouseButtons = 0;           ///< the mouse buttons the host source held at its last sample

    /// The watch request (0 none, 1 start, 2 stop), taken at the frame boundary
    std::atomic<int> _watchRequest{0};
    std::atomic<bool> _watching{false};
    PortReadWatch _watch;
    void ApplyWatchRequest();

    void SubmitHostKeys(const std::vector<uint8_t>& keys);
    void SubmitMouse(const MouseSample& mouse);
};

}  // namespace controllers
