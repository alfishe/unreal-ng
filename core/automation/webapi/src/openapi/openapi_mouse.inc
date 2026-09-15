    // Kempston Mouse Injection endpoints (implementation: api/mouse_api.cpp)
    // Design: docs/inprogress/2026-09-12-kempston-mouse/automation-interfaces.md §4.4
    {
        static const char* kMouseUnits =
            " Units: dx/dy in emulated pixels (+dx = right, +dy = up), wheel steps in notches "
            "(+ = away from user), click hold time in emulated frames. Limits per call: dx/dy -127..127, "
            "steps -7..7 (not 0), frames 1..65535 (default 2), counters 0..255. Out-of-range values are "
            "rejected (400), never clamped. The change is applied before the response returns; the guest "
            "reacts when it next reads the mouse ports, so run frames before checking the screen.";

        auto addMouseIdParam = [](Json::Value& op) {
            op["parameters"][0]["name"] = "id";
            op["parameters"][0]["in"] = "path";
            op["parameters"][0]["required"] = true;
            op["parameters"][0]["schema"]["type"] = "string";
            op["tags"].append("Mouse Injection");
        };

        // Common responses. isInput=false for GET routes (no 400/409 there).
        auto addMouseResponses = [](Json::Value& op, const std::string& okDescription, bool isInput) {
            op["responses"]["200"]["description"] = okDescription;
            if (isInput)
            {
                op["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
                    "#/components/schemas/MouseInjectResponse";
                op["responses"]["400"]["description"] =
                    "Missing field, wrong JSON type, unknown button name or value out of range";
                op["responses"]["409"]["description"] = "TTD replay in progress; live mouse input refused";
            }
            op["responses"]["404"]["description"] = "Emulator not found";
            op["responses"]["500"]["description"] = "Mouse manager not available";
        };

        auto intProperty = [](Json::Value& schema, const char* name, int min, int max, const std::string& text) {
            schema["properties"][name]["type"] = "integer";
            schema["properties"][name]["minimum"] = min;
            schema["properties"][name]["maximum"] = max;
            schema["properties"][name]["description"] = text;
        };

        auto buttonProperty = [](Json::Value& schema) {
            Json::Value& button = schema["properties"]["button"];
            button["type"] = "string";
            for (const char* name : {"left", "right", "middle", "l", "r", "m"})
                button["enum"].append(name);
            button["description"] = "Mouse button name (case-insensitive)";
            schema["required"].append("button");
        };

        // POST /api/v1/emulator/{id}/mouse/move
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/mouse/move"]["post"];
            addMouseIdParam(op);
            op["summary"] = "Move the Kempston mouse (relative)";
            op["description"] = std::string("Add dx/dy to the Kempston mouse X/Y counters (wrap mod 256). "
                                            "Either field may be omitted (= 0) but at least one must be non-zero.") +
                                kMouseUnits;
            op["requestBody"]["required"] = true;
            Json::Value& schema = op["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            intProperty(schema, "dx", -127, 127, "Emulated pixels, + = right");
            intProperty(schema, "dy", -127, 127, "Emulated pixels, + = up");
            addMouseResponses(op, "Mouse moved; response includes the resulting state", true);
        }

        // POST /api/v1/emulator/{id}/mouse/press
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/mouse/press"]["post"];
            addMouseIdParam(op);
            op["summary"] = "Press and hold a mouse button";
            op["description"] = std::string("Press a Kempston mouse button; it stays pressed until released.") +
                                kMouseUnits;
            op["requestBody"]["required"] = true;
            Json::Value& schema = op["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            buttonProperty(schema);
            addMouseResponses(op, "Button pressed", true);
        }

        // POST /api/v1/emulator/{id}/mouse/release
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/mouse/release"]["post"];
            addMouseIdParam(op);
            op["summary"] = "Release a held mouse button";
            op["description"] =
                std::string("Release a Kempston mouse button (cancels a pending click on it).") + kMouseUnits;
            op["requestBody"]["required"] = true;
            Json::Value& schema = op["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            buttonProperty(schema);
            addMouseResponses(op, "Button released", true);
        }

        // POST /api/v1/emulator/{id}/mouse/click
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/mouse/click"]["post"];
            addMouseIdParam(op);
            op["summary"] = "Click a mouse button";
            op["description"] = std::string("Press a Kempston mouse button now and release it after 'frames' "
                                            "emulated frames. A new click replaces a pending one.") +
                                kMouseUnits;
            op["requestBody"]["required"] = true;
            Json::Value& schema = op["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            buttonProperty(schema);
            intProperty(schema, "frames", 1, 65535, "Hold time in emulated frames");
            schema["properties"]["frames"]["default"] = 2;
            addMouseResponses(op, "Button pressed; release pending", true);
        }

        // POST /api/v1/emulator/{id}/mouse/buttons
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/mouse/buttons"]["post"];
            addMouseIdParam(op);
            op["summary"] = "Set which mouse buttons are pressed";
            op["description"] = std::string("Set the complete Kempston mouse button state: listed buttons are "
                                            "pressed, all others released. [] releases all.") +
                                kMouseUnits;
            op["requestBody"]["required"] = true;
            Json::Value& schema = op["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            Json::Value& pressed = schema["properties"]["pressed"];
            pressed["type"] = "array";
            pressed["items"]["type"] = "string";
            for (const char* name : {"left", "right", "middle", "l", "r", "m"})
                pressed["items"]["enum"].append(name);
            pressed["description"] = "Buttons to hold pressed";
            schema["required"].append("pressed");
            addMouseResponses(op, "Button state set", true);
        }

        // GET /api/v1/emulator/{id}/mouse/buttons
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/mouse/buttons"]["get"];
            addMouseIdParam(op);
            op["summary"] = "List valid mouse button names";
            op["description"] = "Button names accepted by the mouse routes, with their one-letter aliases.";
            addMouseResponses(op, "Button names", false);
        }

        // POST /api/v1/emulator/{id}/mouse/wheel
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/mouse/wheel"]["post"];
            addMouseIdParam(op);
            op["summary"] = "Scroll the mouse wheel";
            op["description"] = std::string("Add steps to the 4-bit wheel counter (wrap mod 16). A warning is "
                                            "returned when no wheel is fitted.") +
                                kMouseUnits;
            op["requestBody"]["required"] = true;
            Json::Value& schema = op["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            intProperty(schema, "steps", -7, 7, "Notches, + = away from user, 0 rejected");
            schema["required"].append("steps");
            addMouseResponses(op, "Wheel scrolled", true);
        }

        // POST /api/v1/emulator/{id}/mouse/release_all
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/mouse/release_all"]["post"];
            addMouseIdParam(op);
            op["summary"] = "Release all mouse buttons";
            op["description"] =
                std::string("Release every Kempston mouse button and cancel a pending click.") + kMouseUnits;
            addMouseResponses(op, "All buttons released", true);
        }

        // POST /api/v1/emulator/{id}/mouse/counters
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/mouse/counters"]["post"];
            addMouseIdParam(op);
            op["summary"] = "Set raw mouse counters (debug)";
            op["description"] = std::string("Write the raw X/Y counter values, e.g. to restore a known state "
                                            "before a test. Not a 'move pointer to screen position' command. "
                                            "Recorded by the TTD journal.") +
                                kMouseUnits;
            op["requestBody"]["required"] = true;
            Json::Value& schema = op["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            intProperty(schema, "x", 0, 255, "Raw X counter value");
            intProperty(schema, "y", 0, 255, "Raw Y counter value");
            schema["required"].append("x");
            schema["required"].append("y");
            addMouseResponses(op, "Counters set", true);
        }

        // GET /api/v1/emulator/{id}/mouse/status
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/mouse/status"]["get"];
            addMouseIdParam(op);
            op["summary"] = "Get mouse state";
            op["description"] = "Kempston mouse counters, buttons, wheel, the bytes the guest reads from ports "
                                "#FADF/#FBDF/#FFDF, pending click and presence.";
            addMouseResponses(op, "Mouse state", false);
            op["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
                "#/components/schemas/MouseStatus";
        }
    }
