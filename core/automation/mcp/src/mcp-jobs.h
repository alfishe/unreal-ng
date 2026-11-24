#pragma once

// MCP smart tool: jobs — the long verbs run as jobs (PLAN #122 S2): list, status, wait (a long-poll of up to 60 s),
// cancel, start. A thin adapter over the WebAPI /api/v1/jobs routes (jobs::Execute, core/src/emulator/jobs/jobcontrol.h).
// Design: docs/emulator/design/control-interfaces/jobs.md

#include <string>

#include <json/json.h>

#include "mcp-tools.h"

namespace mcp
{

/// Registers the jobs tool
void RegisterJobs(ToolRegistry& registry);

/// The tool's request for its arguments: method, path, body (exposed for tests); false with @p error on bad arguments
bool JobsRequest(const Json::Value& args, const std::string& emulatorId, std::string& method, std::string& path,
                 Json::Value& body, std::string& error);

} // namespace mcp
