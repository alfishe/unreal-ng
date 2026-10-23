#pragma once

/// @file gccompanion.h
/// @brief The macOS companion endpoint (architecture.md §4, D18): GameController.framework next to the IOHID
/// primary. It reports each `GCController` (its vendor name; GameController hides VID / PID) and the presses of its
/// face and shoulder buttons, for EndpointMerge to match it to its IOHID device. It never gives input: every control
/// it reports is in the raw HID report as well, and the Elite paddles it never reports (research-host-apis.md §2.3c).
/// Once matched it drives what IOHID cannot: the motors through GCDeviceHaptics (CoreHaptics, the handle and trigger
/// localities), e.g. a DualSense over Bluetooth. Its battery and light are not used yet: IBackend has no surface for
/// them.
///
/// Everything runs on one private serial queue; GameController posts its connect notifications on the main thread,
/// so a process without a main run loop (the tests, a headless CLI) sees no controllers: harmless.
/// C++ header (no Objective-C types): the backend is C++.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "hostbackend.h"

namespace controllers
{

class GcCompanion
{
public:
    struct Pad
    {
        std::string key;         ///< "gc#<n>", unique while the process runs
        std::string vendorName;  ///< GCController.vendorName
        uint8_t actuators = 0;   ///< RumbleActuator bits from its haptic localities; 0 without haptics
    };
    struct Callbacks
    {
        std::function<void(const Pad& pad)> added;
        std::function<void(const std::string& key)> removed;
        /// A face or shoulder button went down; @p timeMs in the steady clock's milliseconds, when it reached us
        std::function<void(const std::string& key, uint64_t timeMs)> pressed;
    };

    GcCompanion();
    ~GcCompanion();
    GcCompanion(const GcCompanion&) = delete;
    GcCompanion& operator=(const GcCompanion&) = delete;

    /// Start reporting; the callbacks run on the companion's queue. False with the reason when GameController is not
    /// usable here
    bool Start(Callbacks callbacks, std::string& reason);
    /// No callback runs after this returns
    void Stop();

    /// Set the motors of a pad with haptics. Never blocks: applied on the companion's queue, the latest wins. False for
    /// an unknown pad or one without haptics
    bool SetRumble(const std::string& key, const RumbleLevels& levels);

private:
    struct State;
    std::shared_ptr<State> _state;
};

}  // namespace controllers
