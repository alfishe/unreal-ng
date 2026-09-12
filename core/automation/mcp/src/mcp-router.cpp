// MCP router tools implementation — see mcp-router.h

#include "mcp-router.h"

#include "target-resolver.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace mcp
{

/// region <OpenApiCache>

OpenApiCache::OpenApiCache(IApiCaller::Ptr caller, std::chrono::steady_clock::duration ttl)
    : _caller(std::move(caller)), _ttl(ttl)
{
}

void OpenApiCache::Get(SpecCallback done)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_hasSpec && (std::chrono::steady_clock::now() - _fetchedAt) < _ttl)
        {
            Json::Value copy = _spec;
            done(true, std::move(copy));
            return;
        }
    }

    if (!_caller)
    {
        done(false, Json::Value());
        return;
    }

    // Note: concurrent callers may trigger duplicate fetches; harmless (last write wins)
    _caller->Call("GET", "/api/v1/openapi.json", nullptr, [this, done](int status, Json::Value body) {
        bool ok = status == 200 && body.isObject() && body.isMember("paths");
        if (ok)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _spec = body;
            _fetchedAt = std::chrono::steady_clock::now();
            _hasSpec = true;
            Json::Value copy = _spec;
            done(true, std::move(copy));
        }
        else
        {
            done(false, std::move(body));
        }
    });
}

void OpenApiCache::Invalidate()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _hasSpec = false;
    _spec = Json::Value();
}

/// endregion </OpenApiCache>

/// region <Operation flattening & scoring>

