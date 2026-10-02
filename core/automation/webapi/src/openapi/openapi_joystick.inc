    // Kempston Joystick Injection endpoints (implementation: api/joystick_api.cpp, logic: common/joystickjson.h)
    // Design: docs/inprogress/2026-09-15-atm-baseconf-highres-ports/tdd-kempston-joystick.md §5
    {
        static const char* kJoystickUnits =
            " Buttons: up, down, left, right, fire, b5, b6, b7 (case-insensitive); several as one string "
            "('up+fire', 'up,fire') or as an array. The device byte is active high: right=0x01 left=0x02 "
            "down=0x04 up=0x08 fire=0x10; the guest reads it at IN #1F (ATM3 outside shadow mode, Scorpion "
            "#FF1F, TS-Conf). Out-of-range values are rejected (400), never clamped. A running machine applies the "
            "change at its next instruction; a paused one applies it on its next executed instruction (run_frames, "
            "step, resume), so the 'state' in the reply is the state before that. The guest reacts when it next "
            "reads the port, so run frames before checking the screen. A fitted-but-unwired machine or a not-fitted joystick answers 200 with a 'warning'.";

        auto addJoystickIdParam = [](Json::Value& op) {
            op["parameters"][0]["name"] = "id";
            op["parameters"][0]["in"] = "path";
            op["parameters"][0]["required"] = true;
            op["parameters"][0]["schema"]["type"] = "string";
            op["tags"].append("Joystick Injection");
        };

        auto addJoystickResponses = [](Json::Value& op, const std::string& okDescription, bool isInput) {
            op["responses"]["200"]["description"] = okDescription;
            if (isInput)
            {
                op["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
                    "#/components/schemas/JoystickInjectResponse";
                op["responses"]["400"]["description"] =
                    "Missing field, wrong JSON type, unknown button name or value out of range";
                op["responses"]["409"]["description"] = "TTD replay in progress; live joystick input refused";
            }
            op["responses"]["404"]["description"] = "Emulator not found";
            op["responses"]["500"]["description"] = "Joystick manager or device not available";
        };

        auto buttonsProperty = [](Json::Value& schema, const std::string& description) {
            Json::Value& buttons = schema["properties"]["buttons"];
            buttons["oneOf"][0]["type"] = "string";
            buttons["oneOf"][1]["type"] = "array";
            buttons["oneOf"][1]["items"]["type"] = "string";
            for (const char* name : {"up", "down", "left", "right", "fire", "b5", "b6", "b7"})
                buttons["oneOf"][1]["items"]["enum"].append(name);
            buttons["description"] = description;
        };

        // POST /api/v1/emulator/{id}/joystick/press
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/joystick/press"]["post"];
            addJoystickIdParam(op);
            op["summary"] = "Press and hold joystick buttons";
            op["description"] =
                std::string("Press Kempston joystick buttons; they stay held until released.") + kJoystickUnits;
            op["requestBody"]["required"] = true;
            Json::Value& schema = op["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            buttonsProperty(schema, "Buttons to hold ('button' is accepted as an alias of 'buttons')");
            schema["required"].append("buttons");
            addJoystickResponses(op, "Buttons pressed; response includes the resulting state", true);
        }

        // POST /api/v1/emulator/{id}/joystick/release
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/joystick/release"]["post"];
            addJoystickIdParam(op);
            op["summary"] = "Release joystick buttons";
            op["description"] = std::string("Release Kempston joystick buttons (cancels a pending tap on them).") +
                                kJoystickUnits;
            op["requestBody"]["required"] = true;
            Json::Value& schema = op["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            buttonsProperty(schema, "Buttons to release");
            schema["required"].append("buttons");
            addJoystickResponses(op, "Buttons released", true);
        }

        // POST /api/v1/emulator/{id}/joystick/set
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/joystick/set"]["post"];
            addJoystickIdParam(op);
            op["summary"] = "Set exactly the held joystick buttons";
            op["description"] =
                std::string("Set the whole device byte: either 'state' (0..255, D5..D7 included) or 'buttons' "
                            "(the listed buttons are held, all others released; an empty list releases all). "
                            "Cancels a pending tap. Recorded by the TTD journal.") +
                kJoystickUnits;
            op["requestBody"]["required"] = true;
            Json::Value& schema = op["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            schema["properties"]["state"]["type"] = "integer";
            schema["properties"]["state"]["minimum"] = 0;
            schema["properties"]["state"]["maximum"] = 255;
            schema["properties"]["state"]["description"] = "The device byte (active high)";
            buttonsProperty(schema, "Alternative to 'state': the buttons to hold");
            addJoystickResponses(op, "State set", true);
        }

        // POST /api/v1/emulator/{id}/joystick/tap
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/joystick/tap"]["post"];
            addJoystickIdParam(op);
            op["summary"] = "Tap joystick buttons";
            op["description"] = std::string("Press buttons now and release them after 'frames' emulated frames. "
                                            "A new tap replaces a pending one.") +
                                kJoystickUnits;
            op["requestBody"]["required"] = true;
            Json::Value& schema = op["requestBody"]["content"]["application/json"]["schema"];
            schema["type"] = "object";
            buttonsProperty(schema, "Buttons to tap");
            schema["properties"]["frames"]["type"] = "integer";
            schema["properties"]["frames"]["minimum"] = 1;
            schema["properties"]["frames"]["maximum"] = 65535;
            schema["properties"]["frames"]["default"] = 2;
            schema["properties"]["frames"]["description"] = "Hold time in emulated frames";
            schema["required"].append("buttons");
            addJoystickResponses(op, "Buttons pressed; release pending", true);
        }

        // GET /api/v1/emulator/{id}/joystick
        {
            Json::Value& op = paths["/api/v1/emulator/{id}/joystick"]["get"];
            addJoystickIdParam(op);
            op["summary"] = "Get joystick state";
            op["description"] =
                "Kempston joystick device byte, held buttons, the byte IN #1F returns from the device, whether "
                "this machine's port decoder answers the joystick ('wired'), the host key bindings and a pending "
                "tap. 'warning' is set when the guest cannot see the buttons (not fitted, not wired).";
            addJoystickResponses(op, "Joystick state", false);
            op["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
                "#/components/schemas/JoystickStatus";
        }
    }
