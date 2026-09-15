#include "cli-processor.h"
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <debugger/debugmanager.h>
#include <debugger/mouse/debugmousemanager.h>

#include "cli-mouse-format.h"

#include <algorithm>
#include <sstream>

/// region <Mouse Injection Commands>

namespace
{
std::string WithWarning(const std::string& line, const MouseInjectResult& result)
{
    std::string text = line + CLIProcessor::NEWLINE;
    if (!result.warning.empty())
        text += "Warning: " + result.warning + CLIProcessor::NEWLINE;
    return text;
}

std::string ErrorLine(const std::string& message)
{
    return "Error: " + message + CLIProcessor::NEWLINE;
}
}  // namespace

void CLIProcessor::HandleMouse(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(ErrorLine("No emulator selected."));
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pDebugManager)
    {
        session.SendResponse(ErrorLine("Unable to access emulator context."));
        return;
    }

    if (args.empty())
    {
        ShowMouseHelp(session);
        return;
    }

    std::string subcommand = args[0];
    std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);

    if (subcommand == "help" || subcommand == "-h" || subcommand == "--help")
    {
        ShowMouseHelp(session);
        return;
    }

    DebugMouseManager* mouse = context->pDebugManager->GetMouseManager();
    if (!mouse)
    {
        session.SendResponse(ErrorLine("Mouse manager not available."));
        return;
    }

    std::string error;

    auto resolveButton = [&](const std::string& usage, MouseButton& button) -> bool {
        if (args.size() < 2)
        {
            session.SendResponse(ErrorLine("Missing button name. Usage: " + usage));
            return false;
        }
        const auto resolved = DebugMouseManager::ResolveButtonName(args[1]);
        if (!resolved)
        {
            session.SendResponse(ErrorLine(CliMouse::UnknownButtonError(args[1])));
            return false;
        }
        button = *resolved;
        return true;
    };

    if (subcommand == "move")
    {
        int dx = 0;
        int dy = 0;
        if (args.size() < 3)
        {
            session.SendResponse(ErrorLine("Missing arguments. Usage: mouse move <dx> <dy>"));
            return;
        }
        if (!CliMouse::ParseIntArg(args[1], "dx", dx, error) || !CliMouse::ParseIntArg(args[2], "dy", dy, error))
        {
            session.SendResponse(ErrorLine(error));
            return;
        }
        const MouseInjectResult result = mouse->Move(dx, dy);
        if (!result.ok())
        {
            session.SendResponse(ErrorLine(result.message));
            return;
        }
        session.SendResponse(WithWarning(CliMouse::FormatMoveLine(dx, dy, mouse->GetState()), result));
    }
    else if (subcommand == "press" || subcommand == "release")
    {
        MouseButton button = MouseButton::Left;
        if (!resolveButton("mouse " + subcommand + " <button>", button))
            return;
        const bool press = subcommand == "press";
        const MouseInjectResult result = press ? mouse->PressButton(button) : mouse->ReleaseButton(button);
        if (!result.ok())
        {
            session.SendResponse(ErrorLine(result.message));
            return;
        }
        session.SendResponse(WithWarning(CliMouse::FormatButtonLine(press ? "Pressed" : "Released",
                                                                    DebugMouseManager::GetButtonName(button),
                                                                    mouse->GetState()),
                                         result));
    }
    else if (subcommand == "click")
    {
        MouseButton button = MouseButton::Left;
        if (!resolveButton("mouse click <button> [frames]", button))
            return;
        uint32_t frames = DebugMouseManager::DEFAULT_CLICK_FRAMES;
        if (args.size() >= 3 && !CliMouse::ParseFramesArg(args[2], frames, error))
        {
            session.SendResponse(ErrorLine(error));
            return;
        }
        const MouseInjectResult result = mouse->Click(button, frames);
        if (!result.ok())
        {
            session.SendResponse(ErrorLine(result.message));
            return;
        }
        session.SendResponse(WithWarning(
            CliMouse::FormatButtonLine("Clicked",
                                       DebugMouseManager::GetButtonName(button) + " for " + std::to_string(frames) +
                                           " frames",
                                       mouse->GetState()),
            result));
    }
    else if (subcommand == "buttons")
    {
        uint8_t pressedBits = 0;
        const std::vector<std::string> tokens(args.begin() + 1, args.end());
        if (!CliMouse::ParseButtonList(tokens, pressedBits, error))
        {
            session.SendResponse(ErrorLine(error));
            return;
        }
        const MouseInjectResult result = mouse->SetPressedButtons(pressedBits);
        if (!result.ok())
        {
            session.SendResponse(ErrorLine(result.message));
            return;
        }
        session.SendResponse(WithWarning(
            CliMouse::FormatButtonLine("Buttons set", CliMouse::FormatPressedNames(pressedBits), mouse->GetState()),
            result));
    }
    else if (subcommand == "wheel")
    {
        int steps = 0;
        if (args.size() < 2)
        {
            session.SendResponse(ErrorLine("Missing steps. Usage: mouse wheel <steps>"));
            return;
        }
        if (!CliMouse::ParseIntArg(args[1], "steps", steps, error))
        {
            session.SendResponse(ErrorLine(error));
            return;
        }
        const MouseInjectResult result = mouse->Wheel(steps);
        if (!result.ok())
        {
            session.SendResponse(ErrorLine(result.message));
            return;
        }
        session.SendResponse(WithWarning(CliMouse::FormatWheelLine(steps, mouse->GetState()), result));
    }
    else if (subcommand == "clear" || subcommand == "release_all")
    {
        const MouseInjectResult result = mouse->ReleaseAllButtons();
        if (!result.ok())
        {
            session.SendResponse(ErrorLine(result.message));
            return;
        }
        session.SendResponse(
            WithWarning(CliMouse::FormatButtonLine("Released all", "none", mouse->GetState()), result));
    }
    else if (subcommand == "status" || subcommand == "info")
    {
        session.SendResponse(CliMouse::FormatStatus(mouse->GetState(), NEWLINE));
    }
    else if (subcommand == "set")
    {
        int x = 0;
        int y = 0;
        if (args.size() < 3)
        {
            session.SendResponse(ErrorLine("Missing arguments. Usage: mouse set <x> <y>"));
            return;
        }
        if (!CliMouse::ParseIntArg(args[1], "x", x, error) || !CliMouse::ParseIntArg(args[2], "y", y, error))
        {
            session.SendResponse(ErrorLine(error));
            return;
        }
        const MouseInjectResult result = mouse->SetCounters(x, y);
        if (!result.ok())
        {
            session.SendResponse(ErrorLine(result.message));
            return;
        }
        session.SendResponse(WithWarning(CliMouse::FormatCountersLine(mouse->GetState()), result));
    }
    else
    {
        session.SendResponse(ErrorLine("Unknown subcommand '" + subcommand + "'") +
                             "Use 'mouse help' for available commands" + NEWLINE);
    }
}

