    // The Stream Deck daemon's supervision by unreal-qt (companion::streamdeck::Supervisor; Stream Deck integration
    // daemon.md §7), its settings, pairing and deck brightness: the verbs of companion::streamdeck::Call, as the CLI
    // `streamdeck`, MCP `streamdeck`, Lua / Python streamdeck(); every route is also a WebSocket call, and the topic
    // streamdeck.status pushes the changes
    {
        auto op = [&paths](const char* path, const char* method, const char* summary, const char* description,
                           const char* ok) -> Json::Value& {
            Json::Value& operation = paths[path][method];
            operation["summary"] = summary;
            operation["description"] = description;
            operation["tags"].append("Stream Deck");
            operation["responses"]["200"]["description"] = ok;
            operation["responses"]["503"]["description"] =
                "supervisor_unavailable: no unreal-qt here (headless, the video wall); start unreal-streamdeck by hand";
            return operation;
        };
        auto param = [](Json::Value& operation, const char* name, const char* in, bool required,
                        const char* description) {
            Json::Value parameter;
            parameter["name"] = name;
            parameter["in"] = in;
            parameter["required"] = required;
            parameter["description"] = description;
            parameter["schema"]["type"] = "string";
            operation["parameters"].append(parameter);
        };
        const char* statusFields =
            "{ok, status: {attached, state: detached | off | waiting | starting | running | adopted | foreign | "
            "backoff | failed | stopped, mode, activity, api_port, viewers_port, binary, own, pid, deck_attached, "
            "restarts, failures, restart_in_ms?, retry_in_ms?, last_exit, error, log, brightness, daemon: the "
            "daemon's /api/v1/health (version, decks, viewers, parents, host, surface, pairing), decks: its "
            "/api/v1/decks}}";

        Json::Value& status = op("/api/v1/streamdeck", "get", "The Stream Deck daemon and its supervisor",
                                 "unreal-qt starts unreal-streamdeck when a deck is attached (mode deck), always, or "
                                 "never (off); restarts it after a crash (1 s, 2 s, 4 s, then a pause of 60 s); adopts "
                                 "one already running (by hand, by Stream Deck Studio) and holds a lease on it",
                                 statusFields);
        status["responses"].removeMember("503");
        op("/api/v1/streamdeck/status", "get", "The same as GET /api/v1/streamdeck", "", statusFields)["responses"]
            .removeMember("503");
        op("/api/v1/streamdeck/start", "post", "Start the daemon (or adopt the one running)",
           "Also in mode off: the user asks", "{ok, requested, status}");
        op("/api/v1/streamdeck/stop", "post", "Stop the daemon",
           "POST /api/v1/quit to it, terminate after 2 s; nothing starts again until start (or the mode changes)",
           "{ok, requested, status}");
        op("/api/v1/streamdeck/restart", "post", "Stop the daemon, then start it", "", "{ok, requested, status}");
        op("/api/v1/streamdeck/settings", "get", "The supervisor's settings",
           "mode: off | deck (default) | always; activity: off | flash (default) | line (how a deck shows what "
           "other surfaces do); api_port (37053), viewers_port (37050); binary (\"\" = beside unreal-qt; "
           "UNREAL_STREAMDECK_BIN wins); brightness per deck serial",
           "{ok, settings}");
        Json::Value& settings = op("/api/v1/streamdeck/settings", "put", "Change the supervisor's settings",
                                   "Body {mode?, activity?, api_port?, viewers_port?, binary?}: saved in unreal-qt's "
                                   "settings; new ports or binary restart a daemon of ours",
                                   "{ok, settings}");
        settings["responses"]["400"]["description"] = "Bad Request: a bad value";
        Json::Value& pairing = op("/api/v1/streamdeck/pair", "get", "The daemon's pairing with unreal-ng",
                                  "state: idle | requesting | pending (code: the 6 digits the GUI prompt shows too) | "
                                  "paired | denied | expired | refused (error) | unreachable; paired, companion_id",
                                  "{ok, pairing}");
        pairing["responses"]["409"]["description"] = "not_running: no daemon answers";
        Json::Value& pair = op("/api/v1/streamdeck/pair", "post", "The daemon asks unreal-ng for a pairing",
                               "One companion for every deck of the daemon. The user approves it at unreal-qt's "
                               "pairing prompt (no automation plane can); the deck's strip shows the code",
                               "{ok, pairing}");
        pair["responses"]["409"]["description"] = "not_running: no daemon answers";
        op("/api/v1/streamdeck/pair", "delete", "Cancel the daemon's pending pairing", "", "{ok, pairing}");
        Json::Value& revoke = op("/api/v1/streamdeck/revoke", "post", "Revoke the daemon's companion",
                                 "Body {id?}: default the daemon's own (its companion file's id). Its token stops "
                                 "working at once; the deck says COMPANION REVOKED - TOUCH TO PAIR",
                                 "{ok, revoked, id}");
        revoke["responses"]["404"]["description"] = "Not Found: not paired, no such companion";
        op("/api/v1/streamdeck/udev", "get", "The Linux udev rule for Stream Decks",
           "The text of 70-unreal-streamdeck.rules and how to install it (an AppImage cannot); needed: true on Linux",
           "{ok, file, rule, needed}")["responses"]
            .removeMember("503");
        Json::Value& getBrightness = op("/api/v1/streamdeck/decks/{deck}/brightness", "get", "A deck's brightness",
                                        "", "{ok, deck: {serial, brightness}}");
        param(getBrightness, "deck", "path", true, "The deck's serial, or first");
        Json::Value& putBrightness = op("/api/v1/streamdeck/decks/{deck}/brightness", "put", "Set a deck's brightness",
                                        "Body {value: 0..100}; kept for that serial and set again when the daemon "
                                        "starts again",
                                        "{ok, deck: {serial, brightness}}");
        param(putBrightness, "deck", "path", true, "The deck's serial, or first");
        putBrightness["responses"]["400"]["description"] = "Bad Request: value 0-100";
    }
