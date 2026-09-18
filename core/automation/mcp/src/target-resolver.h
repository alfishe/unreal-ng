#pragma once

// Target resolution for MCP tools
//
// Every tool accepts "target" — an explicit emulator id or "auto". The auto mode:
//   - 0 instances: create + start a default 128K machine and use it
//   - 1 instance:  use it
//   - >1 instances: refuse with the list of candidates (agent must disambiguate)

#include "webapi-client.h"

#include <functional>
#include <string>

namespace mcp
{

/// region <TargetResolver>

class TargetResolver
{
public:
    /// done(ok, idOrError): ok=true → idOrError holds the emulator id;
    /// ok=false → idOrError holds a human-readable error already phrased for the agent
    using ResolveCallback = std::function<void(bool ok, const std::string& idOrError)>;

    /// Default model used when auto-creating an emulator (matches AGENTS.md model list)
    static constexpr const char* kDefaultAutoCreateModel = "128k";

    /// Resolves "auto" or an explicit emulator id to a concrete id
    static void Resolve(const std::string& target, IApiCaller& caller, ResolveCallback done);

    /// Extracts target from MCP tool arguments (defaults to "auto") and resolves it
    static void ResolveFromArgs(const Json::Value& args, IApiCaller& caller, ResolveCallback done);
};

/// endregion </TargetResolver>

} // namespace mcp
