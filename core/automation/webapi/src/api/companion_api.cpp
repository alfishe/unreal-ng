// Companion clients on the WebAPI (docs/inprogress/2026-10-16-companion-client/protocol.md): pairing (a device asks,
// the user approves at the GUI, the device collects its token once), the companion's own view (`me`), listing and
// revoking, and the companion's take-back of a session. Approval has no route: only the GUI approves

#include "../common/companion_helper.h"
#include "../common/session_helper.h"
#include "../common/statenode_json.h"
#include "../emulator_api.h"

#include "emulator/actions/targetstate.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/companionactions.h"
#include "emulator/execution/companionregistry.h"

using namespace drogon;

namespace api
{
namespace v1
{

extern std::string stateToString(EmulatorStateEnum state);

namespace
{

void ReplyJson(std::function<void(const HttpResponsePtr&)>& callback, const Json::Value& body, HttpStatusCode status)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(status);
    callback(resp);
}

companion::CompanionRegistry& Registry()
{
    return companion::CompanionRegistry::Instance();
}

Json::Value ListJson()
{
    return StateNodeToJson(Registry().Report());
}

/// GET /api/v1/companions/me: the companion, its scopes, and per instance its state and who holds control
Json::Value MeJson(const companion::CompanionInfo& me)
{
    Json::Value out;
    out["companion"] = webapi_companion::CompanionJson(me);
    Json::Value emulators(Json::arrayValue);
    EmulatorManager* manager = EmulatorManager::GetInstance();
    const std::string selected = manager->GetSelectedEmulatorId();
    // The instance a surface following the focus drives (Stream Deck targeting.md): the focused window's
    const std::string followed = actions::TargetState::Instance().Followed();
    int index = 0;
    for (const std::string& id : manager->GetEmulatorIds())
    {
        std::shared_ptr<Emulator> emulator = manager->GetEmulator(id);
        EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
        if (!context)
            continue;
        Json::Value e;
        e["id"] = id;
        e["index"] = index++;
        const MachineIdentity identity = EmulatorManager::GetMachineIdentity(*emulator);
        e["model"] = identity.Valid ? Json::Value(identity.Model) : Json::Value();
        e["state"] = stateToString(emulator->GetState());
        e["selected"] = id == selected;
        e["followed"] = id == followed;
        const EmulatorContext::RunControlState state = context->GetRunControlState();
        Json::Value control = state.claimed ? webapi_companion::HolderJson(state) : Json::Value(Json::objectValue);
        control["held"] = state.claimed;
        control["can_act"] = !state.claimed;
        control["can_take_back"] = state.claimed && (me.scopes & automation_gate::ScopeTakeBack) != 0;
        e["control"] = control;
        // What this companion may do now, button by button (available, scope, reason)
        e["actions"] = StateNodeToJson(companion::ActionsReport(me, companion::ViewOf(*emulator)));
        emulators.append(e);
    }
    out["emulators"] = emulators;
    return out;
}

/// The companion of a request that needs one: 401 when there is none
bool RequireCompanion(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>& callback,
                      companion::CompanionInfo& out)
{
    if (const std::optional<companion::CompanionInfo> me = webapi_companion::CompanionOf(req))
    {
        out = *me;
        return true;
    }
    ReplyJson(callback,
              webapi_companion::ErrorBody("companion_required",
                                          "Only a companion client does this: send Authorization: Bearer <token>"),
              HttpStatusCode::k401Unauthorized);
    return false;
}

}  // namespace

Json::Value CompanionMeJson(const companion::CompanionInfo& companion)
{
    return MeJson(companion);
}

/// @brief GET /api/v1/companions - companions and pending pairings (never a token or a code);
/// DELETE /api/v1/companions - revoke every companion
void EmulatorAPI::companionsCollection(const HttpRequestPtr& req,
                                       std::function<void(const HttpResponsePtr&)>&& callback) const
{
    if (req->method() == drogon::HttpMethod::Delete)
    {
        Json::Value out;
        out["revoked"] = static_cast<Json::UInt>(Registry().RevokeAll());
        return ReplyJson(callback, out, HttpStatusCode::k200OK);
    }
    ReplyJson(callback, ListJson(), HttpStatusCode::k200OK);
}

/// @brief POST /api/v1/companions/pairings - a device asks to pair: {name, device?, scopes?};
/// GET /api/v1/companions/me | {cid} - the companion itself (with a token) or one companion;
/// DELETE /api/v1/companions/me | {cid} - unpair itself / revoke one (id or name)
void EmulatorAPI::companionItem(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& cid) const
{
    const drogon::HttpMethod method = req->method();
    if (cid == "pairings")
    {
        if (method != drogon::HttpMethod::Post)
            return ReplyJson(callback,
                             webapi_companion::ErrorBody("bad_request", "POST a pairing request: {name, device, scopes}"),
                             HttpStatusCode::k405MethodNotAllowed);
        auto json = req->getJsonObject();
        if (!json || !json->isObject() || !(*json)["name"].isString())
            return ReplyJson(callback, webapi_companion::ErrorBody("bad_request", "a pairing needs a name"),
                             HttpStatusCode::k400BadRequest);
        uint32_t scopes = 0;
        if (json->isMember("scopes"))
        {
            std::vector<std::string> names;
            const Json::Value& list = (*json)["scopes"];
            if (!list.isArray())
                return ReplyJson(callback, webapi_companion::ErrorBody("bad_request", "scopes is a list of names"),
                                 HttpStatusCode::k400BadRequest);
            for (const Json::Value& name : list)
                names.push_back(name.asString());
            const std::optional<uint32_t> parsed = companion::CompanionRegistry::ScopesFromNames(names);
            if (!parsed)
                return ReplyJson(callback,
                                 webapi_companion::ErrorBody("bad_request",
                                                             "unknown scope: use run, input, media, machine, rewind, "
                                                             "takeback or all"),
                                 HttpStatusCode::k400BadRequest);
            scopes = *parsed;
        }
        const companion::PairingRequestResult result =
            Registry().RequestPairing((*json)["name"].asString(), json->get("device", "").asString(), scopes,
                                      req->getPeerAddr().toIp());
        if (!result.error.empty())
        {
            Json::Value body = webapi_companion::ErrorBody(result.error, result.message);
            HttpStatusCode status = HttpStatusCode::k400BadRequest;
            if (result.error == "too_many_pairings")
            {
                body["retry"] = true;
                status = HttpStatusCode::k429TooManyRequests;
            }
            else if (result.error == "pairing_unavailable")
                status = HttpStatusCode::k503ServiceUnavailable;
            else if (result.error == "pairing_disabled")
                status = HttpStatusCode::k403Forbidden;
            return ReplyJson(callback, body, status);
        }
        Json::Value out;
        out["pairing_id"] = result.pairing.id;
        out["code"] = result.code;
        out["expires_in"] = result.pairing.expiresIn;
        out["scopes"] = webapi_companion::ScopesJson(result.pairing.scopes);
        return ReplyJson(callback, out, HttpStatusCode::k202Accepted);
    }

    if (cid == "me")
    {
        companion::CompanionInfo me;
        if (!RequireCompanion(req, callback, me))
            return;
        if (method == drogon::HttpMethod::Delete)
        {
            Json::Value out;
            out["revoked"] = Registry().Revoke(me.id) > 0;
            out["id"] = me.id;
            return ReplyJson(callback, out, HttpStatusCode::k200OK);
        }
        return ReplyJson(callback, MeJson(me), HttpStatusCode::k200OK);
    }

    const std::optional<companion::CompanionInfo> found = Registry().Find(cid);
    if (!found)
        return ReplyJson(callback, webapi_companion::ErrorBody("unknown_companion", "No companion '" + cid + "'"),
                         HttpStatusCode::k404NotFound);
    if (method == drogon::HttpMethod::Delete)
    {
        Json::Value out;
        out["revoked"] = Registry().Revoke(found->id) > 0;
        out["id"] = found->id;
        return ReplyJson(callback, out, HttpStatusCode::k200OK);
    }
    ReplyJson(callback, webapi_companion::CompanionJson(*found), HttpStatusCode::k200OK);
}

/// @brief GET /api/v1/companions/pairings/{pid} - poll a pairing (the token once, when approved);
/// DELETE - cancel it
void EmulatorAPI::companionPairing(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& pid) const
{
    if (req->method() == drogon::HttpMethod::Delete)
    {
        if (!Registry().CancelPairing(pid))
            return ReplyJson(callback, webapi_companion::ErrorBody("unknown_pairing", "No pending pairing '" + pid + "'"),
                             HttpStatusCode::k404NotFound);
        Json::Value out;
        out["cancelled"] = true;
        return ReplyJson(callback, out, HttpStatusCode::k200OK);
    }
    const companion::PairingPoll poll = Registry().PollPairing(pid);
    if (poll.state == "unknown")
        return ReplyJson(callback,
                         webapi_companion::ErrorBody("unknown_pairing",
                                                     "No pairing '" + pid + "': a wrong id, or its token was collected"),
                         HttpStatusCode::k404NotFound);
    Json::Value out;
    out["state"] = poll.state;
    if (poll.state == "pending")
        out["expires_in"] = poll.expiresIn;
    if (poll.state == "approved")
    {
        out["token"] = poll.token;
        out["companion_id"] = poll.companionId;
        out["scopes"] = webapi_companion::ScopesJson(poll.scopes);
    }
    ReplyJson(callback, out, HttpStatusCode::k200OK);
}

/// @brief POST /api/v1/emulator/{id}/control/take-back - a companion with `takeback` ends the session that holds the
/// instance, as the GUI's "Take back control" does
void EmulatorAPI::postControlTakeBack(const HttpRequestPtr& req,
                                      std::function<void(const HttpResponsePtr&)>&& callback,
                                      const std::string& id) const
{
    companion::CompanionInfo me;
    if (!RequireCompanion(req, callback, me))
        return;
    if (!(me.scopes & automation_gate::ScopeTakeBack))
        return ReplyJson(callback, webapi_companion::ForbiddenBody(automation_gate::ScopeTakeBack),
                         HttpStatusCode::k403Forbidden);
    auto emulator = getEmulatorByIdOrIndex(id);
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!context)
        return ReplyJson(callback, webapi_companion::ErrorBody("Not Found", "Emulator not found with ID: " + id),
                         HttpStatusCode::k404NotFound);
    const EmulatorContext::RunControlState state = context->GetRunControlState();
    if (!state.claimed || !context->RevokeRunControl("companion '" + me.name + "'"))
        return ReplyJson(callback, webapi_companion::ErrorBody("nothing_held", "No session holds this emulator"),
                         HttpStatusCode::k409Conflict);
    Json::Value out;
    out["taken_back"] = true;
    Json::Value from;
    from["surface"] = state.surfaceLabel;
    from["client"] = state.client;
    out["from"] = from;
    ReplyJson(callback, out, HttpStatusCode::k200OK);
}

/// @brief POST /api/v1/emulator/{id}/control/allow-again - a companion with `takeback` lets the clients in a cooldown
/// take control again, as the GUI's "Allow again" does
void EmulatorAPI::postControlAllowAgain(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback,
                                        const std::string& id) const
{
    companion::CompanionInfo me;
    if (!RequireCompanion(req, callback, me))
        return;
    if (!(me.scopes & automation_gate::ScopeTakeBack))
        return ReplyJson(callback, webapi_companion::ForbiddenBody(automation_gate::ScopeTakeBack),
                         HttpStatusCode::k403Forbidden);
    auto emulator = getEmulatorByIdOrIndex(id);
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!context)
        return ReplyJson(callback, webapi_companion::ErrorBody("Not Found", "Emulator not found with ID: " + id),
                         HttpStatusCode::k404NotFound);
    context->AllowRunControlRetake();
    Json::Value out;
    out["allowed"] = true;
    ReplyJson(callback, out, HttpStatusCode::k200OK);
}

}  // namespace v1
}  // namespace api
