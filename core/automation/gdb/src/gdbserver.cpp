#include "gdbserver.h"
#include "gdbpacket.h"
#include "gdbtarget_z80.h"

#include <common/uuid.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/emulatorcontext.h>
#include <emulator/memory/memory.h>
#include <debugger/debugmanager.h>
#include <debugger/breakpoints/breakpointmanager.h>

#include <iostream>
#include <sstream>

// Socket includes
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

GDBServer::GDBServer() = default;

GDBServer::~GDBServer()
{
    stop();
}

bool GDBServer::start(uint16_t port, const std::string& bindAddress)
{
    if (_running)
    {
        return false;
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "GDBServer: Failed to initialize Winsock" << std::endl;
        return false;
    }
#endif

    _serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (_serverSocket < 0)
    {
        std::cerr << "GDBServer: Failed to create socket" << std::endl;
        return false;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(_serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, bindAddress.c_str(), &addr.sin_addr) <= 0)
    {
        std::cerr << "GDBServer: Invalid bind address: " << bindAddress << std::endl;
#ifdef _WIN32
        closesocket(_serverSocket);
#else
        close(_serverSocket);
#endif
        _serverSocket = -1;
        return false;
    }

    if (bind(_serverSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        std::cerr << "GDBServer: Failed to bind to " << bindAddress << ":" << port << std::endl;
#ifdef _WIN32
        closesocket(_serverSocket);
#else
        close(_serverSocket);
#endif
        _serverSocket = -1;
        return false;
    }

    if (listen(_serverSocket, 5) < 0)
    {
        std::cerr << "GDBServer: Failed to listen" << std::endl;
#ifdef _WIN32
        closesocket(_serverSocket);
#else
        close(_serverSocket);
#endif
        _serverSocket = -1;
        return false;
    }

    _port = port;
    _bindAddress = bindAddress;
    _running = true;
    _stopping = false;

    _acceptThread = std::make_unique<std::thread>(&GDBServer::acceptLoop, this);

    std::cout << "GDBServer: Listening on " << bindAddress << ":" << port << std::endl;
    return true;
}

void GDBServer::stop()
{
    if (!_running)
        return;

    _stopping = true;
    _running = false;

    // Close server socket to unblock accept()
    if (_serverSocket >= 0)
    {
#ifdef _WIN32
        closesocket(_serverSocket);
#else
        close(_serverSocket);
#endif
        _serverSocket = -1;
    }

    // Wait for accept thread
    if (_acceptThread && _acceptThread->joinable())
    {
        _acceptThread->join();
    }
    _acceptThread.reset();

    // Close all sessions
    {
        std::lock_guard<std::mutex> lock(_sessionsMutex);
        for (auto& [socket, session] : _sessions)
        {
            session->stop();
#ifdef _WIN32
            closesocket(socket);
#else
            close(socket);
#endif
        }
        _sessions.clear();
    }

#ifdef _WIN32
    WSACleanup();
#endif

    std::cout << "GDBServer: Stopped" << std::endl;
}

size_t GDBServer::getSessionCount() const
{
    std::lock_guard<std::mutex> lock(_sessionsMutex);
    return _sessions.size();
}

void GDBServer::acceptLoop()
{
    while (_running && !_stopping)
    {
        struct sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);

        int clientSocket = accept(_serverSocket, (struct sockaddr*)&clientAddr, &clientLen);

        if (clientSocket < 0)
        {
            if (_stopping)
                break;
            continue;
        }

        std::cout << "GDBServer: Client connected from "
                  << inet_ntoa(clientAddr.sin_addr) << std::endl;

        // Handle client in a new thread
        std::thread clientThread(&GDBServer::handleClient, this, clientSocket);
        clientThread.detach();
    }
}

