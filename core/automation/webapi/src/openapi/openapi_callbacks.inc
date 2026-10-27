    // The scripts' callbacks (docs/features/script-callbacks.md, callbacks_api.cpp): list and manage, never register
    {
        const std::string list = "/api/v1/emulator/{id}/callbacks";
        const std::string resume = "/api/v1/emulator/{id}/callbacks/{cb_id}/resume";
        const std::string remove = "/api/v1/emulator/{id}/callbacks/{cb_id}";
        const std::string policy = "/api/v1/emulator/{id}/callbacks/{cb_id}/policy";
        auto params = [&paths](const std::string& path, const char* method, bool withCallback) {
            Json::Value& op = paths[path][method];
            op["tags"].append("Debug Commands");
            op["parameters"][0]["name"] = "id";
            op["parameters"][0]["in"] = "path";
            op["parameters"][0]["required"] = true;
            op["parameters"][0]["schema"]["type"] = "string";
            if (withCallback)
            {
                op["parameters"][1]["name"] = "cb_id";
                op["parameters"][1]["in"] = "path";
                op["parameters"][1]["required"] = true;
                op["parameters"][1]["schema"]["type"] = "integer";
                op["parameters"][1]["description"] = "The callback id (callbacks[].id; never reused in a session)";
                op["responses"]["400"]["description"] = "The callback id is not a number";
                op["responses"]["404"]["description"] = "No such emulator or callback";
            }
            return &op;
        };

        Json::Value* op = params(list, "get", false);
        (*op)["summary"] = "List the script callbacks";
        (*op)["description"] =
            "Every Lua / Python callback of the emulator (on_tap, on_breakpoint, on_game_event) with its statistics, "
            "the fields of Lua's callbacks(): id, on (tap, breakpoint, game_event), owner (lua, python), label, target, "
            "breakpoint, event, policy, state (active, suspended, disabled), reason, budget_us, ring, pending, hits, "
            "dropped, calls, errors, overruns, total_us, max_us, p99_us, last_overrun_frame / last_overrun_us (after "
            "an overrun), last_error (after an error). A remote client cannot register a callback: a function lives "
            "in a script. A change is pushed as callbacks_changed on debug.events.";
        (*op)["responses"]["200"]["description"] = "{count, callbacks}";
        (*op)["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] = "#/components/schemas/ScriptCallbackList";
        (*op)["responses"]["404"]["description"] = "No such emulator";

        op = params(resume, "post", true);
        (*op)["summary"] = "Resume a script callback";
        (*op)["description"] =
            "A suspended callback (over its budget, policy suspend) active again, with fresh statistics; an active one "
            "restarts its statistics. A disabled one cannot be resumed (409): the script registers it again.";
        (*op)["responses"]["200"]["description"] = "{status, message, callback}";
        (*op)["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] = "#/components/schemas/ScriptCallbackResult";
        (*op)["responses"]["409"]["description"] = "The callback is disabled";

        op = params(remove, "delete", true);
        (*op)["summary"] = "Remove a script callback";
        (*op)["description"] =
            "Removes the callback and its hidden tap. The script is not told: its function is never called again.";
        (*op)["responses"]["200"]["description"] = "{status, message, id, removed: true}";

        op = params(policy, "put", true);
        (*op)["summary"] = "Set a script callback's over-budget policy";
        (*op)["description"] =
            "Body {policy}: suspend (the default: suspended after 3 calls over the budget within 50 frames or one over "
            "10 times it), disable (the same rule removes its tap) or ignore (counted in overruns only).";
        (*op)["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
        (*op)["requestBody"]["content"]["application/json"]["schema"]["required"].append("policy");
        (*op)["requestBody"]["content"]["application/json"]["schema"]["properties"]["policy"]["type"] = "string";
        for (const char* name : {"suspend", "disable", "ignore"})
            (*op)["requestBody"]["content"]["application/json"]["schema"]["properties"]["policy"]["enum"].append(name);
        (*op)["responses"]["200"]["description"] = "{status, message, callback}";
        (*op)["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] = "#/components/schemas/ScriptCallbackResult";
        (*op)["responses"]["400"]["description"] = "No policy in the body, an unknown policy or a bad callback id";
    }
