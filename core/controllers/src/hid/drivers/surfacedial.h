#pragma once

/// @file surfacedial.h
/// @brief The Microsoft Surface Dial (045E:091B, Bluetooth LE) and devices with its descriptor pattern: a top-level
/// collection Generic Desktop / System Multi-Axis Controller (0x01:0x0E) with a relative Dial (0x01:0x37). Input
/// report 1 (9 bytes): byte 1 bit 0 Button 1 (`push`), bit 1 Digitizer Touch 0x0D:0x33 (`touch`, the hand on the
/// dial), bytes 2-3 the Dial, int16 LE, the steps since the last report (`knob`); the rest is constant. Report 0x32
/// (System Control, sleep / wake) and the vendor reports 0x2A-0x31 are ignored. Feature report 1 holds the steps per
/// turn (Resolution Multiplier 0x01:0x48) and the Simple Haptic Controller's settings: the detents (FeatureReport,
/// written by SurfaceDialWriter); output report 1 plays its clicks (HapticReport; layout in
/// testdata/controllers/hid-captures/surface-dial/macos-ble/README.md).
/// The Dial's touch sensor is slow: the hand shows up and goes away a second or more late (measured 2026-10-19), and
/// no other report carries it (vendor page 0xFF07 is the CFU firmware update channel). DialTouch infers the hand from
/// a turn or a press at once and leaves the release to the sensor.
/// Shared by every OS backend that reads the raw reports (IOHID; Windows HID and Linux hidraw later); Linux evdev
/// gives REL_DIAL and BTN_0 instead and maps them to the same names.

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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

    /// Output report 1 for a pattern (control surfaces controls-and-feedback.md §4): `tick` one click, `double` two
    /// clicks 60 ms apart, `buzz` one buzz (its length is the device's Waveform Cutoff). Repeat Count is the number of
    /// plays (01 03 03 00 00 gave three clicks)
    static std::vector<uint8_t> PatternReport(HapticPattern pattern);
    /// Feature report 1 for the steps and clicks (devices.md §1.2): the Resolution Multiplier, Repeat Count 0, Auto
    /// Trigger 3 (a click at each step) or 1 (none: a free wheel, the steps kept), and @p base's cutoff and retrigger
    /// (the device's own: a write keeps them)
    static std::vector<uint8_t> FeatureReport(const StepsOut& steps, const SurfaceDialFeature& base = {});
    /// Feature report 1 holding @p feature as it is (the found settings written back)
    static std::vector<uint8_t> FeatureBytes(const SurfaceDialFeature& feature);

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

/// The Dial's writer thread (control surfaces architecture.md §9, §11): a report write blocks for a Bluetooth LE
/// round trip, so feature and output reports never leave from the IOHID run loop or the actions worker. Requests only
/// queue, the latest of each kind wins (a pattern not written yet is replaced: a fast turn never builds a backlog of
/// clicks), and the steps go before a pattern. Shared by every backend that writes raw reports.
/// With a Read (a backend that can get feature reports): before its first write the writer reads feature report 1, the
/// settings as found; the steps keep the found cutoff and retrigger; each feature write is read back, and one the
/// device did not take counts as failed; when it ends, a writer that changed the steps writes the found settings back
/// (the Dial keeps detents until it sleeps, so it must not stay clicking after the emulator quits)
class SurfaceDialWriter
{
public:
    enum class ReportType : uint8_t
    {
        Feature,
        Output,
    };
    /// Writes one report (its first byte is the report id); false when the device refused it
    using Write = std::function<bool(ReportType type, const std::vector<uint8_t>& report)>;
    /// Reads feature report 1 into @p report (8 bytes, the id first); false when the device did not answer
    using Read = std::function<bool(std::vector<uint8_t>& report)>;

    explicit SurfaceDialWriter(Write write, Read read = {});
    /// What is queued is dropped; a write in flight finishes first. Then the found settings are written back (Read)
    ~SurfaceDialWriter();
    SurfaceDialWriter(const SurfaceDialWriter&) = delete;
    SurfaceDialWriter& operator=(const SurfaceDialWriter&) = delete;

    void Steps(const StepsOut& steps);
    void Pattern(HapticPattern pattern);
    /// A test effect's clicks (haptics.md §10): the same slot as a pattern
    void Clicks(const HapticClicks& clicks);

    /// Reports written / refused so far (the tests, the live check); a feature write whose read-back differs is refused
    uint64_t Written() const;
    uint64_t Failed() const;
    /// Feature report 1 as the writer found it (Read); empty until it is read or without a Read
    std::optional<SurfaceDialFeature> Found() const;

private:
    void Run();
    bool WriteSteps(const StepsOut& steps);

    Write _write;
    Read _read;
    mutable std::mutex _mutex;
    std::condition_variable _wake;
    std::optional<StepsOut> _steps;
    std::optional<std::vector<uint8_t>> _output;
    std::optional<SurfaceDialFeature> _found;  ///< under _mutex
    SurfaceDialFeature _base;                  ///< the writer thread's: the device's cutoff and retrigger
    bool _changed = false;                     ///< under _mutex: a feature was written (the destructor restores)
    uint64_t _written = 0;
    uint64_t _failed = 0;
    bool _stopping = false;
    std::thread _thread;  ///< last: started when the rest exists
};

}  // namespace controllers
