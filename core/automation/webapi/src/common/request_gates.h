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
/// null: go on to the handler; else the answer (an error, the preflight reply)
drogon::HttpResponsePtr PassRequestGates(const drogon::HttpRequestPtr& req);
}  // namespace webapi_gate
