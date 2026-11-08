#pragma once

/// @file cli-markup.h
/// @brief CLI `markup`: its words into the action and options of markup::Execute (markupcontrol.h), which every
/// surface calls. The answer is the reply's text (or its fields as JSON with --json).
///
///   markup [show] [<range> | <from> [<to>]] [layer=<l>] [limit=<n>]   what the range holds (default the 64K)
///   markup layers                                               every layer counted, the runtime feed
///   markup mark <range> | <from> [<to>] <kind> [<title words>] [cpu]   the user layer (kind: code, data, sprite, ...)
///   markup unmark <range> | <from> [<to>]
///   markup comment <address> [<text words>]                     (no text: removes it)
///   markup clear [<layer>]                                      (user by default)
///   markup save [<profile>] [analysis=all|<kinds>] [confidence=<n>] [keep]   into a software profile
///
/// A range is 8000-80FF or $8000-$80FF; an address $8000, #8000, 0x8000, decimal or a label.

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "debugger/analyzers/analysis/l2model.h"

namespace CliMarkup
{
inline constexpr const char* kUsage =
    "Usage: markup [show] [range | from [to]] [layer=L] [limit=N] | layers | mark <range | from [to]> <kind> [title] "
    "[cpu] | unmark <range | from [to]> | comment <address> [text] | clear [layer] | save [profile] "
    "[analysis=all|code,sprite] [confidence=N] [keep] [--json]";

/// False with the reason in error. The first word is the action (none, or a range / address: show); name=value words
/// set an option by name
inline bool ParseArgs(const std::vector<std::string>& args, std::string& action,
                      std::map<std::string, std::string>& options, bool& json, std::string& error)
{
    static const std::vector<std::string> actions = {"show", "layers", "mark", "unmark", "comment", "clear", "save"};
    action = "show";
    options.clear();
    json = false;
    std::vector<std::string> words;
    for (const std::string& arg : args)
    {
        if (arg == "--json")
            json = true;
        else
            words.push_back(arg);
    }
    size_t first = 0;
    if (!words.empty() && std::find(actions.begin(), actions.end(), words[0]) != actions.end())
    {
        action = words[0];
        first = 1;
    }
    auto isRange = [](const std::string& word) { return word.find('-', 1) != std::string::npos; };
    std::string title;
    std::string text;
    for (size_t i = first; i < words.size(); i++)
    {
        const std::string& word = words[i];
        const size_t equals = word.find('=');
        if (equals != std::string::npos && equals > 0)
        {
            options[word.substr(0, equals)] = word.substr(equals + 1);
            continue;
        }
        const bool where = action == "show" || action == "mark" || action == "unmark";
        analysis::RegionKind kind = analysis::RegionKind::Data;
        if (where && !options.count("range") && !options.count("from") && isRange(word))
            options["range"] = word;
        else if (where && !options.count("range") && !options.count("from"))
            options["from"] = word;
        else if (action == "mark" && !options.count("kind") && analysis::ParseRegionKind(word, kind))
            options["kind"] = word;
        else if (where && !options.count("range") && !options.count("to") && !options.count("kind"))
            options["to"] = word;
        else if (action == "mark" && word == "cpu")
            options["cpu"] = "true";
        else if (action == "mark" && options.count("kind"))
            title += (title.empty() ? "" : " ") + word;
        else if (action == "comment" && !options.count("address"))
            options["address"] = word;
        else if (action == "comment")
            text += (text.empty() ? "" : " ") + word;
        else if (action == "clear" && !options.count("layer"))
            options["layer"] = word;
        else if (action == "save" && word == "keep")
            options["keep"] = "true";
        else if (action == "save" && !options.count("profile"))
            options["profile"] = word;
        else
        {
            error = action + ": unexpected '" + word + "'";
            return false;
        }
    }
    if (!title.empty())
        options["title"] = title;
    if (!text.empty())
        options["text"] = text;
    return true;
}
}  // namespace CliMarkup
