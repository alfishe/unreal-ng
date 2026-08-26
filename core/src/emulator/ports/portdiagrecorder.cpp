#include "stdafx.h"

#include "portdiagrecorder.h"

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <sstream>

// Binary export writes raw PortTraceEvent structs; pin the layout the Python
// converter (tools/porttrace/porttrace_convert.py, struct "<QIHHHBBBBxx") depends on
static_assert(sizeof(PortTraceEvent) == 24, "PortTraceEvent must stay 24 bytes (binary trace format v1)");
static_assert(offsetof(PortTraceEvent, timestamp) == 0 && offsetof(PortTraceEvent, frameNumber) == 8 &&
                  offsetof(PortTraceEvent, rawPort) == 12 && offsetof(PortTraceEvent, decodedPort) == 14 &&
                  offsetof(PortTraceEvent, pc) == 16 && offsetof(PortTraceEvent, value) == 18 &&
                  offsetof(PortTraceEvent, decodeRuleIndex) == 19 && offsetof(PortTraceEvent, deviceId) == 20 &&
                  offsetof(PortTraceEvent, flags) == 21,
              "PortTraceEvent field layout is part of the binary trace format v1");

/// region <Filter matching>

bool PortTraceFilterRule::matches(const PortTraceEvent& event) const
{
    if (decodedPort && event.decodedPort != *decodedPort)
        return false;
    if (rawPort && event.rawPort != *rawPort)
        return false;
    if (device && event.deviceId != *device)
        return false;
    if (directionOut && event.isOut() != *directionOut)
        return false;
    if (pcRange && (event.pc < pcRange->first || event.pc > pcRange->second))
        return false;
    if (valueRange && (event.value < valueRange->first || event.value > valueRange->second))
        return false;
    if (unmappedOnly && event.decodedPort != 0x0000)
        return false;

    return true;
}

bool PortTraceFilterSet::matches(const PortTraceEvent& event) const
{
    // Include pass: empty include list = include everything;
    // otherwise at least one compound rule must match
    if (!include.empty())
    {
        bool anyMatch = false;
        for (const auto& rule : include)
        {
            if (rule.matches(event))
            {
                anyMatch = true;
                break;
            }
        }

        if (!anyMatch)
            return false;
    }

    // Exclude pass: any match rejects (exclude always wins)
    for (const auto& rule : exclude)
    {
        if (rule.matches(event))
            return false;
    }

    return true;
}

/// endregion </Filter matching>

/// region <PortActivitySummary>

void PortActivitySummary::onEvent(uint32_t frame, bool isOut, const PortDecodeDisposition& disp)
{
    if (frame != frameNumber)
    {
        reset(frame);
    }

    if (isOut)
    {
        outCount++;
        if (disp.decodedPort == 0x0000 && !disp.wasBeta128Gated)
            unmappedOutCount++;
    }
    else
    {
        inCount++;
        if (disp.decodedPort == 0x0000 && !disp.wasBeta128Gated)
            unmappedInCount++;
    }

    if (disp.wasBeta128Gated)
        beta128GatedCount++;
}

void PortActivitySummary::reset(uint32_t frame)
{
    frameNumber = frame;
    inCount = 0;
    outCount = 0;
    unmappedInCount = 0;
    unmappedOutCount = 0;
    beta128GatedCount = 0;
}

/// endregion </PortActivitySummary>

/// region <Session control>

void PortDiagnosticRecorder::start()
{
    // Recreate the ring buffer so produced/evicted counters restart with the session
    // (RingBuffer::clear() intentionally preserves them)
    _events = std::make_unique<RingBuffer<PortTraceEvent>>(_capacity);
    _totalFiltered.store(0, std::memory_order_relaxed);
    _autoStopped.store(false, std::memory_order_release);
    _sessionState.store(PortTraceSessionState::Capturing, std::memory_order_release);
}

void PortDiagnosticRecorder::stop()
{
    _sessionState.store(PortTraceSessionState::Stopped, std::memory_order_release);
}

