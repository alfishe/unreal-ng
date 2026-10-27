#include "hid/drivers/xboxbluetoothoutput.h"

#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#endif

#include <utility>

namespace controllers
{

namespace
{
constexpr uint16_t kMicrosoft = 0x045E;
constexpr uint8_t kEnableAll = 0x0F;  // left trigger, right trigger, low motor, high motor
constexpr uint8_t kDuration = 0xFF;   // 10 ms units
constexpr uint8_t kDelay = 0x00;
constexpr uint8_t kRepeat = 0xEB;

uint8_t Percent(float level)
{
    return static_cast<uint8_t>((level < 0 ? 0 : level > 1 ? 1 : level) * 100.0f + 0.5f);
}
}  // namespace

bool XboxBluetoothOutput::Handles(bool bluetooth, uint16_t vendor, const std::string& layout,
                                  const std::string& family)
{
    return bluetooth && vendor == kMicrosoft && layout == "xbox" && family != "xbox-360";
}

std::vector<uint8_t> XboxBluetoothOutput::RumbleReport(const RumbleLevels& levels)
{
    return {kRumbleReport,
            kEnableAll,
            Percent(levels.leftTrigger),
            Percent(levels.rightTrigger),
            Percent(levels.low),
            Percent(levels.high),
            kDuration,
            kDelay,
            kRepeat};
}

/// region <RumbleWriter>

RumbleWriter::RumbleWriter(Write write, std::chrono::milliseconds interval)
    : _write(std::move(write)), _interval(interval), _thread([this] { Run(); })
{
}

RumbleWriter::~RumbleWriter()
{
    bool stop = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stopping = true;
        _pending.reset();
    }
    _wake.notify_all();
    _thread.join();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        stop = !_last || *_last != RumbleLevels{};
    }
    // A device already gone refuses it at once
    if (stop && _write)
        _write(RumbleLevels{});
}

void RumbleWriter::Set(const RumbleLevels& levels)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _pending = levels;
    }
    _wake.notify_all();
}

uint64_t RumbleWriter::Written() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _written;
}

uint64_t RumbleWriter::Failed() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _failed;
}

void RumbleWriter::Run()
{
#if defined(__APPLE__)
    pthread_setname_np("rumble-writer");
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), "rumble-writer");
#endif
    using Clock = std::chrono::steady_clock;
    std::optional<Clock::time_point> previous;
    std::unique_lock<std::mutex> lock(_mutex);
    while (true)
    {
        _wake.wait(lock, [this] { return _stopping || _pending; });
        if (_stopping)
            return;
        // At most one write per interval: what arrives meanwhile replaces the pending levels
        if (previous && Clock::now() < *previous + _interval)
        {
            if (_wake.wait_until(lock, *previous + _interval, [this] { return _stopping; }))
                return;
            continue;
        }
        if (_last && *_pending == *_last)
        {
            _pending.reset();
            continue;
        }
        const RumbleLevels levels = *_pending;
        _pending.reset();
        lock.unlock();
        const bool ok = _write && _write(levels);
        previous = Clock::now();
        lock.lock();
        _last = ok ? std::optional<RumbleLevels>(levels) : std::nullopt;
        ++(ok ? _written : _failed);
    }
}

/// endregion </RumbleWriter>

}  // namespace controllers
