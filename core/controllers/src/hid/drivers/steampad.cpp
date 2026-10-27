#include "hid/drivers/steampad.h"

#include <algorithm>

namespace controllers
{

namespace
{
/// The control slots of both models
enum Slot : size_t
{
    kSouth,
    kEast,
    kWest,
    kNorth,
    kLb,
    kRb,
    kLt,
    kRt,
    kView,
    kMenu,
    kGuide,
    kLstickClick,
    kRstickClick,
    kMisc1,
    kMisc2,
    kMisc3,
    kP1,
    kP2,
    kP3,
    kP4,
    kDpadUp,
    kDpadDown,
    kDpadLeft,
    kDpadRight,
    kLstickX,
    kLstickY,
    kRstickX,
    kRstickY,
    kSlotCount,
};

const char* const kSlotNames[kSlotCount] = {
    "south",        "east",         "west",      "north",     "lb",        "rb",         "lt",
    "rt",           "view",         "menu",      "guide",     "lstick.click", "rstick.click", "misc.1",
    "misc.2",       "misc.3",       "paddle.p1", "paddle.p2", "paddle.p3", "paddle.p4",  "dpad.up",
    "dpad.down",    "dpad.left",    "dpad.right", "lstick.x", "lstick.y",  "rstick.x",   "rstick.y",
};

/// The 2015 pad has no QAM, no right touchpad click of its own (the right pad is the right stick) and two grips
bool ControllerHas(size_t slot)
{
    return slot != kMisc2 && slot != kMisc3 && slot != kP2 && slot != kP4;
}

/// SDL_hidapi_steam.c's masks of ulButtons
constexpr uint64_t kRightBumper = 0x00000004;
constexpr uint64_t kLeftBumper = 0x00000008;
constexpr uint64_t kNorthMask = 0x00000010;
constexpr uint64_t kEastMask = 0x00000020;
constexpr uint64_t kWestMask = 0x00000040;
constexpr uint64_t kSouthMask = 0x00000080;
constexpr uint64_t kDpadUpMask = 0x00000100;
constexpr uint64_t kDpadRightMask = 0x00000200;
constexpr uint64_t kDpadLeftMask = 0x00000400;
constexpr uint64_t kDpadDownMask = 0x00000800;
constexpr uint64_t kMenuMask = 0x00001000;  ///< the left of the Steam button: Back
constexpr uint64_t kSteamMask = 0x00002000;
constexpr uint64_t kEscapeMask = 0x00004000;  ///< the right one: Start
constexpr uint64_t kBackLeftMask = 0x00008000;
constexpr uint64_t kBackRightMask = 0x00010000;
constexpr uint64_t kLeftPadClicked = 0x00020000;
constexpr uint64_t kRightPadClicked = 0x00040000;
constexpr uint64_t kLeftPadFingerDown = 0x00080000;
constexpr uint64_t kRightPadFingerDown = 0x00100000;
constexpr uint64_t kJoystickButton = 0x00400000;
constexpr uint64_t kLeftPadAndJoystick = 0x00800000;
constexpr uint64_t kTriggerBytes = 0xFFFF000000ULL;  ///< bytes 3-4: the triggers, not buttons

/// SDL's SteamControllerTriton button bits
constexpr uint32_t kTritonA = 0x00000001;
constexpr uint32_t kTritonB = 0x00000002;
constexpr uint32_t kTritonX = 0x00000004;
constexpr uint32_t kTritonY = 0x00000008;
constexpr uint32_t kTritonQam = 0x00000010;
constexpr uint32_t kTritonR3 = 0x00000020;
constexpr uint32_t kTritonView = 0x00000040;
constexpr uint32_t kTritonR4 = 0x00000080;
constexpr uint32_t kTritonR5 = 0x00000100;
constexpr uint32_t kTritonR = 0x00000200;
constexpr uint32_t kTritonDpadDown = 0x00000400;
constexpr uint32_t kTritonDpadRight = 0x00000800;
constexpr uint32_t kTritonDpadLeft = 0x00001000;
constexpr uint32_t kTritonDpadUp = 0x00002000;
constexpr uint32_t kTritonMenu = 0x00004000;
constexpr uint32_t kTritonL3 = 0x00008000;
constexpr uint32_t kTritonSteam = 0x00010000;
constexpr uint32_t kTritonL4 = 0x00020000;
constexpr uint32_t kTritonL5 = 0x00040000;
constexpr uint32_t kTritonL = 0x00080000;
constexpr uint32_t kTritonRightPadClick = 0x00400000;
constexpr uint32_t kTritonLeftPadClick = 0x04000000;

constexpr uint8_t kTritonState = 0x42;
constexpr uint8_t kTritonStateBle = 0x45;
constexpr uint8_t kTritonWirelessX = 0x46;
constexpr uint8_t kTritonStateTimestamp = 0x47;
constexpr uint8_t kTritonWireless = 0x79;
constexpr size_t kTritonStateSize = 1 + 45;  ///< the id and TritonMTUNoQuat_t

constexpr uint8_t kClearDigitalMappings = 0x81;
constexpr uint8_t kSetSettingsValues = 0x87;
constexpr uint8_t kLoadDefaultSettings = 0x8E;
constexpr uint8_t kDongleGetWirelessState = 0xB4;
constexpr uint8_t kSettingLeftTrackpadMode = 7;
constexpr uint8_t kSettingRightTrackpadMode = 8;
constexpr uint8_t kSettingLizardMode = 9;
constexpr uint8_t kSettingSmoothAbsoluteMouse = 24;
constexpr uint8_t kSettingWirelessPacketVersion = 49;
constexpr uint8_t kTrackpadNone = 7;
constexpr int kTriggerMaxAnalog = 26000;  ///< STEAMCONTROLLER_TRIGGER_MAX_ANALOG

int16_t S16(const uint8_t* at)
{
    return static_cast<int16_t>(static_cast<uint16_t>(at[0] | (at[1] << 8)));
}

float Stick(int value)
{
    return std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f);
}

/// SDL's RotatePad by 15 degrees, the sine and cosine as constants (no libm: the same bits on every compiler)
void Rotate(int& x, int& y, bool clockwise)
{
    constexpr float kCos = 0.96592583f;
    constexpr float kSin = 0.25881905f;
    const float s = clockwise ? -kSin : kSin;
    const int ox = x;
    const int oy = y;
    x = static_cast<int>(kCos * static_cast<float>(ox) - s * static_cast<float>(oy));
    y = static_cast<int>(s * static_cast<float>(ox) + kCos * static_cast<float>(oy));
}
}  // namespace

