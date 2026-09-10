// WebApiClient — loopback HTTP implementation of IApiCaller
//
// Uses drogon::HttpClient against 127.0.0.1:8090 (the WebAPI listener on the
// shared drogon app instance). Fully asynchronous: never blocks an IO thread.

#include "webapi-client.h"

#include <drogon/HttpClient.h>
#include <drogon/HttpAppFramework.h>

#include <iostream>

namespace mcp
{

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
    auto* client = new drogon::HttpClientPtr(drogon::HttpClient::newHttpClient("http://127.0.0.1:8090", drogon::app().getLoop()));
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
    req->setPath(path);

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

    // 120s timeout: long run_frames requests may take a while
    (*holder)->sendRequest(req, [callback](drogon::ReqResult result, const drogon::HttpResponsePtr& resp) {
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
        callback(static_cast<int>(resp->statusCode()), std::move(parsed));
    }, 120.0);
}

/// endregion </WebApiClient>

} // namespace mcp
