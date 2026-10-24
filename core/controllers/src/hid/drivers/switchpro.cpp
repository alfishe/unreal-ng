#include "hid/drivers/switchpro.h"

#include <algorithm>
#include <iterator>

namespace controllers
{

namespace
{
/// The rounding boundaries of the frequency code, millihertz: code e + 1 from ceil(10 * 2^((e + 0.5) / 32) * 1000),
/// e = 0x40..0xDF (generated once with Python's math; the codes match dekuNukem's rumble_data_table.md)
constexpr uint32_t kFrequencyBoundaries[160] = {
    40436, 41321, 42226, 43151, 44096, 45061, 46048, 47056, 48087, 49140,
    50216, 51315, 52439, 53587, 54761, 55960, 57185, 58437, 59717, 61024,
    62361, 63726, 65122, 66548, 68005, 69494, 71016, 72571, 74160, 75784,
    77443, 79139, 80872, 82642, 84452, 86301, 88191, 90122, 92096, 94112,
    96173, 98279, 100431, 102630, 104877, 107174, 109521, 111919, 114370, 116874,
    119433, 122048, 124721, 127452, 130243, 133095, 136009, 138987, 142031, 145141,
    148319, 151567, 154885, 158277, 161743, 165284, 168904, 172602, 176382, 180244,
    184191, 188224, 192346, 196557, 200861, 205260, 209754, 214347, 219041, 223837,
    228739, 233747, 238866, 244096, 249441, 254903, 260485, 266189, 272018, 277974,
    284061, 290281, 296637, 303133, 309770, 316553, 323485, 330568, 337807, 345204,
    352763, 360487, 368381, 376448, 384691, 393114, 401722, 410519, 419508, 428694,
    438081, 447674, 457477, 467494, 477731, 488192, 498882, 509806, 520969, 532377,
    544035, 555947, 568121, 580561, 593274, 606265, 619540, 633106, 646970, 661136,
    675613, 690407, 705525, 720974, 736762, 752895, 769381, 786228, 803444, 821037,
    839016, 857388, 876162, 895347, 914953, 934988, 955461, 976383, 997763, 1019611,
    1041938, 1064754, 1088069, 1111894, 1136241, 1161122, 1186547, 1212529, 1239080, 1266212,
};

/// SDL's EncodeRumbleHighAmplitude table: step k's amplitude (dekuNukem's amplitude table) * 65535
constexpr uint16_t kAmplitudeSteps[101] = {
    0,     514,   775,   921,   1096,  1303,  1550,  1843,  2192,  2606,  3100,  3686,  4383,  5213,  6199,
    7372,  7698,  8039,  8395,  8767,  9155,  9560,  9984,  10426, 10887, 11369, 11873, 12398, 12947, 13520,
    14119, 14744, 15067, 15397, 15734, 16079, 16431, 16790, 17158, 17534, 17918, 18310, 18711, 19121, 19540,
    19967, 20405, 20851, 21308, 21775, 22251, 22739, 23236, 23745, 24265, 24797, 25340, 25894, 26462, 27041,
    27633, 28238, 28856, 29488, 30134, 30794, 31468, 32157, 32861, 33581, 34316, 35068, 35836, 36620, 37422,
    38242, 39079, 39935, 40809, 41703, 42616, 43549, 44503, 45477, 46473, 47491, 48531, 49593, 50679, 51789,
    52923, 54082, 55266, 56476, 57713, 58977, 60268, 61588, 62936, 64315, 65535,
};

constexpr uint8_t kProprietaryReport = 0x80;
constexpr uint8_t kProprietaryAck = 0x81;
constexpr uint8_t kSubcommandReport = 0x01;
constexpr uint8_t kRumbleReport = 0x10;
constexpr uint8_t kSubcommandReply = 0x21;
constexpr uint8_t kFullReport = 0x30;
constexpr uint8_t kFullMcuReport = 0x31;

constexpr uint8_t kHandshake = 0x02;
constexpr uint8_t kHighSpeed = 0x03;
constexpr uint8_t kForceUsb = 0x04;
constexpr uint8_t kSetInputMode = 0x03;
constexpr uint8_t kSpiRead = 0x10;
constexpr uint8_t kPlayerLights = 0x30;
constexpr uint8_t kEnableVibration = 0x48;

constexpr uint32_t kUserCalibration = 0x8010;
constexpr uint8_t kUserCalibrationLength = 22;
constexpr uint32_t kFactoryCalibration = 0x603D;
constexpr uint8_t kFactoryCalibrationLength = 18;

std::vector<uint8_t> SpiRead(uint32_t address, uint8_t length)
{
    return {static_cast<uint8_t>(address), static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address >> 16),
            static_cast<uint8_t>(address >> 24), length};
}

uint32_t SpiAddress(const uint8_t* reply)
{
    return reply[15] | (reply[16] << 8) | (reply[17] << 16) | (static_cast<uint32_t>(reply[18]) << 24);
}

/// The control slots of Controls()
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
    kLt,
    kRt,
    kView,
    kMenu,
    kLstickClick,
    kRstickClick,
    kGuide,
    kShare,
    kLstickX,
    kLstickY,
    kRstickX,
    kRstickY,
    kSlotCount,
};

