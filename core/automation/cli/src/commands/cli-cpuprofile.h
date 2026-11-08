#pragma once

/// @file cli-cpuprofile.h
/// @brief CLI `cpuprofile`: its words into the operation and options of profile::Execute (profilecontrol.h), which
/// every surface calls. The answer is the reply's text (or its fields as JSON with --json).
///
///   cpuprofile start [nostack] [beam] [noframes] [reset] [stream=<file>] [ring_kib=<n>]
///   cpuprofile stop | reset | status
///   cpuprofile mark <name> [drop]
///   cpuprofile report [<top>] [since=<mark>] [sort=self|inclusive|calls] [beam=true]
///   cpuprofile frames [<last>] [top=<n>]
///   cpuprofile export <kit|json|entries|sym|ann> [<path>] [since=<mark>]

#include <map>
#include <string>
#include <vector>

namespace CliCpuProfile
{
inline constexpr const char* kUsage =
    "Usage: cpuprofile start [nostack] [beam] [noframes] [reset] [stream=FILE] [ring_kib=N] | stop | reset | status | "
    "mark <name> [drop] | report [top] [since=MARK] [sort=self|inclusive|calls] [beam=true] | frames [last] [top=N] | "
    "export <kit|json|entries|sym|ann> [path] [since=MARK] [--json]";

/// False with the reason in error. The first word is the operation (none: status); name=value words set an option
inline bool ParseArgs(const std::vector<std::string>& args, std::string& op, std::map<std::string, std::string>& options,
                      bool& json, std::string& error)
{
    op = "status";
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
    op = words[0];
    for (size_t i = 1; i < words.size(); i++)
    {
        const std::string& word = words[i];
        const size_t eq = word.find('=');
        if (eq != std::string::npos && eq > 0)
        {
            options[word.substr(0, eq)] = word.substr(eq + 1);
            continue;
        }
        if (op == "start" && (word == "nostack" || word == "beam" || word == "noframes" || word == "reset"))
        {
            if (word == "nostack")
                options["stack"] = "false";
            else if (word == "noframes")
                options["frames"] = "false";
            else
                options[word] = "true";
        }
        else if (op == "mark" && word == "drop")
            options["drop"] = "true";
        else if (op == "mark" && !options.count("name"))
            options["name"] = word;
        else if (op == "report" && !options.count("top"))
            options["top"] = word;
        else if (op == "frames" && !options.count("last"))
            options["last"] = word;
        else if (op == "export" && !options.count("format"))
            options["format"] = word;
        else if (op == "export" && !options.count("path"))
            options["path"] = word;
        else
        {
            error = "unexpected word '" + word + "'";
            return false;
        }
    }
    return true;
}
}  // namespace CliCpuProfile
