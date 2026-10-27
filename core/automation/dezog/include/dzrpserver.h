#pragma once

#include "platform-sockets.h"
#include "dzrptypes.h"
#include "dzrpprotocol.h"
#include <atomic>
#include <thread>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace dzrp {

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

    // ZX Next. A bank is an 8K Next bank (RAM 0..223; 0xFF the ROM, 16K that wraps), the offset wraps inside it.
    // The defaults are for machines without them: reads give zeros, writes do nothing
    virtual std::vector<uint8_t> readBankMemory(uint8_t /*bank*/, uint16_t /*offset*/, uint16_t size) const
    {
        return std::vector<uint8_t>(size, 0);
    }
    virtual void writeBankMemory(uint8_t /*bank*/, uint16_t /*offset*/, const std::vector<uint8_t>& /*data*/) {}
    // The sprites (CMD_GET_SPRITES_PALETTE .. CMD_GET_SPRITE_PATTERNS): empty on a machine without them
    /// 256 pairs (colour 8 bit RRRGGGBB, then bit 0 the low blue bit and bit 7 the Layer 2 priority) of sprite palette `which` (0 / 1)
    virtual std::vector<uint8_t> getSpritesPalette(uint8_t /*which*/) const { return {}; }
    /// `count` sprites from `index`, five attribute bytes each (x, y, attr 2, attr 3, attr 4)
    virtual std::vector<uint8_t> getSprites(uint8_t /*index*/, uint8_t /*count*/) const { return {}; }
    /// `count` patterns of 256 bytes from `index`
    virtual std::vector<uint8_t> getSpritePatterns(uint8_t /*index*/, uint8_t /*count*/) const { return {}; }
    /// The sprites' clip window x left, x right, y top, y bottom, then NR #15 (sprite and layers control)
    virtual std::vector<uint8_t> getSpritesClipWindowAndControl() const { return {}; }
    virtual uint8_t readNextReg(uint8_t /*reg*/) const { return 0; }
    virtual void writeNextReg(uint8_t /*reg*/, uint8_t /*value*/) {}

    // Breakpoints
    // bank: wire format is bank+1 (0 = any bank, 1 = bank 0, etc.)
    // condition: in the core breakpoint-condition language (breakpointcondition.h), empty for none; the
    //   breakpoint then stops only when it holds. 0 when it does not compile (the caller may retry without)
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

    // Port I/O (DeZog custom dumps read ports for the variables view:
    // CMD_READ_PORT must answer with exactly one data byte)
    virtual uint8_t readPort(uint16_t port) const = 0;
    virtual void writePort(uint16_t port, uint8_t value) = 0;

    // CMD_INTERRUPT_ON_OFF: IFF1 and IFF2 set (true) or cleared
    virtual void setInterrupts(bool /*enabled*/) {}

    // ZRCP snapshot-save / snapshot-load of a file (ZEsarUX picks the format by the extension). "" = done, else why not
    virtual std::string saveSnapshotFile(const std::string& /*path*/) { return "snapshots not supported"; }
    virtual std::string loadSnapshotFile(const std::string& /*path*/) { return "snapshots not supported"; }

    // --- ZRCP (ZEsarUX protocol) transport capabilities ---
    // Used by the zesarux module's ZRCP server; safe defaults keep other
    // transports working. Executes exactly one instruction. Call only while
    // paused; returns false when the step could not be executed (no emulator
    // or not paused).
    virtual bool stepOnce() { return false; }

    // Disassembles the instruction at addr (operand placeholders resolved).
    // Stores the instruction length in *lenOut when non-null. Empty string =
    // disassembler unavailable.
    virtual std::string disassembleInstruction(uint16_t /*addr*/, uint8_t* /*lenOut*/) { return {}; }

    // Session lifecycle
    // onSessionOpened: CMD_INIT received (start history recording, etc.)
    // onSessionClosed: client disconnected (CMD_CLOSE or socket drop). Implementations
    // should drop debugger-owned breakpoints/watchpoints and let the target run
    // again - DeZog re-sends all breakpoints on the next connect.
    virtual void onSessionOpened() {}
    virtual void onSessionClosed() {}

    // Automation sessions (execution arbiter design §6a): a client connected from `peer` ("host:port") - the GUI shows
    // the user where the debugger is; controlTakenBack: the user took control back at the GUI, the server tells the
    // client why (NTF_PAUSE with the reason) and drops the connection
    virtual void onClientConnected(const std::string& /*peer*/) {}
    virtual bool controlTakenBack() const { return false; }

    // Blocks up to timeoutMs for a debug target to exist (the host UI may
    // still be creating its emulator when DeZog connects). Returns false when
    // no target appeared. The default never blocks; real adapters poll their
    // emulator source. Used by CMD_INIT to fail cleanly instead of reporting
    // machine type UNKNOWN(0) (DeZog: "Unknown machine type 0 received").
    virtual bool waitForTarget(uint32_t /*timeoutMs*/) const { return true; }

    // --- Instruction history (true reverse debugging, Unreal-NG extension) ---
    // One record per executed instruction, index 0 = most recently executed.
    // Memory reads while browsing history reflect the state at that instruction.
    struct HistoryEntry
    {
        Registers regs{};
        std::vector<uint8_t> slots;
        uint8_t opcodes[4] = {0, 0, 0, 0};  // bytes at PC (memory order)
        uint16_t spContent = 0;             // word at (SP)
    };
    virtual bool isHistoryAvailable() const { return false; }
    virtual bool isHistoryRecording() const { return false; }
    virtual std::optional<HistoryEntry> getHistoryEntry(uint32_t /*index*/) { return std::nullopt; }
};

