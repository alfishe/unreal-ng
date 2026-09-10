#pragma once

/// @file python_porttrace.h
/// @brief pybind11 bindings for the Port Diagnostic Recorder (PDR)
///
/// Direct Binding Mandate: native pybind11 access to the core recorder,
/// no WebAPI round-trip. Gated by the runtime FeatureManager feature
/// "porttrace" (alias "pt"). Design: docs/inprogress/2026-08-24-diagnostic-observability/

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <optional>
#include <stdexcept>
#include <string>

#include <base/featuremanager.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/ports/portdecoder.h>
#include <emulator/ports/portdiagrecorder.h>

namespace PythonBindings
{

namespace porttrace_detail
{

/// Resolve the recorder or raise a Python RuntimeError with an actionable message
inline PortDiagnosticRecorder* requireRecorder(Emulator& self, PortDecoder** outDecoder = nullptr)
{
    auto* context = self.GetContext();
    if (!context || !context->pPortDecoder)
        throw std::runtime_error("Port decoder not available");
    if (!context->pFeatureManager || !context->pFeatureManager->isEnabled(Features::kPortTrace))
        throw std::runtime_error("porttrace feature is disabled - enable with feature_set('porttrace', True)");

    PortDiagnosticRecorder* recorder = context->pPortDecoder->getPortTraceRecorder();
    if (!recorder)
        throw std::runtime_error("Port trace recorder not instantiated");

    if (outDecoder)
        *outDecoder = context->pPortDecoder;
    return recorder;
}

inline pybind11::dict eventToDict(const PortTraceEvent& e)
{
    pybind11::dict d;
    d["timestamp"] = e.timestamp;
    d["frame"] = e.frameNumber;
    d["raw_port"] = e.rawPort;
    d["decoded_port"] = e.decodedPort;
    d["decode_rule"] = e.decodeRuleIndex;
    d["value"] = e.value;
    d["pc"] = e.pc;
    d["device"] = std::string(PortDiagnosticRecorder::DeviceIdToString(e.deviceId));
    d["direction"] = e.isOut() ? "OUT" : "IN";
    d["decoded"] = e.wasDecoded();
    d["had_handler"] = e.hadHandler();
    d["beta128_gated"] = e.wasBeta128Gated();
    d["handled_inline"] = e.wasHandledInline();
    d["cf_trdos"] = e.cfTrdosActive();
    d["via_legacy"] = (e.flags & PortTraceFlags::kViaLegacyBasePath) != 0;
    return d;
}

/// Build a compound filter rule from Python kwargs-style optionals
inline PortTraceFilterRule buildRule(std::optional<uint16_t> port, std::optional<uint16_t> raw,
                                     std::optional<std::string> device, std::optional<std::string> direction,
                                     std::optional<std::pair<uint16_t, uint16_t>> pc,
                                     std::optional<std::pair<uint16_t, uint16_t>> value, bool unmapped)
{
    PortTraceFilterRule rule;
    rule.decodedPort = port;
    rule.rawPort = raw;

    if (device)
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
            throw std::invalid_argument("Unknown device: " + *device);
    }

    if (direction)
    {
        if (*direction == "in" || *direction == "IN")
            rule.directionOut = false;
        else if (*direction == "out" || *direction == "OUT")
            rule.directionOut = true;
        else
            throw std::invalid_argument("direction must be 'in' or 'out'");
    }

    rule.pcRange = pc;
    if (value)
    {
        if (value->first > 0xFF || value->second > 0xFF)
            throw std::invalid_argument("value range must be 8-bit");
        rule.valueRange = {static_cast<uint8_t>(value->first), static_cast<uint8_t>(value->second)};
    }
    rule.unmappedOnly = unmapped;

    return rule;
}

}  // namespace porttrace_detail

