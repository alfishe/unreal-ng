#pragma once

// MCP smart tools: debug_code + analyze_performance + analysis
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

/// Registers the analysis tool: the analysis layers (docs/emulator/analysis/analysis-layers.md), one tool with an
/// action (start, stop, status, explain); the calls are counted per action (host.clients.mcp.tools) so a split into
/// several tools can be decided on use
void RegisterAnalysis(ToolRegistry& registry);

} // namespace mcp