// Server configuration
struct ServerConfig
{
    // CMD_INIT: how long to wait for a debug target before answering with an
    // error (covers the host UI creating its emulator after DeZog connects).
    // Configurable so test harnesses can shorten the no-target failure path.
    static constexpr uint32_t DEFAULT_TARGET_WAIT_MS = 2000;

    uint32_t targetWaitMs = DEFAULT_TARGET_WAIT_MS;

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

    /// @brief True while a client session is established.
    ///
    /// The accept happens on m_acceptThread, so a caller that has just
    /// connected a socket cannot otherwise tell when the server has picked it
    /// up. Companion to isRunning() above; also the honest thing for status
    /// reporting to read.
    bool hasClient() const
    {
        std::lock_guard<std::mutex> lock(m_sessionMutex);
        // Not `m_clientSocket >= 0`: SOCKET is UINT_PTR (unsigned) on
        // Windows, so that comparison is always true - even after disconnect
        // resets the field to INVALID_SOCKET - and hasClient() never reports
        // false again. POSIX's plain-int SOCKET made the bug invisible there.
        return m_clientSocket != INVALID_SOCKET;
    }

    // Called by emulator when execution pauses
    void notifyPause(BreakReason reason, uint16_t addr, uint8_t bank = 0,
                     const std::string& message = "");

private:
    void acceptLoop();
    void sessionLoop(SOCKET clientSocket);
    Response handleCommand(const Command& cmd);

    // Command handlers
    Response handleInit(const Command& cmd);
    /// The user took control back at the GUI: NTF_PAUSE with the reason (DeZog shows it), the session then closes
    void tellControlTakenBack();
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
    Response handleReadBankMem(const Command& cmd);
    Response handleWriteBankMem(const Command& cmd);
    Response handleSetNextRegs(const Command& cmd);
    Response handleGetTbblueReg(const Command& cmd);
    Response handleReadMemBlocks(const Command& cmd);
    Response handleGetSpritesPalette(const Command& cmd);
    Response handleGetSprites(const Command& cmd);
    Response handleGetSpritePatterns(const Command& cmd);
    Response handleGetSpritesClip(const Command& cmd);
    Response handleSetBorder(const Command& cmd);
    Response handleReadPort(const Command& cmd);
    Response handleWritePort(const Command& cmd);
    // dezogif's commands (DeZog sends them to a real ZX Next only; not advertised, answered in their shape)
    Response handleSetBreakpoints(const Command& cmd);
    Response handleRestoreMem(const Command& cmd);
    Response handleLoopback(const Command& cmd);
    Response handleExecAsm(const Command& cmd);
    Response handleInterruptOnOff(const Command& cmd);
    Response handleReadState(const Command& cmd);
    Response handleWriteState(const Command& cmd);
    Response handleGetHistoryInfo(const Command& cmd);
    Response handleGetHistoryEntry(const Command& cmd);

    // Serialize registers + slots in CMD_GET_REGISTERS layout (shared by GET_REGISTERS / history)
    void appendRegisters(std::vector<uint8_t>& out, const IDebugInterface::Registers& regs,
                         const std::vector<uint8_t>& slots);

    // Build NAK response
    Response makeNak(uint8_t seqNo);

    // Send all bytes atomically (mutex-protected)
    bool sendAll(SOCKET socket, const std::vector<uint8_t>& data);

    IDebugInterface* m_debug;
    ServerConfig m_config;
    SOCKET m_listenSocket = INVALID_SOCKET;
    uint16_t m_actualPort = 0;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::thread m_acceptThread;

    // Active session - protects both m_clientSocket and socket writes
    mutable std::mutex m_sessionMutex;
    SOCKET m_clientSocket = INVALID_SOCKET;

    // Breakpoint tracking (permanent breakpoints only - temp handled by IDebugInterface)
    std::unordered_map<uint16_t, uint16_t> m_breakpoints;  // id -> addr

    // Action to run after the current response has been written to the socket.
    // Used by CMD_PAUSE so the ACK always precedes the resulting NTF_PAUSE.
    std::function<void()> m_postResponseAction;
};

} // namespace dzrp
