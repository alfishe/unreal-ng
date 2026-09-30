    // Video debug translation (PLAN #42; docs/inprogress/2026-09-27-video-debug-translation): one description
    // of the picture per video family (VideoMapService), shared by CLI `video ...`, Lua / Python video_*()
    // and the MCP aspects video_layout / video_text. Coordinates: surface pixels of a layer (text layers too:
    // 640x200 for 80x25 cells); beam T-states use the renderer origin (T 0 = first T of the left border).
    // Sources are physical: space (ram, palette, register, internal_table), page, offset, bit_mask, role,
    // plus the Z80 addresses the byte is visible at under current paging.
    {
        auto idParam = [](Json::Value& op) {
            Json::Value p;
            p["name"] = "id";
            p["in"] = "path";
            p["required"] = true;
            p["schema"]["type"] = "string";
            op["parameters"].append(p);
        };
        auto queryParam = [](Json::Value& op, const char* name, const char* type, const char* description) {
            Json::Value p;
            p["name"] = name;
            p["in"] = "query";
            p["required"] = false;
            p["schema"]["type"] = type;
            p["description"] = description;
            op["parameters"].append(p);
        };

        Json::Value& layout = paths["/api/v1/emulator/{id}/video/layout"]["get"];
        layout["summary"] = "Video layout of the current mode";
        layout["tags"].append("Screen State");
        layout["description"] =
            "mapped (false when no mapper describes the mode), family (zx, alco, atm, profi, none), video_mode, "
            "tstates_per_line, lines, layers[] (id, surface{width, height, bits_per_pixel, text_columns, text_rows, "
            "cell_width, cell_height}, window{first_line, line_count, first_t, t_count, dots_per_t}) and "
            "framebuffer{width, height, surface_left, surface_top}: surface pixel (x, y) is framebuffer pixel "
            "(surface_left + x, surface_top + y).";
        idParam(layout);
        layout["responses"]["200"]["description"] = "Video layout";
        layout["responses"]["404"]["description"] = "Emulator not found";

        Json::Value& pixel = paths["/api/v1/emulator/{id}/video/pixel"]["get"];
        pixel["summary"] = "What makes a pixel";
        pixel["tags"].append("Screen State");
        pixel["description"] =
            "The memory, registers and palette cell behind a pixel: layer, x, y, sources[] (space, page, offset, "
            "width, bit_mask, role, z80[]), colour_index, rgb, rendered_rgb (what the framebuffer holds), state_at "
            "and values_at (current). With t= the point under the beam at that frame T: a layer pixel, or the "
            "border (border: true, with its register and palette sources). available: false when the point has "
            "neither.";
        idParam(pixel);
        queryParam(pixel, "x", "integer", "Surface x of the layer (with y)");
        queryParam(pixel, "y", "integer", "Surface y of the layer (with x)");
        queryParam(pixel, "layer", "integer", "Layer index, default 0");
        queryParam(pixel, "t", "integer", "T-state in the frame instead of x / y");
        pixel["responses"]["200"]["description"] = "Pixel sources";
        pixel["responses"]["400"]["description"] = "Neither x and y nor t given";
        pixel["responses"]["404"]["description"] = "Emulator not found";

        Json::Value& address = paths["/api/v1/emulator/{id}/video/address"]["get"];
        address["summary"] = "Which pixels a byte feeds";
        address["tags"].append("Screen State");
        address["description"] =
            "areas[] (layer, x, y, width, height) of every layer the byte feeds in the current mode, and "
            "feeds_picture. Either a physical RAM byte (page + offset 0..0x3FFF) or a Z80 address resolved through "
            "the current paging (ROM and non-screen memory feed nothing). Numbers are decimal or 0x-hex.";
        idParam(address);
        queryParam(address, "page", "integer", "RAM page (with offset)");
        queryParam(address, "offset", "integer", "Offset in the page, 0..0x3FFF (with page)");
        queryParam(address, "z80", "integer", "Z80 address instead of page / offset");
        address["responses"]["200"]["description"] = "Areas the byte feeds";
        address["responses"]["400"]["description"] = "Neither page and offset nor z80 given";
        address["responses"]["404"]["description"] = "Emulator not found";

        Json::Value& text = paths["/api/v1/emulator/{id}/video/text"]["get"];
        text["summary"] = "Text grid of a text mode";
        text["tags"].append("Screen State");
        text["description"] =
            "Exact text of a text layer (ATM Turbo 2+ ATMTX, ZX-Evo ATMTL): layer, columns, rows and lines[] with "
            "text (printable characters, '.' for other codes), codes and attrs (hex, two digits per cell). "
            "available: false in bitmap modes - read those with the screen OCR (/capture/ocr).";
        idParam(text);
        queryParam(text, "layer", "integer", "Layer index, default 0");
        text["responses"]["200"]["description"] = "Text grid";
        text["responses"]["404"]["description"] = "Emulator not found";
    }
