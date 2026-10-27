#pragma once

// MCP smart tool: actions — the host actions of the core ActionRegistry (control surfaces actions.md §6): list (the
// catalogue with whether each can run now, and why not) and run (one action by id with its value). A thin adapter
// over GET /api/v1/controllers/actions and POST /api/v1/controllers/actions/{id}: the same ids, values, gates and
// errors as a pad, the Surface Dial, a companion, the CLI `action` and the scripts' action().

#include "mcp-tools.h"

namespace mcp
{

/// Registers the actions tool
void RegisterActions(ToolRegistry& registry);

} // namespace mcp
