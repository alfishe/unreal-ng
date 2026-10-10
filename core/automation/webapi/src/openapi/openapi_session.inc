    // Automation sessions (execution arbiter design §6a): the session is the emulator's run control. While one is open,
    // the GUI shows it and locks its controls; the user can take control back, and from then on every request of the
    // session (the X-Unreal-Session header) is answered 409 revoked_by_user with retry: false - stop, do not retry, do
    // not take control again without asking the user (a re-take by the same client is refused revoked_cooldown)
    {
        const char* kPath = "/api/v1/emulator/{id}/control/session";
        auto idParam = [&](const char* method) {
            paths[kPath][method]["parameters"][0]["name"] = "id";
            paths[kPath][method]["parameters"][0]["in"] = "path";
            paths[kPath][method]["parameters"][0]["required"] = true;
            paths[kPath][method]["parameters"][0]["schema"]["type"] = "string";
        };
        paths[kPath]["post"]["summary"] = "Open an automation session (take run control)";
        paths[kPath]["post"]["tags"].append("Execution");
        paths[kPath]["post"]["description"] =
            "Takes the emulator's run control for this client. Body (optional): client (a name, shown to the user and "
            "the key of the re-take cooldown), surface (webapi | mcp), lease_seconds (default 60; the session ends "
            "after that long without a request of it; 0 = none). Send the returned session id in the "
            "X-Unreal-Session header on every request: those requests resume and step while other surfaces' runs are "
            "refused. 409 held_by_other or revoked_cooldown, both retry: false.";
        idParam("post");
        paths[kPath]["post"]["responses"]["200"]["description"] =
            "Session opened: session (the id for X-Unreal-Session), surface, client, since_ms, lease_seconds";
        paths[kPath]["post"]["responses"]["409"]["description"] =
            "held_by_other (another surface holds run control) or revoked_cooldown (the user took control back from "
            "this client; it may not take it again until the user allows it): retry is false";

        paths[kPath]["get"]["summary"] = "Get the automation session";
        paths[kPath]["get"]["tags"].append("Execution");
        paths[kPath]["get"]["description"] =
            "held, surface, client, since_ms, lease_seconds, retake_cooldowns (clients the user took control back "
            "from and the seconds left).";
        idParam("get");
        paths[kPath]["get"]["responses"]["200"]["description"] = "The session state";

        paths[kPath]["delete"]["summary"] = "End the automation session";
        paths[kPath]["delete"]["tags"].append("Execution");
        paths[kPath]["delete"]["description"] =
            "Ends the session in the X-Unreal-Session header (released: false when it was not the holder, e.g. the user "
            "took it back already).";
        idParam("delete");
        paths[kPath]["delete"]["responses"]["200"]["description"] = "released";
        paths[kPath]["delete"]["responses"]["400"]["description"] = "No X-Unreal-Session header";
    }
