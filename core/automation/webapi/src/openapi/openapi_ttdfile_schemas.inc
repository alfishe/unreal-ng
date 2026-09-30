    // TTD file info and the recorded machine (api/ttd_api.cpp; core ttdfileinfo.h)
    {
        Json::Value& m = schemas["TTDRecordedMachine"];
        m["type"] = "object";
        m["description"] =
            "The machine a TTD session was recorded on: what an instance must match to load it. Same keys in "
            "ttd/status (machine), ttd/file-info, Lua / Python ttd_status and ttd_file_info.";
        m["properties"]["model_id"]["type"] = "integer";
        m["properties"]["model_id"]["description"] = "MEM_MODEL value of the recorded model";
        m["properties"]["model"]["type"] = "string";
        m["properties"]["model"]["nullable"] = true;
        m["properties"]["model"]["description"] = "Short model name (e.g. PENTAGON); null when this build does not know the id";
        m["properties"]["ram_page_bound"]["type"] = "integer";
        m["properties"]["ram_page_bound"]["description"] = "Exclusive RAM page-index bound of the model";
        m["properties"]["rom_signature"]["type"] = "string";
        m["properties"]["rom_signature"]["nullable"] = true;
        m["properties"]["rom_signature"]["description"] =
            "FNV-1a fingerprint of the ROM set, hex string 0x... (a 64-bit value does not survive a JSON number); "
            "null = unknown, not checked on load";
        m["properties"]["peripheral_mask"]["type"] = "integer";
        m["properties"]["peripheral_mask"]["format"] = "uint64";
        m["properties"]["peripheral_mask"]["description"] = "Bit i set: peripheral id i was fitted (TTD PeripheralId)";
        m["properties"]["peripherals"]["type"] = "array";
        m["properties"]["peripherals"]["items"]["type"] = "string";
        m["properties"]["peripherals"]["description"] =
            "Fitted devices by name, ascending id: turbosound, betadisk, tape, covox, tsfm, gs, scorpion-profrom, "
            "kempston-mouse, atm-paging, profi-paging, moonsound, gs-lw, neogs, plus3-paging, upd765, evo-sdcard, "
            "tsconf, ata, ds12887, evo-ps2, evo-turbo-cache (idN for an id this build does not know)";
        m["properties"]["general_sound"]["type"] = "string";
        for (const char* v : {"none", "z80", "lw", "ngs"})
            m["properties"]["general_sound"]["enum"].append(v);
        m["properties"]["general_sound"]["description"] =
            "General Sound slot card of the recording (control/audio/gs switch_personality names): the card to fit "
            "before ttd/load";
        m["properties"]["turbo_sound"]["type"] = "string";
        for (const char* v : {"none", "turbosound", "tsfm"})
            m["properties"]["turbo_sound"]["enum"].append(v);
        m["properties"]["turbo_sound"]["description"] = "TurboSound slot device of the recording";
    }
    {
        Json::Value& f = schemas["TTDFileInfoResponse"];
        f["type"] = "object";
        f["description"] = "A .ttd file's header, sections and recorded machine (GET /api/v1/ttd/file-info)";
        f["properties"]["ok"]["type"] = "boolean";
        f["properties"]["error"]["type"] = "string";
        f["properties"]["error"]["description"] = "Why the file could not be read (ok false only)";
        f["properties"]["path"]["type"] = "string";
        f["properties"]["file_bytes"]["type"] = "integer";
        f["properties"]["file_bytes"]["format"] = "uint64";
        f["properties"]["schema_version"]["type"] = "integer";
        f["properties"]["flags"]["type"] = "integer";
        f["properties"]["flags"]["description"] = "Raw header flags (ttddumpformat.h)";
        f["properties"]["captured_at_unix_ms"]["type"] = "integer";
        f["properties"]["captured_at_unix_ms"]["format"] = "uint64";
        f["properties"]["recorded_by"]["type"] = "string";
        f["properties"]["recorded_by"]["nullable"] = true;
        f["properties"]["recorded_by"]["description"] = "Symbolic id of the instance that recorded the session";
        f["properties"]["session_state"]["type"] = "string";
        for (const char* v : {"idle", "recording", "detached"})
            f["properties"]["session_state"]["enum"].append(v);
        f["properties"]["session_start_frame"]["type"] = "integer";
        f["properties"]["session_start_frame"]["format"] = "uint64";
        f["properties"]["session_end_frame"]["type"] = "integer";
        f["properties"]["session_end_frame"]["format"] = "uint64";
        f["properties"]["checkpoint_count"]["type"] = "integer";
        f["properties"]["page_slot_count"]["type"] = "integer";
        f["properties"]["sections"]["type"] = "object";
        for (const char* s : {"write_journal", "write_journal_complete", "coverage_index", "bookmarks",
                              "input_journal", "external_events", "port_journals", "top_clock_time"})
            f["properties"]["sections"]["properties"][s]["type"] = "boolean";
        f["properties"]["machine"]["$ref"] = "#/components/schemas/TTDRecordedMachine";
        f["properties"]["peripherals_from_header"]["type"] = "boolean";
        f["properties"]["peripherals_from_header"]["description"] =
            "false: the file predates the header's device mask; the device set was read at its first checkpoint";
    }
