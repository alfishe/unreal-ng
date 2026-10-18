    // Controllers: the machine's connectors and their retro devices (controller-manager surfaces.md §6), through
    // ControllerControl
    {
        auto idParameter = [](Json::Value& operation, int index) {
            operation["parameters"][index]["name"] = "id";
            operation["parameters"][index]["in"] = "path";
            operation["parameters"][index]["required"] = true;
            operation["parameters"][index]["schema"]["type"] = "string";
            operation["parameters"][index]["description"] = "Emulator UUID or symbolic id";
        };
        auto connectorParameter = [](Json::Value& operation, int index) {
            operation["parameters"][index]["name"] = "connector";
            operation["parameters"][index]["in"] = "path";
            operation["parameters"][index]["required"] = true;
            operation["parameters"][index]["schema"]["type"] = "string";
            operation["parameters"][index]["description"] =
                "A connector: id (kempston, sinclair-1, keyjoy.1, next.left, fuller), alias (left, right) or "
                "kind:index; 'auto' for plug. Unknown or ambiguous selectors answer with the candidates";
        };
        auto replies = [](Json::Value& operation) {
            operation["responses"]["200"]["description"] =
                "The envelope: ok, error, message, connector, pending, revision, then the verb's fields";
            operation["responses"]["400"]["description"] = "bad-request (unknown verb, option or device)";
            operation["responses"]["404"]["description"] = "unknown-connector, unknown-emulator";
            operation["responses"]["409"]["description"] =
                "incompatible, fixed, owned-by-replay, recording (TTD v1), ambiguous-connector";
        };

        Json::Value& list = paths["/api/v1/emulator/{id}/controllers"]["get"];
        list["summary"] = "List the machine's joystick connectors";
        list["description"] =
            "Every connector (field 'connectors'): id, alias, kind, index, fixed, the plugged device, the devices it "
            "accepts, the device's options and the controls held now. 'revision' increases with every plug";
        list["tags"].append("Controllers");
        idParameter(list, 0);
        replies(list);

        Json::Value& get = paths["/api/v1/emulator/{id}/controllers/{connector}"]["get"];
        get["summary"] = "One connector; 'targets' where a retro device can go, 'auto' the automatic choice";
        get["description"] =
            "GET /controllers/{connector}: the connector (field 'info'). GET /controllers/targets?device=<kind>: the "
            "connectors that take the device in order (field 'targets'), 'default' (the index plug auto uses) and "
            "'refusal'. GET /controllers/auto: the port-read evidence since the last load (field 'evidence': active, "
            "frames_observed, window, classes with frames and reads_per_frame) and the decision drawn from it (field "
            "'decision': decisive, keyboard, device, connector, label, reason)";
        get["tags"].append("Controllers");
        idParameter(get, 0);
        connectorParameter(get, 1);
        get["parameters"][2]["name"] = "device";
        get["parameters"][2]["in"] = "query";
        get["parameters"][2]["required"] = false;
        get["parameters"][2]["schema"]["type"] = "string";
        get["parameters"][2]["description"] = "targets: the retro device (kempston-joystick, sinclair-joystick, ...)";
        replies(get);

        Json::Value& post = paths["/api/v1/emulator/{id}/controllers/{connector}/{verb}"]["post"];
        post["summary"] = "Plug, unplug or set the options of a connector's device; auto/apply re-runs the auto choice";
        post["description"] =
            "Body (JSON) or query: device (plug), port (sinclair-joystick: 1 or 2), shift (cursor-joystick), keys "
            "(keys-joystick: 'up:q,down:a,left:o,right:p,fire:space'), extended, im2-leak (kempston-joystick), async. "
            "A change applies at the next frame boundary and the reply waits for it (up to 5 s) unless async";
        post["tags"].append("Controllers");
        idParameter(post, 0);
        connectorParameter(post, 1);
        post["parameters"][2]["name"] = "verb";
        post["parameters"][2]["in"] = "path";
        post["parameters"][2]["required"] = true;
        post["parameters"][2]["schema"]["type"] = "string";
        post["parameters"][2]["schema"]["enum"].append("plug");
        post["parameters"][2]["schema"]["enum"].append("unplug");
        post["parameters"][2]["schema"]["enum"].append("options");
        post["parameters"][2]["schema"]["enum"].append("apply");
        replies(post);
    }
