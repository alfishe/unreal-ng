#pragma once

// MCP tool: vdac4_state - the TS-Conf VDAC4 board (Tang FT812 core + ESP32-S3 running apps such as DOOM): the Tang's
// personality, the ESP's phase, the running app, the link, the audio player and the ESP log. A thin adapter over the
// WebAPI GET /vdac4/state (Vdac4Control::GetState, safe to poll).
// Design: docs/inprogress/2026-11-15-tsconf-vdac4/design.md §10.2

#include <string>

#include <json/json.h>

#include "mcp-tools.h"

namespace mcp
{

/// Registers the vdac4_state tool
void RegisterVdac4State(ToolRegistry& registry);

/// The text the tool answers with for a GET /vdac4/state reply; `logLines` = the newest log lines shown (exposed for
/// tests)
std::string Vdac4StateText(const Json::Value& reply, size_t logLines);

} // namespace mcp
