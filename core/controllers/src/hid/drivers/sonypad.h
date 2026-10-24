#pragma once

/// @file sonypad.h
/// @brief The full input reports of the DualSense, the DualSense Edge and the DualShock 4 (haptics.md §9, H4), and
/// the DualShock 4 output report. Layouts as Linux's drivers/hid/hid-playstation.c reads them (struct
/// dualsense_input_report, struct dualshock4_input_report_common / _bt / _usb, dualsense_parse_report,
/// dualshock4_parse_report; torvalds/linux 4169c9c90532) and SDL3's src/joystick/hidapi/SDL_hidapi_ps5.c /
/// SDL_hidapi_ps4.c (libsdl-org/SDL 3cffe56317b9):
///
/// | Pad / bus | Report | Size | The common part from | CRC |
/// |:--|:--|:--|:--|:--|
/// | DualSense USB | 0x01 | 64 | byte 1 | none |
/// | DualSense Bluetooth | 0x31 | 78 | byte 2 | CRC-32 of A1 + bytes 0..73 at 74..77 |
/// | DualShock 4 USB | 0x01 | 64 | byte 1 | none |
/// | DualShock 4 Bluetooth | 0x11 | 78 | byte 3 | CRC-32 of A1 + bytes 0..73 at 74..77 |
///
/// Over Bluetooth both pads start with a short report 0x01 (the HID descriptor's, read by HidPad) and send the full
/// one once a feature report was read from them (the calibration 0x05: hid-playstation reads it at probe; SDL reads
/// 0x09 and 0x20 "this will also enable enhanced reports over Bluetooth"). Decode takes only the full reports and says
/// so; the short one stays HidPad's.
///
/// DualSense common part (offsets from its start): 0 x, 1 y, 2 rx, 3 ry, 4 z (L2), 5 rz (R2), 6 sequence, 7-10
/// buttons, 15-26 gyro / accel, 32-39 two touch points, 52 status (battery). DualShock 4: 0-3 sticks, 4-6 buttons,
/// 7 z, 8 rz, 29 status; the touch reports follow the 32-byte common part (a count, then 9 bytes each: a time stamp
/// and two points).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "hid/drivers/padprotocol.h"
#include "hid/hidpad.h"
#include "hostbackend.h"

namespace controllers
{

enum class SonyModel : uint8_t
{
    DualSense,
    DualSenseEdge,
    DualShock4,
};

/// The CRC-32 of a PlayStation Bluetooth report: zlib's CRC-32 of @p seed followed by @p data (hid-playstation's
/// PS_INPUT_CRC32_SEED 0xA1, PS_OUTPUT_CRC32_SEED 0xA2, PS_FEATURE_CRC32_SEED 0xA3)
uint32_t PlayStationCrc32(uint8_t seed, const uint8_t* data, size_t size);
/// The last four bytes of @p report hold the CRC-32 (little endian) of @p seed + the rest
bool PlayStationCrcValid(uint8_t seed, const uint8_t* report, size_t size);

class SonyPad
{
public:
    static constexpr uint8_t kInputCrcSeed = 0xA1;
    static constexpr uint8_t kFeatureCrcSeed = 0xA3;
    /// The calibration feature report: reading it switches a Bluetooth pad to its full report (hid-playstation)
    static constexpr uint8_t kCalibrationFeature = 0x05;
    static constexpr size_t kCalibrationFeatureSize = 41;
    /// The DualShock 4 USB calibration feature (37 bytes, no CRC). SDL's HIDAPI_DriverPS4_LoadCalibrationData reads it
    /// first over Bluetooth as well: "this report switches them into advanced report mode" (the 0x11 report)
    static constexpr uint8_t kDs4UsbCalibrationFeature = 0x02;
    static constexpr size_t kDs4UsbCalibrationFeatureSize = 37;
    static constexpr size_t kDs4UsbCalibrationMinSize = 35;  ///< SDL's short-read limit
    /// The DualSense firmware feature report (64 bytes, the update version at 44-45, hid-playstation)
    static constexpr uint8_t kFirmwareFeature = 0x20;
    static constexpr size_t kFirmwareFeatureSize = 64;

    /// 054C:0CE6 DualSense, 054C:0DF2 DualSense Edge, 054C:05C4 / 09CC / 0BA0 DualShock 4 (the last the wireless
    /// adapter); nullopt for anything else
    static std::optional<SonyModel> ModelOf(uint16_t vendor, uint16_t product);

    explicit SonyPad(SonyModel model);

    SonyModel Model() const
    {
        return _model;
    }
    /// The controls Decode reports for the model
    static std::vector<std::string> Controls(SonyModel model);

    /// A full input report: every control that changed goes to @p sink and the battery is read. False for any other
    /// report (the Bluetooth short report 0x01, a feature reply) and for a Bluetooth report whose CRC is wrong (it is
    /// counted, nothing is emitted)
    bool Decode(const uint8_t* report, size_t size, const ControlSink& sink);

