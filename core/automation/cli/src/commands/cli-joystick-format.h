#pragma once

/// Kempston joystick CLI: argument parsing, command execution and output formatting.
/// Header-only with no socket or CLIProcessor dependency, so core-tests can run the exact
/// command text against a real DebugJoystickManager (joystick TDD §5, JOY-13). The
/// CLIProcessor handler only resolves the emulator and sends the returned text.
/// Every range check and every message comes from DebugJoystickManager, as for the other surfaces.

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "debugger/joystick/debugjoystickmanager.h"

namespace CliJoystick
{

inline std::string Hex2(uint8_t value)
{
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "0x%02X", value);
    return buffer;
}

inline std::string Lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/// Whole token as an integer: decimal, 0x.. / #.. hex. Rejects empty input and trailing junk.
inline bool ParseInteger(const std::string& text, long long& out)
{
    if (text.empty() || std::isspace(static_cast<unsigned char>(text.front())))
        return false;

    int base = 10;
    size_t skip = 0;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        base = 16;
        skip = 2;
    }
    else if (text.size() > 1 && text[0] == '#')
    {
        base = 16;
        skip = 1;
    }

    errno = 0;
    char* end = nullptr;
    const long long value = std::strtoll(text.c_str() + skip, &end, base);
    if (end == text.c_str() + skip || *end != '\0' || errno == ERANGE)
        return false;
    out = value;
    return true;
}

/// Tokens after the verb joined into one list: "up fire" and "up,fire" and "up+fire" are the same
inline std::string JoinNames(const std::vector<std::string>& args, size_t from, size_t to)
{
    std::string joined;
    for (size_t i = from; i < to && i < args.size(); i++)
        joined += (joined.empty() ? "" : ",") + args[i];
    return joined;
}

inline std::string ErrorLine(const std::string& message, const std::string& newline)
{
    return "Error: " + message + newline;
}

/// "up,fire" or "none"
inline std::string FormatButtons(const std::vector<std::string>& buttons)
{
    std::string text;
    for (const std::string& name : buttons)
        text += (text.empty() ? "" : ",") + name;
    return text.empty() ? "none" : text;
}

/// "Joystick pressed: up -> state=0x08 buttons=up (IN #1F=0x08)" plus the optional warning line
inline std::string FormatResult(const JoystickInjectResult& result, const JoystickStateSnapshot& state,
                                const std::string& newline)
{
    std::string text = result.message + " -> state=" + Hex2(state.state) + " buttons=" +
                       FormatButtons(state.buttons) + " (IN #1F=" + Hex2(state.portValue) + ")" + newline;
    if (!result.warning.empty())
        text += "Warning: " + result.warning + newline;
    return text;
}

/// Multi-line "joystick status" block
inline std::string FormatStatus(const JoystickStateSnapshot& state, const std::string& newline)
{
    if (!state.available)
        return "Kempston Joystick [not available]" + newline;

    std::string text;
    text += std::string("Kempston Joystick [") + (state.present ? "present" : "absent") + "]" + newline;
    text += "  State: " + Hex2(state.state) + " (" + FormatButtons(state.buttons) + ")" + newline;
    text += "  Port: IN #1F=" + Hex2(state.portValue) + newline;
    text += std::string("  Routing: ") +
            (state.wired ? "this machine decodes the Kempston joystick port"
                         : "this machine does not decode a Kempston joystick port") +
            newline;
    text += "  Host keys: " + (state.keys.empty() ? std::string("none") : state.keys) + newline;
    if (state.pendingTapMask != 0)
    {
        text += "  Pending tap: mask " + Hex2(state.pendingTapMask) + ", " + std::to_string(state.pendingTapFramesLeft) +
                " frame(s) left" + newline;
    }
    else
    {
        text += "  Pending tap: none" + newline;
    }
    return text;
}

inline std::string FormatList(const std::string& newline)
{
    std::string text = "Joystick buttons:" + newline;
    const std::vector<std::string> names = DebugJoystickManager::GetAllButtonNames();
    for (const std::string& name : names)
    {
        text += "  " + name + " (" + Hex2(DebugJoystickManager::ResolveButtonNames(name)) + ")" + newline;
    }
    text += "Several buttons: up+fire or up,fire or 'up fire'" + newline;
    return text;
}

