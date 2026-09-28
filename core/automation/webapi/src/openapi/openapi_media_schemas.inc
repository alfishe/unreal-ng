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
    schemas["MediaReply"]["properties"]["savedPath"]["type"] = "string";
    schemas["MediaReply"]["properties"]["retargeted"]["type"] = "boolean";

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
