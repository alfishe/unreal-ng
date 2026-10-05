    // ZX-bus slots (ZX-bus slots architecture.md §9): the machine's buses, slots and cards through SlotControl
    // (core/src/emulator/slots/slotcontrol.h). Every change is applied by restarting the machine (owner decision Q6):
    // the reply names the new emulator id. The verbs come from SlotControl at run time.
    {
        auto idParameter = [](Json::Value& operation, int index) {
            operation["parameters"][index]["name"] = "id";
            operation["parameters"][index]["in"] = "path";
            operation["parameters"][index]["required"] = true;
            operation["parameters"][index]["schema"]["type"] = "string";
            operation["parameters"][index]["description"] = "Emulator UUID, symbolic id or index";
        };
        auto slotParameter = [](Json::Value& operation, int index) {
            operation["parameters"][index]["name"] = "slot";
            operation["parameters"][index]["in"] = "path";
            operation["parameters"][index]["required"] = true;
            operation["parameters"][index]["schema"]["type"] = "string";
            operation["parameters"][index]["description"] =
                "A slot of the machine: ay-socket, <bus>.<n> (zxbus.1, edge.1, atm-iobus.1, profi-bus.1, cpu-socket.1), "
                "<bus>.next for the next free slot of a bus, or auto (plug only: where the planner puts the card)";
        };
        auto changeReply = [](Json::Value& operation) {
            operation["responses"]["200"]["description"] =
                "status applied (the machine was restarted: restart.emulatorId is the new id, restart.started) or "
                "dry-run (the plan only)";
            operation["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
                "#/components/schemas/SlotChangeReply";
            operation["responses"]["400"]["description"] = "bad-request: unknown card, option or verb";
            operation["responses"]["404"]["description"] = "no-machine: no such emulator, or no slot declaration";
            operation["responses"]["409"]["description"] =
                "refused (message 'refused: ...' or 'needs replaceIfIncompatible: ...'; plan.removed lists every card "
                "the change would remove with the options that put it back) or recording (a TTD recording runs, "
                "R-OP-7). The body carries the plan";
            operation["responses"]["409"]["content"]["application/json"]["schema"]["$ref"] =
                "#/components/schemas/SlotChangeReply";
            operation["responses"]["500"]["description"] = "failed: the new machine could not be created; the old one keeps running";
        };
        auto changeBody = [](Json::Value& operation, bool card) {
            Json::Value& schema = operation["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            if (card)
            {
                schema["properties"]["card"]["type"] = "string";
                schema["properties"]["card"]["description"] = "Card id (GET slots/catalog): multisound, neogs, gs, tsfm, ...";
                schema["properties"]["adapter"]["type"] = "string";
                schema["properties"]["adapter"]["description"] = "An adapter in the slot (the card sits behind it)";
            }
            schema["properties"]["options"]["description"] =
                "Card options: \"dip=ym,saa gsRam=2m\" or {\"dip\": [\"ym\", \"saa\"], \"gsRam\": \"2m\"}; set merges "
                "them over the slot's current options";
            schema["properties"]["replaceIfIncompatible"]["type"] = "boolean";
            schema["properties"]["replaceIfIncompatible"]["description"] =
                "Allow the removals (and an unrealistic fit) the plan lists; without it such a change is refused with "
                "the plan (owner decision Q1)";
            schema["properties"]["dryRun"]["type"] = "boolean";
            schema["properties"]["dryRun"]["description"] = "Plan only: nothing changes";
            schema["properties"]["mediaDisposition"]["type"] = "string";
            schema["properties"]["mediaDisposition"]["enum"].append("save");
            schema["properties"]["mediaDisposition"]["enum"].append("discard");
            schema["properties"]["mediaDisposition"]["description"] =
                "Unsaved media of a removed card (sd.ngs): save them into their files or discard them; without it "
                "such a change is refused";
        };

        Json::Value& list = paths["/api/v1/emulator/{id}/slots"]["get"];
        list["summary"] = "The machine's buses, slots, fitted cards and built-in devices";
        list["description"] =
            "The slot report (DeviceState::Slots): the board, its buses (kind, arbitration, physical slots, retrofit "
            "note for a bus the board has no connector for), every configured slot (card, options, adapter, fit "
            "real / adapter / unrealistic, state active / disabled with the reason, functions, port claims, media, "
            "notes), every built-in device (active, switched off, shadowed by a slot, replaced in its socket) and "
            "the plan log";
        list["tags"].append("Slots");
        idParameter(list, 0);
        list["responses"]["200"]["description"] = "The slot report (status ok)";
        list["responses"]["404"]["description"] = "no-machine";

        Json::Value& catalog = paths["/api/v1/emulator/{id}/slots/catalog"]["get"];
        catalog["summary"] = "Every card with its options and how it fits this machine";
        catalog["description"] =
            "cards[]: id, name, native bus, the bus signals it needs, options (name, kind enum / set, values, default), "
            "functions, ports, media slots, emulated; thisMachine: the slot the planner suggests, fit (real / adapter / "
            "unrealistic), adapter, outcome (fits / needs-replace / refused), what it would remove, the reasons, the "
            "slots it is fitted in";
        catalog["tags"].append("Slots");
        idParameter(catalog, 0);
        catalog["responses"]["200"]["description"] = "The catalog (status ok)";
        catalog["responses"]["404"]["description"] = "no-machine";

        std::string tables;
        for (std::string_view name : slots::MatrixTableNames())
            tables += (tables.empty() ? "" : ", ") + std::string(name);
        Json::Value& matrix = paths["/api/v1/emulator/{id}/slots/matrix"]["get"];
        matrix["summary"] = "The compatibility tables (markdown)";
        matrix["description"] = "tables[]: name and markdown, generated from the reference data by the plan engine "
                                "(compatibility-matrix.md §1-§4). Tables: " + tables;
        matrix["tags"].append("Slots");
        idParameter(matrix, 0);
        matrix["parameters"][1]["name"] = "table";
        matrix["parameters"][1]["in"] = "query";
        matrix["parameters"][1]["required"] = false;
        matrix["parameters"][1]["schema"]["type"] = "string";
        matrix["parameters"][1]["description"] = "One table (default: all)";
        matrix["responses"]["200"]["description"] = "The tables (status ok)";
        matrix["responses"]["400"]["description"] = "unknown table";

        Json::Value& post = paths["/api/v1/emulator/{id}/slots/{slot}/{verb}"]["post"];
        post["summary"] = "plug / remove / options: a slot change, applied by a restart";
        post["description"] =
            "plug: a card into the slot (body card, options, adapter); remove: the card out of it; options: the "
            "card's options (as PUT .../options). The change is planned against the machine's slot set first: "
            "refused when the plan refuses it, when it would remove a card or fit one unrealistically without "
            "replaceIfIncompatible, while a TTD recording runs, or when a removed card's medium has unsaved changes "
            "without mediaDisposition. dryRun returns the plan. Applied, the machine is restarted with the new slot "
            "set (a new emulator id, the machine state is lost, the media follow) and started again when it ran";
        post["tags"].append("Slots");
        idParameter(post, 0);
        slotParameter(post, 1);
        post["parameters"][2]["name"] = "verb";
        post["parameters"][2]["in"] = "path";
        post["parameters"][2]["required"] = true;
        post["parameters"][2]["schema"]["type"] = "string";
        post["parameters"][2]["schema"]["enum"].append("plug");
        post["parameters"][2]["schema"]["enum"].append("remove");
        post["parameters"][2]["schema"]["enum"].append("options");
        changeBody(post, true);
        changeReply(post);

        Json::Value& put = paths["/api/v1/emulator/{id}/slots/{slot}/options"]["put"];
        put["summary"] = "Change a slot card's options (applied by a restart)";
        put["description"] = "The options merge over the slot's current ones; planned and applied like a plug";
        put["tags"].append("Slots");
        idParameter(put, 0);
        slotParameter(put, 1);
        changeBody(put, false);
        changeReply(put);
    }
