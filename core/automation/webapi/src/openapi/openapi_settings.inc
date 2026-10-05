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
        "Setting name (fast_tape, turbo_tape, fast_disk, turbo_mode, speed, turbo_audio, turbo_active, "
        "trdos_traps, audio_rate, ay_voicing, ay_punch, ay_room, beeper_punch, ...). speed and the "
        "sound-character settings return 'allowed' with the accepted values; turbo_active returns read_only: true";
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
        "ay_voicing accepts classic (default, alias legacy), headphones, flat, warm, tv or small_speaker; ay_room accepts 9db (default), off, 15db, 14db, 13db, 12db, "
        "6db, 3db, 2db, 1db; ay_punch / beeper_punch accept on/off or a JSON bool. Sound-character "
        "values apply at the next frame boundary, runtime only; unknown values return 400 with 'allowed'. "
        "speed: host speed multiplier 1, 2, 4, 8 or 16 (a JSON number, or a decimal / \"0x..\" string), applied at "
        "the next frame; while a time-travel (TTD) recording runs only 1 is accepted (409 otherwise), and a change "
        "on a stopped or loaded TTD session drops that session's history. turbo_mode: bool, run as fast as the "
        "host allows (the turbomode feature); true answers 409 while a TTD recording runs. turbo_audio: bool, "
        "keep audio (at a raised pitch) in turbo mode. turbo_active is read-only (400): it reports whether the "
        "engine runs unthrottled now - turbo_mode, or turbo tape warping a load";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["responses"]["200"]["description"] = "Setting updated";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["responses"]["400"]["description"] =
        "Bad value (e.g. speed not 1, 2, 4, 8 or 16), or a PUT to the read-only turbo_active";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["responses"]["404"]["description"] =
        "Unknown emulator id or unknown setting";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["responses"]["409"]["description"] =
        "Refused while a time-travel (TTD) recording runs: speed other than 1, or turbo_mode true";

