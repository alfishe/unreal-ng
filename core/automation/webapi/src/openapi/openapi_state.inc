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

    // Paging State endpoint (P1-2 design: tagged latches + bank table)
    paths["/api/v1/emulator/{id}/state/paging"]["get"]["summary"] = "Get unified paging state";
    paths["/api/v1/emulator/{id}/state/paging"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/state/paging"]["get"]["description"] =
        "Tagged paging latches + bank table (P1-2 design). Returns model, latches array (port, tags, device, "
        "gate, latch name, value, decoded bits), banks array (bank, address_range, type, page, ROM name/role/signature "
        "when applicable), paging_locked and trdos_active flags. Same report as CLI `paging`, Lua `paging_state()`, "
        "Python `paging_state()` and MCP `inspect_state` aspect `paging`.";
    paths["/api/v1/emulator/{id}/state/paging"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/paging"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/paging"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/paging"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/paging"]["get"]["responses"]["200"]["description"] = "Paging state";
    paths["/api/v1/emulator/{id}/state/paging"]["get"]["responses"]["404"]["description"] = "Emulator not found";

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

    paths["/api/v1/emulator/{id}/state/audio/fm"]["get"]["summary"] = "Get TurboSound FM overview";
    paths["/api/v1/emulator/{id}/state/audio/fm"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/fm"]["get"]["description"] =
        "TurboSound FM (2 x YM2203) board latches (selected chip, status-read mode, FM enable, trim) and a per-chip summary (prescaler, channel-3 mode, keyed/sounding channels, status byte). 404 when the slot device is the plain TurboSound. Same report as the CLI `state audio fm`, Lua `audio_fm_state()`, Python `audio_fm_state()` and MCP `inspect_state` aspect `audio_fm`.";
    paths["/api/v1/emulator/{id}/state/audio/fm"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/fm"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/fm"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/fm"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/fm"]["get"]["responses"]["200"]["description"] = "Get TurboSound FM overview";

    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["summary"] = "Get TurboSound FM chip details";
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["description"] =
        "Full FM half of one YM2203: mode register (channel-3 normal/extended/CSM), timers A/B (values, periods, running, remaining T-states), busy, and every channel with block/F-number/frequency, feedback, algorithm, key-on mask, and each operator's registers (DT, MUL, TL, KS, AR, DR, SR, SL, RR, SSG-EG), pitch, live envelope state and attenuation, plus the last DAC word.";
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["parameters"][1]["name"] = "chip";
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["parameters"][1]["description"] = "FM chip index (0 or 1)";
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["responses"]["200"]["description"] = "Get TurboSound FM chip details";

    paths["/api/v1/emulator/{id}/state/fdc"]["get"]["summary"] = "Get Beta Disk WD1793 state";
    paths["/api/v1/emulator/{id}/state/fdc"]["get"]["tags"].append("Disk State");
    paths["/api/v1/emulator/{id}/state/fdc"]["get"]["description"] =
        "WD1793 registers (command, track, sector, data, status), decoded status bits for the last command type, last command, FSM state, DRQ/INTRQ, Beta128 system register, density, selected drive and side, and all four drives (present, inserted, path, track, side, motor, write protect, image geometry). 404 on machines without a Beta Disk interface.";
    paths["/api/v1/emulator/{id}/state/fdc"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/fdc"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/fdc"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/fdc"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/fdc"]["get"]["responses"]["200"]["description"] = "Get Beta Disk WD1793 state";


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
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["description"] =
        "General Sound card (dedicated 12 MHz Z80 coprocessor + 4 x 8-bit DAC, requires [SOUND] GSType=Z80): "
        "mailbox status with decoded flags, host command/data bytes, MPAG banking latch, per-channel DAC sample "
        "and 6-bit volume, firmware/RAM fitment and the coprocessor PC/SP/AF/halted state. 404 when the machine "
        "has no GS card fitted.";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["responses"]["200"]["description"] = "GS state";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["responses"]["404"]["description"] =
        "Emulator or GS card not found";

    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["summary"] = "General Sound control actions";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["description"] =
        "Drive the GS card the way ZX software does (each action flushes the coprocessor to the current ZX tact "
        "first): reset (full power-on), reset_card (#33 bit7 - CPU/banking/timing, mailbox survives), nmi (#33 "
        "bit6), send_command (OUT #BB), send_data (OUT #B3), read_status (IN #BB, returns the value), read_data "
        "(IN #B3, clears the data-pending flag and returns the value), switch_personality (runtime LLE <-> "
        "lightweight card swap - the host mailbox and activity counters survive the handoff; applied at the "
        "next frame boundary).";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["type"] = "object";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["type"] = "string";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("reset");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("reset_card");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("nmi");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("send_command");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("send_data");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("read_status");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("read_data");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["value"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("switch_personality");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["personality"]["type"] = "string";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["personality"]["enum"].append("z80");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["personality"]["enum"].append("lw");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["responses"]["200"]["description"] =
        "Action result (read_status/read_data carry the byte in 'value'; switch_personality carries the requested "
        "target and the current personality)";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["responses"]["400"]["description"] =
        "Unknown action, invalid value or unknown personality";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["responses"]["404"]["description"] =
        "Emulator or GS card not found";

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
    paths["/api/v1/emulator/state/audio/fm"]["get"]["summary"] = "Get TurboSound FM overview (active emulator)";
    paths["/api/v1/emulator/state/audio/fm"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/fm"]["get"]["responses"]["200"]["description"] = "TurboSound FM overview";

    paths["/api/v1/emulator/state/audio/fm/{chip}"]["get"]["summary"] = "Get TurboSound FM chip details (active emulator)";
    paths["/api/v1/emulator/state/audio/fm/{chip}"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/fm/{chip}"]["get"]["parameters"][0]["name"] = "chip";
    paths["/api/v1/emulator/state/audio/fm/{chip}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/state/audio/fm/{chip}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/state/audio/fm/{chip}"]["get"]["parameters"][0]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/state/audio/fm/{chip}"]["get"]["responses"]["200"]["description"] = "TurboSound FM chip details";

    paths["/api/v1/emulator/state/fdc"]["get"]["summary"] = "Get Beta Disk WD1793 state (active emulator)";
    paths["/api/v1/emulator/state/fdc"]["get"]["tags"].append("Disk State (Active)");
    paths["/api/v1/emulator/state/fdc"]["get"]["responses"]["200"]["description"] = "WD1793 state";

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
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][2]["name"] = "length";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][2]["description"] =
        "Number of bytes to read (default: 128)";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][3]["name"] = "format";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][3]["in"] = "query";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][3]["required"] = false;
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][3]["description"] =
        "'hexdump' (default), 'full' (data array) or 'sparse' (fill-run segments)";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][3]["schema"]["type"] = "string";
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
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][3]["name"] = "filter";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][3]["in"] = "query";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][3]["required"] = false;
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][3]["description"] =
        "Set 'sparse' to compress 0x00/0xFF runs into fill segments (much smaller than the byte array)";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][3]["schema"]["type"] = "string";
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

