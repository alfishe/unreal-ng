#pragma once

// MCP tool: signature_info - media signatures (core/swsignature): of a host file, of the media loaded in a machine, and
// how two files relate. Every action is one SignatureControl verb over the WebAPI's /signature routes; the game profile
// a signature chooses is the controllers tool's match action.

#include "mcp-tools.h"

namespace mcp
{

/// Registers the signature_info tool
void RegisterSignatureInfo(ToolRegistry& registry);

/// The text an agent reads of one signature tree ({name, kind, format, size, sha256, content, keys, file_set, ...})
std::string SignatureText(const Json::Value& signature);

} // namespace mcp
