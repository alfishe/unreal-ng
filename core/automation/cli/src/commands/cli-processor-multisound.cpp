#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/sound/midi/midicontrol.h>
#include <emulator/state/devicestate.h>
#include <emulator/state/statenodejson.h>

#include <string>

#include "cli-multisound.h"
#include "cli-processor.h"

/// region <ZX-MultiSound and MIDI>

// multisound [--full|--json], midi [--json], midi panic: thin adapters over DeviceState::MultiSound / Midi and
// MidiControl (cli-multisound.h). ZX-MultiSound tdd-integration.md §5

namespace
{
    std::string Lines(const std::string& text, const char* newline)
    {
        std::string out;
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

void CLIProcessor::HandleMultiSound(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }
    const StateNode report = DeviceState::MultiSound(emulator->GetContext());
    const std::string mode = args.empty() ? "" : args[0];
    if (mode == "--json")
        session.SendResponse(StateNodeToJsonText(report) + NEWLINE);
    else if (mode == "--full")
        session.SendResponse(Lines(DeviceState::ToText(report), NEWLINE));
    else if (mode.empty())
        session.SendResponse(Lines(CliMultiSound::Summary(report), NEWLINE));
    else
        session.SendResponse(std::string("Usage: multisound [--full|--json]") + NEWLINE);
}

void CLIProcessor::HandleMidi(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }
    const std::string verb = args.empty() ? "state" : args[0];
    if (verb == "panic")
    {
        const MidiControlReply reply = MidiControl::Execute(emulator->GetContext(), "panic");
        session.SendResponse((reply.ok ? std::string() : std::string("Error: ")) + reply.message + NEWLINE);
        return;
    }
    if (verb == "state" || verb == "--json")
    {
        const StateNode report = DeviceState::Midi(emulator->GetContext());
        const bool json = verb == "--json" || (args.size() > 1 && args[1] == "--json");
        session.SendResponse(json ? StateNodeToJsonText(report) + NEWLINE : Lines(CliMultiSound::MidiText(report), NEWLINE));
        return;
    }
    session.SendResponse(std::string("Usage: midi [state] [--json] | midi panic") + NEWLINE);
}

/// endregion </ZX-MultiSound and MIDI>
