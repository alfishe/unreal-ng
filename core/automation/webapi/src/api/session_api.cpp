// Automation sessions on the WebAPI (execution arbiter design §6a, X7): open, read and end the session that holds an
// emulator's run control. The session's requests carry its id in the X-Unreal-Session header (automation-webapi.cpp
// answers a taken-back session's requests revoked_by_user and renews a live one's idle lease)

#include "../common/session_helper.h"
#include "../emulator_api.h"

#include <chrono>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

using namespace drogon;

namespace api
{
namespace v1
{

namespace
{

void ReplyJson(std::function<void(const HttpResponsePtr&)>& callback, const Json::Value& body, HttpStatusCode status)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(status);
    callback(resp);
}

Json::Value SessionJson(const EmulatorContext::RunControlState& state, const std::string& emulatorId)
{
    Json::Value out;
    out["emulator_id"] = emulatorId;
    out["held"] = state.claimed;
    if (state.claimed)
    {
        out["surface"] = state.surfaceLabel;
        out["client"] = state.client;
        out["peer"] = state.peer;
        out["since_ms"] = static_cast<Json::Int64>(state.sinceMs);
        out["lease_seconds"] = state.leaseMs / 1000;
        out["lease_seconds_left"] = (state.leaseLeftMs + 999) / 1000;
        out["implicit"] = state.implicit;
        out["holder"] = webapi_session::HolderJson(state);
    }
    Json::Value cooldowns(Json::arrayValue);
    for (const auto& [key, secondsLeft] : state.cooldowns)
    {
        Json::Value c;
        c["client"] = key;
        c["seconds_left"] = static_cast<Json::Int64>(secondsLeft);
        cooldowns.append(c);
    }
    out["retake_cooldowns"] = cooldowns;
    return out;
}

}  // namespace

/// @brief POST /api/v1/emulator/{id}/control/session - open an automation session: take the emulator's run control.
/// Body (optional): {client: "<name>", surface: "webapi" | "mcp", lease_seconds: 60 (0 = none)}. 200: {session, ...};
/// 409 held_by_other (another surface holds it) or revoked_cooldown (the user took control back from this client a
/// short while ago) - both with retry: false. An implicit session of another client gives way (took_over names it):
/// it only made that client visible; an explicit session is never taken over
void EmulatorAPI::postControlSession(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator || !emulator->GetContext())
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;
        return ReplyJson(callback, error, HttpStatusCode::k404NotFound);
    }
    const auto json = req->getJsonObject();
    std::string client = json && json->isMember("client") ? (*json)["client"].asString() : std::string();
    std::string surface = json && json->isMember("surface") ? (*json)["surface"].asString() : std::string("webapi");
    if (surface != "webapi" && surface != "mcp")
        surface = "webapi";
    const uint32_t leaseSeconds = json && json->isMember("lease_seconds") ? (*json)["lease_seconds"].asUInt() : 60u;
    // Where the client is: the HTTP peer; the MCP server (loopback) passes its own client's
    std::string peer = req->getPeerAddr().toIpPort();
    if (surface == "mcp" && json && json->isMember("peer") && !(*json)["peer"].asString().empty())
        peer = (*json)["peer"].asString();

    EmulatorContext* context = emulator->GetContext();
    // The client drove the machine before taking control: the gate's implicit session for it gives way to this one
    context->ReleaseImplicitRunControl(webapi_session::GateClientOf(req).identity);
    // Another client's implicit session gives way too (the take says whose)
    const EmulatorContext::RunControlState before = context->GetRunControlState();
    const unreal::UUID session = unreal::UUID::Generate();
    std::string reason;
    if (!context->TakeRunControl(session, surface, &reason, client, std::chrono::milliseconds(uint64_t(leaseSeconds) * 1000),
                                 peer))
    {
        const bool cooldown = reason.rfind("revoked_cooldown", 0) == 0;
        Json::Value error;
        if (cooldown)
        {
            error["error"] = "revoked_cooldown";
            error["retry"] = false;
            error["message"] = reason;
        }
        else
            error = webapi_session::HeldByOtherBody(context->GetRunControlState(), reason);
        error["emulator_id"] = emulator->GetId();
        return ReplyJson(callback, error, HttpStatusCode::k409Conflict);
    }
    Json::Value out = SessionJson(context->GetRunControlState(), emulator->GetId());
    out["session"] = session.toString();
    out["header"] = webapi_session::kHeader;
    if (before.claimed && before.implicit)
        out["took_over"] = webapi_session::HolderJson(before);
    ReplyJson(callback, out, HttpStatusCode::k200OK);
}

/// @brief GET /api/v1/emulator/{id}/control/session - who holds run control (kind, lease left), since when, the
/// clients in a cooldown. `yours`: the session is the asking client's - the one in its X-Unreal-Session header, or the
/// implicit session the gate holds for it. The id (`session`) is shown only for the asker's own implicit session: an
/// id is a bearer token, never shown to another client, and the asker already goes on in that session without it
void EmulatorAPI::getControlSession(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                    const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator || !emulator->GetContext())
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;
        return ReplyJson(callback, error, HttpStatusCode::k404NotFound);
    }
    EmulatorContext* context = emulator->GetContext();
    Json::Value out = SessionJson(context->GetRunControlState(), emulator->GetId());
    const unreal::UUID ownImplicit = context->ImplicitRunControlOwner(webapi_session::GateClientOf(req).identity);
    out["yours"] = webapi_session::HoldsControl(req, context) || !ownImplicit.isNil();
    if (!ownImplicit.isNil())
        out["session"] = ownImplicit.toString();
    ReplyJson(callback, out, HttpStatusCode::k200OK);
}

/// @brief DELETE /api/v1/emulator/{id}/control/session - end the session in the X-Unreal-Session header (a no-op for
/// a session that is not the holder, a taken-back one included). Without the header (or with one that holds nothing):
/// the implicit session the gate holds for this client (it goes on in it without the id anyway); another client's session is never ended here -
/// the reply says who holds it and how to resolve it
void EmulatorAPI::deleteControlSession(const HttpRequestPtr& req,
                                       std::function<void(const HttpResponsePtr&)>&& callback,
                                       const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator || !emulator->GetContext())
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;
        return ReplyJson(callback, error, HttpStatusCode::k404NotFound);
    }
    EmulatorContext* context = emulator->GetContext();
    const std::optional<unreal::UUID> session = webapi_session::SessionOf(req);
    bool held = false;
    if (session)
    {
        held = context->HasRunControl(*session);
        context->ReleaseRunControl(*session);
    }
    // No header, or one of a session that is gone (expired, taken over): the client's own implicit session, if any
    if (!held)
        held = context->ReleaseImplicitRunControl(webapi_session::GateClientOf(req).identity);
    const EmulatorContext::RunControlState state = context->GetRunControlState();
    Json::Value out = SessionJson(state, emulator->GetId());
    out["released"] = held;
    if (!held)
        out["message"] = state.claimed ? std::string("This client holds no session here. ") +
                                             EmulatorContext::DescribeHolder(state)
                                       : std::string("This client holds no session here; run control is free");
    ReplyJson(callback, out, HttpStatusCode::k200OK);
}

}  // namespace v1
}  // namespace api
