    // Analysis layers (analysis::Execute, docs/emulator/analysis/analysis-layers.md): the L0 access facts (P1), the L1 build (P2)
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
            schema["properties"]["record"]["type"] = "boolean";
            schema["properties"]["record"]["description"] =
                "Also start the TTD recording that 'analysis build' replays into L1 (stop ends it)";
        }
        start["responses"]["200"]["description"] = "state, level, mode, frame, facts {allocated, bytes, z80_bytes_touched, pages}, text";
        start["responses"]["400"]["description"] = "Unknown level, mode or option";
        start["responses"]["409"]["description"] = "Memory tracking or the recording could not start";
        start["responses"]["503"]["description"] = "record: no time travel engine on this instance";

        Json::Value& build = paths["/api/v1/emulator/{id}/analysis/build"]["post"];
        build["summary"] = "Build L1 (code behaviour) from the recording - a long operation";
        build["tags"].append("Analyzers");
        build["description"] =
            "Ends the recording, captures it with the session's facts (a trace bundle) and replays it on a muted "
            "headless analysis instance from its first keyframe to its end, folding every instruction into L1: basic "
            "blocks, the call graph (CALL / RST / RET, tail jumps, PUSH + RET dispatch, JP (HL) targets, interrupts), "
            "routines with their profiles (read / write / exec ranges, ports, T-state share, frames, live registers), "
            "loops (trips, induction registers, strides), base + offset fields, data flow and branch conditions. "
            "Answers at once with the job id; the WebSocket topics analysis.progress and analysis.ready follow the job "
            "(or GET status). Then explain a routine's address for its fact sheet.";
        idParameter(build);
        build["requestBody"]["required"] = false;
        {
            Json::Value& schema = build["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            schema["properties"]["frames"]["type"] = "integer";
            schema["properties"]["frames"]["description"] = "Replay at most this many frames (default: all)";
            schema["properties"]["path"]["type"] = "string";
            schema["properties"]["path"]["description"] = "Write the L1 JSON to this file when the build is done";
            schema["properties"]["wait"]["type"] = "integer";
            schema["properties"]["wait"]["description"] = "Milliseconds (<= 30000) to wait for the build before answering";
        }
        build["responses"]["200"]["description"] = "job, state, bundle, progress, ready, text";
        build["responses"]["400"]["description"] = "A bad frames / wait value or an unknown option";
        build["responses"]["409"]["description"] = "No recording, or a build is running";

        Json::Value& cancel = paths["/api/v1/emulator/{id}/analysis/cancel"]["post"];
        cancel["summary"] = "Cancel the running L1 build";
        cancel["tags"].append("Analyzers");
        cancel["description"] = "Cancels the build job and answers the status.";
        idParameter(cancel);
        cancel["responses"]["200"]["description"] = "The status after the cancel, text";
        cancel["responses"]["409"]["description"] = "No build is running";

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
            "hold: bytes allocated, Z80 addresses touched, physical pages with facts; whether a recording runs; the L1 "
            "job (l1.progress) and the L1 result (l1.ready).";
        idParameter(status);
        status["responses"]["200"]["description"] = "state, level, mode, frame, facts, recording, l1 {progress, ready}, text";

        Json::Value& explain = paths["/api/v1/emulator/{id}/analysis/explain"]["get"];
        explain["summary"] = "Explain a routine (L1 fact sheet) or one byte (L0 access facts)";
        explain["tags"].append("Analyzers");
        explain["description"] =
            "What the access facts know of one byte: kinds (OpcodeFetch, OperandFetch, DataRead, DataWrite, StackRead, "
            "StackWrite, BlockAccess, WrittenWhileCode), the last reader and writer (PC and label), whether several "
            "instructions read or wrote it, the first and last frame, the smallest and largest value written and how "
            "many distinct values (1, 2, 3+), the read / write / execute counters and the label of the address. text "
            "carries the same as lines. With an L1 built and a routine running at the address (what=auto or "
            "routine), the routine instead: its summary fields and, in text, a fact sheet of at most 1 KB with a "
            "disassembly excerpt of at most 24 lines.";
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
            "The page (absolute index, ram:N or rom:N): L0 in physical mode (default the page mapped at the address "
            "now); L1 picks the routine of that page";
        explain["parameters"][3]["name"] = "what";
        explain["parameters"][3]["in"] = "query";
        explain["parameters"][3]["required"] = false;
        explain["parameters"][3]["schema"]["type"] = "string";
        explain["parameters"][3]["schema"]["enum"].append("auto");
        explain["parameters"][3]["schema"]["enum"].append("routine");
        explain["parameters"][3]["schema"]["enum"].append("byte");
        explain["parameters"][3]["description"] = "auto (default): the routine when L1 has one there, else the byte";
        explain["responses"]["200"]["description"] =
            "address, mode, bank or page, label, accessed, kinds, kinds_mask, first_frame, last_frame, last_reader, "
            "last_writer, multi_reader, multi_writer, written {distinct, min, max}, counters, text";
        explain["responses"]["400"]["description"] = "No address, a bad page, an unknown option";
        explain["responses"]["404"]["description"] = "No such label; what=routine: no L1 routine runs there";
        explain["responses"]["409"]["description"] =
            "The tracker level is counters: no facts (analysis start first); what=routine: no L1 built yet";
        Json::Value& query = paths["/api/v1/emulator/{id}/analysis/query"]["get"];
        query["summary"] = "L2 regions: what the bytes are";
        query["tags"].append("Analyzers");
        query["description"] =
            "The claims the L2 detectors made from the last L1 (signatures, code map, text, init tables, records, "
            "pointer / handler tables, variables, in that order): each region with its range (physical and Z80), kind, "
            "geometry (fields, instances, stride, terminator, generator, role), the routines that use it, the evidence "
            "and an integer confidence 0..1000. A byte may carry several claims; kind=conflicts lists the pairs that "
            "cannot both hold. text is a compact table.";
        idParameter(query);
        query["parameters"][1]["name"] = "kind";
        query["parameters"][1]["in"] = "query";
        query["parameters"][1]["required"] = false;
        query["parameters"][1]["schema"]["type"] = "string";
        query["parameters"][1]["description"] = "all (default), conflicts or a region kind (code, text, records, ...)";
        query["parameters"][2]["name"] = "range";
        query["parameters"][2]["in"] = "query";
        query["parameters"][2]["required"] = false;
        query["parameters"][2]["schema"]["type"] = "string";
        query["parameters"][2]["description"] = "Hex start-end (8000-83FF); default the whole 64K";
        query["parameters"][3]["name"] = "limit";
        query["parameters"][3]["in"] = "query";
        query["parameters"][3]["required"] = false;
        query["parameters"][3]["schema"]["type"] = "integer";
        query["parameters"][3]["description"] = "The most regions answered (default 200)";
        query["responses"]["200"]["description"] = "kind, range, regions (or conflicts), count, truncated, text";
        query["responses"]["400"]["description"] = "An unknown kind, a bad range or limit";
        query["responses"]["409"]["description"] = "No L2 yet (build first)";

        Json::Value& exportDraft = paths["/api/v1/emulator/{id}/analysis/export"]["post"];
        exportDraft["summary"] = "Export the L2 draft";
        exportDraft["tags"].append("Analyzers");
        exportDraft["description"] =
            "Writes the L2 regions as a SkoolKit control file (ctl, for sna2skool.py -c), a skool draft that builds "
            "relocatable (labels on referenced addresses, @isub inside instructions and on page operands, @keep on "
            "constants, ALIGN 256 before page tables, @org at range starts and the fixed tail), the record field lists "
            "(fields) and graphics geometry (geometry) as JSON, the relocation layout (layout: REGION_START, "
            "SHIFT_END), a symbol file through the skoolkit-ctl codec (symbols) or the region map (map). With a path "
            "the file goes there; without, the answer's content carries it.";
        idParameter(exportDraft);
        {
            Json::Value& schema = exportDraft["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            schema["required"].append("format");
            schema["properties"]["format"]["type"] = "string";
            for (const char* format : {"ctl", "skool", "fields", "geometry", "layout", "symbols", "map"})
                schema["properties"]["format"]["enum"].append(format);
            schema["properties"]["path"]["type"] = "string";
            schema["properties"]["path"]["description"] = "Write the file here (none: content in the answer)";
            schema["properties"]["ranges"]["type"] = "string";
            schema["properties"]["ranges"]["description"] = "Hex Z80 ranges, comma separated (default 4000-FFFF)";
            schema["properties"]["width"]["type"] = "integer";
            schema["properties"]["width"]["description"] = "Bytes per DEFB line (default 8)";
        }
        exportDraft["responses"]["200"]["description"] = "format, entries, bytes, ranges, path, content, text";
        exportDraft["responses"]["400"]["description"] = "No or an unknown format, bad ranges or width";
        exportDraft["responses"]["409"]["description"] = "No L2 yet (build first), or the path could not be written";
    }
