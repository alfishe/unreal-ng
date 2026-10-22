// Stream Deck API: the daemon's supervision by unreal-qt (companion::streamdeck::Supervisor; Stream Deck integration
// daemon.md §7), its settings, its pairing and the brightness of its decks over HTTP. Every route is one verb of
// companion::streamdeck::Call, the code the CLI, MCP, Lua and Python run too: the args come from the query and the
// JSON body, the reply is the verb's body with its status. The WebSocket `call` of a route runs this same handler;
// the topic `streamdeck.status` pushes the changes.
//
// The verbs that call the daemon (pairing, pair, pair_cancel, brightness) block up to 1.5 s on its loopback port:
// they run off the I/O thread.

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <thread>

#include <emulator/companions/streamdeckapi.h>

#include "../common/statenode_json.h"

using namespace drogon;

namespace api
{
namespace v1
{

void addCorsHeaders(drogon::HttpResponsePtr& resp);

namespace
{
using Callback = std::function<void(const HttpResponsePtr&)>;

void Respond(const Callback& callback, int status, const Json::Value& body)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(static_cast<HttpStatusCode>(status));
    addCorsHeaders(resp);
    callback(resp);
}

bool ArgsOf(const HttpRequestPtr& req, StateNode& args)
{
    args = StateNode::Object();
    for (const auto& [name, value] : req->getParameters())
        args[name] = value;
    if (req->body().empty())
        return true;
    const std::shared_ptr<Json::Value> json = req->getJsonObject();
    if (!json)
        return false;
    if (json->isObject())
        for (const std::string& name : json->getMemberNames())
            args[name] = JsonToStateNode((*json)[name]);
    return true;
}

bool Blocking(const std::string& verb)
{
    return verb == "pairing" || verb == "pair" || verb == "pair_cancel" || verb == "brightness";
}

void Run(const HttpRequestPtr& req, Callback callback, std::string verb,
         std::initializer_list<std::pair<const char*, std::string>> path = {})
{
    StateNode args;
    if (!ArgsOf(req, args))
    {
        Json::Value body;
        body["ok"] = false;
        body["error"] = "Bad Request";
        body["message"] = "the body is not JSON";
        Respond(callback, 400, body);
        return;
    }
    for (const auto& [name, value] : path)
        args[name] = value;
    auto run = [callback = std::move(callback), verb = std::move(verb), args = std::move(args)] {
        const companion::streamdeck::Reply reply = companion::streamdeck::Call(verb, args);
        Respond(callback, reply.status, StateNodeToJson(reply.body));
    };
    if (Blocking(verb))
        std::thread(std::move(run)).detach();  // bounded by the daemon call's own timeout
    else
        run();
}

/// A verb on a method it does not take
void WrongMethod(const Callback& callback, const std::string& method, const std::string& verb, const char* takes)
{
    Json::Value body;
    body["ok"] = false;
    body["error"] = "Method Not Allowed";
    body["message"] = method + " /api/v1/streamdeck/" + verb + ": " + takes;
    Respond(callback, 405, body);
}
} // namespace

void EmulatorAPI::getStreamDeck(const HttpRequestPtr& req, SurfaceCallback&& callback) const
{
    Run(req, std::move(callback), "status");
}

void EmulatorAPI::getStreamDeckVerb(const HttpRequestPtr& req, SurfaceCallback&& callback,
                                    const std::string& verb) const
{
    if (verb == "status" || verb == "settings" || verb == "udev")
        Run(req, std::move(callback), verb);
    else if (verb == "pair" || verb == "pairing")
        Run(req, std::move(callback), "pairing");
    else
        WrongMethod(callback, "GET", verb, "GET takes status, settings, pair, udev");
}

void EmulatorAPI::postStreamDeckVerb(const HttpRequestPtr& req, SurfaceCallback&& callback,
                                     const std::string& verb) const
{
    if (verb == "start" || verb == "stop" || verb == "restart" || verb == "pair" || verb == "revoke")
        Run(req, std::move(callback), verb);
    else
        WrongMethod(callback, "POST", verb, "POST takes start, stop, restart, pair, revoke");
}

void EmulatorAPI::putStreamDeckVerb(const HttpRequestPtr& req, SurfaceCallback&& callback,
                                    const std::string& verb) const
{
    if (verb == "settings")
        Run(req, std::move(callback), verb);
    else
        WrongMethod(callback, "PUT", verb, "PUT takes settings");
}

void EmulatorAPI::deleteStreamDeckVerb(const HttpRequestPtr& req, SurfaceCallback&& callback,
                                       const std::string& verb) const
{
    if (verb == "pair")
        Run(req, std::move(callback), "pair_cancel");
    else
        WrongMethod(callback, "DELETE", verb, "DELETE takes pair (cancel a pending pairing)");
}

void EmulatorAPI::getStreamDeckBrightness(const HttpRequestPtr& req, SurfaceCallback&& callback,
                                          const std::string& deck) const
{
    Run(req, std::move(callback), "brightness", {{"deck", deck}});
}

void EmulatorAPI::putStreamDeckBrightness(const HttpRequestPtr& req, SurfaceCallback&& callback,
                                          const std::string& deck) const
{
    Run(req, std::move(callback), "brightness", {{"deck", deck}});
}

} // namespace v1
} // namespace api
