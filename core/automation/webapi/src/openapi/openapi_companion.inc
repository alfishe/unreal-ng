    // Companion clients (docs/inprogress/2026-10-16-companion-client/protocol.md): a paired device acts as the user's
    // hands. Pairing: the device asks, the user approves at the GUI (no route approves), the device collects its token
    // once. With Authorization: Bearer <token> its changes open no session, within its scopes
    {
        auto param = [&](const char* path, const char* method, const char* name) {
            Json::Value p;
            p["name"] = name;
            p["in"] = "path";
            p["required"] = true;
            p["schema"]["type"] = "string";
            paths[path][method]["parameters"].append(p);
        };
        auto bearer = [&](const char* path, const char* method) {
            Json::Value requirement;
            requirement["companionBearer"] = Json::Value(Json::arrayValue);
            paths[path][method]["security"].append(requirement);
        };
        auto op = [&](const char* path, const char* method, const char* summary, const char* description) {
            paths[path][method]["summary"] = summary;
            paths[path][method]["tags"].append("Companions");
            paths[path][method]["description"] = description;
        };

        const char* kList = "/api/v1/companions";
        op(kList, "get", "List companions and pending pairings",
           "companions (id, name, device, scopes, source paired | environment, paired_ms, last_seen_ms), pairings "
           "(pairing_id, name, device, peer, expires_in, scopes) and approval_available (a GUI can approve), pairing_allowed (the user accepts requests). Never a "
           "token or a code.");
        paths[kList]["get"]["responses"]["200"]["description"] = "The companions and pairings";
        op(kList, "delete", "Revoke every companion", "Their tokens stop working at once.");
        paths[kList]["delete"]["responses"]["200"]["description"] = "revoked: the number revoked";

        const char* kItem = "/api/v1/companions/{cid}";
        op(kItem, "post", "Request a pairing (cid = pairings)",
           "POST /api/v1/companions/pairings with {name (1-64 characters), device?, scopes? (run, input, media, "
           "machine, rewind, debug, takeback, all; default: all)}. 202: {pairing_id, code (six digits to show on "
           "the device), expires_in (120), scopes}. The user compares the code and allows it at the GUI.");
        param(kItem, "post", "cid");
        paths[kItem]["post"]["responses"]["202"]["description"] = "Pairing requested: poll it";
        paths[kItem]["post"]["responses"]["400"]["description"] = "bad_request: no name, an unknown scope";
        paths[kItem]["post"]["responses"]["429"]["description"] =
            "too_many_pairings: 4 pending, or one from this address already (retry: true)";
        paths[kItem]["post"]["responses"]["403"]["description"] =
            "pairing_disabled: the user turned pairing requests off at the GUI";
        paths[kItem]["post"]["responses"]["503"]["description"] =
            "pairing_unavailable: no GUI to approve; provision companions in UNREAL_COMPANION_TOKENS";
        op(kItem, "get", "The companion itself (cid = me) or one companion",
           "GET /api/v1/companions/me with the companion's token: {companion, emulators: [{id, index, model, state, "
           "selected, control: {held, surface, client, peer, since_ms, implicit, can_act, can_take_back}, actions: "
           "{pause, resume, step, ...: {available, scope, reason}}}]} - what the companion may do now. Poll it for a "
           "live status. Any other cid: that companion (id or name).");
        param(kItem, "get", "cid");
        bearer(kItem, "get");
        paths[kItem]["get"]["responses"]["200"]["description"] = "The companion";
        paths[kItem]["get"]["responses"]["401"]["description"] = "companion_required / companion_unknown (me)";
        paths[kItem]["get"]["responses"]["404"]["description"] = "unknown_companion";
        op(kItem, "delete", "Revoke a companion (cid = me: unpair itself)", "By id or name.");
        param(kItem, "delete", "cid");
        paths[kItem]["delete"]["responses"]["200"]["description"] = "revoked, id";
        paths[kItem]["delete"]["responses"]["404"]["description"] = "unknown_companion";

        const char* kPairing = "/api/v1/companions/pairings/{pid}";
        op(kPairing, "get", "Poll a pairing",
           "state: pending (expires_in) | approved (token, companion_id, scopes - delivered once, the pairing is gone "
           "after this answer) | denied | expired. Poll once a second.");
        param(kPairing, "get", "pid");
        paths[kPairing]["get"]["responses"]["200"]["description"] = "The pairing's state";
        paths[kPairing]["get"]["responses"]["404"]["description"] =
            "unknown_pairing: a wrong id, or the token was already collected";
        op(kPairing, "delete", "Cancel a pairing", "The GUI prompt closes.");
        param(kPairing, "delete", "pid");
        paths[kPairing]["delete"]["responses"]["200"]["description"] = "cancelled";
        paths[kPairing]["delete"]["responses"]["404"]["description"] = "unknown_pairing";

        const char* kTakeBack = "/api/v1/emulator/{id}/control/take-back";
        op(kTakeBack, "post", "Take control back (companion)",
           "A companion with the takeback scope ends the session that holds the instance, as the GUI's Take back "
           "control: its direct run stops, its client gets revoked_by_user and a re-take cooldown.");
        param(kTakeBack, "post", "id");
        bearer(kTakeBack, "post");
        paths[kTakeBack]["post"]["responses"]["200"]["description"] = "taken_back, from {surface, client}";
        paths[kTakeBack]["post"]["responses"]["401"]["description"] = "companion_required / companion_unknown";
        paths[kTakeBack]["post"]["responses"]["403"]["description"] = "companion_forbidden: no takeback scope";
        paths[kTakeBack]["post"]["responses"]["409"]["description"] = "nothing_held";

        const char* kAllow = "/api/v1/emulator/{id}/control/allow-again";
        op(kAllow, "post", "Allow re-takes (companion)",
           "A companion with the takeback scope lifts the re-take cooldowns, as the GUI's Allow again.");
        param(kAllow, "post", "id");
        bearer(kAllow, "post");
        paths[kAllow]["post"]["responses"]["200"]["description"] = "allowed";
        paths[kAllow]["post"]["responses"]["401"]["description"] = "companion_required / companion_unknown";
        paths[kAllow]["post"]["responses"]["403"]["description"] = "companion_forbidden: no takeback scope";
    }