const char* const kSlotNames[kSlotCount] = {
    "south", "east",         "west",         "north", "dpad.up", "dpad.down", "dpad.left", "dpad.right",
    "lb",    "rb",           "lt",           "rt",    "view",    "menu",      "lstick.click", "rstick.click",
    "guide", "share",        "lstick.x",     "lstick.y", "rstick.x", "rstick.y",
};
}  // namespace

/// region <SwitchRumble>

uint8_t SwitchRumble::FrequencyCode(uint32_t hz)
{
    const uint64_t milli = static_cast<uint64_t>(hz) * 1000;
    const auto* end = std::end(kFrequencyBoundaries);
    const auto* at = std::upper_bound(std::begin(kFrequencyBoundaries), end, milli,
                                      [](uint64_t value, uint32_t boundary) { return value < boundary; });
    return static_cast<uint8_t>(0x40 + (at - std::begin(kFrequencyBoundaries)));
}

uint8_t SwitchRumble::AmplitudeStep(float level)
{
    if (!(level > 0))
        return 0;
    const float clamped = level > 1 ? 1 : level;
    const auto value = static_cast<uint32_t>(clamped * 65535.0f + 0.5f);
    for (uint8_t step = 0; step < 101; step++)
        if (value <= kAmplitudeSteps[step])
            return step;
    return 100;
}

std::array<uint8_t, 4> SwitchRumble::Encode(float low, uint32_t lowHz, float high, uint32_t highHz)
{
    const uint8_t lowStep = AmplitudeStep(low);
    const uint8_t highStep = AmplitudeStep(high);
    if (lowStep == 0 && highStep == 0)
        return {0x00, 0x01, 0x40, 0x40};
    // The bands' code ranges: hf 0x0004-0x01FC (codes 0x61-0xDF), lf 0x01-0x7F (codes 0x41-0xBF)
    const uint8_t highCode = std::clamp<uint8_t>(FrequencyCode(highHz != 0 ? highHz : kHighHz), 0x61, 0xDF);
    const uint8_t lowCode = std::clamp<uint8_t>(FrequencyCode(lowHz != 0 ? lowHz : kLowHz), 0x41, 0xBF);
    const uint16_t hf = static_cast<uint16_t>((highCode - 0x60) * 4);
    const uint8_t lf = static_cast<uint8_t>(lowCode - 0x40);
    const uint8_t ha = static_cast<uint8_t>(highStep * 2);
    const uint16_t la = static_cast<uint16_t>(((lowStep & 1) ? 0x8000 : 0) | (0x40 + lowStep / 2));
    return {static_cast<uint8_t>(hf & 0xFF), static_cast<uint8_t>(ha | ((hf >> 8) & 0x01)),
            static_cast<uint8_t>(lf | ((la >> 8) & 0x80)), static_cast<uint8_t>(la & 0xFF)};
}

