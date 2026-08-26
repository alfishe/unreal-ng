/**
 * @file cli-processor-porttrace.cpp
 * @brief Port Diagnostic Recorder (PDR) command handlers for the CLI processor
 *
 * Handles the `port-trace` (alias `porttrace`) command family:
 *   port-trace start|stop|pause|resume|clear|status
 *   port-trace dump [N]
 *   port-trace save <path> [json|csv|bin]
 *   port-trace include <conditions...>     (compound rule: all conditions AND)
 *   port-trace exclude <conditions...>
 *   port-trace filter show|clear [includes|excludes]
 *   port-trace preset <name>
 *   port-trace config capacity <n> | overflow ring|stop
 *
 * Gated by the runtime FeatureManager feature "porttrace" (alias "pt").
 * Design: docs/inprogress/2026-08-24-diagnostic-observability/
 */

#include "cli-processor.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <sstream>

#include "base/featuremanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/ports/portdiagrecorder.h"

namespace
{

// CLIProcessor member handlers see NEWLINE unqualified; these free helpers need it too
constexpr const char* NEWLINE = CLIProcessor::NEWLINE;

std::optional<uint32_t> parseNumber(const std::string& text, int base)
{
    try
    {
        size_t pos = 0;
        unsigned long value = std::stoul(text, &pos, base);
        if (pos != text.size() || value > 0xFFFFFFFFul)
            return std::nullopt;
        return static_cast<uint32_t>(value);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<uint16_t> parseHex16(const std::string& text)
{
    auto value = parseNumber(text, 16);
    if (!value || *value > 0xFFFF)
        return std::nullopt;
    return static_cast<uint16_t>(*value);
}

std::optional<PortDeviceId> parseDeviceId(const std::string& name)
{
    for (int id = 0; id <= static_cast<int>(PortDeviceId::Custom); id++)
    {
        PortDeviceId device = static_cast<PortDeviceId>(id);
        std::string deviceName = PortDiagnosticRecorder::DeviceIdToString(device);
        if (deviceName == "Unknown")
            continue;

        // Case-insensitive comparison
        if (deviceName.size() == name.size() &&
            std::equal(deviceName.begin(), deviceName.end(), name.begin(),
                       [](char a, char b) { return ::tolower(a) == ::tolower(b); }))
            return device;
    }
    return std::nullopt;
}

/// Parse "<dimension> <value...>" condition pairs into one compound rule.
/// Returns error text on failure, empty string on success.
std::string parseFilterRule(const std::vector<std::string>& args, size_t start, PortTraceFilterRule& rule)
{
    size_t i = start;
    while (i < args.size())
    {
        std::string dim = args[i];
        std::transform(dim.begin(), dim.end(), dim.begin(), ::tolower);

        if (dim == "unmapped")
        {
            rule.unmappedOnly = true;
            i += 1;
            continue;
        }

        if (i + 1 >= args.size())
            return "Missing value for condition '" + dim + "'";

        if (dim == "port")
        {
            auto port = parseHex16(args[i + 1]);
            if (!port)
                return "Invalid port (hex expected): " + args[i + 1];
            rule.decodedPort = *port;
            i += 2;
        }
        else if (dim == "raw")
        {
            auto port = parseHex16(args[i + 1]);
            if (!port)
                return "Invalid raw port (hex expected): " + args[i + 1];
            rule.rawPort = *port;
            i += 2;
        }
        else if (dim == "device")
        {
            auto device = parseDeviceId(args[i + 1]);
            if (!device)
                return "Unknown device: " + args[i + 1];
            rule.device = *device;
            i += 2;
        }
        else if (dim == "direction")
        {
            std::string dir = args[i + 1];
            std::transform(dir.begin(), dir.end(), dir.begin(), ::tolower);
            if (dir == "in")
                rule.directionOut = false;
            else if (dir == "out")
                rule.directionOut = true;
            else
                return "Invalid direction (in/out expected): " + args[i + 1];
            i += 2;
        }
        else if (dim == "pc")
        {
            if (i + 2 >= args.size())
                return "pc requires two hex values: pc <lo> <hi>";
            auto lo = parseHex16(args[i + 1]);
            auto hi = parseHex16(args[i + 2]);
            if (!lo || !hi)
                return "Invalid pc range (hex expected)";
            rule.pcRange = {*lo, *hi};
            i += 3;
        }
        else if (dim == "value")
        {
            if (i + 2 >= args.size())
                return "value requires two hex values: value <lo> <hi>";
            auto lo = parseHex16(args[i + 1]);
            auto hi = parseHex16(args[i + 2]);
            if (!lo || !hi || *lo > 0xFF || *hi > 0xFF)
                return "Invalid value range (8-bit hex expected)";
            rule.valueRange = {static_cast<uint8_t>(*lo), static_cast<uint8_t>(*hi)};
            i += 3;
        }
        else
        {
            return "Unknown condition '" + dim + "' (port/raw/device/direction/pc/value/unmapped)";
        }
    }

    return "";
}

const char* sessionStateName(PortTraceSessionState state)
{
    switch (state)
    {
        case PortTraceSessionState::Capturing: return "capturing";
        case PortTraceSessionState::Paused:    return "paused";
        default:                               return "stopped";
    }
}

std::string formatEventTable(const std::vector<PortTraceEvent>& events)
{
    std::ostringstream out;
    out << "  #    Frame  T-State        Dir  Raw    Decoded  Value  PC     Device          Flags" << NEWLINE;
    out << "----  ------  -------------  ---  -----  -------  -----  -----  --------------  ------" << NEWLINE;

    char line[160];
    for (size_t i = 0; i < events.size(); i++)
    {
        const PortTraceEvent& e = events[i];
        std::string flags;
        if (e.wasDecoded()) flags += 'D';
        if (e.hadHandler()) flags += 'H';
        if (e.wasBeta128Gated()) flags += 'G';
        if (e.wasHandledInline()) flags += 'I';
        if (e.cfTrdosActive()) flags += 'T';
        if (e.flags & PortTraceFlags::kViaLegacyBasePath) flags += 'L';

        snprintf(line, sizeof(line), "%4zu  %6u  %13llu  %-3s  %04X   %04X     %02X     %04X   %-14s  %s",
                 i, e.frameNumber, (unsigned long long)e.timestamp, e.isOut() ? "OUT" : "IN", e.rawPort,
                 e.decodedPort, e.value, e.pc, PortDiagnosticRecorder::DeviceIdToString(e.deviceId),
                 flags.c_str());
        out << line << NEWLINE;
    }
    out << "Flags: D=decoded  H=hadHandler  G=beta128Gated  I=handledInline  T=cfTrdos  L=legacyPath" << NEWLINE;

    return out.str();
}

}  // namespace

void CLIProcessor::ShowPortTraceHelp(const ClientSession& session)
{
    std::ostringstream help;
    help << "Port trace commands (runtime feature 'porttrace', alias 'pt'):" << NEWLINE;
    help << "  port-trace start                    - Start capture (clears buffer)" << NEWLINE;
    help << "  port-trace stop                     - Stop capture (data preserved)" << NEWLINE;
    help << "  port-trace pause|resume             - Suspend / continue capture" << NEWLINE;
    help << "  port-trace clear                    - Purge buffer" << NEWLINE;
    help << "  port-trace status                   - Session state, counters, filter" << NEWLINE;
    help << "  port-trace dump [N]                 - Show last N events (default 32)" << NEWLINE;
    help << "  port-trace save <path> [json|csv|bin|binz] - Save trace to file (default json;" << NEWLINE;
    help << "                                        binz = zstd-compressed, ~50-100x smaller)" << NEWLINE;
    help << "  port-trace include <cond...>        - Add compound include rule (AND within rule)" << NEWLINE;
    help << "  port-trace exclude <cond...>        - Add compound exclude rule (exclude wins)" << NEWLINE;
    help << "    conditions: port <hex> | raw <hex> | device <name> | direction in|out" << NEWLINE;
    help << "                pc <lo> <hi> | value <lo> <hi> | unmapped" << NEWLINE;
    help << "    example: port-trace include port FFFD direction out" << NEWLINE;
    help << "  port-trace filter show              - Show filter configuration" << NEWLINE;
    help << "  port-trace filter clear [includes|excludes] - Reset filter rules" << NEWLINE;
    help << "  port-trace preset <name>            - all|ay-only|fdc-only|no-fdc|no-fe|sound|paging|outs-only|ins-only|unmapped" << NEWLINE;
    help << "  port-trace config capacity <n>      - Ring buffer capacity (only while stopped)" << NEWLINE;
    help << "  port-trace config overflow ring|stop - Evict oldest vs stop-when-full" << NEWLINE;
    session.SendResponse(help.str());
}

void CLIProcessor::HandlePortTrace(const ClientSession& session, const std::vector<std::string>& args)
{
    if (args.empty())
    {
        ShowPortTraceHelp(session);
        return;
    }

    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select' or 'list' to manage emulators." +
                             std::string(NEWLINE));
        return;
    }

