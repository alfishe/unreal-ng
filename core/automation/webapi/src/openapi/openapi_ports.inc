    // Port-map introspection (P1-5: static device/port/gate table + live routing flags;
    // P1-2 tagged registry: rows carry semantic tags + live-value latch bindings)
    paths["/api/v1/emulator/{id}/ports"]["get"]["summary"] = "Static port map + live routing flags";
    paths["/api/v1/emulator/{id}/ports"]["get"]["tags"].append("Screen State");
    paths["/api/v1/emulator/{id}/ports"]["get"]["description"] =
        "Which devices respond to which ports on this machine right now, under which gating "
        "conditions. Entries derive from the model port decoder (address qualification "
        "(port & mask) == match); mouse rows appear only while the mouse is fitted, Beta128 "
        "rows only while the TR-DOS interface is configured. Each entry also carries the "
        "tagged-registry fields: tags (semantic categories - keyboard, memory, rom, screen, "
        "storage, mouse, joystick, system, plus sound members sound_ay/sound_covox/"
        "sound_sounddrive/sound_gs/sound_moonsound; a row can carry several, e.g. Pentagon "
        "#FB is covox AND sounddrive) and latch (the live-value binding name such as p7FFD, "
        "p1FFD, pDFFD, pEFF7, pFFF7_w2, and pFE for the ULA port: border, MIC, EAR; null when the row latches "
        "nothing). A latched row also carries latch_value (what the CPU last wrote) and latch_fields (it decoded: "
        "ram_bank / shadow_screen / rom_select / locked for #7FFD, border / mic / ear for #FE, ...). The live block "
        "reports the routing flags that flip rows on/off: trdos_active, mouse_ports_decoded "
        "(design Q4, same source as /mouse/status routing) and shadow_monitor_paged "
        "(Scorpion-only, null elsewhere). Tag and latch names come from the core single "
        "source (PortTagSetToStrings / PagingLatchToString) shared with MCP, CLI, Lua and "
        "Python.";
    paths["/api/v1/emulator/{id}/ports"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ports"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ports"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ports"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ports"]["get"]["responses"]["200"]["description"] =
        "Port map: model, entries[{port,mask,match,device,gate,tags[],latch}], "
        "live{trdos_active,mouse_ports_decoded,mouse_routing_note,shadow_monitor_paged}";

    // A debugger's port write through the decoder (debugger additions tdd §1, PortWrite)
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/ports/out"]["post"];
        op["summary"] = "Write a port through the machine's decoder (debugger)";
        op["tags"].append("Debug Commands");
        op["description"] =
            "The machine's own port decoder handles the write, so it has every side effect a CPU OUT has: paging "
            "(#7FFD, #1FFD, TS-Conf #13AF...), TS-Conf registers (a DMA start included), AY, border, a floppy "
            "controller command, a card's port. Unlike a CPU OUT: no port or memory breakpoint fires, device wait "
            "states are dropped (the CPU's clock stays where it was), and a TTD recording keeps it as a tool edit "
            "(a debugger-edit marker with the device states it changed). It runs where nothing else drives the "
            "machine: paused (moment 'paused'), never started ('stopped') or between two frames of a running "
            "machine ('frame', no pause). The snapshot's seq grows. Example: {\"port\":\"0x13AF\",\"value\":\"0x20\"} "
            "maps RAM page #20 into window 3 on TS-Conf.";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        op["parameters"][0]["schema"]["type"] = "string";
        op["requestBody"]["required"] = true;
        Json::Value& body = op["requestBody"]["content"]["application/json"]["schema"];
        body["type"] = "object";
        body["required"].append("port");
        body["required"].append("value");
        body["properties"]["port"]["description"] = "0..#FFFF: a number, or text 0x13AF, #13AF, 13AFh or decimal";
        body["properties"]["port"]["oneOf"][0]["type"] = "integer";
        body["properties"]["port"]["oneOf"][1]["type"] = "string";
        body["properties"]["value"]["description"] = "0..#FF: a number or text, as port";
        body["properties"]["value"]["oneOf"][0]["type"] = "integer";
        body["properties"]["value"]["oneOf"][1]["type"] = "string";
        op["responses"]["200"]["description"] = "Written: {port: \"0x13AF\", value: \"0x20\", moment: paused | stopped | frame}";
        op["responses"]["400"]["description"] = "Bad body, port or value (the message names it)";
        op["responses"]["404"]["description"] = "No such emulator";
        op["responses"]["503"]["description"] = "No coherent moment within 500 ms (another client is stepping the emulator): try again";
    }
