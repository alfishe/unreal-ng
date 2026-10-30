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

        Json::Value& gallery = paths["/api/v1/emulator/{id}/analysis/gallery"]["post"];
        gallery["summary"] = "Graphics found by L2, drawn as PNG";
        gallery["tags"].append("Analyzers");
        gallery["description"] =
            "Without an address: the graphics regions L2 claimed (fonts, sprites, tiles, screens and back buffers, "
            "level maps) with their geometry (width, height, frames, stride, mask, prefix, layout). With an address "
            "(or a label): the region starting there (else the first covering it) drawn from the memory at the end of "
            "the replay - frames in a grid, black ink on white paper (a tile's attribute colours it, a mask's "
            "transparent pixels light grey), screens in the display file's row order, attributes and map cells as "
            "8x8 colour cells - zoomed scale times, as a PNG: base64 in png, or written to path.";
        idParameter(gallery);
        {
            Json::Value& schema = gallery["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            schema["properties"]["address"]["type"] = "string";
            schema["properties"]["address"]["description"] = "$8200, 0x8200, decimal or a label (none: the list)";
            schema["properties"]["kind"]["type"] = "string";
            for (const char* kind : {"all", "font", "sprite", "tile", "screen", "level_map"})
                schema["properties"]["kind"]["enum"].append(kind);
            schema["properties"]["scale"]["type"] = "integer";
            schema["properties"]["scale"]["description"] = "Integer zoom 1..8 (default 2)";
            schema["properties"]["path"]["type"] = "string";
            schema["properties"]["path"]["description"] = "Write the PNG here (none: base64 in png)";
            schema["properties"]["limit"]["type"] = "integer";
            schema["properties"]["limit"]["description"] = "The most regions listed (default 200)";
        }
        gallery["responses"]["200"]["description"] =
            "list: regions, count, truncated, text; one: region, width, height, frames, scale, bytes, mime_type, "
            "png, path, text";
        gallery["responses"]["400"]["description"] = "An unknown kind, a bad scale or limit";
        gallery["responses"]["404"]["description"] = "No graphics region at the address, or no such label";
        gallery["responses"]["409"]["description"] =
            "No L2 yet (build first), a geometry with no picture, or the path could not be written";

        // L3 (P5): the same options in the query (GET) or the body (POST)
        auto bodyOf = [](Json::Value& op, const std::vector<std::pair<const char*, const char*>>& fields) {
            Json::Value& schema = op["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            for (const auto& [name, text] : fields)
            {
                const bool object = std::string(name) == "spec" || std::string(name) == "experiment" ||
                                    std::string(name) == "names";
                if (object)
                    for (const char* type : {"object", "array", "string"})
                        schema["properties"][name]["oneOf"].append(Json::Value(Json::objectValue))["type"] = type;
                else
                    schema["properties"][name]["type"] = "string";
                schema["properties"][name]["description"] = text;
            }
        };
        auto queryOf = [&idParameter](Json::Value& op, const std::vector<std::pair<const char*, const char*>>& fields) {
            idParameter(op);
            for (const auto& [name, text] : fields)
            {
                Json::Value p;
                p["name"] = name;
                p["in"] = "query";
                p["required"] = false;
                p["schema"]["type"] = "string";
                p["description"] = text;
                op["parameters"].append(p);
            }
        };
        const std::vector<std::pair<const char*, const char*>> findingsFields = {
            {"op", "list (default) or add (POST: a note)"},
            {"kind", "claim, conflict, bug, routine, unknown, coverage, note, experiment, entry"},
            {"status", "observed, inferred, refuted, confirmed (add: the note's status)"},
            {"range", "Hex start-end (list: overlapping; add: the note's range)"},
            {"limit", "The most findings answered (default 200)"},
            {"format", "table (default), markdown (zx-reaper re/findings.md) or jsonl"},
            {"path", "Write the markdown / jsonl here"},
            {"claim", "add: the sentence"},
            {"evidence", "add: evidence, ';' separated"},
            {"by", "add: who (default agent)"},
            {"title", "add: a title"}};
        const char* findingsText =
            "The L3 findings register of the last build: every L2 claim, conflict, candidate bug (reads of never "
            "written bytes, writes into code outside SMC, stack imbalance, indexes past a table, counters that wrap), "
            "unnamed hot routine and unknown area, with the notes, coverage and experiments added since; each with "
            "evidence, a status (observed, inferred, refuted, confirmed) and an id F1.. in a fixed order.";
        for (const char* method : {"get", "post"})
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/analysis/findings"][method];
            op["summary"] = std::string(method) == "get" ? "List the L3 findings" : "List the L3 findings or add a note";
            op["tags"].append("Analyzers");
            op["description"] = findingsText;
            if (std::string(method) == "get")
                queryOf(op, findingsFields);
            else
            {
                idParameter(op);
                bodyOf(op, findingsFields);
            }
            op["responses"]["200"]["description"] = "count, total, by_kind, by_status, findings (or content), text";
            op["responses"]["400"]["description"] = "An unknown op, kind, status or format, a bad range";
            op["responses"]["409"]["description"] = "No L2 yet (build first), or the path could not be written";
        }
        const std::vector<std::pair<const char*, const char*>> reviewFields = {
            {"op", "next (default), list, show, resolve"},
            {"id", "show / resolve: the finding (F12) or the naming batch (N3)"},
            {"verdict", "resolve: ok (confirmed), wrong (refuted), skip (out of the queue)"},
            {"label", "resolve: the entry's label in the draft"},
            {"title", "resolve: the entry's title in the draft"},
            {"comment", "resolve: a comment in the draft"},
            {"kind", "resolve: what the draft makes of the finding's range (code, data, text, font, sprite, tile, "
                     "screen, variable, pointer_table, handler_table, rng, music, packed, level_map, unused)"},
            {"names", "resolve of a naming batch: {\"$9000\": \"PlayerY\", \"$9A10\": {\"label\": ..., \"title\": "
                      "...}} or [{\"at\", \"label\", \"title\"}]; the subjects left out are skipped"},
            {"by", "resolve: who decided (default agent)"},
            {"experiment", "resolve: an experiment spec whose result marks the finding"},
            {"ranges", "Hex Z80 ranges, comma separated (default 4000-FFFF)"},
            {"reason", "list / next: one reason (bug, conflict, unnamed-hot, low-confidence, unknown, entry, name)"},
            {"offset", "list / next: items of the queue skipped (default 0)"},
            {"limit", "The queue's cap (default 40; the rest is one summary)"}};
        for (const char* method : {"get", "post"})
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/analysis/review"][method];
            op["summary"] = "The L3 review queue";
            op["tags"].append("Analyzers");
            op["description"] =
                "Only the findings that need a decision: candidate bugs, conflicts, unnamed hot routines, low "
                "confidence claims, unknown areas over 16 bytes, entry candidates, then naming batches (N1..: up to 10 "
                "draft labels named after their address, a 2 KB sheet), in a fixed order, capped with one summary item. "
                "next answers the next item with a fact sheet under 1 KB; resolve writes the decision into the register "
                "and the draft export (label, title, comment, kind; a batch's names).";
            if (std::string(method) == "get")
                queryOf(op, reviewFields);
            else
            {
                idParameter(op);
                bodyOf(op, reviewFields);
            }
            op["responses"]["200"]["description"] = "next / show: item, sheet, sheet_bytes, queue; list: items, queue "
                                                    "(open, shown, offset, resolved, summary, reason, by_reason); resolve: "
                                                    "finding, kind, experiments (a batch: batch, named, skipped); text";
            op["responses"]["400"]["description"] = "An unknown op or verdict, a bad label, a resolve with no decision";
            op["responses"]["404"]["description"] = "No such finding or naming batch";
            op["responses"]["409"]["description"] = "No L2 yet (build first), a label already used";
        }
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/analysis/experiment"]["post"];
            op["summary"] = "Run L3 experiments on muted clones";
            op["tags"].append("Analyzers");
            op["description"] =
                "Each spec: pokes, registers and input events (keys, joystick) applied on a muted headless analysis "
                "instance seeked to a TTD point (start, end or frame:N), then N frames, then checks over memory, the "
                "screen, watched addresses (pc / write / read) and new code. The source instance is never touched; "
                "the experiments run in a bounded pool, each on its own clone. The result marks the finding "
                "observed or refuted.";
            idParameter(op);
            bodyOf(op, {{"spec", "One spec object or an array of them (or their JSON text)"},
                        {"path", "A JSON file with the spec(s)"},
                        {"parallel", "Clones at once (1..8)"},
                        {"finding", "The finding the result marks"}});
            op["responses"]["200"]["description"] = "results, observed, refuted, errors, workers, peak_concurrency, seeks, micros, text";
            op["responses"]["400"]["description"] = "A bad spec (the field and what it takes)";
            op["responses"]["409"]["description"] = "No L2 yet (build first), or no recording kept";
        }
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/analysis/cover"]["post"];
            op["summary"] = "L3 directed coverage";
            op["tags"].append("Analyzers");
            op["description"] =
                "Flips branches the session took one way only: holds the byte a branch tests (the compared constant "
                "first) or presses the keys / joystick directions of the port it reads, in experiments on clones, and "
                "reports the new code as coverage findings and draft notes.";
            idParameter(op);
            bodyOf(op, {{"budget", "Experiments in all (default 64)"},
                        {"per_branch", "Attempts per branch (default 8)"},
                        {"frames", "Frames per attempt (default 50)"},
                        {"parallel", "Clones at once (1..8)"},
                        {"limit", "Branches tried at most (default 200)"},
                        {"ranges", "Hex Z80 ranges, comma separated (default 4000-FFFF)"}});
            op["responses"]["200"]["description"] = "candidates, tried, flipped, attempts, new_code, branches, text";
            op["responses"]["400"]["description"] = "A bad budget, frames or range";
            op["responses"]["409"]["description"] = "No L2 yet (build first)";
        }
    }
