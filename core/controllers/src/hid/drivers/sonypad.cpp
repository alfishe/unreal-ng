#include "hid/drivers/sonypad.h"

#include "hid/drivers/dualsenseoutput.h"

namespace controllers
{

namespace
{
constexpr uint16_t kSony = 0x054C;
constexpr uint8_t kOutputCrcSeed = 0xA2;

/// The control slots, in this order for every model (the Edge and the DualShock 4 use a part of them)
enum Slot : size_t
{
    kSouth,
    kEast,
    kWest,
    kNorth,
    kDpadUp,
    kDpadDown,
    kDpadLeft,
    kDpadRight,
    kLb,
    kRb,
    kLtClick,
    kRtClick,
    kView,
    kMenu,
    kLstickClick,
    kRstickClick,
    kGuide,
    kTouchpadClick,
    kLstickX,
    kLstickY,
    kRstickX,
    kRstickY,
    kLt,
    kRt,
    kTouchpadX,
    kTouchpadY,
    kMic,      ///< DualSense: the mute button
    kPaddleLeft,   ///< Edge: paddle.p3, the Elite's upper left (hostcontrols.cpp: P1 / P2 right, P3 / P4 left)
    kPaddleRight,  ///< Edge: paddle.p1, the Elite's upper right; an Elite profile works on the Edge
    kFn1,          ///< Edge
    kFn2,          ///< Edge
    kSlotCount,
};

const char* const kSlotNames[kSlotCount] = {
    "south",        "east",         "west",     "north",          "dpad.up",   "dpad.down", "dpad.left",
    "dpad.right",   "lb",           "rb",       "lt.click",       "rt.click",  "view",      "menu",
    "lstick.click", "rstick.click", "guide",    "touchpad.click", "lstick.x",  "lstick.y",  "rstick.x",
    "rstick.y",     "lt",           "rt",       "touchpad.x",     "touchpad.y", "misc.1",   "paddle.p3",
    "paddle.p1",    "misc.2",       "misc.3",
};

size_t SlotCount(SonyModel model)
{
    switch (model)
    {
        case SonyModel::DualSenseEdge: return kSlotCount;
        case SonyModel::DualSense: return kMic + 1;
        case SonyModel::DualShock4: return kTouchpadY + 1;
    }
    return 0;
}

/// A byte axis 0..255 centred at 128, as HidPad normalises a logical 0..255 field: -1 at 0, 1 at 255
float Stick(uint8_t value)
{
    return value >= 128 ? (value - 128) / 127.0f : (value - 128) / 128.0f;
}

/// A coordinate 0..size-1 as a centred axis, the same rule (the centre is the upper middle value)
float Centred(int value, int size)
{
    const int max = size - 1;
    const int centre = size / 2;
    if (value < 0)
        value = 0;
    if (value > max)
        value = max;
    const int half = value >= centre ? max - centre : centre;
    return half > 0 ? static_cast<float>(value - centre) / static_cast<float>(half) : 0.0f;
}

/// Hat 0..7 clockwise from up -> the d-pad bits up, right, down, left (8 and above: centred)
uint8_t HatBits(uint8_t hat)
{
    static const uint8_t kBits[8] = {0x1, 0x3, 0x2, 0x6, 0x4, 0xC, 0x8, 0x9};
    return hat < 8 ? kBits[hat] : 0;
}

uint8_t Byte(float level)
{
    return static_cast<uint8_t>((level < 0 ? 0 : level > 1 ? 1 : level) * 255.0f + 0.5f);
}

void PutCrc(std::vector<uint8_t>& report, uint8_t seed)
{
    const uint32_t crc = PlayStationCrc32(seed, report.data(), report.size() - 4);
    for (size_t index = 0; index < 4; index++)
        report[report.size() - 4 + index] = static_cast<uint8_t>(crc >> (8 * index));
}
}  // namespace

uint32_t PlayStationCrc32(uint8_t seed, const uint8_t* data, size_t size)
{
    return DualSenseOutput::Crc32(data, size, DualSenseOutput::Crc32(&seed, 1));
}

bool PlayStationCrcValid(uint8_t seed, const uint8_t* report, size_t size)
{
    if (size < 5)
        return false;
    const uint32_t stored = report[size - 4] | (report[size - 3] << 8) | (report[size - 2] << 16) |
                            (static_cast<uint32_t>(report[size - 1]) << 24);
    return PlayStationCrc32(seed, report, size - 4) == stored;
}

std::optional<SonyModel> SonyPad::ModelOf(uint16_t vendor, uint16_t product)
{
    if (vendor != kSony)
        return std::nullopt;
    switch (product)
    {
        case 0x0CE6: return SonyModel::DualSense;
        case 0x0DF2: return SonyModel::DualSenseEdge;
        case 0x05C4:
        case 0x09CC:
        case 0x0BA0: return SonyModel::DualShock4;
        default: return std::nullopt;
    }
}

SonyPad::SonyPad(SonyModel model)
    : _model(model), _names(kSlotNames, kSlotNames + SlotCount(model)), _values(_names.size())
{
}

std::vector<std::string> SonyPad::Controls(SonyModel model)
{
    std::vector<std::string> names;
    for (size_t index = 0; index < SlotCount(model); index++)
        if (index != kLtClick && index != kRtClick)
            names.emplace_back(kSlotNames[index]);
    return names;
}

BatteryState SonyPad::DualSenseBattery(uint8_t status)
{
    const int level = status & 0x0F;
    const int tens = level * 10 + 5 > 100 ? 100 : level * 10 + 5;
    BatteryState battery;
    battery.known = true;
    battery.coarse = true;
    switch (status >> 4)
    {
        case 0x0: battery.level = tens; break;
        case 0x1:
            battery.level = tens;
            battery.charging = true;
            battery.wired = true;
            break;
        case 0x2:
            battery.level = 100;
            battery.wired = true;
            break;
        case 0xA:  // voltage or temperature out of range
        case 0xB:  // temperature error
            battery.coarse = false;
            battery.wired = true;
            break;
        default: return {};  // 0xF charging error, or undefined
    }
    return battery;
}

BatteryState SonyPad::DualShock4Battery(uint8_t status)
{
    const int level = status & 0x0F;
    BatteryState battery;
    battery.known = true;
    battery.coarse = true;
    if (status & 0x10)
    {
        battery.wired = true;
        if (level <= 10)
        {
            battery.level = level < 10 ? level * 10 + 5 : 100;
            battery.charging = true;
        }
        else if (level == 11)
            battery.level = 100;  // full
        else
            return {};  // 14 voltage / temperature, 15 charge error
        return battery;
    }
    battery.level = level < 10 ? level * 10 + 5 : 100;
    return battery;
}

void SonyPad::Set(size_t index, float value, bool button, const ControlSink& sink)
{
    if (index >= _values.size())
        return;
    Value& slot = _values[index];
    const bool first = !slot.seen;
    if (!first && slot.value == value)
        return;
    slot.seen = true;
    slot.value = value;
    // As HidPad: a button's first report says only a press, an axis its position
    if (button && first && value == 0)
        return;
    sink(_names[index], value);
}

void SonyPad::DecodeCommon(const uint8_t* common, size_t touchOffset, size_t touchSize, const ControlSink& sink)
{
    const bool dualSense = _model != SonyModel::DualShock4;
    const uint8_t* buttons = common + (dualSense ? 7 : 4);
    const uint8_t hat = HatBits(buttons[0] & 0x0F);
    const bool bits[] = {
        (buttons[0] & 0x20) != 0,  // Cross
        (buttons[0] & 0x40) != 0,  // Circle
        (buttons[0] & 0x10) != 0,  // Square
        (buttons[0] & 0x80) != 0,  // Triangle
        (hat & 0x1) != 0,          // up
        (hat & 0x4) != 0,          // down
        (hat & 0x8) != 0,          // left
        (hat & 0x2) != 0,          // right
        (buttons[1] & 0x01) != 0,  // L1
        (buttons[1] & 0x02) != 0,  // R1
        (buttons[1] & 0x04) != 0,  // L2
        (buttons[1] & 0x08) != 0,  // R2
        (buttons[1] & 0x10) != 0,  // Create / Share
        (buttons[1] & 0x20) != 0,  // Options
        (buttons[1] & 0x40) != 0,  // L3
        (buttons[1] & 0x80) != 0,  // R3
        (buttons[2] & 0x01) != 0,  // PS
        (buttons[2] & 0x02) != 0,  // touchpad
    };
    for (size_t index = 0; index < sizeof(bits) / sizeof(bits[0]); index++)
        if (index != kLtClick && index != kRtClick)  // the router derives them from lt / rt (hostcontrols.h)
            Set(index, bits[index] ? 1.0f : 0.0f, true, sink);
    Set(kLstickX, Stick(common[0]), false, sink);
    Set(kLstickY, Stick(common[1]), false, sink);
    Set(kRstickX, Stick(common[2]), false, sink);
    Set(kRstickY, Stick(common[3]), false, sink);
    Set(kLt, (dualSense ? common[4] : common[7]) / 255.0f, false, sink);
    Set(kRt, (dualSense ? common[5] : common[8]) / 255.0f, false, sink);

    // The first touch point: its position while a finger is on the pad, the centre when none is
    // (contact bit 7 set: no finger; x 12 bits, y 12 bits). A DualShock 4 report without touch reports keeps the last
    const uint8_t* point = nullptr;
    const int height = dualSense ? 1080 : 942;  // DS_TOUCHPAD_HEIGHT, DS4_TOUCHPAD_HEIGHT
    if (dualSense)
        point = common + 32;
    else if (touchSize >= 1 + 9 && common[touchOffset] > 0)
        point = common + touchOffset + 2;  // the count, the first touch report's time stamp, then its points
    if (point != nullptr && (point[0] & 0x80) == 0)
    {
        const int x = point[1] | ((point[2] & 0x0F) << 8);
        const int y = (point[2] >> 4) | (point[3] << 4);
        Set(kTouchpadX, Centred(x, 1920), false, sink);
        Set(kTouchpadY, Centred(y, height), false, sink);
    }
    else if (point != nullptr || !_values[kTouchpadX].seen)
    {
        Set(kTouchpadX, 0.0f, false, sink);
        Set(kTouchpadY, 0.0f, false, sink);
    }

    if (dualSense)
    {
        Set(kMic, (buttons[2] & 0x04) != 0 ? 1.0f : 0.0f, true, sink);
        if (_model == SonyModel::DualSenseEdge)
        {
            Set(kPaddleLeft, (buttons[2] & 0x40) != 0 ? 1.0f : 0.0f, true, sink);
            Set(kPaddleRight, (buttons[2] & 0x80) != 0 ? 1.0f : 0.0f, true, sink);
            Set(kFn1, (buttons[2] & 0x10) != 0 ? 1.0f : 0.0f, true, sink);
            Set(kFn2, (buttons[2] & 0x20) != 0 ? 1.0f : 0.0f, true, sink);
        }
        _battery = DualSenseBattery(common[52]);
    }
    else
        _battery = DualShock4Battery(common[29]);
}

bool SonyPad::Decode(const uint8_t* report, size_t size, const ControlSink& sink)
{
    if (size == 0)
        return false;
    const bool dualSense = _model != SonyModel::DualShock4;
    if (report[0] == 0x01 && size == 64)
    {
        // USB: the common part from byte 1; the DualShock 4's touch reports after its 32 bytes
        DecodeCommon(report + 1, 32, size - 1 - 32, sink);
        return true;
    }
    const uint8_t bluetoothId = dualSense ? 0x31 : 0x11;
    if (report[0] == bluetoothId && size == 78)
    {
        if (!PlayStationCrcValid(kInputCrcSeed, report, size))
        {
            ++_crcFailures;
            return false;
        }
        _extended = true;
        const size_t offset = dualSense ? 2 : 3;
        DecodeCommon(report + offset, 32, size - 4 - offset - 32, sink);
        return true;
    }
    return false;
}

/// region <DualShock4Output>

std::vector<uint8_t> DualShock4Output::UsbReport(const RumbleLevels& levels, const LightColor& light)
{
    std::vector<uint8_t> report(32, 0);
    report[0] = 0x05;
    uint8_t* common = report.data() + 1;
    common[0] = 0x03;  // DS4_OUTPUT_VALID_FLAG0_MOTOR | DS4_OUTPUT_VALID_FLAG0_LED
    common[3] = Byte(levels.high);
    common[4] = Byte(levels.low);
    common[5] = light.red;
    common[6] = light.green;
    common[7] = light.blue;
    return report;
}

std::vector<uint8_t> DualShock4Output::BluetoothReport(const RumbleLevels& levels, const LightColor& light)
{
    std::vector<uint8_t> report(78, 0);
    report[0] = 0x11;
    report[1] = 0xC0;  // DS4_OUTPUT_HWCTL_HID | DS4_OUTPUT_HWCTL_CRC32, the default poll interval
    uint8_t* common = report.data() + 3;
    common[0] = 0x03;
    common[3] = Byte(levels.high);
    common[4] = Byte(levels.low);
    common[5] = light.red;
    common[6] = light.green;
    common[7] = light.blue;
    PutCrc(report, kOutputCrcSeed);
    return report;
}

/// endregion </DualShock4Output>

/// region <SonyProtocol>

SonyProtocol::SonyProtocol(SonyModel model, bool bluetooth)
    : _model(model), _bluetooth(bluetooth), _ready(!bluetooth), _modeSwitched(model != SonyModel::DualShock4)
{
    // A DualShock 4 report sets motors and light together: the light starts at player 1's blue, not off
    if (model == SonyModel::DualShock4)
        _light = {0x00, 0x00, 0x40};
}

void SonyProtocol::SetRumble(const RumbleLevels& levels)
{
    _rumble = levels;
}

bool SonyProtocol::SetLight(const LightColor& color)
{
    _light = color;
    _lightSet = true;
    _lightPending = true;
    return true;
}

void SonyProtocol::SetPlayer(int player)
{
    _player = player;
    _playerPending = true;
    if (_model == SonyModel::DualShock4 && !_lightSet)
    {
        // hid-playstation's player_colors: blue, red, green, pink
        static const LightColor kColors[4] = {{0x00, 0x00, 0x40}, {0x40, 0x00, 0x00}, {0x00, 0x40, 0x00},
                                              {0x20, 0x00, 0x20}};
        _light = player > 0 ? kColors[(player - 1) % 4] : LightColor{};
        _lightPending = true;
    }
}

PadOutReport SonyProtocol::Build(bool rumble, bool light, bool player)
{
    PadOutReport out;
    if (_model == SonyModel::DualShock4)
        out.bytes = _bluetooth ? DualShock4Output::BluetoothReport(_rumble, _light)
                               : DualShock4Output::UsbReport(_rumble, _light);
    else
    {
        DualSenseOutputState state;
        if (rumble)
            state.rumble = _rumble;
        if (light)
        {
            state.light = _light;
            state.releaseLight = !_lightReleased;
        }
        if (player)
            state.playerLeds = DualSenseOutput::PlayerLeds(_player);
        out.bytes = DualSenseOutput::Report(state, _bluetooth, _sequence);
        if (_bluetooth)
            _sequence = static_cast<uint8_t>((_sequence + 1) & 0x0F);
    }
    _inFlightRumble = rumble || _model == SonyModel::DualShock4;
    _inFlightLevels = _rumble;
    _inFlightLight = light || _model == SonyModel::DualShock4;
    _inFlightPlayer = player;
    return out;
}

std::optional<PadOutReport> SonyProtocol::Next(uint64_t nowMs, uint64_t& wakeMs)
{
    wakeMs = 0;
    if (!_ready)
    {
        if (_featureTries >= kFeatureAttempts)
            _ready = true;  // the pad may be in its full mode anyway (GameController asked first): write on
        else if (nowMs < _featureAfter)
        {
            wakeMs = _featureAfter;
            return std::nullopt;
        }
        else
        {
            ++_featureTries;
            PadOutReport read;
            read.kind = PadOutReport::Kind::GetFeature;
            // A DualShock 4 reads 0x02 first, then 0x05 with its CRC (SDL); a DualSense only 0x05 (hid-playstation)
            read.featureId = _modeSwitched ? SonyPad::kCalibrationFeature : SonyPad::kDs4UsbCalibrationFeature;
            read.featureSize =
                _modeSwitched ? SonyPad::kCalibrationFeatureSize : SonyPad::kDs4UsbCalibrationFeatureSize;
            return read;
        }
    }
    const bool rumble = !_writtenRumble || *_writtenRumble != _rumble;
    const bool player = _playerPending && _model != SonyModel::DualShock4;
    if (!rumble && !_lightPending && !player)
        return std::nullopt;
    if (_wroteOnce && nowMs < _lastWriteMs + kIntervalMs)
    {
        wakeMs = _lastWriteMs + kIntervalMs;
        return std::nullopt;
    }
    _lastWriteMs = nowMs;
    _wroteOnce = true;
    PadOutReport out = Build(rumble, _lightPending, player);
    _lightPending = false;
    _playerPending = _playerPending && _model != SonyModel::DualShock4 && !player;
    return out;
}

void SonyProtocol::OnWritten(bool ok, uint64_t nowMs)
{
    (void)nowMs;
    if (_inFlightRumble)
        _writtenRumble = ok ? std::optional<RumbleLevels>(_inFlightLevels) : std::nullopt;
    if (_inFlightLight)
    {
        if (ok && _lightSet)
            _lightReleased = true;
        else if (!ok)
            _lightPending = true;  // once more with the next report
    }
    if (_inFlightPlayer && !ok)
        _playerPending = true;
    _inFlightRumble = _inFlightLight = _inFlightPlayer = false;
}

void SonyProtocol::OnFeature(uint8_t featureId, const std::vector<uint8_t>& data, uint64_t nowMs)
{
    if (featureId == SonyPad::kDs4UsbCalibrationFeature && !_modeSwitched)
    {
        // SDL accepts 35 bytes or more; the report carries no CRC
        if (data.size() >= SonyPad::kDs4UsbCalibrationMinSize &&
            data.size() <= SonyPad::kDs4UsbCalibrationFeatureSize && data[0] == featureId)
        {
            _modeSwitched = true;
            _featureTries = 0;  // the 0x05 read gets its own attempts
        }
        else
            _featureAfter = nowMs + kFeatureRetryMs;
        return;
    }
    if (featureId != SonyPad::kCalibrationFeature)
        return;
    // hid-playstation's ps_get_report: the size, the id, over Bluetooth the CRC (seed 0xA3)
    const bool valid = data.size() == SonyPad::kCalibrationFeatureSize && data[0] == featureId &&
                       (!_bluetooth || PlayStationCrcValid(SonyPad::kFeatureCrcSeed, data.data(), data.size()));
    if (valid)
        _ready = true;
    else
        _featureAfter = nowMs + kFeatureRetryMs;
}

std::optional<PadOutReport> SonyProtocol::Final()
{
    if (_writtenRumble && *_writtenRumble == RumbleLevels{})
        return std::nullopt;
    _rumble = {};
    return Build(true, false, false);
}

/// endregion </SonyProtocol>

}  // namespace controllers
