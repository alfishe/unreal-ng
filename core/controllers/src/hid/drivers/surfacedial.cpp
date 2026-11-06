#include "hid/drivers/surfacedial.h"

#include <algorithm>
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

std::vector<uint8_t> SurfaceDial::StopReport()
{
    constexpr uint8_t kStop = 2;
    return {kOutputReport, 1, kStop, 0, 0};
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

std::vector<uint8_t> SurfaceDial::FeatureBytes(const SurfaceDialFeature& feature)
{
    return {kFeatureReport,
            static_cast<uint8_t>(feature.stepsPerTurn & 0xFF),
            static_cast<uint8_t>(feature.stepsPerTurn >> 8),
            feature.repeatCount,
            feature.autoTrigger,
            feature.cutoff,
            static_cast<uint8_t>(feature.retriggerMs & 0xFF),
            static_cast<uint8_t>(feature.retriggerMs >> 8)};
}

/// region <SurfaceDialPlays>

namespace
{
constexpr uint8_t kBuzzWave = 4;
}  // namespace

uint32_t SurfaceDialPlays::PlayMs(const std::vector<uint8_t>& report) const
{
    if (report.size() < 5)
        return 0;
    const uint32_t count = report[1] > 0 ? report[1] : 1;
    const uint32_t period = static_cast<uint32_t>(report[3] | (report[4] << 8));
    return count * (report[2] == kBuzzWave ? _timing.buzzMs : std::max(_timing.clickMs, period));
}

uint32_t SurfaceDialPlays::Strength(const std::vector<uint8_t>& report)
{
    if (report.size() < 3)
        return 0;
    return (report[2] == kBuzzWave ? 1000u : 0u) + report[1];
}

void SurfaceDialPlays::Offer(std::vector<uint8_t> report)
{
    if (!_pending)
    {
        _pending = std::move(report);
        return;
    }
    // The hand outran the Dial: one of the two never plays
    ++_dropped;
    _overflow = true;
    if (Strength(report) >= Strength(*_pending))
        _pending = std::move(report);
}

std::vector<std::vector<uint8_t>> SurfaceDialPlays::Take(uint64_t nowMs)
{
    std::vector<std::vector<uint8_t>> reports;
    if (!Waiting() || nowMs < DueMs())
        return reports;
    if (!_pending)
    {
        // The buzz has played its length: the device would go on to its own cutoff
        reports.push_back(SurfaceDial::StopReport());
        _stopAtMs = 0;
        return reports;
    }
    if (_overflow || _stopAtMs != 0)
        reports.push_back(SurfaceDial::StopReport());
    reports.push_back(std::move(*_pending));
    const std::vector<uint8_t>& play = reports.back();
    _busyUntilMs = nowMs + PlayMs(play);
    _stopAtMs = play.size() > 2 && play[2] == kBuzzWave ? _busyUntilMs : 0;
    _pending.reset();
    _overflow = false;
    return reports;
}

/// endregion </SurfaceDialPlays>

/// region <SurfaceDialWriter>

SurfaceDialWriter::SurfaceDialWriter(Write write, Read read, SurfaceDialPlayTiming timing)
    : _write(std::move(write)), _read(std::move(read)), _plays(timing), _thread([this] { Run(); })
{
}

SurfaceDialWriter::~SurfaceDialWriter()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stopping = true;
        _steps.reset();
        _plays.Clear();
    }
    _wake.notify_all();
    _thread.join();
    // Only the found settings, never a guess: without a read nothing is restored
    if (_changed && _found && _write)
        _write(ReportType::Feature, SurfaceDial::FeatureBytes(*_found));
}

void SurfaceDialWriter::Steps(const StepsOut& steps)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _steps = steps;
    }
    _wake.notify_all();
}

void SurfaceDialWriter::Pattern(HapticPattern pattern)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _plays.Offer(SurfaceDial::PatternReport(pattern));
    }
    _wake.notify_all();
}

void SurfaceDialWriter::Clicks(const HapticClicks& clicks)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _plays.Offer(SurfaceDial::HapticReport(clicks));
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

uint64_t SurfaceDialWriter::Dropped() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _plays.Dropped();
}

std::optional<SurfaceDialFeature> SurfaceDialWriter::Found() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _found;
}

bool SurfaceDialWriter::WriteSteps(const StepsOut& steps)
{
    const std::vector<uint8_t> report = SurfaceDial::FeatureReport(steps, _base);
    if (!_write || !_write(ReportType::Feature, report))
        return false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _changed = true;
    }
    if (!_read)
        return true;
    // Read back: a write the device accepted but did not apply is a failure (measured: fields outside the logical
    // range are dropped silently, POC 030)
    std::vector<uint8_t> back;
    SurfaceDialFeature applied;
    if (!_read(back) || !SurfaceDial::ParseFeature(back.data(), back.size(), applied))
        return false;
    _base = applied;
    SurfaceDialFeature wanted;
    SurfaceDial::ParseFeature(report.data(), report.size(), wanted);
    return applied.stepsPerTurn == wanted.stepsPerTurn && applied.repeatCount == wanted.repeatCount &&
           applied.autoTrigger == wanted.autoTrigger;
}

void SurfaceDialWriter::Run()
{
#if defined(__APPLE__)
    pthread_setname_np("dial-writer");
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), "dial-writer");
#endif
    // The settings as found, before anything is written: the steps keep its cutoff and retrigger, the end restores it
    std::vector<uint8_t> found;
    SurfaceDialFeature parsed;
    if (_read && _read(found) && SurfaceDial::ParseFeature(found.data(), found.size(), parsed))
    {
        _base = parsed;
        std::lock_guard<std::mutex> lock(_mutex);
        _found = parsed;
    }
    std::unique_lock<std::mutex> lock(_mutex);
    while (true)
    {
        _wake.wait(lock, [this] { return _stopping || _steps || _plays.Waiting(); });
        if (_stopping)
            return;
        // The steps first: a pattern after a context change plays in the new feel
        if (_steps)
        {
            const StepsOut steps = *_steps;
            _steps.reset();
            lock.unlock();
            const bool ok = WriteSteps(steps);
            lock.lock();
            ++(ok ? _written : _failed);
            continue;
        }
        // A play waits until the last one has played, the stop until the buzz has (SurfaceDialPlays); new steps or
        // the end do not
        const uint64_t nowMs = DialNowMs();
        if (nowMs < _plays.DueMs())
        {
            _wake.wait_for(lock, std::chrono::milliseconds(_plays.DueMs() - nowMs),
                           [this] { return _stopping || _steps.has_value(); });
            continue;
        }
        const std::vector<std::vector<uint8_t>> reports = _plays.Take(nowMs);
        lock.unlock();
        uint64_t written = 0;
        for (const std::vector<uint8_t>& report : reports)
            written += _write && _write(ReportType::Output, report) ? 1 : 0;
        lock.lock();
        _written += written;
        _failed += reports.size() - written;
    }
}

/// endregion </SurfaceDialWriter>

}  // namespace controllers
