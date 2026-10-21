#pragma once

/// @file session_helper.h
/// @brief Automation sessions on the WebAPI (execution arbiter design §6a, X7): a client opens one with
/// POST /api/v1/emulator/{id}/control/session and sends its id in the `X-Unreal-Session` header on every request.
/// The session is the emulator's run-control claim: its requests run, step and resume while it holds control;
/// another surface's are refused. When the user takes control back at the GUI, every request of the session is
/// answered 409 `revoked_by_user` with `retry: false` - the client stops, it does not retry and does not take
/// control again without asking the user.

#include <optional>
#include <string>

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include "common/uuid.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/automationgate.h"
#include "emulator/execution/executionarbiter.h"

namespace webapi_session
{

constexpr const char* kHeader = "X-Unreal-Session";

/// The session id the request carries, if any (a malformed one counts as none)
inline std::optional<unreal::UUID> SessionOf(const drogon::HttpRequestPtr& req)
{
    if (!req)
        return std::nullopt;
    const std::string& text = req->getHeader(kHeader);
    if (text.empty())
        return std::nullopt;
    const unreal::UUID id(text);
    if (id.isNil())
        return std::nullopt;
    return id;
}

/// The request belongs to the session that holds this emulator's run control
inline bool HoldsControl(const drogon::HttpRequestPtr& req, EmulatorContext* context)
{
    const std::optional<unreal::UUID> id = SessionOf(req);
    return id && context && context->HasRunControl(*id);
}

/// Who asks, for the execution arbiter: the session (its surface and id) when the request carries the session that
/// holds control, a companion client (companion_helper.h) as itself, otherwise the surface alone
inline execution::Requester RequesterOf(const drogon::HttpRequestPtr& req, EmulatorContext* context,
                                        execution::Origin fallback = execution::Origin::WebApi)
{
    if (req && req->attributes()->find("unreal.companion"))
        return execution::Requester{execution::Origin::Companion, {}};
    const std::optional<unreal::UUID> id = SessionOf(req);
    if (id && context && context->HasRunControl(*id))
        return execution::Requester{execution::OriginFromName(context->GetRunControlState().surfaceLabel),
                                    id->toString()};
    return execution::Requester{fallback, {}};
}

/// The answer to a request of a session the user took control back from. Final: the client must not retry
inline Json::Value RevokedBody()
{
    Json::Value body;
    body["error"] = "revoked_by_user";
    body["retry"] = false;
    body["message"] =
        "The user took control back (at the GUI or from a companion): this automation session has ended. Stop. Do not "
        "retry, and do not take control again without asking the user.";
    return body;
}

/// Who drives the machine without a session, for the automation gate (automationgate.h): the surface ("mcp" when
/// the MCP server calls, else "webapi"), a client name (the MCP agent, or the User-Agent's product) and the peer host.
/// One identity is one implicit session
struct GateClient
{
    std::string surface;
    std::string client;
    std::string host;
    std::string peer;      // host:port, what the GUI shows
    std::string identity;
};

/// Request attributes the WebSocket sets on a call it forwards: the socket's peer (host:port) and host
constexpr const char* kPeerAttribute = "unreal.peer";
constexpr const char* kPeerHostAttribute = "unreal.peer_host";

inline GateClient GateClientOf(const drogon::HttpRequestPtr& req)
{
    GateClient c;
    const bool mcp = req->getHeader("X-Unreal-Source") == "mcp";
    c.surface = mcp ? "mcp" : "webapi";
    c.client = mcp ? std::string("MCP agent") : automation_gate::ClientFromUserAgent(req->getHeader("User-Agent"));
    // A call the WebSocket forwarded (api-websocket.cpp) has no peer of its own: the socket's, as attributes
    const auto& attributes = req->attributes();
    c.host = attributes->find(kPeerHostAttribute) ? attributes->get<std::string>(kPeerHostAttribute)
                                                  : req->getPeerAddr().toIp();
    c.peer = attributes->find(kPeerAttribute) ? attributes->get<std::string>(kPeerAttribute)
                                              : req->getPeerAddr().toIpPort();
    c.identity = c.surface + "|" + c.host + "|" + c.client;
    return c;
}

/// Request attributes the gate leaves for the reply (the post-handling advice)
constexpr const char* kImplicitSessionAttribute = "unreal.implicit_session";
constexpr const char* kImplicitOpenedAttribute = "unreal.implicit_opened";

}  // namespace webapi_session
