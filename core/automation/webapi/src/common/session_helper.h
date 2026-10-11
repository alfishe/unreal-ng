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
/// holds control, otherwise the surface alone
inline execution::Requester RequesterOf(const drogon::HttpRequestPtr& req, EmulatorContext* context,
                                        execution::Origin fallback = execution::Origin::WebApi)
{
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
        "The user took control back at the emulator's GUI: this automation session has ended. Stop. Do not retry, and "
        "do not take control again without asking the user.";
    return body;
}

}  // namespace webapi_session
