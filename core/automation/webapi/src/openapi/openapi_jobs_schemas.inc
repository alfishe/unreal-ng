    // Jobs (openapi_jobs.inc)
    {
        Json::Value& job = schemas["Job"];
        job["type"] = "object";
        job["description"] = "A job (jobs::Info::ToNode); status, error, text and result once it ended";
        for (const char* name : {"job", "created_ms", "started_ms", "ended_ms", "elapsed_ms", "queue_position",
                                 "status", "wait_ms"})
            job["properties"][name]["type"] = "integer";
        for (const char* name : {"kind", "emulator_id", "summary", "error", "text", "cancel"})
            job["properties"][name]["type"] = "string";
        job["properties"]["state"]["type"] = "string";
        for (const char* state : {"queued", "running", "done", "failed", "cancelled"})
            job["properties"]["state"]["enum"].append(state);
        for (const char* name : {"cancellable", "cancel_requested", "result_dropped", "ended"})
            job["properties"][name]["type"] = "boolean";
        Json::Value& progress = job["properties"]["progress"];
        progress["type"] = "object";
        for (const char* name : {"done", "total", "percent"})
            progress["properties"][name]["type"] = "integer";
        progress["properties"]["unit"]["type"] = "string";
        job["properties"]["result"]["type"] = "object";
        job["properties"]["result"]["description"] = "The verb's answer, as the call without async gives it";

        Json::Value& accepted = schemas["JobAccepted"];
        accepted["type"] = "object";
        accepted["description"] = "A long verb queued as a job (HTTP 202)";
        accepted["properties"]["job"]["type"] = "integer";
        for (const char* name : {"state", "kind", "emulator_id", "summary", "status_url", "wait_url", "text"})
            accepted["properties"][name]["type"] = "string";
        accepted["properties"]["async"]["type"] = "boolean";

        Json::Value& list = schemas["JobList"];
        list["type"] = "object";
        for (const char* name : {"instance", "state", "text"})
            list["properties"][name]["type"] = "string";
        for (const char* name : {"count", "total", "active"})
            list["properties"][name]["type"] = "integer";
        list["properties"]["jobs"]["type"] = "array";
        list["properties"]["jobs"]["items"]["$ref"] = "#/components/schemas/Job";
        Json::Value& limits = list["properties"]["limits"];
        limits["type"] = "object";
        for (const char* name : {"workers", "max_queued", "keep_finished", "keep_ms", "keep_result_bytes", "max_wait_ms"})
            limits["properties"][name]["type"] = "integer";
    }
