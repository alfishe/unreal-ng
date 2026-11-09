#pragma once

/// @file pictureviewmode.h
/// @brief The names of the window setting `view` (pictureview.h says what each shows): full, screen, content.
/// Light on purpose: WindowDisplay validates the word with it and includes nothing of the screen.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace PictureView
{

enum class Mode : uint8_t
{
    Full,     ///< the whole frame, border included (the default)
    Screen,   ///< the working picture: the paper, or the whole frame when the mode has no border
    Content   ///< what the layers draw: the working picture cut by the clip windows (a Next); else as Screen
};

/// The names in the order every surface lists them
inline const std::vector<std::string>& Names()
{
    static const std::vector<std::string> names = {"full", "screen", "content"};
    return names;
}

inline std::string NameOf(Mode mode)
{
    return Names()[static_cast<size_t>(mode)];
}

/// A name (any case) to its mode; false for none
inline bool Parse(const std::string& word, Mode& out)
{
    std::string lower = word;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    const std::vector<std::string>& names = Names();
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (names[i] == lower)
        {
            out = static_cast<Mode>(i);
            return true;
        }
    }
    return false;
}

/// "full, screen, content"
inline std::string NameList()
{
    std::string list;
    for (const std::string& name : Names())
        list += (list.empty() ? "" : ", ") + name;
    return list;
}

}  // namespace PictureView
