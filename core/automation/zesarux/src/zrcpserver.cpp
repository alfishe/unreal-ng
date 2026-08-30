#include "zrcpserver.h"
#include "zesaruxcondition.h"
#include "platform-sockets.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace zrcp
{
namespace
{
/// ASCII uppercase. DeZog matches "CALL"/"RST " (and the DZRP disassembly view
/// expects conventional casing), but our Z80 disassembler emits lowercase.
std::string toUpperAscii(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string trimString(const std::string& s)
{
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin])))
        ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return s.substr(begin, end - begin);
}

std::vector<std::string> splitWords(const std::string& s)
{
    std::vector<std::string> words;
    size_t pos = 0;
    while (pos < s.size())
    {
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
            ++pos;
        if (pos >= s.size())
            break;
        const size_t start = pos;
        while (pos < s.size() && !std::isspace(static_cast<unsigned char>(s[pos])))
            ++pos;
        words.push_back(s.substr(start, pos - start));
    }
    return words;
}

// ZEsarUX parse_string_to_number semantics: plain digits are decimal (that is
// what DeZog sends for read-memory/write-memory-raw/disassemble/set-register),
// "0x"/"#" prefix or trailing h/H mean hex (set-membreakpoint sends "<hex>h").
bool parseNumber(const std::string& token, long& out)
{
    std::string t = trimString(token);
    if (t.empty())
        return false;
    if (t[0] == '#')
        t = "0x" + t.substr(1);

    int base = 10;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
        base = 16;
    else if (t.back() == 'h' || t.back() == 'H')
    {
        t.pop_back();
        base = 16;
    }

    char* end = nullptr;
    const long value = std::strtol(t.c_str(), &end, base);
    if (end == t.c_str() || *end != '\0')
        return false;
    out = value;
    return true;
}

bool parseYesNo(const std::string& token, bool fallback)
{
    const std::string upper = toUpperAscii(trimString(token));
    if (upper == "YES" || upper == "Y" || upper == "1" || upper == "TRUE")
        return true;
    if (upper == "NO" || upper == "N" || upper == "0" || upper == "FALSE")
        return false;
    return fallback;
}

// print_registers flag letters (order is load-bearing): S Z 5 H 3 P N C,
// '-' when the flag is clear.
std::string flagsToString(uint8_t flags)
{
    static const char kLetters[8] = {'S', 'Z', '5', 'H', '3', 'P', 'N', 'C'};
    std::string out(8, '-');
    for (int i = 0; i < 8; ++i)
        if (flags & (0x80 >> i))
            out[i] = kLetters[i];
    return out;
}

std::string hex4Upper(uint16_t value)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04X", value);
    return buf;
}

std::string hex2Upper(uint8_t value)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02X", value);
    return buf;
}

bool isRstOpcode(uint8_t opcode)
{
    return (opcode & 0xC7) == 0xC7;
}

bool isUnbankedMachine(dzrp::MachineType type)
{
    return type == dzrp::MachineType::ZX48K || type == dzrp::MachineType::ZX16K;
}

// ZRCP MMU word for one 16 KB slot: 0x8000|romPage for ROM, else the RAM bank.
// DeZog parseSlots maps >= 0x8000 to bank 8+(v&1) (its ROM0/ROM1 encoding).
uint16_t mmuWordForSlot(int slot, const std::vector<uint8_t>& slots, bool unbanked)
{
    // dzrp getSlots() is DeZog's 48K model ({0, 1}); the ZEsarUX 48K page
    // layout is ROM0, bank 5, bank 2, bank 0.
    static const uint16_t kUnbanked[4] = {0x8000, 5, 2, 0};
    if (unbanked)
        return kUnbanked[slot];

    const uint8_t v = slot < static_cast<int>(slots.size()) ? slots[slot] : 0;
    // dzrp slot encoding: 8 = ROM0, 9 = ROM1, otherwise RAM bank number
    return v >= 8 ? static_cast<uint16_t>(0x8000 | ((v - 8) & 1)) : v;
}

// Zone digit of the remote_disassemble address prefix: RAM bank / ROM page
// visible at addr. Always emitted so the prefix stays exactly 7 chars wide
// (DeZog slices mnemonic = substring(7, 7+4)).
int zoneDigitForAddr(uint16_t addr, const std::vector<uint8_t>& slots, bool unbanked)
{
    const uint16_t word = mmuWordForSlot(addr >> 14, slots, unbanked);
    return word >= 0x8000 ? (word & 1) : word;
}

std::string machineString(dzrp::MachineType type)
{
    // DeZog dispatches case-insensitively on "48k"/"128k" substrings. All
    // banked models report 128K (documented approximation for clones).
    return isUnbankedMachine(type) ? "ZX Spectrum 48K" : "ZX Spectrum 128K";
}

// set-register NAME=<value> mapping onto dzrp::RegisterId (case-insensitive)
struct RegisterNameEntry
{
    const char* name;
    dzrp::RegisterId id;
};

const RegisterNameEntry kRegisterNames[] = {
    {"PC", dzrp::RegisterId::PC},   {"SP", dzrp::RegisterId::SP},
    {"AF", dzrp::RegisterId::AF},   {"BC", dzrp::RegisterId::BC},
    {"DE", dzrp::RegisterId::DE},   {"HL", dzrp::RegisterId::HL},
    {"IX", dzrp::RegisterId::IX},   {"IY", dzrp::RegisterId::IY},
    {"AF'", dzrp::RegisterId::AF2}, {"BC'", dzrp::RegisterId::BC2},
    {"DE'", dzrp::RegisterId::DE2}, {"HL'", dzrp::RegisterId::HL2},
    {"A", dzrp::RegisterId::A},     {"F", dzrp::RegisterId::F},
    {"B", dzrp::RegisterId::B},     {"C", dzrp::RegisterId::C},
    {"D", dzrp::RegisterId::D},     {"E", dzrp::RegisterId::E},
    {"H", dzrp::RegisterId::H},     {"L", dzrp::RegisterId::L},
    {"IXH", dzrp::RegisterId::IXH}, {"IXL", dzrp::RegisterId::IXL},
    {"IYH", dzrp::RegisterId::IYH}, {"IYL", dzrp::RegisterId::IYL},
    {"A'", dzrp::RegisterId::A2},   {"F'", dzrp::RegisterId::F2},
    {"B'", dzrp::RegisterId::B2},   {"C'", dzrp::RegisterId::C2},
    {"D'", dzrp::RegisterId::D2},   {"E'", dzrp::RegisterId::E2},
    {"H'", dzrp::RegisterId::H2},   {"L'", dzrp::RegisterId::L2},
    {"I", dzrp::RegisterId::I},     {"R", dzrp::RegisterId::R},
    {"IM", dzrp::RegisterId::IM},
};

