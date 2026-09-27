    // Settings Management endpoints
    paths["/api/v1/emulator/{id}/settings"]["get"]["summary"] = "Get all emulator settings";
    paths["/api/v1/emulator/{id}/settings"]["get"]["tags"].append("Settings Management");
    paths["/api/v1/emulator/{id}/settings"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/settings"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/settings"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/settings"]["get"]["parameters"][0]["description"] = "Emulator UUID or index";
    paths["/api/v1/emulator/{id}/settings"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/settings"]["get"]["responses"]["200"]["description"] = "Settings list";
    paths["/api/v1/emulator/{id}/settings"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/SettingsResponse";

    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["summary"] = "Get specific setting value";
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["tags"].append("Settings Management");
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][1]["description"] =
        "Setting name (fast_tape, turbo_tape, fast_disk, trdos_traps, audio_rate, ay_voicing, ay_punch, "
        "ay_room, beeper_punch, ...). Sound-character settings return 'allowed' with the accepted values";
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["responses"]["200"]["description"] = "Setting value";

    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["summary"] = "Update specific setting";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["tags"].append("Settings Management");
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["value"]["type"] = "string";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["value"]["description"] =
        "New setting value; audio_rate accepts one of 44100, 48000, 88200, 96000, 176400, "
        "192000 or \"auto\" (rate pin, runtime only - never persisted to the ini); "
        "ay_voicing accepts headphones (default), classic (alias legacy), flat, warm, tv or small_speaker; ay_room accepts 9db (default), off, 15db, 14db, 13db, 12db, "
        "6db, 3db, 2db, 1db; ay_punch / beeper_punch accept on/off or a JSON bool. Sound-character "
        "values apply at the next frame boundary, runtime only; unknown values return 400 with 'allowed'";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["responses"]["200"]["description"] = "Setting updated";

