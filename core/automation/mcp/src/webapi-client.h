#pragma once

// Loopback client abstraction for the MCP layer
//
// All MCP smart/router tools reach emulator functionality through the existing
// WebAPI (Drogon REST on 127.0.0.1:8090). The IApiCaller interface keeps the
// dispatcher/tools/router drogon-free so they can be unit-tested with a fake,
// while WebApiClient (webapi-client.cpp) performs the actual async HTTP calls.

#include <mutex>
#include <atomic>
#include <json/json.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

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

    /// Executes an HTTP method with raw binary body and custom headers (for file uploads)
    virtual void CallRaw(const std::string& method, const std::string& path,
                         const std::vector<uint8_t>& body,
                         const std::map<std::string, std::string>& headers,
                         ApiCallback callback) = 0;

    /// Automation session (execution arbiter design §6a): the id sent as X-Unreal-Session on every call (empty: none)
    virtual void SetSession(const std::string& sessionId) { (void)sessionId; }
    virtual std::string Session() const { return {}; }
    /// A call was answered revoked_by_user since the last check (the user took control back at the GUI): the session
    /// is forgotten. True once per take-back
    virtual bool TakeRevoked() { return false; }
    /// The automation gate opened a session for a call that changed the machine without one (automationgate.h): the
    /// advice to take control explicitly, once per such session; empty otherwise
    virtual std::string TakeImplicitAdvice() { return {}; }
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
    void CallRaw(const std::string& method, const std::string& path,
                 const std::vector<uint8_t>& body,
                 const std::map<std::string, std::string>& headers,
                 ApiCallback callback) override;
    void SetSession(const std::string& sessionId) override;
    std::string Session() const override;
    bool TakeRevoked() override;
    std::string TakeImplicitAdvice() override;

private:
    /// Notes a revoked_by_user answer: the session is over, the dispatcher tells the agent to stop
    void NoteResponse(int status, const Json::Value& body);
    mutable std::mutex _sessionMutex;
    std::string _session;
    std::atomic<bool> _revoked{false};
    std::string _implicitAdvice;      // pending for the dispatcher (guarded by _sessionMutex)
    std::string _advisedSession;      // the implicit session the agent was already told about

    /// Lazily created loopback client (requires the drogon app loop to be running)
    void EnsureClient();

    void* _client = nullptr; // Opaque drogon::HttpClientPtr — avoids drogon types in this header
};

/// endregion </WebApiClient>

} // namespace mcp
