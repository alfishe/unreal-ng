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
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][1]["description"] = "Setting name";
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
         ["properties"]["value"]["description"] = "New setting value";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["responses"]["200"]["description"] = "Setting updated";

