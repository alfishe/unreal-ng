#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>

#include <common/uuid.h>

class GDBSession;
class GDBPacketReader;
class Emulator;
class EmulatorContext;
class BreakpointManager;

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
    // Shared with the session's own thread (handleClient): stop() wakes it, the thread ends it and closes the socket
    std::unordered_map<int, std::shared_ptr<GDBSession>> _sessions;
};

/// @brief A single GDB client session
///
/// Handles packet I/O, dispatch, and emulator state for one connected client.
/// Implements run-control claim per TDD §3.3.
///
/// One thread (run()) reads the socket, answers the packets and watches a running target: while a `c` runs it keeps
/// reading, so a 0x03 interrupts it; the stop reply goes out when the machine stops
class GDBSession
{
public:
    GDBSession(int socket, bool noAckMode = false);
    ~GDBSession();

    /// @brief Run the session (blocking, called from dedicated thread). Ends when the client detaches, the socket
    /// closes or stop() is called; then removes this session's breakpoints and releases run control
    void run();

    /// @brief Stop the session: run() ends within one poll quantum (any thread)
    void stop();

    /// @brief Check if session is active
    bool isActive() const { return _active; }

    /// @brief Get attached emulator instance ID (empty if not attached)
    const std::string& getAttachedInstanceId() const { return _attachedInstanceId; }

    /// @brief Set auto-attach mode
    void setAutoAttach(bool enable) { _autoAttach = enable; }

    /// A handler's reply that sends nothing now: a `c` whose stop reply comes when the machine stops
    static constexpr const char* kDeferredReply = "\x7f_DEFERRED_";

private:
    // Packet I/O
    bool sendPacket(const std::string& data);
    bool sendRaw(const std::string& data);
    /// Waits up to @p timeoutMs for bytes and feeds them all: complete packets (acked) and interrupts ("\x03") go to
    /// _incoming in arrival order. False when the socket closed
    bool pumpInput(int timeoutMs);
    void feedByte(char c);
    /// The client's packet loop is over: this session's breakpoints go, run control is released
    void endSession();

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
    /// M (hex data) or X (binary data, '}' escapes)
    std::string handleWriteMemory(const std::string& params, bool binary);
    /// The physical view 0x01PPAAAA names a RAM page this machine has (config ramsize)
    bool hasRamPage(uint64_t address) const;

    // Breakpoints
    std::string handleSetBreakpoint(const std::string& params);
    std::string handleRemoveBreakpoint(const std::string& params);
    // Removes this session's Z0 / Z1 breakpoint at the address (when another owner's was adopted, only the record);
    // false when the session set none there
    bool forgetExecutionBreakpoint(BreakpointManager& breakpoints, uint16_t address);

    // Execution control
    std::string handleContinue();
    /// The user took control back from this session at the GUI (execution arbiter design §6a): true when it did and
    /// the packet runs, steps or writes - the client gets the reason on its console and an error (E5E), once per packet
    bool RefuseAfterTakeBack(const std::string& packet);
    std::string handleStep();
    std::string handleInterrupt();
    /// A `c` in flight: the machine stopped (or the user took control back) - its stop reply, else empty
    std::string pollRunningStop();
    /// The stop reply for the machine's last stop (Emulator::LastStop): gdb's own breakpoint or watchpoint by its id
    std::string classifyStop();
    /// The machine stands in recorded history (TTD detached): a forward run cuts the history there and goes on live
    /// (time-travel TDD §8.3). False with a console message when the cut is refused
    bool inHistory() const;
    bool leaveHistory();
    /// gdb's breakpoints need the debug mode and the breakpoints feature (unreal-qt starts with both off)
    void ensureDebugEnabled();
    /// Removes every breakpoint and watchpoint this session set (only its own ids)
    void forgetAllBreakpoints();

    // Reverse execution (TTD)
    std::string handleBackwardStep();
    std::string handleBackwardContinue();

    // Instance management
    /// Binds the session to the instance (the attach's pause, run control when @p claim)
    void attachTo(const std::shared_ptr<Emulator>& emulator, const std::string& instanceId, bool claim);
    std::string handleAttach(const std::string& pid);
    std::string handleDetach();

    int _socket = -1;
    std::atomic<bool> _active{true};
    bool _noAckMode = false;
    bool _autoAttach = true;

    std::string _attachedInstanceId;
    std::unique_ptr<GDBPacketReader> _reader;
    /// Packets and interrupts read but not handled yet (the socket is read in chunks)
    std::deque<std::string> _incoming;

    // This session's execution breakpoints (Z0 / Z1) by address: a conditional one has no address key in the
    // manager, and a repeated Z0 replaces the conditions. All of them unshared: never another surface's
    struct OwnBreakpoint
    {
        uint16_t id = 0;
        bool hardware = false;   // Z1: its stop reply says hwbreak
    };
    std::unordered_map<uint16_t, OwnBreakpoint> _executionBreakpoints;
    // This session's watchpoints by (Z type, address, length): one unshared core breakpoint each (a range for a
    // length over 1, read | write for Z4)
    std::map<std::tuple<uint8_t, uint16_t, uint32_t>, uint16_t> _watchpoints;

    // Attached emulator
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;

    // Run-control claim (TDD §3.3)
    unreal::UUID _sessionUuid;  // Generated at session start, used for claim ownership

    // A `c` in flight (its stop reply pending) and whether gdb's 0x03 asked it to stop
    bool _running = false;
    bool _interruptRequested = false;
    // gdb stopped the machine (attach, interrupt, a stop of its own run): a detach lets it run again. Not after a
    // stop somebody else made (an external pause)
    bool _resumeOnDetach = false;
    // The history cut of a forward run from the past is told on the console once
    bool _cutWarned = false;

    // Stop reason tracking: the reply `?` repeats
    std::string _lastStopReply = "T05thread:01;";

    // Client compatibility quirk flags (TDD §4.7.1, §4.2.2)
    struct ClientQuirks
    {
        bool watchValueFormat = false;    // IDA rumored: watch:ADDR=VALUE instead of watch:ADDR
        bool needsFlattenedXML = false;   // Client failed to parse xi:include
        bool legacyThreadSyntax = false;  // Older GDB versions
    };
    ClientQuirks _quirks;
};