std::array<uint8_t, 8> SwitchRumble::Pair(const RumbleLevels& levels)
{
    const std::array<uint8_t, 4> one = Encode(levels.low, levels.lowHz, levels.high, levels.highHz);
    return {one[0], one[1], one[2], one[3], one[0], one[1], one[2], one[3]};
}

std::array<uint8_t, 8> SwitchRumble::Neutral()
{
    return {0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};
}

/// endregion </SwitchRumble>

/// region <SwitchStickCalibration>

SwitchStickCalibration SwitchStickCalibration::Parse(const uint8_t* left, const uint8_t* right)
{
    auto first = [](const uint8_t* b, int at) { return static_cast<uint16_t>(((b[at + 1] << 8) & 0xF00) | b[at]); };
    auto second = [](const uint8_t* b, int at) { return static_cast<uint16_t>((b[at + 2] << 4) | (b[at + 1] >> 4)); };
    SwitchStickCalibration out;
    out.above[0][0] = first(left, 0);
    out.above[0][1] = second(left, 0);
    out.centre[0][0] = first(left, 3);
    out.centre[0][1] = second(left, 3);
    out.below[0][0] = first(left, 6);
    out.below[0][1] = second(left, 6);
    out.centre[1][0] = first(right, 0);
    out.centre[1][1] = second(right, 0);
    out.below[1][0] = first(right, 3);
    out.below[1][1] = second(right, 3);
    out.above[1][0] = first(right, 6);
    out.above[1][1] = second(right, 6);
    for (int stick = 0; stick < 2; stick++)
        for (int axis = 0; axis < 2; axis++)
        {
            // Unset flash: SDL's defaults (the centre 2048, the ranges 0.7 of the centre)
            if (out.centre[stick][axis] == 0xFFF)
                out.centre[stick][axis] = 2048;
            if (out.above[stick][axis] == 0xFFF)
                out.above[stick][axis] = static_cast<uint16_t>(out.centre[stick][axis] * 7 / 10);
            if (out.below[stick][axis] == 0xFFF)
                out.below[stick][axis] = static_cast<uint16_t>(out.centre[stick][axis] * 7 / 10);
        }
    return out;
}

/// endregion </SwitchStickCalibration>

/// region <SwitchProPad>

SwitchProPad::SwitchProPad() : _values(kSlotCount, 0.0f), _seen(kSlotCount, false)
{
    SetCalibration(SwitchStickCalibration{});
}

std::vector<std::string> SwitchProPad::Controls()
{
    return std::vector<std::string>(std::begin(kSlotNames), std::end(kSlotNames));
}

void SwitchProPad::SetCalibration(const SwitchStickCalibration& calibration)
{
    _calibration = calibration;
    for (int stick = 0; stick < 2; stick++)
        for (int axis = 0; axis < 2; axis++)
        {
            _extentBelow[stick][axis] = calibration.below[stick][axis] * 7 / 10;
            _extentAbove[stick][axis] = calibration.above[stick][axis] * 7 / 10;
        }
}

BatteryState SwitchProPad::BatteryOf(uint8_t status)
{
    BatteryState battery;
    battery.known = true;
    battery.coarse = true;
    const int level = (status & 0xE0) >> 4;  // 0, 2, 4, 6, 8
    battery.level = level * 100 / 8;
    battery.charging = (status & 0x10) != 0;
    battery.wired = (status & 0x01) != 0 || battery.charging;
    if (battery.charging && level == 8)
        battery.charging = false;  // SDL: charged
    return battery;
}

float SwitchProPad::Axis(int raw, int stick, int axis)
{
    const int delta = raw - _calibration.centre[stick][axis];
    if (delta >= 0)
    {
        int& extent = _extentAbove[stick][axis];
        if (delta > extent)
            extent = delta;
        return extent > 0 ? static_cast<float>(delta) / static_cast<float>(extent) : 0.0f;
    }
    int& extent = _extentBelow[stick][axis];
    if (-delta > extent)
        extent = -delta;
    return extent > 0 ? static_cast<float>(delta) / static_cast<float>(extent) : 0.0f;
}

