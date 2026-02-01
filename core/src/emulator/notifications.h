#pragma once

#include <string>

#include "3rdparty/message-center/eventqueue.h"
#include "common/uuid.h"

/// Payload for emulator selection change notifications
/// Sent when the active/selected emulator instance changes in the CLI or UI
/// Uses cross-platform UUID class for strong typing without platform-specific dependencies
class EmulatorSelectionPayload : public MessagePayload
{
public:
    UUID previousEmulatorId;  // Nil UUID if no previous selection
    UUID newEmulatorId;       // Nil UUID if selection cleared

    /// Construct from string UUIDs (automatically parsed)
    EmulatorSelectionPayload(const std::string& prevId, const std::string& newId)
        : MessagePayload(),
          previousEmulatorId(prevId.empty() ? UUID() : UUID(prevId)),
          newEmulatorId(newId.empty() ? UUID() : UUID(newId))
    {
    }

    /// Construct from UUID objects directly
    EmulatorSelectionPayload(const UUID& prevId, const UUID& newId)
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
    UUID _emulatorId;
    uint32_t _frameCounter;

public:
    EmulatorFramePayload(const UUID& emulatorId, uint32_t counter)
        : MessagePayload()
        , _emulatorId(emulatorId)
        , _frameCounter(counter)
    {
    }
    
    /// Construct from string UUID (automatically parsed)
    EmulatorFramePayload(const std::string& emulatorId, uint32_t counter)
        : MessagePayload()
        , _emulatorId(emulatorId.empty() ? UUID() : UUID(emulatorId))
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
    UUID _emulatorId;           // UUID of the emulator instance
    uint8_t _driveId;           // Drive index (0=A, 1=B, 2=C, 3=D)
    std::string _diskPath;      // Full path to disk image file

public:
    FDDDiskPayload(const UUID& emulatorId, uint8_t driveId, const std::string& diskPath)
        : MessagePayload()
        , _emulatorId(emulatorId)
        , _driveId(driveId)
        , _diskPath(diskPath)
    {
    }
    
    /// Construct from string UUID (automatically parsed)
    FDDDiskPayload(const std::string& emulatorId, uint8_t driveId, const std::string& diskPath)
        : MessagePayload()
        , _emulatorId(emulatorId.empty() ? UUID() : UUID(emulatorId))
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
