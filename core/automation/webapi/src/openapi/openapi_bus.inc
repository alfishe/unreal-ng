    // Bus analyzer: every peripheral bus (SPI masters of the Z-Controller, Next, DivMMC, NeoGS), its trace, checks, SD card
    // timing profiles and export. Design: docs/inprogress/2026-10-11-bus-analyzer/
    {
        auto param = [&paths](const std::string& path, const char* verb, const char* name, const char* in, const char* type,
                              bool required, const char* description) {
            Json::Value p;
            p["name"] = name;
            p["in"] = in;
            p["required"] = required;
            p["schema"]["type"] = type;
            p["description"] = description;
            paths[path][verb]["parameters"].append(p);
        };
        auto busParams = [&](const std::string& path, const char* verb) {
            param(path, verb, "id", "path", "string", true, "Emulator id or index");
            param(path, verb, "bus", "path", "string", true,
                  "Bus id from GET /bus: zc.spi (TS-Conf, ZX-Evo), next.spi, divmmc.spi, neogs.sd, neogs.mp3.sci, neogs.mp3.sdi, "
                  "ide0 / ide1 (ATA)");
        };
        auto eventQuery = [&](const std::string& path, const char* verb) {
            param(path, verb, "kinds", "query", "string", false,
                  "Comma list: select, deselect, byte, early, truncated, command, read, write, reg_read, reg_write, ata_command, data; "
                  "groups spi, sd, ata");
            param(path, verb, "slot", "query", "integer", false, "Device slot (chip-select line)");
            param(path, verb, "since", "query", "integer", false, "Only events after this seq");
            param(path, verb, "from", "query", "integer", false, "First frame");
            param(path, verb, "to", "query", "integer", false, "Last frame");
            param(path, verb, "limit", "query", "integer", false, "The newest N matches (0 = all)");
        };
        const char* headerText =
            "Bus header: available, id, protocol, label, byte_time_ns, devices[] {slot, name, sd {present, type, state, timing, seed, "
            "blocks_read, blocks_written}}, trace {enabled, size, capacity, evicted, rewound, last_seq}, checks {enabled, findings, "
            "by_rule}";

        const std::string listPath = "/api/v1/emulator/{id}/bus";
        paths[listPath]["get"]["summary"] = "List the machine's buses";
        paths[listPath]["get"]["tags"].append("Bus analyzer");
        paths[listPath]["get"]["description"] =
            "Every peripheral bus of the machine and of the cards in its slots, each with its devices, trace and checks state. "
            "Tracing and checks are off until switched on with POST /bus/{bus}/trace-journal.";
        param(listPath, "get", "id", "path", "string", true, "Emulator id or index");
        paths[listPath]["get"]["responses"]["200"]["description"] = std::string("buses[] of: ") + headerText;
        paths[listPath]["get"]["responses"]["404"]["description"] = "Emulator not found";

        const std::string eventsPath = "/api/v1/emulator/{id}/bus/{bus}/events";
        paths[eventsPath]["get"]["summary"] = "Bus trace events";
        paths[eventsPath]["get"]["tags"].append("Bus analyzer");
        paths[eventsPath]["get"]["description"] =
            "Transactions (chip select, byte with MOSI / MISO, early access, truncated byte) and protocol events (SD command "
            "with argument and R1, block read, block written), oldest first, each with seq, frame, t, pc and source (cpu, dma, "
            "card_cpu, card_dma).";
        busParams(eventsPath, "get");
        eventQuery(eventsPath, "get");
        paths[eventsPath]["get"]["responses"]["200"]["description"] =
            std::string(headerText) + ", events[] {seq, frame, t, pc, source, slot, kind, mosi, miso, read | write, value | command, "
                                      "argument, r1, crc_valid | block, ok}";
        paths[eventsPath]["get"]["responses"]["400"]["description"] = "A bad query parameter (the message names it)";
        paths[eventsPath]["get"]["responses"]["404"]["description"] = "Emulator or bus not found (the message lists the buses)";

        const std::string findingsPath = "/api/v1/emulator/{id}/bus/{bus}/findings";
        paths[findingsPath]["get"]["summary"] = "Bus check findings";
        paths[findingsPath]["get"]["tags"].append("Bus analyzer");
        paths[findingsPath]["get"]["description"] =
            "What the checks found: early_access, select_mid_byte, deselect_mid_command, command_while_busy, "
            "command_drops_answer, crc_error, dma_collision, ata_data_without_drq, ata_command_drops_data, ata_write_while_busy - each "
            "with severity, the PC and what it means on the real hardware.";
        busParams(findingsPath, "get");
        param(findingsPath, "get", "rules", "query", "string", false, "Comma list of rule names");
        param(findingsPath, "get", "since", "query", "integer", false, "Only findings after this seq");
        param(findingsPath, "get", "limit", "query", "integer", false, "The newest N (0 = all)");
        paths[findingsPath]["get"]["responses"]["200"]["description"] =
            std::string(headerText) + ", findings[] {seq, event_seq, frame, t, pc, source, slot, rule, severity, value, detail}";
        paths[findingsPath]["get"]["responses"]["400"]["description"] = "A bad query parameter";
        paths[findingsPath]["get"]["responses"]["404"]["description"] = "Emulator or bus not found";

        const std::string controlPath = "/api/v1/emulator/{id}/bus/{bus}/trace-journal";
        paths[controlPath]["post"]["summary"] = "Switch a bus's trace and checks";
        paths[controlPath]["post"]["tags"].append("Bus analyzer");
        paths[controlPath]["post"]["description"] =
            "Body {\"enabled\": bool, \"checks\": bool, \"clear\": bool, \"capacity\": N, \"protocol_only\": bool}; an absent key "
            "keeps the setting; protocol_only leaves out single bytes, data words and status reads (a whole boot fits). bus "
            "\"all\" applies to every bus and answers with the list. Observes only: does not change the machine.";
        busParams(controlPath, "post");
        Json::Value& controlSchema = paths[controlPath]["post"]["requestBody"]["content"]["application/json"]["schema"];
        controlSchema["type"] = "object";
        controlSchema["properties"]["enabled"]["type"] = "boolean";
        controlSchema["properties"]["checks"]["type"] = "boolean";
        controlSchema["properties"]["clear"]["type"] = "boolean";
        controlSchema["properties"]["capacity"]["type"] = "integer";
        controlSchema["properties"]["protocol_only"]["type"] = "boolean";
        paths[controlPath]["post"]["responses"]["200"]["description"] = headerText;
        paths[controlPath]["post"]["responses"]["404"]["description"] = "Emulator or bus not found";

        const std::string timingPath = "/api/v1/emulator/{id}/bus/{bus}/sd-timing";
        for (const char* verb : {"get", "post"})
        {
            paths[timingPath][verb]["summary"] = "SD card timing profile";
            paths[timingPath][verb]["tags"].append("Bus analyzer");
            paths[timingPath][verb]["description"] =
                "An SD card's timing: ideal (instant answers, the default), realistic (NCR 1-8 bytes, the data token after "
                "100 us-2 ms, write busy 250 us-20 ms, ready 50-500 ms after the first ACMD41; random by seed) or slow (the "
                "specification's worst case: NCR 8, 100 ms, 250 ms, 1 s). POST body {\"slot\", \"profile\", \"seed\"} changes "
                "the machine; the card applies it at its next access. GET reports it.";
            busParams(timingPath, verb);
            paths[timingPath][verb]["responses"]["200"]["description"] = std::string(headerText) + ", slot";
            paths[timingPath][verb]["responses"]["400"]["description"] = "Unknown profile";
            paths[timingPath][verb]["responses"]["404"]["description"] = "Emulator or bus not found, or no SD card on it";
        }
        param(timingPath, "get", "slot", "query", "integer", false, "The card's slot (default: the first card)");
        Json::Value& timingSchema = paths[timingPath]["post"]["requestBody"]["content"]["application/json"]["schema"];
        timingSchema["type"] = "object";
        timingSchema["properties"]["slot"]["type"] = "integer";
        timingSchema["properties"]["profile"]["type"] = "string";
        timingSchema["properties"]["profile"]["enum"].append("ideal");
        timingSchema["properties"]["profile"]["enum"].append("realistic");
        timingSchema["properties"]["profile"]["enum"].append("slow");
        timingSchema["properties"]["seed"]["type"] = "integer";

        const std::string exportPath = "/api/v1/emulator/{id}/bus/{bus}/export";
        paths[exportPath]["get"]["summary"] = "Export a bus trace (VCD / JSON lines)";
        paths[exportPath]["get"]["tags"].append("Bus analyzer");
        paths[exportPath]["get"]["description"] =
            "format=vcd: the SPI wires (sck, mosi, miso, cs_n per device) synthesized from the transactions at the bus's byte "
            "time, for GTKWave / PulseView. format=jsonl: the bus header line, then one event per line with every field. "
            "The event query parameters filter; limit defaults to 0 (all).";
        busParams(exportPath, "get");
        param(exportPath, "get", "format", "query", "string", false, "vcd (default) or jsonl");
        param(exportPath, "get", "path", "query", "string", false,
              "Write to this file on the emulator host instead; the reply is JSON {ok, bus, format, path, bytes}");
        eventQuery(exportPath, "get");
        paths[exportPath]["get"]["responses"]["200"]["description"] =
            "text/plain: the VCD or JSON lines; with path: {ok, bus, format, path, bytes}";
        paths[exportPath]["get"]["responses"]["400"]["description"] = "Unknown format, a bad query parameter, or a path that cannot be written";
        paths[exportPath]["get"]["responses"]["404"]["description"] = "Emulator or bus not found";
    }