void CLIProcessor::ShowMouseHelp(const ClientSession& session)
{
    std::stringstream ss;
    ss << "Usage: mouse <subcommand> [args]" << NEWLINE;
    ss << NEWLINE;
    ss << "Subcommands:" << NEWLINE;
    ss << "  move <dx> <dy>          - Move by dx,dy emulated pixels (+x right, +y up; -127..127)" << NEWLINE;
    ss << "  press <button>          - Press and hold a button (left|right|middle, or l|r|m)" << NEWLINE;
    ss << "  release <button>        - Release a button" << NEWLINE;
    ss << "  click <button> [frames] - Press, hold for frames (default 2), release" << NEWLINE;
    ss << "  buttons <none|b1,b2..>  - Set exactly which buttons are pressed" << NEWLINE;
    ss << "  wheel <steps>           - Scroll wheel -7..7 (+ = away from you)" << NEWLINE;
    ss << "  clear                   - Release all buttons, cancel pending click" << NEWLINE;
    ss << "  status                  - Show counters, buttons, wheel, port values" << NEWLINE;
    ss << "  set <x> <y>             - Debug: write raw X/Y counters (0..255)" << NEWLINE;
    ss << NEWLINE;
    ss << "The mouse is relative: programs track their own cursor from counter changes." << NEWLINE;
    ss << "For reproducible results: pause, inject, then 'run_frames N'." << NEWLINE;
    ss << NEWLINE;
    ss << "Examples:" << NEWLINE;
    ss << "  mouse move 10 -5        - 10 right, 5 down" << NEWLINE;
    ss << "  mouse click left        - Click left button (2 frames)" << NEWLINE;
    ss << "  mouse click right 5     - Hold right button for 5 frames" << NEWLINE;
    ss << "  mouse buttons left,middle" << NEWLINE;
    ss << "  mouse wheel -1          - One notch towards you" << NEWLINE;

    session.SendResponse(ss.str());
}

/// endregion </Mouse Injection Commands>
