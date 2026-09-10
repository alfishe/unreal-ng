#pragma once

#include <functional>
#include <string>
#include <unordered_map>

/// @brief Routes RSP packets to appropriate handlers
///
/// Provides a registry of packet handlers indexed by command prefix.
/// Supports both exact matches and prefix matches for extensible commands.
class GDBDispatcher
{
public:
    using Handler = std::function<std::string(const std::string&)>;

    /// @brief Register a handler for exact command match
    void registerHandler(const std::string& command, Handler handler);

    /// @brief Register a handler for command prefix match
    void registerPrefixHandler(const std::string& prefix, Handler handler);

    /// @brief Dispatch a packet to the appropriate handler
    /// @return Response packet data, or empty string for unsupported
    std::string dispatch(const std::string& packet) const;

    /// @brief Check if a command is supported
    bool isSupported(const std::string& command) const;

private:
    std::unordered_map<std::string, Handler> _exactHandlers;
    std::unordered_map<std::string, Handler> _prefixHandlers;
};
