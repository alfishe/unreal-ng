#pragma once

/// Kempston Mouse CLI helpers: argument parsing and output formatting.
/// Header-only, no socket or CLIProcessor dependencies, so core-tests can unit-test them
/// (automation-interfaces §4.5, §5.6). Range checks stay in DebugMouseManager; these
/// functions only check that a token is an integer / a valid button name.

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "debugger/mouse/debugmousemanager.h"

namespace CliMouse
{

/// Parse a whole token as a base-10 signed integer. Rejects empty input, trailing junk
/// ("10px"), leading/trailing spaces and values that do not fit in long long.
inline bool ParseStrictInt(const std::string& text, long long& out)
{
    if (text.empty() || std::isspace(static_cast<unsigned char>(text.front())))
        return false;

    errno = 0;
    char* end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE)
        return false;

    out = value;
    return true;
}

/// Parse an int argument (dx, dy, steps, x, y). On failure fills error with a message.
inline bool ParseIntArg(const std::string& text, const char* name, int& out, std::string& error)
{
    long long value = 0;
    if (!ParseStrictInt(text, value) || value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max())
    {
        error = std::string("Invalid ") + name + " '" + text + "': expected an integer";
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

/// Parse a click frame count into uint32 first, so 70000 reaches the manager unwrapped
/// and a negative value is not silently turned into a huge unsigned number.
inline bool ParseFramesArg(const std::string& text, uint32_t& out, std::string& error)
{
    long long value = 0;
    if (!ParseStrictInt(text, value))
    {
        error = "Invalid frame count '" + text + "': expected an integer";
        return false;
    }
    if (value < 0 || value > static_cast<long long>(std::numeric_limits<uint32_t>::max()))
    {
        error = "frames=" + std::to_string(value) + " out of range 1.." +
                std::to_string(DebugMouseManager::MAX_CLICK_FRAMES);
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

/// Error text for an unknown button name (same wording in CLI, Python, Lua)
inline std::string UnknownButtonError(const std::string& name)
{
    return "Unknown mouse button '" + name + "'. Valid: left, right, middle (or l, r, m)";
}

/// Parse "none" or comma-separated button names; several tokens are also accepted
/// ("left,middle" or "left middle"). Duplicates are ignored. Result: bit set = pressed.
inline bool ParseButtonList(const std::vector<std::string>& tokens, uint8_t& pressedBits, std::string& error)
{
    pressedBits = 0;
    if (tokens.empty())
    {
        error = "Missing button list. Usage: mouse buttons <none|b1,b2..>";
        return false;
    }

    for (const std::string& token : tokens)
    {
        size_t start = 0;
        while (start <= token.size())
        {
            size_t comma = token.find(',', start);
            if (comma == std::string::npos)
                comma = token.size();
            const std::string name = token.substr(start, comma - start);
            start = comma + 1;

            if (name.empty())
                continue;

            std::string lower = name;
            for (char& c : lower)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower == "none")
                continue;

            const auto button = DebugMouseManager::ResolveButtonName(name);
            if (!button)
            {
                error = UnknownButtonError(name);
                return false;
            }
            pressedBits |= static_cast<uint8_t>(*button);
        }
    }
    return true;
}

inline std::string Hex2(uint8_t value)
{
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "0x%02X", value);
    return buffer;
}

/// "L--", "LRM", "--M": pressed buttons in L/R/M order, '-' when up
inline std::string FormatButtonsShort(const MouseStateSnapshot& state)
{
    std::string text = "---";
    if (state.IsPressed(MouseButton::Left))
        text[0] = 'L';
    if (state.IsPressed(MouseButton::Right))
        text[1] = 'R';
    if (state.IsPressed(MouseButton::Middle))
        text[2] = 'M';
    return text;
}

inline std::string SignedText(int value)
{
    return (value >= 0 ? "+" : "") + std::to_string(value);
}

/// "Moved: dx=+10 dy=-5 -> X=41 Y=80"
inline std::string FormatMoveLine(int dx, int dy, const MouseStateSnapshot& state)
{
    return "Moved: dx=" + SignedText(dx) + " dy=" + SignedText(dy) + " -> X=" + std::to_string(state.x) +
           " Y=" + std::to_string(state.y);
}

/// "Pressed: left -> buttons=L-- (#FADF=0x0E)"
inline std::string FormatButtonLine(const std::string& verb, const std::string& detail,
                                    const MouseStateSnapshot& state)
{
    return verb + ": " + detail + " -> buttons=" + FormatButtonsShort(state) + " (#FADF=" +
           Hex2(state.portButtons) + ")";
}

/// "Wheel: -1 -> wheel=15 (#FADF=0xFF)"
inline std::string FormatWheelLine(int steps, const MouseStateSnapshot& state)
{
    return "Wheel: " + SignedText(steps) + " -> wheel=" + std::to_string(state.wheel) + " (#FADF=" +
           Hex2(state.portButtons) + ")";
}

/// "Counters set: X=40 Y=40 (#FBDF=0x28 #FFDF=0x28)"
inline std::string FormatCountersLine(const MouseStateSnapshot& state)
{
    return "Counters set: X=" + std::to_string(state.x) + " Y=" + std::to_string(state.y) + " (#FBDF=" +
           Hex2(state.portX) + " #FFDF=" + Hex2(state.portY) + ")";
}

/// Comma-separated pressed button names, or "none"
inline std::string FormatPressedNames(uint8_t pressedBits)
{
    std::string names;
    for (MouseButton button : {MouseButton::Left, MouseButton::Right, MouseButton::Middle})
    {
        if (pressedBits & static_cast<uint8_t>(button))
            names += (names.empty() ? "" : ",") + DebugMouseManager::GetButtonName(button);
    }
    return names.empty() ? "none" : names;
}

/// Multi-line "mouse status" block (automation-interfaces §4.5)
inline std::string FormatStatus(const MouseStateSnapshot& state, const std::string& newline = "\n")
{
    if (!state.available)
        return "Kempston Mouse [not available]" + newline;

    auto upDown = [&state](MouseButton button) { return state.IsPressed(button) ? "down" : "up"; };

    std::string text;
    text += std::string("Kempston Mouse [") + (state.present ? "present" : "absent") + "]" + newline;
    text += "  X=" + std::to_string(state.x) + " (" + Hex2(state.x) + ")  Y=" + std::to_string(state.y) + " (" +
            Hex2(state.y) + ")" + newline;
    text += std::string("  Buttons: left=") + upDown(MouseButton::Left) + " right=" + upDown(MouseButton::Right) +
            " middle=" + upDown(MouseButton::Middle) + "  (mask " + Hex2(state.buttonMask) + ")" + newline;
    text += "  Wheel: " + std::to_string(state.wheel) + (state.wheelEnabled ? "" : " (no wheel fitted)") + newline;
    text += "  Ports: #FADF=" + Hex2(state.portButtons) + " #FBDF=" + Hex2(state.portX) +
            " #FFDF=" + Hex2(state.portY) + newline;
    if (state.pendingClickButton.has_value())
    {
        text += "  Pending click: " + DebugMouseManager::GetButtonName(*state.pendingClickButton) + ", " +
                std::to_string(state.pendingClickFramesLeft) + " frame(s) left" + newline;
    }
    else
    {
        text += "  Pending click: none" + newline;
    }
    text += std::string("  TTD journal: ") + (state.journalSupported ? "supported" : "unsupported") + newline;
    return text;
}

}  // namespace CliMouse
