    // Snapshot control endpoints
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["summary"] = "Load snapshot file";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["tags"].append("Snapshot Control");
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["description"] = "Path to snapshot file (.z80, .sna)";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["responses"]["200"]["description"] =
        "Snapshot loaded successfully";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["responses"]["400"]["description"] =
        "Invalid path or file format";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["responses"]["404"]["description"] = "Emulator not found";

    // POST /api/v1/emulator/{id}/snapshot/save - Save snapshot file
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["summary"] = "Save snapshot file";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["tags"].append("Snapshot Control");
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["description"] =
        "Save current emulator state to a snapshot file (.sna format)";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["description"] = "Path to save snapshot file (.sna)";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["force"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["force"]["description"] = "Set to true to overwrite existing file";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["responses"]["200"]["description"] =
        "Snapshot saved successfully";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["responses"]["400"]["description"] = "Failed to save snapshot";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["responses"]["404"]["description"] = "Emulator not found";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["responses"]["409"]["description"] =
        "File already exists (use force: true to overwrite)";

    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["summary"] = "Get snapshot status";
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["tags"].append("Snapshot Control");
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["responses"]["200"]["description"] =
        "Snapshot status information";