// Number of recorded history entries (indices 0..size-1 are valid). Probed
// with an exponential + binary search over getHistoryEntry (no size API).
uint32_t probeHistorySize(dzrp::IDebugInterface* debug)
{
    if (!debug->isHistoryAvailable())
        return 0;
    if (!debug->getHistoryEntry(0).has_value())
        return 0;

    uint32_t lo = 0;  // highest known-valid index
    uint32_t hi = 2;  // lowest known-invalid index
    while (debug->getHistoryEntry(hi).has_value())
    {
        lo = hi;
        if (hi >= (1u << 25))
            return hi + 1;  // probe cap: enormous history, report what is verified
        hi <<= 1;
    }
    while (lo + 1 < hi)
    {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (debug->getHistoryEntry(mid).has_value())
            lo = mid;
        else
            hi = mid;
    }
    return lo + 1;
}
}  // namespace

Server::Server(dzrp::IDebugInterface* debug, const ServerConfig& config)
    : m_debug(debug)
    , m_config(config)
{
}

Server::~Server()
{
    stop();
}

bool Server::start()
{
    if (m_running.load())
        return true;

    initializeSockets();

    m_listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenSocket == INVALID_SOCKET)
    {
        std::cerr << "[ZRCP] Failed to create socket\n";
        return false;
    }

    int opt = 1;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_config.port);
    inet_pton(AF_INET, m_config.bindAddress.c_str(), &addr.sin_addr);

    if (bind(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        std::cerr << "[ZRCP] Failed to bind to port " << m_config.port << "\n";
        closeSocket(m_listenSocket);
        m_listenSocket = -1;
        return false;
    }

    socklen_t addrLen = sizeof(addr);
    getsockname(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), &addrLen);
    m_actualPort = ntohs(addr.sin_port);

    if (listen(m_listenSocket, 1) < 0)
    {
        std::cerr << "[ZRCP] Failed to listen\n";
        closeSocket(m_listenSocket);
        m_listenSocket = -1;
        return false;
    }

    m_running.store(true);
    m_stopRequested.store(false);
    m_acceptThread = std::thread(&Server::acceptLoop, this);

    std::cout << "[ZRCP] Server listening on " << m_config.bindAddress
              << ":" << m_actualPort << "\n";
    return true;
}

void Server::stop()
{
    if (!m_running.load())
        return;

    m_stopRequested.store(true);

    if (m_listenSocket != INVALID_SOCKET)
    {
        closeSocket(m_listenSocket);
        m_listenSocket = -1;
    }

    {
        std::lock_guard<std::mutex> lock(m_sessionMutex);
        if (m_clientSocket != INVALID_SOCKET)
        {
            closeSocket(m_clientSocket);
            m_clientSocket = -1;
        }
    }

    // Wake a blocked run-waiter (it re-checks m_stopRequested)
    {
        std::lock_guard<std::mutex> lock(m_runMutex);
        m_runCv.notify_all();
    }

    if (m_acceptThread.joinable())
        m_acceptThread.join();

    m_running.store(false);
    cleanupSockets();

    std::cout << "[ZRCP] Server stopped\n";
}

void Server::notifyPause(dzrp::BreakReason reason, uint16_t addr, uint8_t /*bank*/)
{
    std::lock_guard<std::mutex> lock(m_runMutex);
    m_runStopFlag = true;
    m_runReason = reason;
    m_runAddr = addr;
    m_runCv.notify_all();
}

void Server::acceptLoop()
{
    while (!m_stopRequested.load())
    {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);

        int clientSock = accept(m_listenSocket,
                                reinterpret_cast<sockaddr*>(&clientAddr),
                                &clientLen);

        if (clientSock == INVALID_SOCKET)
        {
            if (m_stopRequested.load())
                break;
            continue;
        }

        char clientIp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp));
        std::cout << "[ZRCP] Client connected from " << clientIp << "\n";

        {
            std::lock_guard<std::mutex> lock(m_sessionMutex);
            m_clientSocket = clientSock;
        }

        sessionLoop(clientSock);

        // Session teardown: drop our breakpoint/watchpoint tables (the adapter
        // removed the emulator-owned ones in onSessionClosed) and reset state.
        for (auto& [id, entry] : m_breakpoints)
            uninstallBreakpoint(entry);
        m_breakpoints.clear();
        for (const WatchKey& watch : m_watchpoints)
            m_debug->removeWatchpoint(watch.addr, 0, watch.size,
                                      static_cast<dzrp::WatchAccess>(watch.access));
        m_watchpoints.clear();

        {
            std::lock_guard<std::mutex> lock(m_sessionMutex);
            if (m_clientSocket == clientSock)
            {
                closeSocket(m_clientSocket);
                m_clientSocket = -1;
            }
        }

        if (m_sessionOpened)
        {
            m_debug->onSessionClosed();
            m_sessionOpened = false;
        }
        m_enteredCpuStep = false;
        m_debugSettings = 0;
        m_historyEnabled = false;
        m_historyStarted = false;
        m_historyMaxSize = 32768;
        m_extendedStackEnabled = false;
        m_codeCoverageEnabled = false;

        std::cout << "[ZRCP] Client disconnected\n";
    }
}

