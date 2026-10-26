#pragma once

/// @file cli-analysis.h
/// @brief CLI `analysis`: its words into the action and options of analysis::Execute (analysiscontrol.h), which every
/// surface calls. The answer is the reply's text (or its fields as JSON with --json).
///
///   analysis start [facts|counters] [z80|physical] [record]   (also level=<l> mode=<m> record=true)
///   analysis stop | status | cancel
///   analysis build [<frames>] [path=<file>] [wait=<ms>]
///   analysis explain <address> [<page>] [routine|byte]          (also page=<p> what=<w>)
///   analysis query [<kind>] [<range>] [limit=<n>]               (L2 claims: kind all | conflicts | records | ...)
///   analysis export <format> [<path>] [ranges=<r,...>] [width=<n>]   (ctl, skool, fields, geometry, layout, ...)
///   analysis gallery [<kind>] [<address> [<path>]] [scale=<n>] [limit=<n>]   (L2 graphics; one as a PNG)

#include <map>
#include <string>
#include <vector>

namespace CliAnalysis
{
inline constexpr const char* kUsage =
    "Usage: analysis start [facts|counters] [z80|physical] [record] | stop | status | build [frames] [path=F] "
    "[wait=MS] | cancel | explain <address> [page] [routine|byte] | query [kind] [range] [limit=N] | export <format> "
    "[path] [ranges=5900-59FF,8000-FFFF] [width=N] | gallery [font|sprite|tile|screen|level_map] [address [path.png]] "
    "[scale=N] [limit=N] [--json]";

/// False with the reason in error. The first word is the action (none: status); name=value words set an option by name
inline bool ParseArgs(const std::vector<std::string>& args, std::string& action, std::map<std::string, std::string>& options,
                      bool& json, std::string& error)
{
    action = "status";
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
    if (words.empty())
        return true;
    action = words[0];
    for (size_t i = 1; i < words.size(); i++)
    {
        const std::string& word = words[i];
        const size_t equals = word.find('=');
        if (equals != std::string::npos && equals > 0)
        {
            options[word.substr(0, equals)] = word.substr(equals + 1);
            continue;
        }
        if (action == "start")
        {
            if (word == "facts" || word == "counters")
                options["level"] = word;
            else if (word == "z80" || word == "physical" || word == "phys")
                options["mode"] = word;
            else if (word == "record")
                options["record"] = "true";
            else
            {
                error = "start: facts | counters, z80 | physical, record, not '" + word + "'";
                return false;
            }
        }
        else if (action == "build" && !options.count("frames"))
            options["frames"] = word;
        else if (action == "explain" && (word == "routine" || word == "byte" || word == "auto"))
            options["what"] = word;
        else if (action == "explain" && !options.count("address"))
            options["address"] = word;
        else if (action == "explain" && !options.count("page"))
            options["page"] = word;
        else if (action == "query" && !options.count("kind"))
            options["kind"] = word;
        else if (action == "query" && !options.count("range"))
            options["range"] = word;
        else if (action == "export" && !options.count("format"))
            options["format"] = word;
        else if (action == "export" && !options.count("path"))
            options["path"] = word;
        else if (action == "gallery" && !options.count("kind") && !options.count("address") &&
                 (word == "all" || word == "font" || word == "sprite" || word == "tile" || word == "screen" ||
                  word == "level_map"))
            options["kind"] = word;
        else if (action == "gallery" && !options.count("address"))
            options["address"] = word;
        else if (action == "gallery" && !options.count("path"))
            options["path"] = word;
        else
        {
            error = action + ": unexpected '" + word + "'";
            return false;
        }
    }
    return true;
}
}  // namespace CliAnalysis
