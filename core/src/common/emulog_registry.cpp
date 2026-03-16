#include "emulog_registry.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace emu::log {

LoggerRegistry& LoggerRegistry::instance()
{
    static LoggerRegistry registry;
    return registry;
}

void LoggerRegistry::registerCategory(CategoryState& state)
{
    std::lock_guard lock(_mutex);

    // Avoid double-registration
    if (_byId.find(state.id) != _byId.end())
        return;

    _byId[state.id] = &state;
    _byName[state.name] = &state;

    // Resolve parent link
    resolveParent(state);
}

void LoggerRegistry::resolveParent(CategoryState& state)
{
    // Find parent by stripping last ".component" from name
    std::string name(state.name);
    auto dot = name.rfind('.');
    if (dot == std::string::npos)
    {
        state.parent = nullptr;  // Root category
        return;
    }

    std::string parentName = name.substr(0, dot);
    auto it = _byName.find(parentName);
    if (it != _byName.end())
    {
        state.parent = it->second;
    }
    else
    {
        state.parent = nullptr;  // Parent not registered (yet)
    }
}

void LoggerRegistry::setEnabled(const std::string& pattern, bool enabled, Level level)
{
    std::lock_guard lock(_mutex);

    if (pattern == "*")
    {
        // Enable/disable all
        for (auto& [id, cat] : _byId)
        {
            cat->enabled.store(enabled, std::memory_order_relaxed);
            if (enabled)
                cat->level.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
        }
        return;
    }

    // Check for wildcard suffix "fdc.*"
    if (pattern.size() >= 2 && pattern.back() == '*' && pattern[pattern.size() - 2] == '.')
    {
        std::string prefix = pattern.substr(0, pattern.size() - 1);  // "fdc."

        // Also enable/disable the parent itself ("fdc")
        std::string parentName = pattern.substr(0, pattern.size() - 2);
        auto parentIt = _byName.find(parentName);
        if (parentIt != _byName.end())
        {
            parentIt->second->enabled.store(enabled, std::memory_order_relaxed);
            if (enabled)
                parentIt->second->level.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
        }

        // Enable all children with this prefix
        for (auto& [name, cat] : _byName)
        {
            if (name.compare(0, prefix.size(), prefix) == 0)
            {
                cat->enabled.store(enabled, std::memory_order_relaxed);
                if (enabled)
                    cat->level.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
            }
        }
        return;
    }

    // Exact match
    auto it = _byName.find(pattern);
    if (it != _byName.end())
    {
        it->second->enabled.store(enabled, std::memory_order_relaxed);
        if (enabled)
            it->second->level.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
    }
}

void LoggerRegistry::disableAll()
{
    std::lock_guard lock(_mutex);
    for (auto& [id, cat] : _byId)
    {
        cat->enabled.store(false, std::memory_order_relaxed);
    }
}

std::vector<std::pair<std::string, bool>> LoggerRegistry::listCategories() const
{
    std::lock_guard lock(_mutex);

    std::vector<std::pair<std::string, bool>> result;
    result.reserve(_byName.size());

    for (const auto& [name, cat] : _byName)
    {
        result.emplace_back(name, cat->enabled.load(std::memory_order_relaxed));
    }

    // Sort by name for consistent output
    std::sort(result.begin(), result.end());
    return result;
}

size_t LoggerRegistry::serializeCategoryList(char* buffer, size_t bufferSize) const
{
    std::lock_guard lock(_mutex);

    // Collect and sort
    std::vector<std::pair<std::string, CategoryState*>> sorted;
    sorted.reserve(_byName.size());
    for (const auto& [name, cat] : _byName)
    {
        sorted.emplace_back(name, cat);
    }
    std::sort(sorted.begin(), sorted.end());

    size_t written = 0;
    for (const auto& [name, cat] : sorted)
    {
        bool en = cat->enabled.load(std::memory_order_relaxed);
        uint8_t lvl = cat->level.load(std::memory_order_relaxed);
        int n = std::snprintf(buffer + written, bufferSize - written,
                              "%s:%d:%d\n", name.c_str(), en ? 1 : 0, lvl);
        if (n <= 0 || written + static_cast<size_t>(n) >= bufferSize)
            break;
        written += static_cast<size_t>(n);
    }

    return written;
}

CategoryState* LoggerRegistry::findCategory(const std::string& name) const
{
    std::lock_guard lock(_mutex);
    auto it = _byName.find(name);
    return (it != _byName.end()) ? it->second : nullptr;
}

size_t LoggerRegistry::categoryCount() const
{
    std::lock_guard lock(_mutex);
    return _byName.size();
}

}  // namespace emu::log