void Server::sessionLoop(int clientSocket)
{
    // Welcome banner + prompt (DeZog waits for the prompt after connect)
    sendLine(clientSocket, "Welcome to " + m_config.serverName +
                               " - ZEsarUX ZRCP " + SERVER_VERSION + " compatible debug server");
    sendPrompt(clientSocket);

    std::string recvBuffer;
    char tempBuf[4096];

    while (!m_stopRequested.load())
    {
        const int pollResult = waitForSocketRead(clientSocket, 100);
        if (pollResult < 0)
            break;
        if (pollResult == 0)
            continue;

        const ssize_t bytesRead =
            recv(clientSocket, tempBuf, sizeof(tempBuf), 0);
        if (bytesRead <= 0)
            break;

        recvBuffer.append(tempBuf, tempBuf + bytesRead);
        if (recvBuffer.size() > 65536)
            break;  // runaway line guard

        size_t newline;
        while ((newline = recvBuffer.find('\n')) != std::string::npos)
        {
            std::string line = recvBuffer.substr(0, newline);
            recvBuffer.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            if (!handleCommand(clientSocket, line))
                return;  // quit (prompt already sent)
            if (!sendPrompt(clientSocket))
                return;
        }
    }
}

/// region <Command dispatch>

bool Server::handleCommand(int sock, const std::string& line)
{
    // New response: sendPrompt decides from this flag whether the answer needs
    // a leading empty line (see there). Only this thread sends.
    m_answerHasData = false;

    // One-line trace: an unanswered command freezes DeZog's serialized queue
    // with no client-side hint, so keep the last command visible.
    std::string trace = line;
    if (trace.size() > 80)
        trace = trace.substr(0, 77) + "...";
    std::cout << "[ZRCP] cmd: " << trace << "\n";

    const size_t spacePos = line.find(' ');
    const std::string cmd =
        (spacePos == std::string::npos) ? line : line.substr(0, spacePos);
    std::string params =
        (spacePos == std::string::npos) ? std::string() : line.substr(spacePos + 1);
    while (!params.empty() && params[0] == ' ')
        params.erase(params.begin());
    const std::vector<std::string> words = splitWords(params);

    if (cmd.empty())
        return true;  // blank line: ack (prompt only)

    if (cmd == "quit")
    {
        sendPrompt(sock);  // client waits for the prompt before closing
        return false;
    }
    if (cmd == "close-all-menus")
        return true;
    if (cmd == "about")
    {
        sendLine(sock, m_config.serverName + " ZRCP debug server (ZEsarUX " +
                          SERVER_VERSION + " wire compatible)");
        return true;
    }
    if (cmd == "get-version")
    {
        sendLine(sock, SERVER_VERSION);
        return true;
    }
    if (cmd == "set-debug-settings")
    {
        long value = 0;
        if (!parseNumber(params, value))
        {
            sendLine(sock, "ERROR. No parameter set");
            return true;
        }
        m_debugSettings = static_cast<unsigned>(value);
        return true;
    }
    if (cmd == "hard-reset-cpu")
    {
        sendLog(sock, "hard-reset-cpu ignored: machine resets are owned by the host emulator");
        return true;
    }
    if (cmd == "enter-cpu-step")
    {
        if (!m_sessionOpened)
        {
            if (!m_debug->waitForTarget(2000))
            {
                sendLine(sock, "Error. Can not enter cpu step mode");
                return true;
            }
            // ZEsarUX pauses the core when entering cpu-step mode; the adapter
            // does the same (broadcast so the host UI reflects the pause).
            m_debug->onSessionOpened();
            m_sessionOpened = true;
        }
        m_enteredCpuStep = true;
        return true;
    }
    if (cmd == "exit-cpu-step")
    {
        m_enteredCpuStep = false;
        return true;  // staying paused; session close resumes the target
    }
    if (cmd == "get-current-machine")
    {
        sendLine(sock, machineString(m_debug->getMachineType()));
        return true;
    }
    if (cmd == "smartload" || cmd == "load")
    {
        sendLog(sock, "load request ignored ('" + params +
                          "'): program loading is owned by the host emulator");
        return true;
    }
    if (cmd == "get-registers")
    {
        sendLine(sock, buildRegistersLine(m_debug->getRegisters(), m_debug->getSlots()));
        return true;
    }
    if (cmd == "set-register")
    {
        handleSetRegister(sock, params);
        return true;
    }
    if (cmd == "read-memory")
    {
        handleReadMemory(sock, words);
        return true;
    }
    if (cmd == "write-memory-raw")
    {
        handleWriteMemoryRaw(sock, words);
        return true;
    }
    if (cmd == "disassemble")
    {
        handleDisassemble(sock, words);
        return true;
    }
    if (cmd == "cpu-step")
    {
        handleCpuStep(sock);
        return true;
    }
    if (cmd == "cpu-step-over")
    {
        handleCpuStepOver(sock);
        return true;
    }
    if (cmd == "run")
    {
        // Options (verbose/limit/...) are accepted and ignored; DeZog sends
        // the plain command. The "Running until" line must go out before any
        // client interrupt (DeZog refuses to interrupt before seeing it).
        if (!m_enteredCpuStep)
        {
            sendLine(sock, "Error. You must first enter cpu-step mode");
            return true;
        }
        if (!sendLine(sock,
                      "Running until a breakpoint, key press or data sent, menu "
                      "opening or other event"))
            return false;  // broken socket: do not start an uninterruptable run
        runUntilStop(sock);
        return true;
    }
    if (cmd == "set-breakpointaction")
        return true;  // no action support; entry is created by set-breakpoint
    if (cmd == "set-breakpoint")
    {
        handleSetBreakpoint(sock, words, params);
        return true;
    }
    if (cmd == "set-breakpointpasscount")
    {
        long id = 0;
        long count = 0;
        if (words.size() >= 2 && parseNumber(words[0], id) && parseNumber(words[1], count))
        {
            BreakpointEntry& entry = m_breakpoints[static_cast<uint16_t>(id)];
            entry.passCount = static_cast<uint32_t>(count);
            entry.hitCount = 0;
        }
        return true;
    }
    if (cmd == "enable-breakpoint")
    {
        long id = 0;
        if (!words.empty() && parseNumber(words[0], id))
        {
            BreakpointEntry& entry = m_breakpoints[static_cast<uint16_t>(id)];
            entry.enabled = true;
            installBreakpoint(entry);
        }
        return true;
    }
    if (cmd == "disable-breakpoint")
    {
        // Init sends 100 of these; unknown ids are silently tolerated.
        long id = 0;
        if (!words.empty() && parseNumber(words[0], id))
        {
            auto it = m_breakpoints.find(static_cast<uint16_t>(id));
            if (it != m_breakpoints.end())
            {
                it->second.enabled = false;
                uninstallBreakpoint(it->second);
            }
        }
        return true;
    }
    if (cmd == "enable-breakpoints")
    {
        for (auto& [id, entry] : m_breakpoints)
        {
            entry.enabled = true;
            installBreakpoint(entry);
        }
        return true;
    }
    if (cmd == "disable-breakpoints")
    {
        for (auto& [id, entry] : m_breakpoints)
        {
            entry.enabled = false;
            uninstallBreakpoint(entry);
        }
        return true;
    }
    if (cmd == "clear-membreakpoints")
    {
        for (const WatchKey& watch : m_watchpoints)
            m_debug->removeWatchpoint(watch.addr, 0, watch.size,
                                      static_cast<dzrp::WatchAccess>(watch.access));
        m_watchpoints.clear();
        return true;
    }
    if (cmd == "set-membreakpoint")
    {
        handleSetMembreakpoint(sock, words);
        return true;
    }
    if (cmd == "cpu-history")
    {
        handleCpuHistory(sock, words);
        return true;
    }
    if (cmd == "extended-stack")
    {
        handleExtendedStack(sock, words);
        return true;
    }
    if (cmd == "cpu-code-coverage")
    {
        // Coverage data is not recorded (documented limitation).
        if (!words.empty())
        {
            if (words[0] == "enabled" && words.size() >= 2)
            {
                if (words[1] != "get")
                    m_codeCoverageEnabled = parseYesNo(words[1], m_codeCoverageEnabled);
            }
            // "enabled get", "get" and "clear" answer with nothing
        }
        return true;
    }
    if (cmd == "get-memory-pages")
    {
        const bool unbanked = isUnbankedMachine(m_debug->getMachineType());
        const std::vector<uint8_t> slots = m_debug->getSlots();
        // e.g. "RO1 RA5 RA2 RA0 " - one token per 16 KB page, each followed by
        // a space (DeZog counts split(' ').length - 1 tokens)
        std::string out;
        for (int slot = 0; slot < 4; ++slot)
        {
            const uint16_t word = mmuWordForSlot(slot, slots, unbanked);
            out += word >= 0x8000 ? "RO" : "RA";
            out += std::to_string(word >= 0x8000 ? (word & 1) : word);
            out += ' ';
        }
        sendLine(sock, out);
        return true;
    }
    if (cmd == "get-tstates-partial")
    {
        sendLine(sock, "0");  // no t-states counter in the Emulator API
        return true;
    }
    if (cmd == "reset-tstates-partial")
        return true;
    if (cmd == "get-cpu-frequency")
    {
        sendLine(sock, "3500000");  // CPU_CLOCK_RATE of the ZX Spectrum models
        return true;
    }

    sendLine(sock, "Unknown command");
    return true;
}

