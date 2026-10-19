#pragma once

/// @file iohidbackend.h
/// @brief The macOS primary backend (architecture.md §4): IOHIDManager on the library's `controllers-io` thread.
/// Real VID / PID / version / serial; every HID pad through its report descriptor (HidPad), Xbox USB pads through
/// the dext's GIP pipe (XboxGipPad); Apple's synthetic 045E:028E version 0 pad is filtered out (research §2.1).

#include <memory>

#include "hostbackend.h"

namespace controllers
{

std::unique_ptr<IBackend> CreateIoHidBackend();

}  // namespace controllers
