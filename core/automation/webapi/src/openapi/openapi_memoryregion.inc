    // Device memory regions (core emulator/memory/devicememory.h, DeviceState::MemoryRegions /
    // MemoryRegionRead; the same functions behind CLI `memory region`, Lua / Python memory_regions /
    // region_read / region_write / region_save / region_load and the MCP aspect memory_region)
    {
        auto idParam = [&](const char* path, const char* method) {
            Json::Value& p = paths[path][method]["parameters"][0];
            p["name"] = "id";
            p["in"] = "path";
            p["required"] = true;
            p["schema"]["type"] = "string";
        };

        const char* kList = "/api/v1/emulator/{id}/memory/regions";
        paths[kList]["get"]["summary"] = "List device memory regions";
        paths[kList]["get"]["tags"].append("Memory State");
        paths[kList]["get"]["description"] =
            "Memory a device owns outside the CPU's RAM / ROM pages, by name: regions[] of name, description, size, "
            "size_hex, page_size, pages, writable and write_path (how a write reaches the device). The Sprinter has "
            "'vram' (256 KB video RAM: row x 1024 + column; mode table at columns #300-#39F, palettes at #3E0-#3FF, "
            "R, G, B per pen); TS-Conf has 'cram' (palette) and 'sfile' (sprite table), 512 bytes each: word n at "
            "offset 2n, low byte first, the FM window's layout (CRAM: bits 14-10 R, 9-5 G, 4-0 B, bit 15 VDAC; sprite "
            "d = SFILE words 3d..3d+2). Every machine with a CMOS clock has 'cmos' (the cells as the guest addresses "
            "them; a write is a guest write, the time registers set the clock), the ZX-Evo also 'eeprom' (the AVR's "
            "4 KiB EEPROM).";
        idParam(kList, "get");
        paths[kList]["get"]["responses"]["200"]["description"] = "Regions (an empty list on machines without any)";
        paths[kList]["get"]["responses"]["404"]["description"] = "Emulator not found";

        const char* kRegion = "/api/v1/emulator/{id}/memory/region/{name}";
        paths[kRegion]["get"]["summary"] = "Read a device memory region";
        paths[kRegion]["get"]["tags"].append("Memory State");
        paths[kRegion]["get"]["description"] =
            "Bytes [offset, offset + length) of the region: format=hex (default; one hex string), data (an array), "
            "sparse (0x00 / 0xFF runs folded into fill segments) - at most 65536 bytes as JSON - or binary "
            "(application/octet-stream, any length; headers X-Region, X-Offset and X-Unreal-Space / -Address / "
            "-Length as every binary memory read). Pages of the same bytes: "
            "/memory/page/{name}/{n} (16 KB pages, the ram / rom page shape).";
        idParam(kRegion, "get");
        {
            Json::Value& p = paths[kRegion]["get"]["parameters"];
            p[1]["name"] = "name";
            p[1]["in"] = "path";
            p[1]["required"] = true;
            p[1]["schema"]["type"] = "string";
            p[1]["description"] = "Region name (GET /memory/regions), e.g. vram";
            p[2]["name"] = "offset";
            p[2]["in"] = "query";
            p[2]["required"] = false;
            p[2]["schema"]["type"] = "string";
            p[2]["description"] = "Start: decimal, 0x or # hex (default 0)";
            p[3]["name"] = "length";
            p[3]["in"] = "query";
            p[3]["required"] = false;
            p[3]["schema"]["type"] = "string";
            p[3]["description"] = "Byte count (default 256)";
            p[4]["name"] = "format";
            p[4]["in"] = "query";
            p[4]["required"] = false;
            p[4]["schema"]["type"] = "string";
            p[4]["description"] = "hex (default), data, sparse or binary";
        }
        paths[kRegion]["get"]["responses"]["200"]["description"] = "The bytes";
        paths[kRegion]["get"]["responses"]["400"]["description"] = "Range outside the region, bad format";
        paths[kRegion]["get"]["responses"]["404"]["description"] = "Emulator or region not found";

        paths[kRegion]["post"]["summary"] = "Write, save or load a device memory region";
        paths[kRegion]["post"]["tags"].append("Memory State");
        paths[kRegion]["post"]["description"] =
            "Default action write: {\"offset\": \"0x17F0\", \"hex\": \"0000A8\"} or {\"offset\": 6128, \"data\": [0, 0, "
            "168]} - through the device's own write path (Sprinter: SprinterVideoRam::Write, so a palette byte "
            "refreshes its pen and a mode byte moves the frame INT), marked as a tool edit for TTD. {\"action\": "
            "\"save\", \"path\": \"vram.bin\", \"offset\": 0, \"length\": 0} writes the bytes to a file (length 0 = "
            "to the end); {\"action\": \"load\", \"path\": \"vram.bin\", \"offset\": 0} writes a file in (clipped at "
            "the end of the region).";
        idParam(kRegion, "post");
        paths[kRegion]["post"]["parameters"][1]["name"] = "name";
        paths[kRegion]["post"]["parameters"][1]["in"] = "path";
        paths[kRegion]["post"]["parameters"][1]["required"] = true;
        paths[kRegion]["post"]["parameters"][1]["schema"]["type"] = "string";
        paths[kRegion]["post"]["requestBody"]["required"] = true;
        paths[kRegion]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
        paths[kRegion]["post"]["responses"]["200"]["description"] = "Written / saved / loaded";
        paths[kRegion]["post"]["responses"]["400"]["description"] = "Bad body, range outside the region, file error";
        paths[kRegion]["post"]["responses"]["404"]["description"] = "Emulator or region not found";
    }
