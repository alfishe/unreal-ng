#pragma once

/// @file padprotocol.h
/// @brief The output side of a pad that talks a protocol over HID (haptics.md §9, H4): the DualSense / DualShock 4
/// (SonyProtocol) and the Switch Pro controller (SwitchProtocol). A protocol is pure: it is told the time, what came
/// in and what the router wants (rumble, light, player), and answers which report to write next and when to ask
/// again, so its tests run on byte fixtures and a fake clock on every OS. PadPump runs one on its own thread for a
/// backend: IBackend::SetRumble / SetLight only queue (they never block), the writes and the feature reads (a
/// Bluetooth round trip each) happen there.

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "hostbackend.h"

namespace controllers
{

/// One thing to do on the device
struct PadOutReport
{
    enum class Kind : uint8_t
    {
        Output,      ///< write @p bytes as an output report (bytes[0] the report id)
        GetFeature,  ///< read feature report @p featureId, @p featureSize bytes with the id
        SetFeature,  ///< write @p bytes as a feature report (bytes[0] the report id, 0 for a device without ids)
    };
    Kind kind = Kind::Output;
    std::vector<uint8_t> bytes;
    uint8_t featureId = 0;
    size_t featureSize = 0;
};

class IPadProtocol
{
public:
    virtual ~IPadProtocol() = default;

    /// The next report at @p nowMs (steady milliseconds), or nullopt with @p wakeMs the time to ask again (0: only
    /// after the next call of the setters or OnInput)
    virtual std::optional<PadOutReport> Next(uint64_t nowMs, uint64_t& wakeMs) = 0;
    /// The report Next gave was written (false: refused)
    virtual void OnWritten(bool ok, uint64_t nowMs)
    {
        (void)ok;
        (void)nowMs;
    }
    /// The answer to a GetFeature; empty when the read failed
    virtual void OnFeature(uint8_t featureId, const std::vector<uint8_t>& data, uint64_t nowMs)
    {
        (void)featureId;
        (void)data;
        (void)nowMs;
    }
    /// An input report of the device (subcommand replies, acknowledgements)
    virtual void OnInput(const uint8_t* report, size_t size, uint64_t nowMs)
    {
        (void)report;
        (void)size;
        (void)nowMs;
    }

    virtual void SetRumble(const RumbleLevels& levels) = 0;
    /// False when the device has no light
    virtual bool SetLight(const LightColor& color)
    {
        (void)color;
        return false;
    }
    /// The player number its LEDs show (1..), 0 none
    virtual void SetPlayer(int player)
    {
        (void)player;
    }
    /// What stops the motors when the device goes or the backend stops; nullopt when they are not running
    virtual std::optional<PadOutReport> Final() = 0;
};

/// Runs a protocol on its own thread (one per device)
class PadPump
{
public:
    using Write = std::function<bool(const std::vector<uint8_t>& report)>;
    /// Read feature report @p id into @p data (sized @p size, data[0] = id); false when it failed
    using GetFeature = std::function<bool(uint8_t id, size_t size, std::vector<uint8_t>& data)>;
    using Clock = std::function<uint64_t()>;

    /// @p clock: steady milliseconds (the default the steady clock); @p setFeature writes a SetFeature report (as
    /// @p write: bytes[0] the report id)
    PadPump(std::unique_ptr<IPadProtocol> protocol, Write write, GetFeature getFeature, Clock clock = {},
            Write setFeature = {});
    /// What is queued is dropped, a write in flight finishes; then the protocol's Final is written
    ~PadPump();
    PadPump(const PadPump&) = delete;
    PadPump& operator=(const PadPump&) = delete;

    void SetRumble(const RumbleLevels& levels);
    bool SetLight(const LightColor& color);
    void SetPlayer(int player);
    /// An input report, from the backend's reading thread: the protocol sees it at once, on that thread
    void Input(const uint8_t* report, size_t size);

    /// Writes done / refused, features read (the tests, the live check)
    uint64_t Written() const;
    uint64_t Failed() const;

private:
    void Run();
    uint64_t Now() const;

    std::unique_ptr<IPadProtocol> _protocol;  ///< under _mutex
    Write _write;
    GetFeature _getFeature;
    Clock _clock;
    Write _setFeature;
    mutable std::mutex _mutex;
    std::condition_variable _wake;
    uint64_t _written = 0;
    uint64_t _failed = 0;
    bool _stopping = false;
    std::thread _thread;  ///< last: started when the rest exists
};

}  // namespace controllers