/// Register port trace methods on the Emulator Python class
template <typename EmulatorClass>
inline void registerPortTraceBindings(EmulatorClass& emulatorClass)
{
    namespace py = pybind11;
    using porttrace_detail::buildRule;
    using porttrace_detail::eventToDict;
    using porttrace_detail::requireRecorder;

    emulatorClass
        // ── Session lifecycle ──
        .def("porttrace_start", [](Emulator& self) { requireRecorder(self)->start(); },
             "Start port trace capture (clears buffer)")
        .def("porttrace_stop", [](Emulator& self) { requireRecorder(self)->stop(); },
             "Stop port trace capture (data preserved)")
        .def("porttrace_pause", [](Emulator& self) { requireRecorder(self)->pause(); }, "Pause capture")
        .def("porttrace_resume", [](Emulator& self) { requireRecorder(self)->resume(); }, "Resume capture")
        .def("porttrace_clear", [](Emulator& self) { requireRecorder(self)->clear(); }, "Purge event buffer")

        // ── Status ──
        .def("porttrace_status",
             [](Emulator& self) -> py::dict {
                 PortDecoder* decoder = nullptr;
                 auto* recorder = requireRecorder(self, &decoder);
                 py::dict d;
                 switch (recorder->getSessionState())
                 {
                     case PortTraceSessionState::Capturing: d["state"] = "capturing"; break;
                     case PortTraceSessionState::Paused:    d["state"] = "paused"; break;
                     default:                               d["state"] = "stopped"; break;
                 }
                 d["events"] = recorder->eventCount();
                 d["capacity"] = recorder->capacity();
                 d["total_produced"] = recorder->totalProduced();
                 d["total_evicted"] = recorder->totalEvicted();
                 d["total_filtered"] = recorder->totalFiltered();
                 d["auto_stopped"] = recorder->wasAutoStopped();
                 d["overflow"] =
                     recorder->overflowMode() == PortTraceOverflowMode::Ring ? "ring" : "stop-when-full";
                 d["filter"] = recorder->describeFilter();

                 const PortActivitySummary& summary = decoder->getActivitySummary();
                 py::dict frame;
                 frame["frame"] = summary.frameNumber;
                 frame["in"] = summary.inCount;
                 frame["out"] = summary.outCount;
                 frame["unmapped_in"] = summary.unmappedInCount;
                 frame["unmapped_out"] = summary.unmappedOutCount;
                 frame["beta128_gated"] = summary.beta128GatedCount;
                 d["activity"] = frame;
                 return d;
             },
             "Session state, counters, filter, and frame activity summary")

        // ── Configuration (only while stopped) ──
        .def("porttrace_set_capacity",
             [](Emulator& self, size_t events) -> bool { return requireRecorder(self)->setCapacity(events); },
             "Set ring buffer capacity (only while stopped)", py::arg("events"))
        .def("porttrace_set_overflow",
             [](Emulator& self, const std::string& mode) -> bool {
                 if (mode == "ring")
                     return requireRecorder(self)->setOverflowMode(PortTraceOverflowMode::Ring);
                 if (mode == "stop")
                     return requireRecorder(self)->setOverflowMode(PortTraceOverflowMode::StopWhenFull);
                 throw std::invalid_argument("overflow mode must be 'ring' or 'stop'");
             },
             "Set overflow mode: 'ring' (evict oldest) or 'stop' (stop-when-full)", py::arg("mode"))

        // ── Filtering (compound rules: kwargs = AND, separate calls = OR) ──
        .def("porttrace_include",
             [](Emulator& self, std::optional<uint16_t> port, std::optional<uint16_t> raw,
                std::optional<std::string> device, std::optional<std::string> direction,
                std::optional<std::pair<uint16_t, uint16_t>> pc,
                std::optional<std::pair<uint16_t, uint16_t>> value, bool unmapped) {
                 requireRecorder(self)->addIncludeRule(
                     buildRule(port, raw, device, direction, pc, value, unmapped));
             },
             "Add compound include rule (all given kwargs must match; separate calls OR together)",
             py::arg("port") = py::none(), py::arg("raw") = py::none(), py::arg("device") = py::none(),
             py::arg("direction") = py::none(), py::arg("pc") = py::none(), py::arg("value") = py::none(),
             py::arg("unmapped") = false)
        .def("porttrace_exclude",
             [](Emulator& self, std::optional<uint16_t> port, std::optional<uint16_t> raw,
                std::optional<std::string> device, std::optional<std::string> direction,
                std::optional<std::pair<uint16_t, uint16_t>> pc,
                std::optional<std::pair<uint16_t, uint16_t>> value, bool unmapped) {
                 requireRecorder(self)->addExcludeRule(
                     buildRule(port, raw, device, direction, pc, value, unmapped));
             },
             "Add compound exclude rule (exclude always wins over include)", py::arg("port") = py::none(),
             py::arg("raw") = py::none(), py::arg("device") = py::none(), py::arg("direction") = py::none(),
             py::arg("pc") = py::none(), py::arg("value") = py::none(), py::arg("unmapped") = false)
        .def("porttrace_filter_clear",
             [](Emulator& self, const std::string& what) {
                 auto* recorder = requireRecorder(self);
                 if (what == "includes")
                     recorder->clearIncludeRules();
                 else if (what == "excludes")
                     recorder->clearExcludeRules();
                 else
                     recorder->clearAllRules();
             },
             "Clear filter rules ('all', 'includes' or 'excludes')", py::arg("what") = "all")
        .def("porttrace_filter_show",
             [](Emulator& self) -> std::string { return requireRecorder(self)->describeFilter(); },
             "Human-readable filter description")
        .def("porttrace_preset",
             [](Emulator& self, const std::string& name) {
                 auto* recorder = requireRecorder(self);
                 if (name == "all") recorder->presetAll();
                 else if (name == "ay-only") recorder->presetAyOnly();
                 else if (name == "fdc-only") recorder->presetFdcOnly();
                 else if (name == "no-fdc") recorder->presetNoFdc();
                 else if (name == "outs-only") recorder->presetOutsOnly();
                 else if (name == "ins-only") recorder->presetInsOnly();
                 else if (name == "unmapped") recorder->presetUnmapped();
                 else if (name == "no-fe") recorder->presetNoFe();
                 else if (name == "sound") recorder->presetSound();
                 else if (name == "paging") recorder->presetPaging();
                 else throw std::invalid_argument("Unknown preset: " + name);
             },
             "Apply filter preset: all|ay-only|fdc-only|no-fdc|no-fe|sound|paging|outs-only|ins-only|unmapped", py::arg("name"))

        // ── Retrieval ──
        .def("porttrace_events",
             [](Emulator& self) -> py::list {
                 py::list result;
                 for (const auto& e : requireRecorder(self)->getAll())
                     result.append(eventToDict(e));
                 return result;
             },
             "All buffered events as list of dicts")
        .def("porttrace_events_last",
             [](Emulator& self, size_t count) -> py::list {
                 py::list result;
                 for (const auto& e : requireRecorder(self)->getLast(count))
                     result.append(eventToDict(e));
                 return result;
             },
             "Last N buffered events", py::arg("count"))
        .def("porttrace_events_since",
             [](Emulator& self, uint64_t timestamp) -> py::list {
                 py::list result;
                 for (const auto& e : requireRecorder(self)->getSince(timestamp))
                     result.append(eventToDict(e));
                 return result;
             },
             "Events with timestamp >= given absolute T-state", py::arg("timestamp"))

        // ── Persistence ──
        .def("porttrace_save",
             [](Emulator& self, const std::string& path, const std::string& format) -> bool {
                 PortDecoder* decoder = nullptr;
                 auto* recorder = requireRecorder(self, &decoder);
                 PortTraceExportFormat fmt = PortTraceExportFormat::JSON;
                 if (format == "csv")
                     fmt = PortTraceExportFormat::CSV;
                 else if (format == "bin" || format == "binary")
                     fmt = PortTraceExportFormat::Binary;
                 else if (format == "binz")
                     fmt = PortTraceExportFormat::BinaryCompressed;
                 else if (format != "json")
                     throw std::invalid_argument("format must be json/csv/bin/binz");
                 return recorder->saveToFile(path, fmt, decoder->getPortTraceSessionInfo());
             },
             "Save trace to file (json/csv/bin/binz — binz is zstd-compressed)", py::arg("path"),
             py::arg("format") = "json");
}

}  // namespace PythonBindings
