    // ==========================================
    // Batch Execution Endpoints
    // ==========================================

    // POST /api/v1/batch/execute
    paths["/api/v1/batch/execute"]["post"]["summary"] = "Execute batch commands in parallel";
    paths["/api/v1/batch/execute"]["post"]["description"] =
        "Execute multiple commands across emulator instances using a 4-thread pool. ~2-3ms for 48 instances.";
    paths["/api/v1/batch/execute"]["post"]["tags"].append("Batch Execution");
    paths["/api/v1/batch/execute"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/batch/execute"]["post"]["requestBody"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/BatchExecuteRequest";
    paths["/api/v1/batch/execute"]["post"]["responses"]["200"]["description"] = "All commands succeeded";
    paths["/api/v1/batch/execute"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/BatchResult";
    paths["/api/v1/batch/execute"]["post"]["responses"]["207"]["description"] =
        "Partial success (some commands failed)";
    paths["/api/v1/batch/execute"]["post"]["responses"]["207"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/BatchResult";
    paths["/api/v1/batch/execute"]["post"]["responses"]["400"]["description"] =
        "Invalid request (missing emulator/command, command not batchable)";

    // GET /api/v1/batch/commands
    paths["/api/v1/batch/commands"]["get"]["summary"] = "List batchable commands";
    paths["/api/v1/batch/commands"]["get"]["description"] =
        "Returns list of command names that can be used in batch execution";
    paths["/api/v1/batch/commands"]["get"]["tags"].append("Batch Execution");
    paths["/api/v1/batch/commands"]["get"]["responses"]["200"]["description"] = "List of batchable commands";
    paths["/api/v1/batch/commands"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/BatchableCommandsResponse";
