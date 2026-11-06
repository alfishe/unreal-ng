// The scripts' callbacks on the WebAPI (docs/features/script-callbacks.md): a remote client cannot register a function
// (it lives in a Lua / Python script), but it lists the callbacks with their statistics and resumes, removes or
// re-policies one, as Lua's callbacks() / callback_resume / callback_remove / callback_policy. The report and the verbs
// are core callback_control's (the CLI and MCP say the same); every route is also a WebSocket call, and a change is
// pushed as callbacks_changed on debug.events (debug-event-hub.cpp)

#include "../common/statenode_json.h"
#include "../emulator_api.h"

#include <debugger/callbacks/callbackcontrol.h>
#include <debugger/debugmanager.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>

using namespace drogon;

namespace api
{
namespace v1
{

extern void addCorsHeaders(HttpResponsePtr& resp);

namespace
{
void Reply(std::function<void(const HttpResponsePtr&)>& callback, const Json::Value& body, int status)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(static_cast<HttpStatusCode>(status));
    addCorsHeaders(resp);
    callback(resp);
}

/// The emulator's callback manager; nullptr after a 404 (no emulator). A manager-less emulator gives nullptr with
/// `found` true: the verbs answer 503 themselves
ScriptCallbackManager* CallbacksOrError(const std::string& id, std::function<void(const HttpResponsePtr&)>& callback,
                                        std::shared_ptr<Emulator>& hold, bool& found)
{
    auto* manager = EmulatorManager::GetInstance();
    hold = manager ? manager->GetEmulator(id) : nullptr;
    found = hold != nullptr;
    if (!hold)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator with specified ID not found";
        Reply(callback, error, 404);
        return nullptr;
    }
    EmulatorContext* context = hold->GetContext();
    return context && context->pDebugManager ? context->pDebugManager->GetScriptCallbacks() : nullptr;
}

void ReplyOutcome(std::function<void(const HttpResponsePtr&)>& callback, const callback_control::Outcome& outcome)
{
    Reply(callback, StateNodeToJson(outcome.body), outcome.status);
}
}  // namespace

/// @brief GET /api/v1/emulator/{id}/callbacks - {count, hooks?: {unpaced, unpaced_spans, last_frame_us, max_frame_us,
/// frames_over_share, frame_share_percent}, callbacks: [{id, on, mode, owner, label, target, breakpoint, event?,
/// policy, state, reason?, budget_us, ring, pending, hits, dropped, calls, errors, overruns, total_us, max_us, p99_us,
/// aborted?, instructions?, last_overrun_frame?, last_overrun_us?, last_error?}]}: every script callback of the
/// emulator, the Lua table's fields
void EmulatorAPI::getCallbacks(const HttpRequestPtr& /*req*/, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    std::shared_ptr<Emulator> hold;
    bool found = false;
    ScriptCallbackManager* callbacks = CallbacksOrError(id, callback, hold, found);
    if (!found)
        return;
    const std::vector<CallbackInfo> list = callbacks ? callbacks->List() : std::vector<CallbackInfo>();
    const CallbackHookStatus hooks = callbacks ? callbacks->HookStatus() : CallbackHookStatus{};
    Reply(callback, StateNodeToJson(callback_control::ListNode(list, callbacks ? &hooks : nullptr)), 200);
}

/// @brief POST /api/v1/emulator/{id}/callbacks/{cb_id}/resume - a suspended callback active again with fresh
/// statistics: {status, message, callback}; 404 unknown, 409 disabled (the script registers it again)
void EmulatorAPI::resumeCallback(const HttpRequestPtr& /*req*/, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id, const std::string& cbId) const
{
    std::shared_ptr<Emulator> hold;
    bool found = false;
    ScriptCallbackManager* callbacks = CallbacksOrError(id, callback, hold, found);
    if (found)
        ReplyOutcome(callback, callback_control::Resume(callbacks, cbId));
}

/// @brief DELETE /api/v1/emulator/{id}/callbacks/{cb_id} - removes the callback and its tap: {status, message, id,
/// removed}; 404 unknown
void EmulatorAPI::removeCallback(const HttpRequestPtr& /*req*/, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id, const std::string& cbId) const
{
    std::shared_ptr<Emulator> hold;
    bool found = false;
    ScriptCallbackManager* callbacks = CallbacksOrError(id, callback, hold, found);
    if (found)
        ReplyOutcome(callback, callback_control::Remove(callbacks, cbId));
}

/// @brief PUT /api/v1/emulator/{id}/callbacks/{cb_id}/policy - {policy: suspend|disable|ignore|unpaced}, what happens over
/// the budget: {status, message, callback}; 400 a bad policy, 404 unknown
void EmulatorAPI::setCallbackPolicy(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                    const std::string& id, const std::string& cbId) const
{
    std::shared_ptr<Emulator> hold;
    bool found = false;
    ScriptCallbackManager* callbacks = CallbacksOrError(id, callback, hold, found);
    if (!found)
        return;
    auto json = req->getJsonObject();
    if (!json || !(*json)["policy"].isString())
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Body: {\"policy\": \"suspend\" | \"disable\" | \"ignore\"}";
        Reply(callback, error, 400);
        return;
    }
    ReplyOutcome(callback, callback_control::SetPolicy(callbacks, cbId, (*json)["policy"].asString()));
}

}  // namespace v1
}  // namespace api
