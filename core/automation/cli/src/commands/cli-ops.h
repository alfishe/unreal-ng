#pragma once

/// @file cli-ops.h
/// @brief CLI `ops`: its arguments into the OperationsStatus query every surface makes (since, limit). The answer is
/// OperationsStatus::Text (core/automation/operationsstatus.h).

#include <cstdint>
#include <string>
#include <vector>

namespace CliOps
{
inline constexpr uint64_t kDefaultLimit = 20;
inline constexpr const char* kUsage =
    "Usage: ops [n|all] | ops since <seq> [n]  (default: the newest 20; n = 0 or all: every entry kept)";

/// `ops` -> newest 20; `ops <n>` -> newest n (0: all); `ops all`; `ops since <seq> [n|all]` -> after seq (all, or the
/// newest n of those). False: not one of these
inline bool ParseArgs(const std::vector<std::string>& args, uint64_t& since, uint64_t& limit)
{
    auto number = [](const std::string& text, uint64_t& value) {
        if (text.empty() || text.size() > 19 || text.find_first_not_of("0123456789") != std::string::npos)
            return false;
        value = std::stoull(text);
        return true;
    };
    since = 0;
    limit = kDefaultLimit;
    size_t next = 0;
    if (!args.empty() && args[0] == "since")
    {
        if (args.size() < 2 || !number(args[1], since))
            return false;
        limit = 0;
        next = 2;
    }
    if (args.size() > next + 1)
        return false;
    if (args.size() == next + 1)
    {
        if (args[next] == "all")
            limit = 0;
        else if (!number(args[next], limit))
            return false;
    }
    return true;
}
}  // namespace CliOps