/// region <SteamPad>

std::optional<SteamModel> SteamPad::ModelOf(uint16_t vendor, uint16_t product)
{
    if (vendor != kValve)
        return std::nullopt;
    switch (product)
    {
        case 0x1102: return SteamModel::Controller;
        case 0x1142: return SteamModel::ControllerDongle;
        case 0x1302:
        case 0x1303: return SteamModel::Triton;
        case 0x1304:
        case 0x1305: return SteamModel::TritonDongle;
        default: return std::nullopt;
    }
}

std::vector<std::string> SteamPad::Controls(SteamModel model)
{
    std::vector<std::string> names;
    for (size_t slot = 0; slot < kSlotCount; slot++)
        if (IsTriton(model) || ControllerHas(slot))
            names.emplace_back(kSlotNames[slot]);
    return names;
}

SteamPad::SteamPad(SteamModel model) : _model(model), _values(kSlotCount, 0.0f), _seen(kSlotCount, false)
{
}

void SteamPad::Set(size_t slot, float value, const ControlSink& sink)
{
    if (_seen[slot] && _values[slot] == value)
        return;
    _seen[slot] = true;
    _values[slot] = value;
    if (sink)
        sink(kSlotNames[slot], value);
}

void SteamPad::Release(const ControlSink& sink)
{
    for (size_t slot = 0; slot < kSlotCount; slot++)
        if (_seen[slot] && _values[slot] != 0.0f)
            Set(slot, 0.0f, sink);
    _lastStick[0] = _lastStick[1] = 0;
}

