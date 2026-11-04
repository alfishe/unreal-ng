// Frame watches on the CLI (docs/inprogress/2026-11-03-run-until-watches): run_until key=value ... and
// watch add|list|del. The verbs and the text are core framewatch's, as every other plane's

#include "cli-processor.h"

#include <debugger/watch/framewatch.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/execution/runcontrol.h>

namespace
{
/// key=value tokens (a bare number: max_frames). False with `error` for a token without '='
bool KeyValues(const std::vector<std::string>& args, size_t from, std::vector<std::pair<std::string, std::string>>& out,
               std::string& error)
{
    for (size_t i = from; i < args.size(); i++)
    {
        const std::string& token = args[i];
        const size_t eq = token.find('=');
        if (eq == std::string::npos)
        {
            if (!token.empty() && token.find_first_not_of("0123456789") == std::string::npos)
            {
                out.emplace_back("max_frames", token);
                continue;
            }
            error = "expected key=value, got '" + token + "'";
            return false;
        }
        std::string value = token.substr(eq + 1);
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
            value = value.substr(1, value.size() - 2);
        out.emplace_back(token.substr(0, eq), value);
    }
    return true;
}

const char* kRunUntilUsage =
    "Usage: run_until [max_frames] key=value ...   (whole frames until the rules hold at a frame end; leaves it paused)\n"
    "  if=<expr>          frame-end condition: M(x), W(x), registers, FRAME, x@prev, labels ('M(#5C3A)<M(#5C3A)@prev')\n"
    "  text=<text>        the text is on screen (ROM-font OCR; the text layer in text modes)\n"
    "  screen=changed     the screen differs from the start; screen=stable [stable_frames=N, default 25]: still N frames\n"
    "  event=<name>       a game event of the profile fires\n"
    "  match=any|all      any rule (default) or all at the same frame end\n"
    "  max_frames=N       1..200000, default 3000; breakpoints=true: a breakpoint on the way ends it\n"
    "Example: run_until text=\"1982 Sinclair\" max_frames=300";

const char* kWatchUsage =
    "Usage: watch add key=value ...  arm a watch on the running machine (the run_until rules plus label=, then=pause|notify,\n"
    "                                once=true|false); a hit pauses at that frame end and is pushed as watch_hit\n"
    "       watch list                the armed watches with their hits\n"
    "       watch del <id|all>        remove";
}  // namespace

void CLIProcessor::HandleRunUntil(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    if (args.empty() || args[0] == "help" || args[0] == "-h" || args[0] == "--help")
    {
        session.SendResponse(ClientSession::FormatForTerminal(kRunUntilUsage));
        return;
    }
    std::vector<std::pair<std::string, std::string>> options;
    std::string error;
    framewatch::Spec spec;
    unsigned maxFrames = 0;
    bool breakpoints = false;
    if (!KeyValues(args, 0, options, error) || !framewatch::RunOptions(options, spec, maxFrames, breakpoints, error))
    {
        session.SendResponse("Error: " + error + NEWLINE);
        return;
    }
    std::shared_ptr<Emulator> shared = EmulatorManager::GetInstance()->GetEmulator(emulator->GetId());
    if (!shared)
    {
        session.SendResponse("Error: the emulator is gone" + std::string(NEWLINE));
        return;
    }
    const execution::RunReply reply =
        execution::RunControl(shared).RunUntil(CliRequester(shared->GetContext()), spec, maxFrames, breakpoints);
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + reply.message + NEWLINE);
        return;
    }
    session.SendResponse(reply.message + NEWLINE);
}

void CLIProcessor::HandleWatch(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    EmulatorContext* context = emulator->GetContext();
    const std::string verb = args.empty() ? "list" : args[0];
    framewatch::Outcome outcome;
    if (verb == "list" || verb == "ls")
        outcome = framewatch::ListWatches(context);
    else if (verb == "add")
    {
        std::vector<std::pair<std::string, std::string>> options;
        std::string error;
        if (!KeyValues(args, 1, options, error))
        {
            session.SendResponse("Error: " + error + NEWLINE);
            return;
        }
        outcome = framewatch::AddWatch(context, options);
    }
    else if ((verb == "del" || verb == "delete" || verb == "remove" || verb == "rm") && args.size() == 2)
        outcome = framewatch::RemoveWatch(context, args[1]);
    else
    {
        session.SendResponse(ClientSession::FormatForTerminal(kWatchUsage));
        return;
    }
    session.SendResponse((outcome.status >= 400 ? "Error: " : "") + outcome.text + NEWLINE);
}
