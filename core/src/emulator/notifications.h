#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "3rdparty/message-center/eventqueue.h"
#include "common/uuid.h"

class EmulatorContext;

/// region <Generic payload types>

/// Allows to pass 32 bit numbers in MessageCenter message
/// Example: messageCenter.Post(topic, new SimpleNumberPayload(0x12345678));
class SimpleNumberPayload : public MessagePayload
{
public:
    uint32_t _payloadNumber;

public:
    SimpleNumberPayload(uint32_t value) : MessagePayload(), _payloadNumber(value) {}
    virtual ~SimpleNumberPayload() = default;
};

/// Allows to transfer uint8_t data blocks (as std::vector<uint8_t>) in MessageCenter message
/// std::move for parameter is mandatory since we don't want double copy for all content
/// Warning: payloads longer than 10k are not recommended.
/// Example:
///   std::vector<uint8_t> payload = { 0x00, 0x01, 0x02, 0x03 };
///   messageCenter.Post(topic, new SimpleByteDataPayload(std::move(payload)));
class SimpleByteDataPayload : public MessagePayload
{
public:
    std::vector<uint8_t> _payloadByteVector;

public:
    SimpleByteDataPayload(std::vector<uint8_t>&& payload) : MessagePayload(), _payloadByteVector(std::move(payload)) {}
    virtual ~SimpleByteDataPayload() = default;
};

/// endregion </Generic payload types>

/// Payload allowing MessageCenter notifications to be targeted to a specific emulator UUID.
class TargetContextPayload : public MessagePayload
{
public:
    unreal::UUID targetEmulatorId;

    TargetContextPayload() : MessagePayload(), targetEmulatorId() {}
    TargetContextPayload(const unreal::UUID& emulatorId) : MessagePayload(), targetEmulatorId(emulatorId) {}
    TargetContextPayload(const std::string& emulatorId)
        : MessagePayload(), targetEmulatorId(emulatorId.empty() ? unreal::UUID() : unreal::UUID(emulatorId)) {}

    virtual ~TargetContextPayload() = default;
};

/// Payload for emulator selection change notifications
/// Sent when the active/selected emulator instance changes in the CLI or UI
/// Uses cross-platform UUID class for strong typing without platform-specific dependencies
class EmulatorSelectionPayload : public MessagePayload
{
public:
    unreal::UUID previousEmulatorId;  // Nil UUID if no previous selection
    unreal::UUID newEmulatorId;       // Nil UUID if selection cleared

    /// Construct from string UUIDs (automatically parsed)
    EmulatorSelectionPayload(const std::string& prevId, const std::string& newId)
        : MessagePayload(),
          previousEmulatorId(prevId.empty() ? unreal::UUID() : unreal::UUID(prevId)),
          newEmulatorId(newId.empty() ? unreal::UUID() : unreal::UUID(newId))
    {
    }

    /// Construct from UUID objects directly
    EmulatorSelectionPayload(const unreal::UUID& prevId, const unreal::UUID& newId)
        : MessagePayload(), previousEmulatorId(prevId), newEmulatorId(newId)
    {
    }

    virtual ~EmulatorSelectionPayload() = default;
};

/// Payload for emulator frame refresh notifications
/// Contains emulator ID and frame counter
/// Used for per-instance frame refresh events
/// Example: messageCenter.Post(NC_VIDEO_FRAME_REFRESH, new EmulatorFramePayload(emulatorId, 12345));
class EmulatorFramePayload : public MessagePayload
{
public:
    unreal::UUID _emulatorId;
    uint32_t _frameCounter;

public:
    EmulatorFramePayload(const unreal::UUID& emulatorId, uint32_t counter)
        : MessagePayload()
        , _emulatorId(emulatorId)
        , _frameCounter(counter)
    {
    }
    
    /// Construct from string UUID (automatically parsed)
    EmulatorFramePayload(const std::string& emulatorId, uint32_t counter)
        : MessagePayload()
        , _emulatorId(emulatorId.empty() ? unreal::UUID() : unreal::UUID(emulatorId))
        , _frameCounter(counter)
    {
    }
    
    virtual ~EmulatorFramePayload() = default;
};

