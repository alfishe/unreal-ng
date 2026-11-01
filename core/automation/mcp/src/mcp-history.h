#pragma once

// MCP smart tool: device_history — the history of each device's own state machine (FDC commands, ...), by the
// device's registry id: list, read, config (off / ring / capture with any limit, a stream to a JSON Lines file),
// clear. A thin adapter over the WebAPI /history routes (HistoryControl, core/src/debugger/history/historycontrol.h).
// Design: docs/inprogress/2026-10-31-debugger-device-io/README.md

#include <string>

#include <json/json.h>

#include "mcp-tools.h"

namespace mcp
{

/// Registers the device_history tool
void RegisterDeviceHistory(ToolRegistry& registry);

/// The text the tool answers with for a WebAPI reply (exposed for tests)
std::string DeviceHistoryText(const std::string& action, const Json::Value& reply);

} // namespace mcp