void SwitchProPad::Set(size_t index, float value, bool button, const ControlSink& sink)
{
    const bool first = !_seen[index];
    if (!first && _values[index] == value)
        return;
    _seen[index] = true;
    _values[index] = value;
    if (button && first && value == 0)
        return;
    sink(kSlotNames[index], value);
}

bool SwitchProPad::Decode(const uint8_t* report, size_t size, const ControlSink& sink)
{
    if (size < 13 || (report[0] != kFullReport && report[0] != kFullMcuReport && report[0] != kSubcommandReply))
        return false;
    const uint8_t right = report[3];
    const uint8_t shared = report[4];
    const uint8_t left = report[5];
    const bool bits[] = {
        (right & 0x04) != 0,   // B
        (right & 0x08) != 0,   // A
        (right & 0x01) != 0,   // Y
        (right & 0x02) != 0,   // X
        (left & 0x02) != 0,    // up
        (left & 0x01) != 0,    // down
        (left & 0x08) != 0,    // left
        (left & 0x04) != 0,    // right
        (left & 0x40) != 0,    // L
        (right & 0x40) != 0,   // R
        (left & 0x80) != 0,    // ZL
        (right & 0x80) != 0,   // ZR
        (shared & 0x01) != 0,  // -
        (shared & 0x02) != 0,  // +
        (shared & 0x08) != 0,  // LS
        (shared & 0x04) != 0,  // RS
        (shared & 0x10) != 0,  // Home
        (shared & 0x20) != 0,  // Capture
    };
    for (size_t index = 0; index < sizeof(bits) / sizeof(bits[0]); index++)
    {
        // ZL / ZR are digital: lt / rt at 0 or 1 (SDL's trigger axes)
        const bool trigger = index == kLt || index == kRt;
        Set(index, bits[index] ? 1.0f : 0.0f, !trigger, sink);
    }
    const uint8_t* sticks[2] = {report + 6, report + 9};
    for (int stick = 0; stick < 2; stick++)
    {
        const uint8_t* b = sticks[stick];
        const int x = b[0] | ((b[1] & 0x0F) << 8);
        const int y = (b[1] >> 4) | (b[2] << 4);
        Set(stick == 0 ? kLstickX : kRstickX, Axis(x, stick, 0), false, sink);
        // Up is larger on the pad; -1 is up here (hostcontrols.h)
        Set(stick == 0 ? kLstickY : kRstickY, -Axis(y, stick, 1), false, sink);
    }
    _battery = BatteryOf(report[2]);
    return true;
}

/// endregion </SwitchProPad>

/// region <SwitchProtocol>

uint8_t SwitchProtocol::PlayerLights(int player)
{
    static const uint8_t kPatterns[8] = {0x1, 0x3, 0x7, 0xF, 0x9, 0x5, 0xD, 0x6};
    return player <= 0 ? 0 : kPatterns[(player - 1) % 8];
}

SwitchProtocol::SwitchProtocol(bool bluetooth, CalibrationSink onCalibration, uint64_t startDelayMs)
    : _bluetooth(bluetooth), _onCalibration(std::move(onCalibration))
{
    auto proprietary = [this](uint8_t command, bool wait) {
        Step step;
        step.kind = Step::Kind::Proprietary;
        step.id = command;
        step.waitReply = wait;
        _steps.push_back(step);
    };
    auto subcommand = [this](uint8_t id, std::vector<uint8_t> data) {
        Step step;
        step.kind = Step::Kind::Subcommand;
        step.id = id;
        step.data = std::move(data);
        _steps.push_back(step);
    };
    if (startDelayMs > 0)
    {
        Step delay;
        delay.delayMs = startDelayMs;
        _steps.push_back(delay);
    }
    if (!bluetooth)
    {
        proprietary(kHandshake, true);
        proprietary(kHighSpeed, true);
        proprietary(kHandshake, true);
        proprietary(kForceUsb, false);
    }
    subcommand(kSpiRead, SpiRead(kUserCalibration, kUserCalibrationLength));
    subcommand(kSpiRead, SpiRead(kFactoryCalibration, kFactoryCalibrationLength));
    subcommand(kEnableVibration, {0x01});
    if (!bluetooth)
        proprietary(kForceUsb, false);  // "ForceUSB doesn't generate an ACK"
    subcommand(kSetInputMode, {kFullReport});
}

