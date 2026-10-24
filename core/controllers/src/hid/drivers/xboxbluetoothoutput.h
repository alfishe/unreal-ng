#pragma once

/// @file xboxbluetoothoutput.h
/// @brief The rumble of an Xbox One / Series pad over Bluetooth (haptics.md §9): output report 3, as SDL's HIDAPI Xbox
/// One driver and Linux's hid-microsoft (struct xb1s_ff_report) send it, with the impulse triggers. Shared by the
/// macOS IOHID backend (IOHIDDeviceSetReport) and the Linux evdev backend (hidraw write() where evdev gives no
/// FF_RUMBLE), and the writer thread that keeps SetRumble from blocking.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "hostbackend.h"

namespace controllers
{

class XboxBluetoothOutput
{
public:
    static constexpr uint8_t kRumbleReport = 0x03;

    /// An Xbox One / Series pad (the One S, the Series, the Elites, the Adaptive) over Bluetooth: the device database's
    /// xbox layout from Microsoft, not the 360 (no Bluetooth, its own protocol)
    static bool Handles(bool bluetooth, uint16_t vendor, const std::string& layout, const std::string& family);

    /// Report 3 (9 bytes): 03, enable 0F (all four actuators), left trigger, right trigger, low (left, heavy) motor,
    /// high (right) motor as 0..100, duration FF (2.55 s), delay 00, repeat EB (the pulse repeated 235 times: on until
    /// the next report, ~10 minutes at most). All levels zero stop the motors
    static std::vector<uint8_t> RumbleReport(const RumbleLevels& levels);
};

/// A device's rumble writes off the caller's thread (IBackend::SetRumble never blocks): the latest levels win, levels
/// equal to the written ones are not written again, and two writes are at least @p interval apart (a Bluetooth link
/// takes an output report every few ms; the mixer's envelopes come at 8 ms). When it ends, the motors are stopped
/// (levels zero written) unless zero was the last write
class RumbleWriter
{
public:
    /// Writes one set of levels to the device; false when it refused them
    using Write = std::function<bool(const RumbleLevels& levels)>;

    static constexpr std::chrono::milliseconds kInterval{8};

    explicit RumbleWriter(Write write, std::chrono::milliseconds interval = kInterval);
    /// What is queued is dropped, a write in flight finishes; then zero is written when the motors may be running
    ~RumbleWriter();
    RumbleWriter(const RumbleWriter&) = delete;
    RumbleWriter& operator=(const RumbleWriter&) = delete;

    /// Queue @p levels; replaces levels not written yet
    void Set(const RumbleLevels& levels);

    /// Writes done / refused so far (the tests, the live check)
    uint64_t Written() const;
    uint64_t Failed() const;

private:
    void Run();

    Write _write;
    const std::chrono::milliseconds _interval;
    mutable std::mutex _mutex;
    std::condition_variable _wake;
    std::optional<RumbleLevels> _pending;  ///< under _mutex
    /// Under _mutex: what the device runs, zero at first; nullopt after a refused write (it may have landed: the next
    /// levels are written even when equal, the end writes zero)
    std::optional<RumbleLevels> _last = RumbleLevels{};
    uint64_t _written = 0;
    uint64_t _failed = 0;
    bool _stopping = false;
    std::thread _thread;  ///< last: started when the rest exists
};

}  // namespace controllers
