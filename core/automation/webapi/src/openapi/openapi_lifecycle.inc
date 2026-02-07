    // ==========================================
    // Emulator Management Endpoints
    // ==========================================

    // GET /api/v1/emulator
    paths["/api/v1/emulator"]["get"]["summary"] = "List all emulators";
    paths["/api/v1/emulator"]["get"]["tags"].append("Emulator Management");
    paths["/api/v1/emulator"]["get"]["responses"]["200"]["description"] = "Successful response";
    paths["/api/v1/emulator"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/EmulatorList";

    // POST /api/v1/emulator/create - Create new emulator (without starting)
    paths["/api/v1/emulator/create"]["post"]["summary"] = "Create new emulator";
    paths["/api/v1/emulator/create"]["post"]["tags"].append("Emulator Control");
    paths["/api/v1/emulator/create"]["post"]["requestBody"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/CreateEmulatorRequest";
    paths["/api/v1/emulator/create"]["post"]["responses"]["201"]["description"] = "Emulator created";

    // GET /api/v1/emulator/status
    paths["/api/v1/emulator/status"]["get"]["summary"] = "Get overall emulator status";
    paths["/api/v1/emulator/status"]["get"]["tags"].append("Emulator Management");
    paths["/api/v1/emulator/status"]["get"]["responses"]["200"]["description"] = "Successful response";

    // GET /api/v1/emulator/models
    paths["/api/v1/emulator/models"]["get"]["summary"] = "Get available emulator models";
    paths["/api/v1/emulator/models"]["get"]["tags"].append("Emulator Management");
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["description"] =
        "List of available ZX Spectrum models with RAM configurations";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["type"] =
        "object";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["count"]["type"] = "integer";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["count"]["description"] = "Total number of available models";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["type"] = "array";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["type"] = "object";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["properties"]["id"]["type"] = "integer";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["properties"]["name"]["type"] = "string";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["properties"]["full_name"]["type"] = "string";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["properties"]["default_ram_kb"]["type"] = "integer";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["properties"]["available_ram_sizes_kb"]["type"] = "array";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["properties"]["available_ram_sizes_kb"]["items"]["type"] = "integer";

    // GET /api/v1/emulator/{id}
    paths["/api/v1/emulator/{id}"]["get"]["summary"] = "Get emulator details";
    paths["/api/v1/emulator/{id}"]["get"]["tags"].append("Emulator Management");
    paths["/api/v1/emulator/{id}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}"]["get"]["parameters"][0]["description"] = "Emulator UUID or index (0-based)";
    paths["/api/v1/emulator/{id}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}"]["get"]["responses"]["200"]["description"] = "Successful response";

    // DELETE /api/v1/emulator/{id}
    paths["/api/v1/emulator/{id}"]["delete"]["summary"] = "Remove emulator";
    paths["/api/v1/emulator/{id}"]["delete"]["tags"].append("Emulator Management");
    paths["/api/v1/emulator/{id}"]["delete"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}"]["delete"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}"]["delete"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}"]["delete"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}"]["delete"]["responses"]["204"]["description"] = "Emulator removed";

    // POST /api/v1/emulator/start - Create and start a new emulator
    paths["/api/v1/emulator/start"]["post"]["tags"].append("Emulator Control");
    paths["/api/v1/emulator/start"]["post"]["summary"] = "Create and start a new emulator";
    paths["/api/v1/emulator/start"]["post"]["description"] =
        "Creates a new emulator instance and immediately starts it";
    paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["symbolic_id"]["type"] = "string";
    paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["model"]["type"] = "string";
    paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["ram_size"]["type"] = "integer";
    paths["/api/v1/emulator/start"]["post"]["responses"]["201"]["description"] = "Emulator created and started";
    paths["/api/v1/emulator/start"]["post"]["responses"]["201"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/EmulatorInfo";

    // Control endpoints
    // POST /api/v1/emulator/{id}/start - Start existing emulator
    paths["/api/v1/emulator/{id}/start"]["post"]["tags"].append("Emulator Control");
    paths["/api/v1/emulator/{id}/start"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/start"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/start"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/start"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/start"]["post"]["responses"]["200"]["description"] = "Emulator started";

    paths["/api/v1/emulator/{id}/stop"]["post"]["summary"] = "Stop emulator";
    paths["/api/v1/emulator/{id}/stop"]["post"]["tags"].append("Emulator Control");
    paths["/api/v1/emulator/{id}/stop"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/stop"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/stop"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/stop"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/stop"]["post"]["responses"]["200"]["description"] = "Emulator stopped";

    paths["/api/v1/emulator/{id}/pause"]["post"]["summary"] = "Pause emulator";
    paths["/api/v1/emulator/{id}/pause"]["post"]["tags"].append("Emulator Control");
    paths["/api/v1/emulator/{id}/pause"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/pause"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/pause"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/pause"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/pause"]["post"]["responses"]["200"]["description"] = "Emulator paused";

    paths["/api/v1/emulator/{id}/resume"]["post"]["summary"] = "Resume emulator";
    paths["/api/v1/emulator/{id}/resume"]["post"]["tags"].append("Emulator Control");
    paths["/api/v1/emulator/{id}/resume"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/resume"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/resume"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/resume"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/resume"]["post"]["responses"]["200"]["description"] = "Emulator resumed";

    paths["/api/v1/emulator/{id}/reset"]["post"]["summary"] = "Reset emulator";
    paths["/api/v1/emulator/{id}/reset"]["post"]["tags"].append("Emulator Control");
    paths["/api/v1/emulator/{id}/reset"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/reset"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/reset"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/reset"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/reset"]["post"]["responses"]["200"]["description"] = "Emulator reset";