void GDBServer::handleClient(int clientSocket)
{
    auto session = std::make_unique<GDBSession>(clientSocket);
    session->setAutoAttach(_autoAttach);

    {
        std::lock_guard<std::mutex> lock(_sessionsMutex);
        _sessions[clientSocket] = std::move(session);
    }

    // This blocks until session ends
    {
        std::lock_guard<std::mutex> lock(_sessionsMutex);
        auto it = _sessions.find(clientSocket);
        if (it != _sessions.end())
        {
            it->second->run();
        }
    }

    // Cleanup
    {
        std::lock_guard<std::mutex> lock(_sessionsMutex);
        _sessions.erase(clientSocket);
    }

#ifdef _WIN32
    closesocket(clientSocket);
#else
    close(clientSocket);
#endif

    std::cout << "GDBServer: Client disconnected" << std::endl;
}

// GDBSession implementation

GDBSession::GDBSession(int socket, bool noAckMode)
    : _socket(socket)
    , _noAckMode(noAckMode)
    , _reader(std::make_unique<GDBPacketReader>())
    , _sessionUuid(UUID::Generate())
{
}

GDBSession::~GDBSession()
{
    stop();
}

void GDBSession::run()
{
    // Auto-attach if enabled and single instance exists
    if (_autoAttach && !_emulator)
    {
        auto* mgr = EmulatorManager::GetInstance();
        if (mgr)
        {
            auto ids = mgr->GetEmulatorIds();
            if (ids.size() == 1)
            {
                _emulator = mgr->GetEmulator(ids[0]);
                if (_emulator)
                {
                    _context = _emulator->GetContext();
                    _attachedInstanceId = ids[0];
                    _lastStopReason = StopReason::Attached;
                }
            }
        }
    }

    while (_active)
    {
        std::string packet = receivePacket();
        if (packet.empty())
        {
            if (!_active)
                break;
            continue;
        }

        std::string response = handlePacket(packet);
        if (!response.empty())
        {
            sendPacket(response);
        }
    }
}

void GDBSession::stop()
{
    _active = false;

    // Release run-control claim
    if (_context)
    {
        _context->ReleaseRunControl(_sessionUuid);
    }

    _emulator.reset();
    _context = nullptr;
}

bool GDBSession::sendPacket(const std::string& data)
{
    std::string packet = GDBPacket::encode(data);
    return sendRaw(packet);
}

bool GDBSession::sendRaw(const std::string& data)
{
#ifdef _WIN32
    int sent = send(_socket, data.c_str(), static_cast<int>(data.size()), 0);
    return sent == static_cast<int>(data.size());
#else
    auto sent = send(_socket, data.c_str(), data.size(), 0);
    return sent == static_cast<decltype(sent)>(data.size());
#endif
}

std::string GDBSession::receivePacket()
{
    _reader->reset();

    char buf[1];
    while (_active)
    {
#ifdef _WIN32
        int n = recv(_socket, buf, 1, 0);
#else
        auto n = recv(_socket, buf, 1, 0);
#endif
        if (n <= 0)
        {
            _active = false;
            return "";
        }

        // Check for interrupt
        if (_reader->hasInterrupt())
        {
            _reader->clearInterrupt();
            return "\x03";
        }

        if (_reader->feed(buf[0]))
        {
            if (_reader->isChecksumValid())
            {
                if (!_noAckMode)
                {
                    sendRaw("+");
                }
                return _reader->extractData();
            }
            else
            {
                if (!_noAckMode)
                {
                    sendRaw("-");
                }
                _reader->reset();
            }
        }
    }

    return "";
}

std::string GDBSession::handlePacket(const std::string& packet)
{
    if (packet.empty())
        return "";

    // Handle interrupt
    if (packet[0] == '\x03')
    {
        return handleInterrupt();
    }

    char cmd = packet[0];
    std::string args = packet.substr(1);

    switch (cmd)
    {
        case '?':
            return formatStopReply();

        case 'g':
            return handleReadRegisters();

        case 'G':
            return handleWriteRegisters(args);

        case 'p':
            return handleReadRegister(args);

        case 'P':
            return handleWriteRegister(args);

        case 'm':
            return handleReadMemory(args);

        case 'M':
        case 'X':
            return handleWriteMemory(args);

        case 'c':
        case 'C':
            return handleContinue();

        case 's':
        case 'S':
            return handleStep();

        case 'Z':
            return handleSetBreakpoint(args);

        case 'z':
            return handleRemoveBreakpoint(args);

        case 'D':
            return handleDetach();

        case 'k':
            // Kill - treat as detach, never terminate emulator
            return handleDetach();

        case 'H':
            // Thread selection - always OK (single-threaded)
            return "OK";

        case 'T':
            // Thread alive query - thread 1 always alive
            return "OK";

        case 'q':
            return handleQuery(packet);

        case 'Q':
            if (packet == "QStartNoAckMode")
            {
                _noAckMode = true;
                return "OK";
            }
            return "";

        case 'v':
            return handleVCommand(packet);

        default:
            return "";  // Empty response = unsupported
    }
}

