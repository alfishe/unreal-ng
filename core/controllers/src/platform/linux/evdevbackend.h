#pragma once

/// @file evdevbackend.h
/// @brief The Linux primary backend (architecture.md §4, research-host-apis.md §4): every /dev/input/event* node
/// with gamepad or joystick buttons, read on the library's `controllers-evdev` thread through poll(); hot plug by
/// inotify on /dev/input. The kernel's drivers are preferred: xpad, hid-microsoft / xpadneo, hid-playstation,
/// hid-sony and hid-nintendo name the buttons by position (BTN_SOUTH ..) and give the motors (FF_RUMBLE), the LED
/// class lights and the power supplies; the per-driver naming (platform/linuxpads.h) adds the DualSense Edge's
/// buttons (Linux 7.2), the Switch pads' Capture, the Joy-Cons held sideways or joined as one pad (JoyConPairing, as
/// on macOS) and joycond's combined device (its id the same pair id). Generic joysticks report `button.<n>` /
/// `axis.<n>`; axes are scaled by their EVIOCGABS range. A node the user cannot open (no `input` group, a Joy-Con
/// joycond took) is not shown; one that stops being readable is closed (joycond takes it), its combined device comes.
/// The hidraw node of the same HID device (hid/hidrawnode.h, found through sysfs) is opened only for what the kernel
/// driver lacks (HidrawUseFor): the Elite 2's paddles and profile over Bluetooth (HidPad), the Edge's paddles and Fn
/// buttons before 7.2 (SonyPad), the Xbox Bluetooth rumble without FF_RUMBLE (report 3), a DualSense's adaptive
/// triggers beside hid-playstation's motors (trigger-only report), the Switch pads' HD rumble beside hid-nintendo
/// (SwitchProtocol::RumbleOnly), the Surface Dial's clicks and detents; and the whole protocol of a PlayStation or
/// Switch pad under hid-generic (SonyProtocol / SwitchProtocol, as on macOS). The evdev and hidraw sides are one
/// logical device (HidrawSourceMerge). Without access to the node (no udev rule: data/controllers/70-unreal-ng.rules)
/// the device works from evdev and DeviceNotice / RumbleUnavailable say what is missing.

#include <memory>

#include "hostbackend.h"

namespace controllers
{

std::unique_ptr<IBackend> CreateEvdevBackend();

/// A pad's evdev key code (BTN_SOUTH .., xpad's paddles) -> its canonical control; nullptr for one without a name
const char* EvdevPadButton(unsigned code);

}  // namespace controllers
