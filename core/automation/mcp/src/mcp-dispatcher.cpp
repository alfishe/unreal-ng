// MCP dispatcher implementation — see mcp-dispatcher.h

#include "mcp-dispatcher.h"

#include "mcp-protocol.h"
#include "mcp-resources.h"
#include "mcp-sse.h"

#include <memory>
#include <string>

namespace mcp
{

/// region <McpDispatcher>

namespace
{

/// Short agent-facing briefing returned by initialize
const char* const kServerInstructions =
    "Unreal-NG ZX Spectrum emulator control surface. Core tools: emulator_manage, load_software, "
    "control_execution, inspect_state, type_input. Router tools: search_api / invoke_api expose the "
    "full WebAPI (see openapi.json). Resolve the machine with target:\"auto\" unless several "
    "instances run. Resources: unreal://keyboard-layout, unreal://basic-reference, unreal://z80-isa, "
    "unreal://trdos-commands, unreal://memory-map, unreal://emulator-state.";

bool IsSupportedProtocolVersion(const std::string& version)
{
    for (const char* supported : kSupportedProtocolVersions)
    {
        if (version == supported)
        {
            return true;
        }
    }
    return false;
}

} // namespace

McpDispatcher::McpDispatcher(std::unique_ptr<ToolRegistry> registry)
    : _registry(std::move(registry))
{
}

void McpDispatcher::Dispatch(const Json::Value& request, IApiCaller& caller, ResponseCallback done)
{
    Dispatch(request, caller, std::move(done), NotifyCallback());
}

void McpDispatcher::Dispatch(const Json::Value& request, IApiCaller& caller, ResponseCallback done,
                             const NotifyCallback& notify)
{
    // Batching was removed in the 2025-03-26 revision of the spec
    if (!request.isObject())
    {
        done(MakeRpcError(Json::Value(), kInvalidRequest,
                          "Invalid Request: expected a single JSON-RPC object, batching is not supported"));
        return;
    }

    const bool hasId = request.isMember("id");
    const Json::Value id = hasId ? request["id"] : Json::Value();

    // Request-shape validation per JSON-RPC 2.0
    if (request.get("jsonrpc", "").asString() != "2.0" || !request.isMember("method") || !request["method"].isString())
    {
        done(MakeRpcError(id, kInvalidRequest, "Invalid Request: expected jsonrpc \"2.0\" and a string method"));
        return;
    }

    // Notifications never receive a response; every notification is a no-op
    // (notifications/initialized is the only one clients normally send)
    if (!hasId)
    {
        done(Json::Value());
        return;
    }

    const std::string method = request["method"].asString();
    const Json::Value& params = request.isMember("params") && request["params"].isObject()
                                    ? request["params"]
                                    : Json::Value::nullSingleton();

    if (method == "initialize")
    {
        Json::Value result;
        const std::string clientVersion = params.get("protocolVersion", "").asString();
        result["protocolVersion"] = IsSupportedProtocolVersion(clientVersion) ? clientVersion : std::string(kProtocolVersion);

        Json::Value tools;
        tools["listChanged"] = false;
        Json::Value resources;
        resources["subscribe"] = false;
        resources["listChanged"] = false;
        result["capabilities"]["tools"] = tools;
        result["capabilities"]["resources"] = resources;

        Json::Value serverInfo;
        serverInfo["name"] = kServerName;
        serverInfo["version"] = kServerVersion;
        result["serverInfo"] = serverInfo;
        result["instructions"] = kServerInstructions;

        done(MakeRpcResult(id, std::move(result)));
        return;
    }

    if (method == "ping")
    {
        done(MakeRpcResult(id, Json::Value(Json::objectValue)));
        return;
    }

    if (method == "tools/list")
    {
        Json::Value result;
        result["tools"] = _registry->ToolListJson();
        done(MakeRpcResult(id, std::move(result)));
        return;
    }

    if (method == "tools/call")
    {
        const std::string name = params.get("name", "").asString();
        const ToolDefinition* tool = _registry->Find(name);
        if (!tool)
        {
            done(MakeRpcError(id, kInvalidParams, "Unknown tool: " + name));
            return;
        }

        // Tool execution problems are reported through the tool result
        // (isError: true), never as JSON-RPC errors, per the MCP spec
        static const Json::Value kEmptyArguments(Json::objectValue);
        const Json::Value& arguments =
            params.isMember("arguments") && params["arguments"].isObject() ? params["arguments"] : kEmptyArguments;

        // Progress plumbing: the client opts in per-request via
        // params._meta.progressToken (copied — `params` does not outlive this
        // call). Without a token or notify sink the tool gets a no-op sink.
        ProgressFn progress;
        if (notify && params.isMember("_meta") && params["_meta"].isObject() &&
            params["_meta"].isMember("progressToken"))
        {
            auto token = std::make_shared<Json::Value>(params["_meta"]["progressToken"]);
            progress = [notify, token](double value, double total, const std::string& message) {
                notify(MakeProgressNotification(*token, value, total, message));
            };
        }
        else
        {
            progress = [](double, double, const std::string&) {};
        }

        // Tool handlers start asynchronous work and return immediately; the
        // request object backing `params` does not outlive this call. Keep a
        // copy of the arguments alive until the result callback fires so that
        // handlers may safely capture `args` by reference in callback chains.
        auto argumentsCopy = std::make_shared<Json::Value>(arguments);
        try
        {
            tool->handler(*argumentsCopy, caller, [argumentsCopy, id, done](ToolResult result) {
                Json::Value payload;
                Json::Value text;
                text["type"] = "text";
                text["text"] = result.text;
                payload["content"].append(text);
                if (!result.structured.isNull())
                {
                    payload["structuredContent"] = result.structured;
                }
                if (result.isError)
                {
                    payload["isError"] = true;
                }
                done(MakeRpcResult(id, std::move(payload)));
            }, progress);
        }
        catch (const std::exception& e)
        {
            // e.g. jsoncpp type errors from malformed argument values — report as
            // a tool execution error instead of killing the event-loop thread
            Json::Value payload;
            Json::Value text;
            text["type"] = "text";
            text["text"] = std::string("Tool crashed while processing arguments: ") + e.what();
            payload["content"].append(text);
            payload["isError"] = true;
            done(MakeRpcResult(id, std::move(payload)));
        }
        return;
    }

    if (method == "resources/list")
    {
        Json::Value result;
        result["resources"] = McpResources::ListJson();
        done(MakeRpcResult(id, std::move(result)));
        return;
    }

    if (method == "resources/read")
    {
        const std::string uri = params.get("uri", "").asString();
        if (uri.empty())
        {
            done(MakeRpcError(id, kInvalidParams, "Missing resource uri"));
            return;
        }

        McpResources::Read(uri, caller, [id, done](bool ok, Json::Value contents) {
            if (ok)
            {
                done(MakeRpcResult(id, std::move(contents)));
            }
            else
            {
                done(MakeRpcError(id, kInvalidParams, contents.asString()));
            }
        });
        return;
    }

    if (method == "prompts/list")
    {
        // Prompts capability is not advertised; an empty list keeps
        // generic clients happy
        Json::Value result;
        result["prompts"] = Json::Value(Json::arrayValue);
        done(MakeRpcResult(id, std::move(result)));
        return;
    }

    if (method == "prompts/get")
    {
        done(MakeRpcError(id, kInvalidParams, "Unknown prompt: this server exposes no prompts"));
        return;
    }

    done(MakeRpcError(id, kMethodNotFound, "Method not found: " + method));
}

/// endregion </McpDispatcher>

} // namespace mcp