SteamPad::Event SteamPad::EventOf(SteamModel model, const uint8_t* report, size_t size)
{
    if (report == nullptr || size < 2)
        return Event::None;
    if (IsTriton(model))
    {
        if (report[0] == kTritonWireless || report[0] == kTritonWirelessX)
            return report[1] == 1 ? Event::Disconnected : report[1] == 2 ? Event::Connected : Event::None;
        const bool state =
            report[0] == kTritonState || report[0] == kTritonStateBle || report[0] == kTritonStateTimestamp;
        return state && size >= kTritonStateSize ? Event::State : Event::None;
    }
    if (size < 4 || report[0] != 0x01 || report[1] != 0x00)
        return Event::None;
    if (report[2] == 3)
        return size < 5 || report[3] < 1 ? Event::None : report[4] == 1 ? Event::Disconnected : Event::Connected;
    return report[2] == 1 && size >= 24 ? Event::State : Event::None;
}

SteamPad::Event SteamPad::Decode(const uint8_t* report, size_t size, const ControlSink& sink)
{
    if (report == nullptr || size == 0)
        return Event::None;
    return IsTriton(_model) ? DecodeTriton(report, size, sink) : DecodeController(report, size, sink);
}

SteamPad::Event SteamPad::DecodeController(const uint8_t* report, size_t size, const ControlSink& sink)
{
    // ValveInReportHeader_t: version 0x0001, type, length; type 3 the dongle's wireless event (D0G_IS_VALID_
    // WIRELESS_EVENT: a length of at least 1, the event in byte 4)
    const Event event = EventOf(_model, report, size);
    if (event != Event::State)
        return event;
    // ValveControllerStatePacket_t from byte 4: the packet number, the buttons at 8, the pads at 16 and 20
    uint64_t buttons = 0;
    for (int i = 7; i >= 0; i--)
        buttons = buttons << 8 | report[8 + i];
    const int leftTrigger = report[11];
    const int rightTrigger = report[12];
    buttons &= ~kTriggerBytes;
    const int16_t leftX = S16(report + 16);
    const int16_t leftY = S16(report + 18);

    // FormatStatePacketUntilGyro: the left pair is the stick unless the left pad is touched
    int16_t stickX = 0;
    int16_t stickY = 0;
    if (buttons & kLeftPadFingerDown)
    {
        if (buttons & kLeftPadAndJoystick)
        {
            stickX = _lastStick[0];
            stickY = _lastStick[1];
        }
        else
            _lastStick[0] = _lastStick[1] = 0;
    }
    else
    {
        stickX = _lastStick[0] = leftX;
        stickY = _lastStick[1] = leftY;
        // Old firmware sends the pad's click for the stick's when the pad is not in use
        if (!(buttons & kLeftPadAndJoystick) && (buttons & kLeftPadClicked))
        {
            buttons &= ~kLeftPadClicked;
            buttons |= kJoystickButton;
        }
    }
    int rightX = S16(report + 20);
    int rightY = S16(report + 22);
    Rotate(rightX, rightY, false);
    if (buttons & kRightPadFingerDown)
    {
        rightX += 1000;
        rightY += 1000;
    }

    const auto bit = [buttons](uint64_t mask) { return (buttons & mask) != 0 ? 1.0f : 0.0f; };
    Set(kSouth, bit(kSouthMask), sink);
    Set(kEast, bit(kEastMask), sink);
    Set(kWest, bit(kWestMask), sink);
    Set(kNorth, bit(kNorthMask), sink);
    Set(kLb, bit(kLeftBumper), sink);
    Set(kRb, bit(kRightBumper), sink);
    Set(kView, bit(kMenuMask), sink);
    Set(kMenu, bit(kEscapeMask), sink);
    Set(kGuide, bit(kSteamMask), sink);
    Set(kLstickClick, bit(kJoystickButton), sink);
    Set(kRstickClick, bit(kRightPadClicked), sink);
    Set(kMisc1, bit(kLeftPadClicked), sink);
    Set(kP1, bit(kBackRightMask), sink);
    Set(kP3, bit(kBackLeftMask), sink);
    Set(kDpadUp, bit(kDpadUpMask), sink);
    Set(kDpadDown, bit(kDpadDownMask), sink);
    Set(kDpadLeft, bit(kDpadLeftMask), sink);
    Set(kDpadRight, bit(kDpadRightMask), sink);
    // The trigger byte n as SDL widens it: (n << 7 | n) of 26000 is full
    const auto trigger = [](int n) {
        return std::min(1.0f, static_cast<float>(n << 7 | n) / static_cast<float>(kTriggerMaxAnalog));
    };
    Set(kLt, trigger(leftTrigger), sink);
    Set(kRt, trigger(rightTrigger), sink);
    // Up is positive on the pad; -1 is up here (hostcontrols.h)
    Set(kLstickX, Stick(stickX), sink);
    Set(kLstickY, -Stick(stickY), sink);
    Set(kRstickX, Stick(rightX), sink);
    Set(kRstickY, -Stick(rightY), sink);
    return Event::State;
}

