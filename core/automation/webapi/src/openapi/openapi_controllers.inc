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

    // The host side, for the process: profiles, bindings, devices (controller-manager surfaces.md §2, §6), through
    // HostInputControl; the Qt client owns them (503 unavailable elsewhere)
    {
        auto verbParameter = [](Json::Value& operation, std::initializer_list<const char*> verbs) {
            operation["parameters"][0]["name"] = "verb";
            operation["parameters"][0]["in"] = "path";
            operation["parameters"][0]["required"] = true;
            operation["parameters"][0]["schema"]["type"] = "string";
            for (const char* verb : verbs)
                operation["parameters"][0]["schema"]["enum"].append(verb);
        };
        auto replies = [](Json::Value& operation) {
            operation["responses"]["200"]["description"] = "The envelope: ok, error, message, then the verb's fields";
            operation["responses"]["400"]["description"] = "bad-request, invalid-binding";
            operation["responses"]["404"]["description"] = "unknown-profile, unknown-device";
            operation["responses"]["409"]["description"] = "read-only (a built-in profile cannot be deleted)";
            operation["responses"]["503"]["description"] = "unavailable: no host input in this process";
        };

        Json::Value& get = paths["/api/v1/controllers/{verb}"]["get"];
        get["summary"] = "Host game controllers: devices, context, profiles, profile, bindings, haptics";
        get["description"] =
            "devices: the connected host devices. context: the machine, the loaded file's SHA-256, the manual game "
            "profile, the applied profiles and the warnings. profiles (?set=all|play|debug|scheme|game&machine=): "
            "every profile with its kind, scope, specificity, match and whether it applies; shipped games not parsed "
            "yet have loaded=false. profile (?id=): one profile as JSON ('profile') and YAML ('yaml'). bindings "
            "(?device=&layer=): the effective bindings as written, each with the profile it came from. haptics "
            "(?device=): per device whether the emulator can drive its motors (available, else the reason: no "
            "motors, an Xbox pad over USB on macOS, no write access on Linux), its actuators (low, high, "
            "left-trigger, right-trigger) and the test effects rumble plays (id, label, ms)";
        get["tags"].append("Controllers");
        verbParameter(get, {"devices", "context", "profiles", "profile", "bindings", "haptics"});
        replies(get);

        Json::Value& post = paths["/api/v1/controllers/{verb}"]["post"];
        post["summary"] =
            "Host game controllers: save, delete, apply a profile; bind, unbind a control; the scheme; rumble";
        post["description"] =
            "Body (JSON) or query. save {text}: a profile as YAML or JSON (schema 1) into the user's profiles, applied "
            "at once. delete {id}: a user profile (a built-in of the same id comes back). apply {id}: the game profile "
            "chosen by hand, for a program the emulator did not load itself ('none' = by the loaded file). bind "
            "{control, spec, layer, family | device | profile}: spec is a binding object ({key: ENTER}, {retro: "
            "'auto:fire'}, {none: true}); it goes into user/<family>, user/device-<id>, user/global or the named "
            "profile (a shipped one is copied into the user's). unbind {control, layer, ...}: removes it there. "
            "scheme {id, family | device | profile}: the scheme that profile extends (scheme/kempston, "
            "scheme/sinclair-1; 'none' = no scheme). rumble {device, effect}: play a test effect on a host device "
            "(identify by default; low, high, clicks, left-trigger, right-trigger, swell as haptics lists them), as "
            "debug feedback that plays while the game's haptics are held back; unavailable with the reason when the "
            "emulator cannot drive the device's motors";
        post["tags"].append("Controllers");
        verbParameter(post, {"save", "delete", "apply", "bind", "unbind", "scheme", "rumble"});
        replies(post);
    }

    // The host actions (core ActionRegistry, control surfaces actions.md §1, §6): the same ids a pad, the Surface Dial,
    // a companion, MCP `actions`, the CLI `action` and the scripts' action() run
    {
        auto instanceParameter = [](Json::Value& operation, const char* in) {
            Json::Value parameter;
            parameter["name"] = "instance";
            parameter["in"] = in;
            parameter["required"] = false;
            parameter["description"] = "Emulator id or index; default: the selected instance, else the only one";
            parameter["schema"]["type"] = "string";
            operation["parameters"].append(parameter);
        };
        Json::Value& get = paths["/api/v1/controllers/actions"]["get"];
        get["summary"] = "Host actions: the catalogue and whether each can run now for this caller";
        get["description"] =
            "Every action (core and, with the Qt client, its GUI ones) in registration order: id, label, group, kind "
            "(press, delta, level, text), held (the release matters too), gui (only with the Qt client), scope (the "
            "companion scope it needs now: run, debug, rewind, ...; null = it changes nothing), needs_pause, params "
            "(the params it reads, one line: \"key, hold?, frames?\"; absent when none), available and, when not, error (not_now, Not Implemented, Run-control held, companion_forbidden, "
            "held_by_session, held_by_other) and reason. A dry run of the gates: no session is opened";
        get["tags"].append("Controllers");
        instanceParameter(get, "query");
        get["responses"]["200"]["description"] = "{ok, instance, actions[]}";
        get["responses"]["404"]["description"] = "Not Found: no such instance";

        Json::Value& post = paths["/api/v1/controllers/actions/{action}"]["post"];
        post["summary"] = "Run a host action on an instance (its actions worker, in order)";
        post["description"] =
            "Body {instance?, value?}. value by the action's kind: press - omitted / true (a press), false (a "
            "release, for held actions), or {pressed}; delta - a number of steps or {delta, rate}; level - 0..1; "
            "text - a string. {params: {...}} adds parameters (run.to_scanline: line; run.n_scanlines: count). The "
            "gates of every origin apply: a changing action without a session opens the implicit run-control session "
            "(automation_session in the reply), a companion token is checked against its scopes. The reply waits for "
            "the action: {ok, action, instance, message, result} or {ok: false, error, message}";
        post["tags"].append("Controllers");
        Json::Value action;
        action["name"] = "action";
        action["in"] = "path";
        action["required"] = true;
        action["description"] = "The action id (GET /api/v1/controllers/actions lists them): run.pause_toggle, "
                                "ttd.jog_frames, mach.nmi, ...";
        action["schema"]["type"] = "string";
        post["parameters"].append(action);
        post["responses"]["200"]["description"] = "Done; result is the action's body (a step: pc, sp, state, stop)";
        post["responses"]["400"]["description"] = "Bad Request: a value of the wrong kind, no instance named";
        post["responses"]["403"]["description"] = "companion_forbidden: the companion lacks the scope";
        post["responses"]["404"]["description"] = "Not Found: no such action or instance";
        post["responses"]["409"]["description"] =
            "not_now (needs a pause), Run-control held, held_by_session, held_by_other, revoked_by_user, Conflict";
        post["responses"]["501"]["description"] = "Not Implemented: a stub of the catalogue";
    }

    // The control surfaces' context and targets (actions::ContextState, actions::TargetState; Stream Deck integration
    // targeting.md), the readouts and the activity feed: the verbs of actions::surfaces::Call, as the CLI `surface`,
    // MCP `actions`, Lua / Python surfaces(); every route is also a WebSocket call
    {
        auto op = [&paths](const char* path, const char* method, const char* summary, const char* description,
                           const char* ok) -> Json::Value& {
            Json::Value& operation = paths[path][method];
            operation["summary"] = summary;
            operation["description"] = description;
            operation["tags"].append("Controllers");
            operation["responses"]["200"]["description"] = ok;
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
        const char* targetObject =
            "a target is {instance, index, model, ram_kb, view: {id, name} | null, mode: follow | view | instance, pin, "
            "source: view | selected | sole | none}; instance null when there is no machine";

        Json::Value& context = op("/api/v1/surfaces/context", "get", "The instance's surface context",
                                  "The context in force (play, ttd, ttd.timecode, debug.*), whether somebody set it, by "
                                  "whom, and the default it returns to; set it with the ctx.set action",
                                  "{ok, instance, context, set, by, default}");
        param(context, "instance", "query", false, "Emulator id or index; default: the followed instance");

        op("/api/v1/surfaces/views", "get", "The views (windows) in focus order and the followed instance",
           "Each view shows one active instance; the followed instance is the active instance of the front view that "
           "has one, else (headless) the selected instance, else the sole one. A WebAPI call without an id acts on it "
           "(T-1). followed is a target object",
           "{ok, followed, views: [{id, name, owner, position, active, index}], revision}");
        Json::Value& addView = op("/api/v1/surfaces/views", "post", "Register a view of the automation plane",
                                  "Body {name, active?: id | index, activate?: bool}. For a window of another process "
                                  "and for tests; it stays until DELETE. The Qt clients register their own windows",
                                  "{ok, view, followed}");
        addView["responses"]["404"]["description"] = "Not Found: no such instance";
        Json::Value& putView = op("/api/v1/surfaces/views/{view}", "put", "Change a view",
                                  "Body {active?: id | index | \"\" (only a view of the automation plane), activate?: "
                                  "true (to the front of the focus order, as a focus would)}",
                                  "{ok, view, followed}");
        param(putView, "view", "path", true, "The view id (v1)");
        putView["responses"]["409"]["description"] = "Conflict: the active instance of a window is its client's";
        Json::Value& deleteView = op("/api/v1/surfaces/views/{view}", "delete", "Remove a view of the automation plane",
                                     "Its view: pins fall back to following, with a notice", "{ok, removed, followed}");
        param(deleteView, "view", "path", true, "The view id");
        deleteView["responses"]["409"]["description"] = "Conflict: a window of a client";

        Json::Value& target = op("/api/v1/surfaces/target", "get", "A target: the followed instance or a session's",
                                 targetObject, "{ok, target, session?}");
        param(target, "session", "query", false, "A surface session id; default: the followed instance");

        op("/api/v1/surfaces/sessions", "get", "The surface sessions and their targets",
           "Each surface (a Stream Deck, one session per serial; a REST surface): id, name, kind, serial, pin (follow, "
           "view:<id>, instance:<id>), target, notice (why a pin fell back)",
           "{ok, sessions: [...]}");
        Json::Value& openSession = op("/api/v1/surfaces/sessions", "post", "Open a surface session of the automation plane",
                                      "Body {name, kind?: api, serial?, target?: follow | view:<id> | instance:<id> | "
                                      "{view} | {instance}}. A REST surface that wants a target of its own; it stays "
                                      "until DELETE. A deck's session comes with the surface topic's join",
                                      "{ok, session}");
        openSession["responses"]["400"]["description"] = "Bad Request: no name, a target that does not parse";
        Json::Value& getSession = op("/api/v1/surfaces/sessions/{session}", "get", "One surface session", "",
                                     "{ok, session}");
        param(getSession, "session", "path", true, "The session id (s1)");
        Json::Value& closeSession = op("/api/v1/surfaces/sessions/{session}", "delete",
                                       "Close a surface session of the automation plane", "", "{ok, closed}");
        param(closeSession, "session", "path", true, "The session id");
        closeSession["responses"]["409"]["description"] = "Conflict: a session of a surface (it closes it itself)";
        Json::Value& pin = op("/api/v1/surfaces/sessions/{session}/pin", "put", "Pin or unpin a surface session",
                              "Body {target: follow | view:<id> | instance:<id|index> | {view} | {instance}}. A view pin "
                              "follows that window's active instance; an instance pin stays on it; follow unpins",
                              "{ok, session}");
        param(pin, "session", "path", true, "The session id");
        pin["responses"]["404"]["description"] = "Not Found: no such session, view or instance";
        pin["responses"]["409"]["description"] = "not_now: a view pin on a headless host (no views)";

        Json::Value& readouts = op("/api/v1/surfaces/readouts", "get", "The readouts' values now",
                                   "The vocabulary a surface draws from, all from published state: ttd.position, "
                                   "run.state, machine, media.tape, media.disk, audio.mute, debug.mode, toggle.<id> "
                                   "(toggle.feature.<name>, toggle.set.<name>). The surface topic pushes their changes",
                                   "{ok, instance, values: {name: value}, unknown: []}");
        param(readouts, "instance", "query", false, "Emulator id or index; default: the followed instance");
        param(readouts, "names", "query", false, "Names or prefixes, comma separated (run.state,media.*); default all");

        Json::Value& activity = op("/api/v1/surfaces/activity", "get", "The activity feed: who ran which action",
                                   "Display only: each action the registry ran, with its value, origin (gui, "
                                   "host_device, companion, webapi, mcp, cli, lua, python), surface name and result; "
                                   "deltas of one surface within 100 ms are one entry. The ring keeps 256",
                                   "{ok, entries: [{seq, instance, id, value, origin, surface, ok, error?, t_ms, "
                                   "merged}], last, dropped}");
        param(activity, "since", "query", false, "Entries after this seq (the previous answer's last)");
        param(activity, "instance", "query", false, "Only this instance's");
        param(activity, "limit", "query", false, "At most this many, the newest (1..256, 64)");
    }