/// Snapshot of the floppy subsystem as seen by the UI (status bar LED / tooltip).
/// Produced by WD1793::getFDDState() (initial read when a UI binds to an emulator)
/// and carried by FDDStatePayload / NC_FDD_STATE_CHANGED on every change afterwards,
/// so consumers cache it and never poll the controller.
struct FDDStateInfo
{
    uint8_t driveId = 0;        // Selected drive (0=A, 1=B, 2=C, 3=D)
    uint8_t track = 0;          // Physical head position (cylinder) of the selected drive
    uint8_t side = 0;           // Selected side / head (0=bottom, 1=top)
    uint8_t sector = 0;         // WD1793 sector register
    bool motorOn = false;       // Spindle motor running
    bool busy = false;          // Command in progress
    bool diskInserted = false;  // Image mounted in the selected drive
    bool writeProtected = false;

    bool operator==(const FDDStateInfo& o) const
    {
        return driveId == o.driveId && track == o.track && side == o.side && sector == o.sector &&
               motorOn == o.motorOn && busy == o.busy && diskInserted == o.diskInserted &&
               writeProtected == o.writeProtected;
    }
    bool operator!=(const FDDStateInfo& o) const { return !(*this == o); }

    char getDriveLetter() const { return static_cast<char>('A' + (driveId & 0x03)); }
};

/// NC_FDD_STATE_CHANGED payload: the emulator instance plus its current FDDStateInfo
class FDDStatePayload : public MessagePayload
{
public:
    unreal::UUID _emulatorId;  // UUID of the emulator instance
    FDDStateInfo _state;

public:
    FDDStatePayload(const std::string& emulatorId, const FDDStateInfo& state)
        : MessagePayload()
        , _emulatorId(emulatorId.empty() ? unreal::UUID() : unreal::UUID(emulatorId))
        , _state(state)
    {
    }

    virtual ~FDDStatePayload() = default;
};

/// Payload for FDD disk insert/eject notifications
/// Contains emulator ID, drive number, and disk image path
/// Example: messageCenter.Post(NC_FDD_DISK_INSERTED, new FDDDiskPayload(emulatorId, driveId, path));
class FDDDiskPayload : public MessagePayload
{
public:
    unreal::UUID _emulatorId;           // UUID of the emulator instance
    uint8_t _driveId;           // Drive index (0=A, 1=B, 2=C, 3=D)
    std::string _diskPath;      // Full path to disk image file
    std::string _reason;        // NC_FDD_DISK_SAVE_RETARGETED: why the original format refused the image

public:
    FDDDiskPayload(const unreal::UUID& emulatorId, uint8_t driveId, const std::string& diskPath)
        : MessagePayload()
        , _emulatorId(emulatorId)
        , _driveId(driveId)
        , _diskPath(diskPath)
    {
    }
    
    /// Construct from string UUID (automatically parsed)
    FDDDiskPayload(const std::string& emulatorId, uint8_t driveId, const std::string& diskPath)
        : MessagePayload()
        , _emulatorId(emulatorId.empty() ? unreal::UUID() : unreal::UUID(emulatorId))
        , _driveId(driveId)
        , _diskPath(diskPath)
    {
    }

    /// Retargeted save: the original format refused the image, it was saved as diskPath (UDI) instead
    FDDDiskPayload(const std::string& emulatorId, uint8_t driveId, const std::string& diskPath, const std::string& reason)
        : MessagePayload()
        , _emulatorId(emulatorId.empty() ? unreal::UUID() : unreal::UUID(emulatorId))
        , _driveId(driveId)
        , _diskPath(diskPath)
        , _reason(reason)
    {
    }
    
    virtual ~FDDDiskPayload() = default;
    
    // Helper to get drive letter from drive ID
    char getDriveLetter() const
    {
        return static_cast<char>('A' + (_driveId & 0x03));
    }
};

/// Snapshot of WD1793 visible state for NC_FDC_STATE_CHANGED.
/// Posted by the FDC only when one of the display-relevant fields changes.
/// Always a complete snapshot — observers never need to merge deltas.
class FDCStatePayload : public MessagePayload
{
public:
    unreal::UUID _emulatorId;      // Owning emulator instance (nil if unknown)
    uint8_t _driveId = 0;          // Selected drive index [0..3] (0 = A)
    uint8_t _side = 0;             // 0 = bottom, 1 = top
    uint8_t _trackRegister = 0;    // WD1793 track register value
    uint8_t _sectorRegister = 0;   // WD1793 sector register value
    uint8_t _physicalTrack = 0;    // FDD head position (actual cylinder)
    uint8_t _command = 0;          // Last command byte (with flag bits)
    uint8_t _status = 0;           // Status register snapshot
    bool _busy = false;            // WDS_BUSY
    bool _drq = false;             // DRQ output
    bool _motorOn = false;         // Selected drive motor state
    bool _diskInserted = false;    // Selected drive has a disk