SteamPad::Event SteamPad::DecodeTriton(const uint8_t* report, size_t size, const ControlSink& sink)
{
    const Event event = EventOf(_model, report, size);
    if (event != Event::State)
        return event;
    // TritonMTUNoQuat_t (and the timestamped one, the same up to the sticks) after the id: a sequence number, the
    // buttons at 2, the triggers at 6 and 8, the sticks at 10-17
    const uint32_t buttons = static_cast<uint32_t>(report[2]) | static_cast<uint32_t>(report[3]) << 8 |
                             static_cast<uint32_t>(report[4]) << 16 | static_cast<uint32_t>(report[5]) << 24;
    const auto bit = [buttons](uint32_t mask) { return (buttons & mask) != 0 ? 1.0f : 0.0f; };
    Set(kSouth, bit(kTritonA), sink);
    Set(kEast, bit(kTritonB), sink);
    Set(kWest, bit(kTritonX), sink);
    Set(kNorth, bit(kTritonY), sink);
    Set(kLb, bit(kTritonL), sink);
    Set(kRb, bit(kTritonR), sink);
    // SDL_hidapi_steam_triton.c: BACK from the MENU bit, START from the VIEW bit
    Set(kView, bit(kTritonMenu), sink);
    Set(kMenu, bit(kTritonView), sink);
    Set(kGuide, bit(kTritonSteam), sink);
    Set(kMisc2, bit(kTritonQam), sink);
    Set(kLstickClick, bit(kTritonL3), sink);
    Set(kRstickClick, bit(kTritonR3), sink);
    // R4 / R5 / L4 / L5: SDL's right and left paddles 1 / 2, the Elite P1 P2 (right) P3 P4 (left)
    Set(kP1, bit(kTritonR4), sink);
    Set(kP2, bit(kTritonR5), sink);
    Set(kP3, bit(kTritonL4), sink);
    Set(kP4, bit(kTritonL5), sink);
    Set(kMisc1, bit(kTritonLeftPadClick), sink);
    Set(kMisc3, bit(kTritonRightPadClick), sink);
    Set(kDpadUp, bit(kTritonDpadUp), sink);
    Set(kDpadDown, bit(kTritonDpadDown), sink);
    Set(kDpadLeft, bit(kTritonDpadLeft), sink);
    Set(kDpadRight, bit(kTritonDpadRight), sink);
    Set(kLt, std::clamp(static_cast<float>(S16(report + 6)) / 32767.0f, 0.0f, 1.0f), sink);
    Set(kRt, std::clamp(static_cast<float>(S16(report + 8)) / 32767.0f, 0.0f, 1.0f), sink);
    Set(kLstickX, Stick(S16(report + 10)), sink);
    Set(kLstickY, -Stick(S16(report + 12)), sink);
    Set(kRstickX, Stick(S16(report + 14)), sink);
    Set(kRstickY, -Stick(S16(report + 16)), sink);
    return Event::State;
}