std::string GDBSession::handleQuery(const std::string& packet)
{
    if (packet.starts_with("qSupported"))
    {
        return handleQSupported(packet.substr(10));
    }
    if (packet.starts_with("qXfer"))
    {
        return handleQXfer(packet.substr(5));
    }
    if (packet == "qC")
    {
        return "QC1";  // Current thread ID = 1
    }
    if (packet == "qfThreadInfo")
    {
        return "m1";  // Thread 1
    }
    if (packet == "qsThreadInfo")
    {
        return "l";  // End of list
    }
    if (packet == "qAttached")
    {
        return "1";  // Attached to existing process
    }
    if (packet.starts_with("qRcmd,"))
    {
        std::string cmd = GDBPacket::hexDecode(packet.substr(6));
        return handleMonitor(cmd);
    }

    return "";  // Unsupported query
}

std::string GDBSession::handleQSupported(const std::string& /*params*/)
{
    std::string caps = "PacketSize=4000;";
    caps += "QStartNoAckMode+;";
    caps += "qXfer:features:read+;";
    caps += "qXfer:osdata:read+;";
    caps += "swbreak+;";
    caps += "hwbreak+;";
    caps += "vContSupported+;";
    caps += "multiprocess-;";

    // TODO: Add ReverseStep+;ReverseContinue+ when TTD is enabled

    return caps;
}

std::string GDBSession::handleQXfer(const std::string& params)
{
    if (params.starts_with(":features:read:target.xml:"))
    {
        std::string xml = GDBTargetZ80::generateFlatTargetXML(_context);
        return "l" + xml;
    }

    if (params.starts_with(":osdata:read:processes:"))
    {
        std::ostringstream xml;
        xml << "<osdata type=\"processes\">\n";

        auto* mgr = EmulatorManager::GetInstance();
        if (mgr)
        {
            auto ids = mgr->GetEmulatorIds();
            int pid = 1;
            for (const auto& id : ids)
            {
                auto emu = mgr->GetEmulator(id);
                std::string state = emu ? (emu->IsPaused() ? "stopped" : "running") : "unknown";
                xml << "  <item>\n";
                xml << "    <column name=\"pid\">" << pid++ << "</column>\n";
                xml << "    <column name=\"user\">emulator</column>\n";
                xml << "    <column name=\"command\">" << id << "</column>\n";
                xml << "    <column name=\"state\">" << state << "</column>\n";
                xml << "  </item>\n";
            }
        }

        xml << "</osdata>";
        return "l" + xml.str();
    }

    return "";
}

std::string GDBSession::handleVCommand(const std::string& packet)
{
    if (packet == "vMustReplyEmpty")
    {
        return "";
    }

    if (packet.starts_with("vCont?"))
    {
        return "vCont;c;C;s;S";
    }

    if (packet.starts_with("vCont;"))
    {
        if (packet.find('c') != std::string::npos)
        {
            return handleContinue();
        }
        if (packet.find('s') != std::string::npos)
        {
            return handleStep();
        }
    }

    if (packet.starts_with("vAttach;"))
    {
        return handleAttach(packet.substr(8));
    }

    return "";
}

