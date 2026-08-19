#pragma once

#include <cstdint>
#include <string>

#include "3rdparty/message-center/eventqueue.h"
#include "common/uuid.h"
using unreal::UUID;  // Explicitly bring into scope to avoid Windows GUID typedef collision

class EmulatorContext;

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

/// Payload for FDD disk insert/eject notifications
/// Contains emulator ID, drive number, and disk image path
/// Example: messageCenter.Post(NC_FDD_DISK_INSERTED, new FDDDiskPayload(emulatorId, driveId, path));
class FDDDiskPayload : public MessagePayload
{
public:
    unreal::UUID _emulatorId;           // UUID of the emulator instance
    uint8_t _driveId;           // Drive index (0=A, 1=B, 2=C, 3=D)
    std::string _diskPath;      // Full path to disk image file

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
    
    virtual ~FDDDiskPayload() = default;
    
    // Helper to get drive letter from drive ID
    char getDriveLetter() const
    {
        return static_cast<char>('A' + (_driveId & 0x03));
    }
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

/// Payload for NC_EMULATOR_STATE_CHANGE.
/// `_payloadNumber` carries the new EmulatorStateEnum value (StateRun /
/// StatePaused / StateResumed / StateStopped) — same as the legacy
/// SimpleNumberPayload so legacy observers are unaffected.
class EmulatorStateChangePayload : public SimpleNumberPayload
{
public:
    unreal::UUID emulatorId;

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

    BreakpointTriggeredPayload(const unreal::UUID& id, uint32_t breakpointId, uint16_t addr)
        : SimpleNumberPayload(breakpointId), emulatorId(id), address(addr) {}

    BreakpointTriggeredPayload(const std::string& id, uint32_t breakpointId, uint16_t addr)
        : SimpleNumberPayload(breakpointId)
        , emulatorId(id.empty() ? unreal::UUID() : unreal::UUID(id))
        , address(addr)
    {}

    virtual ~BreakpointTriggeredPayload() = default;
};

/// endregion </Instance-tagged payloads (GDB TDD §6.3 prerequisite)>
