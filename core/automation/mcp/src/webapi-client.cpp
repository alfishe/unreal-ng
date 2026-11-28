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
#include <vector>

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

/// The loopback connections and the calls each has not answered yet
struct WebApiClient::Connections
{
    struct Connection
    {
        drogon::HttpClientPtr client;
        size_t unanswered = 0;
    };

    /// The connection for the next call (requires the drogon app loop to be running); Release gives it back
    std::pair<drogon::HttpClientPtr, size_t> Acquire()
    {
        std::lock_guard<std::mutex> lock(mutex);
        size_t best = 0;
        for (size_t i = 1; i < list.size(); ++i)
            if (list[i].unanswered < list[best].unanswered)
                best = i;
        if (list.empty() || (list[best].unanswered != 0 && list.size() < kMaxConnections))
        {
            // The drogon app loop is available once the WebAPI thread has called run(). Calls can only arrive
            // through the /mcp endpoint which is served after that, so getLoop() is always valid here.
            long port = 8090;
            if (const char* portEnv = std::getenv("UNREAL_WEBAPI_PORT"))
            {
                const long parsed = std::strtol(portEnv, nullptr, 10);
                if (parsed >= 1 && parsed <= 65535)
                    port = parsed;
            }
            const std::string url = "http://127.0.0.1:" + std::to_string(port);
            list.push_back({drogon::HttpClient::newHttpClient(url, drogon::app().getLoop()), 0});
            best = list.size() - 1;
        }
        ++list[best].unanswered;
        return {list[best].client, best};
    }

    void Release(size_t index)
    {
        std::lock_guard<std::mutex> lock(mutex);
        --list[index].unanswered;
    }

    std::mutex mutex;
    std::vector<Connection> list; // only grows: an index stays valid
};

WebApiClient::WebApiClient() : _connections(std::make_shared<Connections>()) {}

WebApiClient::~WebApiClient() = default;

void WebApiClient::Call(const std::string& method, const std::string& path, const Json::Value* body, ApiCallback callback)
{
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
    else if (method == "PATCH")
    {
        httpMethod = drogon::Patch;
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
    const auto [client, index] = _connections->Acquire();
    client->sendRequest(req, [this, callback, connections = _connections, index = index](
                                 drogon::ReqResult result, const drogon::HttpResponsePtr& resp) {
        connections->Release(index); // first: the callback's next call may take it
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

    const auto [client, index] = _connections->Acquire();
    client->sendRequest(req, [this, callback, connections = _connections, index = index](
                                 drogon::ReqResult result, const drogon::HttpResponsePtr& resp) {
        connections->Release(index); // first: the callback's next call may take it
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

