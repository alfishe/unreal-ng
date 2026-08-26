#pragma once

/// @file lua_porttrace.h
/// @brief sol2 (Lua) bindings for the Port Diagnostic Recorder (PDR)
///
/// Same API surface as the Python bindings, adapted to Lua conventions
/// (tables in, tables out). Gated by the runtime FeatureManager feature
/// "porttrace" (alias "pt"). Design: docs/inprogress/2026-08-24-diagnostic-observability/

#include <sol/sol.hpp>

#include <functional>
#include <string>

#include <base/featuremanager.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/ports/portdecoder.h>
#include <emulator/ports/portdiagrecorder.h>

namespace LuaPortTrace
{

/// Resolve the recorder; returns nullptr (with message in outError) when the
/// feature is off or no emulator is selected
inline PortDiagnosticRecorder* resolveRecorder(Emulator* emulator, PortDecoder** outDecoder,
                                               std::string& outError)
{
    if (!emulator)
    {
        outError = "No emulator selected";
        return nullptr;
    }

    auto* context = emulator->GetContext();
    if (!context || !context->pPortDecoder)
    {
        outError = "Port decoder not available";
        return nullptr;
    }

    if (!context->pFeatureManager || !context->pFeatureManager->isEnabled(Features::kPortTrace))
    {
        outError = "porttrace feature is disabled - enable with 'feature porttrace on'";
        return nullptr;
    }

    PortDiagnosticRecorder* recorder = context->pPortDecoder->getPortTraceRecorder();
    if (!recorder)
    {
        outError = "Port trace recorder not instantiated";
        return nullptr;
    }

    if (outDecoder)
        *outDecoder = context->pPortDecoder;
    return recorder;
}

inline sol::table eventToTable(sol::state_view lua, const PortTraceEvent& e)
{
    sol::table t = lua.create_table();
    t["timestamp"] = e.timestamp;
    t["frame"] = e.frameNumber;
    t["raw_port"] = e.rawPort;
    t["decoded_port"] = e.decodedPort;
    t["decode_rule"] = e.decodeRuleIndex;
    t["value"] = e.value;
    t["pc"] = e.pc;
    t["device"] = std::string(PortDiagnosticRecorder::DeviceIdToString(e.deviceId));
    t["direction"] = e.isOut() ? "OUT" : "IN";
    t["decoded"] = e.wasDecoded();
    t["had_handler"] = e.hadHandler();
    t["beta128_gated"] = e.wasBeta128Gated();
    t["handled_inline"] = e.wasHandledInline();
    t["cf_trdos"] = e.cfTrdosActive();
    t["via_legacy"] = (e.flags & PortTraceFlags::kViaLegacyBasePath) != 0;
    return t;
}

/// Build a compound rule from a Lua table: { port=0xFFFD, direction="out", ... }
inline bool ruleFromTable(const sol::table& spec, PortTraceFilterRule& rule, std::string& outError)
{
    if (sol::optional<uint16_t> port = spec["port"])
        rule.decodedPort = *port;
    if (sol::optional<uint16_t> raw = spec["raw"])
        rule.rawPort = *raw;

    if (sol::optional<std::string> device = spec["device"])
    {
        bool found = false;
        for (int id = 0; id <= static_cast<int>(PortDeviceId::Custom); id++)
        {
            if (*device == PortDiagnosticRecorder::DeviceIdToString(static_cast<PortDeviceId>(id)))
            {
                rule.device = static_cast<PortDeviceId>(id);
                found = true;
                break;
            }
        }
        if (!found)
        {
            outError = "Unknown device: " + *device;
            return false;
        }
    }

    if (sol::optional<std::string> direction = spec["direction"])
    {
        if (*direction == "in" || *direction == "IN")
            rule.directionOut = false;
        else if (*direction == "out" || *direction == "OUT")
            rule.directionOut = true;
        else
        {
            outError = "direction must be 'in' or 'out'";
            return false;
        }
    }

    if (sol::optional<sol::table> pc = spec["pc"])
        rule.pcRange = {(*pc)[1].get<uint16_t>(), (*pc)[2].get<uint16_t>()};
    if (sol::optional<sol::table> value = spec["value"])
        rule.valueRange = {(*value)[1].get<uint8_t>(), (*value)[2].get<uint8_t>()};
    if (sol::optional<bool> unmapped = spec["unmapped"])
        rule.unmappedOnly = *unmapped;

    return true;
}

/// Register the porttrace_* Lua functions. getEmulator resolves the target
/// instance per call (the explicitly bound one or the currently selected one).
inline void registerBindings(sol::state& lua, std::function<Emulator*()> getEmulator)
{
    lua.set_function("porttrace_start", [getEmulator]() -> bool {
        std::string error;
        auto* recorder = resolveRecorder(getEmulator(), nullptr, error);
        if (!recorder) return false;
        recorder->start();
        return true;
    });

    lua.set_function("porttrace_stop", [getEmulator]() -> bool {
        std::string error;
        auto* recorder = resolveRecorder(getEmulator(), nullptr, error);
        if (!recorder) return false;
        recorder->stop();
        return true;
    });

    lua.set_function("porttrace_pause", [getEmulator]() -> bool {
        std::string error;
        auto* recorder = resolveRecorder(getEmulator(), nullptr, error);
        if (!recorder) return false;
        recorder->pause();
        return true;
    });

    lua.set_function("porttrace_resume", [getEmulator]() -> bool {
        std::string error;
        auto* recorder = resolveRecorder(getEmulator(), nullptr, error);
        if (!recorder) return false;
        recorder->resume();
        return true;
    });

    lua.set_function("porttrace_clear", [getEmulator]() -> bool {
        std::string error;
        auto* recorder = resolveRecorder(getEmulator(), nullptr, error);
        if (!recorder) return false;
        recorder->clear();
        return true;
    });

    lua.set_function("porttrace_status", [getEmulator](sol::this_state s) -> sol::table {
        sol::state_view lua(s);
        sol::table t = lua.create_table();
        std::string error;
        PortDecoder* decoder = nullptr;
        auto* recorder = resolveRecorder(getEmulator(), &decoder, error);
        if (!recorder)
        {
            t["error"] = error;
            return t;
        }

        switch (recorder->getSessionState())
        {
            case PortTraceSessionState::Capturing: t["state"] = "capturing"; break;
            case PortTraceSessionState::Paused:    t["state"] = "paused"; break;
            default:                               t["state"] = "stopped"; break;
        }
        t["events"] = recorder->eventCount();
        t["capacity"] = recorder->capacity();
        t["total_produced"] = recorder->totalProduced();
        t["total_evicted"] = recorder->totalEvicted();
        t["total_filtered"] = recorder->totalFiltered();
        t["auto_stopped"] = recorder->wasAutoStopped();
        t["overflow"] = recorder->overflowMode() == PortTraceOverflowMode::Ring ? "ring" : "stop-when-full";
        t["filter"] = recorder->describeFilter();

        const PortActivitySummary& summary = decoder->getActivitySummary();
        sol::table activity = lua.create_table();
        activity["frame"] = summary.frameNumber;
        activity["in"] = summary.inCount;
        activity["out"] = summary.outCount;
        activity["unmapped_in"] = summary.unmappedInCount;
        activity["unmapped_out"] = summary.unmappedOutCount;
        activity["beta128_gated"] = summary.beta128GatedCount;
        t["activity"] = activity;
        return t;
    });

    lua.set_function("porttrace_set_capacity", [getEmulator](size_t events) -> bool {
        std::string error;
        auto* recorder = resolveRecorder(getEmulator(), nullptr, error);
        return recorder && recorder->setCapacity(events);
    });

    lua.set_function("porttrace_set_overflow", [getEmulator](const std::string& mode) -> bool {
        std::string error;
        auto* recorder = resolveRecorder(getEmulator(), nullptr, error);
        if (!recorder) return false;
        if (mode == "ring")
            return recorder->setOverflowMode(PortTraceOverflowMode::Ring);
        if (mode == "stop")
            return recorder->setOverflowMode(PortTraceOverflowMode::StopWhenFull);
        return false;
    });

    lua.set_function("porttrace_include", [getEmulator](const sol::table& spec) -> bool {
        std::string error;
        auto* recorder = resolveRecorder(getEmulator(), nullptr, error);
        if (!recorder) return false;
        PortTraceFilterRule rule;
        if (!ruleFromTable(spec, rule, error)) return false;
        recorder->addIncludeRule(rule);
        return true;
    });

    lua.set_function("porttrace_exclude", [getEmulator](const sol::table& spec) -> bool {
        std::string error;
        auto* recorder = resolveRecorder(getEmulator(), nullptr, error);
        if (!recorder) return false;
        PortTraceFilterRule rule;
        if (!ruleFromTable(spec, rule, error)) return false;
        recorder->addExcludeRule(rule);
        return true;
    });

    lua.set_function("porttrace_filter_clear", [getEmulator](sol::optional<std::string> what) -> bool {
        std::string error;
        auto* recorder = resolveRecorder(getEmulator(), nullptr, error);
        if (!recorder) return false;
        if (what && *what == "includes")
            recorder->clearIncludeRules();
        else if (what && *what == "excludes")
            recorder->clearExcludeRules();
        else
            recorder->clearAllRules();
        return true;
    });

    lua.set_function("porttrace_filter_show", [getEmulator]() -> std::string {
        std::string error;
        auto* recorder = resolveRecorder(getEmulator(), nullptr, error);
        return recorder ? recorder->describeFilter() : error;
    });

    lua.set_function("porttrace_preset", [getEmulator](const std::string& name) -> bool {
        std::string error;
        auto* recorder = resolveRecorder(getEmulator(), nullptr, error);
        if (!recorder) return false;
        if (name == "all") recorder->presetAll();
        else if (name == "ay-only") recorder->presetAyOnly();
        else if (name == "fdc-only") recorder->presetFdcOnly();
        else if (name == "no-fdc") recorder->presetNoFdc();
        else if (name == "outs-only") recorder->presetOutsOnly();
        else if (name == "ins-only") recorder->presetInsOnly();
        else if (name == "unmapped") recorder->presetUnmapped();
        else return false;
        return true;
    });

    lua.set_function("porttrace_events", [getEmulator](sol::this_state s) -> sol::table {
        sol::state_view lua(s);
        sol::table result = lua.create_table();
        std::string error;
        auto* recorder = resolveRecorder(getEmulator(), nullptr, error);
        if (!recorder) return result;
        int index = 1;
        for (const auto& e : recorder->getAll())
            result[index++] = eventToTable(lua, e);
        return result;
    });

    lua.set_function("porttrace_events_last", [getEmulator](size_t count, sol::this_state s) -> sol::table {
        sol::state_view lua(s);
        sol::table result = lua.create_table();
        std::string error;
        auto* recorder = resolveRecorder(getEmulator(), nullptr, error);
        if (!recorder) return result;
        int index = 1;
        for (const auto& e : recorder->getLast(count))
            result[index++] = eventToTable(lua, e);
        return result;
    });

    lua.set_function("porttrace_save",
                     [getEmulator](const std::string& path, sol::optional<std::string> format) -> bool {
                         std::string error;
                         PortDecoder* decoder = nullptr;
                         auto* recorder = resolveRecorder(getEmulator(), &decoder, error);
                         if (!recorder) return false;
                         PortTraceExportFormat fmt = PortTraceExportFormat::JSON;
                         if (format && *format == "csv")
                             fmt = PortTraceExportFormat::CSV;
                         else if (format && (*format == "bin" || *format == "binary"))
                             fmt = PortTraceExportFormat::Binary;
                         return recorder->saveToFile(path, fmt, decoder->getPortTraceSessionInfo());
                     });
}

}  // namespace LuaPortTrace