std::string GDBSession::handleMonitor(const std::string& cmd)
{
    std::string response;

    if (cmd == "help")
    {
        response = "Available commands:\n";
        response += "  monitor help      - this message\n";
        response += "  monitor model     - show emulator model\n";
        response += "  monitor status    - show emulator status\n";
        response += "  monitor reset     - reset emulator\n";
        response += "  monitor instances - list emulator instances\n";
        response += "  monitor frame     - show frame/tstate info\n";
    }
    else if (cmd == "model")
    {
        if (_emulator)
        {
            response = "Model: ZX Spectrum\n";  // TODO: get actual model name from config
        }
        else
        {
            response = "No emulator attached\n";
        }
    }
    else if (cmd == "status")
    {
        if (_emulator && _context)
        {
            std::ostringstream ss;
            ss << "Status: " << (_emulator->IsPaused() ? "paused" : "running") << "\n";
            ss << "Instance: " << _attachedInstanceId << "\n";

            auto claimState = _context->GetRunControlState();
            if (claimState.claimed)
            {
                ss << "Run-control: claimed by " << claimState.surfaceLabel << "\n";
            }
            else
            {
                ss << "Run-control: unclaimed\n";
            }

            response = ss.str();
        }
        else
        {
            response = "No emulator attached\n";
        }
    }
    else if (cmd == "reset")
    {
        if (_emulator)
        {
            if (!_emulator->IsPaused())
            {
                response = "Error: emulator must be paused to reset\n";
            }
            else
            {
                _emulator->Reset();
                response = "Emulator reset\n";
            }
        }
        else
        {
            response = "No emulator attached\n";
        }
    }
    else if (cmd == "instances")
    {
        auto* mgr = EmulatorManager::GetInstance();
        if (mgr)
        {
            auto ids = mgr->GetEmulatorIds();
            std::ostringstream ss;
            ss << "Instances: " << ids.size() << "\n";
            int pid = 1;
            for (const auto& id : ids)
            {
                auto emu = mgr->GetEmulator(id);
                std::string state = emu ? (emu->IsPaused() ? "paused" : "running") : "unknown";
                ss << "  " << pid++ << ": " << id << " (" << state << ")\n";
            }
            response = ss.str();
        }
        else
        {
            response = "EmulatorManager not available\n";
        }
    }
    else if (cmd == "frame")
    {
        if (_emulator)
        {
            Z80State* state = _emulator->GetZ80State();
            if (state)
            {
                std::ostringstream ss;
                ss << "T-state: " << state->t << "\n";
                ss << "PC: 0x" << std::hex << state->pc << "\n";
                response = ss.str();
            }
            else
            {
                response = "Z80 state not available\n";
            }
        }
        else
        {
            response = "No emulator attached\n";
        }
    }
    else
    {
        response = "Unknown monitor command: " + cmd + "\nType 'monitor help' for available commands.\n";
    }

    return GDBPacket::hexEncode(response);
}

std::string GDBSession::handleReadRegisters()
{
    if (!_context)
    {
        return "E31";  // Not attached
    }

    return GDBTargetZ80::serializeRegisters(_context);
}

std::string GDBSession::handleWriteRegisters(const std::string& data)
{
    if (!_context)
    {
        return "E31";
    }

    if (!_emulator || !_emulator->IsPaused())
    {
        return "E0D";  // Must be paused to write
    }

    if (GDBTargetZ80::deserializeRegisters(_context, data))
    {
        return "OK";
    }

    return "E01";
}

std::string GDBSession::handleReadRegister(const std::string& params)
{
    auto regnum = GDBPacket::parseHex(params);
    if (!regnum)
    {
        return "E01";
    }

    if (!_context)
    {
        return "E31";
    }

    return GDBTargetZ80::readRegister(_context, static_cast<int>(*regnum));
}

std::string GDBSession::handleWriteRegister(const std::string& params)
{
    auto eqPos = params.find('=');
    if (eqPos == std::string::npos)
    {
        return "E01";
    }

    auto regnum = GDBPacket::parseHex(params.substr(0, eqPos));
    if (!regnum)
    {
        return "E01";
    }

    if (!_context)
    {
        return "E31";
    }

    if (!_emulator || !_emulator->IsPaused())
    {
        return "E0D";
    }

    return GDBTargetZ80::writeRegister(_context, static_cast<int>(*regnum), params.substr(eqPos + 1));
}

