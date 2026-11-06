    // CPU time profile (profile::Execute, docs/emulator/analysis/cpu-profile.md): T-states per routine found at run
    // time, self and inclusive, marks, the per-frame series, exports
    {
        const auto idParameter = [](Json::Value& operation) {
            operation["parameters"][0]["name"] = "id";
            operation["parameters"][0]["in"] = "path";
            operation["parameters"][0]["required"] = true;
            operation["parameters"][0]["schema"]["type"] = "string";
        };
        const auto queryParameter = [](Json::Value& operation, const char* name, const char* type, const char* text) {
            Json::Value parameter;
            parameter["name"] = name;
            parameter["in"] = "query";
            parameter["required"] = false;
            parameter["schema"]["type"] = type;
            parameter["description"] = text;
            operation["parameters"].append(parameter);
        };
        const char* const prefix = "/api/v1/emulator/{id}/cpuprofile/";
        const auto route = [&](const char* op, const char* method) -> Json::Value& {
            return paths[std::string(prefix) + op][method];
        };

        Json::Value& start = route("start", "post");
        start["summary"] = "Start the CPU time profile";
        start["tags"].append("Profiler Commands");
        start["description"] =
            "Books every instruction's T-states to its physical address (page + offset) and, with stack, to the "
            "routines found at run time from CALL / RST / interrupt entries (no map or labels needed): self and "
            "inclusive time, calls, callers, and the attribution notes (JP entries, PUSH + RET dispatches, frames "
            "left by SP moves, patched returns). The CPU is held for the switch. A first start, reset=true or another "
            "stack setting starts a fresh window; otherwise the window goes on.";
        idParameter(start);
        start["requestBody"]["required"] = false;
        {
            Json::Value& schema = start["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            schema["properties"]["stack"]["type"] = "boolean";
            schema["properties"]["stack"]["description"] = "Routines with self / inclusive time (default true)";
            schema["properties"]["beam"]["type"] = "boolean";
            schema["properties"]["beam"]["description"] = "Self time per 1/64 of the frame (default false)";
            schema["properties"]["frames"]["type"] = "boolean";
            schema["properties"]["frames"]["description"] = "The per-frame series ring (default true)";
            schema["properties"]["ring_kib"]["type"] = "integer";
            schema["properties"]["ring_kib"]["description"] = "The series ring's size in KiB (default 4096)";
            schema["properties"]["stream"]["type"] = "string";
            schema["properties"]["stream"]["description"] = "A JSONL file the per-frame series is appended to";
            schema["properties"]["reset"]["type"] = "boolean";
            schema["properties"]["reset"]["description"] = "Start a fresh window";
        }
        start["responses"]["200"]["description"] = "The status (as GET status) and text";
        start["responses"]["400"]["description"] = "An unknown option or a value that does not parse";
        start["responses"]["503"]["description"] = "No debug manager on this instance";

        Json::Value& stop = route("stop", "post");
        stop["summary"] = "Stop the CPU time profile (the counters stay readable)";
        stop["tags"].append("Profiler Commands");
        idParameter(stop);
        stop["responses"]["200"]["description"] = "The status and text";

        Json::Value& reset = route("reset", "post");
        reset["summary"] = "Zero the CPU time profile (marks dropped)";
        reset["tags"].append("Profiler Commands");
        idParameter(reset);
        reset["responses"]["200"]["description"] = "The status and text";

        Json::Value& mark = route("mark", "post");
        mark["summary"] = "Set or drop a named baseline";
        mark["tags"].append("Profiler Commands");
        mark["description"] = "Copies the counters as the baseline name: since=name reads the time after it. At most 8.";
        idParameter(mark);
        {
            Json::Value& schema = mark["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            schema["properties"]["name"]["type"] = "string";
            schema["properties"]["drop"]["type"] = "boolean";
            schema["required"].append("name");
        }
        mark["responses"]["200"]["description"] = "name, frame, marks, text";
        mark["responses"]["404"]["description"] = "drop: no such mark";
        mark["responses"]["409"]["description"] = "No profile yet, or 8 marks already";

        Json::Value& status = route("status", "get");
        status["summary"] = "CPU time profile totals (polled: no hold, no copy)";
        status["tags"].append("Profiler Commands");
        idParameter(status);
        status["responses"]["200"]["description"] =
            "active, stack, beam, frames, start_frame, frame, instructions, tstates, interrupts, acknowledges, "
            "routines, pages, discontinuities, marks, text";

        Json::Value& report = route("report", "get");
        report["summary"] = "Routines by self / inclusive time";
        report["tags"].append("Profiler Commands");
        idParameter(report);
        queryParameter(report, "since", "string", "The time after this mark");
        queryParameter(report, "top", "integer", "Rows (default 24)");
        queryParameter(report, "sort", "string", "self (default), inclusive or calls");
        queryParameter(report, "callers", "boolean", "The callers of each routine (default true)");
        queryParameter(report, "beam", "boolean", "Self time per 1/64 of the frame (when recorded)");
        report["responses"]["200"]["description"] =
            "format, version, window, routines [{name, entry, page, kinds, calls, self, inclusive, self_permille, "
            "inclusive_permille, callers, notes}], text";
        report["responses"]["404"]["description"] = "No such mark";

        Json::Value& frames = route("frames", "get");
        frames["summary"] = "The per-frame series";
        frames["tags"].append("Profiler Commands");
        idParameter(frames);
        queryParameter(frames, "last", "integer", "The last N frames (default 50)");
        queryParameter(frames, "top", "integer", "Routines per frame (default 8)");
        frames["responses"]["200"]["description"] = "frames [{frame, tstates, instructions, routines}], frames_kept, frames_lost, text";
        frames["responses"]["409"]["description"] = "The series is off";

        Json::Value& exportOp = route("export", "post");
        exportOp["summary"] = "Export the profile (kit, json, entries, sym, ann)";
        exportOp["tags"].append("Profiler Commands");
        exportOp["description"] =
            "kit: the L1 subset kit/fig_budget.py reads (pages, routines.profile, instructions); json: the full "
            "profile; entries: the routine entries found (JSON); sym: label = $ADDR lines; ann: an .ann seed. Written "
            "to path on the emulator's host, or answered in content.";
        idParameter(exportOp);
        {
            Json::Value& schema = exportOp["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            schema["properties"]["format"]["type"] = "string";
            for (const char* format : {"kit", "json", "entries", "sym", "ann"})
                schema["properties"]["format"]["enum"].append(format);
            schema["properties"]["path"]["type"] = "string";
            schema["properties"]["since"]["type"] = "string";
            schema["required"].append("format");
        }
        exportOp["responses"]["200"]["description"] = "format, path, bytes (or content), text";
        exportOp["responses"]["409"]["description"] = "No profile yet, or the file cannot be written";
    }
