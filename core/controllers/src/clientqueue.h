#pragma once

/// @file clientqueue.h
/// @brief Which client on this machine owns the host's controllers. Every client opens the same devices, so two of
/// them would both act on a turn of the Dial and both write its feel and haptics. The one the user activated last owns
/// them: the clients keep one shared list of their process ids, the last activated first, and each one owns the
/// devices while it is the first live one (HostInputService::SetOwner). This is the pure part: the list and its
/// edits; the client keeps the file and asks the OS which processes live (unreal-qt HostControllers).

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace controllers
{

class ClientQueue
{
public:
    using Alive = std::function<bool(int64_t pid)>;

    /// One process id per line; anything else (an empty or broken file) is skipped
    static std::vector<int64_t> Parse(const std::string& text);
    static std::string Format(const std::vector<int64_t>& pids);

    /// @p pid activated: first in the list (once); the dead ones are dropped
    static void Claim(std::vector<int64_t>& pids, int64_t pid, const Alive& alive);
    /// @p pid quits: out of the list
    static void Leave(std::vector<int64_t>& pids, int64_t pid);
    /// The first live process of the list; 0 when none lives
    static int64_t Owner(const std::vector<int64_t>& pids, const Alive& alive);
};

}  // namespace controllers
