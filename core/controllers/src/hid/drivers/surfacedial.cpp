#include "hid/drivers/surfacedial.h"

#include <chrono>

#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#endif

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

std::vector<uint8_t> SurfaceDial::PatternReport(HapticPattern pattern)
{
    switch (pattern)
    {
        case HapticPattern::Tick: return HapticReport({HapticClicks::Wave::Click, 1, 80});
        case HapticPattern::Double: return HapticReport({HapticClicks::Wave::Click, 2, 60});
        case HapticPattern::Buzz: return HapticReport({HapticClicks::Wave::Buzz, 1, 80});
    }
    return HapticReport({});
}

std::vector<uint8_t> SurfaceDial::FeatureReport(const StepsOut& steps, const SurfaceDialFeature& base)
{
    constexpr uint8_t kAutoNone = 1;
    constexpr uint8_t kAutoClick = 3;
    const uint16_t perTurn = steps.perTurn == 0 ? 36 : steps.perTurn;
    // The descriptor's logical minimum of the cutoff and the retrigger is 1: 0 would be ignored by the device
    const uint8_t cutoff = base.cutoff == 0 ? 5 : base.cutoff;
    const uint16_t retrigger = base.retriggerMs == 0 ? 80 : base.retriggerMs;
    return {kFeatureReport,
            static_cast<uint8_t>(perTurn & 0xFF),
            static_cast<uint8_t>(perTurn >> 8),
            0,
            steps.clicks ? kAutoClick : kAutoNone,
            cutoff,
            static_cast<uint8_t>(retrigger & 0xFF),
            static_cast<uint8_t>(retrigger >> 8)};
}

/// region <SurfaceDialWriter>

SurfaceDialWriter::SurfaceDialWriter(Write write) : _write(std::move(write)), _thread([this] { Run(); })
{
}

SurfaceDialWriter::~SurfaceDialWriter()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stopping = true;
        _feature.reset();
        _output.reset();
    }
    _wake.notify_all();
    _thread.join();
}

void SurfaceDialWriter::Steps(const StepsOut& steps)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _feature = SurfaceDial::FeatureReport(steps);
    }
    _wake.notify_all();
}

void SurfaceDialWriter::Pattern(HapticPattern pattern)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _output = SurfaceDial::PatternReport(pattern);
    }
    _wake.notify_all();
}

void SurfaceDialWriter::Clicks(const HapticClicks& clicks)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _output = SurfaceDial::HapticReport(clicks);
    }
    _wake.notify_all();
}

uint64_t SurfaceDialWriter::Written() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _written;
}

uint64_t SurfaceDialWriter::Failed() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _failed;
}

void SurfaceDialWriter::Run()
{
#if defined(__APPLE__)
    pthread_setname_np("dial-writer");
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), "dial-writer");
#endif
    std::unique_lock<std::mutex> lock(_mutex);
    while (true)
    {
        _wake.wait(lock, [this] { return _stopping || _feature || _output; });
        if (_stopping)
            return;
        // The steps first: a pattern after a context change plays in the new feel
        const ReportType type = _feature ? ReportType::Feature : ReportType::Output;
        std::vector<uint8_t> report = std::move(type == ReportType::Feature ? *_feature : *_output);
        (type == ReportType::Feature ? _feature : _output).reset();
        lock.unlock();
        const bool ok = _write && _write(type, report);
        lock.lock();
        ++(ok ? _written : _failed);
    }
}

/// endregion </SurfaceDialWriter>

}  // namespace controllers