void SwitchProtocol::Pad(std::vector<uint8_t>& report) const
{
    report.resize(_bluetooth ? kBluetoothLength : kUsbLength, 0);
}

std::vector<uint8_t> SwitchProtocol::Subcommand(uint8_t id, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> report = {kSubcommandReport, _packet};
    _packet = static_cast<uint8_t>((_packet + 1) & 0x0F);
    report.insert(report.end(), _sent.begin(), _sent.end());
    report.push_back(id);
    report.insert(report.end(), data.begin(), data.end());
    Pad(report);
    return report;
}

std::vector<uint8_t> SwitchProtocol::RumbleReport(const std::array<uint8_t, 8>& rumble)
{
    std::vector<uint8_t> report = {kRumbleReport, _packet};
    _packet = static_cast<uint8_t>((_packet + 1) & 0x0F);
    report.insert(report.end(), rumble.begin(), rumble.end());
    Pad(report);
    return report;
}

std::vector<uint8_t> SwitchProtocol::Proprietary(uint8_t command) const
{
    std::vector<uint8_t> report = {kProprietaryReport, command};
    Pad(report);
    return report;
}

void SwitchProtocol::SetRumble(const RumbleLevels& levels)
{
    _rumble = levels;
}

void SwitchProtocol::SetPlayer(int player)
{
    _player = player;
    _playerPending = true;
}

void SwitchProtocol::Advance(uint64_t nowMs)
{
    ++_step;
    _started = false;
    _stepStarted = false;
    _tries = 0;
    _stepStart = nowMs;
    if (_step >= _steps.size() && !_calibrationDone)
        Calibrated();  // the factory read never answered: the defaults stay, the user's sticks if they came
}

void SwitchProtocol::Calibrated()
{
    _calibrationDone = true;
    if (!_user || !_onCalibration)
        return;
    // A user calibration with no factory one: its sticks where the magic is, SDL's defaults elsewhere
    uint8_t left[9];
    uint8_t right[9];
    const SwitchStickCalibration defaults;
    const auto& user = *_user;
    const bool leftSet = user[0] == 0xB2 && user[1] == 0xA1;
    const bool rightSet = user[11] == 0xB2 && user[12] == 0xA1;
    if (!leftSet && !rightSet)
        return;
    std::copy(user.begin() + 2, user.begin() + 11, left);
    std::copy(user.begin() + 13, user.begin() + 22, right);
    SwitchStickCalibration calibration = SwitchStickCalibration::Parse(left, right);
    for (int axis = 0; axis < 2; axis++)
    {
        if (!leftSet)
        {
            calibration.centre[0][axis] = defaults.centre[0][axis];
            calibration.below[0][axis] = defaults.below[0][axis];
            calibration.above[0][axis] = defaults.above[0][axis];
        }
        if (!rightSet)
        {
            calibration.centre[1][axis] = defaults.centre[1][axis];
            calibration.below[1][axis] = defaults.below[1][axis];
            calibration.above[1][axis] = defaults.above[1][axis];
        }
    }
    _onCalibration(calibration);
}

