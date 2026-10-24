#pragma once

/// @file switchpro.h
/// @brief The Nintendo Switch Pro Controller (057E:2009) over Bluetooth and USB (haptics.md §9, H4): its full input
/// report 0x30, the subcommands that set it up, and its HD rumble. Sources: dekuNukem's Nintendo_Switch_Reverse_
/// Engineering (bluetooth_hid_notes.md, bluetooth_hid_subcommands_notes.md, rumble_data_table.md; b354f21ae81f),
/// SDL3's src/joystick/hidapi/SDL_hidapi_switch.c (libsdl-org/SDL 3cffe56317b9) and Linux's drivers/hid/hid-nintendo.c
/// (torvalds/linux 4169c9c90532).
///
/// - Input 0x30 (and the controller state at the head of a subcommand reply 0x21): 1 timer, 2 battery (high nibble:
///   level 0-8 in its upper three bits, bit 4 charging) and connection (bit 0: USB powered), 3-5 buttons (3: Y X B A
///   SR SL R ZR from bit 0; 4: - + RS LS Home Capture; 5: down up right left SR SL L ZL), 6-8 the left stick and 9-11
///   the right one, two 12-bit values each (x = b0 | (b1 & 0x0F) << 8, y = b1 >> 4 | b2 << 4, up is larger).
///   The faces are positional: B south, A east, Y west, X north (SDL without button labels).
/// - Output 0x01: a packet number 0-15, 8 rumble bytes (left, right actuator), a subcommand and its data; 0x10 the
///   rumble alone; 0x80 the USB-only commands (handshake, high speed, force USB). 49 bytes over Bluetooth, 64 over USB
///   (SDL's WritePacket).
/// - HD rumble, per actuator 4 bytes: the high band's frequency (9 bits, `hf`) and amplitude (`ha`), the low band's
///   frequency (7 bits, `lf`) and amplitude (9 bits, `la`): `hf & 0xFF`, `ha | hf >> 8`, `lf | (la >> 8 & 0x80)`,
///   `la & 0xFF`. A frequency f is the code e = round(32 log2(f / 10)), hf = (e - 0x60) * 4, lf = e - 0x40; the
///   amplitude is a step k 0..100 of dekuNukem's table (ha = 2k, la = 0x40 + k / 2 with bit 15 for an odd k), k 100 =
///   1.0, the most the table calls safe. No libm at run time: the frequency code comes from a table of the rounding
///   boundaries, the amplitude from SDL's table of the steps (amplitude * 65535).

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "hid/drivers/padprotocol.h"
#include "hid/hidpad.h"
#include "hostbackend.h"

namespace controllers
{

class SwitchRumble
{
public:
    /// The actuators' resonances (dekuNukem: the neutral 00 01 40 40 is 320 Hz high, 160 Hz low): the frequencies of
    /// the handle motors' levels when an effect names none
    static constexpr uint16_t kLowHz = 160;
    static constexpr uint16_t kHighHz = 320;

    /// round(32 log2(@p hz / 10)), clamped to 0x40..0xE0
    static uint8_t FrequencyCode(uint32_t hz);
    /// The step 0..100 of a level 0..1 (the first step whose amplitude is at least the level, as SDL encodes it)
    static uint8_t AmplitudeStep(float level);
    /// One actuator's 4 bytes; both levels zero give the neutral 00 01 40 40. @p lowHz / @p highHz 0: the defaults
    static std::array<uint8_t, 4> Encode(float low, uint32_t lowHz, float high, uint32_t highHz);
    /// Both actuators (8 bytes): the low level on the low band, the high level on the high band, the same on the left
    /// and the right
    static std::array<uint8_t, 8> Pair(const RumbleLevels& levels);
    static std::array<uint8_t, 8> Neutral();
};

/// The sticks' calibration, 12-bit raw units (SDL's LoadStickCalibration)
struct SwitchStickCalibration
{
    /// [stick][axis]: stick 0 left, 1 right; axis 0 x, 1 y
    uint16_t centre[2][2] = {{2048, 2048}, {2048, 2048}};
    uint16_t below[2][2] = {{1433, 1433}, {1433, 1433}};  ///< the range below the centre (SDL's 0xFFF default)
    uint16_t above[2][2] = {{1433, 1433}, {1433, 1433}};

    /// From the 9 bytes of each stick (SPI 0x603D factory, 0x8012 / 0x801D user): the left stick packs x-above,
    /// y-above, x-centre, y-centre, x-below, y-below; the right one x-centre, y-centre, x-below, y-below, x-above,
    /// y-above (12 bits each, little endian nibbles). 0xFFF (unset flash) takes SDL's defaults
    static SwitchStickCalibration Parse(const uint8_t* left, const uint8_t* right);
};

/// Reads 0x30 / 0x21 into controls
class SwitchProPad
{
public:
    static constexpr uint16_t kNintendo = 0x057E;
    static constexpr uint16_t kProController = 0x2009;
    static bool Handles(uint16_t vendor, uint16_t product)
    {
        return vendor == kNintendo && product == kProController;
    }

