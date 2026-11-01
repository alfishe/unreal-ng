#pragma once
/// @file devicehistory.h
/// @brief The history of one peripheral's own state machine (docs/inprogress/2026-10-31-debugger-device-io): the
/// commands of a WD1793, the register writes of an AY or an OPL, the host traffic of a GS. One pattern for every
/// device:
///
/// | Mode    | Cost on the device's path                    | Keeps                                            |
/// |---------|----------------------------------------------|--------------------------------------------------|
/// | off     | one relaxed load and a not-taken branch      | nothing, no memory                               |
/// | ring    | a copy into a preallocated ring              | the last `limit` records (the standby mode)      |
/// | capture | a copy into a growing buffer                 | every record up to `limit` (0: no limit), then it counts the dropped ones |
///
/// and, with any mode but off, an optional stream: every record is also written to a file (JSON Lines, one record
/// per line) by a writer thread of its own, up to `streamLimit` records (0: no limit). The device's path only copies
/// the record into the writer's queue; rendering and file I/O happen on the writer thread. Stats keep counting what
/// the stream wrote until the next configuration.
///
/// A device records with `if (history.On()) [[unlikely]] history.Push(record);` and may complete its newest record
/// later (a command's status at its end) through UpdateLast. A record goes to the stream when the next one is
/// pushed (or the history is reconfigured, cleared or destroyed), so a completed record is streamed completed.
///
/// `Record` is a copyable value type with `void ToState(StateNode& node) const` (its fields for every automation
/// plane and for the stream).
///
/// Threads: the device pushes on the emulation thread; configuration, reads and clears come from the automation
/// threads. A mutex guards the buffers; it is only taken when the history is on.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "emulator/state/statenode.h"
#include "emulator/state/statenodejson.h"

namespace history
{

enum class Mode : uint8_t
{
    Off,
    Ring,
    Capture,
};

const char* ModeName(Mode mode);
/// "off", "ring", "capture"; false for anything else
bool ParseMode(const std::string& text, Mode& mode);

struct Config
{
    Mode mode = Mode::Off;
    size_t limit = 0;           ///< ring: its size (0: the device's default); capture: the cap (0: no cap)
    std::string stream;         ///< a file every record is written to as well (JSON Lines); "": none
    uint64_t streamLimit = 0;   ///< records to the file at most (0: no limit)
};

struct Stats
{
    Config config;
    uint64_t written = 0;       ///< records pushed since the last clear
    uint64_t kept = 0;          ///< records in memory now
    uint64_t dropped = 0;       ///< records not kept: overwritten in the ring, over the capture cap
    uint64_t streamed = 0;      ///< records written to the stream file
    std::string streamError;    ///< why the stream stopped ("" while it runs or when there is none)
    bool paused = false;        ///< held: nothing recorded, the records kept
};

/// What the automation planes see of any device's history
class IDeviceHistory
{
public:
    virtual ~IDeviceHistory() = default;

    /// The registry id of the device ("beta128", "zxbus.1") and what its records are ("FDC commands")
    virtual std::string Device() const = 0;
    virtual const std::string& What() const = 0;

    /// Applies a configuration: off drops the records; a new ring size or mode starts empty. False with `error`
    /// for a stream file that cannot be opened (the history keeps its previous configuration)
    virtual bool Configure(const Config& config, std::string& error) = 0;
    virtual void Clear() = 0;
    /// Holds the history: nothing is recorded, what it has stays readable (and streamed); resume goes on with it
    virtual void Pause(bool paused) = 0;
    virtual Stats GetStats() const = 0;

    /// The newest `last` records (0: all), oldest first, each as its ToState
    virtual StateNode Records(size_t last) const = 0;
};

/// The stats of a history as the planes report them
StateNode StatsNode(const IDeviceHistory& history);

/// The histories of one machine (EmulatorContext::histories): its devices register theirs while they live
class Registry
{
public:
    void Add(IDeviceHistory* history);
    void Remove(IDeviceHistory* history);

    /// Runs `f(IDeviceHistory&)` on the history of a device; false when it has none. Under the registry's lock: a
    /// device cannot go away meanwhile
    template <typename F>
    bool With(const std::string& device, F&& f) const
    {
        std::lock_guard lock(_mutex);
        for (IDeviceHistory* history : _histories)
        {
            if (history->Device() == device)
            {
                f(*history);
                return true;
            }
        }
        return false;
    }