    FDCStatePayload() = default;
    explicit FDCStatePayload(const unreal::UUID& emulatorId) : _emulatorId(emulatorId) {}
    virtual ~FDCStatePayload() = default;

    // Helper to get drive letter from drive ID
    char getDriveLetter() const
    {
        return static_cast<char>('A' + (_driveId & 0x03));
    }
};

/// NC_CPU_FREQ_CHANGED payload: emulator instance and new CPU frequency.
///
/// _frequencyHz / _freqMultiplier always report the real, current, applied
/// value - every consumer that just wants "what speed is it running at right
/// now" (automation, tests, the WebAPI) reads exactly those two fields and
/// gets it immediately on every logical change, same as before this payload
/// grew the oscillation fields below.
///
/// _oscillating / _oscLowHz / _oscHighHz are a display hint, nothing more:
/// some guest software (TS-Conf's Wild Commander AY player is the one this
/// was built for) flips SYS_CONFIG's clock bits every single frame as a
/// software turbo trick, so "the current value" changes 50x/sec and is
/// meaningless to show as a single number. Z80 classifies this at the
/// source (see Z80::NotifyCPUFrequencyChanged) and sets _oscillating with
/// the two bouncing values in _oscLowHz/_oscHighHz once flips start arriving
/// faster than about 1/sec; it clears back to false after ~3s of a settled
/// value. A UI that cares (StatusBarManager) renders "lo<->hi" while
/// _oscillating is set and the plain _frequencyHz otherwise. A consumer that
/// ignores these three fields entirely still sees fully correct behavior.
class CPUFreqPayload : public MessagePayload
{
public:
    unreal::UUID _emulatorId;
    uint32_t _frequencyHz;     // Actual frequency in Hz (e.g., 3500000, 7000000, 14000000)
    uint8_t _freqMultiplier;   // Multiplier relative to base (1, 2, 4, etc.)
    bool _oscillating = false;  // Display hint: the clock is flipping faster than ~1/sec
    uint32_t _oscLowHz = 0;     // Valid only when _oscillating: the lower of the two bouncing values
    uint32_t _oscHighHz = 0;    // Valid only when _oscillating: the higher of the two bouncing values

public:
    CPUFreqPayload(const std::string& emulatorId, uint32_t frequencyHz, uint8_t freqMultiplier,
                   bool oscillating = false, uint32_t oscLowHz = 0, uint32_t oscHighHz = 0)
        : MessagePayload()
        , _emulatorId(emulatorId.empty() ? unreal::UUID() : unreal::UUID(emulatorId))
        , _frequencyHz(frequencyHz)
        , _freqMultiplier(freqMultiplier)
        , _oscillating(oscillating)
        , _oscLowHz(oscLowHz)
        , _oscHighHz(oscHighHz)
    {
    }

    virtual ~CPUFreqPayload() = default;
};

/// Payload for requesting single sync mode in videowall
/// Example: messageCenter.Post(NC_VIDEOWALL_SINGLE_SYNC_MODE, new VideowallSyncModePayload(emulatorId, true));
class VideowallSyncModePayload : public MessagePayload
{
public:
    unreal::UUID _emulatorId;
    bool _enable;

public:
    VideowallSyncModePayload(const unreal::UUID& emulatorId, bool enable)
        : MessagePayload()
        , _emulatorId(emulatorId)
        , _enable(enable)
    {
    }
    
    VideowallSyncModePayload(const std::string& emulatorId, bool enable)
        : MessagePayload()
        , _emulatorId(emulatorId.empty() ? unreal::UUID() : unreal::UUID(emulatorId))
        , _enable(enable)
    {
    }
    
    virtual ~VideowallSyncModePayload() = default;
};


/// region <Instance-tagged payloads (GDB TDD §6.3 prerequisite)>
//
// These payloads inherit from SimpleNumberPayload so that existing observers
// reading `_payloadNumber` keep working unchanged. New observers (GDB stub,
// per-instance videowall, TTD seek) dynamic_cast to read the instance UUID.
//
// The UUID is mandatory at the post site; if a caller cannot identify the
// instance it should pass a nil UUID (default-constructed) — observers that
// still ignore the field continue to work, but instance-filtered observers
// will treat nil as "does not match my instance".

