#include "sdb/automation.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <thread>

#include "httplib.h"

namespace sdb::automation
{

namespace
{

constexpr const char* kProtocolVersion = "2025-06-18";
const std::set<std::string> kProtocols = {"2025-06-18", "2025-03-26", "2024-11-05"};

std::string Lower(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool Contains(const std::string& hay, const std::string& needle)
{
    return Lower(hay).find(needle) != std::string::npos;
}

std::vector<std::string> Tokenize(const std::string& q)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : q)
    {
        if (std::isalnum(static_cast<unsigned char>(c)))
            cur += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (!cur.empty())
        {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

std::vector<std::string> Segments(const std::string& path)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : path)
    {
        if (c == '/' || c == '_' || c == '-')
        {
            if (!cur.empty())
                out.push_back(Lower(cur));
            cur.clear();
        }
        else if (c != '{' && c != '}')
            cur += c;
    }
    if (!cur.empty())
        out.push_back(Lower(cur));
    return out;
}

// Path segment hits weigh most, then summary / description and tags, then parameter names.
int Score(const Route& r, const std::vector<std::string>& tokens)
{
    int s = 0;
    const auto segs = Segments(r.path);
    for (const auto& t : tokens)
    {
        if (std::any_of(segs.begin(), segs.end(), [&](const std::string& g) {
                return g == t || (g.size() > t.size() && g.find(t) != std::string::npos);
            }))
        {
            s += 3;
            continue;
        }
        if (Contains(r.summary, t) || Contains(r.description, t))
        {
            s += 2;
            continue;
        }
        if (std::any_of(r.tags.begin(), r.tags.end(), [&](const std::string& g) { return Contains(g, t); }))
        {
            s += 2;
            continue;
        }
        if (std::any_of(r.params.begin(), r.params.end(),
                        [&](const Param& p) { return Contains(p.name, t) || Contains(p.description, t); }))
            s += 1;
    }
    return s;
}

// "/api/v1/x/{id}" against "/api/v1/x/5": captures into `out`
bool MatchPath(const std::string& pattern, const std::string& path, std::map<std::string, std::string>& out)
{
    auto split = [](const std::string& p) {
        std::vector<std::string> v;
        std::string cur;
        for (char c : p)
        {
            if (c == '/')
            {
                v.push_back(cur);
                cur.clear();
            }
            else
                cur += c;
        }
        v.push_back(cur);
        return v;
    };
    const auto a = split(pattern), b = split(path);
    if (a.size() != b.size())
        return false;
    std::map<std::string, std::string> cap;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].size() > 2 && a[i].front() == '{' && a[i].back() == '}')
            cap[a[i].substr(1, a[i].size() - 2)] = b[i];
        else if (a[i] != b[i])
            return false;
    }
    out.insert(cap.begin(), cap.end());
    return true;
}

json ParamJson(const Param& p)
{
    json j{{"name", p.name}, {"in", p.in}, {"type", p.type}};
    if (!p.description.empty())
        j["description"] = p.description;
    if (p.required)
        j["required"] = true;
    if (!p.values.empty())
        j["enum"] = p.values;
    return j;
}

json Describe(const Route& r)
{
    json j{{"method", r.method}, {"path", r.path}, {"summary", r.summary}};
    if (!r.description.empty())
        j["description"] = r.description;
    if (!r.tags.empty())
        j["tags"] = r.tags;
    json ps = json::array();
    for (const auto& p : r.params)
        ps.push_back(ParamJson(p));
    if (!ps.empty())
        j["params"] = ps;
    if (!r.example.is_null())
        j["example"] = r.example;
    return j;
}

json RpcResult(const json& id, json result)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}
json RpcError(const json& id, int code, const std::string& msg)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", msg}}}};
}

}  // namespace

