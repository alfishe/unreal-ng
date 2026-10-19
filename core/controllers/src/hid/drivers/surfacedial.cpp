#include "hid/drivers/surfacedial.h"

#include <chrono>

namespace controllers
{

namespace
{
constexpr uint16_t kGenericDesktop = 0x01;
constexpr uint16_t kButtonPage = 0x09;
constexpr uint16_t kDigitizer = 0x0D;
constexpr uint16_t kMultiAxis = 0x0E;  ///< System Multi-Axis Controller (application collection)
constexpr uint16_t kDialUsage = 0x37;
constexpr uint16_t kTouchUsage = 0x33;
constexpr size_t kFeatureSize = 8;

HidField Field(uint32_t bitOffset, uint8_t bitSize, uint16_t page, uint16_t usage, int32_t min, int32_t max)
{
    HidField field;
    field.reportId = SurfaceDial::kInputReport;
    field.bitOffset = bitOffset;
    field.bitSize = bitSize;
    field.usagePage = page;
    field.usage = usage;
    field.logicalMin = min;
    field.logicalMax = max;
    field.application = kMultiAxis;
    return field;
}

const HidField* Find(const HidDescriptor& descriptor, uint16_t page, uint16_t usage)
{
    for (const HidField& field : descriptor.fields)
        if (field.application == kMultiAxis && field.usagePage == page && field.usage == usage)
            return &field;
    return nullptr;
}
}  // namespace

bool SurfaceDial::Handles(uint16_t vendor, uint16_t product)
{
    return vendor == kVendor && product == kProduct;
}

bool SurfaceDial::Matches(const HidDescriptor& descriptor)
{
    return Find(descriptor, kGenericDesktop, kDialUsage) != nullptr;
}

SurfaceDial::SurfaceDial()
    : _push(Field(0, 1, kButtonPage, 1, 0, 1)),
      _touch(Field(1, 1, kDigitizer, kTouchUsage, 0, 1)),
      _dial(Field(8, 16, kGenericDesktop, kDialUsage, -32767, 32767))
{
}

SurfaceDial::SurfaceDial(const HidDescriptor& descriptor) : SurfaceDial()
{
    _hasReportIds = descriptor.hasReportIds;
    if (const HidField* dial = Find(descriptor, kGenericDesktop, kDialUsage))
        _dial = *dial;
    const HidField* push = Find(descriptor, kButtonPage, 1);
    const HidField* touch = Find(descriptor, kDigitizer, kTouchUsage);
    _hasPush = push != nullptr;
    _hasTouch = touch != nullptr;
    if (_hasPush)
        _push = *push;
    if (_hasTouch)
        _touch = *touch;
}

std::vector<std::string> SurfaceDial::Controls()
{
    return {kKnob, kPush, kTouch};
}

bool DialTouch::Update(bool sensor, bool turned, bool pushed, uint64_t nowMs)
{
    _pushed = pushed;
    if (turned || pushed)
    {
        _lastActionMs = nowMs;
        if (!sensor)
            _inferred = true;
    }
    if (sensor)
        _inferred = false;  // the sensor caught up: its release ends the touch
    _sensor = sensor;
    return Touched();
}

bool DialTouch::Expire(uint64_t nowMs)
{
    if (_inferred && !_pushed && nowMs - _lastActionMs >= kUnconfirmedMs)
        _inferred = false;
    return Touched();
}

uint64_t DialNowMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

void SurfaceDial::Decode(const uint8_t* report, size_t size, const ControlSink& sink, uint64_t nowMs)
{
    int32_t steps = 0;
    if (!ReadHidField(_dial, _hasReportIds, report, size, steps))
        return;  // another report: sleep / wake, the vendor page
    int32_t touch = 0;
    int32_t push = 0;
    const bool sensor = _hasTouch && ReadHidField(_touch, _hasReportIds, report, size, touch) && touch != 0;
    const bool pushed = _hasPush && ReadHidField(_push, _hasReportIds, report, size, push) && push != 0;
    const bool touched = _hasTouch && _hand.Update(sensor, steps != 0, pushed, nowMs);

    // The hand arrives before what it does and leaves after it
    if (touched && !_touched)
        sink(kTouch, 1.0f);
    if (pushed != _pushed)
        sink(kPush, pushed ? 1.0f : 0.0f);
    if (steps != 0)
        sink(kKnob, static_cast<float>(steps));
    if (!touched && _touched)
        sink(kTouch, 0.0f);
    _touched = touched;
    _pushed = pushed;
}

void SurfaceDial::Expire(uint64_t nowMs, const ControlSink& sink)
{
    if (!_hasTouch)
        return;
    const bool touched = _hand.Expire(nowMs);
    if (!touched && _touched)
        sink(kTouch, 0.0f);
    _touched = touched;
}

bool SurfaceDial::ParseFeature(const uint8_t* report, size_t size, SurfaceDialFeature& out)
{
    if (report == nullptr || size < kFeatureSize || report[0] != kFeatureReport)
        return false;
    out.stepsPerTurn = static_cast<uint16_t>(report[1] | (report[2] << 8));
    out.repeatCount = report[3];
    out.autoTrigger = report[4];
    out.cutoff = report[5];
    out.retriggerMs = static_cast<uint16_t>(report[6] | (report[7] << 8));
    return true;
}

std::vector<uint8_t> SurfaceDial::HapticReport(const HapticClicks& clicks)
{
    constexpr uint8_t kClick = 3;
    constexpr uint8_t kBuzz = 4;
    // Repeat Count is the number of plays: the measured 01 03 03 00 00 gave three clicks
    const uint8_t count = clicks.count > 0 ? clicks.count : 1;
    return {kOutputReport, count, clicks.wave == HapticClicks::Wave::Buzz ? kBuzz : kClick,
            static_cast<uint8_t>(clicks.periodMs & 0xFF), static_cast<uint8_t>(clicks.periodMs >> 8)};
}

}  // namespace controllers
