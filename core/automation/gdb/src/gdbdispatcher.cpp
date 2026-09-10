#include "gdbdispatcher.h"

void GDBDispatcher::registerHandler(const std::string& command, Handler handler)
{
    _exactHandlers[command] = std::move(handler);
}

void GDBDispatcher::registerPrefixHandler(const std::string& prefix, Handler handler)
{
    _prefixHandlers[prefix] = std::move(handler);
}

std::string GDBDispatcher::dispatch(const std::string& packet) const
{
    if (packet.empty())
    {
        return "";
    }

    // Try exact match first
    auto it = _exactHandlers.find(packet);
    if (it != _exactHandlers.end())
    {
        return it->second(packet);
    }

    // Try prefix matches (longest match wins)
    std::string bestPrefix;
    const Handler* bestHandler = nullptr;

    for (const auto& [prefix, handler] : _prefixHandlers)
    {
        if (packet.starts_with(prefix) && prefix.length() > bestPrefix.length())
        {
            bestPrefix = prefix;
            bestHandler = &handler;
        }
    }

    if (bestHandler)
    {
        return (*bestHandler)(packet);
    }

    // Unsupported - return empty
    return "";
}

bool GDBDispatcher::isSupported(const std::string& command) const
{
    if (_exactHandlers.contains(command))
    {
        return true;
    }

    for (const auto& [prefix, _] : _prefixHandlers)
    {
        if (command.starts_with(prefix))
        {
            return true;
        }
    }

    return false;
}
