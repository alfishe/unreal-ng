    // Feature Management endpoints
    paths["/api/v1/emulator/{id}/features"]["get"]["summary"] = "List all emulator features";
    paths["/api/v1/emulator/{id}/features"]["get"]["tags"].append("Feature Management");
    paths["/api/v1/emulator/{id}/features"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/features"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/features"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/features"]["get"]["parameters"][0]["description"] = "Emulator UUID or index";
    paths["/api/v1/emulator/{id}/features"]["get"]["parameters"][0]["schema"]["type"] = "string";
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
        "Feature name (e.g., sound, breakpoints, calltrace)";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["responses"]["200"]["description"] = "Feature state";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["responses"]["404"]["description"] = "Feature not found";

    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["summary"] = "Enable or disable a feature";
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
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["responses"]["404"]["description"] = "Feature not found";