    SwitchProPad();
    static std::vector<std::string> Controls();
    /// A full report 0x30 / 0x31 or a subcommand reply 0x21: every control that changed goes to @p sink. False for any
    /// other report (the simple 0x3F stays HidPad's)
    bool Decode(const uint8_t* report, size_t size, const ControlSink& sink);
    /// The sticks' scale: SDL's extents, 0.7 of the calibrated range, widened by what the stick reaches
    void SetCalibration(const SwitchStickCalibration& calibration);

    BatteryState Battery() const
    {
        return _battery;
    }
    /// SDL: the level 0 (empty) - 8 (full) of the high nibble, bit 4 charging, connection bit 0 the cable
    static BatteryState BatteryOf(uint8_t status);

private:
    float Axis(int raw, int stick, int axis);
    void Set(size_t index, float value, bool button, const ControlSink& sink);

    SwitchStickCalibration _calibration;
    int _extentBelow[2][2] = {};
    int _extentAbove[2][2] = {};
    std::vector<float> _values;
    std::vector<bool> _seen;
    BatteryState _battery;
};

/// The Pro Controller's setup and rumble (IPadProtocol, run by a PadPump), SDL's HIDAPI_DriverSwitch_OpenJoystick in
/// order: a wait for macOS's own handshake (kStartDelayMs); over USB the handshake 80 02, high speed 80 03, 80 02,
/// force USB 80 04; the user and factory stick calibration (SPI read 0x10 at 0x8010, 22 bytes, and 0x603D, 18
/// bytes); vibration on (0x48 01); over USB force USB again; the full report (0x03 30); the player lights (0x30).
/// Each step waits for its reply (0x81 / 0x21 with the ack bit and the same subcommand, an SPI reply of the same
/// address) kReplyMs, at most kAttempts writes, then goes on without it. Then the rumble: a 0x10 report when the
/// levels change, at most every kRumbleIntervalMs, the running rumble written again every kRumbleRefreshMs (SDL:
/// "how often you have to refresh a long duration rumble to keep the motors running"), the neutral rumble once at
/// zero.
class SwitchProtocol : public IPadProtocol
{
public:
    static constexpr uint64_t kStartDelayMs = 250;
    static constexpr uint64_t kReplyMs = 100;
    static constexpr int kAttempts = 5;
    static constexpr uint64_t kRumbleIntervalMs = 30;
    static constexpr uint64_t kRumbleRefreshMs = 50;
    static constexpr size_t kBluetoothLength = 49;
    static constexpr size_t kUsbLength = 64;

    using CalibrationSink = std::function<void(const SwitchStickCalibration& calibration)>;

    /// @p onCalibration: called from OnInput (the thread that passes the input) once the calibration is read
    SwitchProtocol(bool bluetooth, CalibrationSink onCalibration, uint64_t startDelayMs = kStartDelayMs);

    std::optional<PadOutReport> Next(uint64_t nowMs, uint64_t& wakeMs) override;
    void OnInput(const uint8_t* report, size_t size, uint64_t nowMs) override;
    void SetRumble(const RumbleLevels& levels) override;
    void SetPlayer(int player) override;
    std::optional<PadOutReport> Final() override;

    /// The setup has run (every step answered or given up)
    bool Ready() const
    {
        return _step >= _steps.size();
    }
    /// SDL's player_pattern: 1, 3, 7, F, 9, 5, D, 6 for players 1-8 (bit 0 the first LED); 0 none
    static uint8_t PlayerLights(int player);

    /// region <Report builders (the tests)>
    std::vector<uint8_t> Subcommand(uint8_t id, const std::vector<uint8_t>& data);
    std::vector<uint8_t> RumbleReport(const std::array<uint8_t, 8>& rumble);
    std::vector<uint8_t> Proprietary(uint8_t command) const;
    /// endregion </Report builders>

private:
    struct Step
    {
        enum class Kind : uint8_t
        {
            Delay,
            Proprietary,
            Subcommand,
        };
        Kind kind = Kind::Delay;
        uint8_t id = 0;  ///< the command / subcommand
        std::vector<uint8_t> data;
        bool waitReply = true;
        uint64_t delayMs = 0;
    };
    void Advance(uint64_t nowMs);
    void Pad(std::vector<uint8_t>& report) const;
    void Calibrated();

    bool _bluetooth;
    CalibrationSink _onCalibration;
    std::vector<Step> _steps;
    size_t _step = 0;
    bool _started = false;  ///< the current step was written (waiting for its reply)
    int _tries = 0;
    uint64_t _deadline = 0;
    uint64_t _stepStart = 0;
    bool _stepStarted = false;
    uint8_t _packet = 0;

    std::optional<std::array<uint8_t, 22>> _user;
    bool _calibrationDone = false;

    RumbleLevels _rumble;
    std::array<uint8_t, 8> _sent = Neutral8();
    bool _rumbleWritten = false;  ///< anything since the setup
    uint64_t _lastRumbleMs = 0;
    int _player = 0;
    bool _playerPending = false;

    static std::array<uint8_t, 8> Neutral8()
    {
        return SwitchRumble::Neutral();
    }
};

}  // namespace controllers