/// What kind of access a breakpoint fired on (BreakpointTriggeredPayload's address is the
/// PC, the memory address or the port accordingly)
enum class BreakpointHitKind : uint8_t
{
    Execute = 0,
    MemoryRead,
    MemoryWrite,
    PortIn,
    PortOut
};

/// The automation surfaces' name of a breakpoint access kind
inline const char* BreakpointHitKindName(BreakpointHitKind kind)
{
    switch (kind)
    {
        case BreakpointHitKind::Execute: return "execute";
        case BreakpointHitKind::MemoryRead: return "read";
        case BreakpointHitKind::MemoryWrite: return "write";
        case BreakpointHitKind::PortIn: return "port_in";
        case BreakpointHitKind::PortOut: return "port_out";
    }
    return "execute";
}

/// Why the emulator paused, carried with NC_EMULATOR_STATE_CHANGE (StatePaused)
enum class PauseCause : uint8_t
{
    Request = 0,  // Pause() called by a user, a surface or the machine
    Breakpoint    // a breakpoint fired during the emulator's own run
};

/// Payload for NC_EMULATOR_STATE_CHANGE.
/// `_payloadNumber` carries the new EmulatorStateEnum value (StateRun /
/// StatePaused / StateResumed / StateStopped) — same as the legacy
/// SimpleNumberPayload so legacy observers are unaffected.
class EmulatorStateChangePayload : public SimpleNumberPayload
{
public:
    unreal::UUID emulatorId;
    // StatePaused only: the cause, and for a breakpoint which one and where. Set at the
    // post: the notification is delivered later, when the emulator may have moved on
    PauseCause pauseCause{PauseCause::Request};
    uint16_t breakpointId{0xFFFF};
    uint16_t address{0};
    BreakpointHitKind hitKind{BreakpointHitKind::Execute};

    EmulatorStateChangePayload(const unreal::UUID& id, uint32_t newState)
        : SimpleNumberPayload(newState), emulatorId(id) {}

    EmulatorStateChangePayload(const std::string& id, uint32_t newState)
        : SimpleNumberPayload(newState)
        , emulatorId(id.empty() ? unreal::UUID() : unreal::UUID(id))
    {}

    virtual ~EmulatorStateChangePayload() = default;
};

/// Payload for NC_EXECUTION_BREAKPOINT.
/// `_payloadNumber` carries the breakpoint ID — same as the legacy
/// SimpleNumberPayload so legacy observers are unaffected. Adds the instance
/// UUID and the Z80 address that triggered the hit (PC for execution, target
/// address for memory R/W, port for I/O) so GDB and the videowall can both
/// filter by instance and report a precise stop location.
class BreakpointTriggeredPayload : public SimpleNumberPayload
{
public:
    unreal::UUID emulatorId;
    uint16_t    address;   // Z80 address that triggered (0 if unknown / N/A)
    bool        hidden{false};

    BreakpointTriggeredPayload(const unreal::UUID& id, uint32_t breakpointId, uint16_t addr, bool isHidden = false)
        : SimpleNumberPayload(breakpointId), emulatorId(id), address(addr), hidden(isHidden) {}

    BreakpointTriggeredPayload(const std::string& id, uint32_t breakpointId, uint16_t addr, bool isHidden = false)
        : SimpleNumberPayload(breakpointId)
        , emulatorId(id.empty() ? unreal::UUID() : unreal::UUID(id))
        , address(addr)
        , hidden(isHidden)
    {}

    virtual ~BreakpointTriggeredPayload() = default;
};

/// Payload of NC_EXECUTION_CPU_STEP posted when a direct run ends (Emulator::DirectStepScope:
/// RunSingleCPUCycle, RunNCPUCycles, RunFrame, RunTStates, RunUntil*). The other posts of
/// NC_EXECUTION_CPU_STEP carry no payload. `stopped`: a breakpoint ended the run early
class CpuStepPayload : public MessagePayload
{
public:
    unreal::UUID emulatorId;
    bool stopped{false};
    uint16_t breakpointId{0xFFFF};
    uint16_t address{0};
    BreakpointHitKind hitKind{BreakpointHitKind::Execute};

    explicit CpuStepPayload(const unreal::UUID& id) : emulatorId(id) {}
    explicit CpuStepPayload(const std::string& id) : emulatorId(id.empty() ? unreal::UUID() : unreal::UUID(id)) {}
    virtual ~CpuStepPayload() = default;
};