void Server::handleSetRegister(int sock, const std::string& params)
{
    const size_t eq = params.find('=');
    if (eq == std::string::npos)
    {
        sendLine(sock, "Error. Unknown parameter");
        return;
    }

    const std::string name = toUpperAscii(trimString(params.substr(0, eq)));
    long value = 0;
    if (!parseNumber(params.substr(eq + 1), value))
    {
        sendLine(sock, "Error. Unknown parameter");
        return;
    }

    // IFF1/IFF2 are not modeled: acknowledge and ignore (ZEsarUX-style parity)
    if (name == "IFF1" || name == "IFF2")
        return;

    for (const RegisterNameEntry& entry : kRegisterNames)
    {
        if (name == entry.name)
        {
            m_debug->setRegister(entry.id, static_cast<uint16_t>(value & 0xFFFF));
            return;
        }
    }
    sendLine(sock, "Error. Unknown register " + name);
}

void Server::handleReadMemory(int sock, const std::vector<std::string>& words)
{
    long addr = 0;
    long len = 1;
    if (words.empty() || !parseNumber(words[0], addr))
    {
        sendLine(sock, "ERROR. No parameter set");
        return;
    }
    if (words.size() >= 2 && !parseNumber(words[1], len))
        len = 1;
    if (len < 0)
        len = 0;
    if (len > 65536)
    {
        // One 64K address space is all there is (DeZog fetches it whole for
        // its disassembly view: read-memory 0 65536).
        sendLine(sock, "Error. Invalid length");
        return;
    }

    // readMemory() takes a uint16_t length and wraps addresses itself - read
    // in chunks so a full 64K request (length 65536 truncates to 0 as uint16)
    // still yields exactly len bytes.
    std::vector<uint8_t> data;
    data.reserve(static_cast<size_t>(len));
    uint16_t cursor = static_cast<uint16_t>(addr);
    for (long remaining = len; remaining > 0;)
    {
        const uint16_t chunk = static_cast<uint16_t>(remaining < 0xFFFF ? remaining : 0xFFFF);
        const std::vector<uint8_t> part = m_debug->readMemory(cursor, chunk);
        data.insert(data.end(), part.begin(), part.end());
        cursor = static_cast<uint16_t>(cursor + chunk);
        remaining -= chunk;
    }
    std::string hex;
    hex.reserve(data.size() * 2);
    for (uint8_t byte : data)
        hex += hex2Upper(byte);
    sendLine(sock, hex);
}

void Server::handleWriteMemoryRaw(int sock, const std::vector<std::string>& words)
{
    if (words.size() < 2)
    {
        sendLine(sock, "ERROR. No parameter set");
        return;
    }

    long addr = 0;
    if (!parseNumber(words[0], addr))
    {
        sendLine(sock, "ERROR. No parameter set");
        return;
    }

    std::string hex;
    for (size_t i = 1; i < words.size(); ++i)
        hex += words[i];
    if (hex.size() % 2 != 0 ||
        hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
    {
        sendLine(sock, "Error. Invalid data");
        return;
    }

    std::vector<uint8_t> data;
    data.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2)
        data.push_back(static_cast<uint8_t>(std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16)));

    m_debug->writeMemory(static_cast<uint16_t>(addr), data);
}