    /// Every history: device, what, stats
    StateNode List() const;

private:
    mutable std::mutex _mutex;
    std::vector<IDeviceHistory*> _histories;
};

namespace detail
{

/// Opens a stream file; nullptr with `error`
std::FILE* OpenStream(const std::string& path, std::string& error);

/// The stream file of a history: the device's path queues copies of its records, a writer thread renders them
/// (ToState, compact JSON) and writes one line each
template <typename Record>
class StreamFile
{
public:
    ~StreamFile()
    {
        Close();
    }

    bool Open(const std::string& path, uint64_t limit, std::string& error)
    {
        Close();
        _file = OpenStream(path, error);
        if (_file == nullptr)
        {
            return false;
        }
        _limit = limit;
        _written.store(0, std::memory_order_relaxed);
        _stop = false;
        _error.clear();
        _writer = std::thread([this] { Run(); });
        return true;
    }

    /// Writes what is queued, then closes the file
    void Close()
    {
        if (_file == nullptr)
        {
            return;
        }
        {
            std::lock_guard lock(_mutex);
            _stop = true;
        }
        _wake.notify_one();
        _writer.join();
        std::fclose(_file);
        _file = nullptr;
    }

    bool IsOpen() const
    {
        return _file != nullptr;
    }

    void Enqueue(const Record& record)
    {
        std::lock_guard lock(_mutex);
        _pending.push_back(record);
        if (_pending.size() >= kWakeAt)
        {
            _wake.notify_one();
        }
    }

    uint64_t Written() const
    {
        return _written.load(std::memory_order_relaxed);
    }
    std::string Error() const
    {
        std::lock_guard lock(_mutex);
        return _error;
    }

private:
    static constexpr size_t kWakeAt = 256;

    void Run()
    {
        std::vector<Record> batch;
        bool stop = false;
        while (!stop)
        {
            {
                std::unique_lock lock(_mutex);
                // Wakes for a full batch, a stop, or every 100 ms for what is queued
                _wake.wait_for(lock, std::chrono::milliseconds(100),
                               [this] { return _stop || _pending.size() >= kWakeAt; });
                batch.swap(_pending);
                stop = _stop;
            }
            for (const Record& record : batch)
            {
                if (_limit != 0 && _written.load(std::memory_order_relaxed) >= _limit)
                {
                    break;
                }
                StateNode node = StateNode::Object();
                record.ToState(node);
                const std::string line = StateNodeToJsonText(node) + "\n";
                if (std::fwrite(line.data(), 1, line.size(), _file) != line.size())
                {
                    std::lock_guard lock(_mutex);
                    _error = "write failed";
                    break;
                }
                _written.fetch_add(1, std::memory_order_relaxed);
            }
            batch.clear();
        }
        std::fflush(_file);
    }

    std::FILE* _file = nullptr;
    uint64_t _limit = 0;
    std::thread _writer;
    mutable std::mutex _mutex;
    std::condition_variable _wake;
    std::vector<Record> _pending;
    bool _stop = false;
    std::atomic<uint64_t> _written{ 0 };
    std::string _error;
};

}  // namespace detail

template <typename Record>
class DeviceHistory final : public IDeviceHistory
{
public:
    /// `defaultRing`: the ring size of `ring` without a limit
    DeviceHistory(std::string device, std::string what, size_t defaultRing = 256)
        : _device(std::move(device)), _what(std::move(what)), _defaultRing(defaultRing ? defaultRing : 1)
    {
    }
    /// Named by the registry when asked: a history owned above the device (the AY on whatever takes the AY ports,
    /// the GS on whichever personality fills the slot) follows the slot set. Called on the control path only
    DeviceHistory(std::function<std::string()> device, std::string what, size_t defaultRing = 256)
        : _resolve(std::move(device)), _what(std::move(what)), _defaultRing(defaultRing ? defaultRing : 1)
    {
    }
    ~DeviceHistory() override
    {
        std::lock_guard lock(_mutex);
        FlushOpen();
        _stream.Close();
    }

    /// The whole cost of an off history on the device's path
    bool On() const
    {
        return _on.load(std::memory_order_relaxed);
    }

    /// region <The device's path (emulation thread)>

    void Push(const Record& record)
    {
        std::lock_guard lock(_mutex);
        if (_config.mode == Mode::Off)
        {
            return;
        }
        FlushOpen();
        _written++;
        if (_config.mode == Mode::Ring)
        {
            if (_records.size() < _capacity)
            {
                _records.push_back(record);
            }
            else
            {
                _records[_head] = record;
                _head = (_head + 1) % _capacity;
                _dropped++;
            }
            _hasOpen = true;
        }
        else if (_config.limit == 0 || _records.size() < _config.limit)
        {
            _records.push_back(record);
            _hasOpen = true;
        }
        else
        {
            _dropped++;
            _overflow = record;     // not kept, still streamed
            _hasOverflow = true;
        }
    }

