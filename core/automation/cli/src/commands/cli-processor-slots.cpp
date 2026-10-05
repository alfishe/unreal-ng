#include <emulator/emulator.h>
#include <emulator/slots/slotcontrol.h>

#include <string>

#include "cli-processor.h"
#include "cli-slots.h"

/// region <Slots>

// slots [verb] ... : a thin adapter over SlotControl (core/src/emulator/slots/slotcontrol.h), see cli-slots.h.
// ZX-bus slots architecture.md §9

namespace
{
    /// "\n" -> the CLI's line end
    std::string Lines(const std::string& text, const char* newline)
    {
        std::string out;
        out.reserve(text.size() + text.size() / 16);
        for (char c : text)
        {
            if (c == '\n')
                out += newline;
            else
                out += c;
        }
        return out;
    }
}  // namespace

void CLIProcessor::HandleSlots(const ClientSession& session, const std::vector<std::string>& args)
{
    if (!args.empty() && (args[0] == "help" || args[0] == "--help"))
    {
        session.SendResponse(Lines(CliSlots::Usage(), NEWLINE));
        return;
    }
    SlotControlRequest request;
    bool json = false;
    std::string error;
    if (!CliSlots::ParseArgs(args, request, json, error))
    {
        session.SendResponse(Lines("Error: " + error + "\n" + CliSlots::Usage(), NEWLINE));
        return;
    }
    if (request.verb != "matrix")
    {
        auto emulator = GetSelectedEmulator(session);
        if (!emulator)
        {
            session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
            return;
        }
        request.emulatorId = emulator->GetId();
    }
    // Nothing here may keep the old machine alive across a restart
    _emulator.reset();

    const SlotControlReply reply = SlotControl::Execute(request);
    session.SendResponse(Lines(CliSlots::Render(request, reply, json), NEWLINE));
}

/// endregion </Slots>
