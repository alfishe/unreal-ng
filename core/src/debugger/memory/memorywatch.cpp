#include "memorywatch.h"

#include <algorithm>
#include <cctype>
#include <functional>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccesstracker.h"

namespace MemoryWatch
{
namespace
{
constexpr uint32_t kTimeoutMs = 500;
constexpr uint32_t kMaxKeys = 65536;

MonitoringOptions ToTracker(const Options& options)
{
    MonitoringOptions result;
    result.trackCallers = options.callers;
    result.trackDataFlow = options.values;
    result.maxCallers = options.maxCallers;
    result.maxDataValues = options.maxValues;
    return result;
}

Options FromTracker(const MonitoringOptions& options)
{
    Options result;
    result.callers = options.trackCallers;
    result.values = options.trackDataFlow;
    result.maxCallers = options.maxCallers;
    result.maxValues = options.maxDataValues;
    return result;
}

void CopyStats(const AccessStats& stats, Watch& watch)
{
    watch.reads = stats.readCount;
    watch.writes = stats.writeCount;
    watch.executes = stats.executeCount;
    watch.callers.insert(stats.callerAddresses.begin(), stats.callerAddresses.end());
    watch.values.insert(stats.dataValues.begin(), stats.dataValues.end());
}

std::vector<Watch> Snapshot(const MemoryAccessTracker& tracker)
{
    std::vector<Watch> watches;
    for (const MonitoredRegion& region : tracker.GetMonitoredRegions())
    {
        Watch watch;
        watch.name = region.name;
        watch.start = region.startAddress;
        watch.length = region.length;
        watch.options = FromTracker(region.options);
        CopyStats(region.stats, watch);
        watches.push_back(std::move(watch));
    }
    for (const MonitoredPort& port : tracker.GetMonitoredPorts())
    {
        Watch watch;
        watch.name = port.name;
        watch.port = true;
        watch.start = port.port;
        watch.mask = port.mask;
        watch.options = FromTracker(port.options);
        CopyStats(port.stats, watch);
        watches.push_back(std::move(watch));
    }
    std::sort(watches.begin(), watches.end(), [](const Watch& a, const Watch& b) { return a.name < b.name; });
    return watches;
}

bool Taken(const MemoryAccessTracker& tracker, const std::string& name)
{
    for (const MonitoredRegion& region : tracker.GetMonitoredRegions())
        if (region.name == name)
            return true;
    for (const MonitoredPort& port : tracker.GetMonitoredPorts())
        if (port.name == name)
            return true;
    return false;
}

/// Runs `work` on the tracker at a coherent moment; `work` sets the error when it refuses
Result Run(Emulator* emulator, const std::function<void(MemoryAccessTracker&, Result&)>& work)
{
    Result result;
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!context || !context->pMemory)
    {
        result.error = "emulator not available";
        return result;
    }
    MemoryAccessTracker& tracker = context->pMemory->GetAccessTracker();
    const Emulator::CoherentMoment where = emulator->RunAtCoherentMoment(
        [&]() {
            work(tracker, result);
            tracker.PublishWatches();   // a paused machine ends no frame: the topic sees the change now
        },
        kTimeoutMs);
    if (where == Emulator::CoherentMoment::Busy)
    {
        result.busy = true;
        result.error = "no coherent moment within 500 ms (the emulator is stepping or changing state); try again";
        return result;
    }
    result.ok = result.error.empty();
    result.moment = Emulator::CoherentMomentName(where);
    return result;
}

bool ValidOptions(const Options& options, std::string& error)
{
    if (options.maxCallers == 0 || options.maxCallers > kMaxKeys || options.maxValues == 0 || options.maxValues > 256)
    {
        error = "max_callers must be 1 to 65536, max_values 1 to 256";
        return false;
    }
    return true;
}
}  // namespace

bool ValidName(const std::string& name, std::string& error)
{
    const bool ok = !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](unsigned char c) {
                        return std::isalnum(c) || c == '_' || c == '-' || c == '.';
                    });
    if (!ok)
        error = "a watch name is 1 to 64 letters, digits, '_', '-' or '.'; got '" + name + "'";
    return ok;
}

Result AddRegion(Emulator* emulator, const std::string& name, uint32_t start, uint32_t length, const Options& options)
{
    Result result;
    if (!ValidName(name, result.error) || !ValidOptions(options, result.error))
        return result;
    if (start > 0xFFFF || length == 0 || start + length > 0x10000)
    {
        result.error = "a region is CPU addresses: start 0..#FFFF, length 1.., start + length <= #10000";
        return result;
    }
    return Run(emulator, [&](MemoryAccessTracker& tracker, Result& out) {
        if (Taken(tracker, name))
        {
            out.error = "a watch named '" + name + "' exists";
            return;
        }
        tracker.AddMonitoredRegion(name, static_cast<uint16_t>(start), length, ToTracker(options));
        for (Watch& watch : Snapshot(tracker))
            if (watch.name == name)
                out.watches.push_back(std::move(watch));
    });
}

Result AddPort(Emulator* emulator, const std::string& name, uint32_t port, uint32_t mask, const Options& options)
{
    Result result;
    if (!ValidName(name, result.error) || !ValidOptions(options, result.error))
        return result;
    if (port > 0xFFFF || mask > 0xFFFF || mask == 0)
    {
        result.error = "port and mask are 0..#FFFF, the mask not 0";
        return result;
    }
    return Run(emulator, [&](MemoryAccessTracker& tracker, Result& out) {
        if (Taken(tracker, name))
        {
            out.error = "a watch named '" + name + "' exists";
            return;
        }
        tracker.AddMonitoredPort(name, static_cast<uint16_t>(port), ToTracker(options), static_cast<uint16_t>(mask));
        for (Watch& watch : Snapshot(tracker))
            if (watch.name == name)
                out.watches.push_back(std::move(watch));
    });
}

Result Remove(Emulator* emulator, const std::string& name)
{
    return Run(emulator, [&](MemoryAccessTracker& tracker, Result& out) {
        if (!tracker.RemoveMonitoredRegion(name) && !tracker.RemoveMonitoredPort(name))
            out.error = "no watch named '" + name + "'";
    });
}

Result Clear(Emulator* emulator)
{
    return Run(emulator, [](MemoryAccessTracker& tracker, Result&) { tracker.ClearMonitored(); });
}

Result List(Emulator* emulator)
{
    return Run(emulator, [](MemoryAccessTracker& tracker, Result& out) { out.watches = Snapshot(tracker); });
}
}  // namespace MemoryWatch
