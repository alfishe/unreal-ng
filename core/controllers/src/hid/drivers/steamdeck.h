#pragma once

/// @file steamdeck.h
/// @brief The Steam Deck's built-in controller (28DE:1205, USB interface 2, the "Valve Software Steam Deck
/// Controller"): the 64-byte deck-state input report and the feature reports that drive it (Valve's
/// controller_constants.h / controller_structs.h as SDL ships them; the kernel's hid-steam.c). Shared by every backend
/// that reads the raw reports (Linux hidraw on the Deck itself; a companion stream later).
///
/// Input report (no report id, 64 bytes): [0] version 1, [2] type 9 (ID_CONTROLLER_DECK_STATE), [3] length; from [4]
/// uint32 packet number, uint32 buttons low and high, int16 left pad x y, right pad x y, accel x y z, gyro x y z, the
/// quaternion w x y z, uint16 trigger left right (0..32767), int16 left stick x y, right stick x y, uint16 pad pressure
/// left right. Pads and sticks: y is + up (the vocabulary's is - up).
///
/// Feature reports (HIDIOCSFEATURE, 64 bytes after a report id 0): [0] message type, [1] payload length, payload,
/// packed little-endian. 0x81 clears the digital mappings and 0x87 sets settings ({uint8 id, uint16 value}): together
/// they switch off the lizard mode (the trackpad mouse, the buttons as keys), which comes back on its own unless the
/// watchdog is fed. 0xEB drives the two handle motors, 0x8F pulses the trackpads' voice coils (left and right swapped in
/// this report), 0xEA plays a voice coil command (tick, click, tone, noise, sweep, script).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "hid/hidpad.h"
#include "hostbackend.h"

namespace controllers
{

class SteamDeck
{
public:
    static constexpr uint16_t kVendor = 0x28DE;
    static constexpr uint16_t kProduct = 0x1205;
    static constexpr int kInterface = 2;
    static constexpr size_t kReportSize = 64;
    /// A feature report's buffer: the report id 0 and 64 bytes
    static constexpr size_t kFeatureSize = 65;
    /// Feed the watchdog every this many input reports (~250 Hz: under a second), as SDL does
    static constexpr unsigned kWatchdogReports = 200;

    /// The voice coils of the trackpads (MsgTriggerHaptic side). Measured on a Deck (2026-10-20): 0 left, 1 right,
    /// 2 both; SDL's controller_structs.h says 1 / 2 / 3, and the firmware ignores 3
    enum class Pad : uint8_t
    {
        Left = 0,
        Right = 1,
        Both = 2,
    };
    /// MsgTriggerHaptic cmd
    enum class PadCommand : uint8_t
    {
        Off = 0,
        Tick = 1,
        Click = 2,
        Tone = 3,
        Rumble = 4,
        Noise = 5,
        Script = 6,
        Sweep = 7,
    };
    /// One voice coil command (0xEA), played by the firmware: a tick, a click, a tone of @p frequency Hz or noise for
    /// @p durationMs (negative: until Off), a log sweep, a script; gain in dB. Rumble (4) plays nothing on a Deck
    struct PadEffect
    {
        Pad pad = Pad::Both;
        PadCommand command = PadCommand::Tick;
        int8_t gainDb = 0;
        uint16_t frequency = 0;
        int16_t durationMs = 0;
        uint16_t noiseIntensity = 0;
        uint16_t lfoFrequency = 0;
        uint8_t lfoDepth = 0;
        uint8_t scriptId = 0;
        uint16_t sweepStart = 0;
        uint16_t sweepEnd = 0;
    };

    static bool Handles(uint16_t vendor, uint16_t product);

    static std::vector<std::string> Controls();
    /// One input report: every control that changed goes to @p sink (the first report gives the ones that are on and
    /// every axis). A trackpad's x and y are 0 while no finger is on it; the motion axes are -1..1 of ±2000 °/s and
    /// ±2 g, in SDL's frame (x right, y up, z towards the player). Other reports are ignored
    void Decode(const uint8_t* report, size_t size, const ControlSink& sink);
    /// Reports decoded since the last watchdog feed reached kWatchdogReports: the caller sends WatchdogReports()
    bool WatchdogDue();

    /// region <Feature reports: kFeatureSize bytes each, the report id 0 first>
    /// The lizard mode off: clear the digital mappings, then the settings (no mouse, no trackpad click)
    static std::vector<std::vector<uint8_t>> LizardOffReports();
    /// The lizard mode's watchdog: kept off while these go out
    static std::vector<std::vector<uint8_t>> WatchdogReports();
    /// The handle motors 0..1: left (low frequency) and right (high frequency)
    static std::vector<uint8_t> RumbleReport(float left, float right);
    /// A pulse train on a trackpad's voice coil: @p count pulses of @p onUs, @p offUs apart, at @p gainDb (-24..+6)
    static std::vector<uint8_t> PulseReport(Pad pad, uint16_t onUs, uint16_t offUs, uint16_t count, int8_t gainDb);
    /// A voice coil command
    static std::vector<uint8_t> PadEffectReport(const PadEffect& effect);
    /// endregion </Feature reports>

    /// A level 0..1 as the voice coils' gain: -24 dB (quiet) .. 0 dB; @p level 0 is the floor
    static int8_t GainDb(float level);

private:
    uint32_t _buttonsLow = 0;
    uint32_t _buttonsHigh = 0;
    int16_t _axes[20] = {};
    bool _padTouched[2] = {};
    bool _seen = false;
    unsigned _reports = 0;
};

}  // namespace controllers