    auto* context = emulator->GetContext();
    if (!context || !context->pPortDecoder)
    {
        session.SendResponse("Port decoder not available." + std::string(NEWLINE));
        return;
    }

    PortDecoder* decoder = context->pPortDecoder;

    // Runtime feature gate
    if (!context->pFeatureManager || !context->pFeatureManager->isEnabled(Features::kPortTrace))
    {
        session.SendResponse("porttrace feature is disabled - enable with 'feature porttrace on'" +
                             std::string(NEWLINE));
        return;
    }

    PortDiagnosticRecorder* recorder = decoder->getPortTraceRecorder();
    if (!recorder)
    {
        session.SendResponse("Port trace recorder not instantiated (toggle 'feature porttrace on' again)." +
                             std::string(NEWLINE));
        return;
    }

    std::string action = args[0];
    std::transform(action.begin(), action.end(), action.begin(), ::tolower);

    if (action == "start")
    {
        recorder->start();
        std::ostringstream out;
        out << "Port trace started (capacity: " << recorder->capacity() << ", overflow: "
            << (recorder->overflowMode() == PortTraceOverflowMode::Ring ? "ring" : "stop-when-full") << ")"
            << NEWLINE;
        session.SendResponse(out.str());
    }
    else if (action == "stop")
    {
        recorder->stop();
        std::ostringstream out;
        out << "Port trace stopped: " << recorder->eventCount() << " events captured" << NEWLINE;
        session.SendResponse(out.str());
    }
    else if (action == "pause")
    {
        recorder->pause();
        session.SendResponse("Port trace paused." + std::string(NEWLINE));
    }
    else if (action == "resume")
    {
        recorder->resume();
        session.SendResponse("Port trace resumed." + std::string(NEWLINE));
    }
    else if (action == "clear")
    {
        recorder->clear();
        session.SendResponse("Port trace buffer cleared." + std::string(NEWLINE));
    }
    else if (action == "status")
    {
        std::ostringstream out;
        out << "Port trace status" << NEWLINE;
        out << "  State:     " << sessionStateName(recorder->getSessionState())
            << (recorder->wasAutoStopped() ? " (auto-stopped: buffer full)" : "") << NEWLINE;
        out << "  Events:    " << recorder->eventCount() << " / " << recorder->capacity() << NEWLINE;
        out << "  Produced:  " << recorder->totalProduced() << "  Evicted: " << recorder->totalEvicted()
            << "  Filtered out: " << recorder->totalFiltered() << NEWLINE;
        out << "  Overflow:  "
            << (recorder->overflowMode() == PortTraceOverflowMode::Ring ? "ring" : "stop-when-full") << NEWLINE;
        out << "  Filter:    " << recorder->describeFilter() << NEWLINE;

        const PortActivitySummary& summary = decoder->getActivitySummary();
        out << "  Frame " << summary.frameNumber << ": in=" << summary.inCount << " out=" << summary.outCount
            << " unmappedIn=" << summary.unmappedInCount << " unmappedOut=" << summary.unmappedOutCount
            << " gated=" << summary.beta128GatedCount << NEWLINE;
        session.SendResponse(out.str());
    }
    else if (action == "dump")
    {
        size_t count = 32;
        if (args.size() >= 2)
        {
            auto parsed = parseNumber(args[1], 10);
            if (parsed && *parsed > 0)
                count = *parsed;
        }

        std::vector<PortTraceEvent> events = recorder->getLast(count);
        std::ostringstream out;
        out << "Port trace: " << recorder->eventCount() << " events buffered, showing last " << events.size()
            << NEWLINE;
        out << formatEventTable(events);
        session.SendResponse(out.str());
    }
    else if (action == "save")
    {
        if (args.size() < 2)
        {
            session.SendResponse("Usage: port-trace save <path> [json|csv|bin]" + std::string(NEWLINE));
            return;
        }

        PortTraceExportFormat format = PortTraceExportFormat::JSON;
        if (args.size() >= 3)
        {
            std::string fmt = args[2];
            std::transform(fmt.begin(), fmt.end(), fmt.begin(), ::tolower);
            if (fmt == "csv")
                format = PortTraceExportFormat::CSV;
            else if (fmt == "bin" || fmt == "binary")
                format = PortTraceExportFormat::Binary;
            else if (fmt == "binz")
                format = PortTraceExportFormat::BinaryCompressed;
            else if (fmt != "json")
            {
                session.SendResponse("Unknown format (json/csv/bin/binz expected): " + args[2] + NEWLINE);
                return;
            }
        }

        size_t count = recorder->eventCount();
        if (recorder->saveToFile(args[1], format, decoder->getPortTraceSessionInfo()))
        {
            std::ostringstream out;
            out << "Saved " << count << " events to " << args[1] << NEWLINE;
            session.SendResponse(out.str());
        }
        else
        {
            session.SendResponse("Failed to save trace to " + args[1] + NEWLINE);
        }
    }
    else if (action == "include" || action == "exclude")
    {
        if (args.size() < 2)
        {
            session.SendResponse("Usage: port-trace " + action + " <conditions...>" + NEWLINE);
            return;
        }

        PortTraceFilterRule rule;
        std::string error = parseFilterRule(args, 1, rule);
        if (!error.empty())
        {
            session.SendResponse(error + NEWLINE);
            return;
        }

        if (action == "include")
            recorder->addIncludeRule(rule);
        else
            recorder->addExcludeRule(rule);

        session.SendResponse("Filter rule added. Effective: " + recorder->describeFilter() + NEWLINE);
    }
    else if (action == "filter")
    {
        std::string sub = args.size() >= 2 ? args[1] : "show";
        std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

        if (sub == "show")
        {
            session.SendResponse("Filter: " + recorder->describeFilter() + NEWLINE);
        }
        else if (sub == "clear")
        {
            std::string what = args.size() >= 3 ? args[2] : "";
            std::transform(what.begin(), what.end(), what.begin(), ::tolower);
            if (what == "includes")
                recorder->clearIncludeRules();
            else if (what == "excludes")
                recorder->clearExcludeRules();
            else
                recorder->clearAllRules();
            session.SendResponse("Filter cleared. Effective: " + recorder->describeFilter() + NEWLINE);
        }
        else
        {
            session.SendResponse("Usage: port-trace filter show|clear [includes|excludes]" + std::string(NEWLINE));
        }
    }
    else if (action == "preset")
    {
        if (args.size() < 2)
        {
            session.SendResponse(
                "Usage: port-trace preset all|ay-only|fdc-only|no-fdc|no-fe|sound|paging|outs-only|ins-only|unmapped" +
                std::string(NEWLINE));
            return;
        }

        std::string name = args[1];
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (name == "all")
            recorder->presetAll();
        else if (name == "ay-only")
            recorder->presetAyOnly();
        else if (name == "fdc-only")
            recorder->presetFdcOnly();
        else if (name == "no-fdc")
            recorder->presetNoFdc();
        else if (name == "outs-only")
            recorder->presetOutsOnly();
        else if (name == "ins-only")
            recorder->presetInsOnly();
        else if (name == "unmapped")
            recorder->presetUnmapped();
        else if (name == "no-fe")
            recorder->presetNoFe();
        else if (name == "sound")
            recorder->presetSound();
        else if (name == "paging")
            recorder->presetPaging();
        else
        {
            session.SendResponse("Unknown preset: " + args[1] + NEWLINE);
            return;
        }

        session.SendResponse("Preset applied. Effective: " + recorder->describeFilter() + NEWLINE);
    }
    else if (action == "config")
    {
        if (args.size() >= 3 && args[1] == "capacity")
        {
            auto capacity = parseNumber(args[2], 10);
            if (!capacity || *capacity == 0)
            {
                session.SendResponse("Invalid capacity: " + args[2] + NEWLINE);
                return;
            }
            if (recorder->setCapacity(*capacity))
                session.SendResponse("Capacity set to " + std::to_string(*capacity) + NEWLINE);
            else
                session.SendResponse("Capacity can only be changed while stopped." + std::string(NEWLINE));
        }
        else if (args.size() >= 3 && args[1] == "overflow")
        {
            std::string mode = args[2];
            std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
            PortTraceOverflowMode overflow;
            if (mode == "ring")
                overflow = PortTraceOverflowMode::Ring;
            else if (mode == "stop")
                overflow = PortTraceOverflowMode::StopWhenFull;
            else
            {
                session.SendResponse("Invalid overflow mode (ring/stop expected): " + args[2] + NEWLINE);
                return;
            }
            if (recorder->setOverflowMode(overflow))
                session.SendResponse("Overflow mode set to " + mode + NEWLINE);
            else
                session.SendResponse("Overflow mode can only be changed while stopped." + std::string(NEWLINE));
        }
        else
        {
            session.SendResponse("Usage: port-trace config capacity <n> | overflow ring|stop" +
                                 std::string(NEWLINE));
        }
    }
    else
    {
        ShowPortTraceHelp(session);
    }
}
