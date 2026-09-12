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

    // POST /api/v1/emulator/{id}/nmi - NMI pulse / Scorpion MNI magic button
    paths["/api/v1/emulator/{id}/nmi"]["post"]["summary"] = "Pulse the NMI line";
    paths["/api/v1/emulator/{id}/nmi"]["post"]["description"] =
        "Pulse the Z80 NMI line (accepted at the next instruction boundary: 11T, PC pushed, "
        "vector #0066, IFF2<-IFF1). With {\"magic\": true} on Scorpion models the Shadow Monitor "
        "is paged into #0000 first, so the handler at #0066 executes monitor code (the MNI "
        "magic button). Non-Scorpion models always take the plain NMI.";
    paths["/api/v1/emulator/{id}/nmi"]["post"]["tags"].append("Emulator Control");
    paths["/api/v1/emulator/{id}/nmi"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/nmi"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/nmi"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/nmi"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/nmi"]["post"]["requestBody"]["required"] = false;
    paths["/api/v1/emulator/{id}/nmi"]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/emulator/{id}/nmi"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["magic"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/nmi"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["magic"]["description"] = "true = Scorpion MNI (NMI + Shadow Monitor); default false";
    paths["/api/v1/emulator/{id}/nmi"]["post"]["responses"]["200"]["description"] = "NMI/MNI requested";

    // POST /api/v1/emulator/{id}/model - Switch machine model
    paths["/api/v1/emulator/{id}/model"]["post"]["summary"] = "Switch machine model";
    paths["/api/v1/emulator/{id}/model"]["post"]["description"] =
        "Switch the emulator to a different ZX Spectrum machine model. "
        "This stops and destroys the current emulator, creates a new one with the requested model, "
        "and starts it. The new emulator will have a different ID.";
    paths["/api/v1/emulator/{id}/model"]["post"]["tags"].append("Emulator Control");
    paths["/api/v1/emulator/{id}/model"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/model"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/model"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/model"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/model"]["post"]["parameters"][0]["description"] = "Current emulator ID";
    paths["/api/v1/emulator/{id}/model"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/model"]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/emulator/{id}/model"]["post"]["requestBody"]["content"]["application/json"]["schema"]["required"][0] = "model";
    paths["/api/v1/emulator/{id}/model"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["model"]["type"] = "string";
    paths["/api/v1/emulator/{id}/model"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["model"]["description"] = "Model short name (e.g., 'PENTAGON', '48K', '128K', 'SCORPION')";
    paths["/api/v1/emulator/{id}/model"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["ram_size"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/model"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["ram_size"]["description"] = "Optional RAM size in KB";
    paths["/api/v1/emulator/{id}/model"]["post"]["responses"]["200"]["description"] = "Model switched successfully";
    paths["/api/v1/emulator/{id}/model"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/emulator/{id}/model"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["status"]["type"] = "string";
    paths["/api/v1/emulator/{id}/model"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["old_emulator_id"]["type"] = "string";
    paths["/api/v1/emulator/{id}/model"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["new_emulator_id"]["type"] = "string";
    paths["/api/v1/emulator/{id}/model"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["model"]["type"] = "string";
    paths["/api/v1/emulator/{id}/model"]["post"]["responses"]["400"]["description"] = "Invalid model name or missing 'model' field";
    paths["/api/v1/emulator/{id}/model"]["post"]["responses"]["404"]["description"] = "Emulator not found";

    // POST /api/v1/videowall/singlesync - Videowall single-sync mode
    paths["/api/v1/videowall/singlesync"]["post"]["summary"] = "Set videowall single-sync mode";
    paths["/api/v1/videowall/singlesync"]["post"]["description"] =
        "Enable or disable single-sync mode for the standalone videowall. "
        "When enabled with an emulator_id, the videowall locks onto that "
        "instance as its sole sync source; when disabled, the videowall "
        "returns to cycling through all running instances.";
    paths["/api/v1/videowall/singlesync"]["post"]["tags"].append("Emulator Control");
    paths["/api/v1/videowall/singlesync"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/videowall/singlesync"]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/videowall/singlesync"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["enable"]["type"] = "boolean";
    paths["/api/v1/videowall/singlesync"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["enable"]["description"] = "true = lock videowall to a single emulator";
    paths["/api/v1/videowall/singlesync"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["emulator_id"]["type"] = "string";
    paths["/api/v1/videowall/singlesync"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["emulator_id"]["description"] = "Emulator instance to sync to (empty when disabling)";
    paths["/api/v1/videowall/singlesync"]["post"]["responses"]["200"]["description"] = "Mode applied (success=true)";
    paths["/api/v1/videowall/singlesync"]["post"]["responses"]["400"]["description"] = "Invalid JSON body";
    paths["/api/v1/videowall/singlesync"]["post"]["responses"]["500"]["description"] =
        "Could not apply the requested mode (success=false)";
