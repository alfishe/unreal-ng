    // ZX Spectrum Next debugger reports and the NextREG write control
    // (docs/inprogress/2026-10-07-zx-next/design-automation-coverage.md; core DeviceState::NextDma / NextVideo / NextPalette / NextPorts /
    // NextRegRead; the same reports on the CLI `state next`, MCP inspect_state, Lua and Python). Each GET answers 404 on another machine
    {
        struct NextQuery
        {
            const char* name;
            const char* description;
        };
        struct NextReportRoute
        {
            const char* path;
            const char* summary;
            const char* description;
            const char* schema;
            std::initializer_list<NextQuery> query;
        };
        const NextReportRoute reports[] = {
            {"/api/v1/emulator/{id}/state/next/dma", "Get the Next DMA",
             "The DMA as programmed through WR0-WR6 and as it runs (core DeviceState::NextDma; CLI `state next dma`, Lua next_dma(), Python "
             "next_dma(), MCP inspect_state aspect next_dma): mode (zxn | z80, the port the last access used), enabled / transferring / "
             "waiting / end_of_block, burst (continuous | burst | byte), prescaler, auto_restart, ce_wait, ports a and b (address, type "
             "memory | io, step inc | dec | fixed, timing), direction, block_length, counter, src / dst with the kind of the 8K slot they hit "
             "now (src_kind, dst_kind: a transfer into ROM shows here), holds_bus, wait_until_28, dma_delay, the read mask / sequence and "
             "the status byte the next read returns, and the NR #CC-#CE interrupt enables. Reading it does NOT advance the read sequence.",
             "NextDmaReport", {}},
            {"/api/v1/emulator/{id}/state/next/video", "Get the Next video layers",
             "Layer order (NR #15), ULA (standard | screen1 | hicolour | hires by port #FF, scroll, clip, ULANext), LoRes, Layer 2 (resolution, "
             "banks, scroll, clip, CPU mapping), tilemap (NR #6B bits, map and tile bank and offset, scroll, clip), sprites switches, the "
             "transparency colours, raster position (frame T, hc, vc) and timing family (core DeviceState::NextVideo; CLI `state next "
             "video`, Lua next_video(), Python next_video(), MCP aspect next_video).",
             "NextVideoReport", {}},
            {"/api/v1/emulator/{id}/state/next/palette", "Get the Next palettes",
             "The eight 9-bit palettes (ula_1 layer2_1 sprites_1 tilemap_1 ula_2 layer2_2 sprites_2 tilemap_2 = NR #43 bits 6:4 order) with "
             "the selected palette, index and auto-increment, the transparent indexes, and the entries asked for: index, rgb9, red, green, "
             "blue, priority (Layer 2). Reading does not move the palette index or complete a half-written 9-bit entry (core "
             "DeviceState::NextPalette; CLI `state next palette`, Lua next_palette(), Python next_palette(), MCP aspect next_palette).",
             "NextPaletteReport",
             {{"palette", "0-7, a name (ula_1 layer2_1 sprites_1 tilemap_1 ula_2 layer2_2 sprites_2 tilemap_2), all, or empty = the selected one"},
              {"range", "entries: N or A-B (0-255, decimal or hex 0x / #); empty = all 256"}}},
            {"/api/v1/emulator/{id}/state/next/ports", "Get the Next port decode and enable word",
             "The internal port enable word (NR #82-#85) bit by bit and, with `port`, which device answers that port for a read or a write: "
             "device, decoded_by, enabled, enabled_by (NR, bit), enforced (the emulator gates the ports by that bit; the others decode "
             "regardless) and the side effect of the access (core DeviceState::NextPorts; CLI `state next ports`, Lua next_ports(), Python "
             "next_ports(), MCP aspect next_ports).",
             "NextPortsReport",
             {{"port", "hex port (6B, 0x253B, #E3, 253Bh); empty = only the enable word"},
              {"access", "r | read (default) or w | write"}}},
            {"/api/v1/emulator/{id}/state/next/copper", "Get the Next copper list",
             "The copper: control mode (NR #62 bits 7:6: stopped, restart_loop, continue_loop, restart_each_frame), the write address (NR "
             "#60 / #61 / #62 / #63 pointer, reading it does not step it), the pc, the line offset (NR #64), list_length and `instructions`: "
             "index, word, op (wait | move | halt | nop), reg / reg_name / value or hpos / vpos, text, current. A window `from` / `count`; the "
             "words around the pc come as around_pc when it is outside the window; raw = the whole 2048 bytes as hex (core "
             "DeviceState::NextCopper; CLI `state next copper`, Lua next_copper(), Python next_copper(), MCP aspect next_copper).",
             "NextCopperReport",
             {{"from", "first instruction, 0-1023 (decimal, 0x / # hex); default 0"},
              {"count", "how many, 1-1024; default 64"},
              {"raw", "true: the whole list as hex in `raw`"}}},
            {"/api/v1/emulator/{id}/state/next/sprites", "Get the Next sprites",
             "The sprite engine: switches (NR #15), clip window (NR #19), the collision / too-many flags (not cleared by the report, a port #303B "
             "read clears them), visible_count over all 128 and the attributes decoded for the visible sprites of the page (all=true: every "
             "one): kind basic | anchor | relative, x, y (9 bit; a relative sprite resolved against its anchor), pattern, four_bit, "
             "palette_offset, mirrors, rotate, scale_x / scale_y, visible, the five raw bytes; a summary of the 16K pattern memory (core "
             "DeviceState::NextSprites; CLI `state next sprites`, Lua next_sprites(), Python next_sprites(), MCP aspect next_sprites).",
             "NextSpritesReport",
             {{"from", "first sprite, 0-127; default 0"},
              {"count", "how many sprites of the page, 1-128; default 128"},
              {"all", "true: list the invisible ones too"}}},
            {"/api/v1/emulator/{id}/state/next/nextreg", "Read a NextREG without side effects",
             "One register (`reg`): value as a read returns it, the stored byte, access, reset value and decoded bits for NR #02 / #03 / #07; "
             "or the whole table (only the registers that differ from their reset with changed=true). The select latch, palette indexes and "
             "read counters are untouched (core DeviceState::NextRegRead; CLI `state next nextreg`, Lua next_nextreg(), Python "
             "next_nextreg(), MCP aspect next_nextreg). Write: POST /next/nextreg.",
             "NextRegReadReport",
             {{"reg", "hex register number (07, 0x07, #07); empty = all"},
              {"changed", "true: only registers that differ from their reset (with reg empty)"}}},
        };
        for (const NextReportRoute& r : reports)
        {
            Json::Value& get = paths[r.path]["get"];
            get["summary"] = r.summary;
            get["tags"].append("Machine State");
            get["description"] = r.description;
            get["parameters"][0]["name"] = "id";
            get["parameters"][0]["in"] = "path";
            get["parameters"][0]["required"] = true;
            get["parameters"][0]["schema"]["type"] = "string";
            Json::ArrayIndex index = 1;
            for (const NextQuery& q : r.query)
            {
                get["parameters"][index]["name"] = q.name;
                get["parameters"][index]["in"] = "query";
                get["parameters"][index]["description"] = q.description;
                get["parameters"][index]["schema"]["type"] = "string";
                index++;
            }
            get["responses"]["200"]["description"] = "Next report";
            get["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] = std::string("#/components/schemas/") + r.schema;
            get["responses"]["400"]["description"] = "Bad query parameter";
            get["responses"]["404"]["description"] = "Emulator not found, or not a ZX Spectrum Next";
        }

        const char* kNextRegCtl = "/api/v1/emulator/{id}/next/nextreg";
        Json::Value& post = paths[kNextRegCtl]["post"];
        post["summary"] = "Write a NextREG";
        post["tags"].append("Machine State");
        post["description"] =
            "Writes NextREG `reg` through the board's single write choke point, so the NextREG journal sees it, where nothing else drives the "
            "machine (paused, never started, or between two frames of a running one; no breakpoint fires, TTD records a tool edit). Body "
            "{\"reg\": \"07\", \"value\": \"03\", \"door\": \"nextreg\"}: reg and value are hex strings (07, 0x07, #07) or JSON numbers; door "
            "= nextreg (default: the NEXTREG instruction, no bus cycle, the select latch untouched), port (OUT #243B,reg then OUT #253B,value "
            "through the port decoder) or internal (the board's own write). Answers ok, reg, value, previous, after (what a read returns), door, "
            "moment. CLI `next nextreg <reg> <value> [port]`, Lua next_nextreg_write(), Python next_nextreg_write(). 409 when the machine is "
            "not a Next, 503 when no coherent moment came within 500 ms.";
        post["parameters"][0]["name"] = "id";
        post["parameters"][0]["in"] = "path";
        post["parameters"][0]["required"] = true;
        post["parameters"][0]["schema"]["type"] = "string";
        Json::Value& body = post["requestBody"]["content"]["application/json"]["schema"];
        body["type"] = "object";
        body["required"].append("reg");
        body["required"].append("value");
        body["properties"]["reg"]["type"] = "string";
        body["properties"]["reg"]["description"] = "register number, hex (or a JSON number)";
        body["properties"]["value"]["type"] = "string";
        body["properties"]["value"]["description"] = "value, hex (or a JSON number)";
        body["properties"]["door"]["type"] = "string";
        body["properties"]["door"]["enum"].append("nextreg");
        body["properties"]["door"]["enum"].append("port");
        body["properties"]["door"]["enum"].append("internal");
        post["responses"]["200"]["description"] = "The write: ok, reg, value, previous, after, door, moment";
        post["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] = "#/components/schemas/NextRegWriteResult";
        post["responses"]["400"]["description"] = "Bad body";
        post["responses"]["404"]["description"] = "Emulator not found";
        post["responses"]["409"]["description"] = "Not a ZX Spectrum Next";
        post["responses"]["503"]["description"] = "No coherent moment within 500 ms (the emulator is being stepped)";
    }
