    // Media reply (MediaControl's envelope; media-control-design.md MC-1, MC-10)
    schemas["MediaReply"]["type"] = "object";
    schemas["MediaReply"]["properties"]["ok"]["type"] = "boolean";
    schemas["MediaReply"]["properties"]["error"]["type"] = "string";
    schemas["MediaReply"]["properties"]["error"]["description"] =
        "unknown-slot, ambiguous-slot, kind-mismatch, unreadable-source, unknown-format, dirty, recording, in-use, "
        "not-supported, does-not-fit, io-error, bad-request (only when ok is false)";
    schemas["MediaReply"]["properties"]["message"]["type"] = "string";
    schemas["MediaReply"]["properties"]["slot"]["type"] = "string";
    schemas["MediaReply"]["properties"]["slot"]["description"] = "The resolved slot id (a selector like 'B' comes back as 'fdd.b')";
    schemas["MediaReply"]["properties"]["pending"]["type"] = "boolean";
    schemas["MediaReply"]["properties"]["revision"]["type"] = "integer";
    schemas["MediaReply"]["properties"]["report"]["type"] = "array";
    schemas["MediaReply"]["properties"]["report"]["items"]["type"] = "string";
    schemas["MediaReply"]["properties"]["slots"]["type"] = "array";
    schemas["MediaReply"]["properties"]["slots"]["items"]["$ref"] = "#/components/schemas/MediaSlot";
    schemas["MediaReply"]["properties"]["detached"]["type"] = "array";
    schemas["MediaReply"]["properties"]["detached"]["items"]["$ref"] = "#/components/schemas/MediaSlot";
    schemas["MediaReply"]["properties"]["info"]["$ref"] = "#/components/schemas/MediaSlot";
    schemas["MediaReply"]["properties"]["formats"]["type"] = "object";
    schemas["MediaReply"]["properties"]["formats"]["additionalProperties"]["type"] = "array";
    schemas["MediaReply"]["properties"]["formats"]["additionalProperties"]["items"]["type"] = "string";
    schemas["MediaReply"]["properties"]["file"]["$ref"] = "#/components/schemas/MediaFileClass";
    schemas["MediaReply"]["properties"]["targets"]["type"] = "array";
    schemas["MediaReply"]["properties"]["targets"]["items"]["$ref"] = "#/components/schemas/MediaTarget";
    schemas["MediaReply"]["properties"]["default"]["type"] = "integer";
    schemas["MediaReply"]["properties"]["default"]["nullable"] = true;
    schemas["MediaReply"]["properties"]["default"]["description"] =
        "targets: the index used without asking (the only target, or drive A for a floppy image); null: ask";
    schemas["MediaReply"]["properties"]["refusal"]["type"] = "string";
    schemas["MediaReply"]["properties"]["refusal"]["nullable"] = true;
    schemas["MediaReply"]["properties"]["refusal"]["description"] = "targets: why no slot takes the file";
    schemas["MediaReply"]["properties"]["savedPath"]["type"] = "string";
    schemas["MediaReply"]["properties"]["retargeted"]["type"] = "boolean";

    schemas["MediaFileClass"]["type"] = "object";
    schemas["MediaFileClass"]["properties"]["path"]["type"] = "string";
    schemas["MediaFileClass"]["properties"]["folder"]["type"] = "boolean";
    schemas["MediaFileClass"]["properties"]["kinds"]["type"] = "array";
    schemas["MediaFileClass"]["properties"]["kinds"]["description"] =
        "Most likely first: floppy, tape, hdd, sdcard, optical, snapshot, rzx, zxpoly, symbols, rom; empty: unknown";
    schemas["MediaFileClass"]["properties"]["kinds"]["items"]["type"] = "string";
    schemas["MediaFileClass"]["properties"]["format"]["type"] = "string";
    schemas["MediaFileClass"]["properties"]["evidence"]["type"] = "array";
    schemas["MediaFileClass"]["properties"]["evidence"]["items"]["type"] = "string";

    schemas["MediaTarget"]["type"] = "object";
    schemas["MediaTarget"]["properties"]["action"]["type"] = "string";
    schemas["MediaTarget"]["properties"]["action"]["enum"].append("insert");
    schemas["MediaTarget"]["properties"]["action"]["enum"].append("load");
    schemas["MediaTarget"]["properties"]["action"]["enum"].append("newMachine");
    schemas["MediaTarget"]["properties"]["as"]["type"] = "string";
    schemas["MediaTarget"]["properties"]["slot"]["type"] = "string";
    schemas["MediaTarget"]["properties"]["slot"]["nullable"] = true;
    schemas["MediaTarget"]["properties"]["model"]["type"] = "string";
    schemas["MediaTarget"]["properties"]["model"]["description"] = "newMachine: the model to start (empty: the default)";
    schemas["MediaTarget"]["properties"]["label"]["type"] = "string";
    schemas["MediaTarget"]["properties"]["occupiedBy"]["type"] = "string";
    schemas["MediaTarget"]["properties"]["occupiedBy"]["nullable"] = true;
    schemas["MediaTarget"]["properties"]["dirty"]["type"] = "boolean";
    schemas["MediaTarget"]["properties"]["autostart"]["type"] = "boolean";

    schemas["MediaSlot"]["type"] = "object";
    schemas["MediaSlot"]["properties"]["id"]["type"] = "string";
    schemas["MediaSlot"]["properties"]["kind"]["type"] = "string";
    schemas["MediaSlot"]["properties"]["kind"]["enum"].append("floppy");
    schemas["MediaSlot"]["properties"]["kind"]["enum"].append("tape");
    schemas["MediaSlot"]["properties"]["kind"]["enum"].append("block");
    schemas["MediaSlot"]["properties"]["kind"]["enum"].append("optical");
    schemas["MediaSlot"]["properties"]["label"]["type"] = "string";
    schemas["MediaSlot"]["properties"]["index"]["type"] = "integer";
    schemas["MediaSlot"]["properties"]["aliases"]["type"] = "array";
    schemas["MediaSlot"]["properties"]["aliases"]["items"]["type"] = "string";
    schemas["MediaSlot"]["properties"]["tags"]["type"] = "array";
    schemas["MediaSlot"]["properties"]["tags"]["items"]["type"] = "string";
    schemas["MediaSlot"]["properties"]["removable"]["type"] = "boolean";
    schemas["MediaSlot"]["properties"]["acceptsFolder"]["type"] = "boolean";
    schemas["MediaSlot"]["properties"]["writeProtect"]["type"] = "boolean";
    schemas["MediaSlot"]["properties"]["guestName"]["type"] = "string";
    schemas["MediaSlot"]["properties"]["detached"]["type"] = "boolean";
    schemas["MediaSlot"]["properties"]["state"]["type"] = "string";
    schemas["MediaSlot"]["properties"]["state"]["enum"].append("empty");
    schemas["MediaSlot"]["properties"]["state"]["enum"].append("present");
    schemas["MediaSlot"]["properties"]["state"]["enum"].append("pending");
    schemas["MediaSlot"]["properties"]["state"]["enum"].append("detached");
    schemas["MediaSlot"]["properties"]["medium"]["$ref"] = "#/components/schemas/MediaMedium";

    schemas["MediaMedium"]["type"] = "object";
    schemas["MediaMedium"]["nullable"] = true;
    schemas["MediaMedium"]["properties"]["source"]["type"] = "string";
    schemas["MediaMedium"]["properties"]["format"]["type"] = "string";
    schemas["MediaMedium"]["properties"]["access"]["type"] = "string";
    schemas["MediaMedium"]["properties"]["dirty"]["type"] = "boolean";
    schemas["MediaMedium"]["properties"]["dirtyUnits"]["type"] = "integer";
    schemas["MediaMedium"]["properties"]["dirtyUnits"]["description"] = "Changed sectors (block) or tracks (floppy)";
    schemas["MediaMedium"]["properties"]["changes"]["type"] = "string";
    schemas["MediaMedium"]["properties"]["changes"]["description"] =
        "The unsaved changes for people: '1 track: 3 sectors', '1 track: whole', '5 tracks: 20 sectors total', '48 sectors'";