/// Payload of NC_BREAKPOINTS_CHANGED: the breakpoint set of this emulator changed (added,
/// removed, enabled, disabled, regrouped)
class BreakpointsChangedPayload : public MessagePayload
{
public:
    unreal::UUID emulatorId;
    std::string cpu = "main";        ///< which CPU's breakpoints (protocol.md §5.2)
    std::vector<uint16_t> ids;       ///< the breakpoints added, removed or changed (hidden ones never)

    explicit BreakpointsChangedPayload(const unreal::UUID& id) : emulatorId(id) {}
    explicit BreakpointsChangedPayload(const std::string& id) : emulatorId(id.empty() ? unreal::UUID() : unreal::UUID(id)) {}
    virtual ~BreakpointsChangedPayload() = default;
};

/// Payload for NC_FEATURE_CHANGED.
/// Posted by FeatureManager::onFeatureChanged() AFTER all UpdateFeatureCache()
/// calls are complete, so every consumer sees consistent cached state.
/// Informational: caches are already up to date when this fires.
class FeatureChangedPayload : public MessagePayload
{
public:
    unreal::UUID emulatorId;
    std::string featureId;   // Canonical feature ID (e.g. "hud", "sound", "timetravel")
    bool enabled;

    FeatureChangedPayload(const unreal::UUID& id, std::string feature, bool on)
        : MessagePayload(), emulatorId(id), featureId(std::move(feature)), enabled(on) {}

    FeatureChangedPayload(const std::string& id, std::string feature, bool on)
        : MessagePayload()
        , emulatorId(id.empty() ? unreal::UUID() : unreal::UUID(id))
        , featureId(std::move(feature))
        , enabled(on)
    {}

    virtual ~FeatureChangedPayload() = default;
};

/// Payload for NC_SPEED_CHANGED.
/// Posted by Core::SetSpeedMultiplier / EnableTurboMode / DisableTurboMode
/// after state is committed.
class SpeedChangedPayload : public MessagePayload
{
public:
    unreal::UUID emulatorId;
    uint8_t multiplier;   // 1, 2, 4, 8, 16
    bool turboMode;       // true = max-speed mode active

    SpeedChangedPayload(const unreal::UUID& id, uint8_t mult, bool isTurbo)
        : MessagePayload(), emulatorId(id), multiplier(mult), turboMode(isTurbo) {}

    SpeedChangedPayload(const std::string& id, uint8_t mult, bool isTurbo)
        : MessagePayload()
        , emulatorId(id.empty() ? unreal::UUID() : unreal::UUID(id))
        , multiplier(mult)
        , turboMode(isTurbo)
    {}

    virtual ~SpeedChangedPayload() = default;
};

/// Payload for NC_MEDIA_* (media manager slot events)
class MediaSlotPayload : public MessagePayload
{
public:
    unreal::UUID emulatorId;
    std::string slotId;   // "sd.zc", "fdd.a", ...
    std::string kind;     // "block", "floppy", "tape", "optical"
    std::string source;   // source path or description; empty after an eject
    std::string access;   // "readonly", "session", "writethrough"
    std::string path;     // NC_MEDIA_EXPORTED / NC_MEDIA_SAVED: the file written

    MediaSlotPayload(const std::string& id, std::string slot, std::string mediaKind, std::string mediaSource,
                     std::string accessMode)
        : MessagePayload()
        , emulatorId(id.empty() ? unreal::UUID() : unreal::UUID(id))
        , slotId(std::move(slot))
        , kind(std::move(mediaKind))
        , source(std::move(mediaSource))
        , access(std::move(accessMode))
    {}
};

/// Payload for NC_FILE_LOADED.
/// Posted by Emulator::LoadSnapshot / LoadTape / LoadDisk after the loader
/// returns. Carries the result so consumers can show success or failure toasts.
class FileLoadedPayload : public MessagePayload
{
public:
    unreal::UUID emulatorId;
    std::string kind;     // "snapshot", "tape", "disk"
    std::string path;     // full path of the file
    bool ok;              // false = load failed

    FileLoadedPayload(const unreal::UUID& id, std::string fileKind, std::string filePath, bool success)
        : MessagePayload()
        , emulatorId(id)
        , kind(std::move(fileKind))
        , path(std::move(filePath))
        , ok(success)
    {}

