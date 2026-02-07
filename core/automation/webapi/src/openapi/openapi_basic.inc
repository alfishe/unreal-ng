    // BASIC Control endpoints
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["summary"] = "Execute BASIC command";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["tags"].append("BASIC Control");
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["description"] =
        "Inject a command into BASIC edit buffer AND execute it via simulated ENTER key. "
        "If no command specified, executes RUN. "
        "Automatically handles 128K menu navigation if needed. "
        "Returns error if TR-DOS is active or not in BASIC editor.";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["command"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["command"]["description"] = "BASIC command to execute (e.g., 'RUN', 'LIST', 'PRINT 1+1')";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["200"]["description"] =
        "Command injected and executed";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["success"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["success"]["description"] = "True if command was injected and executed";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["message"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["message"]["description"] = "Human-readable result or error message";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["command"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["basic_mode"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["basic_mode"]["description"] = "Detected BASIC mode: '48K', '128K', 'trdos', or 'unknown'";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["400"]["description"] =
        "Not in BASIC editor, TR-DOS active, or other injection error";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["404"]["description"] = "Emulator not found";

    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["summary"] = "Inject BASIC program into memory";
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["tags"].append("BASIC Control");
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["description"] =
        "Inject a multi-line BASIC program into memory without executing. "
        "Uses loadProgram() to tokenize and write to program area. "
        "Lines should be separated by newlines and include line numbers.";
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["program"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["program"]["description"] = "BASIC program text (e.g., '10 PRINT \"HELLO\"\\n20 GOTO 10')";
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["responses"]["200"]["description"] =
        "Program injected successfully";
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["responses"]["400"]["description"] =
        "Missing program parameter or injection failed";

    paths["/api/v1/emulator/{id}/basic/extract"]["get"]["summary"] = "Extract BASIC program";
    paths["/api/v1/emulator/{id}/basic/extract"]["get"]["tags"].append("BASIC Control");
    paths["/api/v1/emulator/{id}/basic/extract"]["get"]["description"] =
        "Extract the current BASIC program from emulator memory as plain text.";
    paths["/api/v1/emulator/{id}/basic/extract"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/basic/extract"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/basic/extract"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/basic/extract"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/extract"]["get"]["responses"]["200"]["description"] = "BASIC program as text";

    paths["/api/v1/emulator/{id}/basic/clear"]["post"]["summary"] = "Clear BASIC program";
    paths["/api/v1/emulator/{id}/basic/clear"]["post"]["tags"].append("BASIC Control");
    paths["/api/v1/emulator/{id}/basic/clear"]["post"]["description"] =
        "Clear the BASIC program in memory (equivalent to NEW command).";
    paths["/api/v1/emulator/{id}/basic/clear"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/basic/clear"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/basic/clear"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/basic/clear"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/clear"]["post"]["responses"]["200"]["description"] = "Program cleared";

    paths["/api/v1/emulator/{id}/basic/state"]["get"]["summary"] = "Get BASIC environment state";
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["tags"].append("BASIC Control");
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["description"] =
        "Get the current BASIC environment state including mode (48K/128K), menu vs editor, "
        "TR-DOS state, and readiness for commands.";
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["responses"]["200"]["description"] = "BASIC state information";
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["state"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["state"]["description"] =
             "State: 'basic48k', 'basic128k', 'menu128k', 'trdos_active', 'trdos_sos_call', 'unknown'";
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["in_editor"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["ready_for_commands"]["type"] = "boolean";

    // POST /api/v1/emulator/{id}/basic/mode - Set BASIC mode
    paths["/api/v1/emulator/{id}/basic/mode"]["post"]["summary"] = "Set BASIC mode";
    paths["/api/v1/emulator/{id}/basic/mode"]["post"]["tags"].append("BASIC Control");
    paths["/api/v1/emulator/{id}/basic/mode"]["post"]["description"] =
        "Force navigation to a specific BASIC mode (48K or 128K editor). "
        "Handles 128K menu navigation automatically if switching modes.";
    paths["/api/v1/emulator/{id}/basic/mode"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/basic/mode"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/basic/mode"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/basic/mode"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/mode"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["mode"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/mode"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["mode"]["description"] = "Target mode: '48k' or '128k'";
    paths["/api/v1/emulator/{id}/basic/mode"]["post"]["responses"]["200"]["description"] = "Mode set successfully";
    paths["/api/v1/emulator/{id}/basic/mode"]["post"]["responses"]["400"]["description"] = "Invalid mode or unable to switch";
    paths["/api/v1/emulator/{id}/basic/mode"]["post"]["responses"]["404"]["description"] = "Emulator not found";

