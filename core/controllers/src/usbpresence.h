#pragma once

/// @file usbpresence.h
/// @brief Is a USB device of a vendor attached? Matching only, never opening it: the Stream Deck daemon's supervisor
/// in unreal-qt asks for Elgato's 0x0FD9 (Stream Deck integration daemon.md §7, Q-1) while the daemon is the only
/// process that opens a deck. IOKit's registry on macOS, SetupAPI on Windows, sysfs on Linux (udev's own source).
/// Cheap (well under a millisecond on macOS and Linux, a few on Windows): polled once a second off the GUI thread.

#include <cstdint>

namespace controllers
{

constexpr uint16_t kElgatoVendorId = 0x0FD9;

/// How many USB devices of `vendorId` are attached now; -1 when this host cannot tell
int CountUsbDevices(uint16_t vendorId);

} // namespace controllers