// ---------------------------------------------------------------- Request / Reply
std::string Request::Str(const std::string& n, const std::string& def) const
{
    if (body.is_object() && body.contains(n))
    {
        const auto& v = body[n];
        return v.is_string() ? v.get<std::string>() : v.dump();
    }
    if (auto it = query.find(n); it != query.end())
        return it->second;
    return def;
}
long long Request::Num(const std::string& n, long long def) const
{
    if (body.is_object() && body.contains(n) && body[n].is_number())
        return body[n].get<long long>();
    const std::string s = Str(n);
    if (s.empty())
        return def;
    try
    {
        return std::stoll(s);
    }
    catch (...)
    {
        return def;
    }
}
double Request::Real(const std::string& n, double def) const
{
    if (body.is_object() && body.contains(n) && body[n].is_number())
        return body[n].get<double>();
    const std::string s = Str(n);
    if (s.empty())
        return def;
    try
    {
        return std::stod(s);
    }
    catch (...)
    {
        return def;
    }
}
bool Request::Flag(const std::string& n, bool def) const
{
    if (body.is_object() && body.contains(n) && body[n].is_boolean())
        return body[n].get<bool>();
    const std::string s = Lower(Str(n));
    if (s.empty())
        return def;
    return s == "1" || s == "true" || s == "yes" || s == "on";
}
bool Request::Has(const std::string& n) const
{
    return (body.is_object() && body.contains(n)) || query.count(n) > 0;
}

Reply Reply::Error(int status, const std::string& message, const std::string& hint)
{
    json b{{"error", message}};
    if (!hint.empty())
        b["hint"] = hint;
    return {status, std::move(b), {}, {}};
}

// ---------------------------------------------------------------- Api
Api::Api(std::string title, std::string version, std::string instructions)
    : _title(std::move(title)), _version(std::move(version)), _instructions(std::move(instructions))
{
}

void Api::Add(Route r)
{
    _routes.push_back(std::move(r));
}

Reply Api::Dispatch(Request req) const
{
    bool pathKnown = false;
    for (const auto& r : _routes)
    {
        std::map<std::string, std::string> cap;
        if (!MatchPath(r.path, req.path, cap))
            continue;
        pathKnown = true;
        if (r.method != req.method)
            continue;
        req.query.insert(cap.begin(), cap.end());
        for (const auto& p : r.params)
            if (p.required && !req.Has(p.name))
                return Reply::Error(400, "missing parameter '" + p.name + "'", p.description);
        try
        {
            return r.handler(req);
        }
        catch (const std::exception& e)
        {
            return Reply::Error(500, e.what());
        }
    }
    if (pathKnown)
        return Reply::Error(405, "method " + req.method + " not allowed on " + req.path);
    return Reply::Error(404, "no route " + req.method + " " + req.path,
                        "GET /api/v1/openapi.json lists every route; MCP: search_api");
}

json Api::Openapi() const
{
    json paths = json::object();
    for (const auto& r : _routes)
    {
        json op{{"summary", r.summary}, {"operationId", Lower(r.method) + std::string(r.path)}};
        if (!r.description.empty())
            op["description"] = r.description;
        if (!r.tags.empty())
            op["tags"] = r.tags;
        json params = json::array(), props = json::object(), required = json::array();
        for (const auto& p : r.params)
        {
            json schema{{"type", p.type}};
            if (!p.values.empty())
                schema["enum"] = p.values;
            if (p.in == "body")
            {
                if (!p.description.empty())
                    schema["description"] = p.description;
                props[p.name] = schema;
                if (p.required)
                    required.push_back(p.name);
            }
            else
            {
                json q{{"name", p.name}, {"in", p.in}, {"schema", schema}, {"required", p.required || p.in == "path"}};
                if (!p.description.empty())
                    q["description"] = p.description;
                params.push_back(q);
            }
        }
        if (!params.empty())
            op["parameters"] = params;
        if (!props.empty())
        {
            json schema{{"type", "object"}, {"properties", props}};
            if (!required.empty())
                schema["required"] = required;
            json media{{"schema", schema}};
            if (!r.example.is_null())
                media["example"] = r.example;
            op["requestBody"] = {{"content", {{"application/json", media}}}};
        }
        op["responses"] = {{"200", {{"description", "OK"}}}};
        paths[r.path][Lower(r.method)] = op;
    }
    return {{"openapi", "3.0.3"}, {"info", {{"title", _title}, {"version", _version}}}, {"paths", paths}};
}