    FileLoadedPayload(const std::string& id, std::string fileKind, std::string filePath, bool success)
        : MessagePayload()
        , emulatorId(id.empty() ? unreal::UUID() : unreal::UUID(id))
        , kind(std::move(fileKind))
        , path(std::move(filePath))
        , ok(success)
    {}

    virtual ~FileLoadedPayload() = default;
};

/// Payload for NC_RZX_PLAYBACK.
/// Posted by RzxSession (emulator/rzx/): event is "started", "snapshot" (a
/// snapshot block applied), "seek", "finished", "desync", "stopped" or "failed"; message says why (a desync: the frame,
/// expected and actual counts). Example: {"desync", "desync (too_few_ins) in
/// frame 1200: expected 3, got 2", 1200, 32315, "/games/eric.rzx"}
class RzxPlaybackPayload : public MessagePayload
{
public:
    unreal::UUID emulatorId;
    std::string event;
    std::string message;
    uint64_t frame;        // frames played when the event happened
    uint64_t totalFrames;
    std::string path;      // the RZX file

    RzxPlaybackPayload(const std::string& id, std::string eventName, std::string text, uint64_t framesPlayed,
                       uint64_t framesTotal, std::string filePath)
        : MessagePayload()
        , emulatorId(id.empty() ? unreal::UUID() : unreal::UUID(id))
        , event(std::move(eventName))
        , message(std::move(text))
        , frame(framesPlayed)
        , totalFrames(framesTotal)
        , path(std::move(filePath))
    {}

    virtual ~RzxPlaybackPayload() = default;
};

/// Payload for NC_DISK_AUTOSTART.
/// Posted by Emulator::AutostartDisk with the decision taken for a freshly opened disk.
class DiskAutostartPayload : public MessagePayload
{
public:
    unreal::UUID emulatorId;
    std::string message;  // Human readable outcome, e.g. "Autostart: GAME" or "This machine has no TR-DOS"
    bool started;         // true = the machine was reset into TR-DOS to start the disk
    bool error;           // true = the disk could not be autostarted because of a problem (HUD warning)

    DiskAutostartPayload(const unreal::UUID& id, std::string text, bool wasStarted, bool isError)
        : MessagePayload()
        , emulatorId(id)
        , message(std::move(text))
        , started(wasStarted)
        , error(isError)
    {}

    virtual ~DiskAutostartPayload() = default;
};

/// Recording type (video, audio, or both)
enum class RecordingType : uint8_t
{
    Video,      // Video-only recording
    Audio,      // Audio-only recording
    VideoAudio  // Video + audio recording (default)
};

/// Payload for NC_RECORDING_STATE.
/// Posted by RecordingManager on start / stop / pause / resume.
class RecordingStatePayload : public MessagePayload
{
public:
    unreal::UUID emulatorId;
    bool recording;       // true = started/active, false = stopped
    bool paused;          // true = paused (only valid when recording = true)
    RecordingType type;   // what is being recorded
    std::string path;     // output file path (meaningful on stop)

    RecordingStatePayload(const unreal::UUID& id, bool isRecording, std::string filePath = {},
                          RecordingType recType = RecordingType::VideoAudio, bool isPaused = false)
        : MessagePayload()
        , emulatorId(id)
        , recording(isRecording)
        , paused(isPaused)
        , type(recType)
        , path(std::move(filePath))
    {}

    RecordingStatePayload(const std::string& id, bool isRecording, std::string filePath = {},
                          RecordingType recType = RecordingType::VideoAudio, bool isPaused = false)
        : MessagePayload()
        , emulatorId(id.empty() ? unreal::UUID() : unreal::UUID(id))
        , recording(isRecording)
        , paused(isPaused)
        , type(recType)
        , path(std::move(filePath))
    {}

    virtual ~RecordingStatePayload() = default;
};

/// Payload for NC_MEMORY_PAGE_CHANGED.
/// Posted once per frame at frame end with accumulated switch info.
class MemoryPagePayload : public MessagePayload
{
public:
    unreal::UUID emulatorId;
    uint8_t bank;         // CPU address space bank index (0-3: 0000-3FFF, 4000-7FFF, 8000-BFFF, C000-FFFF)
    uint8_t page;         // Current physical RAM page mapped to this bank
    uint8_t minPage;      // Minimum page seen this frame (for rapid-switch display)
    uint8_t maxPage;      // Maximum page seen this frame
    uint8_t switchCount;  // Number of switches this frame (0 = no change)