/// endregion </SteamPad>

/// region <SteamProtocol>

std::vector<std::vector<uint8_t>> SteamProtocol::ControllerSetup()
{
    const auto message = [](uint8_t type, std::vector<uint8_t> data) {
        std::vector<uint8_t> out(65, 0);
        out[1] = type;
        out[2] = static_cast<uint8_t>(data.size());
        std::copy(data.begin(), data.end(), out.begin() + 3);
        return out;
    };
    std::vector<std::vector<uint8_t>> out;
    out.push_back(message(kClearDigitalMappings, {}));
    // LOAD_DEFAULT_SETTINGS goes with a length byte of 0 and SDL's three bytes
    out.push_back(message(kLoadDefaultSettings, {}));
    // ADD_SETTING: the number, the value low and high
    out.push_back(message(kSetSettingsValues, {kSettingWirelessPacketVersion, 2, 0, kSettingLeftTrackpadMode,
                                               kTrackpadNone, 0, kSettingRightTrackpadMode, kTrackpadNone, 0,
                                               kSettingSmoothAbsoluteMouse, 0, 0}));
    return out;
}

std::vector<uint8_t> SteamProtocol::TritonLizardOff()
{
    std::vector<uint8_t> out(64, 0);
    out[0] = 1;  // the report id
    out[1] = kSetSettingsValues;
    out[2] = 3;  // one ControllerSetting
    out[3] = kSettingLizardMode;
    out[4] = 0;  // LIZARD_MODE_OFF
    out[5] = 0;
    return out;
}

SteamProtocol::SteamProtocol(SteamModel model) : _model(model), _connected(!SteamPad::IsDongle(model))
{
    if (model == SteamModel::ControllerDongle)
    {
        // HIDAPI_DriverSteam_InitDevice: ask the dongle whether a pad is there; set it up anyway (one that is not
        // refuses, one that comes later is set up again on its connect event)
        std::vector<uint8_t> ask(65, 0);
        ask[1] = kDongleGetWirelessState;
        _queue.push_back(ask);
    }
    if (!SteamPad::IsTriton(model))
        QueueSetup();
}

void SteamProtocol::QueueSetup()
{
    for (auto& message : ControllerSetup())
        _queue.push_back(std::move(message));
}

std::optional<PadOutReport> SteamProtocol::Next(uint64_t nowMs, uint64_t& wakeMs)
{
    wakeMs = 0;
    if (!_queue.empty())
    {
        PadOutReport out;
        out.kind = PadOutReport::Kind::SetFeature;
        out.bytes = std::move(_queue.front());
        _queue.erase(_queue.begin());
        return out;
    }
    if (SteamPad::IsTriton(_model) && _connected)
    {
        if (nowMs >= _nextTritonMs)
        {
            _nextTritonMs = nowMs + kTritonRepeatMs;
            PadOutReport out;
            out.kind = PadOutReport::Kind::SetFeature;
            out.bytes = TritonLizardOff();
            return out;
        }
        wakeMs = _nextTritonMs;
    }
    return std::nullopt;
}

void SteamProtocol::OnInput(const uint8_t* report, size_t size, uint64_t nowMs)
{
    (void)nowMs;
    const SteamPad::Event event = SteamPad::EventOf(_model, report, size);
    if (event == SteamPad::Event::Connected || (event == SteamPad::Event::State && !_connected))
    {
        if (!_connected && !SteamPad::IsTriton(_model))
            QueueSetup();  // a pad came to the dongle: out of lizard mode it goes too
        _connected = true;
        _nextTritonMs = 0;  // Triton's lizard mode off at once
    }
    else if (event == SteamPad::Event::Disconnected)
        _connected = false;
}

/// endregion </SteamProtocol>

}  // namespace controllers
