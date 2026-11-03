#pragma once

/// @file readoutschanged.h
/// @brief NC_READOUTS_CHANGED: the one notification of published state that has no other. The WebSocket hub reads
/// the control surfaces' values on events, never on a beat (ws-hub.h); a producer whose change no other notification
/// tells (a tape started while paused, a mixer row muted, the TTD session stopped) posts this, so the value a deck
/// shows follows at once. Cheap: one post; nobody listens when no surface subscribes.

#include <string>

#include "3rdparty/message-center/eventqueue.h"

/// The payload: the instance ("" = every instance, or the process) and the area that changed (diagnostics only:
/// the hub reads every value of the instance again)
class ReadoutsChangedPayload : public MessagePayload
{
public:
    std::string emulatorId;
    std::string area;  ///< "tape", "media.protect", "audio.mixer", "ttd", "debug.tools", "analysis", "window", ...

    ReadoutsChangedPayload(std::string id, std::string what) : emulatorId(std::move(id)), area(std::move(what)) {}
};

class EmulatorContext;

/// Posts NC_READOUTS_CHANGED (any thread; never waits)
void NotifyReadoutsChanged(const std::string& emulatorId, const std::string& area);
/// The same for the instance of `context` (null, or no emulator yet: every instance)
void NotifyReadoutsChanged(const EmulatorContext* context, const std::string& area);