void PortDiagnosticRecorder::pause()
{
    PortTraceSessionState expected = PortTraceSessionState::Capturing;
    _sessionState.compare_exchange_strong(expected, PortTraceSessionState::Paused, std::memory_order_acq_rel);
}

void PortDiagnosticRecorder::resume()
{
    PortTraceSessionState expected = PortTraceSessionState::Paused;
    _sessionState.compare_exchange_strong(expected, PortTraceSessionState::Capturing, std::memory_order_acq_rel);
}

void PortDiagnosticRecorder::clear()
{
    _events->clear();
}

/// endregion </Session control>

/// region <Configuration>

bool PortDiagnosticRecorder::setCapacity(size_t events)
{
    if (events == 0 || getSessionState() != PortTraceSessionState::Stopped)
        return false;

    _capacity = events;
    _events = std::make_unique<RingBuffer<PortTraceEvent>>(_capacity);

    return true;
}

bool PortDiagnosticRecorder::setOverflowMode(PortTraceOverflowMode mode)
{
    if (getSessionState() != PortTraceSessionState::Stopped)
        return false;

    _overflowMode = mode;

    return true;
}

/// endregion </Configuration>

/// region <Filtering>

void PortDiagnosticRecorder::setFilter(const PortTraceFilterSet& filter)
{
    std::unique_lock lock(_filterMutex);
    _filter = filter;
}

void PortDiagnosticRecorder::addIncludeRule(const PortTraceFilterRule& rule)
{
    std::unique_lock lock(_filterMutex);
    _filter.include.push_back(rule);
}

void PortDiagnosticRecorder::addExcludeRule(const PortTraceFilterRule& rule)
{
    std::unique_lock lock(_filterMutex);
    _filter.exclude.push_back(rule);
}

void PortDiagnosticRecorder::clearIncludeRules()
{
    std::unique_lock lock(_filterMutex);
    _filter.include.clear();
}

void PortDiagnosticRecorder::clearExcludeRules()
{
    std::unique_lock lock(_filterMutex);
    _filter.exclude.clear();
}

void PortDiagnosticRecorder::clearAllRules()
{
    std::unique_lock lock(_filterMutex);
    _filter.include.clear();
    _filter.exclude.clear();
}

PortTraceFilterSet PortDiagnosticRecorder::getFilter() const
{
    std::shared_lock lock(_filterMutex);
    return _filter;
}

void PortDiagnosticRecorder::presetAll()
{
    clearAllRules();
}

void PortDiagnosticRecorder::presetAyOnly()
{
    PortTraceFilterSet filter;

    PortTraceFilterRule fffd;
    fffd.decodedPort = 0xFFFD;
    filter.include.push_back(fffd);

    PortTraceFilterRule bffd;
    bffd.decodedPort = 0xBFFD;
    filter.include.push_back(bffd);

    setFilter(filter);
}

void PortDiagnosticRecorder::presetFdcOnly()
{
    PortTraceFilterSet filter;

    for (uint16_t port : {0x001F, 0x003F, 0x005F, 0x007F, 0x00FF})
    {
        PortTraceFilterRule rule;
        rule.decodedPort = port;
        filter.include.push_back(rule);
    }

    setFilter(filter);
}

void PortDiagnosticRecorder::presetNoFdc()
{
    PortTraceFilterSet filter;

    for (uint16_t port : {0x001F, 0x003F, 0x005F, 0x007F, 0x00FF})
    {
        PortTraceFilterRule rule;
        rule.decodedPort = port;
        filter.exclude.push_back(rule);
    }

    setFilter(filter);
}

void PortDiagnosticRecorder::presetOutsOnly()
{
    PortTraceFilterSet filter;

    PortTraceFilterRule rule;
    rule.directionOut = true;
    filter.include.push_back(rule);

    setFilter(filter);
}

void PortDiagnosticRecorder::presetInsOnly()
{
    PortTraceFilterSet filter;

    PortTraceFilterRule rule;
    rule.directionOut = false;
    filter.include.push_back(rule);

    setFilter(filter);
}

