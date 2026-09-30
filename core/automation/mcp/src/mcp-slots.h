#pragma once

// MCP smart tool: media — the machine's media slots (floppy drives, SD cards,
// later tape / IDE / CD): list, insert, swap, eject, save, export, discard,
// rescan, create, protect. A thin adapter over the WebAPI /media routes, which
// are MediaControl (core/src/emulator/media/mediacontrol.h): same verbs,
// options, selectors and error codes as every other surface.
// Design: docs/inprogress/2026-09-28-storage-manager/media-control-design.md

#include <string>
#include <utility>
#include <vector>

#include "mcp-tools.h"

namespace mcp
{

/// Registers the media tool
void RegisterMediaSlots(ToolRegistry& registry);

/// The actions and their options the tool offers. MCP is a WebAPI client and
/// does not link the core, so this is a copy of MediaControl's table; a test
/// in core-tests (mcp-slots_test.cpp) keeps the two equal
const std::vector<std::pair<std::string, std::vector<std::string>>>& MediaToolActions();

/// The tape file extensions load_software accepts: a copy of the core's
/// TapeLoaderRegistry list, kept equal by the same test
const std::vector<std::string>& TapeExtensions();

} // namespace mcp
