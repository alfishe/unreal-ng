    // Monitoring (monitoring::Call, core/src/emulator/monitoring/monitoringapi.h; command-interface.md §17): the host
    // process, the clients, each instance's measured pace and its summaries, read-only; the verbs of the CLI
    // `monitor`, MCP inspect_state aspect `monitor`, Lua / Python monitor(). Every route is also a WebSocket call, and
    // each name but breakpoint_groups is a /api/v1/ws topic of the same name
    {
        auto op = [&paths](const char* path, const char* summary, const char* description,
                           const char* ok) -> Json::Value& {
            Json::Value& operation = paths[path]["get"];
            operation["summary"] = summary;
            operation["description"] = description;
            operation["tags"].append("Monitoring");
            operation["responses"]["200"]["description"] = ok;
            operation["responses"]["404"]["description"] = "Not Found: no such name, or no such instance";
            return operation;
        };
        auto param = [](Json::Value& operation, const char* name, const char* in, bool required,
                        const char* description) {
            Json::Value parameter;
            parameter["name"] = name;
            parameter["in"] = in;
            parameter["required"] = required;
            parameter["description"] = description;
            parameter["schema"]["type"] = "string";
            operation["parameters"].append(parameter);
        };
        const char* instanceText = "An instance id or index (default: every instance for /monitor, the followed one "
                                   "for a name)";

        Json::Value& all = op("/api/v1/monitor", "Everything a monitoring page shows",
                              "The host process, the clients and every instance (or the one named) with each "
                              "summary; published state only, never a wait for an emulation thread",
                              "{ok, host: host.stats, clients: host.clients, instances: [{instance, index, model, "
                              "emulator.stats, debug.breakpoints, media.slots, recording.state, debug.labels, "
                              "ttd.status, debug.analyzers, controllers.state}]}");
        param(all, "instance", "query", false, instanceText);

        Json::Value& name = op(
            "/api/v1/monitor/{name}", "One monitoring value",
            "host.stats: {cpu_percent (100 = one core), cpu_percent_host, cores, rss_bytes, peak_rss_bytes, "
            "virtual_bytes, threads, cpu_time_s, uptime_s, interval_s}; host.clients: per plane (rest, ws, events, "
            "debug_ws, mcp, cli) {connections, opened, requests}, companions, instances; emulator.stats: {state, "
            "measuring, fps, nominal_fps, speed_percent, frame_time_us {avg, max}, frame_budget_percent, "
            "halted_percent, interrupts_per_frame, frame, t_states, turbo, cpu {mhz, base_mhz, multiplier, "
            "host_multiplier, hw_turbo_ratio}}; debug.breakpoints: {total, active, inactive, owned, by_kind, groups, "
            "last_hit}; media.slots: {slots [{id, kind, present, pending, name, format, dirty, changed_units, "
            "protect}], loaded, dirty}; recording.state: {recording, paused, mode, file, frames, duration_s, "
            "file_bytes, fps}; debug.labels: {total, sets}; ttd.status: {state, recording, history, frame, first, "
            "end, frames, checkpoints, memory_bytes, history_bytes, bookmarks, ...}; debug.analyzers: {opcode, memory, "
            "calltrace, coverage, pchist}; controllers.state: {connectors, plugged}; breakpoint_groups: {groups "
            "[{name, total, active, ids}]}",
            "{ok, instance? (an instance's name), ...the value's fields}");
        param(name, "name", "path", true,
              "host.stats | host.clients | emulator.stats | debug.breakpoints | media.slots | recording.state | "
              "debug.labels | ttd.status | debug.analyzers | controllers.state | breakpoint_groups");
        param(name, "instance", "query", false, instanceText);

        Json::Value& groups = op("/api/v1/emulator/{id}/breakpoints/groups", "The breakpoint groups",
                                 "The user breakpoints' groups (owned and hidden breakpoints are not listed), by name",
                                 "{ok, instance, groups: [{name, total, active, ids}]}");
        param(groups, "id", "path", true, "Emulator instance id");
    }
