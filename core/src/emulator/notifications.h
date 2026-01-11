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
