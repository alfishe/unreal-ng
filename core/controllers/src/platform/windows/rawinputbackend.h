#pragma once

/// @file rawinputbackend.h
/// @brief The Windows HID backend (architecture.md §4, research-host-apis.md §3.2): the shared HID drivers over Windows'
/// HID stack, on the library's `controllers-rawinput` thread with a message-only window.
/// - Pads: Raw Input (joystick, game pad, multi-axis controller; RIDEV_INPUTSINK, so they read in the background like
///   the IOHID ones, and RIDEV_DEVNOTIFY for hot plug). Windows hands out no report descriptor, so the HidPad's fields
///   are located from the preparsed data: each usage is written all zeros and all ones with HidP_SetUsageValue /
///   HidP_SetUsages and the bits that changed are its place (LocateHidBits). XInput-class endpoints (`&IG_` in the
///   path) are skipped: GameInput or XInput serves those pads (one device once). A DualSense over USB rumbles through
///   its output report 2 (DualSenseOutput), written with WriteFile.
/// - The Surface Dial and devices of its pattern (top-level collection 0x01:0x0E, surface-dial.md §4): found with
///   SetupDi on the HID interface class and RegisterDeviceNotification, read with overlapped ReadFile into
///   SurfaceDial::Decode; its clicks and steps go through a SurfaceDialWriter with HidD_SetOutputReport /
///   HidD_SetFeature. The thread wakes 10 times a second while a Dial is open, for the inferred touch's end.

#include <memory>

#include "hostbackend.h"

namespace controllers
{

std::unique_ptr<IBackend> CreateRawInputBackend();

}  // namespace controllers
