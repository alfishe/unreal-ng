#include "hid/drivers/steamdeck.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace controllers
{

namespace
{
constexpr uint8_t kDeckState = 9;  // ID_CONTROLLER_DECK_STATE

// Feature report message types (controller_constants.h)
constexpr uint8_t kClearDigitalMappings = 0x81;
constexpr uint8_t kSetSettingsValues = 0x87;
constexpr uint8_t kHapticPulse = 0x8F;
constexpr uint8_t kHapticCommand = 0xEA;
constexpr uint8_t kRumbleCommand = 0xEB;

// Settings (ControllerSettings) and their values
constexpr uint8_t kLeftTrackpadMode = 7;
constexpr uint8_t kRightTrackpadMode = 8;
constexpr uint8_t kSmoothAbsoluteMouse = 24;
constexpr uint8_t kLeftTrackpadClickPressure = 52;
constexpr uint8_t kRightTrackpadClickPressure = 53;
constexpr uint16_t kTrackpadNone = 7;

struct Bit
{
    uint32_t mask;
    const char* control;
};
constexpr Bit kLow[] = {
    {0x00000080, "south"},          {0x00000020, "east"},           {0x00000040, "west"},
    {0x00000010, "north"},          {0x00000100, "dpad.up"},        {0x00000800, "dpad.down"},
    {0x00000400, "dpad.left"},      {0x00000200, "dpad.right"},     {0x00000008, "lb"},
    {0x00000004, "rb"},             {0x00001000, "view"},           {0x00004000, "menu"},
    {0x00002000, "guide"},          {0x00010000, "paddle.p2"},      {0x00008000, "paddle.p4"},
    {0x00400000, "lstick.click"},   {0x04000000, "rstick.click"},   {0x00020000, "ltouchpad.click"},
    {0x00040000, "rtouchpad.click"}, {0x00080000, "ltouchpad.touch"}, {0x00100000, "rtouchpad.touch"},
};
constexpr Bit kHigh[] = {
    {0x00000400, "paddle.p1"},
    {0x00000200, "paddle.p3"},
    {0x00004000, "lstick.touch"},
    {0x00008000, "rstick.touch"},
    {0x00040000, "misc.1"},  // the quick access button (···)
};
constexpr uint32_t kLeftTouch = 0x00080000;
constexpr uint32_t kRightTouch = 0x00100000;

/// The analog fields: their offset in the report, the control, and how a raw value maps to it
enum class Scale : uint8_t
{
    Stick,     ///< int16 -> -1..1
    StickY,    ///< int16, + up -> -1..1, - up
    Unsigned,  ///< 0..32767 -> 0..1
    Motion,    ///< int16 -> -1..1 (full scale)
    MotionNeg,
};
struct Analog
{
    size_t offset;
    const char* control;
    Scale scale;
    int deadband;  ///< raw counts a change must exceed: the motion sensors and the pads are never still
};
// The trackpads first (0..3: they read 0 while untouched), then the rest. Motion in SDL's frame: (x, z, -y)
constexpr Analog kAnalog[] = {
    {16, "ltouchpad.x", Scale::Stick, 16},      {18, "ltouchpad.y", Scale::StickY, 16},
    {20, "rtouchpad.x", Scale::Stick, 16},      {22, "rtouchpad.y", Scale::StickY, 16},
    {24, "accel.x", Scale::Motion, 32},         {28, "accel.y", Scale::Motion, 32},
    {26, "accel.z", Scale::MotionNeg, 32},      {30, "gyro.x", Scale::Motion, 16},
    {34, "gyro.y", Scale::Motion, 16},          {32, "gyro.z", Scale::MotionNeg, 16},
    {44, "lt", Scale::Unsigned, 0},             {46, "rt", Scale::Unsigned, 0},
    {48, "lstick.x", Scale::Stick, 0},          {50, "lstick.y", Scale::StickY, 0},
    {52, "rstick.x", Scale::Stick, 0},          {54, "rstick.y", Scale::StickY, 0},
    {56, "ltouchpad.pressure", Scale::Unsigned, 64}, {58, "rtouchpad.pressure", Scale::Unsigned, 64},
};
constexpr size_t kAnalogCount = sizeof(kAnalog) / sizeof(kAnalog[0]);
static_assert(kAnalogCount <= 20, "SteamDeck::_axes holds the analog fields");

uint32_t U32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

int16_t I16(const uint8_t* p)
{
    return static_cast<int16_t>(static_cast<uint16_t>(p[0] | p[1] << 8));
}

float Value(int16_t raw, Scale scale)
{
    switch (scale)
    {
        case Scale::Stick:
            return raw >= 0 ? raw / 32767.0f : raw / 32768.0f;
        case Scale::StickY:
            return raw >= 0 ? -raw / 32767.0f : -raw / 32768.0f;
        case Scale::Unsigned:
            return std::clamp(raw / 32767.0f, 0.0f, 1.0f);
        case Scale::Motion:
            return raw / 32768.0f;
        case Scale::MotionNeg:
            return raw == -32768 ? 1.0f : -raw / 32768.0f;
    }
    return 0;
}

std::vector<uint8_t> Feature(uint8_t type, std::initializer_list<uint8_t> payload)
{
    std::vector<uint8_t> report(SteamDeck::kFeatureSize, 0);
    report[1] = type;
    report[2] = static_cast<uint8_t>(payload.size());
    std::copy(payload.begin(), payload.end(), report.begin() + 3);
    return report;
}

uint8_t Lo(unsigned value)
{
    return static_cast<uint8_t>(value & 0xFF);
}

uint8_t Hi(unsigned value)
{
    return static_cast<uint8_t>((value >> 8) & 0xFF);
}

uint16_t Level16(float level)
{
    return static_cast<uint16_t>(std::clamp(level, 0.0f, 1.0f) * 65535.0f + 0.5f);
}
}  // namespace

bool SteamDeck::Handles(uint16_t vendor, uint16_t product)
{
    return vendor == kVendor && product == kProduct;
}

std::vector<std::string> SteamDeck::Controls()
{
    std::vector<std::string> out;
    for (const Bit& bit : kLow)
        out.emplace_back(bit.control);
    for (const Bit& bit : kHigh)
        out.emplace_back(bit.control);
    for (const Analog& analog : kAnalog)
        out.emplace_back(analog.control);
    return out;
}

void SteamDeck::Decode(const uint8_t* report, size_t size, const ControlSink& sink)
{
    if (size < kReportSize || report[0] != 1 || report[2] != kDeckState)
        return;
    _reports++;
    const uint32_t low = U32(report + 8);
    const uint32_t high = U32(report + 12);
    for (const Bit& bit : kLow)
        if (((low ^ _buttonsLow) & bit.mask) || (!_seen && (low & bit.mask)))
            sink(bit.control, low & bit.mask ? 1.0f : 0.0f);
    for (const Bit& bit : kHigh)
        if (((high ^ _buttonsHigh) & bit.mask) || (!_seen && (high & bit.mask)))
            sink(bit.control, high & bit.mask ? 1.0f : 0.0f);
    _buttonsLow = low;
    _buttonsHigh = high;

    const bool touched[2] = {(low & kLeftTouch) != 0, (low & kRightTouch) != 0};
    for (size_t index = 0; index < kAnalogCount; ++index)
    {
        const Analog& analog = kAnalog[index];
        int16_t raw = I16(report + analog.offset);
        const bool pad = index < 4;
        if (pad && !touched[index / 2])
            raw = 0;  // the last position stays in the report after the finger left
        const bool released = pad && _padTouched[index / 2] && !touched[index / 2];
        if (_seen && !released && std::abs(raw - _axes[index]) <= analog.deadband)
            continue;
        _axes[index] = raw;
        sink(analog.control, Value(raw, analog.scale));
    }
    _padTouched[0] = touched[0];
    _padTouched[1] = touched[1];
    _seen = true;
}

bool SteamDeck::WatchdogDue()
{
    if (_reports < kWatchdogReports)
        return false;
    _reports = 0;
    return true;
}

std::vector<std::vector<uint8_t>> SteamDeck::LizardOffReports()
{
    return {Feature(kClearDigitalMappings, {}),
            Feature(kSetSettingsValues, {kSmoothAbsoluteMouse, 0, 0,                                         //
                                         kLeftTrackpadMode, Lo(kTrackpadNone), Hi(kTrackpadNone),            //
                                         kRightTrackpadMode, Lo(kTrackpadNone), Hi(kTrackpadNone),           //
                                         kLeftTrackpadClickPressure, 0xFF, 0xFF,                             //
                                         kRightTrackpadClickPressure, 0xFF, 0xFF})};
}

std::vector<std::vector<uint8_t>> SteamDeck::WatchdogReports()
{
    return {Feature(kClearDigitalMappings, {}),
            Feature(kSetSettingsValues, {kRightTrackpadMode, Lo(kTrackpadNone), Hi(kTrackpadNone)})};
}

std::vector<uint8_t> SteamDeck::RumbleReport(float left, float right)
{
    // MsgSimpleRumbleCmd: type 0, intensity 0 (the system's), the speeds, the gains SDL and hid-steam use
    const uint16_t l = Level16(left);
    const uint16_t r = Level16(right);
    return Feature(kRumbleCommand, {0, 0, 0, Lo(l), Hi(l), Lo(r), Hi(r), 2, 0});
}

std::vector<uint8_t> SteamDeck::PulseReport(Pad pad, uint16_t onUs, uint16_t offUs, uint16_t count, int8_t gainDb)
{
    // Left and right are swapped in this report (hid-steam: "for legacy reasons"): 0 is the right pad here
    const uint8_t side = pad == Pad::Left ? 1 : pad == Pad::Right ? 0 : 2;
    return Feature(kHapticPulse, {side, Lo(onUs), Hi(onUs), Lo(offUs), Hi(offUs), Lo(count), Hi(count),
                                  static_cast<uint8_t>(gainDb)});
}

std::vector<uint8_t> SteamDeck::PadEffectReport(const PadEffect& effect)
{
    const auto duration = static_cast<uint16_t>(effect.durationMs);
    return Feature(kHapticCommand,
                   {static_cast<uint8_t>(effect.pad), static_cast<uint8_t>(effect.command), 0,
                    static_cast<uint8_t>(effect.gainDb), Lo(effect.frequency), Hi(effect.frequency), Lo(duration),
                    Hi(duration), Lo(effect.noiseIntensity), Hi(effect.noiseIntensity), Lo(effect.lfoFrequency),
                    Hi(effect.lfoFrequency), effect.lfoDepth, 0, effect.scriptId, Lo(effect.sweepStart),
                    Hi(effect.sweepStart), Lo(effect.sweepEnd), Hi(effect.sweepEnd)});
}

int8_t SteamDeck::GainDb(float level)
{
    if (level <= 0.0f)
        return -24;
    const float db = 20.0f * std::log10(std::min(level, 1.0f));
    return static_cast<int8_t>(std::lround(std::max(db, -24.0f)));
}

}  // namespace controllers
