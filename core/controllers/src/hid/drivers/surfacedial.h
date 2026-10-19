#pragma once

/// @file surfacedial.h
/// @brief The Microsoft Surface Dial (045E:091B, Bluetooth LE) and devices with its descriptor pattern: a top-level
/// collection Generic Desktop / System Multi-Axis Controller (0x01:0x0E) with a relative Dial (0x01:0x37). Input
/// report 1 (9 bytes): byte 1 bit 0 Button 1 (`push`), bit 1 Digitizer Touch 0x0D:0x33 (`touch`, the hand on the
/// dial), bytes 2-3 the Dial, int16 LE, the steps since the last report (`knob`); the rest is constant. Report 0x32
/// (System Control, sleep / wake) and the vendor reports 0x2A-0x31 are ignored. Feature report 1 holds the steps per
/// turn (Resolution Multiplier 0x01:0x48) and the Simple Haptic Controller's settings, kept for the detents of a later
/// step; output report 1 plays its clicks (HapticReport; layout in
/// testdata/controllers/hid-captures/surface-dial/macos-ble/README.md).
/// The Dial's touch sensor is slow: the hand shows up and goes away a second or more late (measured 2026-10-19), and
/// no other report carries it (vendor page 0xFF07 is the CFU firmware update channel). DialTouch infers the hand from
/// a turn or a press at once and leaves the release to the sensor.
/// Shared by every OS backend that reads the raw reports (IOHID; Windows HID and Linux hidraw later); Linux evdev
/// gives REL_DIAL and BTN_0 instead and maps them to the same names.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "hid/hiddescriptor.h"
#include "hid/hidpad.h"
#include "hostbackend.h"

namespace controllers
{

/// Feature report 1 of the Surface Dial (8 bytes): id, Resolution Multiplier (uint16), the Simple Haptic Controller's
/// Repeat Count, Auto Trigger waveform, Waveform Cutoff Time and Retrigger Period (uint16, ms). As found on the
/// owner's Dial: `01 24 00 00 01 05 50 00` (36 steps, no local clicks)
struct SurfaceDialFeature
{
    uint16_t stepsPerTurn = 36;
    uint8_t repeatCount = 0;
    uint8_t autoTrigger = 1;  ///< 1 none, 3 a click per step
    uint8_t cutoff = 5;
    uint16_t retriggerMs = 80;
};

/// The hand on the Dial, sooner than its sensor: a turn or a press means a hand on it at once; the sensor's release
/// ends that. A touch only inferred, which the sensor never confirmed (the hand left before the sensor saw it), ends
/// kUnconfirmedMs after the last action (Expire); the sensor's own touch is never cut short
class DialTouch
{
public:
    static constexpr uint32_t kUnconfirmedMs = 3000;

    /// One report (Linux: one event): the sensor's bit, a turn in it, the button held. Returns the touch to show
    bool Update(bool sensor, bool turned, bool pushed, uint64_t nowMs);
    /// No report since: returns the touch to show (an unconfirmed inferred touch may have ended)
    bool Expire(uint64_t nowMs);
    bool Touched() const
    {
        return _sensor || _inferred;
    }

private:
    bool _sensor = false;
    bool _inferred = false;
    bool _pushed = false;
    uint64_t _lastActionMs = 0;
};

/// Milliseconds of the steady clock: the time DialTouch counts in
uint64_t DialNowMs();

class SurfaceDial
{
public:
    static constexpr uint16_t kVendor = 0x045E;
    static constexpr uint16_t kProduct = 0x091B;
    static constexpr uint8_t kInputReport = 0x01;
    static constexpr uint8_t kFeatureReport = 0x01;
    static constexpr uint8_t kOutputReport = 0x01;
    static constexpr const char* kKnob = "knob";
    static constexpr const char* kPush = "push";
    static constexpr const char* kTouch = "touch";

    /// The Surface Dial by its VID:PID
    static bool Handles(uint16_t vendor, uint16_t product);
    /// A device of the Dial's pattern: a Dial usage (0x01:0x37) inside a System Multi-Axis Controller (0x01:0x0E)
    static bool Matches(const HidDescriptor& descriptor);

    /// The Surface Dial's measured layout of input report 1
    SurfaceDial();
    /// The layout read from @p descriptor (a device of the pattern); a missing button or touch is never reported
    explicit SurfaceDial(const HidDescriptor& descriptor);

    static std::vector<std::string> Controls();
    /// One input report: `knob` with the signed steps when the dial turned, `push` and `touch` on their edges (the
    /// first report gives the ones that are on; `touch` as DialTouch shows it). Other report ids are ignored
    void Decode(const uint8_t* report, size_t size, const ControlSink& sink, uint64_t nowMs);
    void Decode(const uint8_t* report, size_t size, const ControlSink& sink)
    {
        Decode(report, size, sink, DialNowMs());
    }
    /// Between reports (a backend's timer): `touch` off when an inferred touch ended
    void Expire(uint64_t nowMs, const ControlSink& sink);

    /// Output report 1 (5 bytes) of the Simple Haptic Controller: id, Repeat Count, Manual Trigger waveform (3 click,
    /// 4 buzz), Retrigger Period (uint16 LE, ms). The Dial plays the waveform @p clicks.count times (1..255; 0 is 1)
    static std::vector<uint8_t> HapticReport(const HapticClicks& clicks);

    /// Feature report 1 -> @p out; false for a report of another id or too short
    static bool ParseFeature(const uint8_t* report, size_t size, SurfaceDialFeature& out);
    /// The settings last read from the device (the defaults until a feature report is read)
    const SurfaceDialFeature& Feature() const
    {
        return _feature;
    }
    void SetFeature(const SurfaceDialFeature& feature)
    {
        _feature = feature;
    }

private:
    HidField _push;
    HidField _touch;
    HidField _dial;
    bool _hasReportIds = true;
    bool _hasPush = true;
    bool _hasTouch = true;
    bool _pushed = false;
    bool _touched = false;
    DialTouch _hand;
    SurfaceDialFeature _feature;
};

}  // namespace controllers
