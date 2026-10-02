    // Keyboard Injection endpoints
    auto addKeyboardIdParam = [&](const std::string& path, const std::string& method) {
        paths[path][method]["parameters"][0]["name"] = "id";
        paths[path][method]["parameters"][0]["in"] = "path";
        paths[path][method]["parameters"][0]["required"] = true;
        paths[path][method]["parameters"][0]["schema"]["type"] = "string";
        paths[path][method]["tags"].append("Keyboard Injection");
    };

    // POST /api/v1/emulator/{id}/keyboard/tap
    addKeyboardIdParam("/api/v1/emulator/{id}/keyboard/tap", "post");
    paths["/api/v1/emulator/{id}/keyboard/tap"]["post"]["summary"] = "Tap a single key";
    paths["/api/v1/emulator/{id}/keyboard/tap"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["key"]["type"] = "string";
    paths["/api/v1/emulator/{id}/keyboard/tap"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["frames"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/keyboard/tap"]["post"]["responses"]["200"]["description"] = "Key tapped";

    // POST /api/v1/emulator/{id}/keyboard/combo
    addKeyboardIdParam("/api/v1/emulator/{id}/keyboard/combo", "post");
    paths["/api/v1/emulator/{id}/keyboard/combo"]["post"]["summary"] = "Tap a key combo";
    paths["/api/v1/emulator/{id}/keyboard/combo"]["post"]["description"] =
        "Each name in 'keys' may be a ZX matrix key or a physical PC key with no ZX "
        "equivalent at all (e.g. 'f12', 'rshift'), mixed freely - needed for host-level "
        "combos such as TS-Conf's BIOS Setup entry, Right Shift + F12 ({\"keys\": [\"rshift\", \"f12\"]}).";
    paths["/api/v1/emulator/{id}/keyboard/combo"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["keys"]["type"] = "array";
    paths["/api/v1/emulator/{id}/keyboard/combo"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["keys"]["items"]["type"] = "string";
    paths["/api/v1/emulator/{id}/keyboard/combo"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["frames"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/keyboard/combo"]["post"]["responses"]["200"]["description"] = "Combo tapped";

    // POST /api/v1/emulator/{id}/keyboard/type
    addKeyboardIdParam("/api/v1/emulator/{id}/keyboard/type", "post");
    paths["/api/v1/emulator/{id}/keyboard/type"]["post"]["summary"] = "Type text sequence";
    paths["/api/v1/emulator/{id}/keyboard/type"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["text"]["type"] = "string";
    paths["/api/v1/emulator/{id}/keyboard/type"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["delay_frames"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/keyboard/type"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["tokenized"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/keyboard/type"]["post"]["responses"]["200"]["description"] = "Text queued";

    // POST /api/v1/emulator/{id}/keyboard/macro
    addKeyboardIdParam("/api/v1/emulator/{id}/keyboard/macro", "post");
    paths["/api/v1/emulator/{id}/keyboard/macro"]["post"]["summary"] = "Execute predefined macro";
    paths["/api/v1/emulator/{id}/keyboard/macro"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["name"]["type"] = "string";
    paths["/api/v1/emulator/{id}/keyboard/macro"]["post"]["responses"]["200"]["description"] = "Macro queued";

    // POST /api/v1/emulator/{id}/keyboard/release_all
    addKeyboardIdParam("/api/v1/emulator/{id}/keyboard/release_all", "post");
    paths["/api/v1/emulator/{id}/keyboard/release_all"]["post"]["summary"] = "Release all keys";
    paths["/api/v1/emulator/{id}/keyboard/release_all"]["post"]["responses"]["200"]["description"] =
        "All keys released";

    // GET /api/v1/emulator/{id}/keyboard/status
    addKeyboardIdParam("/api/v1/emulator/{id}/keyboard/status", "get");
    paths["/api/v1/emulator/{id}/keyboard/status"]["get"]["summary"] = "Get keyboard status";
    paths["/api/v1/emulator/{id}/keyboard/status"]["get"]["responses"]["200"]["description"] =
        "Keyboard status: sequence_running, host_route (as set) and host_route_effective (AUTO resolved), ps2_controller";

    // POST /api/v1/emulator/{id}/keyboard/route
    addKeyboardIdParam("/api/v1/emulator/{id}/keyboard/route", "post");
    paths["/api/v1/emulator/{id}/keyboard/route"]["post"]["summary"] = "Route the host keyboard";
    paths["/api/v1/emulator/{id}/keyboard/route"]["post"]["tags"].append("Keyboard Injection");
    paths["/api/v1/emulator/{id}/keyboard/route"]["post"]["description"] =
        "Where host and injected keys go: \"matrix\" (the ZX keyboard matrix), \"ps2\" (the PS/2 keyboard "
        "controller: ZX-Evo AVR, ATM Turbo 2+ keyboard controller), \"both\", or \"auto\" (both where a PS/2 "
        "controller exists, the matrix elsewhere; [INPUT] HostKeyboard= in the machine INI). Injected ZX keys reach "
        "the PS/2 side as the PC keys they stand for. Refused (409) while a TTD recording runs.";
    paths["/api/v1/emulator/{id}/keyboard/route"]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/emulator/{id}/keyboard/route"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["route"]["type"] = "string";
    paths["/api/v1/emulator/{id}/keyboard/route"]["post"]["responses"]["200"]["description"] = "The route in force";
    paths["/api/v1/emulator/{id}/keyboard/route"]["post"]["responses"]["400"]["description"] = "Unknown route";
    paths["/api/v1/emulator/{id}/keyboard/route"]["post"]["responses"]["409"]["description"] = "A TTD recording runs";

    // GET /api/v1/emulator/{id}/keyboard/keys
    addKeyboardIdParam("/api/v1/emulator/{id}/keyboard/keys", "get");
    paths["/api/v1/emulator/{id}/keyboard/keys"]["get"]["summary"] = "List valid keys";
    paths["/api/v1/emulator/{id}/keyboard/keys"]["get"]["responses"]["200"]["description"] = "Validated key names";


    // POST /api/v1/emulator/{id}/keyboard/press - Press and hold a key
    paths["/api/v1/emulator/{id}/keyboard/press"]["post"]["summary"] = "Press and hold a key";
    paths["/api/v1/emulator/{id}/keyboard/press"]["post"]["tags"].append("Keyboard Injection");
    paths["/api/v1/emulator/{id}/keyboard/press"]["post"]["description"] =
        "Press and hold a key in the keyboard matrix (stays pressed until released).";
    paths["/api/v1/emulator/{id}/keyboard/press"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/keyboard/press"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/keyboard/press"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/keyboard/press"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/keyboard/press"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/keyboard/press"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["key"]["type"] = "string";
    paths["/api/v1/emulator/{id}/keyboard/press"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["key"]["description"] = "Key name (e.g., 'a', 'enter', 'cs'), or a physical PC key name for a PS/2 machine such as the ZX-Evo ('f1', 'home', 'esc', 'pc.up')";
    paths["/api/v1/emulator/{id}/keyboard/press"]["post"]["responses"]["200"]["description"] = "Key pressed";
    paths["/api/v1/emulator/{id}/keyboard/press"]["post"]["responses"]["400"]["description"] = "Invalid key name";
    paths["/api/v1/emulator/{id}/keyboard/press"]["post"]["responses"]["404"]["description"] = "Emulator not found";

    // POST /api/v1/emulator/{id}/keyboard/release - Release a held key
    paths["/api/v1/emulator/{id}/keyboard/release"]["post"]["summary"] = "Release a held key";
    paths["/api/v1/emulator/{id}/keyboard/release"]["post"]["tags"].append("Keyboard Injection");
    paths["/api/v1/emulator/{id}/keyboard/release"]["post"]["description"] =
        "Release a previously pressed key from the keyboard matrix.";
    paths["/api/v1/emulator/{id}/keyboard/release"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/keyboard/release"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/keyboard/release"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/keyboard/release"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/keyboard/release"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/keyboard/release"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["key"]["type"] = "string";
    paths["/api/v1/emulator/{id}/keyboard/release"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["key"]["description"] = "Key name to release (ZX or PC key name)";
    paths["/api/v1/emulator/{id}/keyboard/release"]["post"]["responses"]["200"]["description"] = "Key released";
    paths["/api/v1/emulator/{id}/keyboard/release"]["post"]["responses"]["400"]["description"] = "Invalid key name";
    paths["/api/v1/emulator/{id}/keyboard/release"]["post"]["responses"]["404"]["description"] = "Emulator not found";

    // POST /api/v1/emulator/{id}/keyboard/abort - Abort pending keyboard operations
    paths["/api/v1/emulator/{id}/keyboard/abort"]["post"]["summary"] = "Abort pending keyboard operations";
    paths["/api/v1/emulator/{id}/keyboard/abort"]["post"]["tags"].append("Keyboard Injection");
    paths["/api/v1/emulator/{id}/keyboard/abort"]["post"]["description"] =
        "Cancel all pending keyboard injection operations (type, macro sequences).";
    paths["/api/v1/emulator/{id}/keyboard/abort"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/keyboard/abort"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/keyboard/abort"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/keyboard/abort"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/keyboard/abort"]["post"]["responses"]["200"]["description"] = "Pending operations aborted";
    paths["/api/v1/emulator/{id}/keyboard/abort"]["post"]["responses"]["404"]["description"] = "Emulator not found";

