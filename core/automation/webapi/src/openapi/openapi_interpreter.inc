    // ==========================================
    // Python Interpreter Endpoints
    // ==========================================

    // GET /api/v1/python/status - Get Python interpreter status
    paths["/api/v1/python/status"]["get"]["summary"] = "Get Python interpreter status";
    paths["/api/v1/python/status"]["get"]["tags"].append("Interpreter Control");
    paths["/api/v1/python/status"]["get"]["description"] =
        "Returns Python interpreter availability, initialization state, version, and thread information.";
    paths["/api/v1/python/status"]["get"]["responses"]["200"]["description"] = "Python status";
    paths["/api/v1/python/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["type"] =
        "object";
    paths["/api/v1/python/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["available"]["type"] = "boolean";
    paths["/api/v1/python/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["initialized"]["type"] = "boolean";
    paths["/api/v1/python/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["status"]["type"] = "string";

    // POST /api/v1/python/exec - Execute Python code
    paths["/api/v1/python/exec"]["post"]["summary"] = "Execute Python code";
    paths["/api/v1/python/exec"]["post"]["tags"].append("Interpreter Control");
    paths["/api/v1/python/exec"]["post"]["description"] =
        "Execute arbitrary Python code in the embedded interpreter. Returns captured stdout and success status.";
    paths["/api/v1/python/exec"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/python/exec"]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/python/exec"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["code"]
         ["type"] = "string";
    paths["/api/v1/python/exec"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["code"]
         ["description"] = "Python code to execute";
    paths["/api/v1/python/exec"]["post"]["responses"]["200"]["description"] = "Code executed";
    paths["/api/v1/python/exec"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["success"]["type"] = "boolean";
    paths["/api/v1/python/exec"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["output"]["type"] = "string";
    paths["/api/v1/python/exec"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["error"]["type"] = "string";

    // POST /api/v1/python/file - Execute Python file
    paths["/api/v1/python/file"]["post"]["summary"] = "Execute Python file";
    paths["/api/v1/python/file"]["post"]["tags"].append("Interpreter Control");
    paths["/api/v1/python/file"]["post"]["description"] = "Execute a Python script file by path.";
    paths["/api/v1/python/file"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/python/file"]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/python/file"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["path"]
         ["type"] = "string";
    paths["/api/v1/python/file"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["path"]
         ["description"] = "Absolute path to Python script file";
    paths["/api/v1/python/file"]["post"]["responses"]["200"]["description"] = "File executed";

    // POST /api/v1/python/stop - Stop Python execution
    paths["/api/v1/python/stop"]["post"]["summary"] = "Stop Python execution";
    paths["/api/v1/python/stop"]["post"]["tags"].append("Interpreter Control");
    paths["/api/v1/python/stop"]["post"]["description"] =
        "Attempt to interrupt long-running Python execution. Uses cooperative shutdown via stop flag.";
    paths["/api/v1/python/stop"]["post"]["responses"]["200"]["description"] = "Stop signal sent";

    // ==========================================
    // Lua Interpreter Endpoints
    // ==========================================

    // GET /api/v1/lua/status - Get Lua interpreter status
    paths["/api/v1/lua/status"]["get"]["summary"] = "Get Lua interpreter status";
    paths["/api/v1/lua/status"]["get"]["tags"].append("Interpreter Control");
    paths["/api/v1/lua/status"]["get"]["description"] =
        "Returns Lua interpreter availability, initialization state, version, and thread information.";
    paths["/api/v1/lua/status"]["get"]["responses"]["200"]["description"] = "Lua status";
    paths["/api/v1/lua/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/lua/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["available"]["type"] = "boolean";
    paths["/api/v1/lua/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["initialized"]["type"] = "boolean";
    paths["/api/v1/lua/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["status"]["type"] = "string";

    // POST /api/v1/lua/exec - Execute Lua code
    paths["/api/v1/lua/exec"]["post"]["summary"] = "Execute Lua code";
    paths["/api/v1/lua/exec"]["post"]["tags"].append("Interpreter Control");
    paths["/api/v1/lua/exec"]["post"]["description"] =
        "Execute arbitrary Lua code in the embedded interpreter. Returns captured output and success status.";
    paths["/api/v1/lua/exec"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/lua/exec"]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/lua/exec"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["code"]
         ["type"] = "string";
    paths["/api/v1/lua/exec"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["code"]
         ["description"] = "Lua code to execute";
    paths["/api/v1/lua/exec"]["post"]["responses"]["200"]["description"] = "Code executed";
    paths["/api/v1/lua/exec"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["success"]["type"] = "boolean";
    paths["/api/v1/lua/exec"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["output"]["type"] = "string";
    paths["/api/v1/lua/exec"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["error"]["type"] = "string";

    // POST /api/v1/lua/file - Execute Lua file
    paths["/api/v1/lua/file"]["post"]["summary"] = "Execute Lua file";
    paths["/api/v1/lua/file"]["post"]["tags"].append("Interpreter Control");
    paths["/api/v1/lua/file"]["post"]["description"] = "Execute a Lua script file by path.";
    paths["/api/v1/lua/file"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/lua/file"]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/lua/file"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["path"]
         ["type"] = "string";
    paths["/api/v1/lua/file"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["path"]
         ["description"] = "Absolute path to Lua script file";
    paths["/api/v1/lua/file"]["post"]["responses"]["200"]["description"] = "File executed";

    // POST /api/v1/lua/stop - Stop Lua execution
    paths["/api/v1/lua/stop"]["post"]["summary"] = "Stop Lua execution";
    paths["/api/v1/lua/stop"]["post"]["tags"].append("Interpreter Control");
    paths["/api/v1/lua/stop"]["post"]["description"] =
        "Attempt to interrupt long-running Lua execution. Uses cooperative shutdown via stop flag.";
    paths["/api/v1/lua/stop"]["post"]["responses"]["200"]["description"] = "Stop signal sent";