void SwitchProtocol::OnInput(const uint8_t* report, size_t size, uint64_t nowMs)
{
    if (_step >= _steps.size() || !_started || size < 2)
        return;
    const Step& step = _steps[_step];
    if (step.kind == Step::Kind::Proprietary)
    {
        if (report[0] == kProprietaryAck && report[1] == step.id)
            Advance(nowMs);
        return;
    }
    if (step.kind != Step::Kind::Subcommand || report[0] != kSubcommandReply || size < 15)
        return;
    // SDL's ReadSubcommandReply: the ack bit, the same subcommand, for an SPI read the same address and length
    if ((report[13] & 0x80) == 0 || report[14] != step.id)
        return;
    if (step.id == kSpiRead)
    {
        if (size < 20 || !std::equal(step.data.begin(), step.data.end(), report + 15))
            return;
        const uint32_t address = SpiAddress(report);
        const uint8_t length = report[19];
        if (size < 20u + length)
            return;
        if (address == kUserCalibration && length == kUserCalibrationLength)
        {
            std::array<uint8_t, 22> user{};
            std::copy(report + 20, report + 20 + kUserCalibrationLength, user.begin());
            _user = user;
        }
        else if (address == kFactoryCalibration && length == kFactoryCalibrationLength && !_calibrationDone)
        {
            const uint8_t* factory = report + 20;
            uint8_t left[9];
            uint8_t right[9];
            std::copy(factory, factory + 9, left);
            std::copy(factory + 9, factory + 18, right);
            // SDL: the user calibration of a stick wins where its magic B2 A1 is set
            if (_user && (*_user)[0] == 0xB2 && (*_user)[1] == 0xA1)
                std::copy(_user->begin() + 2, _user->begin() + 11, left);
            if (_user && (*_user)[11] == 0xB2 && (*_user)[12] == 0xA1)
                std::copy(_user->begin() + 13, _user->begin() + 22, right);
            _calibrationDone = true;
            if (_onCalibration)
                _onCalibration(SwitchStickCalibration::Parse(left, right));
        }
    }
    Advance(nowMs);
}

std::optional<PadOutReport> SwitchProtocol::Next(uint64_t nowMs, uint64_t& wakeMs)
{
    wakeMs = 0;
    if (_step < _steps.size())
    {
        const Step& step = _steps[_step];
        if (!_stepStarted)
        {
            _stepStarted = true;
            _stepStart = nowMs;
        }
        if (step.kind == Step::Kind::Delay)
        {
            if (nowMs < _stepStart + step.delayMs)
            {
                wakeMs = _stepStart + step.delayMs;
                return std::nullopt;
            }
            Advance(nowMs);
            return Next(nowMs, wakeMs);
        }
        if (_started && nowMs < _deadline)
        {
            wakeMs = _deadline;
            return std::nullopt;
        }
        if (_started && _tries >= kAttempts)
        {
            Advance(nowMs);  // no reply: on without it (SDL treats most of these as optional)
            return Next(nowMs, wakeMs);
        }
        PadOutReport out;
        out.bytes = step.kind == Step::Kind::Proprietary ? Proprietary(step.id) : Subcommand(step.id, step.data);
        ++_tries;
        if (step.waitReply)
        {
            _started = true;
            _deadline = nowMs + kReplyMs;
        }
        else
            Advance(nowMs);
        return out;
    }

    if (_playerPending)
    {
        _playerPending = false;
        PadOutReport out;
        out.bytes = Subcommand(kPlayerLights, {PlayerLights(_player)});
        return out;
    }

    const std::array<uint8_t, 8> wanted = SwitchRumble::Pair(_rumble);
    const bool running = wanted != SwitchRumble::Neutral();
    const bool changed = wanted != _sent || !_rumbleWritten;
    const bool refresh = running && nowMs >= _lastRumbleMs + kRumbleRefreshMs;
    if (!changed && !refresh)
    {
        wakeMs = running ? _lastRumbleMs + kRumbleRefreshMs : 0;
        return std::nullopt;
    }
    if (_rumbleWritten && nowMs < _lastRumbleMs + kRumbleIntervalMs)
    {
        wakeMs = _lastRumbleMs + kRumbleIntervalMs;
        return std::nullopt;
    }
    if (!_rumbleWritten && !running)
    {
        // Nothing has played: the neutral rumble is what the pad runs already
        _rumbleWritten = true;
        return std::nullopt;
    }
    _rumbleWritten = true;
    _lastRumbleMs = nowMs;
    _sent = wanted;
    PadOutReport out;
    out.bytes = RumbleReport(wanted);
    return out;
}

std::optional<PadOutReport> SwitchProtocol::Final()
{
    if (_sent == SwitchRumble::Neutral())
        return std::nullopt;
    _sent = SwitchRumble::Neutral();
    PadOutReport out;
    out.bytes = RumbleReport(_sent);
    return out;
}

/// endregion </SwitchProtocol>

}  // namespace controllers
