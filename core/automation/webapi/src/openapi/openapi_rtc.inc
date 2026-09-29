    // CMOS clock (MC146818 / DS12887; the ZX-Evo AVR's emulation of one) - DeviceState::Rtc for the report and
    // RtcAccess for the cells: the same calls CLI `rtc` / `state rtc`, Lua/Python rtc_state() / rtc_read() /
    // rtc_write() and the MCP aspect `rtc` use. Machines: ATM3 (ZX-Evo), PROFI, SCORPION / PROFSCORP with
    // [HDD] Scheme=SMUC. Anything else answers 404 with the reason.
    {
        const char* kReport = "/api/v1/emulator/{id}/state/rtc";
        paths[kReport]["get"]["summary"] = "Get CMOS clock state";
        paths[kReport]["get"]["tags"].append("Clock State");
        paths[kReport]["get"]["description"] =
            "The machine's CMOS clock: chip (part name), ports (how the Z80 reaches it), cells (64 / 128 / 256), "
            "nvram_file, address_latch, time_mode (host = host local time plus the offset the guest set, emulated = "
            "while a TTD session records, fixed = tests), time as the guest reads it now (year / month / day / "
            "hours / minutes / seconds / day_of_week and text), register_a (uip, divider, rate), register_b (set, "
            "interrupt enables, binary, hour_24, daylight_saving), register_c (irq, periodic, alarm, update_ended), "
            "register_d (battery_ok), the alarm registers and a hex dump of every cell. Peeked: reading never clears "
            "register C. 404 with the reason on machines without a clock the guest can reach.";
        paths[kReport]["get"]["parameters"][0]["name"] = "id";
        paths[kReport]["get"]["parameters"][0]["in"] = "path";
        paths[kReport]["get"]["parameters"][0]["required"] = true;
        paths[kReport]["get"]["parameters"][0]["schema"]["type"] = "string";
        paths[kReport]["get"]["responses"]["200"]["description"] = "CMOS clock state";
        paths[kReport]["get"]["responses"]["404"]["description"] = "No CMOS clock on this machine (the reason is in message)";

        const char* kActive = "/api/v1/emulator/state/rtc";
        paths[kActive]["get"]["summary"] = "Get CMOS clock state (active emulator)";
        paths[kActive]["get"]["tags"].append("Clock State");
        paths[kActive]["get"]["description"] = "Same as /api/v1/emulator/{id}/state/rtc for the only or selected emulator.";
        paths[kActive]["get"]["responses"]["200"]["description"] = "CMOS clock state";
        paths[kActive]["get"]["responses"]["400"]["description"] = "Several emulators running: use the {id} form";
        paths[kActive]["get"]["responses"]["404"]["description"] = "No emulator, or no CMOS clock";

        const char* kCells = "/api/v1/emulator/{id}/rtc/cells";
        paths[kCells]["get"]["summary"] = "Read CMOS cells";
        paths[kCells]["get"]["tags"].append("Clock State");
        paths[kCells]["get"]["description"] =
            "Cells as the guest reads them, without side effects (register C keeps its flags). Cell 0x00 seconds, "
            "0x02 minutes, 0x04 hours, 0x06 day of week, 0x07 day, 0x08 month, 0x09 year, 0x0A-0x0D registers A-D, "
            "0x0E and up RAM. Answer: start, count, bytes (array) and hex.";
        paths[kCells]["get"]["parameters"][0]["name"] = "id";
        paths[kCells]["get"]["parameters"][0]["in"] = "path";
        paths[kCells]["get"]["parameters"][0]["required"] = true;
        paths[kCells]["get"]["parameters"][0]["schema"]["type"] = "string";
        paths[kCells]["get"]["parameters"][1]["name"] = "start";
        paths[kCells]["get"]["parameters"][1]["in"] = "query";
        paths[kCells]["get"]["parameters"][1]["description"] = "First cell, decimal or 0x hex (default 0)";
        paths[kCells]["get"]["parameters"][1]["schema"]["type"] = "string";
        paths[kCells]["get"]["parameters"][2]["name"] = "count";
        paths[kCells]["get"]["parameters"][2]["in"] = "query";
        paths[kCells]["get"]["parameters"][2]["description"] = "Number of cells (default: to the last cell)";
        paths[kCells]["get"]["parameters"][2]["schema"]["type"] = "string";
        paths[kCells]["get"]["responses"]["200"]["description"] = "Cells read";
        paths[kCells]["get"]["responses"]["400"]["description"] = "Bad number or a range outside the chip";
        paths[kCells]["get"]["responses"]["404"]["description"] = "Emulator not found, or no CMOS clock";

        paths[kCells]["post"]["summary"] = "Write CMOS cells";
        paths[kCells]["post"]["tags"].append("Clock State");
        paths[kCells]["post"]["description"] =
            "Write cells exactly as a guest write would: writing the time registers sets the clock (use register B "
            "bit 7 SET around a multi-register set, as a guest does), C and D are read-only, RAM cells are stored. "
            "The chip's address latch is not touched. While a TTD session records, the write parks the CPU and is "
            "marked as a debugger edit. Answer: the cells read back after the write.";
        paths[kCells]["post"]["parameters"][0]["name"] = "id";
        paths[kCells]["post"]["parameters"][0]["in"] = "path";
        paths[kCells]["post"]["parameters"][0]["required"] = true;
        paths[kCells]["post"]["parameters"][0]["schema"]["type"] = "string";
        paths[kCells]["post"]["requestBody"]["required"] = true;
        Json::Value& body = paths[kCells]["post"]["requestBody"]["content"]["application/json"]["schema"];
        body["type"] = "object";
        body["required"].append("start");
        body["required"].append("bytes");
        body["properties"]["start"]["description"] = "First cell: integer or \"0x..\" string";
        body["properties"]["bytes"]["type"] = "array";
        body["properties"]["bytes"]["description"] = "Values 0-255: integers or \"0x..\" strings";
        paths[kCells]["post"]["responses"]["200"]["description"] = "Cells written (read back)";
        paths[kCells]["post"]["responses"]["400"]["description"] = "Bad body or a range outside the chip";
        paths[kCells]["post"]["responses"]["404"]["description"] = "Emulator not found, or no CMOS clock";
    }