    /// From the last full report; unknown before one came
    BatteryState Battery() const
    {
        return _battery;
    }
    /// A full Bluetooth report came: the pad is in its extended mode
    bool Extended() const
    {
        return _extended;
    }
    uint32_t CrcFailures() const
    {
        return _crcFailures;
    }

    /// hid-playstation's dualsense_parse_report: status[0] low nibble the level in tens (0 = 0-9 %), high nibble the
    /// charging state (0 discharging, 1 charging, 2 full, 0xA / 0xB not charging, else unknown)
    static BatteryState DualSenseBattery(uint8_t status);
    /// dualshock4_parse_report: status[0] bit 4 the cable, low nibble the level in tens (11 full on the cable)
    static BatteryState DualShock4Battery(uint8_t status);

private:
    struct Value
    {
        float value = 0;
        bool seen = false;
    };
    void Set(size_t index, float value, bool button, const ControlSink& sink);
    void DecodeCommon(const uint8_t* common, size_t touchOffset, size_t touchSize, const ControlSink& sink);

    SonyModel _model;
    std::vector<std::string> _names;
    std::vector<Value> _values;
    BatteryState _battery;
    bool _extended = false;
    uint32_t _crcFailures = 0;
};

/// The DualShock 4 output report (hid-playstation's struct dualshock4_output_report_common / _usb / _bt and
/// dualshock4_output_worker): the two motors and the light bar, always together (valid flags 0x03; "some 3rd party
/// gamepads expect updates to rumble and lightbar together")
class DualShock4Output
{
public:
    /// USB report 0x05 (32 bytes): flags at 1, the right (light) motor at 4, the left (heavy) at 5, red / green /
    /// blue at 6-8
    static std::vector<uint8_t> UsbReport(const RumbleLevels& levels, const LightColor& light);
    /// Bluetooth report 0x11 (78 bytes): 0x11, hardware control C0 (HID data + CRC), audio 00, the common part from
    /// byte 3, the CRC-32 of A2 + bytes 0..73 at 74..77
    static std::vector<uint8_t> BluetoothReport(const RumbleLevels& levels, const LightColor& light);
};

/// The outputs of a DualSense / DualShock 4 (IPadProtocol, run by a PadPump), USB or Bluetooth:
/// - Bluetooth: first the calibration feature report 0x05 is read, which switches the pad to its full input report
///   (hid-playstation's dualsense_get_calibration_data / dualshock4_get_calibration_data at probe); a failed read is
///   tried again kFeatureRetryMs later, at most kFeatureAttempts times.
/// - Then the queued changes, merged into one report at most every kIntervalMs: the motors (and a DualSense's trigger
///   effects) when they differ from the written ones, the light, the player LEDs. A DualShock 4 takes motors and light
///   together (DualShock4Output), so it keeps both; its light starts at the PlayStation player colour (hid-playstation's
///   dualshock4_set_default_lightbar_colors) until SetLight sets one.
/// - A DualSense's first colour releases the firmware's light bar (light bar setup "light out").
class SonyProtocol : public IPadProtocol
{
public:
    static constexpr uint64_t kIntervalMs = 8;
    static constexpr uint64_t kFeatureRetryMs = 100;
    static constexpr int kFeatureAttempts = 3;

    SonyProtocol(SonyModel model, bool bluetooth);

    std::optional<PadOutReport> Next(uint64_t nowMs, uint64_t& wakeMs) override;
    void OnWritten(bool ok, uint64_t nowMs) override;
    void OnFeature(uint8_t featureId, const std::vector<uint8_t>& data, uint64_t nowMs) override;
    void SetRumble(const RumbleLevels& levels) override;
    bool SetLight(const LightColor& color) override;
    void SetPlayer(int player) override;
    std::optional<PadOutReport> Final() override;

    /// The calibration read succeeded (a Bluetooth pad sends its full report now); true over USB
    bool Ready() const
    {
        return _ready;
    }

private:
    PadOutReport Build(bool rumble, bool light, bool player);

    SonyModel _model;
    bool _bluetooth;
    bool _ready;
    int _featureTries = 0;
    bool _modeSwitched = false;  ///< DualShock 4 over Bluetooth: feature 0x02 was read (then 0x05 follows)
    uint64_t _featureAfter = 0;
    uint8_t _sequence = 0;
    uint64_t _lastWriteMs = 0;
    bool _wroteOnce = false;

    RumbleLevels _rumble;
    std::optional<RumbleLevels> _writtenRumble = RumbleLevels{};  ///< nullopt after a refused write
    LightColor _light;
    bool _lightPending = false;
    bool _lightSet = false;       ///< SetLight chose it (a DualShock 4's player colour no longer applies)
    bool _lightReleased = false;  ///< DualSense: the firmware's light bar was released
    int _player = 0;
    bool _playerPending = false;
    /// The last report's parts, for OnWritten
    bool _inFlightRumble = false;
    RumbleLevels _inFlightLevels;
    bool _inFlightLight = false;
    bool _inFlightPlayer = false;
};

}  // namespace controllers
