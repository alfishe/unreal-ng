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
        "p1FFD, pDFFD, pFFF7_w2; null when the row is not a paging latch). The live block "
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
