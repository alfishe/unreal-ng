#pragma once

// zrcp::Server - ZEsarUX ZRCP text-protocol debug server (TCP).
//
// Wire-compatible with DeZog's ZesaruxRemote/ZesaruxSocket:
//   - line commands; every response is terminated by the "command...> " prompt
//   - blocking, interruptable "run" (client sends a blank line to pause)
//   - breakpoints are ZEsarUX conditions on numbered ids (1..100; 100 is
//     DeZog's temporary step-out breakpoint)
//   - cpu-history feeds DeZog's native reverse debugging
//
// The server is transport-only: all emulator access goes through
// dzrp::IDebugInterface (shared with the DZRP server; DezogDebugAdapter).
// One client at a time; a second connect waits for the session to end.

#include "dzrpserver.h"
#include "zrcptypes.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace zrcp
{
struct ServerConfig
{
    uint16_t port = DEFAULT_PORT;
    std::string bindAddress = "127.0.0.1";
    std::string serverName = "Unreal-NG";
};

class Server
{
public:
    Server(dzrp::IDebugInterface* debug, const ServerConfig& config = {});
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start();
    void stop();
    bool isRunning() const { return m_running.load(); }
    uint16_t getPort() const { return m_actualPort; }

    // Adapter pause-notifier sink: wakes a blocked "run" (thread-safe)
    void notifyPause(dzrp::BreakReason reason, uint16_t addr, uint8_t bank = 0);

private:
    // One DeZog-side breakpoint (keyed by the zrcp id, 1..100)
    struct BreakpointEntry
    {
        uint16_t addr = 0;          // install address (PC literal / step-out resolution)
        std::string condition;      // full condition (diagnostics)
        std::string conditionRest;  // re-evaluated on every hit (without PC literal)
        bool hasAddress = false;    // false when no address could be derived
        bool enabled = false;
        uint32_t passCount = 0;     // 0 = stop on every hit
        uint32_t hitCount = 0;
        uint16_t adapterId = 0;     // dzrp::IDebugInterface breakpoint id, 0 = not installed
    };

    struct WatchKey
    {
        uint16_t addr;
        uint16_t size;
        uint8_t access;
    };

    void acceptLoop();
    void sessionLoop(int clientSocket);

    // Dispatches one command line. Returns false when the session must end
    // (quit or broken socket). The prompt is sent by the caller afterwards.
    bool handleCommand(int sock, const std::string& line);

    // --- command sub-handlers (send their own response lines) ---
    void handleSetRegister(int sock, const std::string& params);
    void handleReadMemory(int sock, const std::vector<std::string>& words);
    void handleWriteMemoryRaw(int sock, const std::vector<std::string>& words);
    void handleDisassemble(int sock, const std::vector<std::string>& words);
    void handleCpuStep(int sock);
    void handleCpuStepOver(int sock);
    void handleSetBreakpoint(int sock, const std::vector<std::string>& words,
                             const std::string& params);
    void handleSetMembreakpoint(int sock, const std::vector<std::string>& words);
    void handleCpuHistory(int sock, const std::vector<std::string>& words);
    void handleExtendedStack(int sock, const std::vector<std::string>& words);

    // --- wire formatting (byte-exact ZEsarUX formats; see zrcp-server.md) ---
    std::string buildRegistersLine(const dzrp::IDebugInterface::Registers& regs,
                                   const std::vector<uint8_t>& slots);
    std::string buildHistoryLine(const dzrp::IDebugInterface::HistoryEntry& entry,
                                 const std::vector<uint8_t>& slots);
    std::string buildMmuString(const std::vector<uint8_t>& slots);
    std::string buildDisassemblyLine(uint16_t addr, const std::vector<uint8_t>& slots);
    // cpu-step / cpu-step-over / run output: optional registers line + one
    // disassembly line at PC (remote_get_regs_disassemble with settings bit 0)
    bool sendStepOutput(int sock);

    // --- execution ---
    // Blocks until the emulator stops or the client sends data (interrupt).
    // reportStop=false suppresses the "Breakpoint fired" lines (cpu-step-over
    // output is regs+disassembly only, like ZEsarUX).
    void runUntilStop(int sock, bool reportStop = true);

    // --- breakpoint management ---
    void installBreakpoint(BreakpointEntry& entry);
    void uninstallBreakpoint(BreakpointEntry& entry);
    BreakpointEntry* findBreakpointByAddr(uint16_t addr);
    bool breakpointShouldStop(BreakpointEntry& entry);  // condition + pass count

    // Instruction length when disassembleInstruction() reports none
    // (CALL / conditional CALL -> 3, RST -> 1, else 1)
    uint8_t fallbackInstructionLength(uint16_t pc);
    // Derives the install address from a step-over/step-out style condition
    // ("SP>=<n>"): pc+len or word@(bpSp-2). Returns false when no SP>= found.
    bool resolveConditionAddress(const std::string& condition, uint16_t& addrOut);

    // --- low-level I/O ---
    bool sendRaw(int sock, const std::string& text);
    bool sendLine(int sock, const std::string& line);   // appends '\n'
    bool sendPrompt(int sock);                          // no trailing newline (like ZEsarUX)
    void sendLog(int sock, const std::string& text);    // "log> " prefixed

    // Byte read helper shared with the condition evaluator
    uint8_t readMemByte(uint16_t addr);

    dzrp::IDebugInterface* m_debug;
    ServerConfig m_config;
    int m_listenSocket = -1;
    uint16_t m_actualPort = 0;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::thread m_acceptThread;
    std::mutex m_sessionMutex;
    int m_clientSocket = -1;

    // Per-session state (session thread only unless noted)
    bool m_sessionOpened = false;   // enter-cpu-step ran onSessionOpened
    bool m_enteredCpuStep = false;
    unsigned m_debugSettings = 0;   // set-debug-settings bitmask (bit 0 = regs output)
    // Set by sendLine, reset per command: an answer without any data line
    // gets an empty line before the prompt (DeZog splits the chunk on '\n'
    // and requires the prompt to be a line of its own).
    bool m_answerHasData = false;
    std::unordered_map<uint16_t, BreakpointEntry> m_breakpoints;
    std::vector<WatchKey> m_watchpoints;
    bool m_historyEnabled = false;
    bool m_historyStarted = false;
    unsigned m_historyMaxSize = 32768;  // echoes set-max-size (get-max-size)
    bool m_extendedStackEnabled = false;
    bool m_codeCoverageEnabled = false;

    // Run waiter: notifyPause() arrives on emulator threads
    std::mutex m_runMutex;
    std::condition_variable m_runCv;
    bool m_runStopFlag = false;
    dzrp::BreakReason m_runReason = dzrp::BreakReason::NONE;
    uint16_t m_runAddr = 0;
};
} // namespace zrcp
