    // Frame watches (docs/inprogress/2026-11-03-run-until-watches, watch_api.cpp): run_until and the armed watches
    {
        auto op = [&paths](const std::string& path, const char* method) -> Json::Value& {
            Json::Value& o = paths[path][method];
            o["tags"].append("Debug Commands");
            o["parameters"][0]["name"] = "id";
            o["parameters"][0]["in"] = "path";
            o["parameters"][0]["required"] = true;
            o["parameters"][0]["schema"]["type"] = "string";
            o["responses"]["404"]["description"] = "No such emulator";
            return o;
        };
        auto rules = [](Json::Value& properties) {
            properties["if"]["type"] = "string";
            properties["if"]["description"] =
                "A frame-end condition in the conditional-breakpoint language without access operands: M(x), W(x), "
                "registers, FRAME, FD, PG0..PG3, x@prev, labels (\"M(#5C3A) < M(#5C3A)@prev\")";
            properties["text"]["type"] = "string";
            properties["text"]["description"] = "The text is on screen (ROM-font OCR as GET /capture/ocr; case-sensitive)";
            properties["screen"]["type"] = "string";
            properties["screen"]["enum"].append("changed");
            properties["screen"]["enum"].append("stable");
            properties["screen"]["description"] =
                "changed: the active screen's digest differs from the start; stable: it held for stable_frames frames";
            properties["stable_frames"]["type"] = "integer";
            properties["stable_frames"]["description"] = "For screen stable: 1..100000, default 25";
            properties["event"]["type"] = "string";
            properties["event"]["description"] = "A game event of the profile (events:) fires after the start";
            properties["match"]["type"] = "string";
            properties["match"]["enum"].append("any");
            properties["match"]["enum"].append("all");
            properties["match"]["description"] = "any rule (default) or all at the same frame end";
        };

        Json::Value& run = op("/api/v1/emulator/{id}/run_until", "post");
        run["summary"] = "Run whole frames until a rule holds at a frame end";
        run["description"] =
            "A direct run (the machine is paused first and stays paused) of at most max_frames frames that stops at "
            "the end of the first frame where the rules hold: a frame-end condition, a text on screen, the screen "
            "changing or holding still, a game event firing. One call instead of run_frames chunks and a read per "
            "chunk; the stop is at a frame boundary, the same on every host. Breakpoints are skipped unless "
            "breakpoints is true. A long call: it runs on the long-call pool.";
        Json::Value& runProps = run["requestBody"]["content"]["application/json"]["schema"]["properties"];
        run["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
        rules(runProps);
        runProps["max_frames"]["type"] = "integer";
        runProps["max_frames"]["description"] = "1..200000, default 3000";
        runProps["breakpoints"]["type"] = "boolean";
        runProps["breakpoints"]["description"] = "A breakpoint on the way ends the run (default false: skipped)";
        run["responses"]["200"]["description"] =
            "{met, outcome (met, budget, breakpoint), frames, frame, pc, ttd_frame?, ms, hit?: {frame, reasons, "
            "text_line?, stable_for?, event_fires?}, text, status, message, sp, state, stop}";
        run["responses"]["400"]["description"] = "No rule, an unknown option, a condition that does not compile, an unknown game event";
        run["responses"]["409"]["description"] = "Run-control held by another surface, or revoked_by_user";

        Json::Value& list = op("/api/v1/emulator/{id}/watches", "get");
        list["summary"] = "List the armed frame watches";
        list["description"] = "Every armed watch with its spec, hits and last hit: {count, watches: [{id, label?, spec, "
                              "then, once, armed_frame, hits, last_hit?, error?}], text}";
        list["responses"]["200"]["description"] = "{count, watches, text}";

        Json::Value& add = op("/api/v1/emulator/{id}/watches", "post");
        add["summary"] = "Arm a frame watch on the running machine";
        add["description"] =
            "The run_until rules checked at every frame end of the running machine (not in a TTD seek). A hit is pushed "
            "as watch_hit on debug.events (frame, reasons, text_line, paused), wakes GET /debug/wait and, with then "
            "pause (the default), pauses the machine at that frame end: the debugger snapshot's pause.reason is then "
            "watch with watch_id. A once watch (the default) goes with its first hit. At most 16.";
        Json::Value& addProps = add["requestBody"]["content"]["application/json"]["schema"]["properties"];
        add["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
        rules(addProps);
        addProps["label"]["type"] = "string";
        addProps["then"]["type"] = "string";
        addProps["then"]["enum"].append("pause");
        addProps["then"]["enum"].append("notify");
        addProps["once"]["type"] = "boolean";
        add["responses"]["201"]["description"] = "{id, watch, text}";
        add["responses"]["400"]["description"] = "A bad spec, or 16 watches armed";

        Json::Value& remove = op("/api/v1/emulator/{id}/watches/{watch}", "delete");
        remove["summary"] = "Remove a frame watch";
        remove["parameters"][1]["name"] = "watch";
        remove["parameters"][1]["in"] = "path";
        remove["parameters"][1]["required"] = true;
        remove["parameters"][1]["schema"]["type"] = "string";
        remove["parameters"][1]["description"] = "The watch id, or all";
        remove["responses"]["200"]["description"] = "{removed: [ids], text}";
        remove["responses"]["404"]["description"] = "No such emulator or watch";
    }