void PortDiagnosticRecorder::presetUnmapped()
{
    PortTraceFilterSet filter;

    PortTraceFilterRule rule;
    rule.unmappedOnly = true;
    filter.include.push_back(rule);

    setFilter(filter);
}

/// endregion </Filtering>

/// region <Hot path>

void PortDiagnosticRecorder::record(const PortTraceEvent& event)
{
    if (_sessionState.load(std::memory_order_acquire) != PortTraceSessionState::Capturing)
        return;

    {
        std::shared_lock lock(_filterMutex);
        if (!_filter.matches(event))
        {
            _totalFiltered.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    if (_overflowMode == PortTraceOverflowMode::StopWhenFull && _events->isFull())
    {
        // Keep the start of the run: auto-stop instead of evicting the oldest events
        _autoStopped.store(true, std::memory_order_release);
        _sessionState.store(PortTraceSessionState::Stopped, std::memory_order_release);
        return;
    }

    _events->push(event);
}

/// endregion </Hot path>

/// region <Retrieval>

std::vector<PortTraceEvent> PortDiagnosticRecorder::getLast(size_t count) const
{
    size_t total = _events->size();
    size_t start = (count >= total) ? 0 : total - count;

    return _events->getRange(start, count);
}

/// endregion </Retrieval>

/// region <Export>

const char* PortDiagnosticRecorder::DeviceIdToString(PortDeviceId id)
{
    switch (id)
    {
        case PortDeviceId::None:           return "None";
        case PortDeviceId::ULA_FE:         return "ULA_FE";
        case PortDeviceId::Memory_7FFD:    return "Memory_7FFD";
        case PortDeviceId::Memory_1FFD:    return "Memory_1FFD";
        case PortDeviceId::Memory_DFFD:    return "Memory_DFFD";
        case PortDeviceId::AY_FFFD:        return "AY_FFFD";
        case PortDeviceId::AY_BFFD:        return "AY_BFFD";
        case PortDeviceId::WD1793_Status:  return "WD1793_Status";
        case PortDeviceId::WD1793_Track:   return "WD1793_Track";
        case PortDeviceId::WD1793_Sector:  return "WD1793_Sector";
        case PortDeviceId::WD1793_Data:    return "WD1793_Data";
        case PortDeviceId::Beta128_System: return "Beta128_System";
        case PortDeviceId::Covox:          return "Covox";
        case PortDeviceId::Custom:         return "Custom";
        default:                           return "Unknown";
    }
}

std::string PortDiagnosticRecorder::describeFilter() const
{
    PortTraceFilterSet filter = getFilter();

    if (filter.include.empty() && filter.exclude.empty())
        return "All ports";

    auto describeRule = [](const PortTraceFilterRule& rule) -> std::string {
        std::ostringstream ss;
        const char* sep = "";
        if (rule.decodedPort)
        {
            ss << sep << "port=0x" << std::hex << std::uppercase << *rule.decodedPort;
            sep = " AND ";
        }
        if (rule.rawPort)
        {
            ss << sep << "raw=0x" << std::hex << std::uppercase << *rule.rawPort;
            sep = " AND ";
        }
        if (rule.device)
        {
            ss << sep << "device=" << DeviceIdToString(*rule.device);
            sep = " AND ";
        }
        if (rule.directionOut)
        {
            ss << sep << "direction=" << (*rule.directionOut ? "OUT" : "IN");
            sep = " AND ";
        }
        if (rule.pcRange)
        {
            ss << sep << "pc=0x" << std::hex << std::uppercase << rule.pcRange->first << "-0x"
               << rule.pcRange->second;
            sep = " AND ";
        }
        if (rule.valueRange)
        {
            ss << sep << "value=0x" << std::hex << std::uppercase << (int)rule.valueRange->first << "-0x"
               << (int)rule.valueRange->second;
            sep = " AND ";
        }
        if (rule.unmappedOnly)
        {
            ss << sep << "unmapped";
        }
        return ss.str();
    };

    std::ostringstream out;
    if (!filter.include.empty())
    {
        out << "include: ";
        for (size_t i = 0; i < filter.include.size(); i++)
            out << (i ? "; " : "") << "{" << describeRule(filter.include[i]) << "}";
    }
    if (!filter.exclude.empty())
    {
        if (!filter.include.empty())
            out << " | ";
        out << "exclude: ";
        for (size_t i = 0; i < filter.exclude.size(); i++)
            out << (i ? "; " : "") << "{" << describeRule(filter.exclude[i]) << "}";
    }

    return out.str();
}

namespace
{
std::string jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += c;
        }
        else if ((unsigned char)c < 0x20)
        {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        }
        else
        {
            out += c;
        }
    }
    return out;
}
}  // namespace

