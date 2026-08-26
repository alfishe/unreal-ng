#pragma once

#include <memory>
#include <string>

class GDBServer;

/// @brief GDB Remote Serial Protocol automation transport
///
/// Exposes the emulator as a GDB RSP target for debugging with GDB, IDA Pro,
/// Ghidra, VS Code, and other GDB-compatible tools. Supports forward and
/// reverse debugging when TTD is enabled.
///
/// See docs/emulator/design/control-interfaces/gdb-protocol.md for protocol details.
/// See docs/emulator/design/debugger/time-travel-debug/gdb-reverse-debugging-tdd.md
/// for reverse debugging design.
class AutomationGDB
{
public:
    AutomationGDB();
    ~AutomationGDB();

    /// @brief Start the GDB server on the configured port
    /// @return true if server started successfully
    bool start();

    /// @brief Stop the GDB server and disconnect all clients
    void stop();

    /// @brief Check if the server is running
    bool isRunning() const;

    /// @brief Get the port the server is listening on
    /// @return Port number, or 0 if not running
    uint16_t getPort() const;

    /// @brief Get the bind address
    std::string getBindAddress() const;

    /// @brief Configuration: set the listen port (default 2000)
    void setPort(uint16_t port);

    /// @brief Configuration: set the bind address (default 127.0.0.1)
    void setBindAddress(const std::string& address);

    /// @brief Configuration: enable auto-attach for single-instance mode
    void setAutoAttach(bool enable);

private:
    std::unique_ptr<GDBServer> _server;
    uint16_t _port = 2000;
    std::string _bindAddress = "127.0.0.1";
    bool _autoAttach = true;
};
