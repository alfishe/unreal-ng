    // Memory State endpoints
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["summary"] = "Get memory overview";
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["responses"]["200"]["description"] = "Memory state overview";

    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["summary"] = "Get RAM state";
    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["responses"]["200"]["description"] = "RAM state details";

    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["summary"] = "Get ROM state";
    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["responses"]["200"]["description"] = "ROM state details";

    // Paging State endpoint (P1-2 design: tagged latches + bank table)
    paths["/api/v1/emulator/{id}/state/paging"]["get"]["summary"] = "Get unified paging state";
    paths["/api/v1/emulator/{id}/state/paging"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/state/paging"]["get"]["description"] =
        "Tagged paging latches + bank table (P1-2 design). Returns model, latches array (port, tags, device, "
        "gate, latch name, value, decoded bits), banks array (bank, address_range, type, page, ROM name/role/signature "
        "when applicable), paging_locked and trdos_active flags. Same report as CLI `paging`, Lua `paging_state()`, "
        "Python `paging_state()` and MCP `inspect_state` aspect `paging`. On PROFI the pDFFD latch decodes to "
        "extended_ram_bank (bits 2:0), sco, worom, cpm, scr and video_512x240 (bit 7).";
    paths["/api/v1/emulator/{id}/state/paging"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/paging"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/paging"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/paging"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/paging"]["get"]["responses"]["200"]["description"] = "Paging state";
    paths["/api/v1/emulator/{id}/state/paging"]["get"]["responses"]["404"]["description"] = "Emulator not found";

    // Screen State endpoints
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["summary"] = "Get screen state overview";
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["tags"].append("Screen State");
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["responses"]["200"]["description"] = "Screen state overview";

    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["summary"] = "Get screen mode details";
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["tags"].append("Screen State");
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["responses"]["200"]["description"] =
        "Screen mode information. video_mode is one of the Screen mode names (e.g. ZX, P16, PROFI, PROFIHR); "
        "PROFIHR (Profi #DFFD bit 7) reports resolution 512x240 in a 608x288 framebuffer. On TS-Conf the name carries "
        "the geometry: TSZX / TS16 / TS256 / TSTX followed by 256x192, 320x200, 320x240 or 360x288 (TSTX: the text "
        "columns and rows, resolution in 14 MHz pixels); the framebuffer is 720x288 for every TS mode.";

    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["summary"] = "Get flash state";
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["tags"].append("Screen State");
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["responses"]["200"]["description"] =
        "Flash state information";

    paths["/api/v1/emulator/{id}/state/screen/attributes"]["get"]["summary"] = "Get decoded screen attributes";
    paths["/api/v1/emulator/{id}/state/screen/attributes"]["get"]["tags"].append("Screen State");
    paths["/api/v1/emulator/{id}/state/screen/attributes"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/screen/attributes"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/screen/attributes"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/screen/attributes"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/screen/attributes"]["get"]["parameters"][1]["name"] = "screen";
    paths["/api/v1/emulator/{id}/state/screen/attributes"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/state/screen/attributes"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/state/screen/attributes"]["get"]["parameters"][1]["description"] =
        "Which screen to read: 0 forces page 5, 1 forces page 7 (shadow-capable models only). Omitted reads both "
        "screens on shadow-capable models, else just the one.";
    paths["/api/v1/emulator/{id}/state/screen/attributes"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/state/screen/attributes"]["get"]["responses"]["200"]["description"] =
        "Per-cell ink/paper/bright/flash decoded from the classic ZX attribute memory layout (offset 0x1800 "
        "within a RAM page, read directly off the RAM page, not the Z80 bank mapping). Returns available, model, "
        "cols (32), rows (24), and screens: an array of {screen, ram_page, cells}, where cells is a row-major "
        "array of 768 {ink, paper, bright, flash} objects (bits 0-2 ink, 3-5 paper, 6 bright, 7 flash).";

    // Audio state endpoints
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["summary"] = "Get AY chips overview";
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["responses"]["200"]["description"] = "AY chips information";

    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["summary"] = "Get specific AY chip details";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][1]["name"] = "chip";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][1]["description"] =
        "AY chip index (0-based)";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["responses"]["200"]["description"] = "AY chip details";

    paths["/api/v1/emulator/{id}/state/audio/fm"]["get"]["summary"] = "Get TurboSound FM overview";
    paths["/api/v1/emulator/{id}/state/audio/fm"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/fm"]["get"]["description"] =
        "TurboSound FM (2 x YM2203) board latches (selected chip, status-read mode, FM enable, trim) and a per-chip summary (prescaler, channel-3 mode, keyed/sounding channels, status byte). 404 when the slot device is the plain TurboSound. Same report as the CLI `state audio fm`, Lua `audio_fm_state()`, Python `audio_fm_state()` and MCP `inspect_state` aspect `audio_fm`.";
    paths["/api/v1/emulator/{id}/state/audio/fm"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/fm"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/fm"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/fm"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/fm"]["get"]["responses"]["200"]["description"] = "Get TurboSound FM overview";

    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["summary"] = "Get TurboSound FM chip details";
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["description"] =
        "Full FM half of one YM2203: mode register (channel-3 normal/extended/CSM), timers A/B (values, periods, running, remaining T-states), busy, and every channel with block/F-number/frequency, feedback, algorithm, key-on mask, and each operator's registers (DT, MUL, TL, KS, AR, DR, SR, SL, RR, SSG-EG), pitch, live envelope state and attenuation, plus the last DAC word.";
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["parameters"][1]["name"] = "chip";
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["parameters"][1]["description"] = "FM chip index (0 or 1)";
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/state/audio/fm/{chip}"]["get"]["responses"]["200"]["description"] = "Get TurboSound FM chip details";

    paths["/api/v1/emulator/{id}/state/fdc"]["get"]["summary"] = "Get Beta Disk WD1793 state";
    paths["/api/v1/emulator/{id}/state/fdc"]["get"]["tags"].append("Disk State");
    paths["/api/v1/emulator/{id}/state/fdc"]["get"]["description"] =
        "WD1793 registers (command, track, sector, data, status), decoded status bits for the last command type, last command, FSM state, DRQ/INTRQ, Beta128 system register, density, selected drive and side, and all four drives (present, inserted, path, track, side, motor, write protect, image geometry). 404 on machines without a Beta Disk interface.";
    paths["/api/v1/emulator/{id}/state/fdc"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/fdc"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/fdc"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/fdc"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/fdc"]["get"]["responses"]["200"]["description"] = "Get Beta Disk WD1793 state";

    paths["/api/v1/emulator/{id}/state/ide"]["get"]["summary"] = "Get IDE board state";
    paths["/api/v1/emulator/{id}/state/ide"]["get"]["tags"].append("Disk State");
    paths["/api/v1/emulator/{id}/state/ide"]["get"]["description"] =
        "The machine's IDE board ([HDD] Scheme: NEMO, NEMO-A8, NEMO-DIVIDE, ATM, SMUC, PROFI, DIVIDE) and the gate it answers in, "
        "the adapter latches (read / write latch, same-port pair flags), the selected unit and INTRQ, and per unit "
        "(master, slave): kind (disk / cdrom), media slot, medium (description, sectors, writable), CHS translation, "
        "the task file with status / error / device control bits decoded, the command in progress (name, phase, LBA, "
        "sectors left, buffer position), and on a CD drive the disc, unit attention, byte count limit, sense key / ASC / "
        "ASCQ and the last packet. 404 on machines without an IDE board.";
    paths["/api/v1/emulator/{id}/state/ide"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/ide"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/ide"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/ide"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/ide"]["get"]["responses"]["200"]["description"] = "IDE board state";
    paths["/api/v1/emulator/{id}/state/ide"]["get"]["responses"]["404"]["description"] = "No IDE board on this machine";

    paths["/api/v1/emulator/{id}/state/tsconf"]["get"]["summary"] = "Get TS-Conf machine state";
    paths["/api/v1/emulator/{id}/state/tsconf"]["get"]["tags"].append("Machine State");
    paths["/api/v1/emulator/{id}/state/tsconf"]["get"]["description"] =
        "The TS-Conf machine (model TSL): build (the emulated firmware: vdac, vdac_ver = STATUS VDAC_VER, blt2; "
        "[MISC] TS_VDAC), memory (MEM_CONFIG decoded, the four window pages, LCK128 mode, lock48, DOS, "
        "virtual TR-DOS, cache, the FM window), video (V_CONFIG decoded: mode ZX / 16C / 256C / TXT, geometry, NOGFX / "
        "NOTSU / GFXOVR; V_PAGE, PAL_SEL, BORDER, offsets; the TSU enables, pages and active sprite count; the raster "
        "line the engine is on with its latched registers and DRAM use), interrupts (mask, pending sources, frame "
        "position), DMA (busy, task, live byte addresses, block counters), the CPU clock and the SD card. 404 on other "
        "machines.";
    paths["/api/v1/emulator/{id}/state/tsconf"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/tsconf"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/tsconf"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/tsconf"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/tsconf"]["get"]["responses"]["200"]["description"] = "TS-Conf machine state";
    paths["/api/v1/emulator/{id}/state/tsconf"]["get"]["responses"]["404"]["description"] = "Not a TS-Conf machine";

    paths["/api/v1/emulator/{id}/state/tsconf/tsu"]["get"]["summary"] = "Get TS-Conf TSU objects and palette";
    paths["/api/v1/emulator/{id}/state/tsconf/tsu"]["get"]["tags"].append("Machine State");
    paths["/api/v1/emulator/{id}/state/tsconf/tsu"]["get"]["description"] =
        "What a TSU / palette debug view needs: t_config, sprites_on, window_360, tilemap_page, sprite_page; "
        "tile_layers[] (name t0 / t1, enabled, draw_tile_zero, graphics_page, x_offset, y_offset, palette = the CRAM "
        "bank bits 7:6, map_offset in each 256-byte map row); sprites[] - all 85 SFILE descriptors (index, active, "
        "leap, layer s0 / s1 / s2 by the LEAP marks, x, y, width, height, x_flip, y_flip, tile, bitmap_x, bitmap_y "
        "in the 512x512 sheet at sprite_page, palette = CRAM bank bits 7:4, words[] raw hex); active_sprites; cram[] - "
        "the 256 palette cells (index, value hex, rgb). The tilemap and the graphics sheets are RAM pages (memory "
        "API); which pixels an object feeds: /video/address. 404 on other machines.";
    paths["/api/v1/emulator/{id}/state/tsconf/tsu"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/tsconf/tsu"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/tsconf/tsu"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/tsconf/tsu"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/tsconf/tsu"]["get"]["responses"]["200"]["description"] = "TSU objects and CRAM";
    paths["/api/v1/emulator/{id}/state/tsconf/tsu"]["get"]["responses"]["404"]["description"] = "Not a TS-Conf machine";

    // Sprinter Sp2000 (core DeviceState::Sprinter / SprinterPortTable / SprinterPortLookup: the same reports
    // CLI `state sprinter`, Lua / Python sprinter_state() / sprinter_ports() / sprinter_port_lookup() and the
    // MCP inspect_state aspect `sprinter` render)
    {
        const char* kState = "/api/v1/emulator/{id}/state/sprinter";
        paths[kState]["get"]["summary"] = "Get Sprinter machine state";
        paths[kState]["get"]["tags"].append("Machine State");
        paths[kState]["get"]["description"] =
            "The Peters Plus Sprinter Sp2000 (model SPRINTER): pld (configuration state unconfigured / loading / "
            "configured, the active configuration module, bitstream writes and hashes - full FNV-1a and MAME's head "
            "hash, fast_start, whether the BIOS opened the port decoder), decoder (CNF map 0-3, clean rules, DOS, PN5, "
            "#7FFD / #1FFD after the clean rules), windows[] (per 16K window: kind ROM / loader ROM / fast RAM / vROM "
            "/ RAM / graphics / ISA / port table, the physical page, writable, the cell that names it), registers "
            "(ROM_RG, SYS_PG, ALL_MODE decoded, PORT_Y, RGMOD, HOLD, SCALE, CBL control), cells (#C0-#FF as hex "
            "rows), clock (turbo requested, front-panel switch, ratio 1 or 6, MHz), frame (320 / 312 lines, base "
            "T-states 71680 / 69888), video (mode page, the picture's dominant square kind, square counts by kind, "
            "HOLD offsets, frame INT positions from the mode table), z84c15 (WCR / MWBR / CSBR / MCR, watchdog, CTC "
            "channels, SIO A / B with the keyboard FIFO, PIO), fdc (the #BD density latch, the WD1793 clock and data "
            "rate, drive), cmos (DS12887A; the full report is /state/rtc), ide (a placeholder until the IDE adapter of "
            "phase S3b) and bios (the ROM file, the pages identified by signature, the shipped images and how to pick "
            "one: [ROM] SPRINTER=). 404 on other machines.";
        paths[kState]["get"]["parameters"][0]["name"] = "id";
        paths[kState]["get"]["parameters"][0]["in"] = "path";
        paths[kState]["get"]["parameters"][0]["required"] = true;
        paths[kState]["get"]["parameters"][0]["schema"]["type"] = "string";
        paths[kState]["get"]["responses"]["200"]["description"] = "Sprinter machine state";
        paths[kState]["get"]["responses"]["404"]["description"] = "Emulator not found, or not a Sprinter";

        const char* kActive = "/api/v1/emulator/state/sprinter";
        paths[kActive]["get"]["summary"] = "Get Sprinter machine state (active emulator)";
        paths[kActive]["get"]["tags"].append("Machine State");
        paths[kActive]["get"]["description"] = "Same as /api/v1/emulator/{id}/state/sprinter for the only or selected emulator.";
        paths[kActive]["get"]["responses"]["200"]["description"] = "Sprinter machine state";
        paths[kActive]["get"]["responses"]["400"]["description"] = "Several emulators running: use the {id} form";
        paths[kActive]["get"]["responses"]["404"]["description"] = "No emulator, or not a Sprinter";

        auto addQuery = [&](const char* path, Json::ArrayIndex index, const char* name, const char* description) {
            Json::Value& queryParam = paths[path]["get"]["parameters"][index];
            queryParam["name"] = name;
            queryParam["in"] = "query";
            queryParam["required"] = false;
            queryParam["description"] = description;
            queryParam["schema"]["type"] = "string";
        };

        const char* kPorts = "/api/v1/emulator/{id}/state/sprinter/ports";
        paths[kPorts]["get"]["summary"] = "Get the Sprinter port table";
        paths[kPorts]["get"]["tags"].append("Machine State");
        paths[kPorts]["get"]["description"] =
            "The port table the BIOS wrote to RAM page #40, decoded for one map / DOS / PN5 (omitted = the machine's "
            "current values) and direction: rows[] of code, name (the port trace's code names), direction r / w, the "
            "address pattern A15..A0 (x = not decoded), an example port and the number of address combinations; "
            "unmapped_combinations (code #00: no device, reads #FF) and z84c15_ports[] (the ports the CPU chip answers "
            "itself, which the table never sees). Index layout: map << 12 | PN5 << 11 | /DOS << 10 | /WR << 9 | A15 "
            "A14 A6 A5 A13 A7 A2 A1 A0. 404 on other machines.";
        paths[kPorts]["get"]["parameters"][0]["name"] = "id";
        paths[kPorts]["get"]["parameters"][0]["in"] = "path";
        paths[kPorts]["get"]["parameters"][0]["required"] = true;
        paths[kPorts]["get"]["parameters"][0]["schema"]["type"] = "string";
        addQuery(kPorts, 1, "map", "Port map 0-3 (CNF bits 4-3); default: the current one");
        addQuery(kPorts, 2, "dos", "1 = TR-DOS on, 0 = off; default: the current state");
        addQuery(kPorts, 3, "pn5", "#7FFD bit 5 (48K lock): 0 / 1; default: the current value");
        addQuery(kPorts, 4, "rw", "r (IN), w (OUT) or rw (both, the default)");
        paths[kPorts]["get"]["responses"]["200"]["description"] = "Decoded port table";
        paths[kPorts]["get"]["responses"]["400"]["description"] = "Bad map / dos / pn5 / rw";
        paths[kPorts]["get"]["responses"]["404"]["description"] = "Emulator not found, or not a Sprinter";

        const char* kLookup = "/api/v1/emulator/{id}/state/sprinter/ports/lookup";
        paths[kLookup]["get"]["summary"] = "Look up one Sprinter port";
        paths[kLookup]["get"]["tags"].append("Machine State");
        paths[kLookup]["get"]["description"] =
            "One port through the port table: results[] per direction with answered_by (PLD or Z84C15), the index "
            "into page #40 and within the map, the 9 address bits, the code and its name, and the fixed decodes the "
            "PLD makes before the table (#FB / #7B, #3C / #7C, #5C). A Z84C15 port gives the chip's trace code "
            "(#100 + low byte); for a write also the PLD's table code (the PLD sees the chip's writes). map / dos / "
            "pn5 default to the machine's current state. 404 on other machines.";
        paths[kLookup]["get"]["parameters"][0]["name"] = "id";
        paths[kLookup]["get"]["parameters"][0]["in"] = "path";
        paths[kLookup]["get"]["parameters"][0]["required"] = true;
        paths[kLookup]["get"]["parameters"][0]["schema"]["type"] = "string";
        addQuery(kLookup, 1, "port", "The 16-bit port, hex: 21BC, #21BC or 0x21BC (required)");
        addQuery(kLookup, 2, "rw", "r, w or rw (both, the default)");
        addQuery(kLookup, 3, "map", "Port map 0-3; default: the current one");
        addQuery(kLookup, 4, "dos", "1 = TR-DOS on, 0 = off; default: the current state");
        addQuery(kLookup, 5, "pn5", "#7FFD bit 5: 0 / 1; default: the current value");
        paths[kLookup]["get"]["responses"]["200"]["description"] = "The port's index, code and name";
        paths[kLookup]["get"]["responses"]["400"]["description"] = "Missing or bad port, bad map / dos / pn5 / rw";
        paths[kLookup]["get"]["responses"]["404"]["description"] = "Emulator not found, or not a Sprinter";

        const char* kText = "/api/v1/emulator/{id}/state/sprinter/text";
        paths[kText]["get"]["summary"] = "Get the Sprinter screen text";
        paths[kText]["get"]["tags"].append("Machine State");
        paths[kText]["get"]["description"] =
            "The text of the picture's text squares from the video RAM mode table (the displayed mode page, 80 x 32: "
            "BIOS SETUP, DSS, its programs' text screens): lines[] of row, text (trailing spaces trimmed; '.' for "
            "codes outside #20-#7E) and codes (hex, two per square); text_squares counts the text squares. Graphics, "
            "border and blank squares read as spaces; a 40-column square gives its character and a space. The ZX "
            "screen OCR (/capture/ocr) reads Spectrum mode only. 404 on other machines.";
        paths[kText]["get"]["parameters"][0]["name"] = "id";
        paths[kText]["get"]["parameters"][0]["in"] = "path";
        paths[kText]["get"]["parameters"][0]["required"] = true;
        paths[kText]["get"]["parameters"][0]["schema"]["type"] = "string";
        paths[kText]["get"]["responses"]["200"]["description"] = "Screen text";
        paths[kText]["get"]["responses"]["404"]["description"] = "Emulator not found, or not a Sprinter";
    }

    paths["/api/v1/emulator/{id}/state/contention"]["get"]["summary"] = "Get memory contention state";
    paths["/api/v1/emulator/{id}/state/contention"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/state/contention"]["get"]["description"] =
        "Where the CPU waits for the video logic: the machine's rule (none, ula48, ula128, gatearray), whether it applies, the 'contention' feature switch and whether contention is in effect, the selected memory interface (fast, debug, fast_contended, debug_contended), the I/O contention rule, per 16K slot its mapping and whether accesses there wait, the +2A/+3 floating-bus latch, whether opcode fetches from RAM wait for an even T-state (even_m1, the Scorpion's Even M1), on the Scorpion its Turbo+ logic firmware (scorpion_turbo_logic: SC15.1 or SC15.3, [MISC] ScorpionTurboLogic), and - while debug mode is on - contended accesses and wait T-states per kind (fetch, read, write, io) for the current frame, the last frame and in total.";
    paths["/api/v1/emulator/{id}/state/contention"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/contention"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/contention"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/contention"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/contention"]["get"]["responses"]["200"]["description"] = "Memory contention state";


    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["summary"] =
        "Get AY chip register details";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["schema"]["type"] =
        "string";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["name"] = "chip";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["schema"]["type"] =
        "integer";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][2]["name"] = "reg";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][2]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][2]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][2]["description"] =
        "Register number (0-15)";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][2]["schema"]["type"] =
        "integer";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["responses"]["200"]["description"] =
        "Register details";

    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["summary"] = "Get beeper state";
    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["responses"]["200"]["description"] = "Beeper state";

    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["summary"] = "Get General Sound state";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["description"] =
        "General Sound card (classic GS, lightweight or NeoGS; [SOUND] GSType=Z80, LW or NGS). One report for every "
        "interface (DeviceState::Gs; CLI state audio gs, Lua/Python gs_state, MCP audio_gs): device, implementation, "
        "mailbox status with decoded flags, host command/data bytes, MPAG banking latch, per-channel DAC sample "
        "and 6-bit volume, firmware/RAM fitment and the coprocessor PC/SP/AF/halted state. On NeoGS a 'neogs' "
        "object adds the card's registers, SD card, MP3 decoder and DMA modules; 'neogs.dma.zx' is the host's "
        "view of ZX-DMA: mode (off | watch | divert), overlay_installed, read_latch (the byte the next host read "
        "gets), pending (none | read | write) and pending_address, bytes_read / bytes_written / bytes_dropped, "
        "wait_tstates, late_starts and late_start_ticks (starts no watch window saw, in 120 MHz card ticks), "
        "watch_setting (selected | always), watch_frames and watch_frames_left (-1: closed). 404 when the "
        "machine has no GS card fitted.";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][1]["name"] = "ram";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][1]["description"] =
        "1: add fixed_window_hex, the card CPU's #4000-#7FFF window (peeked, no side effects)";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["responses"]["200"]["description"] = "GS state";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["responses"]["404"]["description"] =
        "Emulator or GS card not found";

    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["summary"] = "General Sound control actions";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["description"] =
        "Drive the GS card the way ZX software does (each action flushes the coprocessor to the current ZX tact "
        "first): reset (full power-on), reset_card (#33 bit7 - CPU/banking/timing, mailbox survives), nmi (#33 "
        "bit6), send_command (OUT #BB), send_data (OUT #B3), read_status (IN #BB, returns the value), read_data "
        "(IN #B3, clears the data-pending flag and returns the value), switch_personality (runtime swap of the "
        "GS-slot card: classic GS, lightweight player or NeoGS - the host mailbox and activity counters survive "
        "the handoff and an uploaded module is replayed; applied at the next frame boundary), dump_module "
        "(writes the last completed COM30..D2 module upload to a file - 'path' optional, defaults to "
        "'gs-module-dump.mod'; 404 when no upload has completed yet). NeoGS only (409 otherwise): sd_insert "
        "(needs 'path' to a raw SD image), sd_eject, flash_save (saves the reprogrammed flash); carried out on the "
        "machine thread at the next instruction boundary ('status': 'queued' while the emulator runs, 'done' "
        "otherwise); sd_insert/sd_eject are refused while TTD records (the configuration is fixed for a recording). "
        "stereo_mode ('mode': separated | gs | mono) sets how the NeoGS DAC channels reach the two sides: separated as on "
        "the board, gs a 50% cross-feed like the classic GS, mono; kept by the emulator and applied at the next frame.";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["type"] = "object";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["type"] = "string";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("reset");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("reset_card");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("nmi");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("send_command");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("send_data");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("read_status");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("read_data");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["value"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("switch_personality");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["personality"]["type"] = "string";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["personality"]["enum"].append("z80");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["personality"]["enum"].append("lw");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["personality"]["enum"].append("ngs");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("sd_insert");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("sd_eject");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("flash_save");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("stereo_mode");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["mode"]["type"] = "string";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["mode"]["enum"].append("separated");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["mode"]["enum"].append("gs");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["mode"]["enum"].append("mono");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["action"]["enum"].append("dump_module");
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["requestBody"]["content"]["application/json"]["schema"]
                                                               ["properties"]["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["responses"]["200"]["description"] =
        "Action result (read_status/read_data carry the byte in 'value'; switch_personality carries the requested "
        "target and the current personality; dump_module carries 'path', 'bytes' and 'playing')";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["responses"]["400"]["description"] =
        "Unknown action, invalid value or unknown personality";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["responses"]["404"]["description"] =
        "Emulator or GS card not found, or (dump_module) no completed module upload captured yet";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["responses"]["409"]["description"] =
        "sd_insert / sd_eject / flash_save on a card that is not NeoGS, sd_insert / sd_eject while TTD records, "
        "switch_personality while a TTD recording runs (the message says why: stop the recording first), "
        "or any of them while a TTD replay owns the machine";
    paths["/api/v1/emulator/{id}/control/audio/gs"]["post"]["responses"]["422"]["description"] =
        "sd_insert: the image cannot be opened; flash_save: the flash could not be saved";

    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["summary"] = "Get Covox / SoundDrive state";
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["description"] =
        "Covox or SoundDrive DAC. One report for every interface (DeviceState::Covox; CLI state audio covox, "
        "Lua/Python audio_covox_state, MCP audio_covox): fitment (mono #FB or quad SoundDrive), the ports this "
        "model's decoder routes to it (from its port map: port, mask, match, decode, gate), shared_with_beta128 "
        "(Beta-128 registers a DAC row also decodes, with shared_port_rule), the four DAC latches with mute state, "
        "the last left/right amplitude, sound_last_frame, dc_removal and synthesis_suppressed.";
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["responses"]["200"]["description"] = "Covox state";
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["responses"]["404"]["description"] =
        "Emulator not found, or no Covox fitted ([SOUND] CovoxFB / SD)";

    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["summary"] = "Get audio channels overview";
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["responses"]["200"]["description"] =
        "Audio channels information";

    // Active emulator endpoints (no ID required)
    paths["/api/v1/emulator/state/audio/fm"]["get"]["summary"] = "Get TurboSound FM overview (active emulator)";
    paths["/api/v1/emulator/state/audio/fm"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/fm"]["get"]["responses"]["200"]["description"] = "TurboSound FM overview";

    paths["/api/v1/emulator/state/audio/fm/{chip}"]["get"]["summary"] = "Get TurboSound FM chip details (active emulator)";
    paths["/api/v1/emulator/state/audio/fm/{chip}"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/fm/{chip}"]["get"]["parameters"][0]["name"] = "chip";
    paths["/api/v1/emulator/state/audio/fm/{chip}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/state/audio/fm/{chip}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/state/audio/fm/{chip}"]["get"]["parameters"][0]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/state/audio/fm/{chip}"]["get"]["responses"]["200"]["description"] = "TurboSound FM chip details";

    paths["/api/v1/emulator/state/fdc"]["get"]["summary"] = "Get Beta Disk WD1793 state (active emulator)";
    paths["/api/v1/emulator/state/fdc"]["get"]["tags"].append("Disk State (Active)");
    paths["/api/v1/emulator/state/fdc"]["get"]["responses"]["200"]["description"] = "WD1793 state";

    paths["/api/v1/emulator/state/contention"]["get"]["summary"] = "Get memory contention state (active emulator)";
    paths["/api/v1/emulator/state/contention"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/state/contention"]["get"]["responses"]["200"]["description"] = "Memory contention state";

    paths["/api/v1/emulator/state/audio/ay"]["get"]["summary"] = "Get AY chips overview (active emulator)";
    paths["/api/v1/emulator/state/audio/ay"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/ay"]["get"]["responses"]["200"]["description"] = "AY chips information";

    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["summary"] =
        "Get specific AY chip details (active emulator)";
    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["parameters"][0]["name"] = "chip";
    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["parameters"][0]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["responses"]["200"]["description"] = "AY chip details";

    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["summary"] =
        "Get AY chip register details (active emulator)";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["name"] = "chip";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["schema"]["type"] =
        "integer";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["name"] = "reg";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["description"] =
        "Register number (0-15)";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["schema"]["type"] =
        "integer";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["responses"]["200"]["description"] =
        "Register details";

    paths["/api/v1/emulator/state/audio/beeper"]["get"]["summary"] = "Get beeper state (active emulator)";
    paths["/api/v1/emulator/state/audio/beeper"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/beeper"]["get"]["responses"]["200"]["description"] = "Beeper state";

    paths["/api/v1/emulator/state/audio/gs"]["get"]["summary"] = "Get GS state (active emulator)";
    paths["/api/v1/emulator/state/audio/gs"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/gs"]["get"]["responses"]["200"]["description"] = "GS state";

    paths["/api/v1/emulator/state/audio/covox"]["get"]["summary"] = "Get Covox state (active emulator)";
    paths["/api/v1/emulator/state/audio/covox"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/covox"]["get"]["responses"]["200"]["description"] = "Covox state";

    paths["/api/v1/emulator/state/audio/channels"]["get"]["summary"] = "Get audio channels (active emulator)";
    paths["/api/v1/emulator/state/audio/channels"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/channels"]["get"]["responses"]["200"]["description"] =
        "Audio channels information";


    // ==========================================
    // Memory Read/Write Operations
    // ==========================================

    // GET /api/v1/emulator/{id}/memory/read/{address} - Read memory at address
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["summary"] = "Read memory at address";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["description"] =
        "Read one or more bytes from the Z80 address space.";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][1]["name"] = "address";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][1]["description"] = "Z80 address (0-65535)";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][2]["name"] = "length";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][2]["description"] =
        "Number of bytes to read (default: 128)";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][3]["name"] = "format";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][3]["in"] = "query";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][3]["required"] = false;
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][3]["description"] =
        "'hexdump' (default), 'full' (data array) or 'sparse' (fill-run segments)";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["parameters"][3]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["responses"]["200"]["description"] = "Memory contents";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["responses"]["400"]["description"] = "Invalid address";
    paths["/api/v1/emulator/{id}/memory/read/{address}"]["get"]["responses"]["404"]["description"] = "Emulator not found";

    // POST /api/v1/emulator/{id}/memory/write - Write memory
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["summary"] = "Write memory";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["description"] =
        "Write one or more bytes to the Z80 address space.";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["address"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["address"]["description"] = "Z80 start address (0-65535)";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["data"]["type"] = "array";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["data"]["items"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["responses"]["200"]["description"] = "Memory written";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["responses"]["400"]["description"] = "Invalid address or data";
    paths["/api/v1/emulator/{id}/memory/write"]["post"]["responses"]["404"]["description"] = "Emulator not found";

    // GET/POST /api/v1/emulator/{id}/memory/page/{type}/{page} - Read/Write physical memory page
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["summary"] = "Read physical memory page";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["description"] =
        "Read an entire 16KB physical memory page by type (ram/rom) and page number.";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][1]["name"] = "type";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][1]["description"] = "Memory type: 'ram' or 'rom'";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][2]["name"] = "page";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][2]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][2]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][3]["name"] = "filter";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][3]["in"] = "query";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][3]["required"] = false;
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][3]["description"] =
        "Set 'sparse' to compress 0x00/0xFF runs into fill segments (much smaller than the byte array)";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["parameters"][3]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["responses"]["200"]["description"] = "Page data (base64 encoded)";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["responses"]["400"]["description"] = "Invalid type or page number";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["get"]["responses"]["404"]["description"] = "Emulator not found";

    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["summary"] = "Write physical memory page";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["description"] =
        "Write an entire 16KB physical memory page by type and page number.";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][1]["name"] = "type";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][2]["name"] = "page";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][2]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][2]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["responses"]["200"]["description"] = "Page written";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["responses"]["400"]["description"] = "Invalid type, page, or data";
    paths["/api/v1/emulator/{id}/memory/page/{type}/{page}"]["post"]["responses"]["404"]["description"] = "Emulator not found";

    // GET/PUT /api/v1/emulator/{id}/memory/rom/protect - ROM protection control
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["get"]["summary"] = "Get ROM write protection status";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["get"]["description"] =
        "Get whether ROM write protection is enabled.";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["get"]["responses"]["200"]["description"] = "ROM protection status";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["get"]["responses"]["404"]["description"] = "Emulator not found";

    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["summary"] = "Set ROM write protection";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["description"] =
        "Enable or disable ROM write protection.";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["protected"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["responses"]["200"]["description"] = "Protection state updated";
    paths["/api/v1/emulator/{id}/memory/rom/protect"]["put"]["responses"]["404"]["description"] = "Emulator not found";

