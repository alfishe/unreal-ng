    // Port-map introspection (P1-5: static device/port/gate table + live routing flags)
    paths["/api/v1/emulator/{id}/ports"]["get"]["summary"] = "Static port map + live routing flags";
    paths["/api/v1/emulator/{id}/ports"]["get"]["tags"].append("Screen State");
    paths["/api/v1/emulator/{id}/ports"]["get"]["description"] =
        "Which devices respond to which ports on this machine right now, under which gating "
        "conditions. Entries derive from the model port decoder (address qualification "
        "(port & mask) == match); mouse rows appear only while the mouse is fitted, Beta128 "
        "rows only while the TR-DOS interface is configured. The live block reports the "
        "routing flags that flip rows on/off: trdos_active, mouse_ports_decoded (design Q4, "
        "same source as /mouse/status routing) and shadow_monitor_paged (Scorpion-only, "
        "null elsewhere).";
    paths["/api/v1/emulator/{id}/ports"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ports"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ports"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ports"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ports"]["get"]["responses"]["200"]["description"] =
        "Port map: model, entries[{port,mask,match,device,gate}], live{trdos_active,"
        "mouse_ports_decoded,mouse_routing_note,shadow_monitor_paged}";
