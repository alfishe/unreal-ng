#pragma once

#include "dzrptypes.h"
#include "dzrpprotocol.h"
#include <atomic>
#include <thread>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace dzrp {

// Forward declaration
class IDebugInterface;

// Server configuration
struct ServerConfig
{
    uint16_t port = DEFAULT_PORT;
    std::string bindAddress = "127.0.0.1";
    std::string serverName = "Unreal-NG";
    std::string serverVersion = "1.0.0";
};

class Server
{
public:
    explicit Server(IDebugInterface* debug, const ServerConfig& config = {});
    ~Server();

    // Lifecycle
    bool start();
    void stop();
    bool isRunning() const { return m_running.load(); }

    // Get port (useful if config port was 0 for auto-assign)
    uint16_t getPort() const { return m_actualPort; }

    // Called by emulator when execution pauses
    void notifyPause(BreakReason reason, uint16_t addr, uint8_t bank = 0,
                     const std::string& message = "");

private:
    void acceptLoop();
    void sessionLoop(int clientSocket);
    Response handleCommand(const Command& cmd);

    // Command handlers
    Response handleInit(const Command& cmd);
    Response handleClose(const Command& cmd);
    Response handleGetRegisters(const Command& cmd);
    Response handleSetRegister(const Command& cmd);
    Response handleContinue(const Command& cmd);
    Response handlePause(const Command& cmd);
    Response handleReadMem(const Command& cmd);
    Response handleWriteMem(const Command& cmd);
    Response handleAddBreakpoint(const Command& cmd);
    Response handleRemoveBreakpoint(const Command& cmd);
    Response handleAddWatchpoint(const Command& cmd);
    Response handleRemoveWatchpoint(const Command& cmd);
    Response handleGetSupportedCommands(const Command& cmd);
    Response handleSetSlot(const Command& cmd);
    Response handleWriteBank(const Command& cmd);
    Response handleSetBorder(const Command& cmd);
    Response handleReadState(const Command& cmd);
    Response handleWriteState(const Command& cmd);

    // Build NAK response
    Response makeNak(uint8_t seqNo);

    // Send all bytes atomically (mutex-protected)
    bool sendAll(int socket, const std::vector<uint8_t>& data);

    IDebugInterface* m_debug;
    ServerConfig m_config;
    int m_listenSocket = -1;
    uint16_t m_actualPort = 0;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::thread m_acceptThread;

    // Active session - protects both m_clientSocket and socket writes
    std::mutex m_sessionMutex;
    int m_clientSocket = -1;

    // Breakpoint tracking (permanent breakpoints only - temp handled by IDebugInterface)
    std::unordered_map<uint16_t, uint16_t> m_breakpoints;  // id -> addr
};

// Debug interface that emulator must implement
class IDebugInterface
{
public:
    virtual ~IDebugInterface() = default;

    // Execution control
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual bool isPaused() const = 0;

    // Registers - returns raw bytes in DZRP order
    struct Registers
    {
        uint16_t pc, sp;
        uint16_t af, bc, de, hl;
        uint16_t ix, iy;
        uint16_t af2, bc2, de2, hl2;
        uint8_t r, i, im;
    };
    virtual Registers getRegisters() const = 0;
    virtual void setRegister(RegisterId regId, uint16_t value) = 0;

    // Memory
    virtual std::vector<uint8_t> readMemory(uint16_t addr, uint16_t len) const = 0;
    virtual void writeMemory(uint16_t addr, const std::vector<uint8_t>& data) = 0;

    // Memory banking (ZX 128K+)
    virtual std::vector<uint8_t> getSlots() const = 0;  // bank number per slot
    virtual void setSlot(uint8_t slot, uint8_t bank) = 0;
    virtual void writeBank(uint8_t bank, const std::vector<uint8_t>& data) = 0;

    // Breakpoints
    // bank: wire format is bank+1 (0 = any bank, 1 = bank 0, etc.)
    // temporary=true marks breakpoint for auto-removal after next pause
    virtual uint16_t addBreakpoint(uint16_t addr, uint8_t bank = 0,
                                    const std::string& condition = "",
                                    bool temporary = false) = 0;
    virtual void removeBreakpoint(uint16_t id) = 0;
    virtual void clearTemporaryBreakpoints() = 0;

    // Watchpoints
    // bank: wire format is bank+1 (0 = any bank, 1 = bank 0, etc.)
    virtual bool addWatchpoint(uint16_t addr, uint8_t bank, uint16_t size,
                               WatchAccess access) = 0;
    virtual void removeWatchpoint(uint16_t addr, uint8_t bank, uint16_t size,
                                  WatchAccess access) = 0;

    // State (reverse debugging)
    virtual std::vector<uint8_t> captureState() const = 0;
    virtual void restoreState(const std::vector<uint8_t>& state) = 0;

    // Machine info
    virtual MachineType getMachineType() const = 0;

    // Border
    virtual void setBorder(uint8_t color) = 0;
};

} // namespace dzrp
