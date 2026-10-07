    // Snapshot control endpoints
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["summary"] = "Load snapshot file";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["description"] = "Load snapshot from path or embedded content. Accepts: JSON with path, multipart/form-data with file, or raw body with X-Filename header. Max size: 4 MB. A file that needs another machine (a TS-Conf .spg program) switches the model first (ModelSwitch, media kept): the answer's emulator_id is then the new instance, model_switched = true, previous_emulator_id the released one. switch_model = false refuses instead (409 with required_model).";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["tags"].append("Snapshot Control");
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["schema"]["type"] = "string";
    // JSON body option
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["description"] = "Path to snapshot file (.z80, .sna, .szx, .spg - a TS-Conf program, switches to model TSL; an .rzx recording plays on this machine, see rzx/play)";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["switch_model"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["switch_model"]["description"] =
        "A file that needs another model (.spg: TSL): switch the model first (true, default) or refuse (false). Also a query parameter for uploads";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["commit"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["commit"]["description"] =
        "Who writes the machine: omitted = the plan decides (the machine's own policy, else the legacy commit that has always loaded snapshots), 'legacy' = the legacy commit even where the machine has a policy, or a registered policy name (an unknown name is refused and the answer lists the known ones). Also a query parameter for uploads. The answer's report shows the commit that ran";
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
        "Invalid path or file format, or the load was refused (report.refused, report.reason, report.needs say why: a machine policy that cannot take the snapshot in the machine's current state, an unknown commit name)";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["responses"]["404"]["description"] = "Emulator not found";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["responses"]["413"]["description"] =
        "File too large (max 4 MB)";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["responses"]["409"]["description"] =
        "A TTD recording is running (the message says why the action would destroy it; stop the recording first), or the file needs another model and switch_model is false (required_model, required_ram_kb)";

    // POST /api/v1/emulator/{id}/snapshot/save - Save snapshot file
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["summary"] = "Save snapshot file";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["tags"].append("Snapshot Control");
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["description"] =
        "Save the machine's snapshot view to a file (.sna, .z80 or .szx, chosen by the extension). The emulator is stopped first "
        "and the call waits until it has parked (refused if it does not stop within a second). A snapshot holds what a "
        "Spectrum 128K program sees (RAM banks 0-7, #7FFD, CPU, AY): the Sprinter in a ZX mode gives its banks in Spectrum "
        "order and is named by the mode ('Pentagon 128' for P128.ZX, ...); TS-Conf and the ATMs give it while their memory "
        "is laid out as a 128K. Where no such view exists, or the format cannot hold the state, nothing is written and the "
        "400 answer carries 'reason', a machine-readable 'needs' (zx_mode, mode:128k, format:szx, format:z80, pause, "
        "capture_unsupported, ...) and 'formats' = what the machine can be saved as right now. Pentagon 512 / 1024 save as "
        ".szx only; Scorpion as .z80 or .szx. No file is converted into another.";
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
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["responses"]["400"]["description"] =
        "Refused or failed: message, reason, needs, formats (what would work); nothing was written";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["responses"]["404"]["description"] = "Emulator not found";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["responses"]["409"]["description"] =
        "File already exists (use force: true to overwrite)";

    // GET /api/v1/emulator/{id}/snapshot/formats - which formats can be saved now
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/snapshot/formats"]["get"];
        op["summary"] = "Which snapshot formats this machine can be saved in";
        op["tags"].append("Snapshot Control");
        op["description"] =
            "Ask before saving: for each of sna, z80 and szx whether the machine can be saved in it right now, and when not "
            "why ('reason') and what would work ('needs': format:szx, format:z80, zx_mode, mode:128k, ...). 'machine' = what "
            "the file will say it was made on, 'view' = how the state is taken (the Sprinter's mode, the 128K window map). "
            "The answer follows the machine's current mode, so ask again after a mode change.";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        op["parameters"][0]["schema"]["type"] = "string";
        op["responses"]["200"]["description"] = "view_available, machine, view, formats[{format, available, reason, needs, note}]";
        op["responses"]["404"]["description"] = "Emulator not found";
    }

    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["summary"] = "Get snapshot status";
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["tags"].append("Snapshot Control");
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["responses"]["200"]["description"] =
        "Snapshot status information; report = what the snapshot pipeline did with the last load (commit, verdicts, per-block outcomes, warnings, refusal)";

    // POST /api/v1/emulator/{id}/snapshot/inspect - what a load would do, nothing written
    {
        Json::Value& op = paths["/api/v1/emulator/{id}/snapshot/inspect"]["post"];
        op["summary"] = "Inspect a snapshot file against this machine";
        op["tags"].append("Snapshot Control");
        op["description"] =
            "Read a snapshot (.sna, .z80, .szx, .spg) and report what loading it here would do, without touching the "
            "machine: 'image' = the file in the pipeline's neutral form (format and version, the machine it was made on, "
            "memory model, RAM banks as size + hash, CPU, paging latches, border, AY, extensions, warnings), 'plan' = who "
            "would commit it (the caller's commit, the machine's own policy, or the legacy commit) or the refusal and its "
            "reason, would_load and would_commit. Decide with it before a load. Accepts the same sources as snapshot/load "
            "(JSON path, multipart file, raw body).";
        op["parameters"][0]["name"] = "id";
        op["parameters"][0]["in"] = "path";
        op["parameters"][0]["required"] = true;
        op["parameters"][0]["schema"]["type"] = "string";
        Json::Value& props = op["requestBody"]["content"]["application/json"]["schema"]["properties"];
        props["path"]["type"] = "string";
        props["path"]["description"] = "Path to the snapshot file";
        props["commit"]["type"] = "string";
        props["commit"]["description"] = "As in snapshot/load: see what the plan would do for this commit choice";
        op["responses"]["200"]["description"] = "The image and the plan";
        op["responses"]["400"]["description"] = "Not a snapshot this emulator can read (the message says why)";
        op["responses"]["404"]["description"] = "Emulator not found";
    }


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
