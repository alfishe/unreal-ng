// CLI `jobs` (PLAN #122 S2): the long verbs run as jobs through jobs::Execute (emulator/jobs/jobcontrol.h), the same
// actions as GET /api/v1/jobs, GET /api/v1/jobs/{job}?wait_ms=, POST /api/v1/jobs/{job}/cancel,
// POST /api/v1/emulator/{id}/jobs and MCP `jobs`

#include "cli-jobs.h"
#include "cli-processor.h"

#include <emulator/emulator.h>
#include <emulator/jobs/jobcontrol.h>
#include <emulator/state/statenodejson.h>

void CLIProcessor::HandleJobs(const ClientSession& session, const std::vector<std::string>& args)
{
    std::string action;
    std::map<std::string, std::string> options;
    bool json = false;
    std::string error;
    if (!CliJobs::ParseArgs(args, action, options, json, error))
    {
        session.SendResponse("Error: " + error + NEWLINE + CliJobs::kUsage + NEWLINE);
        return;
    }
    // The selected instance: list's default filter, start's target (none: every instance's jobs)
    auto emulator = GetSelectedEmulator(session);
    const jobs::Reply reply = jobs::Execute(emulator.get(), action, options);
    if (json)
    {
        session.SendResponse(StateNodeToJsonText(reply.ToValue()) + NEWLINE);
        return;
    }
    session.SendResponse(FormatForTerminal(reply.Ok() ? reply.text : "Error: " + reply.message) + NEWLINE);
}
