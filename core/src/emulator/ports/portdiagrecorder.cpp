#include "stdafx.h"

#include "portdiagrecorder.h"

#include <zstd.h>

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

void PortDiagnosticRecorder::presetNoFe()
{
    PortTraceFilterSet filter;

    PortTraceFilterRule rule;
    rule.decodedPort = 0x00FE;
    filter.exclude.push_back(rule);

    setFilter(filter);
}

void PortDiagnosticRecorder::presetSound()
{
    PortTraceFilterSet filter;

    for (PortDeviceId device : {PortDeviceId::AY_FFFD, PortDeviceId::AY_BFFD, PortDeviceId::Covox})
    {
        PortTraceFilterRule rule;
        rule.device = device;
        filter.include.push_back(rule);
    }

    setFilter(filter);
}

void PortDiagnosticRecorder::presetPaging()
{
    PortTraceFilterSet filter;

    for (PortDeviceId device :
         {PortDeviceId::Memory_7FFD, PortDeviceId::Memory_1FFD, PortDeviceId::Memory_DFFD})
    {
        PortTraceFilterRule rule;
        rule.device = device;
        filter.include.push_back(rule);
    }

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

/// region <PTR2 v2 columnar delta/xor transform>
///
/// Payload layout (22 bytes/event, no padding — columnar):
///   u64 tsDelta[n]    d[0] = ts[0]; d[i] = ts[i] - ts[i-1]   (wrapping)
///   u32 frameDelta[n]
///   u16 rawXor[n]     x[0] = raw[0]; x[i] = raw[i] ^ raw[i-1]
///   u16 decXor[n]
///   u16 pcXor[n]
///   u8  value[n], u8 rule[n], u8 dev[n], u8 flags[n]
///
/// Timestamps dominate the raw stream's entropy; their deltas are
/// near-constant instruction spacings, so the transform + one zstd frame
/// compresses real traces 50-100x (vs ~9x for zstd on the raw stream).
/// The Python reader (tools/porttrace/porttrace_convert.py) mirrors this.

namespace
{

constexpr size_t kV2BytesPerEvent = 22;

template <typename T>
void appendLE(std::vector<uint8_t>& out, T value)
{
    for (size_t i = 0; i < sizeof(T); i++)
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

template <typename T>
T readLE(const uint8_t* p)
{
    T value = 0;
    for (size_t i = 0; i < sizeof(T); i++)
        value |= static_cast<T>(p[i]) << (8 * i);
    return value;
}

std::vector<uint8_t> encodePayloadV2(const std::vector<PortTraceEvent>& events)
{
    std::vector<uint8_t> out;
    out.reserve(events.size() * kV2BytesPerEvent);

    uint64_t prevTs = 0;
    for (const auto& e : events) { appendLE<uint64_t>(out, e.timestamp - prevTs); prevTs = e.timestamp; }
    uint32_t prevFrame = 0;
    for (const auto& e : events) { appendLE<uint32_t>(out, e.frameNumber - prevFrame); prevFrame = e.frameNumber; }
    uint16_t prev = 0;
    for (const auto& e : events) { appendLE<uint16_t>(out, e.rawPort ^ prev); prev = e.rawPort; }
    prev = 0;
    for (const auto& e : events) { appendLE<uint16_t>(out, e.decodedPort ^ prev); prev = e.decodedPort; }
    prev = 0;
    for (const auto& e : events) { appendLE<uint16_t>(out, e.pc ^ prev); prev = e.pc; }
    for (const auto& e : events) out.push_back(e.value);
    for (const auto& e : events) out.push_back(e.decodeRuleIndex);
    for (const auto& e : events) out.push_back(static_cast<uint8_t>(e.deviceId));
    for (const auto& e : events) out.push_back(e.flags);

    return out;
}

bool decodePayloadV2(const std::vector<uint8_t>& payload, size_t count,
                     std::vector<PortTraceEvent>& outEvents)
{
    if (payload.size() != count * kV2BytesPerEvent)
        return false;

    outEvents.assign(count, PortTraceEvent{});
    const uint8_t* p = payload.data();

    uint64_t ts = 0;
    for (size_t i = 0; i < count; i++, p += 8) { ts += readLE<uint64_t>(p); outEvents[i].timestamp = ts; }
    uint32_t frame = 0;
    for (size_t i = 0; i < count; i++, p += 4) { frame += readLE<uint32_t>(p); outEvents[i].frameNumber = frame; }
    uint16_t prev = 0;
    for (size_t i = 0; i < count; i++, p += 2) { prev ^= readLE<uint16_t>(p); outEvents[i].rawPort = prev; }
    prev = 0;
    for (size_t i = 0; i < count; i++, p += 2) { prev ^= readLE<uint16_t>(p); outEvents[i].decodedPort = prev; }
    prev = 0;
    for (size_t i = 0; i < count; i++, p += 2) { prev ^= readLE<uint16_t>(p); outEvents[i].pc = prev; }
    for (size_t i = 0; i < count; i++) outEvents[i].value = *p++;
    for (size_t i = 0; i < count; i++) outEvents[i].decodeRuleIndex = *p++;
    for (size_t i = 0; i < count; i++) outEvents[i].deviceId = static_cast<PortDeviceId>(*p++);
    for (size_t i = 0; i < count; i++) outEvents[i].flags = *p++;

    return true;
}

}  // namespace

/// endregion </PTR2 v2 columnar delta/xor transform>

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

    if (format == PortTraceExportFormat::BinaryCompressed)
    {
        std::vector<uint8_t> payload = encodePayloadV2(events);

        std::vector<uint8_t> compressed(ZSTD_compressBound(payload.size()));
        size_t compressedSize = ZSTD_compress(compressed.data(), compressed.size(), payload.data(),
                                              payload.size(), /*level=*/19);
        if (ZSTD_isError(compressedSize))
            return false;

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;

        // Header: 32 bytes — magic, version, count, capacity, tpf, ruleCount,
        // compressedSize (u64), reserved. Rules stay uncompressed (tiny).
        uint8_t header[32] = {};
        memcpy(header, "PTR2", 4);
        uint16_t version = 2;
        uint32_t count = static_cast<uint32_t>(events.size());
        uint32_t cap = static_cast<uint32_t>(_capacity);
        uint16_t ruleCount = static_cast<uint16_t>(info.decodeRules.size());
        uint64_t compSize = compressedSize;
        memcpy(header + 4, &version, 2);
        memcpy(header + 6, &count, 4);
        memcpy(header + 10, &cap, 4);
        memcpy(header + 14, &info.tStatesPerFrame, 4);
        memcpy(header + 18, &ruleCount, 2);
        memcpy(header + 20, &compSize, 8);
        out.write(reinterpret_cast<const char*>(header), sizeof(header));

        for (const auto& rule : info.decodeRules)
        {
            out.write(reinterpret_cast<const char*>(&rule.mask), 2);
            out.write(reinterpret_cast<const char*>(&rule.match), 2);
            out.write(reinterpret_cast<const char*>(&rule.port), 2);
        }

        out.write(reinterpret_cast<const char*>(compressed.data()),
                  static_cast<std::streamsize>(compressedSize));

        // Flush before checking: the ofstream destructor's close runs after
        // return, so a failure in the final buffered write (disk full, EIO)
        // would otherwise be reported as success
        out.flush();
        return out.good();
    }

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

        // Flush before checking: the ofstream destructor's close runs after
        // return, so a failure in the final buffered write (disk full, EIO)
        // would otherwise be reported as success
        out.flush();
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

        // Flush before checking: the ofstream destructor's close runs after
        // return, so a failure in the final buffered write (disk full, EIO)
        // would otherwise be reported as success
        out.flush();
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

    out.flush();  // surface close-time write failures (see comment above)
    return out.good();
}

bool PortDiagnosticRecorder::loadFromFile(const std::string& path, PortTraceSessionInfo& outInfo,
                                          std::vector<PortTraceEvent>& outEvents)
{
    outInfo = {};
    outEvents.clear();

    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;

    uint8_t header[32] = {};
    in.read(reinterpret_cast<char*>(header), sizeof(header));
    if (in.gcount() != sizeof(header))
        return false;

    bool isV1 = memcmp(header, "PTRC", 4) == 0;
    bool isV2 = memcmp(header, "PTR2", 4) == 0;
    if (!isV1 && !isV2)
        return false;

    uint32_t count = 0;
    uint16_t ruleCount = 0;
    memcpy(&count, header + 6, 4);
    memcpy(&outInfo.tStatesPerFrame, header + 14, 4);
    memcpy(&ruleCount, header + 18, 2);

    for (uint16_t i = 0; i < ruleCount; i++)
    {
        PortTraceDecodeRule rule;
        in.read(reinterpret_cast<char*>(&rule.mask), 2);
        in.read(reinterpret_cast<char*>(&rule.match), 2);
        in.read(reinterpret_cast<char*>(&rule.port), 2);
        if (!in.good())
            return false;
        outInfo.decodeRules.push_back(rule);
    }

    if (isV1)
    {
        outEvents.resize(count);
        in.read(reinterpret_cast<char*>(outEvents.data()),
                static_cast<std::streamsize>(count * sizeof(PortTraceEvent)));
        return in.gcount() == static_cast<std::streamsize>(count * sizeof(PortTraceEvent));
    }

    // V2: zstd frame of the columnar delta/xor payload
    uint64_t compressedSize = 0;
    memcpy(&compressedSize, header + 20, 8);

    std::vector<uint8_t> compressed(compressedSize);
    in.read(reinterpret_cast<char*>(compressed.data()), static_cast<std::streamsize>(compressedSize));
    if (in.gcount() != static_cast<std::streamsize>(compressedSize))
        return false;

    std::vector<uint8_t> payload(static_cast<size_t>(count) * kV2BytesPerEvent);
    size_t decompressedSize =
        ZSTD_decompress(payload.data(), payload.size(), compressed.data(), compressed.size());
    if (ZSTD_isError(decompressedSize) || decompressedSize != payload.size())
        return false;

    return decodePayloadV2(payload, count, outEvents);
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