    MemoryPagePayload(const unreal::UUID& id, uint8_t bankIndex, uint8_t curPage,
                      uint8_t minP, uint8_t maxP, uint8_t count)
        : MessagePayload()
        , emulatorId(id)
        , bank(bankIndex)
        , page(curPage)
        , minPage(minP)
        , maxPage(maxP)
        , switchCount(count)
    {}

    virtual ~MemoryPagePayload() = default;
};

/// Payload for NC_ROM_PAGE_CHANGED.
/// Posted once per frame at frame end with accumulated switch info.
class ROMPagePayload : public MessagePayload
{
public:
    unreal::UUID emulatorId;
    uint8_t page;         // Current ROM page index (absolute: plane-composed on ProfROM)
    uint8_t minPage;      // Minimum page seen this frame
    uint8_t maxPage;      // Maximum page seen this frame
    uint8_t switchCount;  // Number of switches this frame (0 = no change)

    // ProfROM planes (quadrants). Only the Scorpion ProfROM variant has them, so
    // consumers must gate on planeAware rather than infer a plane from the page:
    // on every other model the ROM page is a plain index with no plane geometry.
    bool planeAware = false;  // Machine carries ProfROM planes
    uint8_t plane = 0;        // Current plane
    uint8_t minPlane = 0;     // Minimum plane seen this frame
    uint8_t maxPlane = 0;     // Maximum plane seen this frame

    ROMPagePayload(const unreal::UUID& id, uint8_t curPage, uint8_t minP, uint8_t maxP, uint8_t count)
        : MessagePayload()
        , emulatorId(id)
        , page(curPage)
        , minPage(minP)
        , maxPage(maxP)
        , switchCount(count)
    {}

    virtual ~ROMPagePayload() = default;
};

/// Payload for NC_SCREEN_PAGE_CHANGED.
/// Posted once per frame at frame end with accumulated switch info.
class ScreenPagePayload : public MessagePayload
{
public:
    unreal::UUID emulatorId;
    uint8_t screen;       // Current screen (0 = normal/page 5, 1 = shadow/page 7)
    uint8_t switchCount;  // Number of switches this frame (0 = no change)

    ScreenPagePayload(const unreal::UUID& id, uint8_t curScreen, uint8_t count)
        : MessagePayload()
        , emulatorId(id)
        , screen(curScreen)
        , switchCount(count)
    {}

    virtual ~ScreenPagePayload() = default;
};

/// Audio source types for NC_AUDIO_ACTIVITY
enum class AudioSource : uint8_t
{
    Beeper = 0,
    Covox = 1,
    AY = 2,         // Single AY-3-8910 / YM2149
    TurboSound = 3, // TurboSound (dual AY)
    TSFM = 4,       // TurboSound FM (dual YM2203: AY + FM) - legacy, kept for compatibility
    FM = 5,         // FM part of YM2203 (separate from AY/TS)
    GeneralSound = 6, // General Sound card (dedicated Z80 + 4xDAC)
    MoonFM = 7,       // MoonSound (OPL4/YMF278B) FM synthesis part
    MoonPCM = 8,      // MoonSound (OPL4/YMF278B) wave sample/PCM part
    NeoGS = 9,        // NeoGS card in the GS slot: its DAC channels (GeneralSound is then silent)
    NeoGSMp3 = 10,    // NeoGS MP3 decoder output
    NeoGSDma = 11,    // NeoGS: the card's own DMA (SD card, MP3 decoder) moving data - not sound
    NeoGSTransfer = 12, // NeoGS: ZX-DMA moving data between the ZX and the card - not sound
    CdAudio = 13        // CD-DA from an ATAPI CD drive (any IDE unit)
};

/// Payload for NC_AUDIO_ACTIVITY.
/// Posted by SoundManager (AudioActivityIndicators) from the audio-settings
/// LEDs: every frame while a source is held active, once when it goes silent.
class AudioActivityPayload : public MessagePayload
{
public:
    unreal::UUID emulatorId;
    AudioSource source;   // Which audio source
    bool active;          // true = started producing sound, false = went silent

    AudioActivityPayload(const unreal::UUID& id, AudioSource src, bool isActive)
        : MessagePayload()
        , emulatorId(id)
        , source(src)
        , active(isActive)
    {}

    virtual ~AudioActivityPayload() = default;
};

/// endregion </Instance-tagged payloads (GDB TDD §6.3 prerequisite)>