    /// Completes the newest record (a command's end): `f(Record&)`; nothing when there is none
    template <typename F>
    void UpdateLast(F&& f)
    {
        std::lock_guard lock(_mutex);
        if (Record* last = Newest())
        {
            f(*last);
        }
    }

    /// endregion </The device's path (emulation thread)>

    /// region <IDeviceHistory>

    std::string Device() const override
    {
        return _resolve ? _resolve() : _device;
    }
    const std::string& What() const override
    {
        return _what;
    }

    bool Configure(const Config& config, std::string& error) override
    {
        std::lock_guard lock(_mutex);
        FlushOpen();
        _stream.Close();
        if (config.mode != Mode::Off && !config.stream.empty() && !_stream.Open(config.stream, config.streamLimit, error))
        {
            // Keep the previous configuration, without its stream
            _config.stream.clear();
            return false;
        }
        _config = config;
        if (_config.mode == Mode::Off)
        {
            _config.stream.clear();
        }
        _capacity = _config.mode == Mode::Ring ? (_config.limit ? _config.limit : _defaultRing) : 0;
        ResetLocked();
        if (_config.mode == Mode::Ring)
        {
            _records.reserve(_capacity);
        }
        _paused = false;
        _on.store(_config.mode != Mode::Off, std::memory_order_relaxed);
        return true;
    }

    void Pause(bool paused) override
    {
        std::lock_guard lock(_mutex);
        _paused = paused;
        _on.store(_config.mode != Mode::Off && !_paused, std::memory_order_relaxed);
    }

    void Clear() override
    {
        std::lock_guard lock(_mutex);
        FlushOpen();
        ResetLocked();
    }

    Stats GetStats() const override
    {
        std::lock_guard lock(_mutex);
        Stats stats;
        stats.config = _config;
        if (_config.mode == Mode::Ring && _config.limit == 0)
        {
            stats.config.limit = _capacity;
        }
        stats.written = _written;
        stats.kept = _records.size();
        stats.dropped = _dropped;
        stats.streamed = _stream.Written();
        stats.streamError = _stream.Error();
        stats.paused = _paused;
        return stats;
    }

    StateNode Records(size_t last) const override
    {
        std::vector<Record> copy = Snapshot(last);
        StateNode list = StateNode::Array();
        for (const Record& record : copy)
        {
            StateNode node = StateNode::Object();
            record.ToState(node);
            list.push(std::move(node));
        }
        return list;
    }

    /// endregion </IDeviceHistory>

    /// The newest `last` records (0: all), oldest first
    std::vector<Record> Snapshot(size_t last = 0) const
    {
        std::lock_guard lock(_mutex);
        const size_t count = last == 0 || last > _records.size() ? _records.size() : last;
        std::vector<Record> copy;
        copy.reserve(count);
        const size_t size = _records.size();
        // The ring's oldest is at _head once it wrapped (then size == capacity); a capture's at 0 (_head stays 0)
        for (size_t i = size - count; i < size; i++)
        {
            copy.push_back(_records[(_head + i) % size]);
        }
        return copy;
    }

private:
    Record* Newest()
    {
        if (_records.empty())
        {
            return nullptr;
        }
        if (_hasOverflow)
        {
            return &_overflow;
        }
        return &_records[(_head + _records.size() - 1) % _records.size()];
    }

    /// The newest record goes to the stream once nothing more can change it
    void FlushOpen()
    {
        if (!_stream.IsOpen())
        {
            _hasOpen = _hasOverflow = false;
            return;
        }
        const Record* open = _hasOverflow ? &_overflow : (_hasOpen ? Newest() : nullptr);
        if (open != nullptr)
        {
            _stream.Enqueue(*open);
        }
        _hasOpen = _hasOverflow = false;
    }

    void ResetLocked()
    {
        _records.clear();
        _head = 0;
        _written = 0;
        _dropped = 0;
        _hasOpen = _hasOverflow = false;
    }

    const std::string _device;
    const std::function<std::string()> _resolve;
    const std::string _what;
    const size_t _defaultRing;

    std::atomic<bool> _on{ false };
    mutable std::mutex _mutex;
    Config _config;
    size_t _capacity = 0;
    std::vector<Record> _records;
    size_t _head = 0;               ///< the ring's oldest record once it wrapped
    uint64_t _written = 0;
    uint64_t _dropped = 0;
    bool _paused = false;
    bool _hasOpen = false;          ///< the newest kept record is not streamed yet
    Record _overflow{};             ///< a capture's newest record over its cap: streamed, not kept
    bool _hasOverflow = false;
    detail::StreamFile<Record> _stream;
};

}  // namespace history
