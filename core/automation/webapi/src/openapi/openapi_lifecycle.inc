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
    paths["/api/v1/emulator/create"]["post"]["description"] =
        "Creates a new emulator instance without starting it. A requested model that cannot "
        "be created on this build (missing port decoder / config folder) fails with 400 + "
        "reason - there is no silent fallback to a default machine.";
    paths["/api/v1/emulator/create"]["post"]["tags"].append("Emulator Control");
    paths["/api/v1/emulator/create"]["post"]["requestBody"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/CreateEmulatorRequest";
    paths["/api/v1/emulator/create"]["post"]["responses"]["201"]["description"] = "Emulator created (with identity fields)";
    paths["/api/v1/emulator/create"]["post"]["responses"]["400"]["description"] =
        "Unknown model / unsupported RAM / model not creatable on this build / ram_power_on not random or zero; "
        "body carries message, requested_model and available_models_endpoint";

    // GET /api/v1/emulator/status
    paths["/api/v1/emulator/status"]["get"]["summary"] = "Get overall emulator status";
    paths["/api/v1/emulator/status"]["get"]["description"] =
        "Instance counts by state, the build fingerprint (server block) and the list of "
        "models this build can actually create (models_creatable)";
    paths["/api/v1/emulator/status"]["get"]["tags"].append("Emulator Management");
    paths["/api/v1/emulator/status"]["get"]["responses"]["200"]["description"] = "Successful response";
    paths["/api/v1/emulator/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["type"] =
        "object";
    paths["/api/v1/emulator/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["server"]["$ref"] = "#/components/schemas/BuildInfo";
    paths["/api/v1/emulator/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models_creatable"]["type"] = "array";
    paths["/api/v1/emulator/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models_creatable"]["items"]["type"] = "string";
    paths["/api/v1/emulator/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models_creatable"]["description"] =
        "Model short names a create request can succeed with, ZX-Poly configuration names included";

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
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["properties"]["creatable"]["type"] = "boolean";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["properties"]["creatable"]["description"] =
        "Whether a create request for this model is expected to succeed on this build";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["properties"]["zxpoly"]["type"] = "boolean";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["properties"]["zxpoly"]["description"] =
        "Present on ZX-Poly configurations (ZXPOLY-48K, ZXPOLY-128K, ZXPOLY-PENTAGON): creating one by name "
        "starts four synchronized instances of base_model and returns the master. They carry no id and no "
        "available_ram_sizes_kb (the RAM size is fixed)";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["properties"]["base_model"]["type"] = "string";

    // GET /api/v1/emulator/{id}
    paths["/api/v1/emulator/{id}"]["get"]["summary"] = "Get emulator details";
    paths["/api/v1/emulator/{id}"]["get"]["tags"].append("Emulator Management");
    paths["/api/v1/emulator/{id}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}"]["get"]["parameters"][0]["description"] = "Emulator UUID or index (0-based)";
    paths["/api/v1/emulator/{id}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}"]["get"]["responses"]["200"]["description"] = "Successful response";
    paths["/api/v1/emulator/{id}"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/EmulatorInfo";

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
         ["model"]["description"] =
        "Model short name (see /api/v1/emulator/models). A ZX-Poly configuration name (ZXPOLY-48K, ZXPOLY-128K, "
        "ZXPOLY-PENTAGON) starts the bare ZX-Poly machine; combine it with `zxpoly` to load a file";
    paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["ram_size"]["type"] = "integer";
    {
        Json::Value& ramPowerOn = paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]
                                       ["schema"]["properties"]["ram_power_on"];
        ramPowerOn["type"] = "string";
        ramPowerOn["enum"].append("random");
        ramPowerOn["enum"].append("zero");
        ramPowerOn["description"] =
            "RAM contents at creation: random (noise in RAM pages 5 and 7, the screen and the shadow screen, the way real DRAM powers up; every other page zero) or zero (every RAM page of the configuration reads 0 - reproducible runs for tests, benchmarks and scripted analysis). Default: [MISC] RAMPowerOn of the model's unreal.ini (RANDOM when unset)"
            ". A ZX-Poly machine applies it to all four modules";
    }
    {
        Json::Value& sprinter = paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]
                                     ["schema"]["properties"]["sprinter"];
        sprinter["type"] = "object";
        sprinter["properties"]["bios"]["type"] = "string";
        sprinter["properties"]["fast_start"]["type"] = "boolean";
        sprinter["properties"]["accel_int_suspend"]["type"] = "boolean";
        sprinter["description"] =
            "A SPRINTER's firmware and start options (core SprinterBios): bios = 3.04 | 3.06 | 3.07 or a file in rom/sprinter (or a path), fast_start (skip the PLD loader), accel_int_suspend (an INT acknowledge blocks accelerator operations); default: [ROM] SPRINTER and [SPRINTER] of the model's unreal.ini (shipped: 3.07). 400 on an unknown image";
    }
    {
        Json::Value& profi = paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]
                                  ["schema"]["properties"]["profi"];
        profi["type"] = "object";
        profi["properties"]["keyboard"]["type"] = "string";
        for (const char* value : {"default", "matrix", "xt", "xttable"})
            profi["properties"]["keyboard"]["enum"].append(value);
        profi["description"] =
            "A PROFI / PROFI3's keyboard ([PROFI] Keyboard=): matrix, xt (the PROFI-XT controller on its reconstructed "
            "firmware), xttable (the controller from its key table) or default (v5 xt, v3 matrix). 400 on an unknown name";
    }
    paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["select"]["type"] = "boolean";
    paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["select"]["description"] =
        "Make the new machine the selected one (default true; the desktop front end then shows it). false keeps "
        "the current selection - for scripted runs that create and remove machines next to the one on screen";
    {
        Json::Value& zxpoly = paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]
                                   ["schema"]["properties"]["zxpoly"];
        zxpoly["description"] =
            "Start a ZX-Poly machine: four synchronized instances of `model` (a base model or a ZX-Poly "
            "configuration name; default PENTAGON). true for the bare "
            "machine, or {\"file\": path} with a .zxp snapshot, a .prom ZX-Poly ROM image (the Test ROM) or a "
            "multiloader disk (.trd/.scl, needs a model with TR-DOS). The returned id is the master - the machine "
            "every endpoint addresses; the three slaves are hidden members. The response carries the group status "
            "under `zxpoly`. Deleting the master removes the group";
        zxpoly["oneOf"][0]["type"] = "boolean";
        zxpoly["oneOf"][1]["type"] = "object";
        zxpoly["oneOf"][1]["properties"]["file"]["type"] = "string";
        zxpoly["oneOf"][1]["properties"]["file"]["description"] = "Host path of a .zxp, .prom or disk image";
    }
    paths["/api/v1/emulator/start"]["post"]["responses"]["201"]["description"] = "Emulator created and started";
    paths["/api/v1/emulator/start"]["post"]["responses"]["201"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/EmulatorInfo";
    paths["/api/v1/emulator/start"]["post"]["responses"]["400"]["description"] =
        "Unknown model / unsupported RAM / model not creatable on this build (no fallback)";

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

    // GET / POST /api/v1/emulator/{id}/switches - front-panel switches (Profi TURBO)
    {
        Json::Value& get = paths["/api/v1/emulator/{id}/switches"]["get"];
        get["tags"].append("Emulator Control");
        get["summary"] = "Front-panel switches";
        get["description"] =
            "The machine's front-panel switches and their positions: the Profi boards' TURBO switch (7 MHz; on the "
            "v3 a loaded floppy head holds 3.5 MHz) and the v5 CP/M switch (holds #DFFD at #00 while on). Empty for "
            "a machine without switches";
        get["parameters"][0]["name"] = "id";
        get["parameters"][0]["in"] = "path";
        get["parameters"][0]["required"] = true;
        get["parameters"][0]["schema"]["type"] = "string";
        get["responses"]["200"]["description"] = "{\"emulator_id\", \"switches\": [{\"name\", \"on\"}]}";
        get["responses"]["404"]["description"] = "Emulator not found";

        Json::Value& post = paths["/api/v1/emulator/{id}/switches"]["post"];
        post["tags"].append("Emulator Control");
        post["summary"] = "Flip a front-panel switch";
        post["description"] =
            "Set a switch (recorded by TTD like a key press). Answers the switches as GET does; 400 for an unknown "
            "switch or one the machine does not have";
        post["parameters"] = get["parameters"];
        Json::Value& schema = post["requestBody"]["content"]["application/json"]["schema"];
        post["requestBody"]["required"] = true;
        schema["type"] = "object";
        schema["required"].append("name");
        schema["required"].append("on");
        schema["properties"]["name"]["type"] = "string";
        schema["properties"]["name"]["enum"].append("turbo");
        schema["properties"]["name"]["enum"].append("cpm");
        schema["properties"]["on"]["type"] = "boolean";
        post["responses"]["200"]["description"] = "The switches after the flip";
        post["responses"]["400"]["description"] = "Bad body, unknown switch or none on this machine";
        post["responses"]["404"]["description"] = "Emulator not found";
    }

    // POST /api/v1/emulator/{id}/model - Switch machine model
    // GET /api/v1/emulator/{id}/zxpoly - ZX-Poly group status
    {
        Json::Value& get = paths["/api/v1/emulator/{id}/zxpoly"]["get"];
        get["tags"].append("Emulator Control");
        get["summary"] = "ZX-Poly group status";
        get["description"] =
            "The ZX-Poly machine the instance belongs to (any member id): the four modules with their platform "
            "registers R0-R3, #3D00, lock, video mode (0-3 one module, 4 16 colours, 5 512x384, 6/7 masked "
            "modes) and the lockstep check (control state of every slave against the master)";
        get["parameters"][0]["name"] = "id";
        get["parameters"][0]["in"] = "path";
        get["parameters"][0]["required"] = true;
        get["parameters"][0]["schema"]["type"] = "string";
        Json::Value& schema = get["responses"]["200"]["content"]["application/json"]["schema"];
        get["responses"]["200"]["description"] = "Group status";
        schema["type"] = "object";
        schema["properties"]["master_id"]["type"] = "string";
        schema["properties"]["locked"]["type"] = "boolean";
        schema["properties"]["slaves_running"]["type"] = "boolean";
        schema["properties"]["parallel_slaves"]["type"] = "boolean";
        schema["properties"]["parallel_slaves"]["description"] =
            "Locked slaves run their frame on worker threads, at the same time";
        schema["properties"]["pipelined_slaves"]["type"] = "boolean";
        schema["properties"]["pipelined_slaves"]["description"] =
            "The slaves' frame overlaps the master's next one (unlimited speed only; the state at every frame "
            "boundary is the same)";
        schema["properties"]["port_3d00"]["type"] = "integer";
        schema["properties"]["video_mode"]["type"] = "integer";
        schema["properties"]["modules"]["type"] = "array";
        schema["properties"]["modules"]["items"]["type"] = "object";
        schema["properties"]["modules"]["items"]["properties"]["module"]["type"] = "integer";
        schema["properties"]["modules"]["items"]["properties"]["id"]["type"] = "string";
        schema["properties"]["modules"]["items"]["properties"]["registers"]["type"] = "array";
        schema["properties"]["modules"]["items"]["properties"]["registers"]["items"]["type"] = "integer";
        schema["properties"]["divergence"]["type"] = "object";
        schema["properties"]["divergence"]["properties"]["diverged"]["type"] = "boolean";
        schema["properties"]["divergence"]["properties"]["module"]["type"] = "integer";
        schema["properties"]["divergence"]["properties"]["what"]["type"] = "string";
        get["responses"]["404"]["description"] = "No such emulator, or it is not a ZX-Poly machine";
    }

    paths["/api/v1/emulator/{id}/model"]["post"]["summary"] = "Switch machine model";
    paths["/api/v1/emulator/{id}/model"]["post"]["description"] =
        "Switch the emulator to a different ZX Spectrum machine model. "
        "The request is validated BEFORE the current instance is touched: a failed switch "
        "(unknown model / unsupported RAM / not creatable on this build) returns 400 and "
        "leaves the current emulator running untouched. A successful switch stops and "
        "destroys the current emulator, creates a new one with the requested model, and "
        "starts it. The new emulator will have a different ID. The media follow: each medium goes into the "
        "slot with the same id on the new machine, unsaved writes included (media.attached). A medium the new "
        "model has no slot for is closed when it has nothing unsaved (media.closed); with unsaved writes the "
        "switch answers 409 and lists them in 'stranded', unless 'stranded' says save, discard or keep "
        "(keep: detached media on the new machine, media.detached).";
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
         ["model"]["description"] = "Model short name (e.g., 'PENTAGON', '48K', '128K', 'SCORPION', 'PROFI')";
    paths["/api/v1/emulator/{id}/model"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["ram_size"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/model"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["ram_size"]["description"] = "Optional RAM size in KB";
    {
        Json::Value& ramPowerOn = paths["/api/v1/emulator/{id}/model"]["post"]["requestBody"]["content"]
                                       ["application/json"]["schema"]["properties"]["ram_power_on"];
        ramPowerOn["type"] = "string";
        ramPowerOn["enum"].append("random");
        ramPowerOn["enum"].append("zero");
        ramPowerOn["description"] =
            "RAM contents of the new machine: random or zero (see POST /api/v1/emulator/create). Default: the "
            "mode the current machine was created with";
    }
    paths["/api/v1/emulator/{id}/model"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["stranded"]["type"] = "string";
    for (const char* value : {"refuse", "save", "discard", "keep"})
        paths["/api/v1/emulator/{id}/model"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
             ["stranded"]["enum"].append(value);
    paths["/api/v1/emulator/{id}/model"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["stranded"]["description"] =
        "Unsaved writes on media the new model has no slot for: refuse (default, 409), save (into their own "
        "files), discard, keep (detached media on the new machine)";
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
    paths["/api/v1/emulator/{id}/model"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["media"]["type"] = "object";
    paths["/api/v1/emulator/{id}/model"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["media"]["description"] = "Slot ids: attached (followed), detached (kept with unsaved writes), closed";
    paths["/api/v1/emulator/{id}/model"]["post"]["responses"]["409"]["description"] =
        "Unsaved writes on media the new model has no slot for (code 'dirty', list in 'stranded'); nothing changed";
    paths["/api/v1/emulator/{id}/model"]["post"]["responses"]["400"]["description"] =
        "Unknown model / unsupported RAM / not creatable on this build; the current instance is left untouched. "
        "Body carries message, requested_model and available_models_endpoint";
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
