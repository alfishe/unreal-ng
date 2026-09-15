#pragma once

// MCP smart tool: manage_symbols — labels, symbol files and source listings
//
// Wires the LabelManager surface (symbol-file load, label list/resolve) and
// the sjasmplus .lst listing surface (load, source_at, step_line, run_to_line)
// into one tool for source-level debugging workflows.

#include "mcp-tools.h"

namespace mcp
{

/// Registers the manage_symbols tool
void RegisterManageSymbols(ToolRegistry& registry);

} // namespace mcp