std::string GDBSession::handleReadMemory(const std::string& params)
{
    auto commaPos = params.find(',');
    if (commaPos == std::string::npos)
    {
        return "E01";
    }

    auto addr = GDBPacket::parseHex(params.substr(0, commaPos));
    auto len = GDBPacket::parseHex(params.substr(commaPos + 1));

    if (!addr || !len)
    {
        return "E01";
    }

    if (!_context || !_context->pMemory)
    {
        return "E31";
    }

    Memory* memory = _context->pMemory;
    std::string result;
    result.reserve(*len * 2);

    for (size_t i = 0; i < *len; i++)
    {
        uint16_t address = static_cast<uint16_t>((*addr + i) & 0xFFFF);
        uint8_t byte = memory->DirectReadFromZ80Memory(address);
        result += GDBPacket::toHex(byte, 2);
    }

    return result;
}

std::string GDBSession::handleWriteMemory(const std::string& params)
{
    // M addr,length:data or X addr,length:binary
    auto commaPos = params.find(',');
    auto colonPos = params.find(':');

    if (commaPos == std::string::npos || colonPos == std::string::npos)
    {
        return "E01";
    }

    auto addr = GDBPacket::parseHex(params.substr(0, commaPos));
    auto len = GDBPacket::parseHex(params.substr(commaPos + 1, colonPos - commaPos - 1));

    if (!addr || !len)
    {
        return "E01";
    }

    if (!_context || !_context->pMemory)
    {
        return "E31";
    }

    if (!_emulator || !_emulator->IsPaused())
    {
        return "E0D";
    }

    std::string data = params.substr(colonPos + 1);
    Memory* memory = _context->pMemory;

    // Decode hex data
    auto bytes = GDBPacket::hexToBytes(data);
    if (bytes.size() != *len)
    {
        return "E01";
    }

    for (size_t i = 0; i < bytes.size(); i++)
    {
        uint16_t address = static_cast<uint16_t>((*addr + i) & 0xFFFF);
        memory->DirectWriteToZ80Memory(address, bytes[i]);
    }

    return "OK";
}

std::string GDBSession::handleSetBreakpoint(const std::string& params)
{
    // Z type,addr,kind
    auto firstComma = params.find(',');
    auto secondComma = params.find(',', firstComma + 1);

    if (firstComma == std::string::npos)
    {
        return "E01";
    }

    auto type = GDBPacket::parseHex(params.substr(0, firstComma));
    auto addr = GDBPacket::parseHex(params.substr(firstComma + 1,
        secondComma != std::string::npos ? secondComma - firstComma - 1 : std::string::npos));

    if (!type || !addr)
    {
        return "E01";
    }

    if (!_emulator || !_context)
    {
        return "E31";
    }

    DebugManager* debugManager = _emulator->GetDebugManager();
    if (!debugManager)
    {
        return "E01";
    }

    BreakpointManager* bpManager = debugManager->GetBreakpointsManager();
    if (!bpManager)
    {
        return "E01";
    }

    uint16_t address = static_cast<uint16_t>(*addr & 0xFFFF);

    switch (*type)
    {
        case 0:  // SW breakpoint
        case 1:  // HW breakpoint (same for emulator)
            bpManager->AddExecutionBreakpoint(address, "gdb");
            break;

        case 2:  // Write watchpoint
            bpManager->AddMemWriteBreakpoint(address, "gdb");
            break;

        case 3:  // Read watchpoint
            bpManager->AddMemReadBreakpoint(address, "gdb");
            break;

        case 4:  // Access watchpoint
            bpManager->AddMemReadBreakpoint(address, "gdb");
            bpManager->AddMemWriteBreakpoint(address, "gdb");
            break;

        default:
            return "";  // Unsupported
    }

    return "OK";
}

