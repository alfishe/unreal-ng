#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

class GDBSession;
class GDBPacketReader;
class Emulator;
class EmulatorContext;

#include <memory>

/// @brief GDB RSP server managing TCP connections and client sessions
///
/// Implements gdbserver --multi model: one shared port, instance selection at attach time.
/// Each connected client gets a GDBSession that handles packet dispatch.
class GDBServer
{
public:
    GDBServer();
    ~GDBServer();

    /// @brief Start listening for connections
    /// @param port TCP port to listen on
    /// @param bindAddress Address to bind to (default 127.0.0.1)
    /// @return true if server started successfully
    bool start(uint16_t port, const std::string& bindAddress = "127.0.0.1");

    /// @brief Stop the server and close all sessions
    void stop();

    /// @brief Check if server is running
    bool isRunning() const { return _running; }

    /// @brief Get the listening port
    uint16_t getPort() const { return _port; }

    /// @brief Get the bind address
    const std::string& getBindAddress() const { return _bindAddress; }

    /// @brief Set auto-attach mode for single-instance deployments
    void setAutoAttach(bool enable) { _autoAttach = enable; }

    /// @brief Get number of active sessions
    size_t getSessionCount() const;

private:
    void acceptLoop();
    void handleClient(int clientSocket);

    int _serverSocket = -1;
    uint16_t _port = 0;
    std::string _bindAddress;
    std::atomic<bool> _running{false};
    std::atomic<bool> _stopping{false};
    bool _autoAttach = true;

    std::unique_ptr<std::thread> _acceptThread;

    mutable std::mutex _sessionsMutex;
    std::unordered_map<int, std::unique_ptr<GDBSession>> _sessions;
};

/// @brief A single GDB client session
///
/// Handles packet I/O, dispatch, and emulator state for one connected client.
/// Implements run-control claim per TDD §3.3.
class GDBSession
{
public:
    GDBSession(int socket, bool noAckMode = false);
    ~GDBSession();

    /// @brief Run the session (blocking, called from dedicated thread)
    void run();

    /// @brief Stop the session
    void stop();

    /// @brief Check if session is active
    bool isActive() const { return _active; }

    /// @brief Get attached emulator instance ID (empty if not attached)
    const std::string& getAttachedInstanceId() const { return _attachedInstanceId; }

    /// @brief Set auto-attach mode
    void setAutoAttach(bool enable) { _autoAttach = enable; }

private:
    // Packet I/O
    bool sendPacket(const std::string& data);
    bool sendRaw(const std::string& data);
    std::string receivePacket();

    // Packet handlers
    std::string handlePacket(const std::string& packet);
    std::string handleQuery(const std::string& packet);
    std::string handleQSupported(const std::string& params);
    std::string handleQXfer(const std::string& params);
    std::string handleVCommand(const std::string& packet);
    std::string handleMonitor(const std::string& cmd);

    // Register/memory access
    std::string handleReadRegisters();
    std::string handleWriteRegisters(const std::string& data);
    std::string handleReadRegister(const std::string& params);
    std::string handleWriteRegister(const std::string& params);
    std::string handleReadMemory(const std::string& params);
    std::string handleWriteMemory(const std::string& params);

    // Breakpoints
    std::string handleSetBreakpoint(const std::string& params);
    std::string handleRemoveBreakpoint(const std::string& params);

    // Execution control
    std::string handleContinue();
    std::string handleStep();
    std::string handleInterrupt();

    // Reverse execution (TTD)
    std::string handleBackwardStep();
    std::string handleBackwardContinue();

    // Stop reason
    std::string formatStopReply();

    // Instance management
    std::string handleAttach(const std::string& pid);
    std::string handleDetach();

    int _socket = -1;
    std::atomic<bool> _active{true};
    bool _noAckMode = false;
    bool _autoAttach = true;

    std::string _attachedInstanceId;
    std::unique_ptr<GDBPacketReader> _reader;

    // Attached emulator
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;

    // Run-control claim token (TDD §3.3)
    bool _hasRunControlClaim = false;

    // Stop reason tracking
    enum class StopReason
    {
        None,
        Breakpoint,
        Watchpoint,
        Step,
        Interrupt,
        Attached
    };
    StopReason _lastStopReason = StopReason::Attached;
    uint16_t _lastWatchAddress = 0;

    // Client compatibility quirk flags (TDD §4.7.1, §4.2.2)
    struct ClientQuirks
    {
        bool watchValueFormat = false;    // IDA rumored: watch:ADDR=VALUE instead of watch:ADDR
        bool needsFlattenedXML = false;   // Client failed to parse xi:include
        bool legacyThreadSyntax = false;  // Older GDB versions
    };
    ClientQuirks _quirks;
};
