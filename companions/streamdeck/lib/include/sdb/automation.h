// Automation planes: one route table served as a REST WebAPI (+ OpenAPI) and as an MCP server with two tools,
// search_api and invoke_api (the unreal-ng model). Hosts (Stream Deck Studio, unreal-streamdeck) register their routes;
// both planes answer from the same handlers, so they always agree.
//
//   GET  /api/v1/openapi.json     the route table as OpenAPI 3.0
//   ANY  /api/v1/...              the routes
//   POST /mcp                     MCP Streamable HTTP, JSON answers (stateless); stdio clients use unreal-mcp-bridge
#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace sdb::automation
{

using json = nlohmann::json;

struct Param
{
    std::string name;
    std::string in = "query";     // query | path | body
    std::string type = "string";  // string | integer | number | boolean | object | array
    std::string description;
    bool required = false;
    std::vector<std::string> values{};  // enum
};

struct Request
{
    std::string method, path;
    std::map<std::string, std::string> query;  // query string, plus {name} path segments
    json body;                                 // parsed JSON body (null when none)

    [[nodiscard]] std::string Str(const std::string& name, const std::string& def = {}) const;
    [[nodiscard]] long long Num(const std::string& name, long long def = 0) const;
    [[nodiscard]] double Real(const std::string& name, double def = 0) const;
    [[nodiscard]] bool Flag(const std::string& name, bool def = false) const;
    [[nodiscard]] bool Has(const std::string& name) const;
};

struct Reply
{
    int status = 200;
    json body = json::object();
    std::string binary, contentType;  // set for images (body then holds metadata, sent as X-Meta header / MCP text)

    static Reply Ok(json body = json::object())
    {
        return {200, std::move(body), {}, {}};
    }
    static Reply Error(int status, const std::string& message, const std::string& hint = {});
    static Reply Png(std::string bytes, json meta = json::object())
    {
        return {200, std::move(meta), std::move(bytes), "image/png"};
    }
    [[nodiscard]] bool IsError() const
    {
        return status >= 400;
    }
};

using Handler = std::function<Reply(const Request&)>;

struct Route
{
    std::string method;  // GET POST PUT DELETE
    std::string path;    // "/api/v1/input/key" or with "{name}" segments
    std::string summary, description;
    std::vector<std::string> tags;
    std::vector<Param> params;
    json example;  // example body / query, shown by search_api
    Handler handler;
};

class Api
{
public:
    Api(std::string title, std::string version, std::string instructions = {});

    void Add(Route r);
    [[nodiscard]] const std::vector<Route>& Routes() const
    {
        return _routes;
    }

    // Dispatch by method + path; 404 / 405 / 400 (missing required parameter) as JSON errors.
    [[nodiscard]] Reply Dispatch(Request req) const;
    [[nodiscard]] json Openapi() const;
    // Keyword search over paths, summaries, tags and parameter names (scored like unreal-ng's search_api).
    [[nodiscard]] json Search(const std::string& query, const std::string& method = {}, int limit = 5) const;
    // One JSON-RPC message in, the answer out (null for notifications).
    [[nodiscard]] json Mcp(const json& message) const;

private:
    [[nodiscard]] json McpCall(const std::string& tool, const json& args) const;
    [[nodiscard]] json ToolResult(const Reply& r, const std::string& what) const;

    std::string _title, _version, _instructions;
    std::vector<Route> _routes;
};

// HTTP listener (cpp-httplib, one thread per connection). Handlers run on the server's threads.
class Server
{
public:
    Server();
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool Start(const Api& api, const std::string& host, int port, std::string* error = nullptr);
    void Stop();
    [[nodiscard]] int Port() const
    {
        return _port;
    }
    [[nodiscard]] bool Running() const
    {
        return _running;
    }

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
    int _port = 0;
    std::atomic<bool> _running{false};
};

[[nodiscard]] std::string Base64(const std::string& bytes);

}  // namespace sdb::automation
