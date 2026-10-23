#pragma once

/// @file evdevbackend.h
/// @brief The Linux primary backend (architecture.md §4, research-host-apis.md §4): every /dev/input/event* node
/// with gamepad or joystick buttons, read on the library's `controllers-evdev` thread through poll(); hot plug by
/// inotify on /dev/input. The kernel's drivers (xpad, hid-playstation, hid-nintendo) already name the buttons by
/// position (BTN_SOUTH ..), so no per-device driver is needed; generic joysticks report `button.<n>`. Absolute axes
/// are scaled by their EVIOCGABS range; xpad's Elite 2 paddles (BTN_GRIP*, older kernels BTN_TRIGGER_HAPPY5-8) are
/// `paddle.p1`..`p4`. A node the user cannot open (no `input` group) is reported in the probe.
/// Two devices also open the hidraw node of the same HID device (hid/hidrawnode.h), found through sysfs: the Xbox
/// Elite 2 over Bluetooth reads its raw reports through HidPad (the paddles and the onboard profile evdev does not
/// give; one logical device, HidrawSourceMerge), and the Surface Dial writes its clicks (output report 1) and detents
/// (feature report 1, HIDIOCSFEATURE) through SurfaceDialWriter. Without access to the node (no udev rule:
/// data/controllers/70-unreal-ng.rules) the device works from evdev and DeviceNotice / RumbleUnavailable say why.

#include <memory>

#include "hostbackend.h"

namespace controllers
{

std::unique_ptr<IBackend> CreateEvdevBackend();

/// A pad's evdev key code (BTN_SOUTH .., xpad's paddles) -> its canonical control; nullptr for one without a name
const char* EvdevPadButton(unsigned code);

}  // namespace controllers