void Server::handleDisassemble(int sock, const std::vector<std::string>& words)
{
    uint16_t addr = m_debug->getRegisters().pc;
    if (!words.empty())
    {
        long value = 0;
        if (!parseNumber(words[0], value))
        {
            sendLine(sock, "Error. Invalid address");
            return;
        }
        addr = static_cast<uint16_t>(value);
    }
    long count = 1;
    if (words.size() >= 2 && !parseNumber(words[1], count))
        count = 1;
    if (count < 1)
        count = 1;
    if (count > 4096)
        count = 4096;

    const std::vector<uint8_t> slots = m_debug->getSlots();
    for (long i = 0; i < count; ++i)
    {
        sendLine(sock, buildDisassemblyLine(addr, slots));
        uint8_t len = 0;
        m_debug->disassembleInstruction(addr, &len);
        addr = static_cast<uint16_t>(addr + (len ? len : 1));
    }
}

void Server::handleCpuStep(int sock)
{
    if (!m_enteredCpuStep)
    {
        sendLine(sock, "Error. You must first enter cpu-step mode");
        return;
    }
    if (!m_debug->stepOnce())
    {
        sendLine(sock, "Error. You must first enter cpu-step mode");
        return;
    }
    sendStepOutput(sock);
}

void Server::handleCpuStepOver(int sock)
{
    if (!m_enteredCpuStep)
    {
        sendLine(sock, "Error. You must first enter cpu-step mode");
        return;
    }

    const uint16_t pc = m_debug->getRegisters().pc;
    uint8_t len = 0;
    const std::string mnemonic = toUpperAscii(m_debug->disassembleInstruction(pc, &len));

    // ZEsarUX cpu-step-over: CALL/RST run until the instruction after them
    // (temporary breakpoint); block-repeats (LDIR...) run until PC changes;
    // everything else is a single step.
    if (mnemonic.rfind("CALL", 0) == 0 || mnemonic.rfind("RST", 0) == 0)
    {
        if (len == 0)
            len = fallbackInstructionLength(pc);
        const uint16_t adapterId =
            m_debug->addBreakpoint(static_cast<uint16_t>(pc + len), 0, "", true);
        // Step-over output is just regs+disassembly: no "Running until"
        // banner and no "Breakpoint fired" echo (ZEsarUX parity)
        runUntilStop(sock, false);
        if (adapterId)
            m_debug->removeBreakpoint(adapterId);
        m_debug->clearTemporaryBreakpoints();
        return;
    }

    const bool blockRepeat = mnemonic.rfind("LDIR", 0) == 0 ||
                             mnemonic.rfind("LDDR", 0) == 0 ||
                             mnemonic.rfind("CPIR", 0) == 0 ||
                             mnemonic.rfind("CPDR", 0) == 0;
    if (blockRepeat)
    {
        // One step executes one repetition; step until the whole block
        // instruction finishes (PC advances), with a safety cap.
        for (uint32_t i = 0; i < 65536 && m_debug->stepOnce(); ++i)
        {
            if (m_debug->getRegisters().pc != pc)
                break;
        }
    }
    else if (!m_debug->stepOnce())
    {
        sendLine(sock, "Error. You must first enter cpu-step mode");
        return;
    }
    sendStepOutput(sock);
}

void Server::handleSetBreakpoint(int sock, const std::vector<std::string>& words,
                                 const std::string& params)
{
    if (words.empty())
    {
        sendLine(sock, "Error. No parameters set");
        return;
    }
    long id = 0;
    if (!parseNumber(words[0], id))
    {
        sendLine(sock, "Error. Index out of range");
        return;
    }

    // Condition = everything after the id (may contain spaces)
    std::string condition;
    const size_t spacePos = params.find(' ');
    if (spacePos != std::string::npos)
        condition = trimString(params.substr(spacePos + 1));

    BreakpointEntry& entry = m_breakpoints[static_cast<uint16_t>(id)];
    uninstallBreakpoint(entry);
    entry.condition = condition;
    entry.hitCount = 0;

    // DeZog's removal idiom: set-breakpoint <id> 0. A plain numeric
    // condition never fires in ZEsarUX either - drop it silently
    long neverFires = 0;
    if (!condition.empty() && parseNumber(condition, neverFires))
    {
        entry.conditionRest.clear();
        entry.hasAddress = false;
        return;
    }

    uint16_t addr = 0;
    std::string rest;
    if (extractPcLiteral(condition, addr, rest))
    {
        // DeZog address breakpoint: "PC=<hex>h [and ROM=n] [and RAM=bank] ..."
        entry.addr = addr;
        entry.conditionRest = rest;
        entry.hasAddress = true;
    }
    else
    {
        // No PC literal: DeZog's step-over sends "SP>=<sp>" and step-out sends
        // "PC=PEEKW(SP-2) AND SP>=<bpSp>". Resolve the stop address now and
        // re-evaluate the full condition on every hit.
        entry.conditionRest = condition;
        entry.hasAddress = resolveConditionAddress(condition, entry.addr);
        if (!entry.hasAddress)
        {
            sendLog(sock, "breakpoint " + std::to_string(id) +
                              " has no address condition and can not fire: '" +
                              condition + "'");
        }
    }

    if (entry.enabled && entry.hasAddress)
        installBreakpoint(entry);
}

