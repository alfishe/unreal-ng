#include "snapshotpolicy.h"

#include <algorithm>
#include <map>

#include "emulator/ports/models/sprinter/sprinterzxsnapshot.h"

namespace snapshot
{
namespace
{
std::mutex& RegistryMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, std::shared_ptr<ISnapshotCommitPolicy>>& Registry()
{
    static std::map<std::string, std::shared_ptr<ISnapshotCommitPolicy>> registry = [] {
        // The machines' policies, known by name from the start (`commit=sprinter-zx`); the machine's decoder hands out the
        // same instance for its own plan step. The shared_ptr does not own it (a function-local static)
        std::map<std::string, std::shared_ptr<ISnapshotCommitPolicy>> builtin;
        builtin["sprinter-zx"] =
            std::shared_ptr<ISnapshotCommitPolicy>(&SprinterZxSnapshot::Instance(), [](ISnapshotCommitPolicy*) {});
        return builtin;
    }();
    return registry;
}
}  // namespace

void SnapshotPolicies::Register(std::shared_ptr<ISnapshotCommitPolicy> policy)
{
    if (!policy)
        return;
    std::lock_guard<std::mutex> lock(RegistryMutex());
    Registry()[policy->Name()] = std::move(policy);
}

void SnapshotPolicies::Unregister(const std::string& name)
{
    std::lock_guard<std::mutex> lock(RegistryMutex());
    Registry().erase(name);
}

ISnapshotCommitPolicy* SnapshotPolicies::Find(const std::string& name)
{
    std::lock_guard<std::mutex> lock(RegistryMutex());
    const auto it = Registry().find(name);
    return it == Registry().end() ? nullptr : it->second.get();
}

std::vector<std::string> SnapshotPolicies::Names()
{
    std::lock_guard<std::mutex> lock(RegistryMutex());
    std::vector<std::string> names{"legacy"};
    for (const auto& entry : Registry())
        names.push_back(entry.first);
    std::sort(names.begin(), names.end());
    return names;
}
}  // namespace snapshot
