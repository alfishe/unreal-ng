    // Tape control endpoints
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["summary"] = "Load tape image";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["description"] = "Load tape from path or embedded content. Accepts: JSON with path, multipart/form-data with file, or raw body with X-Filename header. Max size: 1 MB.";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["tags"].append("Tape Control");
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["parameters"][0]["schema"]["type"] = "string";
    // JSON body option
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["description"] =
        "A tape file (.tap .tzx .spc .sta .ltp .zxt; the content decides the format) or a folder built into a tape";
    // Multipart upload option
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["requestBody"]["content"]["multipart/form-data"]["schema"]
         ["properties"]["file"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["requestBody"]["content"]["multipart/form-data"]["schema"]
         ["properties"]["file"]["format"] = "binary";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["requestBody"]["content"]["multipart/form-data"]["schema"]
         ["properties"]["file"]["description"] = "Tape image file content";
    // Raw body option
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["requestBody"]["content"]["application/octet-stream"]["schema"]
         ["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["requestBody"]["content"]["application/octet-stream"]["schema"]
         ["format"] = "binary";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["responses"]["200"]["description"] = "Tape loaded successfully";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["responses"]["400"]["description"] = "Invalid path or file format";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["responses"]["404"]["description"] = "Emulator not found";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["responses"]["413"]["description"] = "File too large (max 1 MB)";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["responses"]["409"]["description"] =
        "A TTD recording is running; the message says why the action would destroy it. Stop the recording first.";

    paths["/api/v1/emulator/{id}/tape/eject"]["post"]["summary"] = "Eject tape";
    paths["/api/v1/emulator/{id}/tape/eject"]["post"]["description"] =
        "The tape leaves the tape slot of the media manager (same as POST /media/tape/eject).";
    paths["/api/v1/emulator/{id}/tape/eject"]["post"]["tags"].append("Tape Control");
    paths["/api/v1/emulator/{id}/tape/eject"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/tape/eject"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape/eject"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/tape/eject"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/eject"]["post"]["responses"]["200"]["description"] = "Tape ejected";
    paths["/api/v1/emulator/{id}/tape/eject"]["post"]["responses"]["409"]["description"] =
        "A TTD recording is running: the media set is fixed. Stop the recording first.";

    paths["/api/v1/emulator/{id}/tape/play"]["post"]["summary"] = "Start tape playback";
    paths["/api/v1/emulator/{id}/tape/play"]["post"]["tags"].append("Tape Control");
    paths["/api/v1/emulator/{id}/tape/play"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/tape/play"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape/play"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/tape/play"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/play"]["post"]["responses"]["200"]["description"] = "Tape playback started";

    paths["/api/v1/emulator/{id}/tape/stop"]["post"]["summary"] = "Stop tape playback";
    paths["/api/v1/emulator/{id}/tape/stop"]["post"]["tags"].append("Tape Control");
    paths["/api/v1/emulator/{id}/tape/stop"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/tape/stop"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape/stop"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/tape/stop"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/stop"]["post"]["responses"]["200"]["description"] = "Tape playback stopped";

    paths["/api/v1/emulator/{id}/tape/rewind"]["post"]["summary"] = "Rewind tape to beginning";
    paths["/api/v1/emulator/{id}/tape/rewind"]["post"]["tags"].append("Tape Control");
    paths["/api/v1/emulator/{id}/tape/rewind"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/tape/rewind"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape/rewind"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/tape/rewind"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/rewind"]["post"]["responses"]["200"]["description"] = "Tape rewound";

    paths["/api/v1/emulator/{id}/tape/info"]["get"]["summary"] = "Get tape status";
    paths["/api/v1/emulator/{id}/tape/info"]["get"]["tags"].append("Tape Control");
    paths["/api/v1/emulator/{id}/tape/info"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/tape/info"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape/info"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/tape/info"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/info"]["get"]["responses"]["200"]["description"] = "Tape status information";

    // Tape snapshot and seek endpoints (tape-manager design §7.2)
    paths["/api/v1/emulator/{id}/tape"]["get"]["summary"] = "Full tape snapshot: state, position, fast-load plan and block catalog";
    paths["/api/v1/emulator/{id}/tape"]["get"]["tags"].append("Tape Control");
    paths["/api/v1/emulator/{id}/tape"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/tape"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/tape"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape"]["get"]["responses"]["200"]["description"] = "Tape snapshot (loaded/empty, playback state, position, fast_load plan, blocks catalog)";

    paths["/api/v1/emulator/{id}/tape/pause"]["post"]["summary"] = "Pause tape playback (play after it resumes in place)";
    paths["/api/v1/emulator/{id}/tape/pause"]["post"]["tags"].append("Tape Control");
    paths["/api/v1/emulator/{id}/tape/pause"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/tape/pause"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape/pause"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/tape/pause"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/pause"]["post"]["responses"]["200"]["description"] = "Tape paused";
    paths["/api/v1/emulator/{id}/tape/pause"]["post"]["responses"]["400"]["description"] = "Tape is not playing";

    paths["/api/v1/emulator/{id}/tape/seek"]["post"]["summary"] = "Seek tape to a block index (forward or backward; never starts playback)";
    paths["/api/v1/emulator/{id}/tape/seek"]["post"]["tags"].append("Tape Control");
    paths["/api/v1/emulator/{id}/tape/seek"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/tape/seek"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape/seek"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/tape/seek"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/seek"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["block"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/tape/seek"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["block"]["description"] = "Zero-based block index";
    paths["/api/v1/emulator/{id}/tape/seek"]["post"]["responses"]["200"]["description"] = "Seeked; resulting position returned";
    paths["/api/v1/emulator/{id}/tape/seek"]["post"]["responses"]["400"]["description"] = "Block index out of range";

    paths["/api/v1/emulator/{id}/tape/blocks/{index}"]["get"]["summary"] = "Get one tape block descriptor with a payload preview";
    paths["/api/v1/emulator/{id}/tape/blocks/{index}"]["get"]["tags"].append("Tape Control");
    paths["/api/v1/emulator/{id}/tape/blocks/{index}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/tape/blocks/{index}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape/blocks/{index}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/tape/blocks/{index}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/blocks/{index}"]["get"]["parameters"][1]["name"] = "index";
    paths["/api/v1/emulator/{id}/tape/blocks/{index}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape/blocks/{index}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/tape/blocks/{index}"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/tape/blocks/{index}"]["get"]["responses"]["200"]["description"] = "Block descriptor (kind, header fields, speed profile, checksum) + 64-byte hex preview";
    paths["/api/v1/emulator/{id}/tape/blocks/{index}"]["get"]["responses"]["404"]["description"] = "Block index out of range";

    // Tape audio bridge endpoints (tape-audio-bridge design §7.2)
    paths["/api/v1/emulator/{id}/tape/render"]["post"]["summary"] = "Render tape image (or inserted tape) to WAV/FLAC audio";
    paths["/api/v1/emulator/{id}/tape/render"]["post"]["tags"].append("Tape Control");
    paths["/api/v1/emulator/{id}/tape/render"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/tape/render"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape/render"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/tape/render"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/render"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["sourcePath"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/render"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["sourcePath"]["description"] = "Tape image to render (.tap, .tzx, ...); omit to render the inserted tape";
    paths["/api/v1/emulator/{id}/tape/render"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["blocks"]["description"] = "\"all\" (default) or [first, last] catalog indices";
    paths["/api/v1/emulator/{id}/tape/render"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["format"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/render"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["format"]["description"] = "\"wav\" or \"flac\"; must match the outputPath extension that picks the encoder";
    paths["/api/v1/emulator/{id}/tape/render"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["sampleRate"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/tape/render"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["amplitude"]["type"] = "number";
    paths["/api/v1/emulator/{id}/tape/render"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["invertLevel"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/tape/render"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["outputPath"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/render"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["outputPath"]["description"] = "Output .wav or .flac (.flac requires ffmpeg); required";
    paths["/api/v1/emulator/{id}/tape/render"]["post"]["responses"]["200"]["description"] =
        "Render result: blocks_rendered, first/last block, duration_sec, samples_written, encoder, warnings";
    paths["/api/v1/emulator/{id}/tape/render"]["post"]["responses"]["400"]["description"] =
        "Bad body, unknown source, out-of-range blocks, or render/encoder failure";

    paths["/api/v1/emulator/{id}/tape/import"]["post"]["summary"] = "Import WAV/FLAC/MP3 audio as a .tzx/.tap tape image";
    paths["/api/v1/emulator/{id}/tape/import"]["post"]["tags"].append("Tape Control");
    paths["/api/v1/emulator/{id}/tape/import"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/tape/import"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape/import"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/tape/import"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/import"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["sourcePath"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/import"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["sourcePath"]["description"] = "Audio file to import (.wav, .flac, .mp3); required";
    paths["/api/v1/emulator/{id}/tape/import"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["outputPath"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/import"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["outputPath"]["description"] = "Output .tzx (exact pulses) or .tap (ROM-standard content only); required";
    paths["/api/v1/emulator/{id}/tape/import"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["target"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/import"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["target"]["description"] = "\"auto\" (default), \"tzx\" or \"tap\"; must match the outputPath extension";
    paths["/api/v1/emulator/{id}/tape/import"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["hysteresis"]["type"] = "number";
    paths["/api/v1/emulator/{id}/tape/import"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["hysteresis"]["description"] = "Schmitt band, 0.05-0.45 of signal range (default 0.2)";
    paths["/api/v1/emulator/{id}/tape/import"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["insert"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/tape/import"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["insert"]["description"] = "Insert the saved image into this instance (default false)";
    paths["/api/v1/emulator/{id}/tape/import"]["post"]["responses"]["200"]["description"] =
        "Import result: decoder, blocks_recognized, kind_counts, blocks_written, catalog preview, warnings";
    paths["/api/v1/emulator/{id}/tape/import"]["post"]["responses"]["400"]["description"] =
        "Bad body, decode failure, or .tap gate refusal (tap_refusal_reason names the .tzx alternative)";

    // Disk control endpoints
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["summary"] = "Insert disk image";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["description"] = "Insert disk from path or embedded content. Accepts: JSON with path, multipart/form-data with file, or raw body with X-Filename header. Max size: 1 MB.";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["tags"].append("Disk Control");
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][1]["description"] =
        "Drive letter (A-D) or number (0-3)";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][1]["schema"]["type"] = "string";
    // JSON body option
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["description"] = "Path to disk image file (.trd, .scl, .fdi, .udi)";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["autostart"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["autostart"]["description"] = "Quick-reset into TR-DOS and run (drive A only, default false)";
    // Multipart upload option
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["requestBody"]["content"]["multipart/form-data"]["schema"]
         ["properties"]["file"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["requestBody"]["content"]["multipart/form-data"]["schema"]
         ["properties"]["file"]["format"] = "binary";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["requestBody"]["content"]["multipart/form-data"]["schema"]
         ["properties"]["file"]["description"] = "Disk image file content";
    // Raw body option
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["requestBody"]["content"]["application/octet-stream"]["schema"]
         ["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["requestBody"]["content"]["application/octet-stream"]["schema"]
         ["format"] = "binary";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["responses"]["200"]["description"] =
        "Disk inserted successfully";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["responses"]["409"]["description"] =
        "A TTD recording is running; the message says why the action would destroy it. Stop the recording first.";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["responses"]["400"]["description"] =
        "Invalid path, file format, or drive parameter";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["responses"]["413"]["description"] =
        "File too large (max 4 MB: TRD / SCL / FDI, 1.44 MB PC floppies, HD UDI)";

    // POST /api/v1/emulator/{id}/disk/{drive}/create - Create blank disk
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["summary"] = "Create blank disk";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["tags"].append("Disk Control");
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["description"] =
        "Create a blank disk and insert it into the specified drive. format 'unformatted' has no sectors "
        "(for the machine's own FORMAT: TR-DOS, +3DOS); 'plus3' is a formatted +3DOS disk (9 x 512-byte "
        "sectors per track). 'auto' (the default) picks plus3 on a +3 and unformatted elsewhere.";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["parameters"][1]["description"] =
        "Drive letter (A, B, C, or D)";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["type"] = "object";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["cylinders"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["format"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["format"]["description"] = "auto (default), unformatted or plus3";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["cylinders"]["description"] =
        "Number of cylinders/tracks (40 or 80; default: 80 unformatted, 40 plus3)";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["sides"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["sides"]["description"] = "Number of sides (1 or 2; default: 2 unformatted, 1 plus3)";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["responses"]["200"]["description"] =
        "Blank disk created and inserted";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["responses"]["409"]["description"] =
        "A TTD recording is running; the message says why the action would destroy it. Stop the recording first.";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["responses"]["400"]["description"] =
        "Invalid drive, format or geometry parameters";

    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["summary"] = "Eject disk";
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["tags"].append("Disk Control");
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["responses"]["200"]["description"] = "Disk ejected";

    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["summary"] = "Get disk drive status";
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["tags"].append("Disk Control");
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["responses"]["200"]["description"] =
        "Disk drive status information";

    // Disk Inspection endpoints
    // GET /api/v1/emulator/{id}/disk - List all drives
    paths["/api/v1/emulator/{id}/disk"]["get"]["summary"] = "List all disk drives";
    paths["/api/v1/emulator/{id}/disk"]["get"]["tags"].append("Disk Inspection");
    paths["/api/v1/emulator/{id}/disk"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk"]["get"]["responses"]["200"]["description"] =
        "List of drives with status, FDC state, and auto-selection info";

    // GET /api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec} - Read sector
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["summary"] = "Read sector data";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["tags"].append("Disk Inspection");
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][0]["schema"]["type"] =
        "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][1]["schema"]["type"] =
        "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][2]["name"] = "cyl";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][2]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][2]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][2]["schema"]["type"] =
        "integer";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][3]["name"] = "side";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][3]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][3]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][3]["schema"]["type"] =
        "integer";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][4]["name"] = "sec";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][4]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][4]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][4]["description"] =
        "Sector number (1-based)";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][4]["schema"]["type"] =
        "integer";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["responses"]["200"]["description"] =
        "Sector data with address mark, CRC status, and base64-encoded content";

    // PUT /api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec} - a debugger's sector write (SectorWrite)
    {
        const char* kSector = "/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}";
        Json::Value& op = paths[kSector]["put"];
        op["summary"] = "Write bytes into a sector's data field (debugger)";
        op["tags"].append("Disk Inspection");
        op["description"] =
            "The sector is found by its ID (the R byte of its address mark, 1-based on TR-DOS) on the track of the "
            "cylinder and side, whatever the interleave. The bytes go into its data field at offset the way the "
            "WD1793 WRITE SECTOR puts them there: the data CRC is recalculated, the address mark is kept, the image "
            "counts as modified (save on eject). Refused (400 with the reason): an empty drive, a write-protected "
            "disk, a missing track or sector, an ID-only sector, bytes past the data field. It runs at a coherent "
            "moment (paused, stopped or between two frames) and a TTD recording keeps it as a debugger-edit marker. "
            "Example: drive A, 0/0/9, {\"offset\":245,\"hex\":\"4D594449534B\"} renames a TR-DOS disk to MYDISK.";
        op["parameters"] = paths[kSector]["get"]["parameters"];
        op["requestBody"]["required"] = true;
        Json::Value& body = op["requestBody"]["content"]["application/json"]["schema"];
        body["type"] = "object";
        body["properties"]["offset"]["type"] = "integer";
        body["properties"]["offset"]["description"] = "Offset in the data field (default 0)";
        body["properties"]["hex"]["type"] = "string";
        body["properties"]["hex"]["description"] = "Bytes as hex, spaces and commas allowed (one of hex, data, base64)";
        body["properties"]["data"]["type"] = "array";
        body["properties"]["data"]["items"]["type"] = "integer";
        body["properties"]["data"]["description"] = "Bytes 0..255";
        body["properties"]["base64"]["type"] = "string";
        body["properties"]["base64"]["description"] = "Bytes as base64";
        op["responses"]["200"]["description"] =
            "Written: {drive, cylinder, side, sector, offset, bytes_written, sector_size, moment}";
        op["responses"]["400"]["description"] = "Bad request or refused (the message names the reason)";
        op["responses"]["404"]["description"] = "No such emulator";
        op["responses"]["503"]["description"] = "No coherent moment within 500 ms: try again";
    }

    // GET /api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}/raw - Read raw sector
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}/raw"]["get"]["summary"] =
        "Read raw sector bytes";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}/raw"]["get"]["tags"].append("Disk Inspection");
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}/raw"]["get"]["description"] =
        "Returns raw sector bytes including gaps, sync, and marks";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}/raw"]["get"]["responses"]["200"]
         ["description"] = "Raw sector bytes as base64";

    // GET /api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side} - Read track summary
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["summary"] = "Read track summary";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["tags"].append("Disk Inspection");
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][2]["name"] = "cyl";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][2]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][2]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][2]["schema"]["type"] =
        "integer";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][3]["name"] = "side";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][3]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][3]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][3]["schema"]["type"] =
        "integer";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["responses"]["200"]["description"] =
        "Track overview with sector metadata";

    // GET /api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}/raw - Read raw track
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}/raw"]["get"]["summary"] = "Read raw track bytes";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}/raw"]["get"]["tags"].append("Disk Inspection");
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}/raw"]["get"]["description"] =
        "Returns complete 6250-byte MFM stream";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}/raw"]["get"]["responses"]["200"]["description"] =
        "Raw track bytes as base64";

    // GET /api/v1/emulator/{id}/disk/{drive}/image - Full image dump
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["summary"] = "Dump entire disk image";
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["tags"].append("Disk Inspection");
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["description"] =
        "Returns all tracks concatenated as base64. Warning: large response.";
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["responses"]["200"]["description"] =
        "Complete disk image as base64 with geometry metadata";

    // GET /api/v1/emulator/{id}/disk/{drive}/sysinfo - TR-DOS system sector
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["summary"] = "Get TR-DOS system info";
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["tags"].append("Disk Inspection");
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["description"] =
        "Parses TR-DOS system sector (T0/S9) with disk type, label, file count, free sectors";
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["responses"]["200"]["description"] =
        "Parsed TR-DOS system sector";

    // GET /api/v1/emulator/{id}/disk/{drive}/catalog - Disk catalog
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["summary"] = "Get disk catalog";
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["tags"].append("Disk Inspection");
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["description"] =
        "Parses TR-DOS directory (sectors 1-8) returning file names, types, sizes";
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["responses"]["200"]["description"] = "Disk file listing";


    // GET /api/v1/emulator/{id}/disk/{drive} - Get specific disk drive info
    paths["/api/v1/emulator/{id}/disk/{drive}"]["get"]["summary"] = "Get disk drive info";
    paths["/api/v1/emulator/{id}/disk/{drive}"]["get"]["tags"].append("Disk Control");
    paths["/api/v1/emulator/{id}/disk/{drive}"]["get"]["description"] =
        "Get information about a specific disk drive (A, B, C, D).";
    paths["/api/v1/emulator/{id}/disk/{drive}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}"]["get"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}"]["get"]["parameters"][1]["description"] = "Drive letter (A, B, C, D)";
    paths["/api/v1/emulator/{id}/disk/{drive}"]["get"]["responses"]["200"]["description"] = "Disk drive information";
    paths["/api/v1/emulator/{id}/disk/{drive}"]["get"]["responses"]["404"]["description"] = "Emulator or drive not found";