bool PortDiagnosticRecorder::saveToFile(const std::string& path, PortTraceExportFormat format,
                                        const PortTraceSessionInfo& info) const
{
    std::vector<PortTraceEvent> events = getAll();

    if (format == PortTraceExportFormat::Binary)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;

        // Header: 32 bytes — magic, version, count, capacity, tpf, ruleCount, reserved
        uint8_t header[32] = {};
        memcpy(header, "PTRC", 4);
        uint16_t version = 1;
        uint32_t count = static_cast<uint32_t>(events.size());
        uint32_t cap = static_cast<uint32_t>(_capacity);
        uint16_t ruleCount = static_cast<uint16_t>(info.decodeRules.size());
        memcpy(header + 4, &version, 2);
        memcpy(header + 6, &count, 4);
        memcpy(header + 10, &cap, 4);
        memcpy(header + 14, &info.tStatesPerFrame, 4);
        memcpy(header + 18, &ruleCount, 2);
        out.write(reinterpret_cast<const char*>(header), sizeof(header));

        for (const auto& rule : info.decodeRules)
        {
            out.write(reinterpret_cast<const char*>(&rule.mask), 2);
            out.write(reinterpret_cast<const char*>(&rule.match), 2);
            out.write(reinterpret_cast<const char*>(&rule.port), 2);
        }

        if (!events.empty())
            out.write(reinterpret_cast<const char*>(events.data()),
                      static_cast<std::streamsize>(events.size() * sizeof(PortTraceEvent)));

        return out.good();
    }

    if (format == PortTraceExportFormat::CSV)
    {
        std::ofstream out(path, std::ios::trunc);
        if (!out)
            return false;

        char line[256];
        out << "# Unreal-NG Port Access Trace v1\n";
        out << "# Model: " << info.modelName << ", Emulator: " << info.emulatorId << "\n";
        out << "# TStatesPerFrame: " << info.tStatesPerFrame << "\n";
        out << "# Filter: " << describeFilter() << "\n";
        out << "# Events: " << events.size() << " (" << totalEvicted() << " evicted, " << totalFiltered()
            << " filtered out)\n";
        for (size_t i = 0; i < info.decodeRules.size(); i++)
        {
            snprintf(line, sizeof(line), "# DecodeRule %zu: mask=0x%04X match=0x%04X port=0x%04X\n", i,
                     info.decodeRules[i].mask, info.decodeRules[i].match, info.decodeRules[i].port);
            out << line;
        }
        out << "index,timestamp,frame,direction,raw_port,decoded_port,decode_rule,value,pc,device,decoded,"
               "had_handler,beta128_gated,handled_inline,cf_trdos,via_legacy\n";

        for (size_t i = 0; i < events.size(); i++)
        {
            const PortTraceEvent& e = events[i];
            snprintf(line, sizeof(line),
                     "%zu,%llu,%u,%s,0x%04X,0x%04X,%u,0x%02X,0x%04X,%s,%d,%d,%d,%d,%d,%d\n", i,
                     (unsigned long long)e.timestamp, e.frameNumber, e.isOut() ? "OUT" : "IN", e.rawPort,
                     e.decodedPort, e.decodeRuleIndex, e.value, e.pc, DeviceIdToString(e.deviceId),
                     e.wasDecoded() ? 1 : 0, e.hadHandler() ? 1 : 0, e.wasBeta128Gated() ? 1 : 0,
                     e.wasHandledInline() ? 1 : 0, e.cfTrdosActive() ? 1 : 0,
                     (e.flags & PortTraceFlags::kViaLegacyBasePath) ? 1 : 0);
            out << line;
        }

        return out.good();
    }

    // JSON
    std::ofstream out(path, std::ios::trunc);
    if (!out)
        return false;

    char line[256];
    out << "{\n";
    out << "  \"format\": \"unreal-ng-porttrace-v1\",\n";
    out << "  \"session\": {\n";
    out << "    \"emulator_id\": \"" << jsonEscape(info.emulatorId) << "\",\n";
    out << "    \"model\": \"" << jsonEscape(info.modelName) << "\",\n";
    out << "    \"tstates_per_frame\": " << info.tStatesPerFrame << ",\n";
    out << "    \"filter\": \"" << jsonEscape(describeFilter()) << "\",\n";
    out << "    \"capacity\": " << _capacity << ",\n";
    out << "    \"total_captured\": " << totalProduced() << ",\n";
    out << "    \"total_evicted\": " << totalEvicted() << ",\n";
    out << "    \"total_filtered\": " << totalFiltered() << "\n";
    out << "  },\n";

    out << "  \"decode_rules\": [";
    for (size_t i = 0; i < info.decodeRules.size(); i++)
    {
        snprintf(line, sizeof(line), "%s\n    {\"index\": %zu, \"mask\": %u, \"match\": %u, \"port\": %u}",
                 i ? "," : "", i, info.decodeRules[i].mask, info.decodeRules[i].match, info.decodeRules[i].port);
        out << line;
    }
    out << (info.decodeRules.empty() ? "]" : "\n  ]") << ",\n";

    out << "  \"device_map\": {";
    for (int id = 0; id <= (int)PortDeviceId::Custom; id++)
    {
        const char* name = DeviceIdToString((PortDeviceId)id);
        if (std::string(name) == "Unknown")
            continue;
        snprintf(line, sizeof(line), "%s\n    \"%d\": \"%s\"", id ? "," : "", id, name);
        out << line;
    }
    out << "\n  },\n";

    out << "  \"events\": [";
    for (size_t i = 0; i < events.size(); i++)
    {
        const PortTraceEvent& e = events[i];
        snprintf(line, sizeof(line),
                 "%s\n    {\"ts\": %llu, \"frame\": %u, \"raw\": %u, \"dec\": %u, \"rule\": %u, \"val\": %u, "
                 "\"pc\": %u, \"dev\": %u, \"flags\": %u}",
                 i ? "," : "", (unsigned long long)e.timestamp, e.frameNumber, e.rawPort, e.decodedPort,
                 e.decodeRuleIndex, e.value, e.pc, (unsigned)e.deviceId, e.flags);
        out << line;
    }
    out << (events.empty() ? "]" : "\n  ]") << "\n";
    out << "}\n";

    return out.good();
}

/// endregion </Export>

PortDeviceId PortDiagnosticRecorder::ResolveDeviceId(uint16_t decodedPort)
{
    switch (decodedPort)
    {
        case 0x0000: return PortDeviceId::None;
        case 0x00FE: return PortDeviceId::ULA_FE;
        case 0x7FFD: return PortDeviceId::Memory_7FFD;
        case 0x1FFD: return PortDeviceId::Memory_1FFD;
        case 0xDFFD: return PortDeviceId::Memory_DFFD;
        case 0xFFFD: return PortDeviceId::AY_FFFD;
        case 0xBFFD: return PortDeviceId::AY_BFFD;
        case 0x001F: return PortDeviceId::WD1793_Status;  // Kempston shares #1F; WD1793 wins attribution
        case 0x003F: return PortDeviceId::WD1793_Track;
        case 0x005F: return PortDeviceId::WD1793_Sector;
        case 0x007F: return PortDeviceId::WD1793_Data;
        case 0x00FF: return PortDeviceId::Beta128_System;
        case 0x00FB: return PortDeviceId::Covox;
        default:     return PortDeviceId::Custom;
    }
}