namespace
{

struct ApiOperation
{
    std::string method;
    std::string path;
    std::string summary;
    std::string description;
    std::vector<std::string> tags;
    const Json::Value* parameters = nullptr; // raw OpenAPI parameters array
    const Json::Value* bodySchema = nullptr; // application/json schema or nullptr
};

std::vector<std::string> Tokenize(const std::string& query)
{
    std::vector<std::string> tokens;
    std::string current;
    for (char c : query)
    {
        if (std::isalnum(static_cast<unsigned char>(c)))
        {
            current += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        else if (!current.empty())
        {
            tokens.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty())
    {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

std::vector<std::string> SplitPathSegments(const std::string& path)
{
    std::vector<std::string> segments;
    std::string current;
    for (char c : path)
    {
        if (c == '/' || c == '{' || c == '}' || c == '-' || c == '_')
        {
            if (!current.empty())
            {
                segments.push_back(std::move(current));
                current.clear();
            }
        }
        else
        {
            current += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    if (!current.empty())
    {
        segments.push_back(std::move(current));
    }
    return segments;
}

bool ContainsCaseInsensitive(const std::string& haystack, const std::string& lowercaseToken)
{
    if (haystack.size() < lowercaseToken.size())
    {
        return false;
    }
    for (size_t i = 0; i + lowercaseToken.size() <= haystack.size(); ++i)
    {
        size_t j = 0;
        while (j < lowercaseToken.size() &&
               static_cast<char>(std::tolower(static_cast<unsigned char>(haystack[i + j]))) == lowercaseToken[j])
        {
            ++j;
        }
        if (j == lowercaseToken.size())
        {
            return true;
        }
    }
    return false;
}

std::vector<ApiOperation> FlattenOperations(const Json::Value& spec)
{
    std::vector<ApiOperation> operations;
    const Json::Value& paths = spec["paths"];
    if (!paths.isObject())
    {
        return operations;
    }

    static const char* httpMethods[] = {"get", "post", "put", "delete"};
    auto memberNames = paths.getMemberNames();
    for (const std::string& path : memberNames)
    {
        const Json::Value& pathItem = paths[path];
        if (!pathItem.isObject())
        {
            continue;
        }
        for (const char* method : httpMethods)
        {
            if (!pathItem.isMember(method))
            {
                continue;
            }
            const Json::Value& op = pathItem[method];

            ApiOperation operation;
            operation.method = method;
            std::transform(operation.method.begin(), operation.method.end(), operation.method.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            operation.path = path;
            operation.summary = op.get("summary", "").asString();
            operation.description = op.get("description", "").asString();
            if (op.isMember("tags") && op["tags"].isArray())
            {
                for (const auto& tag : op["tags"])
                {
                    operation.tags.push_back(tag.asString());
                }
            }
            if (op.isMember("parameters") && op["parameters"].isArray())
            {
                operation.parameters = &op["parameters"];
            }
            if (op.isMember("requestBody"))
            {
                const Json::Value& schema = op["requestBody"]["content"]["application/json"]["schema"];
                if (schema.isObject())
                {
                    operation.bodySchema = &schema;
                }
            }
            operations.push_back(std::move(operation));
        }
    }
    return operations;
}

/// Keyword score: path segment hits weigh most, then summary/description and tags, then parameter names
int ScoreOperation(const ApiOperation& operation, const std::vector<std::string>& tokens)
{
    if (tokens.empty())
    {
        return 0;
    }

    std::vector<std::string> segments = SplitPathSegments(operation.path);
    int score = 0;

    for (const std::string& token : tokens)
    {
        bool matched = false;

        for (const std::string& segment : segments)
        {
            if (segment == token || (segment.size() > token.size() && segment.find(token) != std::string::npos))
            {
                score += 3;
                matched = true;
                break;
            }
        }
        if (matched)
        {
            continue;
        }

        if (ContainsCaseInsensitive(operation.summary, token) || ContainsCaseInsensitive(operation.description, token))
        {
            score += 2;
            continue;
        }

        bool tagMatch = false;
        for (const std::string& tag : operation.tags)
        {
            if (ContainsCaseInsensitive(tag, token))
            {
                tagMatch = true;
                break;
            }
        }
        if (tagMatch)
        {
            score += 2;
            continue;
        }

        if (operation.parameters && operation.parameters->isArray())
        {
            for (const auto& parameter : *operation.parameters)
            {
                if (ContainsCaseInsensitive(parameter.get("name", "").asString(), token))
                {
                    score += 1;
                    matched = true;
                    break;
                }
            }
        }
    }
    return score;
}

/// Truncates a description to a bounded length for compact tool output
std::string Truncate(const std::string& text, size_t maxLength)
{
    if (text.size() <= maxLength)
    {
        return text;
    }
    return text.substr(0, maxLength) + "...";
}

/// Builds a concrete example body from an OpenAPI schema (example/default/type fallbacks)
Json::Value BuildBodyExample(const Json::Value& schema)
{
    if (!schema.isObject())
    {
        return Json::Value();
    }
    if (schema.isMember("example"))
    {
        return schema["example"];
    }
    std::string type = schema.get("type", "").asString();
    if (type == "object" && schema.isMember("properties"))
    {
        Json::Value example(Json::objectValue);
        auto names = schema["properties"].getMemberNames();
        for (const std::string& name : names)
        {
            example[name] = BuildBodyExample(schema["properties"][name]);
        }
        return example;
    }
    if (type == "array")
    {
        return Json::Value(Json::arrayValue);
    }
    if (type == "integer" || type == "number")
    {
        return Json::Value(0);
    }
    if (type == "boolean")
    {
        return Json::Value(false);
    }
    if (schema.isMember("enum") && schema["enum"].isArray() && schema["enum"].size() > 0)
    {
        return schema["enum"][0];
    }
    return Json::Value("");
}

/// Compact searchable description of one operation
Json::Value DescribeOperation(const ApiOperation& operation)
{
    Json::Value entry;
    entry["method"] = operation.method;
    entry["path"] = operation.path;
    entry["summary"] = operation.summary;

    Json::Value parameters(Json::arrayValue);
    if (operation.parameters && operation.parameters->isArray())
    {
        for (const auto& parameter : *operation.parameters)
        {
            Json::Value param;
            param["name"] = parameter.get("name", "").asString();
            param["in"] = parameter.get("in", "").asString();
            param["required"] = parameter.get("required", false);
            param["type"] = parameter["schema"].get("type", "").asString();
            std::string description = parameter.get("description", "").asString();
            if (!description.empty())
            {
                param["description"] = Truncate(description, 120);
            }
            parameters.append(param);
        }
    }
    entry["parameters"] = parameters;

    if (operation.bodySchema)
    {
        entry["body_example"] = BuildBodyExample(*operation.bodySchema);
    }
    return entry;
}

} // namespace

/// endregion </Operation flattening & scoring>

/// region <Router tools>

namespace
{

std::string DescribeBody(const Json::Value& body);

/// Shared invocation path used by invoke_api and search_api(auto_invoke)
void InvokeEndpoint(const std::string& method, const std::string& rawPath, const Json::Value* body, const Json::Value& query,
                    const Json::Value& args, IApiCaller& caller, ToolCallback done)
{
    auto invoke = [method, rawPath, body, query, &caller, done](const std::string& id) {
        std::string path = rawPath;
        if (!id.empty())
        {
            size_t position;
            while ((position = path.find("{id}")) != std::string::npos)
            {
                path.replace(position, 4, id);
            }
        }

        if (query.isObject() && query.size() > 0)
        {
            std::string queryString;
            auto names = query.getMemberNames();
            for (const std::string& name : names)
            {
                if (!queryString.empty())
                {
                    queryString += "&";
                }
                queryString += name + "=" + query[name].asString();
            }
            path += (path.find('?') == std::string::npos ? "?" : "&") + queryString;
        }

        caller.Call(method, path, body, [method, path, done](int status, Json::Value responseBody) {
            Json::Value structured;
            structured["status"] = status;
            structured["body"] = responseBody;

            std::ostringstream out;
            out << "HTTP " << status << " " << method << " " << path;
            if (responseBody.isObject())
            {
                std::string message = DescribeBody(responseBody);
                if (!message.empty())
                {
                    out << "\n" << message;
                }
            }

            if (status >= 200 && status < 300)
            {
                done(ToolResult::Ok(out.str(), std::move(structured)));
            }
            else if (status == 0)
            {
                done(ToolResult::Error("WebAPI unreachable — is the emulator running with WebAPI enabled (port 8090)?"));
            }
            else
            {
                done(ToolResult::Error(out.str()));
            }
        });
    };

    if (rawPath.find("{id}") != std::string::npos)
    {
        TargetResolver::ResolveFromArgs(args, caller, [invoke, done](bool ok, const std::string& idOrError) {
            if (!ok)
            {
                done(ToolResult::Error(idOrError));
                return;
            }
            invoke(idOrError);
        });
    }
    else
    {
        invoke(std::string());
    }
}

std::string DescribeBody(const Json::Value& body)
{
    if (!body.isObject())
    {
        return "";
    }
    if (body.isMember("message"))
    {
        return body["message"].asString();
    }
    if (body.isMember("error"))
    {
        return body["error"].asString();
    }
    return "";
}

void RegisterSearchApi(ToolRegistry& registry, std::shared_ptr<OpenApiCache> cache, IApiCaller::Ptr caller)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["query"]["type"] = "string";
    schema["properties"]["query"]["description"] = "Keywords, e.g. 'tape play', 'disk catalog', 'registers', 'breakpoint'";
    schema["properties"]["method"]["type"] = "string";
    schema["properties"]["method"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* m : {"GET", "POST", "PUT", "DELETE"})
    {
        schema["properties"]["method"]["enum"].append(m);
    }
    schema["properties"]["method"]["description"] = "Optional HTTP method filter";
    schema["properties"]["limit"]["type"] = "integer";
    schema["properties"]["limit"]["default"] = 5;
    schema["properties"]["limit"]["description"] = "Max matches to return (1-20)";
    schema["properties"]["auto_invoke"]["type"] = "boolean";
    schema["properties"]["auto_invoke"]["default"] = false;
    schema["properties"]["auto_invoke"]["description"] =
        "Execute the endpoint immediately when the search yields exactly one match";
    schema["properties"]["body"]["type"] = "object";
    schema["properties"]["body"]["description"] = "Request body for auto_invoke (POST/PUT)";
    schema["properties"]["query_params"]["type"] = "object";
    schema["properties"]["query_params"]["description"] = "Query parameters for auto_invoke";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["required"].append("query");

    registry.Register(
        "search_api",
        "Search the full emulator WebAPI (100+ REST endpoints) by keywords. Returns matching endpoints with parameters and a "
        "ready-to-use example body. Chain with invoke_api, or set auto_invoke to execute a unique match directly.",
        std::move(schema),
        [cache, caller](const Json::Value& args, IApiCaller& apiCaller, ToolCallback done, const ProgressFn&) {
            std::string query = args["query"].asString();
            if (query.empty())
            {
                done(ToolResult::Error("Missing 'query' argument"));
                return;
            }
            std::string methodFilter = args.isMember("method") ? args["method"].asString() : "";
            std::transform(methodFilter.begin(), methodFilter.end(), methodFilter.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            unsigned limit = args.isMember("limit") ? args["limit"].asUInt() : 5u;
            if (limit < 1) limit = 1;
            if (limit > 20) limit = 20;
            bool autoInvoke = args.isMember("auto_invoke") && args["auto_invoke"].asBool();

            cache->Get([query, methodFilter, limit, autoInvoke, &args, &apiCaller, done](bool ok, Json::Value spec) {
                if (!ok)
                {
                    done(ToolResult::Error("Cannot fetch the OpenAPI spec — is the emulator running with WebAPI enabled?"));
                    return;
                }

                std::vector<std::string> tokens = Tokenize(query);
                std::vector<ApiOperation> operations = FlattenOperations(spec);

                struct ScoredOperation
                {
                    int score;
                    ApiOperation operation;
                };
                std::vector<ScoredOperation> scored;
                for (ApiOperation& operation : operations)
                {
                    if (!methodFilter.empty() && operation.method != methodFilter)
                    {
                        continue;
                    }
                    int score = ScoreOperation(operation, tokens);
                    if (score > 0)
                    {
                        scored.push_back({score, std::move(operation)});
                    }
                }

                std::sort(scored.begin(), scored.end(), [](const ScoredOperation& a, const ScoredOperation& b) {
                    if (a.score != b.score)
                    {
                        return a.score > b.score;
                    }
                    return a.operation.path.size() < b.operation.path.size();
                });

                if (scored.empty())
                {
                    done(ToolResult::Ok("No WebAPI endpoints match '" + query + "'. Try different keywords (e.g. 'tape', 'disk', 'registers')."));
                    return;
                }

                if (autoInvoke && scored.size() == 1)
                {
                    // Collapse search + invoke into one round-trip
                    Json::Value match = DescribeOperation(scored[0].operation);
                    const Json::Value* body = args.isMember("body") && args["body"].isObject() ? &args["body"] : nullptr;
                    const Json::Value queryParams = args.isMember("query_params") ? args["query_params"] : Json::Value();
                    InvokeEndpoint(scored[0].operation.method, scored[0].operation.path, body, queryParams, args, apiCaller,
                                   [match, done](ToolResult result) {
                                       if (!result.isError && result.structured.isObject())
                                       {
                                           Json::Value structured;
                                           structured["match"] = match;
                                           structured["invocation"] = result.structured;
                                           done(ToolResult::Ok(result.text + "\n(auto-invoked unique match)", std::move(structured)));
                                       }
                                       else
                                       {
                                           done(std::move(result));
                                       }
                                   });
                    return;
                }

                size_t returned = std::min<size_t>(limit, scored.size());
                Json::Value matches(Json::arrayValue);
                std::ostringstream out;
                out << returned << " match(es) for '" << query << "':";
                for (size_t i = 0; i < returned; ++i)
                {
                    matches.append(DescribeOperation(scored[i].operation));
                    out << "\n" << (i + 1) << ". " << scored[i].operation.method << " " << scored[i].operation.path;
                    if (!scored[i].operation.summary.empty())
                    {
                        out << " — " << scored[i].operation.summary;
                    }
                }
                Json::Value structured;
                structured["matches"] = matches;
                structured["total_matches"] = static_cast<Json::UInt>(scored.size());
                done(ToolResult::Ok(out.str(), std::move(structured)));
            });
        });
}

void RegisterInvokeApi(ToolRegistry& registry, IApiCaller::Ptr caller)
{
    (void)caller;

    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["method"]["type"] = "string";
    schema["properties"]["method"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* m : {"GET", "POST", "PUT", "DELETE"})
    {
        schema["properties"]["method"]["enum"].append(m);
    }
    schema["properties"]["path"]["type"] = "string";
    schema["properties"]["path"]["description"] =
        "WebAPI path, e.g. /api/v1/emulator/{id}/tape/info. '{id}' is replaced with the resolved target automatically";
    schema["properties"]["body"]["type"] = "object";
    schema["properties"]["body"]["description"] = "JSON request body for POST/PUT";
    schema["properties"]["query_params"]["type"] = "object";
    schema["properties"]["query_params"]["description"] = "Query string parameters";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["required"].append("method");
    schema["required"].append("path");

    registry.Register(
        "invoke_api",
        "Execute any emulator WebAPI endpoint discovered via search_api. Handles '{id}' substitution from the target and "
        "returns the raw HTTP status and JSON body.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& apiCaller, ToolCallback done, const ProgressFn&) {
            std::string method = args["method"].asString();
            std::string path = args["path"].asString();
            if (method.empty() || path.empty())
            {
                done(ToolResult::Error("Both 'method' and 'path' are required"));
                return;
            }
            if (path.find("/api/v1/") != 0)
            {
                done(ToolResult::Error("Path must start with /api/v1/ — see search_api for available endpoints"));
                return;
            }
            const Json::Value* body = args.isMember("body") && args["body"].isObject() ? &args["body"] : nullptr;
            const Json::Value queryParams = args.isMember("query_params") ? args["query_params"] : Json::Value();
            InvokeEndpoint(method, path, body, queryParams, args, apiCaller, done);
        });
}

} // namespace

void RegisterRouterTools(ToolRegistry& registry, IApiCaller::Ptr caller)
{
    auto cache = std::make_shared<OpenApiCache>(caller);
    RegisterSearchApi(registry, std::move(cache), std::move(caller));
    RegisterInvokeApi(registry, nullptr);
}

/// endregion </Router tools>

} // namespace mcp
