#pragma once

// MCP tools of the GUI control plane (gui-control-plane design §4.10): one tool per interface of the catalogue the
// program registered (gui_memory, gui_disassembly, gui_debugger, gui_screen, gui_tilegrid, gui_window, gui_unrealqt,
// ...; `action` is the command), gui_list and gui_state. A thin adapter over /api/v1/gui: the same commands, targets
// and errors as `unreal-gui`, the CLI `gui`, Lua / Python gui_call(). No catalogue (headless): no tool.

#include "mcp-tools.h"

#include <string>

namespace mcp
{

/// Registers the tools of the catalogue's interfaces and gui_list / gui_state; nothing for an empty or bad catalogue
void RegisterGui(ToolRegistry& registry, const std::string& catalogueJson);

} // namespace mcp
