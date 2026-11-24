#pragma once

/// @file cli-jobs.h
/// @brief CLI `jobs`: its words into the action and options of jobs::Execute (emulator/jobs/jobcontrol.h), which every
/// surface calls. The answer is the reply's text (or its fields as JSON with --json).
///
///   jobs [list] [all|this|<instance>] [state=active|finished|all] [limit=<n>]
///   jobs status <job> [result=false]
///   jobs wait <job> [<wait_ms>] [result=false]        (blocks this session up to wait_ms, default 30000, <= 60000)
///   jobs cancel <job>
///   jobs start <ttd|analysis> <verb> [name=value ...] (the verb's options: jobs start ttd dump path=/tmp/a.ttd)

#include <map>
#include <string>
#include <vector>

namespace CliJobs
{
inline constexpr const char* kUsage =
    "Usage: jobs [list] [all|this|<instance>] [state=active|finished|all] [limit=N] | status <job> [result=false] | "
    "wait <job> [wait_ms] [result=false] | cancel <job> | start <ttd|analysis> <verb> [name=value ...] [--json]";

/// False with the reason in error. The first word is the action (none: list); name=value words set an option
inline bool ParseArgs(const std::vector<std::string>& args, std::string& action, std::map<std::string, std::string>& options,
                      bool& json, std::string& error)
{
    action = "list";
    json = false;
    std::vector<std::string> words;
    for (const std::string& arg : args)
    {
        if (arg == "--json")
        {
            json = true;
            continue;
        }
        const size_t eq = arg.find('=');
        if (eq != std::string::npos && eq != 0)
        {
            options[arg.substr(0, eq)] = arg.substr(eq + 1);
            continue;
        }
        words.push_back(arg);
    }
    size_t next = 0;
    if (!words.empty())
        action = words[next++];
    if (action != "list" && action != "status" && action != "wait" && action != "cancel" && action != "start")
    {
        error = "unknown action '" + action + "'";
        return false;
    }
    // The positional words by action
    std::vector<const char*> positional;
    if (action == "list")
        positional = {"instance"};
    else if (action == "status" || action == "cancel")
        positional = {"job"};
    else if (action == "wait")
        positional = {"job", "wait_ms"};
    else
        positional = {"domain", "verb"};
    for (const char* name : positional)
        if (next < words.size())
            options[name] = words[next++];
    if (next < words.size())
    {
        error = "unexpected '" + words[next] + "'";
        return false;
    }
    return true;
}
}  // namespace CliJobs
