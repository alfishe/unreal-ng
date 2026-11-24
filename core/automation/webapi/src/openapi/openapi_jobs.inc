    // Jobs (PLAN #122 S2, docs/emulator/design/control-interfaces/jobs.md): the long verbs with async=true answer
    // 202 with the job at once; list, status, the long-poll (wait_ms, no server thread held) and cancel. Shared with
    // CLI `jobs`, Lua / Python jobs() and MCP `jobs` (jobs::Execute, core/src/emulator/jobs/jobcontrol.h).
    // Included after openapi_ttd.inc and openapi_analysislayers.inc: it adds `async` to their long verbs.
    {
        auto pathParam = [](const char* name, const char* description) {
            Json::Value p;
            p["name"] = name;
            p["in"] = "path";
            p["required"] = true;
            p["schema"]["type"] = "string";
            p["description"] = description;
            return p;
        };
        auto queryParam = [](const char* name, const char* type, const char* description) {
            Json::Value p;
            p["name"] = name;
            p["in"] = "query";
            p["required"] = false;
            p["schema"]["type"] = type;
            p["description"] = description;
            return p;
        };
        auto jobRef = [](Json::Value& response, const char* description) {
            response["description"] = description;
            response["content"]["application/json"]["schema"]["$ref"] = "#/components/schemas/Job";
        };
        const char* kStateText = "active (queued, running), finished (done, failed, cancelled) or all (default)";

        Json::Value& list = paths["/api/v1/jobs"]["get"];
        list["summary"] = "Every instance's jobs";
        list["tags"].append("Jobs");
        list["description"] =
            "The jobs, oldest first (the newest `limit`): queued, running and the last 64 finished (kept 30 min). "
            "instance=<id> filters. The answer carries the registry's limits.";
        list["parameters"].append(queryParam("instance", "string", "all (default) or an emulator id"));
        list["parameters"].append(queryParam("state", "string", kStateText));
        list["parameters"].append(queryParam("limit", "integer", "the newest N, 0..1000 (default 50, 0: all)"));
        list["responses"]["200"]["description"] = "The jobs";
        list["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] = "#/components/schemas/JobList";
        list["responses"]["400"]["description"] = "Unknown option or a bad value";

        Json::Value& instanceList = paths["/api/v1/emulator/{id}/jobs"]["get"];
        instanceList["summary"] = "The instance's jobs";
        instanceList["tags"].append("Jobs");
        instanceList["parameters"].append(pathParam("id", "Emulator id or index"));
        instanceList["parameters"].append(queryParam("state", "string", kStateText));
        instanceList["parameters"].append(queryParam("limit", "integer", "the newest N, 0..1000 (default 50)"));
        instanceList["responses"]["200"]["description"] = "The jobs";
        instanceList["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
            "#/components/schemas/JobList";

        Json::Value& start = paths["/api/v1/emulator/{id}/jobs"]["post"];
        start["summary"] = "Run a long verb as a job";
        start["tags"].append("Jobs");
        start["description"] =
            "The same as the verb with async=true: domain ttd (dump, load, probe-frames, memory-series, find-event, "
            "replay-check, export-clip, journal-build, branch-create) or analysis (build, trace, experiment, cover), "
            "the verb's own options beside (scalars, lists of scalars).";
        start["parameters"].append(pathParam("id", "Emulator id or index"));
        Json::Value& startBody = start["requestBody"]["content"]["application/json"]["schema"];
        startBody["type"] = "object";
        startBody["required"].append("domain");
        startBody["required"].append("verb");
        startBody["properties"]["domain"]["type"] = "string";
        startBody["properties"]["domain"]["enum"].append("ttd");
        startBody["properties"]["domain"]["enum"].append("analysis");
        startBody["properties"]["verb"]["type"] = "string";
        startBody["additionalProperties"] = true;
        start["responses"]["202"]["description"] = "Queued";
        start["responses"]["202"]["content"]["application/json"]["schema"]["$ref"] =
            "#/components/schemas/JobAccepted";
        start["responses"]["400"]["description"] = "Unknown domain or verb, a verb that is not long, a bad option";
        start["responses"]["404"]["description"] = "Emulator not found";
        start["responses"]["409"]["description"] = "The verb refused (no recording, ...) or the queue is full";
        start["responses"]["503"]["description"] = "The emulator is shutting down";

        Json::Value& get = paths["/api/v1/jobs/{job}"]["get"];
        get["summary"] = "One job; with wait_ms a long-poll for its end";
        get["tags"].append("Jobs");
        get["description"] =
            "wait_ms 0 or absent: the job now. 1..60000: answered when the job ends or after wait_ms, whichever is "
            "first, with ended and wait_ms; no server thread waits (a watch on the registry and a timer). More than "
            "60000 is refused. With 256 long-polls waiting already the answer is at once (ended false).";
        get["parameters"].append(pathParam("job", "The job id"));
        get["parameters"].append(queryParam("wait_ms", "integer", "Long-poll up to this many ms (0..60000)"));
        get["parameters"].append(queryParam("result", "boolean", "false: without the result (default true)"));
        jobRef(get["responses"]["200"], "The job");
        get["responses"]["400"]["description"] = "A bad job id or wait_ms";
        get["responses"]["404"]["description"] = "No such job (or forgotten)";

        for (const char* method : {"delete", "post"})
        {
            Json::Value& cancel =
                paths[std::string(method) == "delete" ? "/api/v1/jobs/{job}" : "/api/v1/jobs/{job}/cancel"][method];
            cancel["summary"] = "Cancel a job";
            cancel["tags"].append("Jobs");
            cancel["description"] =
                "A queued job ends cancelled and never runs; a running one is asked to stop (probe-frames, "
                "memory-series, journal-build, analysis build stop early with what they got; the others cannot: 409).";
            cancel["parameters"].append(pathParam("job", "The job id"));
            jobRef(cancel["responses"]["200"], "Cancelled (cancel: cancelled) or asked (cancel: requested)");
            cancel["responses"]["404"]["description"] = "No such job";
            cancel["responses"]["409"]["description"] = "It ended already, or its work cannot stop";
        }

        // async on the long verbs: a body member (POST) or a query parameter (GET)
        const char* kAsync = "Run it as a job: 202 with {job, state, status_url} at once (Jobs)";
        for (const char* route : {"/api/v1/emulator/{id}/ttd/dump", "/api/v1/emulator/{id}/ttd/load",
                                  "/api/v1/emulator/{id}/ttd/probe-frames", "/api/v1/emulator/{id}/ttd/memory-series",
                                  "/api/v1/emulator/{id}/ttd/export-clip", "/api/v1/emulator/{id}/ttd/journal/build",
                                  "/api/v1/emulator/{id}/ttd/branches", "/api/v1/emulator/{id}/analysis/build",
                                  "/api/v1/emulator/{id}/analysis/trace", "/api/v1/emulator/{id}/analysis/experiment",
                                  "/api/v1/emulator/{id}/analysis/cover"})
        {
            if (!paths.isMember(route) || !paths[route].isMember("post"))
                continue;
            Json::Value& op = paths[route]["post"];
            op["parameters"].append(queryParam("async", "boolean", kAsync));
            op["responses"]["202"]["description"] = kAsync;
            op["responses"]["202"]["content"]["application/json"]["schema"]["$ref"] =
                "#/components/schemas/JobAccepted";
        }
        for (const char* route : {"/api/v1/emulator/{id}/ttd/find-event", "/api/v1/emulator/{id}/ttd/replay-check"})
        {
            if (!paths.isMember(route) || !paths[route].isMember("get"))
                continue;
            Json::Value& op = paths[route]["get"];
            op["parameters"].append(queryParam("async", "boolean", kAsync));
            op["responses"]["202"]["description"] = kAsync;
            op["responses"]["202"]["content"]["application/json"]["schema"]["$ref"] =
                "#/components/schemas/JobAccepted";
        }
    }
