#pragma once

// MCP smart tools: debug_code + analyze_performance
//
// debug_code — low-level code workflows: disassemble, in-process assemble,
//              byte-pattern search and one-shot call tracing.
// analyze_performance — coverage (executed addresses), frame cost
//              (work-vs-idle t-states), profiler orchestration and one-shot
//              port I/O tracing.

#include "mcp-tools.h"

namespace mcp
{

/// Registers the debug_code tool
void RegisterDebugCode(ToolRegistry& registry);

/// Registers the analyze_performance tool
void RegisterAnalyzePerformance(ToolRegistry& registry);

} // namespace mcp
