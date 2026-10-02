    // Joystick Injection schemas (api/joystick_api.cpp)
    schemas["JoystickState"]["type"] = "object";
    schemas["JoystickState"]["description"] = "Kempston joystick state";
    schemas["JoystickState"]["properties"]["available"]["type"] = "boolean";
    schemas["JoystickState"]["properties"]["available"]["description"] = "A joystick device exists in this emulator";
    schemas["JoystickState"]["properties"]["present"]["type"] = "boolean";
    schemas["JoystickState"]["properties"]["present"]["description"] =
        "Joystick fitted (config and feature); when false the guest reads 0x00 on the port";
    schemas["JoystickState"]["properties"]["wired"]["type"] = "boolean";
    schemas["JoystickState"]["properties"]["wired"]["description"] =
        "This machine's port decoder answers a Kempston joystick";
    schemas["JoystickState"]["properties"]["state"]["type"] = "integer";
    schemas["JoystickState"]["properties"]["state"]["description"] =
        "Device byte, active high: right=1 left=2 down=4 up=8 fire=0x10, D5..D7 raw";
    schemas["JoystickState"]["properties"]["port_value"]["type"] = "integer";
    schemas["JoystickState"]["properties"]["port_value"]["description"] = "What IN #1F returns from the device now";
    schemas["JoystickState"]["properties"]["buttons"]["type"] = "object";
    schemas["JoystickState"]["properties"]["buttons"]["description"] = "One boolean per button name";
    for (const char* button : {"up", "down", "left", "right", "fire", "b5", "b6", "b7"})
        schemas["JoystickState"]["properties"]["buttons"]["properties"][button]["type"] = "boolean";
    schemas["JoystickState"]["properties"]["pressed"]["type"] = "array";
    schemas["JoystickState"]["properties"]["pressed"]["items"]["type"] = "string";
    schemas["JoystickState"]["properties"]["button_names"]["type"] = "array";
    schemas["JoystickState"]["properties"]["button_names"]["items"]["type"] = "string";
    schemas["JoystickState"]["properties"]["keys"]["type"] = "string";
    schemas["JoystickState"]["properties"]["keys"]["description"] =
        "Host key bindings, 'up:kp_8,...' (empty = no host keys)";
    schemas["JoystickState"]["properties"]["pending_tap"]["type"] = "object";
    schemas["JoystickState"]["properties"]["pending_tap"]["nullable"] = true;
    schemas["JoystickState"]["properties"]["pending_tap"]["properties"]["mask"]["type"] = "integer";
    schemas["JoystickState"]["properties"]["pending_tap"]["properties"]["frames_left"]["type"] = "integer";

    schemas["JoystickStatus"]["description"] = "Response from GET /joystick";
    schemas["JoystickStatus"]["allOf"][0]["$ref"] = "#/components/schemas/JoystickState";
    schemas["JoystickStatus"]["allOf"][1]["type"] = "object";
    schemas["JoystickStatus"]["allOf"][1]["properties"]["emulator_id"]["type"] = "string";
    schemas["JoystickStatus"]["allOf"][1]["properties"]["warning"]["type"] = "string";

    schemas["JoystickInjectResponse"]["type"] = "object";
    schemas["JoystickInjectResponse"]["description"] =
        "Response from a joystick input POST: echoes the input fields plus the resulting state";
    schemas["JoystickInjectResponse"]["properties"]["success"]["type"] = "boolean";
    schemas["JoystickInjectResponse"]["properties"]["message"]["type"] = "string";
    schemas["JoystickInjectResponse"]["properties"]["warning"]["type"] = "string";
    schemas["JoystickInjectResponse"]["properties"]["warning"]["description"] =
        "Set when the input cannot reach the guest (joystick not fitted, or the machine does not decode it)";
    schemas["JoystickInjectResponse"]["properties"]["state"]["$ref"] = "#/components/schemas/JoystickState";
    schemas["JoystickInjectResponse"]["properties"]["buttons"]["type"] = "string";
    schemas["JoystickInjectResponse"]["properties"]["buttons"]["description"] =
        "Echo of the buttons of press / release / tap, as one list string";
    schemas["JoystickInjectResponse"]["properties"]["frames"]["type"] = "integer";
    schemas["JoystickInjectResponse"]["properties"]["frames"]["description"] = "Echo of the hold time of tap";
    schemas["JoystickInjectResponse"]["properties"]["requested_state"]["type"] = "integer";
    schemas["JoystickInjectResponse"]["properties"]["requested_state"]["description"] =
        "Echo of the byte set by 'set' ('state' is the resulting-state object)";
