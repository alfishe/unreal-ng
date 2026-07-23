#include "porttracker.h"

#include <algorithm>
#include <sstream>
#include <iomanip>

#include "emulator/emulatorcontext.h"

// Static empty maps for const-ref returns
const std::unordered_map<uint16_t, uint32_t> PortTracker::_emptyCallerMap;
const std::unordered_map<uint8_t, uint32_t> PortTracker::_emptyValueMap;

PortTracker::PortTracker(EmulatorContext* context)
    : _context(context)
{
}

PortTracker::~PortTracker()
{
}

// === Event Recording ===

void PortTracker::TrackRead(uint16_t port, uint8_t value, uint16_t callerPC)
{
    if (_sessionState != ProfilerSessionState::Capturing)
        return;

    PortData& data = _ports[port];
    data.readCount++;
    data.readCallers[callerPC]++;
    data.readValues[value]++;
}

void PortTracker::TrackWrite(uint16_t port, uint8_t value, uint16_t callerPC)
{
    if (_sessionState != ProfilerSessionState::Capturing)
        return;

    PortData& data = _ports[port];
    data.writeCount++;
    data.writeCallers[callerPC]++;
    data.writeValues[value]++;
}

// === Query API ===

uint32_t PortTracker::GetWriteCount(uint16_t port) const
{
    auto it = _ports.find(port);
    if (it == _ports.end())
        return 0;
    return it->second.writeCount;
}

uint32_t PortTracker::GetReadCount(uint16_t port) const
{
    auto it = _ports.find(port);
    if (it == _ports.end())
        return 0;
    return it->second.readCount;
}

const std::unordered_map<uint16_t, uint32_t>& PortTracker::GetWriteCallers(uint16_t port) const
{
    auto it = _ports.find(port);
    if (it == _ports.end())
        return _emptyCallerMap;
    return it->second.writeCallers;
}

const std::unordered_map<uint16_t, uint32_t>& PortTracker::GetReadCallers(uint16_t port) const
{
    auto it = _ports.find(port);
    if (it == _ports.end())
        return _emptyCallerMap;
    return it->second.readCallers;
}

std::vector<uint16_t> PortTracker::GetActivePorts() const
{
    std::vector<uint16_t> result;
    result.reserve(_ports.size());
    for (const auto& [port, data] : _ports)
    {
        result.push_back(port);
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool PortTracker::HasActivity(uint16_t port) const
{
    return _ports.find(port) != _ports.end();
}

std::vector<PortTracker::PortSummary> PortTracker::GetPortSummaries() const
{
    std::vector<PortSummary> result;
    result.reserve(_ports.size());

    for (const auto& [port, data] : _ports)
    {
        PortSummary summary;
        summary.port = port;
        summary.readCount = data.readCount;
        summary.writeCount = data.writeCount;
        summary.uniqueReadCallers = static_cast<uint32_t>(data.readCallers.size());
        summary.uniqueWriteCallers = static_cast<uint32_t>(data.writeCallers.size());
        result.push_back(summary);
    }

    std::sort(result.begin(), result.end(),
              [](const PortSummary& a, const PortSummary& b) { return a.port < b.port; });

    return result;
}

PortTracker::PortSummary PortTracker::GetPortSummary(uint16_t port) const
{
    PortSummary summary{};
    summary.port = port;

    auto it = _ports.find(port);
    if (it != _ports.end())
    {
        const PortData& data = it->second;
        summary.readCount = data.readCount;
        summary.writeCount = data.writeCount;
        summary.uniqueReadCallers = static_cast<uint32_t>(data.readCallers.size());
        summary.uniqueWriteCallers = static_cast<uint32_t>(data.writeCallers.size());
    }

    return summary;
}

size_t PortTracker::GetActivePortCount() const
{
    return _ports.size();
}

// === Data Value Tracking ===

const std::unordered_map<uint8_t, uint32_t>& PortTracker::GetWriteValues(uint16_t port) const
{
    auto it = _ports.find(port);
    if (it == _ports.end())
        return _emptyValueMap;
    return it->second.writeValues;
}

const std::unordered_map<uint8_t, uint32_t>& PortTracker::GetReadValues(uint16_t port) const
{
    auto it = _ports.find(port);
    if (it == _ports.end())
        return _emptyValueMap;
    return it->second.readValues;
}

// === Lifecycle ===

void PortTracker::Reset()
{
    _ports.clear();
}

bool PortTracker::IsActive() const
{
    return _sessionState == ProfilerSessionState::Capturing;
}

// === Session Control ===

void PortTracker::StartSession()
{
    Reset();
    _sessionState = ProfilerSessionState::Capturing;
}

void PortTracker::PauseSession()
{
    if (_sessionState == ProfilerSessionState::Capturing)
        _sessionState = ProfilerSessionState::Paused;
}

void PortTracker::ResumeSession()
{
    if (_sessionState == ProfilerSessionState::Paused)
        _sessionState = ProfilerSessionState::Capturing;
}

void PortTracker::StopSession()
{
    _sessionState = ProfilerSessionState::Stopped;
}

ProfilerSessionState PortTracker::GetSessionState() const
{
    return _sessionState;
}

// === Reporting ===

std::string PortTracker::GenerateReport() const
{
    std::ostringstream oss;
    oss << "=== Port Activity Report ===\n\n";

    auto summaries = GetPortSummaries();
    if (summaries.empty())
    {
        oss << "No port activity recorded.\n";
        return oss.str();
    }

    oss << std::left << std::setw(8) << "PORT"
        << std::right << std::setw(12) << "READS"
        << std::setw(12) << "WRITES"
        << std::setw(12) << "R-CALLERS"
        << std::setw(12) << "W-CALLERS"
        << "\n";
    oss << std::string(56, '-') << "\n";

    for (const auto& s : summaries)
    {
        oss << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << s.port
            << std::dec << std::setfill(' ')
            << std::right << std::setw(12) << s.readCount
            << std::setw(12) << s.writeCount
            << std::setw(12) << s.uniqueReadCallers
            << std::setw(12) << s.uniqueWriteCallers
            << "\n";
    }

    oss << "\nTotal active ports: " << summaries.size() << "\n";

    return oss.str();
}
