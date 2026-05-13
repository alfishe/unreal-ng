    // Memory State endpoints
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["summary"] = "Get memory overview";
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["responses"]["200"]["description"] = "Memory state overview";

    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["summary"] = "Get RAM state";
    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["responses"]["200"]["description"] = "RAM state details";

    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["summary"] = "Get ROM state";
    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["responses"]["200"]["description"] = "ROM state details";

    // Screen State endpoints
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["summary"] = "Get screen state overview";
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["tags"].append("Screen State");
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["responses"]["200"]["description"] = "Screen state overview";

    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["summary"] = "Get screen mode details";
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["tags"].append("Screen State");
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["responses"]["200"]["description"] =
        "Screen mode information";

    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["summary"] = "Get flash state";
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["tags"].append("Screen State");
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["responses"]["200"]["description"] =
        "Flash state information";

    // Audio state endpoints
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["summary"] = "Get AY chips overview";
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["responses"]["200"]["description"] = "AY chips information";

    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["summary"] = "Get specific AY chip details";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][1]["name"] = "chip";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][1]["description"] =
        "AY chip index (0-based)";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["responses"]["200"]["description"] = "AY chip details";

    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["summary"] =
        "Get AY chip register details";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["schema"]["type"] =
        "string";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["name"] = "chip";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["schema"]["type"] =
        "integer";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][2]["name"] = "reg";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][2]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][2]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][2]["description"] =
        "Register number (0-15)";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][2]["schema"]["type"] =
        "integer";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["responses"]["200"]["description"] =
        "Register details";

    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["summary"] = "Get beeper state";
    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["responses"]["200"]["description"] = "Beeper state";

    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["summary"] = "Get General Sound state";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["responses"]["200"]["description"] = "GS state";

    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["summary"] = "Get Covox state";
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["responses"]["200"]["description"] = "Covox state";

    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["summary"] = "Get audio channels overview";
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["responses"]["200"]["description"] =
        "Audio channels information";

    // Active emulator endpoints (no ID required)
    paths["/api/v1/emulator/state/audio/ay"]["get"]["summary"] = "Get AY chips overview (active emulator)";
    paths["/api/v1/emulator/state/audio/ay"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/ay"]["get"]["responses"]["200"]["description"] = "AY chips information";

    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["summary"] =
        "Get specific AY chip details (active emulator)";
    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["parameters"][0]["name"] = "chip";
    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["parameters"][0]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["responses"]["200"]["description"] = "AY chip details";

    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["summary"] =
        "Get AY chip register details (active emulator)";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["name"] = "chip";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["schema"]["type"] =
        "integer";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["name"] = "reg";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["description"] =
        "Register number (0-15)";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["schema"]["type"] =
        "integer";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["responses"]["200"]["description"] =
        "Register details";

    paths["/api/v1/emulator/state/audio/beeper"]["get"]["summary"] = "Get beeper state (active emulator)";
    paths["/api/v1/emulator/state/audio/beeper"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/beeper"]["get"]["responses"]["200"]["description"] = "Beeper state";

    paths["/api/v1/emulator/state/audio/gs"]["get"]["summary"] = "Get GS state (active emulator)";
    paths["/api/v1/emulator/state/audio/gs"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/gs"]["get"]["responses"]["200"]["description"] = "GS state";

    paths["/api/v1/emulator/state/audio/covox"]["get"]["summary"] = "Get Covox state (active emulator)";
    paths["/api/v1/emulator/state/audio/covox"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/covox"]["get"]["responses"]["200"]["description"] = "Covox state";

    paths["/api/v1/emulator/state/audio/channels"]["get"]["summary"] = "Get audio channels (active emulator)";
    paths["/api/v1/emulator/state/audio/channels"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/channels"]["get"]["responses"]["200"]["description"] =
        "Audio channels information";


    // ==========================================
    // Memory Read/Write Operations
    // ==========================================

    // GET /api/v1/emulator/{id}/memory/read/{address} - Read memory at address
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["summary"] = "Read memory at address";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["description"] =
        "Read one or more bytes from the Z80 address space.";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][1]["name"] = "address";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][1]["description"] = "Z80 address (0-65535)";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["responses"]["200"]["description"] = "Memory contents";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["responses"]["400"]["description"] = "Invalid address";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["responses"]["404"]["description"] = "Emulator not found";

    // POST /api/v1/emulator/{id}/memory/write - Write memory
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["summary"] = "Write memory";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["description"] =
        "Write one or more bytes to the Z80 address space.";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["address"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["address"]["description"] = "Z80 start address (0-65535)";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["data"]["type"] = "array";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["data"]["items"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["responses"]["200"]["description"] = "Memory written";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["responses"]["400"]["description"] = "Invalid address or data";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["responses"]["404"]["description"] = "Emulator not found";

    // GET/POST /api/v1/emulator/{id}/memory/page/{type}/{page} - Read/Write physical memory page
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["summary"] = "Read physical memory page";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["description"] =
        "Read an entire 16KB physical memory page by type (ram/rom) and page number.";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][1]["name"] = "type";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][1]["description"] = "Memory type: 'ram' or 'rom'";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][2]["name"] = "page";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][2]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][2]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["responses"]["200"]["description"] = "Page data (base64 encoded)";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["responses"]["400"]["description"] = "Invalid type or page number";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["responses"]["404"]["description"] = "Emulator not found";

    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["summary"] = "Write physical memory page";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["description"] =
        "Write an entire 16KB physical memory page by type and page number.";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][1]["name"] = "type";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][2]["name"] = "page";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][2]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][2]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["responses"]["200"]["description"] = "Page written";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["responses"]["400"]["description"] = "Invalid type, page, or data";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["responses"]["404"]["description"] = "Emulator not found";

    // GET/PUT /api/v1/emulator/{id}/memory/rom/protect - ROM protection control
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["get"]["summary"] = "Get ROM write protection status";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["get"]["description"] =
        "Get whether ROM write protection is enabled.";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["get"]["responses"]["200"]["description"] = "ROM protection status";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["get"]["responses"]["404"]["description"] = "Emulator not found";

    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["summary"] = "Set ROM write protection";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["description"] =
        "Enable or disable ROM write protection.";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["protected"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["responses"]["200"]["description"] = "Protection state updated";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["responses"]["404"]["description"] = "Emulator not found";

