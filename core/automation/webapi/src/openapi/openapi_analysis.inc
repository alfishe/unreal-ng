    // Analysis & Instrumentation endpoints (beam, screen digest, frame cost)
    paths["/api/v1/emulator/{id}/video/beam"]["get"]["summary"] = "Beam (raster) position";
    paths["/api/v1/emulator/{id}/video/beam"]["get"]["tags"].append("Screen State");
    paths["/api/v1/emulator/{id}/video/beam"]["get"]["description"] =
        "Current raster beam position (dot/line/zone, t-states into the frame) plus "
        "frame timing constants from the machine model.";
    paths["/api/v1/emulator/{id}/video/beam"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/video/beam"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/video/beam"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/video/beam"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/video/beam"]["get"]["responses"]["200"]["description"] =
        "Beam position + frame timing";

    paths["/api/v1/emulator/{id}/state/screen/digest"]["get"]["summary"] = "Screen content digest";
    paths["/api/v1/emulator/{id}/state/screen/digest"]["get"]["tags"].append("Screen State");
    paths["/api/v1/emulator/{id}/state/screen/digest"]["get"]["description"] =
        "Deterministic hash of the VRAM backing banks (change detection). Pass "
        "banks=5,7 to select physical RAM pages (default: model-dependent).";
    paths["/api/v1/emulator/{id}/state/screen/digest"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/screen/digest"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/screen/digest"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/screen/digest"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/screen/digest"]["get"]["parameters"][1]["name"] = "banks";
    paths["/api/v1/emulator/{id}/state/screen/digest"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/state/screen/digest"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/state/screen/digest"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/screen/digest"]["get"]["responses"]["200"]["description"] =
        "Digest (hash, banks, byte count)";

    paths["/api/v1/emulator/{id}/frame_cost"]["get"]["summary"] = "Work-vs-idle t-states per frame";
    paths["/api/v1/emulator/{id}/frame_cost"]["get"]["tags"].append("Profiler Commands");
    paths["/api/v1/emulator/{id}/frame_cost"]["get"]["description"] =
        "Per-frame accounting of active vs halted t-states (HALT bus time is rolled "
        "up per frame). Useful to quantify demo/game CPU load without a full profiler run.";
    paths["/api/v1/emulator/{id}/frame_cost"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/frame_cost"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/frame_cost"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/frame_cost"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/frame_cost"]["get"]["responses"]["200"]["description"] =
        "Frame cost counters (tstates_active/halted, frame budget)";
