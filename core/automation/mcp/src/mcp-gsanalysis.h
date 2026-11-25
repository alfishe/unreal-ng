#pragma once

// MCP smart tool: gs_analysis - the GS card CPU analysis (gs-debugger P5.6; requirements §4.13 X8 / X10 / X11) of the
// GS slot's card (cpu gs) or the ZX-MultiSound's GS (cpu ms): the switch (status, on, off, reset, release), coverage /
// code-data log per physical page with the command handlers executed and the reads of never-written RAM, and the RAM
// heat map with the ranges the DAC (or another channel) read in the last N frames. A thin adapter over the WebAPI
// routes /state/audio/gs/analysis|coverage|heatmap and /control/audio/gs/analysis (core gsanalysisreport.h).

#include <string>
#include <vector>

#include "mcp-tools.h"

namespace mcp
{

/// Registers the gs_analysis tool
void RegisterGSAnalysis(ToolRegistry& registry);

/// The tool's actions: status, on, off, reset, release, coverage, heatmap
const std::vector<std::string>& GSAnalysisToolActions();

} // namespace mcp
