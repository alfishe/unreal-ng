    // Analysis layers (analysis::Execute, docs/emulator/analysis/analysis-layers.md): phase P1, the L0 access facts
    {
        const auto idParameter = [](Json::Value& operation) {
            operation["parameters"][0]["name"] = "id";
            operation["parameters"][0]["in"] = "path";
            operation["parameters"][0]["required"] = true;
            operation["parameters"][0]["schema"]["type"] = "string";
        };

        Json::Value& start = paths["/api/v1/emulator/{id}/analysis/start"]["post"];
        start["summary"] = "Start an analysis session (L0 access facts)";
        start["tags"].append("Analyzers");
        start["description"] =
            "Switches on debug mode and memory tracking, sets the memory tracker's level (facts: the counters plus, per "
            "byte, the access kinds, the last reader and writer, the first and last frame and the range of the values "
            "written; counters: the counters only) and its mode (z80: the 64K address space; physical: per physical "
            "page, allocated when a page is first touched), and starts a fresh memory session. Answers the status and "
            "a text line.";
        idParameter(start);
        start["requestBody"]["required"] = false;
        {
            Json::Value& schema = start["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            schema["properties"]["level"]["type"] = "string";
            schema["properties"]["level"]["enum"].append("facts");
            schema["properties"]["level"]["enum"].append("counters");
            schema["properties"]["mode"]["type"] = "string";
            schema["properties"]["mode"]["enum"].append("z80");
            schema["properties"]["mode"]["enum"].append("physical");
        }
        start["responses"]["200"]["description"] = "state, level, mode, frame, facts {allocated, bytes, z80_bytes_touched, pages}, text";
        start["responses"]["400"]["description"] = "Unknown level, mode or option";
        start["responses"]["409"]["description"] = "Memory tracking could not start";

        Json::Value& stop = paths["/api/v1/emulator/{id}/analysis/stop"]["post"];
        stop["summary"] = "Stop the analysis session";
        stop["tags"].append("Analyzers");
        stop["description"] = "Stops the memory session; the facts stay readable (explain) until the next start.";
        idParameter(stop);
        stop["responses"]["200"]["description"] = "The status after the stop, text";

        Json::Value& status = paths["/api/v1/emulator/{id}/analysis/status"]["get"];
        status["summary"] = "Analysis session status";
        status["tags"].append("Analyzers");
        status["description"] =
            "Session state (capturing, paused, stopped), tracker level and mode, the current frame and what the facts "
            "hold: bytes allocated, Z80 addresses touched, physical pages with facts.";
        idParameter(status);
        status["responses"]["200"]["description"] = "state, level, mode, frame, facts, text";

        Json::Value& explain = paths["/api/v1/emulator/{id}/analysis/explain"]["get"];
        explain["summary"] = "Explain one address from the L0 access facts";
        explain["tags"].append("Analyzers");
        explain["description"] =
            "What the access facts know of one byte: kinds (OpcodeFetch, OperandFetch, DataRead, DataWrite, StackRead, "
            "StackWrite, BlockAccess, WrittenWhileCode), the last reader and writer (PC and label), whether several "
            "instructions read or wrote it, the first and last frame, the smallest and largest value written and how "
            "many distinct values (1, 2, 3+), the read / write / execute counters and the label of the address. text "
            "carries the same as lines.";
        idParameter(explain);
        explain["parameters"][1]["name"] = "address";
        explain["parameters"][1]["in"] = "query";
        explain["parameters"][1]["required"] = true;
        explain["parameters"][1]["schema"]["type"] = "string";
        explain["parameters"][1]["description"] = "$80BC, #80BC, 0x80BC, 80BCh, decimal or a label name";
        explain["parameters"][2]["name"] = "page";
        explain["parameters"][2]["in"] = "query";
        explain["parameters"][2]["required"] = false;
        explain["parameters"][2]["schema"]["type"] = "string";
        explain["parameters"][2]["description"] =
            "Physical mode: the page (absolute index, ram:N or rom:N); default the page mapped at the address now";
        explain["responses"]["200"]["description"] =
            "address, mode, bank or page, label, accessed, kinds, kinds_mask, first_frame, last_frame, last_reader, "
            "last_writer, multi_reader, multi_writer, written {distinct, min, max}, counters, text";
        explain["responses"]["400"]["description"] = "No address, a bad page, an unknown option";
        explain["responses"]["404"]["description"] = "No such label";
        explain["responses"]["409"]["description"] = "The tracker level is counters: no facts (analysis start first)";
    }
