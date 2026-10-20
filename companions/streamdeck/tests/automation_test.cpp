// Automation planes: route dispatch, OpenAPI, search, MCP JSON-RPC, and the HTTP listener end to end.
#include "sdb/automation.h"

#include <string>

#include "check.h"
#include "sdb/http.h"

using namespace sdb;
using namespace sdb::automation;

namespace
{
Api MakeApi()
{
    Api api("Test Deck", "1.0", "drive the test deck");
    api.Add({"GET", "/api/v1/device", "Device model and state", "", {"device"}, {}, nullptr, [](const Request&) {
                 return Reply::Ok({{"model", "plusxl"}});
             }});
    api.Add(
        {"POST",
         "/api/v1/input/key",
         "Press a key",
         "Presses and releases a key",
         {"input"},
         {{"index", "body", "integer", "key index", true}, {"hold_ms", "body", "integer", "hold time"}},
         json{{"index", 3}},
         [](const Request& r) { return Reply::Ok({{"pressed", r.Num("index")}, {"hold", r.Num("hold_ms", 80)}}); }});
    api.Add({"GET", "/api/v1/nodes/{id}", "One scenario node", "", {"scenario"}, {}, nullptr, [](const Request& r) {
                 return Reply::Ok({{"id", r.Str("id")}});
             }});
    api.Add(
        {"GET",
         "/api/v1/screenshot",
         "Screenshot of the deck",
         "",
         {"screenshot"},
         {{"area", "query", "string", "deck|window", false, {"deck", "window"}}},
         nullptr,
         [](const Request& r) { return Reply::Png(std::string("\x89PNG", 4), {{"area", r.Str("area", "deck")}}); }});
    return api;
}
json Tool(const Api& api, int id, const std::string& name, const json& args)
{
    json p = json::object();
    p["name"] = name;
    p["arguments"] = args;
    json m = json::object();
    m["jsonrpc"] = "2.0";
    m["id"] = id;
    m["method"] = "tools/call";
    m["params"] = p;
    return api.Mcp(m)["result"];
}
}  // namespace

void TestAutomation()
{
    std::printf("automation\n");
    const Api api = MakeApi();
    // dispatch: params from body / query / path, errors
    CHECK(api.Dispatch({"GET", "/api/v1/device", {}, nullptr}).body["model"] == "plusxl");
    auto r = api.Dispatch({"POST", "/api/v1/input/key", {}, json{{"index", 7}}});
    CHECK_EQ(r.status, 200);
    CHECK(r.body["pressed"] == 7 && r.body["hold"] == 80);
    CHECK_EQ(api.Dispatch({"POST", "/api/v1/input/key", {{"index", "9"}}, nullptr}).body["pressed"].get<int>(), 9);
    CHECK_EQ(api.Dispatch({"POST", "/api/v1/input/key", {}, json::object()}).status, 400);  // required index missing
    CHECK_EQ(api.Dispatch({"GET", "/api/v1/input/key", {}, nullptr}).status, 405);
    CHECK_EQ(api.Dispatch({"GET", "/api/v1/nope", {}, nullptr}).status, 404);
    CHECK(api.Dispatch({"GET", "/api/v1/nodes/debug", {}, nullptr}).body["id"] == "debug");
    // OpenAPI
    const json spec = api.Openapi();
    CHECK(spec["paths"].contains("/api/v1/input/key"));
    CHECK(spec["paths"]["/api/v1/input/key"]["post"]["requestBody"]["content"]["application/json"]["schema"]["required"]
              [0] == "index");
    CHECK(spec["paths"]["/api/v1/screenshot"]["get"]["parameters"][0]["schema"]["enum"].size() == 2);
    // search: path segments beat summaries
    const json s = api.Search("key press");
    CHECK(s["matches"][0]["path"] == "/api/v1/input/key");
    CHECK(api.Search("zzz")["total_matches"] == 0);
    // MCP
    CHECK(api.Mcp({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}).is_null());
    const json init = api.Mcp(
        {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", {{"protocolVersion", "2025-03-26"}}}});
    CHECK(init["result"]["protocolVersion"] == "2025-03-26");
    CHECK(init["result"]["serverInfo"]["name"] == "Test Deck");
    const json tools = api.Mcp({{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}});
    CHECK(tools["result"]["tools"].size() == 2);
    CHECK(tools["result"]["tools"][0]["name"] == "search_api" && tools["result"]["tools"][1]["name"] == "invoke_api");
    const json call =
        Tool(api, 3, "invoke_api", {{"method", "post"}, {"path", "/api/v1/input/key"}, {"body", {{"index", 4}}}});
    CHECK(call["isError"] == false);
    CHECK(call["structuredContent"]["pressed"] == 4);
    const json shot = Tool(api, 4, "invoke_api", {{"path", "/api/v1/screenshot?area=window"}});
    CHECK(shot["content"][0]["type"] == "image");
    CHECK(shot["content"][0]["data"] == Base64(std::string("\x89PNG", 4)));
    CHECK(shot["structuredContent"]["area"] == "window");
    const json bad = Tool(api, 5, "invoke_api", {{"path", "/api/v1/nope"}});
    CHECK(bad["isError"] == true);
    const json auto1 = Tool(api, 6, "search_api", {{"query", "device model"}, {"auto_invoke", true}});
    CHECK(auto1["structuredContent"]["model"] == "plusxl");
    CHECK(api.Mcp({{"jsonrpc", "2.0"}, {"id", 7}, {"method", "bogus"}})["error"]["code"] == -32601);
    CHECK(Base64("Man") == "TWFu" && Base64("Ma") == "TWE=" && Base64("M") == "TQ==");
    // HTTP end to end on an ephemeral port
    Server srv;
    std::string err;
    CHECK(srv.Start(api, "127.0.0.1", 0, &err));
    const std::string url = "http://127.0.0.1:" + std::to_string(srv.Port());
    auto h = HttpRequest(url, "POST", "/api/v1/input/key", R"({"index":2})");
    CHECK_EQ(h.status, 200);
    CHECK(json::parse(h.body)["pressed"] == 2);
    CHECK_EQ(HttpRequest(url, "GET", "/api/v1/openapi.json").status, 200);
    auto m = HttpRequest(url, "POST", "/mcp", R"({"jsonrpc":"2.0","id":1,"method":"ping"})");
    CHECK(m.status == 200 && json::parse(m.body)["id"] == 1);
    CHECK_EQ(HttpRequest(url, "POST", "/mcp", R"({"jsonrpc":"2.0","method":"notifications/initialized"})").status, 202);
    srv.Stop();
}
