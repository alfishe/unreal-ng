#include "mcp-jobs.h"

#include <memory>
#include <string>

#include "mcp-tool-utils.h"
#include "target-resolver.h"

namespace mcp
{

namespace
{
const char* const kActions[] = {"list", "status", "wait", "cancel", "start"};

/// The job id as a decimal string ("12", 12, "#12"); empty when absent or not a number
std::string JobId(const Json::Value& args)
{
    const Json::Value& v = args["job"];
    std::string text;
    if (v.isUInt64() || v.isUInt())
        text = std::to_string(v.asUInt64());
    else if (v.isString())
        text = v.asString();
    if (!text.empty() && text[0] == '#')
        text.erase(0, 1);
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        return {};
    return text;
}

/// A scalar option of the verb as JSON (an object or array of non-scalars refused)
bool CopyOption(const std::string& name, const Json::Value& value, Json::Value& body, std::string& error)
{
    if (value.isObject())
    {
        error = "options." + name + ": a number, a string, true / false or a list of them";
        return false;
    }
    body[name] = value;
    return true;
}
}  // namespace

bool JobsRequest(const Json::Value& args, const std::string& emulatorId, std::string& method, std::string& path,
                 Json::Value& body, std::string& error)
{
    const std::string action = args["action"].asString();
    body = Json::Value(Json::objectValue);
    if (action == "list")
    {
        method = "GET";
        path = emulatorId.empty() ? "/api/v1/jobs" : Endpoint(emulatorId, "/jobs");
        std::string query;
        for (const char* field : {"state", "limit"})
            if (args.isMember(field))
                query += std::string(query.empty() ? "?" : "&") + field + "=" + UrlEncodeSegment(args[field].asString());
        path += query;
        return true;
    }
    if (action == "start")
    {
        if (emulatorId.empty())
        {
            error = "start: a job runs on an instance (target)";
            return false;
        }
        const std::string domain = args["domain"].asString();
        const std::string verb = args["verb"].asString();
        if (domain != "ttd" && domain != "analysis")
        {
            error = "start: domain is ttd or analysis";
            return false;
        }
        if (verb.empty())
        {
            error = "start: verb is the long verb (ttd: dump, load, probe-frames, memory-series, find-event, "
                    "replay-check, export-clip, journal-build, branch-create; analysis: build, trace, experiment, cover)";
            return false;
        }
        method = "POST";
        path = Endpoint(emulatorId, "/jobs");
        body["domain"] = domain;
        body["verb"] = verb;
        const Json::Value& options = args["options"];
        if (!options.isNull() && !options.isObject())
        {
            error = "options: an object of the verb's options ({\"path\": \"/x.ttd\"})";
            return false;
        }
        if (options.isObject())
            for (const std::string& name : options.getMemberNames())
                if (!CopyOption(name, options[name], body, error))
                    return false;
        return true;
    }
    const std::string job = JobId(args);
    if (job.empty())
    {
        error = action + ": job is the job's id (a positive integer; jobs action=list shows them)";
        return false;
    }
    if (action == "status" || action == "wait")
    {
        method = "GET";
        path = "/api/v1/jobs/" + job;
        std::string query;
        if (action == "wait")
            query = "?wait_ms=" + std::to_string(args.isMember("wait_ms") ? args["wait_ms"].asUInt64() : 30000);
        if (args.isMember("result") && args["result"].isBool() && !args["result"].asBool())
            query += std::string(query.empty() ? "?" : "&") + "result=false";
        path += query;
        return true;
    }
    if (action == "cancel")
    {
        method = "POST";
        path = "/api/v1/jobs/" + job + "/cancel";
        return true;
    }
    error = UnknownValueText("action", action, std::vector<std::string>(std::begin(kActions), std::end(kActions)));
    return false;
}

void RegisterJobs(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["description"] =
        "Emulator id, or 'auto' (start: the instance the job runs on; list: only its jobs, absent: every instance's)";
    schema["properties"]["action"]["type"] = "string";
    for (const char* action : kActions)
        schema["properties"]["action"]["enum"].append(action);
    schema["properties"]["job"]["type"] = "integer";
    schema["properties"]["job"]["description"] = "status / wait / cancel: the job's id (an async call answers it)";
    schema["properties"]["wait_ms"]["type"] = "integer";
    schema["properties"]["wait_ms"]["description"] =
        "wait: answer when the job ends or after this many ms, whichever is first (default 30000, at most 60000; "
        "still running: wait again)";
    schema["properties"]["result"]["type"] = "boolean";
    schema["properties"]["result"]["description"] = "status / wait: false leaves the result out (default true)";
    schema["properties"]["state"]["type"] = "string";
    schema["properties"]["state"]["enum"].append("active");
    schema["properties"]["state"]["enum"].append("finished");
    schema["properties"]["state"]["enum"].append("all");
    schema["properties"]["state"]["description"] = "list: active (queued, running), finished or all (default)";
    schema["properties"]["limit"]["type"] = "integer";
    schema["properties"]["limit"]["description"] = "list: the newest N (default 50, 0 = all kept)";
    schema["properties"]["domain"]["type"] = "string";
    schema["properties"]["domain"]["enum"].append("ttd");
    schema["properties"]["domain"]["enum"].append("analysis");
    schema["properties"]["domain"]["description"] = "start: whose verb";
    schema["properties"]["verb"]["type"] = "string";
    schema["properties"]["verb"]["description"] =
        "start: ttd dump, load, probe-frames, memory-series, find-event, replay-check, export-clip, journal-build, "
        "branch-create; analysis build, trace, experiment, cover";
    schema["properties"]["options"]["type"] = "object";
    schema["properties"]["options"]["description"] =
        "start: the verb's own options as its WebAPI route takes them ({\"path\": \"/tmp/a.ttd\"})";
    schema["required"].append("action");

    registry.Register(
        "jobs",
        "Long verbs as jobs: a TTD dump / load / probe_frames / memory_series / journal_build / export_clip with "
        "async=true (time_travel), an analysis build / trace / experiment / cover with async=true (analysis), or any "
        "of them here with action=start (domain, verb, options) answers the job id at once. action=wait long-polls "
        "for its end (wait_ms up to 60000, then wait again) and answers its state and result; status reads it now; "
        "cancel stops a queued job, or asks a running one to stop (probe_frames, memory_series, journal_build, "
        "analysis build; others run to their end); list shows the jobs (queued, running, done, failed, cancelled; the "
        "last 64 finished are kept up to 30 min). One job runs at a time per instance and domain, in order.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string action = args["action"].asString();
            auto send = [&caller, done](const std::string& method, const std::string& path,
                                        std::shared_ptr<Json::Value> body) {
                const Json::Value* payload = method == "POST" ? body.get() : nullptr;
                caller.Call(method, path, payload, [body, done](int status, Json::Value reply) {
                    if (status >= 200 && status < 300)
                    {
                        std::string text = reply.get("text", "").asString();
                        done(ToolResult::Ok(text.empty() ? "OK" : text, std::move(reply)));
                        return;
                    }
                    if (status == 0)
                    {
                        done(ToolResult::Error("WebAPI unreachable — is the emulator running with WebAPI enabled (port 8090)?"));
                        return;
                    }
                    std::string text = "WebAPI returned HTTP " + std::to_string(status);
                    const std::string details = DescribeErrorBody(reply);
                    if (!details.empty())
                        text += ": " + details;
                    done(ToolResult::Error(text));
                });
            };
            const bool needsTarget = action == "start" || (action == "list" && args.isMember("target"));
            if (!needsTarget)
            {
                std::string method;
                std::string path;
                auto body = std::make_shared<Json::Value>();
                std::string error;
                if (!JobsRequest(args, "", method, path, *body, error))
                {
                    done(ToolResult::Error(error));
                    return;
                }
                send(method, path, body);
                return;
            }
            auto argsCopy = std::make_shared<Json::Value>(args);
            TargetResolver::ResolveFromArgs(args, caller, [argsCopy, send, done](bool ok, const std::string& idOrError) {
                if (!ok)
                {
                    done(ToolResult::Error(idOrError));
                    return;
                }
                std::string method;
                std::string path;
                auto body = std::make_shared<Json::Value>();
                std::string error;
                if (!JobsRequest(*argsCopy, idOrError, method, path, *body, error))
                {
                    done(ToolResult::Error(error));
                    return;
                }
                send(method, path, body);
            });
        });
}

} // namespace mcp
