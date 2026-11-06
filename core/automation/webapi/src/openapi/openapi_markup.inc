    // Memory markup (markup::Execute, docs/inprogress/2026-11-05-software-metadata-profile §5): what the main CPU's
    // memory holds, in layers user > profile > analysis > runtime > bundle
    {
        auto op = [&paths](const std::string& path, const char* method) -> Json::Value& {
            Json::Value& o = paths[path][method];
            o["tags"].append("Debug Commands");
            o["parameters"][0]["name"] = "id";
            o["parameters"][0]["in"] = "path";
            o["parameters"][0]["required"] = true;
            o["parameters"][0]["schema"]["type"] = "string";
            o["responses"]["400"]["description"] = "An unknown option, an address or kind that does not parse";
            o["responses"]["404"]["description"] = "No such emulator, or no such label";
            return o;
        };
        auto query = [](Json::Value& o, const char* name, const char* description) {
            Json::Value parameter;
            parameter["name"] = name;
            parameter["in"] = "query";
            parameter["required"] = false;
            parameter["schema"]["type"] = "string";
            parameter["description"] = description;
            o["parameters"].append(parameter);
        };
        const char* const addressNote = "$8000, #8000, 0x8000, 8000h, decimal or a label";

        Json::Value& show = op("/api/v1/emulator/{id}/markup", "get");
        show["summary"] = "What a CPU range holds: regions, instruction starts, comments";
        show["description"] =
            "The range (default the whole 64K) as spans of one winning claim each, resolved through the paging of the "
            "moment: kind (code, data, text, font, sprite, ...), layer (user, profile, analysis, runtime, bundle), "
            "source, title, confidence, start (the claim's first byte). Also the instruction starts known for certain "
            "(anchors: entries, code claims' first bytes, opcode fetches, L2 instructions) and the comments. The "
            "runtime layer is derived at the query from the L0 facts or the coverage.";
        query(show, "from", addressNote);
        query(show, "to", "The range's last byte (default: from, or #FFFF without from)");
        query(show, "range", "Instead of from / to: 8000-80FF, $8000-$80FF");
        query(show, "layer", "Only the spans this layer wins: user, profile, analysis, runtime, bundle");
        query(show, "limit", "At most this many rows of each list (1-4096, default 200)");
        show["responses"]["200"]["description"] =
            "{from, to, revision, count, truncated, spans [{from, to, kind, layer, source, title, confidence, start}], "
            "anchors [\"$8000\", ...], comments [{address, layer, text}], text}";

        Json::Value& mark = op("/api/v1/emulator/{id}/markup", "put");
        mark["summary"] = "Mark a range as a region kind (the user layer)";
        mark["description"] =
            "The claim is made on the pages the CPU shows now (one per window the range crosses), so it follows its "
            "page; cpu=true makes it on the CPU addresses instead, whatever is mapped. It wins over every other layer.";
        {
            Json::Value& schema = mark["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            schema["properties"]["from"]["type"] = "string";
            schema["properties"]["from"]["description"] = addressNote;
            schema["properties"]["to"]["type"] = "string";
            schema["properties"]["range"]["type"] = "string";
            schema["properties"]["kind"]["type"] = "string";
            schema["properties"]["kind"]["description"] =
                "An analysis region kind: code, data, text, font, sprite, tile, screen, buffer, init_table, records, "
                "variable, pointer_table, handler_table, rng, music, packed, level_map, unused, untouched";
            schema["properties"]["title"]["type"] = "string";
            schema["properties"]["cpu"]["type"] = "boolean";
            schema["required"].append("kind");
        }
        mark["responses"]["200"]["description"] = "{from, to, kind, cpu, revision, text}";

        Json::Value& unmark = op("/api/v1/emulator/{id}/markup", "delete");
        unmark["summary"] = "Forget the user layer's claims over a range";
        unmark["description"] = "The lower layers show again; the part of a claim outside the range stays.";
        query(unmark, "from", addressNote);
        query(unmark, "to", "The range's last byte (default: from)");
        query(unmark, "range", "Instead of from / to: 8000-80FF");
        unmark["responses"]["200"]["description"] = "{from, to, revision, text}";

        Json::Value& layers = op("/api/v1/emulator/{id}/markup/layers", "get");
        layers["summary"] = "Every markup layer counted";
        layers["description"] =
            "Per layer (lowest first) its source and the claims, entries and comments it holds; runtime: what the "
            "runtime layer is derived from now (facts, facts-physical, coverage, none). The WebSocket topic "
            "debug.markup pushes the same counts on every change.";
        layers["responses"]["200"]["description"] =
            "{layers [{layer, source, claims, entries, comments}], runtime, revision, text}";

        Json::Value& clear = op("/api/v1/emulator/{id}/markup/layers", "delete");
        clear["summary"] = "Empty a markup layer";
        clear["description"] = "user (the default), profile, analysis (the last L2 build shows again), runtime or bundle.";
        query(clear, "layer", "user, profile, analysis, runtime, bundle");
        clear["responses"]["200"]["description"] = "{layer, revision, text}";

        Json::Value& comment = op("/api/v1/emulator/{id}/markup/comment", "put");
        comment["summary"] = "Set the user layer's comment at an address";
        comment["description"] = "On the page shown at the address now; an empty text removes it.";
        {
            Json::Value& schema = comment["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            schema["properties"]["address"]["type"] = "string";
            schema["properties"]["address"]["description"] = addressNote;
            schema["properties"]["text"]["type"] = "string";
            schema["required"].append("address");
        }
        comment["responses"]["200"]["description"] = "{address, text, revision, text}";

        Json::Value& save = op("/api/v1/emulator/{id}/markup/save-profile", "post");
        save["summary"] = "Save the user layer (and chosen analysis claims) into a software profile";
        save["description"] =
            "Writes the user layer's regions and comments, and with analysis the L2 claims of those kinds (all, or "
            "kinds) of at least the confidence given (700), into the memory: section of the profile named, else the "
            "matched game profile, else a new user profile keyed by the loaded program; the profile is laid over the "
            "machine again and the user layer emptied (keep=true keeps it). Only a process with software profiles "
            "(unreal-qt) does it, and only for the instance its controllers follow.";
        {
            Json::Value& schema = save["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            schema["properties"]["profile"]["type"] = "string";
            schema["properties"]["analysis"]["type"] = "string";
            schema["properties"]["analysis"]["description"] = "all, or region kinds comma separated (or an array)";
            schema["properties"]["confidence"]["type"] = "integer";
            schema["properties"]["keep"]["type"] = "boolean";
        }
        save["responses"]["200"]["description"] =
            "{profile, path, created, regions, comments, saved_user_claims, saved_comments, saved_analysis_claims, "
            "kept_user_layer, text}";
        save["responses"]["409"]["description"] = "Nothing to save, no profile matched and no program to key a new one";
        save["responses"]["503"]["description"] = "This process keeps no software profiles";
    }
