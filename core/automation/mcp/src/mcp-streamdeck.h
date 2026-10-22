#pragma once

// MCP smart tool: streamdeck — the Stream Deck daemon (unreal-streamdeck) as unreal-qt supervises it (Stream Deck
// integration daemon.md §7): status, start, stop, restart, settings, pairing, pair, pair_cancel, revoke, brightness,
// udev. A thin adapter over the WebAPI /api/v1/streamdeck routes, which are companion::streamdeck::Call
// (core/src/emulator/companions/streamdeckapi.h): the same verbs, fields and errors as the CLI `streamdeck`, Lua and
// Python streamdeck() and the settings page. `pair` only asks: the user approves at the GUI.

#include <string>
#include <vector>

#include "mcp-tools.h"

namespace mcp
{

/// Registers the streamdeck tool
void RegisterStreamDeck(ToolRegistry& registry);

/// The tool's verbs: a copy of companion::streamdeck::Verbs() (MCP does not link the core); a test keeps them equal
const std::vector<std::string>& StreamDeckToolVerbs();

} // namespace mcp