json Api::Search(const std::string& query, const std::string& method, int limit) const
{
    const auto tokens = Tokenize(query);
    std::vector<std::pair<int, const Route*>> hits;
    for (const auto& r : _routes)
    {
        if (!method.empty() && r.method != method)
            continue;
        if (int s = Score(r, tokens); s > 0)
            hits.emplace_back(s, &r);
    }
    std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second->path.size() < b.second->path.size();
    });
    json out = json::array();
    for (int i = 0; i < static_cast<int>(hits.size()) && i < std::clamp(limit, 1, 50); ++i)
        out.push_back(Describe(*hits[static_cast<size_t>(i)].second));
    return {{"matches", out}, {"total_matches", hits.size()}};
}

// ---------------------------------------------------------------- MCP
json Api::ToolResult(const Reply& r, const std::string& what) const
{
    json content = json::array();
    if (!r.binary.empty())
    {
        content.push_back({{"type", "image"}, {"data", Base64(r.binary)}, {"mimeType", r.contentType}});
        content.push_back({{"type", "text"}, {"text", what + ": " + r.body.dump()}});
    }
    else
    {
        std::string text = r.body.dump(2);
        if (text.size() > 64 * 1024)
            text = text.substr(0, 64 * 1024) + "\n... (cut at 64 KB)";
        content.push_back(
            {{"type", "text"},
             {"text", (r.IsError() ? what + " failed (" + std::to_string(r.status) + "): " : what + ": ") + text}});
    }
    json res{{"content", content}, {"isError", r.IsError()}};
    json structured = r.body.is_object() ? r.body : json{{"value", r.body}};
    structured["status"] = r.status;
    res["structuredContent"] = structured;
    return res;
}

json Api::McpCall(const std::string& tool, const json& args) const
{
    auto invoke = [this](const std::string& method, const std::string& path, const json& query, const json& body) {
        Request req;
        req.method = method;
        req.path = path;
        if (auto q = path.find('?'); q != std::string::npos)
        {  // a query string in the path is accepted too
            req.path = path.substr(0, q);
            httplib::Params ps;
            httplib::detail::parse_query_text(path.substr(q + 1), ps);
            for (auto& [k, v] : ps)
                req.query[k] = v;
        }
        if (query.is_object())
            for (auto& [k, v] : query.items())
                req.query[k] = v.is_string() ? v.get<std::string>() : v.dump();
        req.body = body;
        return Dispatch(req);
    };
    if (tool == "search_api")
    {
        const std::string q = args.value("query", "");
        if (q.empty())
            return {{"content", {{{"type", "text"}, {"text", "Missing 'query'"}}}}, {"isError", true}};
        std::string m = args.value("method", "");
        for (auto& c : m)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const json found = Search(q, m, args.value("limit", 5));
        const auto& matches = found["matches"];
        if (args.value("auto_invoke", false) && found["total_matches"] == 1)
        {
            const auto& one = matches[0];
            json r = ToolResult(invoke(one["method"], one["path"], args.value("query_params", json::object()),
                                       args.value("body", json())),
                                one["method"].get<std::string>() + " " + one["path"].get<std::string>() +
                                    " (auto-invoked unique match)");
            r["structuredContent"]["match"] = one;
            return r;
        }
        std::ostringstream text;
        if (matches.empty())
            text << "No endpoints match '" << q
                 << "'. Try other keywords, e.g. 'screenshot', 'key press', 'model', 'scenario'.";
        else
            text << matches.size() << " of " << found["total_matches"].get<size_t>() << " match(es) for '" << q << "':";
        for (size_t i = 0; i < matches.size(); ++i)
        {
            const auto& d = matches[i];
            text << "\n"
                 << i + 1 << ". " << d["method"].get<std::string>() << " " << d["path"].get<std::string>() << " - "
                 << d["summary"].get<std::string>();
            if (d.contains("params"))
                for (const auto& p : d["params"])
                {
                    text << "\n     " << p["name"].get<std::string>() << " (" << p["in"].get<std::string>() << ", "
                         << p["type"].get<std::string>() << (p.value("required", false) ? ", required" : "") << ")";
                    if (p.contains("enum"))
                        text << " " << p["enum"].dump();
                    if (p.contains("description"))
                        text << ": " << p["description"].get<std::string>();
                }
            if (d.contains("example"))
                text << "\n     example: " << d["example"].dump();
        }
        return {
            {"content", {{{"type", "text"}, {"text", text.str()}}}}, {"structuredContent", found}, {"isError", false}};
    }
    if (tool == "invoke_api")
    {
        std::string m = args.value("method", "GET");
        for (auto& c : m)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const std::string path = args.value("path", "");
        if (path.empty())
            return {{"content", {{{"type", "text"}, {"text", "Missing 'path' (find one with search_api)"}}}},
                    {"isError", true}};
        return ToolResult(invoke(m, path, args.value("query_params", json::object()), args.value("body", json())),
                          m + " " + path);
    }
    return {{"content", {{{"type", "text"}, {"text", "Unknown tool '" + tool + "': use search_api or invoke_api"}}}},
            {"isError", true}};
}

