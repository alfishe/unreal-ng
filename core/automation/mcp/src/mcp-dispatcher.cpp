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
    "control_execution, inspect_state, type_input, mouse_input, joystick_input. Time-travel debugging: time_travel (start recording, "
    "run, stop, then find_last / reverse_continue / seek / step backward; inspect_state aspect 'ttd' shows the "
    "session). While recording, speed is held at 1x and turbo / fast loaders are off; loading media wipes the "
    "history. RZX input recordings: rzx_playback (play / stop / status). Assembler sources (decode, encode, convert, the files "
    "of a disk): asm_source. Router tools: search_api / invoke_api expose the "
    "full WebAPI (see openapi.json). Resolve the machine with target:\"auto\" unless several "
    "instances run. Resources: unreal://keyboard-layout, unreal://basic-reference, unreal://z80-isa, "
    "unreal://trdos-commands, unreal://memory-map, unreal://emulator-state. "
    "Automation sessions: control_execution take_control opens one (the user sees it in the GUI, whose controls lock); "
    "release_control ends it. Driving one emulator from a host script too: take_control here (it takes over a "
    "script's implicit session) and send its session id from the script (X-Unreal-Session). If a call answers "
    "revoked_by_user, the user took control back: stop, do not retry, and ask the user before taking control again.";

/// Largest structuredContent echoed into content[] as text; a larger one is cut with a note (the full body stays in
/// structuredContent)
constexpr size_t kStructuredEchoLimit = 64 * 1024;

/// The structuredContent as compact JSON text for content[] (response_format "json"): MCP (2025-06-18, Structured
/// Content) has a tool returning structured content also return it serialized in a TextContent block. Clients that hand
/// the model only content[] (claude.ai connectors) see the text summary otherwise. A picture sent as an image block is
/// not repeated as base64 text
std::string EchoStructured(const Json::Value& structured, const std::string& imageData)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["emitUTF8"] = true;
    std::string json;
    if (!imageData.empty() && structured.isObject() && structured.get("data", "").asString() == imageData)
    {
        Json::Value copy = structured;
        copy["data"] = "[base64 picture: see the image content block]";
        json = Json::writeString(builder, copy);
    }
    else
    {
        json = Json::writeString(builder, structured);
    }
    if (json.size() > kStructuredEchoLimit)
    {
        const size_t total = json.size();
        size_t cut = kStructuredEchoLimit;
        while (cut > 0 && (static_cast<unsigned char>(json[cut]) & 0xC0) == 0x80)
            --cut;   // never split a UTF-8 sequence
        json.resize(cut);
        json += "\n... [truncated: " + std::to_string(total) + " bytes of JSON, first " +
                std::to_string(cut) + " shown; ask for a narrower range]";
    }
    return json;
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
        if (params.isMember("clientInfo") && params["clientInfo"].isObject())
        {
            std::string name = params["clientInfo"].get("name", "").asString();
            const std::string version = params["clientInfo"].get("version", "").asString();
            if (!version.empty())
                name += " " + version;
            std::lock_guard<std::mutex> lock(_clientMutex);
            _clientName = name;
        }
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
        bool replyJson = false;
        if (argumentsCopy->isObject() && argumentsCopy->isMember(kResponseFormatArg))
        {
            const Json::Value format = (*argumentsCopy)[kResponseFormatArg];
            argumentsCopy->removeMember(kResponseFormatArg);
            if (!format.isString() || (format.asString() != "text" && format.asString() != "json"))
            {
                // A tool error, not a JSON-RPC one: the model reads it and corrects the call
                Json::Value payload;
                Json::Value text;
                text["type"] = "text";
                text["text"] = std::string(kResponseFormatArg) + " must be \"text\" or \"json\"";
                payload["content"].append(text);
                payload["isError"] = true;
                done(MakeRpcResult(id, std::move(payload)));
                return;
            }
            replyJson = format.asString() == "json";
        }
        // An argument the schema does not name is a typo ("addr" for "address"): ignored, it would leave the tool's
        // default in place and answer something that looks right (a read from 0). Names starting with '_' are internal;
        // 'target' is taken by every tool (agents send target "auto" everywhere, a tool without a machine ignores it)
        const Json::Value& properties = tool->inputSchema["properties"];
        if (argumentsCopy->isObject() && properties.isObject())
        {
            for (const std::string& key : argumentsCopy->getMemberNames())
            {
                if (key.empty() || key[0] == '_' || key == "target" || properties.isMember(key))
                    continue;
                std::string known;
                for (const std::string& property : properties.getMemberNames())
                    known += (known.empty() ? "" : ", ") + property;
                Json::Value payload;
                Json::Value text;
                text["type"] = "text";
                text["text"] = name + ": unknown argument '" + key + "' (arguments: " + known + ")";
                payload["content"].append(text);
                payload["isError"] = true;
                done(MakeRpcResult(id, std::move(payload)));
                return;
            }
        }
        if (name == "control_execution" && argumentsCopy->isObject() && !argumentsCopy->isMember("_mcp_client"))
        {
            std::lock_guard<std::mutex> lock(_clientMutex);
            if (!_clientName.empty())
                (*argumentsCopy)["_mcp_client"] = _clientName;
        }
        try
        {
            tool->handler(*argumentsCopy, caller, [argumentsCopy, id, done, &caller, replyJson](ToolResult result) {
                // The user took control back at the GUI during this call (execution arbiter design §6a): whatever the
                // tool made of the 409, the agent gets the one answer - stop, do not retry, ask the user
                if (caller.TakeRevoked())
                    result = ToolResult::Error(
                        "revoked_by_user: the user took control of the emulator back (at its GUI or from a companion); "
                        "your automation session has ended. Stop. Do not retry this call and do not take control again "
                        "(control_execution take_control) without asking the user first.");
                // The gate opened a session for this call: advise the agent to take control explicitly (once)
                if (const std::string advice = caller.TakeImplicitAdvice(); !advice.empty())
                    result.text += "\n\n" + advice;
                Json::Value payload;
                Json::Value text;
                text["type"] = "text";
                text["text"] = result.text;
                payload["content"].append(text);
                if (!result.imageData.empty() && !result.isError)
                {
                    Json::Value image;
                    image["type"] = "image";
                    image["data"] = result.imageData;
                    image["mimeType"] = result.imageMimeType;
                    payload["content"].append(image);
                }
                if (!result.structured.isNull())
                {
                    if (replyJson)
                    {
                        Json::Value echo;
                        echo["type"] = "text";
                        echo["text"] = EchoStructured(result.structured, result.imageData);
                        payload["content"].append(echo);
                    }
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
