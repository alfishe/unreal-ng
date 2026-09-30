    // Snapshot control endpoints
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["summary"] = "Load snapshot file";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["description"] = "Load snapshot from path or embedded content. Accepts: JSON with path, multipart/form-data with file, or raw body with X-Filename header. Max size: 4 MB.";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["tags"].append("Snapshot Control");
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["schema"]["type"] = "string";
    // JSON body option
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["description"] = "Path to snapshot file (.z80, .sna, .szx; an .rzx recording plays on this machine, see rzx/play)";
    // Multipart upload option
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["multipart/form-data"]["schema"]
         ["properties"]["file"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["multipart/form-data"]["schema"]
         ["properties"]["file"]["format"] = "binary";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["multipart/form-data"]["schema"]
         ["properties"]["file"]["description"] = "Snapshot file content";
    // Raw body option
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["application/octet-stream"]["schema"]
         ["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["application/octet-stream"]["schema"]
         ["format"] = "binary";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["responses"]["200"]["description"] =
        "Snapshot loaded successfully";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["responses"]["400"]["description"] =
        "Invalid path or file format";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["responses"]["404"]["description"] = "Emulator not found";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["responses"]["413"]["description"] =
        "File too large (max 4 MB)";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["responses"]["409"]["description"] =
        "A TTD recording is running; the message says why the action would destroy it. Stop the recording first.";

    // POST /api/v1/emulator/{id}/snapshot/save - Save snapshot file
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["summary"] = "Save snapshot file";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["tags"].append("Snapshot Control");
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["description"] =
        "Save current emulator state to a snapshot file (.sna, .z80 or .szx, chosen by the extension)";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["description"] = "Path to save snapshot file (.sna, .z80, .szx)";
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


    // POST /api/v1/emulator/{id}/snapshot/transfer - in-memory state transfer (MachineStateTransfer)
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/snapshot/transfer"]["post"];
        op["summary"] = "Transfer the running state to another instance";
        op["tags"].append("Snapshot Control");
        op["description"] =
            "Move this instance's state into another instance in memory, without a snapshot file. Same model and "
            "RAM size: a full clone (in-frame position, every device). Another model: what the target can express "
            "(pages, CPU, #7FFD/#1FFD/#EFF7 paging, border, TR-DOS paging, machine-independent devices: TurboSound "
            "/ TSFM, Covox, General Sound / NeoGS with RAM and flash, MoonSound with wave SRAM, Kempston mouse). "
            "Refused with a per-item reason when the target cannot hold the state (pages it lacks, +2A/+3 all-RAM "
            "modes on a 128K, extended Pentagon / Scorpion paging on another family, ATM / Profi / TSConf on "
            "another model, ZX-Poly modules). Floppies and the tape follow as the target's own in-memory copies (clean, standing for a postfixed file such as game.pentagon-1a2b3c4d.trd, so a save never overwrites the source's image), with their controllers. NOT moved: SD cards, hard disks and CDs (the target keeps its own; the report says so). A target floppy or tape with unsaved writes refuses the transfer (422). Give 'to' for an existing "
            "instance or 'model' for a new one; a new instance gets the source's sound cards.";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        op["parameters"][0]["description"] = "Source emulator id";
        op["parameters"][0]["schema"]["type"] = "string";
        Json::Value& props = op["requestBody"]["content"]["application/json"]["schema"]["properties"];
        op["requestBody"]["required"] = true;
        props["to"]["type"] = "string";
        props["to"]["description"] = "Existing target emulator id (exclusive with 'model')";
        props["model"]["type"] = "string";
        props["model"]["description"] = "Model short name of a new target instance (exclusive with 'to'; see /api/v1/emulator/models)";
        props["ram_size"]["type"] = "integer";
        props["ram_size"]["description"] = "New instance RAM size in KB (default: the model's default)";
        props["symbolic_id"]["type"] = "string";
        props["symbolic_id"]["description"] = "New instance symbolic id (default: transfer-<model>)";
        props["start"]["type"] = "boolean";
        props["start"]["description"] = "Start the new instance (default: when the source is running)";
        props["check"]["type"] = "boolean";
        props["check"]["description"] = "With 'to': only decide whether the transfer is possible; nothing changes";
        props["keep_frame_position"]["type"] = "boolean";
        props["keep_frame_position"]["default"] = true;
        props["keep_frame_position"]["description"] =
            "Keep the source's in-frame position when both models share the frame geometry; otherwise the target "
            "starts at its frame start";
        const char* reportText =
            "Transfer report: ok, mode (clone | cross-model), reason, items [{name, status: copied | dropped | "
            "refused | note, detail}], copied/dropped/refused counts, summary; source_id, target_id, created, state";
        op["responses"]["200"]["description"] = std::string("Transferred into the existing instance (or check result). ") + reportText;
        op["responses"]["201"]["description"] = std::string("New instance created and transferred into; adds model, ram_kb. ") + reportText;
        op["responses"]["400"]["description"] = "Neither or both of 'to' / 'model', unknown or non-creatable model, bad RAM size, 'check' without 'to'";
        op["responses"]["404"]["description"] = "Source or target emulator not found";
        op["responses"]["409"]["description"] = "The target records a TTD session; stop the recording first";
        op["responses"]["422"]["description"] = "The target cannot hold the state; the report says why, per item. Nothing changed";
    }