json Api::Mcp(const json& msg) const
{
    if (!msg.is_object())
        return RpcError(nullptr, -32600, "invalid request");
    const json id = msg.contains("id") ? msg["id"] : json();
    const std::string method = msg.value("method", "");
    const json params = msg.value("params", json::object());
    if (!msg.contains("id"))
        return nullptr;  // notification (notifications/initialized, cancelled, ...)
    if (method == "initialize")
    {
        const std::string v = params.value("protocolVersion", "");
        return RpcResult(id, {{"protocolVersion", kProtocols.count(v) ? v : kProtocolVersion},
                              {"capabilities", {{"tools", {{"listChanged", false}}}}},
                              {"serverInfo", {{"name", _title}, {"version", _version}}},
                              {"instructions", _instructions}});
    }
    if (method == "ping")
        return RpcResult(id, json::object());
    if (method == "tools/list")
    {
        json search{
            {"name", "search_api"},
            {"description",
             "Search the " + _title + " WebAPI (" + std::to_string(_routes.size()) +
                 " endpoints) by keywords. Returns endpoints with parameters and an example. Chain with invoke_api, "
                 "or set auto_invoke to run a unique match."},
            {"inputSchema",
             {{"type", "object"},
              {"properties",
               {{"query",
                 {{"type", "string"},
                  {"description", "Keywords, e.g. 'screenshot', 'press key', 'dial', 'model', 'scenario node'"}}},
                {"method", {{"type", "string"}, {"enum", {"GET", "POST", "PUT", "DELETE"}}}},
                {"limit", {{"type", "integer"}, {"default", 5}}},
                {"auto_invoke", {{"type", "boolean"}, {"default", false}}},
                {"body", {{"type", "object"}}},
                {"query_params", {{"type", "object"}}}}},
              {"required", {"query"}}}}};
        json invoke{
            {"name", "invoke_api"},
            {"description",
             "Call one " + _title + " WebAPI endpoint. Image answers (screenshots) come back as image content."},
            {"inputSchema",
             {{"type", "object"},
              {"properties",
               {{"method", {{"type", "string"}, {"enum", {"GET", "POST", "PUT", "DELETE"}}, {"default", "GET"}}},
                {"path", {{"type", "string"}, {"description", "e.g. /api/v1/device; {name} segments filled in"}}},
                {"query_params", {{"type", "object"}}},
                {"body", {{"type", "object"}}}}},
              {"required", {"path"}}}}};
        return RpcResult(id, {{"tools", {search, invoke}}});
    }
    if (method == "tools/call")
        return RpcResult(id, McpCall(params.value("name", ""), params.value("arguments", json::object())));
    if (method == "resources/list")
        return RpcResult(id, {{"resources", json::array()}});
    if (method == "prompts/list")
        return RpcResult(id, {{"prompts", json::array()}});
    return RpcError(id, -32601, "method not found: " + method);
}