std::string GDBSession::handleRemoveBreakpoint(const std::string& params)
{
    // z type,addr,kind
    auto firstComma = params.find(',');
    auto secondComma = params.find(',', firstComma + 1);

    if (firstComma == std::string::npos)
    {
        return "E01";
    }

    auto type = GDBPacket::parseHex(params.substr(0, firstComma));
    auto addr = GDBPacket::parseHex(params.substr(firstComma + 1,
        secondComma != std::string::npos ? secondComma - firstComma - 1 : std::string::npos));

    if (!type || !addr)
    {
        return "E01";
    }

    if (!_emulator || !_context)
    {
        return "E31";
    }

    DebugManager* debugManager = _emulator->GetDebugManager();
    if (!debugManager)
    {
        return "E01";
    }

    BreakpointManager* bpManager = debugManager->GetBreakpointsManager();
    if (!bpManager)
    {
        return "E01";
    }

    uint16_t address = static_cast<uint16_t>(*addr & 0xFFFF);

    // RemoveBreakpointByAddress removes all breakpoints at that address
    switch (*type)
    {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
            bpManager->RemoveBreakpointByAddress(address);
            break;

        default:
            return "";
    }

    return "OK";
}

std::string GDBSession::handleContinue()
{
    if (!_emulator)
    {
        return "E31";
    }

    if (!_emulator->IsPaused())
    {
        return formatStopReply();  // Already running
    }

    _emulator->Resume();

    // Wait for stop (breakpoint or interrupt)
    // In a real implementation, we'd async wait for NC_EXECUTION_BREAKPOINT
    // For now, return immediately - the emulator runs until breakpoint
    while (_active && !_emulator->IsPaused())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    _lastStopReason = StopReason::Breakpoint;
    return formatStopReply();
}

std::string GDBSession::handleStep()
{
    if (!_emulator)
    {
        return "E31";
    }

    if (!_emulator->IsPaused())
    {
        _emulator->Pause();
    }

    // Execute single instruction
    _emulator->RunSingleCPUCycle(true);

    _lastStopReason = StopReason::Step;
    return formatStopReply();
}

std::string GDBSession::handleInterrupt()
{
    if (_emulator && !_emulator->IsPaused())
    {
        _emulator->Pause();
    }

    _lastStopReason = StopReason::Interrupt;
    return "T02thread:1;";  // SIGINT
}

std::string GDBSession::handleBackwardStep()
{
    // TODO: TTD integration
    return "E01";
}

std::string GDBSession::handleBackwardContinue()
{
    // TODO: TTD integration
    return "E01";
}

std::string GDBSession::formatStopReply()
{
    switch (_lastStopReason)
    {
        case StopReason::Breakpoint:
            return "T05swbreak:;thread:1;";

        case StopReason::Watchpoint:
            return "T05watch:" + GDBPacket::toHex(_lastWatchAddress, 4) + ";thread:1;";

        case StopReason::Interrupt:
            return "T02thread:1;";

        case StopReason::Step:
        case StopReason::Attached:
        case StopReason::None:
        default:
            return "T05thread:1;";
    }
}

std::string GDBSession::handleAttach(const std::string& pid)
{
    auto pidNum = GDBPacket::parseHex(pid);
    if (!pidNum)
    {
        return "E01";
    }

    auto* mgr = EmulatorManager::GetInstance();
    if (!mgr)
    {
        return "E31";
    }

    auto ids = mgr->GetEmulatorIds();
    size_t index = static_cast<size_t>(*pidNum - 1);  // pid is 1-based

    if (index >= ids.size())
    {
        return "E31";
    }

    _emulator = mgr->GetEmulator(ids[index]);
    if (!_emulator)
    {
        return "E31";
    }

    _context = _emulator->GetContext();
    _attachedInstanceId = ids[index];

    // Pause if running
    if (!_emulator->IsPaused())
    {
        _emulator->Pause();
    }

    // Take run-control claim (TDD §3.3)
    if (_context)
    {
        std::string errorReason;
        if (!_context->TakeRunControl(_sessionUuid, "gdb", &errorReason))
        {
            // Another surface holds the claim - report but don't fail attach
            // (read-only operations still allowed)
        }
    }

    _lastStopReason = StopReason::Attached;
    return "T05thread:1;";
}

std::string GDBSession::handleDetach()
{
    // Release run-control claim (TDD §3.3)
    if (_context)
    {
        _context->ReleaseRunControl(_sessionUuid);
    }

    _emulator.reset();
    _context = nullptr;
    _attachedInstanceId.clear();
    _active = false;
    return "OK";
}