void Server::handleSetMembreakpoint(int sock, const std::vector<std::string>& words)
{
    if (words.size() < 2)
    {
        sendLine(sock, "ERROR. No parameter set");
        return;
    }

    long addr = 0;
    long type = 0;
    if (!parseNumber(words[0], addr) || !parseNumber(words[1], type))
    {
        sendLine(sock, "Error. Invalid parameter");
        return;
    }
    long size = 1;
    if (words.size() >= 3 && !parseNumber(words[2], size))
        size = 1;
    if (size < 1)
        size = 1;

    if (type == 0)
    {
        // Remove: match by address + size (DeZog forgets the access type)
        for (auto it = m_watchpoints.begin(); it != m_watchpoints.end(); ++it)
        {
            if (it->addr == (addr & 0xFFFF) && it->size == (size & 0xFFFF))
            {
                m_debug->removeWatchpoint(it->addr, 0, it->size,
                                          static_cast<dzrp::WatchAccess>(it->access));
                m_watchpoints.erase(it);
                break;
            }
        }
        return;
    }

    dzrp::WatchAccess access;
    if (type == 1)
        access = dzrp::WatchAccess::READ;
    else if (type == 2)
        access = dzrp::WatchAccess::WRITE;
    else if (type == 3)
        access = dzrp::WatchAccess::READ_WRITE;
    else
    {
        sendLine(sock, "Error. Invalid type");
        return;
    }

    const uint16_t watchAddr = static_cast<uint16_t>(addr & 0xFFFF);
    const uint16_t watchSize = static_cast<uint16_t>(size & 0xFFFF);
    if (m_debug->addWatchpoint(watchAddr, 0, watchSize, access))
    {
        WatchKey key;
        key.addr = watchAddr;
        key.size = watchSize;
        key.access = static_cast<uint8_t>(access);
        m_watchpoints.push_back(key);
    }
    else
    {
        sendLine(sock, "Error. Can not set memory breakpoint");
    }
}

void Server::handleCpuHistory(int sock, const std::vector<std::string>& words)
{
    const std::string sub = words.empty() ? std::string() : words[0];
    const std::string value = words.size() >= 2 ? words[1] : std::string();

    if (sub == "enabled")
    {
        m_historyEnabled = parseYesNo(value, m_historyEnabled);
        return;
    }
    if (sub == "started")
    {
        m_historyStarted = parseYesNo(value, m_historyStarted);
        return;
    }
    if (sub == "set-max-size")
    {
        long size = 0;
        if (parseNumber(value, size) && size >= 0)
            m_historyMaxSize = static_cast<unsigned>(size);
        return;
    }
    if (sub == "clear")
        return;  // ring buffer clear is not exposed; harmless no-op
    if (sub == "ignrephalt" || sub == "ignrepldxr")
        return;
    if (sub == "is-enabled")
    {
        sendLine(sock, m_historyEnabled ? "1" : "0");
        return;
    }
    if (sub == "is-started")
    {
        sendLine(sock, m_historyStarted ? "1" : "0");
        return;
    }
    if (sub == "get-size")
    {
        sendLine(sock, std::to_string(probeHistorySize(m_debug)));
        return;
    }
    if (sub == "get-max-size")
    {
        sendLine(sock, std::to_string(m_historyMaxSize));
        return;
    }
    if (sub == "get")
    {
        long index = 0;
        if (!parseNumber(value, index) || index < 0)
        {
            sendLine(sock, "ERROR: index out of range");
            return;
        }
        const std::optional<dzrp::IDebugInterface::HistoryEntry> entry =
            m_debug->getHistoryEntry(static_cast<uint32_t>(index));
        if (!entry.has_value())
        {
            // Exact ZEsarUX error text; DeZog checks substring(0,5)=="error"
            sendLine(sock, "ERROR: index out of range");
            return;
        }
        sendLine(sock, buildHistoryLine(*entry, entry->slots));
        return;
    }
    sendLine(sock, "Error. Unknown parameter");
}

void Server::handleExtendedStack(int sock, const std::vector<std::string>& words)
{
    const std::string sub = words.empty() ? std::string() : words[0];

    if (sub == "enabled")
    {
        m_extendedStackEnabled =
            parseYesNo(words.size() >= 2 ? words[1] : std::string(), m_extendedStackEnabled);
        return;
    }
    if (sub == "clear")
        return;
    if (sub == "get")
    {
        if (!m_extendedStackEnabled)
        {
            sendLine(sock, "Error. It's not enabled");
            return;
        }
        long count = 0;
        if (words.size() < 2 || !parseNumber(words[1], count) || count < 0)
        {
            sendLine(sock, "ERROR. No parameter set");
            return;
        }
        if (count > 4096)
            count = 4096;

        // Optional second parameter: stack start address (defaults to SP)
        uint16_t sp = m_debug->getRegisters().sp;
        if (words.size() >= 3)
        {
            long start = 0;
            if (parseNumber(words[2], start))
                sp = static_cast<uint16_t>(start);
        }

        for (long i = 0; i < count; ++i)
        {
            const uint16_t addr = static_cast<uint16_t>(sp + 2 * i);
            const uint16_t value =
                static_cast<uint16_t>(readMemByte(addr) | (readMemByte(static_cast<uint16_t>(addr + 1)) << 8));

            // ZEsarUX tracks the type per pushed value; we approximate with
            // the same opcode heuristics DeZog's stack view uses.
            const char* type = "push";
            if (readMemByte(static_cast<uint16_t>(value - 3)) == 0xCD)
                type = "call";
            else if (isRstOpcode(readMemByte(static_cast<uint16_t>(value - 1))))
                type = "rst";
            sendLine(sock, hex4Upper(value) + "H " + type);
        }
        return;
    }
    sendLine(sock, "Error. Unknown parameter");
}

/// endregion </Command dispatch>

/// region <Wire formatting>

std::string Server::buildMmuString(const std::vector<uint8_t>& slots)
{
    const bool unbanked = isUnbankedMachine(m_debug->getMachineType());
    std::string out;
    out.reserve(32);
    char buf[8];
    // First 4 groups = the 16 KB slots (all DeZog parses); groups 5-8 repeat
    // them, contiguous %04x words like print_registers
    for (int rep = 0; rep < 2; ++rep)
    {
        for (int slot = 0; slot < 4; ++slot)
        {
            std::snprintf(buf, sizeof(buf), "%04x", mmuWordForSlot(slot, slots, unbanked));
            out += buf;
        }
    }
    return out;
}

