    // Feature Management endpoints
    paths["/api/v1/emulator/{id}/features"]["get"]["summary"] = "List all emulator features";
    paths["/api/v1/emulator/{id}/features"]["get"]["tags"].append("Feature Management");
    paths["/api/v1/emulator/{id}/features"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/features"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/features"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/features"]["get"]["parameters"][0]["description"] = "Emulator UUID or index";
    paths["/api/v1/emulator/{id}/features"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/features"]["get"]["description"] =
        "Every registered feature with the state in effect. Time-travel debugging (TTD) holds some features "
        "off: turbomode while a recording runs; fasttape, turbotape and fastdisk while a recording runs, "
        "history is replayed, or the machine sits in history. A held feature is listed as disabled.";
    paths["/api/v1/emulator/{id}/features"]["get"]["responses"]["200"]["description"] = "Feature list";
    paths["/api/v1/emulator/{id}/features"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/FeaturesResponse";

    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["summary"] = "Get specific feature state";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["tags"].append("Feature Management");
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][1]["description"] =
        "Feature id or alias (e.g., sound, breakpoints, calltrace, turbomode / turbo)";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["responses"]["200"]["description"] =
        "Feature state in effect (a feature TTD holds off reads as disabled)";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["responses"]["404"]["description"] =
        "Unknown emulator id or unknown feature";

    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["summary"] = "Enable or disable a feature";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["description"] =
        "Body {\"enabled\": true|false}. POST to the same path does the same. Disabling always succeeds; "
        "enabling a feature that time-travel debugging holds off answers 409 (see the 409 response).";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["tags"].append("Feature Management");
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["enabled"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["enabled"]["description"] = "True to enable, false to disable";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["responses"]["200"]["description"] = "Feature state updated";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["responses"]["400"]["description"] =
        "Missing 'enabled' field in the request body";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["responses"]["404"]["description"] =
        "Unknown emulator id, or unknown feature (the body lists available_features)";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["responses"]["409"]["description"] =
        "The feature exists but time-travel debugging holds it: turbomode while a recording runs; fasttape, "
        "turbotape and fastdisk while a recording runs, history is replayed, or the machine sits in history; "
        "timetravel and debugmode cannot be switched off while a recording runs. The message says why";

