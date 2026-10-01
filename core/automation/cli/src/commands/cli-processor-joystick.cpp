#include "cli-processor.h"
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <debugger/debugmanager.h>
#include <debugger/joystick/debugjoystickmanager.h>

#include "cli-joystick-format.h"

/// region <Joystick Injection Commands>

void CLIProcessor::HandleJoystick(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pDebugManager)
    {
        session.SendResponse(std::string("Error: Unable to access emulator context.") + NEWLINE);
        return;
    }

    // Help needs no device
    if (args.empty() || args[0] == "help" || args[0] == "-h" || args[0] == "--help")
    {
        ShowJoystickHelp(session);
        return;
    }

    DebugJoystickManager* joystick = context->pDebugManager->GetJoystickManager();
    if (!joystick)
    {
        session.SendResponse(std::string("Error: Joystick manager not available.") + NEWLINE);
        return;
    }

    session.SendResponse(CliJoystick::Execute(*joystick, args, NEWLINE));
}

void CLIProcessor::ShowJoystickHelp(const ClientSession& session)
{
    session.SendResponse(CliJoystick::Help(NEWLINE));
}

/// endregion </Joystick Injection Commands>
