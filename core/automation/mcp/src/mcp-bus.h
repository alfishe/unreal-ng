#pragma once

// MCP smart tool: bus_analyzer — every peripheral bus of the machine and its cards (the SPI masters of the
// Z-Controller, the Next, the DivMMC, the NeoGS): list, enable / disable / clear the trace and the checks, read
// events and findings, set an SD card's timing profile, export VCD / JSON lines. A thin adapter over the WebAPI
// /bus routes (DeviceState::Bus*). Design: docs/inprogress/2026-10-11-bus-analyzer/technical-design.md §7

#include <string>

#include <json/json.h>

#include "mcp-tools.h"

namespace mcp
{

/// Registers the bus_analyzer tool
void RegisterBusAnalyzer(ToolRegistry& registry);

/// The text the tool answers with for a WebAPI reply (exposed for tests)
std::string BusAnalyzerText(const std::string& action, const Json::Value& reply);

} // namespace mcp
