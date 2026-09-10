    // Assembly & Listing endpoints (in-process assembler + sjasmplus .lst source mapping)
    paths["/api/v1/emulator/{id}/assemble"]["post"]["summary"] = "Assemble Z80 source to bytes";
    paths["/api/v1/emulator/{id}/assemble"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/assemble"]["post"]["description"] =
        "Two-pass Z80 assembler with labels and $/0x literals. Optionally writes the "
        "result into emulator memory and returns the assembled bytes plus listing.";
    paths["/api/v1/emulator/{id}/assemble"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/assemble"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/assemble"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/assemble"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/assemble"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["code"]["type"] = "string";
    paths["/api/v1/emulator/{id}/assemble"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["address"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/assemble"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["write"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/assemble"]["post"]["responses"]["200"]["description"] =
        "Assembly result (status/size/written/bytes/listing); status=error carries message + line";

    paths["/api/v1/emulator/{id}/listing/load"]["post"]["summary"] = "Load sjasmplus .lst listing";
    paths["/api/v1/emulator/{id}/listing/load"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/listing/load"]["description"] =
        "Parse a sjasmplus .lst file into address-to-source-line mappings for "
        "source-level stepping. Optionally clears a previously loaded listing.";
    paths["/api/v1/emulator/{id}/listing/load"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/listing/load"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/listing/load"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/listing/load"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/listing/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/listing/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["clear"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/listing/load"]["post"]["responses"]["200"]["description"] =
        "Listing loaded (line/address range statistics)";

    paths["/api/v1/emulator/{id}/listing/source_at"]["get"]["summary"] = "Source line covering an address";
    paths["/api/v1/emulator/{id}/listing/source_at"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/listing/source_at"]["get"]["description"] =
        "Map a Z80 address to its source line with surrounding context lines from the loaded listing.";
    paths["/api/v1/emulator/{id}/listing/source_at"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/listing/source_at"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/listing/source_at"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/listing/source_at"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/listing/source_at"]["get"]["parameters"][1]["name"] = "address";
    paths["/api/v1/emulator/{id}/listing/source_at"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/listing/source_at"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/listing/source_at"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/listing/source_at"]["get"]["parameters"][2]["name"] = "context";
    paths["/api/v1/emulator/{id}/listing/source_at"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/listing/source_at"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/listing/source_at"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/listing/source_at"]["get"]["responses"]["200"]["description"] =
        "Source line(s) covering the address";

    paths["/api/v1/emulator/{id}/listing/step_line"]["post"]["summary"] = "Step to next source line";
    paths["/api/v1/emulator/{id}/listing/step_line"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/listing/step_line"]["post"]["description"] =
        "Run until the next source line of the loaded listing (skips breakpoints). "
        "Body: {max_tstates?: N} — 0/absent = ~2 s of emulated time budget.";
    paths["/api/v1/emulator/{id}/listing/step_line"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/listing/step_line"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/listing/step_line"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/listing/step_line"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/listing/step_line"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["max_tstates"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/listing/step_line"]["post"]["responses"]["200"]["description"] =
        "Stepped (hit flag, current line/address)";

    paths["/api/v1/emulator/{id}/listing/run_to_line"]["post"]["summary"] = "Run until a source line";
    paths["/api/v1/emulator/{id}/listing/run_to_line"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/listing/run_to_line"]["post"]["description"] =
        "Run until execution reaches the given listing line (skips breakpoints). "
        "Body: {line: N, max_tstates?: M} — 0/absent = ~10 s of emulated time budget.";
    paths["/api/v1/emulator/{id}/listing/run_to_line"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/listing/run_to_line"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/listing/run_to_line"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/listing/run_to_line"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/listing/run_to_line"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["line"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/listing/run_to_line"]["post"]["requestBody"]["content"]["application/json"]["schema"]
        ["properties"]["max_tstates"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/listing/run_to_line"]["post"]["responses"]["200"]["description"] =
        "Run completed (hit flag, current line/address)";
