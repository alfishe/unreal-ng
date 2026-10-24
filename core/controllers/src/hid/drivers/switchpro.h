#pragma once

/// @file switchpro.h
/// @brief The Nintendo Switch Pro Controller (057E:2009) and the Joy-Cons (057E:2006 L, 2007 R) over Bluetooth and USB
/// (haptics.md §9, H4): their full input report 0x30, the subcommands that set them up, and their HD rumble. Sources: dekuNukem's Nintendo_Switch_Reverse_
/// Engineering (bluetooth_hid_notes.md, bluetooth_hid_subcommands_notes.md, rumble_data_table.md; b354f21ae81f),
/// SDL3's src/joystick/hidapi/SDL_hidapi_switch.c (libsdl-org/SDL 3cffe56317b9) and Linux's drivers/hid/hid-nintendo.c
/// (torvalds/linux 4169c9c90532).
///
/// - Input 0x30 (and the controller state at the head of a subcommand reply 0x21): 1 timer, 2 battery (high nibble:
///   level 0-8 in its upper three bits, bit 4 charging) and connection (bit 0: USB powered), 3-5 buttons (3: Y X B A
///   SR SL R ZR from bit 0; 4: - + RS LS Home Capture; 5: down up right left SR SL L ZL), 6-8 the left stick and 9-11
///   the right one, two 12-bit values each (x = b0 | (b1 & 0x0F) << 8, y = b1 >> 4 | b2 << 4, up is larger).
///   The faces are positional: B south, A east, Y west, X north (SDL without button labels).
/// - A Joy-Con sends the same report with its half of the buttons and its stick (SDL_hidapi_switch.c,
///   HandleFullControllerState). Alone it is a small pad held sideways (SDL's HandleMiniControllerStateL / R: the
///   four buttons by the stick are the faces, SL / SR the shoulders, L / ZL or R / ZR paddles, the stick turned a
///   quarter); an L and an R together are one pad, each half giving its side of the Pro layout
///   (HandleCombinedControllerStateL / R; SL / SR as paddles: SDL's RIGHT_PADDLE1 / 2 and LEFT_PADDLE1 / 2, the Elite
///   P1 / P2 and P3 / P4). In a pair the left one rumbles with the low motor's level, the right one with the high
///   one's (SDL's HIDAPI_DriverSwitch_RumbleJoystick).
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

/// Which buttons and stick a 0x30 report gives, and as what
enum class SwitchLayout : uint8_t
{
    Pro,          ///< the Pro Controller: everything
    JoyConLeft,   ///< a left Joy-Con alone, sideways
    JoyConRight,  ///< a right Joy-Con alone, sideways
    PairLeft,     ///< the left half of a Joy-Con pair
    PairRight,    ///< the right half
    // The Nintendo Switch Online pads talk the Pro Controller's protocol with their own buttons on its bits (SDL's
    // HIDAPI_DriverNintendoClassic, Linux's hid-nintendo nescon / snescon / gencon / n64con_button_mappings)
    Nes,      ///< NES / Famicom: B A on the Pro's B A, Select / Start, L / R (no sticks)
    Snes,     ///< SNES: the Pro's faces, L R ZL ZR, Select / Start (no sticks)
    N64,      ///< N64: A B, Z on ZL, ZR on the left stick click, C-Up / Down / Left / Right on Y / ZR / X / minus
    Genesis,  ///< Sega Genesis / Mega Drive: A B C on A B R, X Y Z on X Y L, Mode on ZR (no sticks)
};

/// Reads 0x30 / 0x21 into controls
class SwitchProPad
{
public:
    static constexpr uint16_t kNintendo = 0x057E;
    static constexpr uint16_t kProController = 0x2009;
    static constexpr uint16_t kJoyConLeft = 0x2006;
    static constexpr uint16_t kJoyConRight = 0x2007;
    /// The charging grip: each Joy-Con in it is an interface of its own under this one product id; which side a half
    /// is comes from the device info (SwitchProtocol's TypeSink)
    static constexpr uint16_t kChargingGrip = 0x200E;
    static bool IsChargingGrip(uint16_t vendor, uint16_t product)
    {
        return vendor == kNintendo && product == kChargingGrip;
    }
    static constexpr uint16_t kSnesController = 0x2017;
    static constexpr uint16_t kN64Controller = 0x2019;
    static constexpr uint16_t kGenesisController = 0x201E;
    static bool Handles(uint16_t vendor, uint16_t product, const std::string& name = {})
    {
        return LayoutFor(vendor, product, name).has_value();
    }
    /// The Pro Controller, a Joy-Con (alone until the backend pairs it), a Nintendo Switch Online pad; nullopt for
    /// anything else (a half of the charging grip 057E:200E included: its side comes from the device info). The NES
    /// pads come as right Joy-Cons: their product name tells them apart (SDL: "NES Controller", "HVC Controller")
    static std::optional<SwitchLayout> LayoutFor(uint16_t vendor, uint16_t product, const std::string& name = {});
    static bool IsJoyCon(SwitchLayout layout)
    {
        return layout == SwitchLayout::JoyConLeft || layout == SwitchLayout::JoyConRight ||
               layout == SwitchLayout::PairLeft || layout == SwitchLayout::PairRight;
    }
    /// The NES, SNES and Genesis pads have no motors; the N64 one and the rest rumble
    static bool HasRumble(SwitchLayout layout)
    {
        return layout != SwitchLayout::Nes && layout != SwitchLayout::Snes && layout != SwitchLayout::Genesis;
    }
    /// The layout of a Joy-Con alone (@p paired false) or as a pair's half
    static SwitchLayout JoyConLayout(bool left, bool paired);
    /// A pair's half rumbles with its side's motor: the left the low level, the right the high one (SDL)
    static RumbleLevels HalfRumble(const RumbleLevels& levels, bool left);

    explicit SwitchProPad(SwitchLayout layout = SwitchLayout::Pro);
    /// The Pro Controller's controls
    static std::vector<std::string> Controls();
    static std::vector<std::string> Controls(SwitchLayout layout);
    SwitchLayout Layout() const
    {
        return _layout;
    }
    /// A Joy-Con joined or left a pair: the next report gives the whole state again (as the first one)
    void SetLayout(SwitchLayout layout);
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

    SwitchLayout _layout;
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
    /// The controller type of the device info (hid-nintendo's joycon_ctlr_type: 1 a left Joy-Con, 2 a right one, 3
    /// the Pro Controller); 0 when the device gave none
    using TypeSink = std::function<void(uint8_t type)>;
    static constexpr uint8_t kTypeJoyConLeft = 0x01;
    static constexpr uint8_t kTypeJoyConRight = 0x02;

    /// @p onCalibration: called from OnInput (the thread that passes the input) once the calibration is read.
    /// @p onType (a charging grip's half): the setup asks the device info (subcommand 0x02, hid-nintendo's
    /// joycon_read_info; the type in the reply's byte 17) right after the USB handshake, and says the type from OnInput,
    /// or 0 from Next once the step gave up
    SwitchProtocol(bool bluetooth, CalibrationSink onCalibration, uint64_t startDelayMs = kStartDelayMs,
                   TypeSink onType = nullptr);

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
    TypeSink _onType;
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
