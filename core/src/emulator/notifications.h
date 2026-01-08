#pragma once

#include <uuid/uuid.h>

#include <string>

#include "3rdparty/message-center/eventqueue.h"

/// Payload for emulator selection change notifications
/// Sent when the active/selected emulator instance changes in the CLI or UI
class EmulatorSelectionPayload : public MessagePayload
{
public:
    uuid_t previousEmulatorId;  // Empty UUID if no previous selection
    uuid_t newEmulatorId;       // Empty UUID if selection cleared

    EmulatorSelectionPayload(const std::string& prevId, const std::string& newId) : MessagePayload()
    {
        // Parse string UUIDs into uuid_t format
        // If parsing fails or string is empty, uuid will be cleared (all zeros)
        if (prevId.empty())
        {
            uuid_clear(previousEmulatorId);
        }
        else
        {
            uuid_parse(prevId.c_str(), previousEmulatorId);
        }

        if (newId.empty())
        {
            uuid_clear(newEmulatorId);
        }
        else
        {
            uuid_parse(newId.c_str(), newEmulatorId);
        }
    }

    // Overloaded constructor accepting uuid_t directly
    EmulatorSelectionPayload(const uuid_t prevId, const uuid_t newId) : MessagePayload()
    {
        uuid_copy(previousEmulatorId, prevId);
        uuid_copy(newEmulatorId, newId);
    }

    virtual ~EmulatorSelectionPayload() = default;
};
