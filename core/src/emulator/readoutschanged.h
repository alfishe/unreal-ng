#pragma once

/// @file readoutschanged.h
/// @brief NC_READOUTS_CHANGED: the one notification of published state that has no other. The WebSocket hub reads
/// the control surfaces' values on events, never on a beat (ws-hub.h); a producer whose change no other notification
/// tells (a tape started while paused, a mixer row muted, the TTD session stopped) posts this, so the value a deck
/// shows follows at once. Cheap: one post; nobody listens when no surface subscribes.

#include <string>

#include "3rdparty/message-center/eventqueue.h"

/// The payload: the instance ("" = every instance, or the process) and the area that changed (the hub reads every
/// value of the instance again; kFrameArea: only the values that move with emulated time)
class ReadoutsChangedPayload : public MessagePayload
{
public:
    std::string emulatorId;
    std::string area;  ///< "tape", "media.protect", "audio.mixer", "ttd", "debug.tools", "analysis", "window", ...

    ReadoutsChangedPayload(std::string id, std::string what) : emulatorId(std::move(id)), area(std::move(what)) {}
};

class EmulatorContext;

/// The area of a frame's published state (the TTD cursor written at the frame boundary): NC_VIDEO_FRAME_REFRESH is
/// posted before it, so a reader woken by that may still see the previous frame's cursor
inline constexpr const char* kFrameArea = "frame";

/// Somebody reads values that move with an instance's emulated time (the WebSocket hub's frame watch). Off: a frame's
/// publish posts nothing (one relaxed load)
void SetFrameReadersWanted(bool wanted);
bool FrameReadersWanted();
/// The frame's published state of `context`'s instance is written (TimeTravelController::PublishCursor, a changed
/// cursor): NC_READOUTS_CHANGED with kFrameArea while FrameReadersWanted()
void NotifyFramePublished(const EmulatorContext* context);

/// Posts NC_READOUTS_CHANGED (any thread; never waits)
void NotifyReadoutsChanged(const std::string& emulatorId, const std::string& area);
/// The same for the instance of `context` (null, or no emulator yet: every instance)
void NotifyReadoutsChanged(const EmulatorContext* context, const std::string& area);