std::string Server::buildRegistersLine(const dzrp::IDebugInterface::Registers& regs,
                                       const std::vector<uint8_t>& slots)
{
    // Byte-exact print_registers format (field order is load-bearing for
    // DeZog's memoized indexOf offsets; note the TWO spaces after R=)
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "PC=%04x SP=%04x AF=%04x BC=%04x HL=%04x DE=%04x IX=%04x IY=%04x "
                  "AF'=%04x BC'=%04x HL'=%04x DE'=%04x I=%02x R=%02x  F=%s F'=%s "
                  "MEMPTR=%04x IM%d IFF-- VPS: 50 MMU=%s",
                  regs.pc, regs.sp, regs.af, regs.bc, regs.hl, regs.de, regs.ix, regs.iy,
                  regs.af2, regs.bc2, regs.hl2, regs.de2, regs.i, regs.r,
                  flagsToString(regs.af & 0xFF).c_str(),
                  flagsToString(regs.af2 & 0xFF).c_str(),
                  0 /* MEMPTR not modeled */, regs.im,
                  buildMmuString(slots).c_str());
    return buf;
}

std::string Server::buildHistoryLine(const dzrp::IDebugInterface::HistoryEntry& entry,
                                     const std::vector<uint8_t>& slots)
{
    // Byte-exact cpu_history_legacy_regs_bin_to_string format: no F=/MEMPTR/VPS,
    // (PC) = 4 opcode bytes in memory order, (SP) hi-lo, trailing space after
    // the MMU groups (ZEsarUX appends modified-memory info there)
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "PC=%04x SP=%04x AF=%04x BC=%04x HL=%04x DE=%04x IX=%04x IY=%04x "
                  "AF'=%04x BC'=%04x HL'=%04x DE'=%04x I=%02x R=%02x IM%d IFF-- "
                  "(PC)=%02x%02x%02x%02x (SP)=%02x%02x MMU=%s ",
                  entry.regs.pc, entry.regs.sp, entry.regs.af, entry.regs.bc,
                  entry.regs.hl, entry.regs.de, entry.regs.ix, entry.regs.iy,
                  entry.regs.af2, entry.regs.bc2, entry.regs.hl2, entry.regs.de2,
                  entry.regs.i, entry.regs.r, entry.regs.im,
                  entry.opcodes[0], entry.opcodes[1], entry.opcodes[2], entry.opcodes[3],
                  (entry.spContent >> 8) & 0xFF, entry.spContent & 0xFF,
                  buildMmuString(slots).c_str());
    return buf;
}

std::string Server::buildDisassemblyLine(uint16_t addr, const std::vector<uint8_t>& slots)
{
    uint8_t len = 0;
    std::string mnemonic = m_debug->disassembleInstruction(addr, &len);
    if (mnemonic.empty())
        mnemonic = "?";

    // 7-char prefix "%04X %X " (address, zone digit) + uppercased mnemonic:
    // DeZog slices the opcode via substring(7, 7+4)
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04X %X ", addr,
                  zoneDigitForAddr(addr, slots, isUnbankedMachine(m_debug->getMachineType())));
    return std::string(buf) + toUpperAscii(mnemonic);
}

bool Server::sendStepOutput(int sock)
{
    // remote_get_regs_disassemble with debug settings bit 0: optional
    // registers line (with TSTATES suffix) + one disassembly line at PC
    if (m_debugSettings & 1)
    {
        if (!sendLine(sock, buildRegistersLine(m_debug->getRegisters(), m_debug->getSlots()) +
                                " TSTATES: 0"))
            return false;
    }
    const uint16_t pc = m_debug->getRegisters().pc;
    return sendLine(sock, buildDisassemblyLine(pc, m_debug->getSlots()));
}

/// endregion </Wire formatting>

/// region <Execution>

// Consumes the client line that interrupted the run (DeZog pauses by sending
// a bare newline). Anything else is logged and discarded.
void drainInterruptLine(int sock)
{
    char buf[256];
    while (true)
    {
        const ssize_t n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0)
            return;
        if (std::memchr(buf, '\n', static_cast<size_t>(n)) != nullptr)
            return;
    }
}

void Server::runUntilStop(int sock, bool reportStop)
{
    while (!m_stopRequested.load())
    {
        // Clear the stop flag BEFORE resuming so a breakpoint that fires
        // immediately cannot be missed (lost-stop race)
        {
            std::lock_guard<std::mutex> lock(m_runMutex);
            m_runStopFlag = false;
            m_runReason = dzrp::BreakReason::NONE;
            m_runAddr = 0;
        }
        m_debug->resume();

        bool interrupted = false;
        while (true)
        {
            if (m_stopRequested.load())
                return;
            {
                std::unique_lock<std::mutex> lock(m_runMutex);
                m_runCv.wait_for(lock, std::chrono::milliseconds(5),
                                 [this] { return m_runStopFlag; });
                if (m_runStopFlag)
                    break;
            }
            // Client data interrupts the run (EOF counts as readable). The
            // pause blocks until the Z80 thread parks and notifies MANUAL.
            if (!interrupted && waitForSocketRead(sock, 0) > 0)
            {
                interrupted = true;
                drainInterruptLine(sock);
                m_debug->pause();
            }
        }

        dzrp::BreakReason reason;
        uint16_t addr;
        {
            std::lock_guard<std::mutex> lock(m_runMutex);
            reason = m_runReason;
            addr = m_runAddr;
        }

        BreakpointEntry* entry =
            reason == dzrp::BreakReason::BREAKPOINT ? findBreakpointByAddr(addr) : nullptr;
        if (entry && !breakpointShouldStop(*entry))
            continue;  // condition false / pass count not reached: resume silently

        if (reason == dzrp::BreakReason::BREAKPOINT && reportStop)
        {
            // ZEsarUX echoes the breakpoint condition
            const std::string detail =
                entry && !entry->condition.empty() ? entry->condition : "PC=" + hex4Upper(addr) + "H";
            sendLine(sock, "Breakpoint fired: " + detail);
        }
        else if (reason == dzrp::BreakReason::WATCHPOINT_READ && reportStop)
        {
            sendLine(sock, "Breakpoint fired: Memory Breakpoint Read Address: " + hex4Upper(addr) + "H");
        }
        else if (reason == dzrp::BreakReason::WATCHPOINT_WRITE && reportStop)
        {
            sendLine(sock, "Breakpoint fired: Memory Breakpoint Write Address: " + hex4Upper(addr) + "H");
        }
        // MANUAL / OTHER: no "Breakpoint fired" line, just the step output
        break;
    }

    sendStepOutput(sock);
}

