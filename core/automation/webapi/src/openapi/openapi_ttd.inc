    // Time-Travel Debug endpoints (P1: status only — parent TDD §10.4)
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["summary"] =
        "Get TTD session state (parent TDD §10.4 — test observability)";
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["tags"].append("Time-Travel Debug");
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["description"] =
        "Returns the current TTD session state, including session frame bounds, "
        "checkpoint count, and COW page-store memory usage. The remaining TTD "
        "automation surface (start/stop/seek/step/find_last/timeline) lands with "
        "Phase 4. When TTD is not compiled in or no manager is present, the "
        "endpoint returns 200 with state=\"idle\" and ttd_available=false so it "
        "doubles as a capability probe.";
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["parameters"][0]["description"] = "Emulator UUID or index";
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["responses"]["200"]["description"] =
        "TTD session snapshot";
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/TTDStatusResponse";
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["responses"]["404"]["description"] =
        "Emulator instance not found";
    paths["/api/v1/emulator/{id}/ttd/status"]["get"]["responses"]["503"]["description"] =
        "Emulator is shutting down";
