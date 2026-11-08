    // The GUI control plane's methods (gui-control-plane design §4.10; api/gui_api.cpp over GuiPlane): what the
    // program's windows and views do, the same commands as `unreal-gui`, MCP gui_<interface>, the CLI `gui`, Lua /
    // Python gui_call(); every route is also a WebSocket call. Open: no session, no companion scope
    {
        auto op = [&paths](const char* path, const char* method, const char* summary, const char* description,
                           const char* ok) -> Json::Value& {
            Json::Value& operation = paths[path][method];
            operation["summary"] = summary;
            operation["description"] = description;
            operation["tags"].append("GUI");
            operation["responses"]["200"]["description"] = ok;
            operation["responses"]["404"]["description"] = "no_gui: this program has no GUI control plane (headless)";
            return operation;
        };
        const char* result =
            "The plane's result: {ok: true, body} or {ok: false, error: not_found | bad_request | ambiguous | "
            "not_now | failed | timeout, message, body?}; a refusal is 200 too";

        op("/api/v1/gui", "get", "Every GUI program on the machine",
           "The plane's list: each host (program, version, pid), its windows and views with their interfaces, state "
           "and machine, the focus records, the WebAPI endpoints",
           "{ok, body: {hosts: [...]}}");
        op("/api/v1/gui/catalogue", "get", "The GUI interfaces",
           "format unreal-ng-gui-catalogue: interfaces[{name, level, description, state[], commands[{method, label, "
           "description, repeats, params[{name, kind: string | int | bool | address, optional, description}]}]}]",
           "The catalogue");
        Json::Value& state = op("/api/v1/gui/state", "get", "A node's state",
                                "?target= in any form the plane resolves: debugger/memory, memory, memory@<instance>",
                                "{ok, body: {host, target, interfaces, state, focus_at, instance?}}");
        Json::Value target;
        target["name"] = "target";
        target["in"] = "query";
        target["required"] = true;
        target["description"] = "The node";
        target["schema"]["type"] = "string";
        state["parameters"].append(target);
        state["responses"]["400"]["description"] = "bad_request: no target";
        Json::Value& call = op("/api/v1/gui/{method}", "post", "Run a GUI command",
                               "{method}: a command of the catalogue (memory.goto, window.move, screen.zoom). Body: "
                               "its parameters and target? (absent: the plane chooses: the only node with the "
                               "interface, else the one focused last)",
                               result);
        Json::Value method;
        method["name"] = "method";
        method["in"] = "path";
        method["required"] = true;
        method["description"] = "The command";
        method["schema"]["type"] = "string";
        call["parameters"].append(method);
        call["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
        call["responses"]["400"]["description"] = "bad_request: the body is not a JSON object";
    }