// ---------------------------------------------------------------- HTTP
struct Server::Impl
{
    httplib::Server http;
    std::thread thread;
};

Server::Server() : _impl(std::make_unique<Impl>()) {}
Server::~Server()
{
    Stop();
}

bool Server::Start(const Api& api, const std::string& host, int port, std::string* error)
{
    auto& s = _impl->http;
    auto toRequest = [](const httplib::Request& hr) {
        Request r;
        r.method = hr.method;
        r.path = hr.path;
        for (const auto& [k, v] : hr.params)
            r.query[k] = v;
        if (!hr.body.empty())
            r.body = json::parse(hr.body, nullptr, false);
        return r;
    };
    auto send = [](httplib::Response& res, const Reply& r) {
        res.status = r.status;
        if (!r.binary.empty())
        {
            res.set_header("X-Meta", r.body.dump());
            res.set_content(r.binary, r.contentType);
        }
        else
        {
            res.set_content(r.body.dump(2), "application/json");
        }
    };
    s.Get("/api/v1/openapi.json", [&api](const httplib::Request&, httplib::Response& res) {
        res.set_content(api.Openapi().dump(2), "application/json");
    });
    s.Post("/mcp", [&api](const httplib::Request& hr, httplib::Response& res) {
        const json msg = json::parse(hr.body, nullptr, false);
        if (msg.is_discarded())
        {
            res.status = 400;
            res.set_content(RpcError(nullptr, -32700, "parse error").dump(), "application/json");
            return;
        }
        if (msg.is_array())
        {
            res.set_content(RpcError(nullptr, -32600, "batches are not supported").dump(), "application/json");
            return;
        }
        const json out = api.Mcp(msg);
        if (out.is_null())
        {
            res.status = 202;
            return;
        }
        res.set_content(out.dump(), "application/json");
    });
    s.Get("/mcp", [](const httplib::Request&, httplib::Response& res) {
        res.status = 405;
        res.set_header("Allow", "POST");
    });
    auto any = [&api, toRequest, send](const httplib::Request& hr, httplib::Response& res) {
        Request r = toRequest(hr);
        if (!hr.body.empty() && r.body.is_discarded())
        {
            send(res, Reply::Error(400, "body is not JSON"));
            return;
        }
        send(res, api.Dispatch(std::move(r)));
    };
    s.Get("/api/v1/.*", any);
    s.Post("/api/v1/.*", any);
    s.Put("/api/v1/.*", any);
    s.Delete("/api/v1/.*", any);
    s.set_default_headers({{"Access-Control-Allow-Origin", "*"}});
    _port = port == 0 ? s.bind_to_any_port(host) : (s.bind_to_port(host, port) ? port : -1);
    if (_port < 0)
    {
        if (error)
            *error = "cannot listen on " + host + ":" + std::to_string(port);
        _port = 0;
        return false;
    }
    _running = true;
    _impl->thread = std::thread([this] {
        _impl->http.listen_after_bind();
        _running = false;
    });
    return true;
}

void Server::Stop()
{
    if (!_impl || !_impl->thread.joinable())
        return;
    _impl->http.stop();
    _impl->thread.join();
    _running = false;
}

std::string Base64(const std::string& in)
{
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3)
    {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) |
                           static_cast<unsigned char>(in[i + 2]);
        out += t[v >> 18];
        out += t[(v >> 12) & 63];
        out += t[(v >> 6) & 63];
        out += t[v & 63];
    }
    if (i + 1 == in.size())
    {
        const unsigned v = static_cast<unsigned char>(in[i]) << 16;
        out += t[v >> 18];
        out += t[(v >> 12) & 63];
        out += "==";
    }
    else if (i + 2 == in.size())
    {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8);
        out += t[v >> 18];
        out += t[(v >> 12) & 63];
        out += t[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

}  // namespace sdb::automation
