#pragma once

/// @file companion_helper.h
/// @brief Companion clients on the WebAPI (docs/inprogress/2026-10-16-companion-client): a paired device sends
/// `Authorization: Bearer <token>` and acts as the user's hands - its changes open no session. The automation gate
/// (automation-webapi.cpp) authenticates the token and leaves the companion on the request for the handlers.

#include <optional>
#include <string>

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include "emulator/emulatorcontext.h"
#include "emulator/execution/automationgate.h"
#include "emulator/execution/companionregistry.h"

namespace api::v1
{
/// The body of GET /api/v1/companions/me for this companion (companion_api.cpp); the events channel sends the same
Json::Value CompanionMeJson(const companion::CompanionInfo& companion);
}  // namespace api::v1

namespace webapi_companion
{

/// The request attribute the gate leaves: the authenticated companion::CompanionInfo
constexpr const char* kAttribute = "unreal.companion";

/// The bearer token of the request, if it carries one ("Authorization: Bearer <token>", the scheme in any case)
inline std::optional<std::string> BearerOf(const drogon::HttpRequestPtr& req)
{
    if (!req)
        return std::nullopt;
    const std::string& header = req->getHeader("Authorization");
    if (header.size() <= 7)
        return std::nullopt;
    std::string scheme = header.substr(0, 7);
    for (char& c : scheme)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (scheme != "bearer ")
        return std::nullopt;
    const size_t start = header.find_first_not_of(' ', 7);
    if (start == std::string::npos)
        return std::nullopt;
    const size_t end = header.find_last_not_of(' ');
    return header.substr(start, end - start + 1);
}

/// The companion the gate authenticated for this request
inline std::optional<companion::CompanionInfo> CompanionOf(const drogon::HttpRequestPtr& req)
{
    if (!req || !req->attributes()->find(kAttribute))
        return std::nullopt;
    return req->attributes()->get<companion::CompanionInfo>(kAttribute);
}

inline Json::Value ScopesJson(uint32_t scopes)
{
    Json::Value list(Json::arrayValue);
    for (const std::string& name : automation_gate::CompanionScopeNames(scopes))
        list.append(name);
    return list;
}

/// A companion as every listing shows it: never its token
inline Json::Value CompanionJson(const companion::CompanionInfo& info)
{
    Json::Value out;
    out["id"] = info.id;
    out["name"] = info.name;
    out["device"] = info.device;
    out["scopes"] = ScopesJson(info.scopes);
    out["source"] = info.source;
    out["paired_ms"] = static_cast<Json::Int64>(info.pairedMs);
    out["last_seen_ms"] = static_cast<Json::Int64>(info.lastSeenMs);
    return out;
}

/// An error body: {error, message, retry: false}
inline Json::Value ErrorBody(const std::string& error, const std::string& message)
{
    Json::Value body;
    body["error"] = error;
    body["retry"] = false;
    body["message"] = message;
    return body;
}

/// Who holds the instance's run control, for held_by_session and `me`
inline Json::Value HolderJson(const EmulatorContext::RunControlState& state)
{
    Json::Value holder;
    holder["surface"] = state.surfaceLabel;
    holder["client"] = state.client;
    holder["peer"] = state.peer;
    holder["since_ms"] = static_cast<Json::Int64>(state.sinceMs);
    holder["implicit"] = state.implicit;
    return holder;
}

/// The holder's name for a message: "MCP agent", "mcp", ...
inline std::string HolderName(const EmulatorContext::RunControlState& state)
{
    return state.client.empty() ? state.surfaceLabel : state.client;
}

/// 409 held_by_session: a session holds the instance; the companion may take control back when it has `takeback`
inline Json::Value HeldBody(const EmulatorContext::RunControlState& state, const companion::CompanionInfo& companion)
{
    const bool takeBack = (companion.scopes & automation_gate::ScopeTakeBack) != 0;
    Json::Value body = ErrorBody("held_by_session",
                                 HolderName(state) + " controls the emulator" +
                                     (takeBack ? "; take control back first (POST /control/take-back)"
                                               : "; this companion may not take control back"));
    body["holder"] = HolderJson(state);
    body["take_back"] = takeBack;
    return body;
}

/// 403 companion_forbidden: `required` = the scope the action needs, which the user did not grant
inline Json::Value ForbiddenBody(uint32_t required)
{
    const std::string name = automation_gate::CompanionScopeName(required);
    Json::Value body = ErrorBody("companion_forbidden", "This companion may not use '" + name +
                                                            "': the user did not grant it at pairing");
    body["required_scope"] = name;
    return body;
}

}  // namespace webapi_companion
