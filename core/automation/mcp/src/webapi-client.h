#pragma once

// Loopback client abstraction for the MCP layer
//
// All MCP smart/router tools reach emulator functionality through the existing
// WebAPI (Drogon REST on 127.0.0.1:8090). The IApiCaller interface keeps the
// dispatcher/tools/router drogon-free so they can be unit-tested with a fake,
// while WebApiClient (webapi-client.cpp) performs the actual async HTTP calls.

#include <json/json.h>

#include <functional>
#include <memory>
#include <string>

namespace mcp
{

/// region <IApiCaller>

/// Async caller into the emulator's own WebAPI
class IApiCaller
{
public:
    using Ptr = std::shared_ptr<IApiCaller>;

    /// Receives HTTP status code (0 = transport failure) and the parsed JSON body
    /// (null when the response body is empty or not valid JSON)
    using ApiCallback = std::function<void(int status, Json::Value body)>;

    virtual ~IApiCaller() = default;

    /// Executes an HTTP method against a WebAPI path, e.g. Call("GET", "/api/v1/emulator", nullptr, cb)
    /// body may be nullptr for GET/DELETE requests
    virtual void Call(const std::string& method, const std::string& path, const Json::Value* body, ApiCallback callback) = 0;
};

/// endregion </IApiCaller>

/// region <WebApiClient>

/// IApiCaller implementation based on drogon::HttpClient against the loopback WebAPI
class WebApiClient : public IApiCaller
{
public:
    WebApiClient() = default;
    ~WebApiClient() override; // frees the lazily created drogon client

    // Non-copyable: owns an opaque heap-allocated drogon client handle
    WebApiClient(const WebApiClient&) = delete;
    WebApiClient& operator=(const WebApiClient&) = delete;

    void Call(const std::string& method, const std::string& path, const Json::Value* body, ApiCallback callback) override;

private:
    /// Lazily created loopback client (requires the drogon app loop to be running)
    void EnsureClient();

    void* _client = nullptr; // Opaque drogon::HttpClientPtr — avoids drogon types in this header
};

/// endregion </WebApiClient>

} // namespace mcp
