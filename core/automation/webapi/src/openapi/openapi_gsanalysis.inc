    // The GS card CPU analysis (gsanalysisreport.h, gs-debugger P5.6; requirements §4.13 X8 / X10 / X11): coverage /
    // CDL, the RAM heat map, the reads of never-written RAM, and its switch. CLI 'gs analysis|coverage|heatmap', Lua /
    // Python gs_analysis / gs_coverage / gs_heatmap, MCP gs_analysis. Handlers: api/gs_analysis_api.cpp
    {
        auto idParameter = [](Json::Value& operation) {
            operation["parameters"][0]["name"] = "id";
            operation["parameters"][0]["in"] = "path";
            operation["parameters"][0]["required"] = true;
            operation["parameters"][0]["schema"]["type"] = "string";
        };
        auto query = [](Json::Value& operation, const char* name, const char* type, const std::string& description) {
            Json::Value& param = operation["parameters"][operation["parameters"].size()];
            param["name"] = name;
            param["in"] = "query";
            param["required"] = false;
            param["schema"]["type"] = type;
            param["description"] = description;
        };
        const std::string cpuText = "The card CPU: gs (default, the GS slot's card) or ms (the ZX-MultiSound's GS)";

        Json::Value& status = paths["/api/v1/emulator/{id}/state/audio/gs/analysis"]["get"];
        status["summary"] = "GS card CPU analysis: state";
        status["description"] =
            "Whether the coverage / heat-map analysis of the card CPU is on and bound to the card's run, its memory, "
            "the heat-map cell size and decay, the card resets seen. Off by default: POST control/audio/gs/analysis "
            "{action: on}. Not available on the lightweight GS (no card CPU)";
        status["tags"].append("Audio State");
        idParameter(status);
        query(status, "cpu", "string", cpuText);
        status["responses"]["200"]["description"] =
            "{available, card, cpu, enabled, recording, allocated, memory_bytes, rom_bytes, ram_bytes, cell_bytes, "
            "cells, decay, frame, created_frame, card_resets, last_reset_frame, text}";
        status["responses"]["404"]["description"] = "Emulator not found, no card fitted, or not available (LW)";

        Json::Value& coverage = paths["/api/v1/emulator/{id}/state/audio/gs/coverage"]["get"];
        coverage["summary"] = "GS card CPU coverage / code-data log";
        coverage["description"] =
            "A flag byte per physical byte of the card's ROM (flash on NeoGS) and RAM, OR-merged over every session "
            "since on or reset: exec 1 (instruction start), operand 2, read 4, write 8 (CPU or a DMA master), jump "
            "16 (a jump / interrupt target), sub 32 (a CALL / RST target), dac 64 (fetched by the DAC), written 128 "
            "(RAM written since the last card reset). Per space: totals and the pages with a mark, each with its "
            "counts and code / data / DAC ranges (offsets in the page, ends inclusive). Also the firmware profile's "
            "command handlers that ran (handlers), and the reads of RAM never written since the card reset with the "
            "PC that read (never_written)";
        coverage["tags"].append("Audio State");
        idParameter(coverage);
        query(coverage, "cpu", "string", cpuText);
        query(coverage, "space", "string", "rom (or flash) or ram (default: both)");
        query(coverage, "page", "integer", "One 16 KB page of space (with sub_entries; raw=1 adds its flag bytes)");
        query(coverage, "ranges", "integer", "Ranges listed per page and kind (default 16, 0: none)");
        query(coverage, "raw", "boolean", "With page: its 16,384 flag bytes as base64 (flags)");
        coverage["responses"]["200"]["description"] =
            "{...state, legend, spaces[{space, name, bytes, pages, totals, page_list[{page, exec, code, read, write, "
            "jump, sub, dac, written, code_ranges, data_ranges, dac_ranges}]}], handlers{available, profile, entries, "
            "executed_count, executed[{command, name, handler, space, page}], not_executed}, never_written{count, "
            "dropped, since_frame, rows[{address, space, page, offset, pc, frame, kind}]}, text}";
        coverage["responses"]["400"]["description"] = "A bad parameter";
        coverage["responses"]["404"]["description"] = "Emulator not found, no card fitted, or not available (LW)";

        Json::Value& heat = paths["/api/v1/emulator/{id}/state/audio/gs/heatmap"]["get"];
        heat["summary"] = "GS card RAM heat map";
        heat["description"] =
            "The card RAM in cells (16 bytes up to 1 MB, at most 65,536 cells), one heat byte per cell and channel: "
            "fetch, read, write (CPU), dac (DAC fetch), sd_dma (NeoGS SD / MP3 DMA), zx_dma (NeoGS ZX-DMA). An access "
            "adds bump (32), saturating at 255; every host frame takes decay (default 8) off. The grid is merged to at "
            "most `cells` cells by their maximum (group), `columns` per row; base64 in data, or hex rows. recent: the "
            "RAM ranges each asked channel touched in the last `frames` frames (DAC and write ranges trimmed to the "
            "bytes the CDL marks)";
        heat["tags"].append("Audio State");
        idParameter(heat);
        query(heat, "cpu", "string", cpuText);
        query(heat, "channels", "string", "Comma list of the grid's channels (default all)");
        query(heat, "recent", "string", "Comma list of the channels whose recent ranges are listed (default dac)");
        query(heat, "frames", "integer", "recent: the last N host frames (default 50)");
        query(heat, "cells", "integer", "The grid's cells at most (default 4096, 0: every cell)");
        query(heat, "format", "string", "base64 (default), hex or none");
        heat["responses"]["200"]["description"] =
            "{...state, bump, data_cells, group, columns, format, channels[{name, accesses, hot_cells, max, data | "
            "rows}], recent{frames, channels[{name, ranges[{start, end, length, page, offset}], truncated}]}, text}";
        heat["responses"]["400"]["description"] = "A bad parameter";
        heat["responses"]["404"]["description"] = "Emulator not found, no card fitted, or not available (LW)";

        Json::Value& post = paths["/api/v1/emulator/{id}/control/audio/gs/analysis"]["post"];
        post["summary"] = "GS card CPU analysis: switch";
        post["description"] =
            "on: allocate (once) and mark from the card's next run; off: stop marking, keep the data; reset: clear the "
            "data; release: free it; status (default). decay: the heat lost per frame (0-255). Off costs the card CPU "
            "nothing: the marking bus callbacks are installed only while on";
        post["tags"].append("Audio Control");
        idParameter(post);
        Json::Value& schema = post["requestBody"]["content"]["application/json"]["schema"];
        schema["type"] = "object";
        schema["properties"]["action"]["type"] = "string";
        schema["properties"]["action"]["enum"].append("status");
        schema["properties"]["action"]["enum"].append("on");
        schema["properties"]["action"]["enum"].append("off");
        schema["properties"]["action"]["enum"].append("reset");
        schema["properties"]["action"]["enum"].append("release");
        schema["properties"]["cpu"]["type"] = "string";
        schema["properties"]["cpu"]["description"] = cpuText;
        schema["properties"]["decay"]["type"] = "integer";
        schema["properties"]["decay"]["description"] = "Heat lost per frame, 0-255 (default 8)";
        post["responses"]["200"]["description"] = "The state after the action (as GET state/audio/gs/analysis) + action";
        post["responses"]["400"]["description"] = "A bad action or decay, or not available (LW)";
        post["responses"]["404"]["description"] = "Emulator not found, or no card fitted";
    }
