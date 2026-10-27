    // Breakpoint endpoints
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["summary"] = "List all breakpoints";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["description"] =
        "Every breakpoint: id, type, address, the access flags, active, note, group; address_end for a range; page "
        "({kind, page}) and slot_only for a physical one; port_mask for a masked port; hit_mode, hit_target and the "
        "live hit_count. The user's breakpoints only: the ones the BASIC typer, the debugger adapters and steps own "
        "are left out unless all=true (then with owner and hidden).";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][1]["name"] = "all";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][1]["schema"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["responses"]["200"]["description"] = "List of breakpoints";

    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["summary"] = "Add breakpoint";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["requestBody"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/AddBreakpointRequest";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["description"] =
        "An execution, read, write, rw, port_in, port_out or port breakpoint: one address or a range (address_end); "
        "a memory address can be a label's name (\"Sparkles.End\": its CPU address), "
        "physical with 'page' (fires on that page through any slot that shows it; slot_only: through one slot), a port "
        "with a mask, a hit policy (hits / hit_mode + hit_target). Errors name the field: a range ending before it "
        "starts, a page range leaving its page, a page the machine lacks, a mask on memory, a policy without a target.";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["responses"]["201"]["description"] = "Breakpoint created";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["responses"]["400"]["description"] =
        "Bad address or type, a page on a port breakpoint, a malformed page or one the machine does not have, a range "
        "that ends before it starts or leaves its page, a mask on a memory breakpoint, a hit policy without a target";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["responses"]["201"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/AddBreakpointResponse";

    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["summary"] = "Clear all breakpoints";
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["description"] =
        "Removes the user's breakpoints; the ones the BASIC typer, the debugger adapters and steps own stay.";
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["responses"]["200"]["description"] = "All breakpoints cleared";

    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["summary"] = "Remove specific breakpoint";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][1]["name"] = "bp_id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["responses"]["200"]["description"] =
        "Breakpoint removed";

    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["summary"] = "Enable breakpoint";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][1]["name"] = "bp_id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["responses"]["200"]["description"] =
        "Breakpoint enabled";

    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["summary"] = "Disable breakpoint";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][1]["name"] = "bp_id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["responses"]["200"]["description"] =
        "Breakpoint disabled";

    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["summary"] = "Get breakpoint status";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["description"] =
        "Returns information about the last triggered breakpoint, including type (memory/port), address, and access "
        "mode; last_triggered_page ({kind, page}) when it is bound to a page; last_triggered_hit_count";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["responses"]["200"]["description"] = "Breakpoint status";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["responses"]["200"]["content"]["application/json"]
         ["schema"]["$ref"] = "#/components/schemas/BreakpointStatusResponse";

    // Hit counters (conditional-breakpoints design F5)
    paths["/api/v1/emulator/{id}/breakpoints/hits/reset"]["post"]["summary"] = "Reset breakpoint hit counters";
    paths["/api/v1/emulator/{id}/breakpoints/hits/reset"]["post"]["description"] =
        "hit_count back to 0: body {\"id\": N} for one breakpoint, no body (or no id) for all. A hit policy counts "
        "again from there.";
    paths["/api/v1/emulator/{id}/breakpoints/hits/reset"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/hits/reset"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/hits/reset"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/hits/reset"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/hits/reset"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/hits/reset"]["post"]["responses"]["200"]["description"] = "Counters reset";
    paths["/api/v1/emulator/{id}/breakpoints/hits/reset"]["post"]["responses"]["404"]["description"] = "No such breakpoint";

    // Breakpoints by selection (BreakpointSelection), groups and notes (debug tools: use cases D4.5)
    paths["/api/v1/emulator/{id}/breakpoints/active"]["get"]["summary"] = "Whether a selection of breakpoints is on";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["get"]["description"] =
        "select (query; default all): all, an id, a kind (exec, read, write, mem, port, in, out, nextreg, nextreg_read, nextreg_write, beam, copper, magic, keyboard) or group:<name>. Answers select, ids, selected and active (true: all of them on; null: none selected). The user's breakpoints only.";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/active"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/active"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["get"]["parameters"][1]["name"] = "select";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/breakpoints/active"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["get"]["responses"]["200"]["description"] = "The selection";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["get"]["responses"]["400"]["description"] = "A selection that does not parse";

    paths["/api/v1/emulator/{id}/breakpoints/active"]["put"]["summary"] = "Switch a selection of breakpoints on or off";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["put"]["description"] =
        "Body {select, active}: select is all, an id, a kind (exec, read, write, mem, port, in, out, nextreg, nextreg_read, nextreg_write, beam, copper, magic, keyboard) or group:<name> (or id / kind / group alone; none: all); active true, false or \"toggle\" (all on when one is off, else all off). One repaint for the whole selection. Answers select, ids, selected, changed, active.";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["put"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/active"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/active"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["put"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["select"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["select"]["description"] = "The selection";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["active"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["active"]["description"] = "true, false or toggle";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["put"]["responses"]["200"]["description"] = "Switched";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["put"]["responses"]["400"]["description"] = "A bad selection or active";
    paths["/api/v1/emulator/{id}/breakpoints/active"]["put"]["responses"]["404"]["description"] = "No breakpoint with that id";

    paths["/api/v1/emulator/{id}/breakpoints/remove"]["post"]["summary"] = "Remove a selection of breakpoints";
    paths["/api/v1/emulator/{id}/breakpoints/remove"]["post"]["description"] =
        "Body {select}: all, an id, a kind (exec, read, write, mem, port, in, out, nextreg, nextreg_read, nextreg_write, beam, copper, magic, keyboard) or group:<name> (none: all). Answers select and removed.";
    paths["/api/v1/emulator/{id}/breakpoints/remove"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/remove"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/remove"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/remove"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/remove"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/remove"]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/emulator/{id}/breakpoints/remove"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["select"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/remove"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["select"]["description"] = "The selection";
    paths["/api/v1/emulator/{id}/breakpoints/remove"]["post"]["responses"]["200"]["description"] = "Removed";
    paths["/api/v1/emulator/{id}/breakpoints/remove"]["post"]["responses"]["400"]["description"] = "A bad selection";
    paths["/api/v1/emulator/{id}/breakpoints/remove"]["post"]["responses"]["404"]["description"] = "No breakpoint with that id";

    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/group"]["put"]["summary"] = "Move a breakpoint to a group";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/group"]["put"]["description"] =
        "Body {group}: the group (made on use); \"\" or \"default\": back to the default group. Answers the breakpoint.";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/group"]["put"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/group"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/group"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/group"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/group"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/group"]["put"]["parameters"][1]["name"] = "bp_id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/group"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/group"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/group"]["put"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/group"]["put"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/group"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["group"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/group"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["group"]["description"] = "The group";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/group"]["put"]["responses"]["200"]["description"] = "The breakpoint";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/group"]["put"]["responses"]["400"]["description"] = "No group in the body";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/group"]["put"]["responses"]["404"]["description"] = "No such breakpoint";

    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/note"]["put"]["summary"] = "Set a breakpoint's note";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/note"]["put"]["description"] =
        "Body {note}: the annotation (\"\" clears it). Answers the breakpoint.";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/note"]["put"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/note"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/note"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/note"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/note"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/note"]["put"]["parameters"][1]["name"] = "bp_id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/note"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/note"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/note"]["put"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/note"]["put"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/note"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["note"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/note"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["note"]["description"] = "The note";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/note"]["put"]["responses"]["200"]["description"] = "The breakpoint";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/note"]["put"]["responses"]["400"]["description"] = "No note in the body";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/note"]["put"]["responses"]["404"]["description"] = "No such breakpoint";

