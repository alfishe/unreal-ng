#include "clientqueue.h"

#include <algorithm>
#include <sstream>

namespace controllers
{

std::vector<int64_t> ClientQueue::Parse(const std::string& text)
{
    std::vector<int64_t> pids;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line))
    {
        if (line.empty() || line.size() > 18 || !std::all_of(line.begin(), line.end(), [](char c) {
                return c >= '0' && c <= '9';
            }))
            continue;
        const int64_t pid = std::stoll(line);
        if (pid > 0 && std::find(pids.begin(), pids.end(), pid) == pids.end())
            pids.push_back(pid);
    }
    return pids;
}

std::string ClientQueue::Format(const std::vector<int64_t>& pids)
{
    std::string text;
    for (const int64_t pid : pids)
        text += std::to_string(pid) + "\n";
    return text;
}

void ClientQueue::Claim(std::vector<int64_t>& pids, int64_t pid, const Alive& alive)
{
    pids.erase(std::remove_if(pids.begin(), pids.end(),
                              [pid, &alive](int64_t other) { return other == pid || (alive && !alive(other)); }),
               pids.end());
    pids.insert(pids.begin(), pid);
}

void ClientQueue::Leave(std::vector<int64_t>& pids, int64_t pid)
{
    pids.erase(std::remove(pids.begin(), pids.end(), pid), pids.end());
}

int64_t ClientQueue::Owner(const std::vector<int64_t>& pids, const Alive& alive)
{
    for (const int64_t pid : pids)
        if (!alive || alive(pid))
            return pid;
    return 0;
}

}  // namespace controllers
