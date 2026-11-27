#pragma once

/// @file request_gates.h
/// @brief The checks every WebAPI request passes before routing (automation-webapi.cpp): the CORS preflight, a
/// companion's token and scopes, an automation session's revocation and lease, and the automation gate's implicit
/// session. A request the WebSocket forwards to its handler (api-websocket.cpp) skips drogon's sync advices, so the
/// socket calls this itself.

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace webapi_gate
{
/// null: go on to the handler; else the answer (an error, the preflight reply). A bad max_chars is a 400 here, before
/// the handler runs
drogon::HttpResponsePtr PassRequestGates(const drogon::HttpRequestPtr& req);

/// Answer budgets (docs/inprogress/2026-11-04-answer-budgets): with ?max_chars=N, a JSON or text/plain reply fitted in N
/// characters (a JSON object gets "truncated", the reply X-Unreal-Truncated: <full size>). Once per request: the
/// post-handling advice and the WebSocket's forward both call it
void FitAnswer(const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp);

/// The error code (debugerror.h): a JSON error reply (status >= 400, an "error" member, no "code") gets "code" from its
/// status ("revoked_by_user": revoked, "Run-control held...": run_control_held); /mcp keeps its JSON-RPC errors. The
/// post-handling advice and the WebSocket's forward both call it
void AddErrorCode(const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp);
}  // namespace webapi_gate