/// endregion </Execution>

/// region <Breakpoint management>

void Server::installBreakpoint(BreakpointEntry& entry)
{
    if (entry.adapterId || !entry.hasAddress)
        return;
    // Conditions/pass counts are evaluated server-side on each hit; the
    // adapter only needs the address
    entry.adapterId = m_debug->addBreakpoint(entry.addr, 0, "", false);
    if (!entry.adapterId)
        std::cout << "[ZRCP] Failed to install breakpoint at " << hex4Upper(entry.addr) << "h\n";
}

void Server::uninstallBreakpoint(BreakpointEntry& entry)
{
    if (entry.adapterId)
    {
        m_debug->removeBreakpoint(entry.adapterId);
        entry.adapterId = 0;
    }
}

Server::BreakpointEntry* Server::findBreakpointByAddr(uint16_t addr)
{
    for (auto& [id, entry] : m_breakpoints)
    {
        if (entry.enabled && entry.hasAddress && entry.addr == addr)
            return &entry;
    }
    return nullptr;
}

bool Server::breakpointShouldStop(BreakpointEntry& entry)
{
    bool stop = true;
    if (!entry.conditionRest.empty())
    {
        dzrp::IDebugInterface::Registers regs = m_debug->getRegisters();
        std::vector<uint8_t> slots = m_debug->getSlots();
        ConditionContext context;
        context.regs = &regs;
        context.slots = &slots;
        context.breakpointAddr = entry.addr;
        context.readMem = [this](uint16_t a) { return readMemByte(a); };

        std::string error;
        stop = ConditionEvaluator::evaluate(entry.conditionRest, context, &error);
        if (!error.empty())
        {
            // Parse failures stop conservatively (see zesaruxcondition.h)
            std::cout << "[ZRCP] Condition error ('" << entry.conditionRest << "'): " << error
                      << " - stopping\n";
        }
    }

    if (stop && entry.passCount > 0)
    {
        ++entry.hitCount;
        if (entry.hitCount < entry.passCount)
            stop = false;  // skip the first passCount-1 hits
    }
    return stop;
}

uint8_t Server::fallbackInstructionLength(uint16_t pc)
{
    // Only needed when the disassembler is unavailable and the opcode is a
    // CALL/RST (the only shapes DeZog's step-over produces)
    const uint8_t opcode = readMemByte(pc);
    if (opcode == 0xCD || (opcode & 0xC7) == 0xC4)
        return 3;  // CALL nn / CALL cc,nn
    return 1;      // RST p (and anything else)
}

bool Server::resolveConditionAddress(const std::string& condition, uint16_t& addrOut)
{
    // Both no-PC-literal shapes DeZog emits carry an "SP>=<decimal>" term.
    const std::string upper = toUpperAscii(trimString(condition));
    const size_t spPos = upper.find("SP>=");
    if (spPos == std::string::npos)
        return false;

    size_t numStart = spPos + 4;
    size_t numEnd = numStart;
    while (numEnd < upper.size() && std::isdigit(static_cast<unsigned char>(upper[numEnd])))
        ++numEnd;
    if (numEnd == numStart)
        return false;
    const uint16_t bpSp = static_cast<uint16_t>(
        std::strtoul(upper.substr(numStart, numEnd - numStart).c_str(), nullptr, 10));

    if (upper.compare(0, 4, "SP>=") == 0)
    {
        // Step-over: "SP>=<current SP>" - stop after the CALL/RST at PC
        // returns, i.e. at the instruction following it
        const uint16_t pc = m_debug->getRegisters().pc;
        uint8_t len = 0;
        m_debug->disassembleInstruction(pc, &len);
        if (len == 0)
            len = fallbackInstructionLength(pc);
        addrOut = static_cast<uint16_t>(pc + len);
        return true;
    }

    // Step-out: "PC=PEEKW(SP-2) AND SP>=<bpSp>" - the return address of the
    // frame popped at SP=bpSp is the word stored at bpSp-2
    addrOut = static_cast<uint16_t>(readMemByte(static_cast<uint16_t>(bpSp - 2)) |
                                    (readMemByte(static_cast<uint16_t>(bpSp - 1)) << 8));
    return true;
}

/// endregion </Breakpoint management>

/// region <Low-level I/O>

bool Server::sendRaw(int sock, const std::string& text)
{
    size_t sent = 0;
    while (sent < text.size())
    {
        const ssize_t n = send(sock, text.data() + sent, text.size() - sent, 0);
        if (n <= 0)
            return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool Server::sendLine(int sock, const std::string& line)
{
    m_answerHasData = true;
    return sendRaw(sock, line + "\n");
}

bool Server::sendPrompt(int sock)
{
    // DeZog (zesaruxsocket.ts receiveSocket) accepts a response only when the
    // chunk splits into >= 2 lines and the last one starts with "command" and
    // ends with "> ". "log> " lines are removed BEFORE that check, so an
    // answer that sent no data line needs an empty line of its own to keep the
    // prompt off split index 0 - otherwise DeZog times out ("did not answer in
    // time") on commands like close-all-menus or hard-reset-cpu.
    if (!m_answerHasData && !sendRaw(sock, "\n"))
        return false;
    // No trailing newline, exactly like ZEsarUX
    return sendRaw(sock, PROMPT);
}

void Server::sendLog(int sock, const std::string& text)
{
    // "log> " lines are forwarded to the DeZog output panel. Sent via sendRaw
    // on purpose: DeZog strips log lines BEFORE its prompt check, so they must
    // not mark the answer as having data - a log-only answer still needs the
    // leading empty line from sendPrompt.
    sendRaw(sock, "log> " + text + "\n");
}

uint8_t Server::readMemByte(uint16_t addr)
{
    const std::vector<uint8_t> data = m_debug->readMemory(addr, 1);
    return data.empty() ? 0 : data[0];
}

/// endregion </Low-level I/O>
}  // namespace zrcp
