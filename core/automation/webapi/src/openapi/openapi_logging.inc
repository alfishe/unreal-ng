    // =========================================================================
    // Logging Control
    // =========================================================================

    // GET /api/v1/emulator/{id}/logging — Full module logger state
    paths["/api/v1/emulator/{id}/logging"]["get"]["summary"] = "Get full module logger state";
    paths["/api/v1/emulator/{id}/logging"]["get"]["description"] =
        "Returns global log level, per-module enable state, submodule masks, and per-module level overrides.";
    paths["/api/v1/emulator/{id}/logging"]["get"]["tags"].append("Logging Control");
    paths["/api/v1/emulator/{id}/logging"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/logging"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/logging"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/logging"]["get"]["parameters"][0]["description"] = "Emulator UUID or index";
    paths["/api/v1/emulator/{id}/logging"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/logging"]["get"]["responses"]["200"]["description"] = "Logger state";
    paths["/api/v1/emulator/{id}/logging"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/LoggerStateResponse";
    paths["/api/v1/emulator/{id}/logging"]["get"]["responses"]["404"]["description"] = "Emulator not found";
    paths["/api/v1/emulator/{id}/logging"]["get"]["responses"]["500"]["description"] = "ModuleLogger not available";

    // PUT /api/v1/emulator/{id}/logging/level — Set global log level
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["summary"] = "Set global log level";
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["description"] =
        "Sets the global minimum log level. All modules inheriting the global level will be affected.";
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["tags"].append("Logging Control");
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["parameters"][0]["description"] = "Emulator UUID or index";
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["type"] = "object";
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["required"].append("level");
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["level"]["type"] = "string";
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["level"]["enum"].append("trace");
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["level"]["enum"].append("debug");
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["level"]["enum"].append("info");
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["level"]["enum"].append("warning");
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["level"]["enum"].append("error");
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["level"]["enum"].append("none");
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["level"]["description"] = "Log level: trace, debug, info, warning, error, none";
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["responses"]["200"]["description"] = "Level updated";
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/SuccessResponse";
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["responses"]["400"]["description"] = "Invalid level value";
    paths["/api/v1/emulator/{id}/logging/level"]["put"]["responses"]["404"]["description"] = "Emulator not found";

    // PUT /api/v1/emulator/{id}/logging/module/{name} — Set module state
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["summary"] = "Set module logging state";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["description"] =
        "Enable/disable a module, set its submodule bitmask, and/or override its log level. "
        "All fields are optional — only specified fields are updated.";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["tags"].append("Logging Control");
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][0]["description"] = "Emulator UUID or index";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][1]["description"] =
        "Module name: core, z80, memory, io, disk, video, sound, dma, loader, debug, disassembler, recording";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][1]["schema"]["enum"].append("core");
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][1]["schema"]["enum"].append("z80");
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][1]["schema"]["enum"].append("memory");
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][1]["schema"]["enum"].append("io");
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][1]["schema"]["enum"].append("disk");
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][1]["schema"]["enum"].append("video");
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][1]["schema"]["enum"].append("sound");
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][1]["schema"]["enum"].append("dma");
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][1]["schema"]["enum"].append("loader");
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][1]["schema"]["enum"].append("debug");
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][1]["schema"]["enum"].append("disassembler");
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["parameters"][1]["schema"]["enum"].append("recording");
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["type"] = "object";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["enabled"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["enabled"]["description"] = "Enable or disable the module";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["level"]["type"] = "string";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["level"]["description"] = "Per-module level override: inherit, trace, debug, info, warning, error, none";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["submodule_mask"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["submodule_mask"]["description"] = "Bitmask of enabled submodules (0xFFFF = all)";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["submodule_mask"]["format"] = "uint16";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["responses"]["200"]["description"] = "Module updated";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/SuccessResponse";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["responses"]["400"]["description"] = "Missing JSON body";
    paths["/api/v1/emulator/{id}/logging/module/{name}"]["put"]["responses"]["404"]["description"] = "Emulator or module not found";

