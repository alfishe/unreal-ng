#pragma once

// MCP smart tool: controllers — the machine's connectors and the retro devices in them (Kempston, Sinclair 1 / 2,
// Cursor, keys, Fuller, the Next's pads): list, get, plug, unplug, options, targets, auto; and the host side: devices,
// context, profiles, profile, save, delete, apply, bindings, bind, unbind, scheme, haptics, rumble, output, light,
// database, database-save, database-delete. A thin
// adapter over the WebAPI /controllers routes, which are ControllerControl (core/src/emulator/io/controllers/
// controllercontrol.h) and HostInputControl (core/controllers/src/hostinputcontrol.h): same verbs, options, selectors
// and error codes as every other surface.
// Design: docs/inprogress/2026-10-17-controller-manager/surfaces.md §7

#include <string>
#include <utility>
#include <vector>

#include "mcp-tools.h"

namespace mcp
{

/// Registers the controllers tool
void RegisterControllers(ToolRegistry& registry);

/// The actions and their options the tool offers. MCP is a WebAPI client and does not link the core, so this is a copy
/// of ControllerControl's table; a test in core-tests (mcp-controllers_test.cpp) keeps the two equal
const std::vector<std::pair<std::string, std::vector<std::string>>>& ControllerToolActions();
/// The host actions (profiles, bindings, devices: no emulator target), a copy of HostInputControl's table
const std::vector<std::pair<std::string, std::vector<std::string>>>& HostControllerToolActions();

} // namespace mcp
