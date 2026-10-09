#pragma once

// MCP tool: asm_source - sources of ZX Spectrum assemblers (unreal-asm): formats, dialects, the files of a disk,
// detect, decode, encode, convert. Every action is one AsmControl verb over the WebAPI's /asm routes.

#include "mcp-tools.h"

namespace mcp
{

/// Registers the asm_source tool
void RegisterAsmSource(ToolRegistry& registry);

} // namespace mcp