inline std::string Help(const std::string& newline)
{
    std::string text;
    text += "Usage: joystick <subcommand> [args]" + newline + newline;
    text += "Subcommands:" + newline;
    text += "  press <buttons>          - Press and hold (up|down|left|right|fire|b5|b6|b7, several: up+fire)" + newline;
    text += "  release <buttons>        - Release buttons" + newline;
    text += "  set <state|buttons|none> - Set exactly the held buttons: a byte (0..255, 0x.. hex) or a list" + newline;
    text += "  tap <buttons> [frames]   - Press, hold for frames (default 2), release" + newline;
    text += "  clear                    - Release everything, cancel a pending tap" + newline;
    text += "  status                   - Show state, port value, routing, host keys, pending tap" + newline;
    text += "  list                     - List the button names" + newline + newline;
    text += "Kempston joystick: active-high byte read at IN #1F (right=1 left=2 down=4 up=8 fire=0x10)." + newline;
    text += "Only machines that decode the port (ATM3, Scorpion, TS-Conf) let the guest see it; others get a warning." + newline;
    text += "For reproducible results: pause, inject, then 'run_frames N'." + newline + newline;
    text += "Examples:" + newline;
    text += "  joystick press up+fire   - Hold up and fire" + newline;
    text += "  joystick tap fire 5      - Fire for 5 frames" + newline;
    text += "  joystick set 0x18        - Exactly up and fire" + newline;
    text += "  joystick set none        - Release everything" + newline;
    return text;
}

/// Run `joystick <args...>` (args exclude the word "joystick"). Always returns the text to send.
inline std::string Execute(DebugJoystickManager& joystick, const std::vector<std::string>& args,
                           const std::string& newline)
{
    if (args.empty())
        return Help(newline);

    const std::string verb = Lower(args[0]);
    if (verb == "help" || verb == "-h" || verb == "--help")
        return Help(newline);

    auto finish = [&](const JoystickInjectResult& result) {
        if (!result.ok())
            return ErrorLine(result.message, newline);
        return FormatResult(result, joystick.GetState(), newline);
    };

    if (verb == "press" || verb == "release")
    {
        if (args.size() < 2)
            return ErrorLine("Missing button name. Usage: joystick " + verb + " <buttons>", newline);
        const std::string names = JoinNames(args, 1, args.size());
        return finish(verb == "press" ? joystick.Press(names) : joystick.Release(names));
    }
    if (verb == "tap")
    {
        if (args.size() < 2)
            return ErrorLine("Missing button name. Usage: joystick tap <buttons> [frames]", newline);

        // A trailing integer is the frame count; the rest are button names
        long long frames = DebugJoystickManager::DEFAULT_TAP_FRAMES;
        size_t nameEnd = args.size();
        long long parsed = 0;
        if (args.size() >= 3 && ParseInteger(args.back(), parsed))
        {
            frames = parsed;
            nameEnd = args.size() - 1;
        }
        else if (args.size() >= 3 && DebugJoystickManager::ResolveButtonNames(args.back()) == 0 &&
                 !args.back().empty() && (std::isdigit(static_cast<unsigned char>(args.back()[0])) || args.back()[0] == '-'))
        {
            return ErrorLine("Invalid frame count '" + args.back() + "': expected an integer", newline);
        }
        return finish(joystick.TapChecked(JoinNames(args, 1, nameEnd), frames));
    }
    if (verb == "set")
    {
        if (args.size() < 2)
            return ErrorLine("Missing state. Usage: joystick set <state|buttons|none>", newline);
        const std::string names = JoinNames(args, 1, args.size());
        long long value = 0;
        if (Lower(names) == "none")
            return finish(joystick.ReleaseAll());
        if (ParseInteger(names, value))
            return finish(joystick.SetStateChecked(value));
        const uint8_t mask = DebugJoystickManager::ResolveButtonNames(names);
        if (mask == 0)  // let the manager word the unknown-name error, like press does
            return finish(joystick.Press(names));
        return finish(joystick.SetStateChecked(mask));
    }
    if (verb == "clear" || verb == "release_all")
        return finish(joystick.ReleaseAll());
    if (verb == "status" || verb == "info")
        return FormatStatus(joystick.GetState(), newline);
    if (verb == "list")
        return FormatList(newline);

    return ErrorLine("Unknown subcommand '" + verb + "'", newline) + "Use 'joystick help' for available commands" +
           newline;
}

}  // namespace CliJoystick
