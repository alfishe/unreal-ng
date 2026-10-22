// WebApiClient — loopback HTTP implementation of IApiCaller
//
// Uses drogon::HttpClient against 127.0.0.1 on the WebAPI listener's port (the
// shared drogon app instance): 8090, or UNREAL_WEBAPI_PORT when set - the same
// override the WebAPI honors, so a second instance's MCP talks to its own
// machines and not to the instance that owns 8090. Fully asynchronous: never
// blocks an IO thread.

#include "webapi-client.h"

#include "mcp-tool-utils.h"

#include <drogon/HttpClient.h>
#include <drogon/HttpAppFramework.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace mcp
{

/// region <Helpers>

namespace
{
    /// The tools build the path with its query already percent-encoded: drogon must not encode it
    /// again (PrepareRequestPath)
    void SetRequestPath(const drogon::HttpRequestPtr& req, const std::string& path)
    {
        req->setPathEncode(false);
        req->setPath(PrepareRequestPath(path));
    }
}

/// endregion </Helpers>

/// region <WebApiClient>

WebApiClient::~WebApiClient()
{
    delete static_cast<drogon::HttpClientPtr*>(_client);
}

void WebApiClient::EnsureClient()
{
    if (_client != nullptr)
    {
        return;
    }

    // The drogon app loop is available once the WebAPI thread has called run().
    // Calls can only arrive through the /mcp endpoint which is served by the
    // same loop, so getLoop() is always valid here.
    long port = 8090;
    if (const char* portEnv = std::getenv("UNREAL_WEBAPI_PORT"))
    {
        const long parsed = std::strtol(portEnv, nullptr, 10);
        if (parsed >= 1 && parsed <= 65535)
            port = parsed;
    }
    const std::string url = "http://127.0.0.1:" + std::to_string(port);
    auto* client = new drogon::HttpClientPtr(drogon::HttpClient::newHttpClient(url, drogon::app().getLoop()));
    _client = client;
}

void WebApiClient::Call(const std::string& method, const std::string& path, const Json::Value* body, ApiCallback callback)
{
    EnsureClient();

    auto* holder = static_cast<drogon::HttpClientPtr*>(_client);
    if (!holder || !*holder)
    {
        std::cerr << "MCP WebApiClient: loopback client unavailable" << std::endl;
        callback(0, Json::Value());
        return;
    }

    drogon::HttpRequestPtr req;
    if (body != nullptr)
    {
        req = drogon::HttpRequest::newHttpJsonRequest(*body);
    }
    else
    {
        req = drogon::HttpRequest::newHttpRequest();
    }
    SetRequestPath(req, path);

    drogon::HttpMethod httpMethod;
    if (method == "GET")
    {
        httpMethod = drogon::Get;
    }
    else if (method == "POST")
    {
        httpMethod = drogon::Post;
    }
    else if (method == "PUT")
    {
        httpMethod = drogon::Put;
    }
    else if (method == "DELETE")
    {
        httpMethod = drogon::Delete;
    }
    else
    {
        std::cerr << "MCP WebApiClient: unsupported method '" << method << "'" << std::endl;
        callback(0, Json::Value());
        return;
    }
    req->setMethod(httpMethod);
    if (const std::string session = Session(); !session.empty())
        req->addHeader("X-Unreal-Session", session);
    req->addHeader("X-Unreal-Source", "mcp");   // the operation log names the MCP server, not the WebAPI

    // No timeout (0): a long call (run_frames, record/frames, the window recording's stop) answers when it is done
    (*holder)->sendRequest(req, [this, callback](drogon::ReqResult result, const drogon::HttpResponsePtr& resp) {
        if (result != drogon::ReqResult::Ok || !resp)
        {
            callback(0, Json::Value());
            return;
        }

        Json::Value parsed;
        auto json = resp->getJsonObject();
        if (json)
        {
            parsed = *json;
        }
        NoteResponse(static_cast<int>(resp->statusCode()), parsed);
        callback(static_cast<int>(resp->statusCode()), std::move(parsed));
    }, 0.0);
}

void WebApiClient::CallRaw(const std::string& method, const std::string& path,
                           const std::vector<uint8_t>& body,
                           const std::map<std::string, std::string>& headers,
                           ApiCallback callback)
{
    EnsureClient();

    auto* holder = static_cast<drogon::HttpClientPtr*>(_client);
    if (!holder || !*holder)
    {
        std::cerr << "MCP WebApiClient: loopback client unavailable" << std::endl;
        callback(0, Json::Value());
        return;
    }

    auto req = drogon::HttpRequest::newHttpRequest();
    SetRequestPath(req, path);
    req->setBody(std::string(reinterpret_cast<const char*>(body.data()), body.size()));
    req->setContentTypeCode(drogon::CT_APPLICATION_OCTET_STREAM);

    for (const auto& [key, value] : headers)
    {
        req->addHeader(key, value);
    }

    drogon::HttpMethod httpMethod = drogon::Post;
    if (method == "PUT")
        httpMethod = drogon::Put;

    req->setMethod(httpMethod);
    if (const std::string session = Session(); !session.empty())
        req->addHeader("X-Unreal-Session", session);
    req->addHeader("X-Unreal-Source", "mcp");   // the operation log names the MCP server, not the WebAPI

    (*holder)->sendRequest(req, [this, callback](drogon::ReqResult result, const drogon::HttpResponsePtr& resp) {
        if (result != drogon::ReqResult::Ok || !resp)
        {
            callback(0, Json::Value());
            return;
        }

        Json::Value parsed;
        auto json = resp->getJsonObject();
        if (json)
        {
            parsed = *json;
        }
        NoteResponse(static_cast<int>(resp->statusCode()), parsed);
        callback(static_cast<int>(resp->statusCode()), std::move(parsed));
    }, 0.0);
}

/// endregion </WebApiClient>

void WebApiClient::SetSession(const std::string& sessionId)
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    _session = sessionId;
}

std::string WebApiClient::Session() const
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    return _session;
}

bool WebApiClient::TakeRevoked()
{
    return _revoked.exchange(false);
}

std::string WebApiClient::TakeImplicitAdvice()
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    std::string advice;
    advice.swap(_implicitAdvice);
    return advice;
}

void WebApiClient::NoteResponse(int status, const Json::Value& body)
{
    if (!body.isObject())
        return;
    // The gate opened a session for this agent's call: tell the agent once per session
    const Json::Value& implicit = body["automation_session"];
    if (implicit.isObject() && implicit.get("implicit", false).asBool())
    {
        std::lock_guard<std::mutex> lock(_sessionMutex);
        const std::string id = implicit.get("id", "").asString();
        if (id != _advisedSession)
        {
            _advisedSession = id;
            _implicitAdvice = "Note: session opened implicitly (you changed the emulator without take_control). For a "
                              "job: control_execution take_control first, release_control when done.";
        }
    }
    if (status != 409 || body.get("error", "").asString() != "revoked_by_user")
        return;
    SetSession({});
    _revoked = true;
}

} // namespace mcp

