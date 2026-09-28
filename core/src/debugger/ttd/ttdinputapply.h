#pragma once

/// @file ttdinputapply.h
/// @brief Applies an input event to the machine's input devices.
///
/// The journal (ttdinputjournal.h) only stores events. This is the one place
/// that knows which device each kind drives. Live input
/// (TimeTravelManager::ApplyLiveInput), journal playback
/// (TimeTravelManager::ServiceInput) and DebugMouseManager's no-TTD fallback
/// all go through ApplyInputEvent, so live and replayed input mutate the
/// machine the same way.
///
/// Adding an input device: a field in TTDInputDevices, its lookup in
/// InputDevicesOf, its kinds in TTDInputKind and their cases in
/// ApplyInputEvent. No call site changes.

#include "ttdinputjournal.h"

class EmulatorContext;
class GeneralSoundCard;
class Keyboard;
class Mouse;

namespace ttd {

/// @brief The devices input events can drive. A null device is absent: the
/// events of its kinds are not applied.
struct TTDInputDevices
{
    Keyboard* keyboard = nullptr;
    Mouse* mouse = nullptr;
    GeneralSoundCard* generalSound = nullptr;
};

/// @brief The context's input devices at this moment. Look them up per event:
/// a General Sound personality switch replaces the card object at a frame
/// boundary.
TTDInputDevices InputDevicesOf(EmulatorContext* context);

/// @brief Apply one event to its device.
/// @return false when the event's device is absent (nothing changed).
bool ApplyInputEvent(const TTDInputEvent& ev, const TTDInputDevices& devices);

} // namespace ttd
