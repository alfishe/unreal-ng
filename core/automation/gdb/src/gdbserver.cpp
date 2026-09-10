#include "gdbserver.h"
#include "gdbpacket.h"
#include "gdbtarget_z80.h"

#include <common/uuid.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/emulatorcontext.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <emulator/notifications.h>
#include <debugger/debugmanager.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/ttd/timetravelmanager.h>
#include <debugger/ttd/ttd_external_events.h>
#include <debugger/ttd/ttd_probe.h>
#include <3rdparty/message-center/messagecenter.h>

#include <cctype>
#include <iomanip>
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

    // Close all sessions - shutdown first to unblock recv()
    {
        std::lock_guard<std::mutex> lock(_sessionsMutex);
        for (auto& [socket, session] : _sessions)
        {
            session->stop();
#ifdef _WIN32
            shutdown(socket, SD_BOTH);
            closesocket(socket);
#else
            shutdown(socket, SHUT_RDWR);
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

    GDBSession* sessionPtr = session.get();

    {
        std::lock_guard<std::mutex> lock(_sessionsMutex);
        _sessions[clientSocket] = std::move(session);
    }

    // Run session WITHOUT holding mutex (allows stop() to work)
    sessionPtr->run();

    // Cleanup
    {
        std::lock_guard<std::mutex> lock(_sessionsMutex);
        _sessions.erase(clientSocket);
    }

#ifdef _WIN32
    shutdown(clientSocket, SD_BOTH);
    closesocket(clientSocket);
#else
    shutdown(clientSocket, SHUT_RDWR);
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
        // Empty response = unsupported (no reply). Special marker = send empty packet.
        if (!response.empty())
        {
            if (response == "\x7f_EMPTY_")
            {
                sendPacket("");  // Send empty packet $#00
            }
            else
            {
                sendPacket(response);
            }
        }
    }
}

void GDBSession::stop()
{
    _active = false;

    // Unsubscribe from state changes (1A.7.3)
    unsubscribeFromStateChanges();

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

        case 'b':
            // Reverse execution commands
            if (args == "s")
                return handleBackwardStep();
            if (args == "c")
                return handleBackwardContinue();
            return "";  // Unknown 'b' command

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
        return "QC01";  // Current thread ID = 1 (hex)
    }
    if (packet == "qfThreadInfo")
    {
        return "m01";  // Thread 1 (hex)
    }
    if (packet == "qsThreadInfo")
    {
        return "l";  // End of list
    }
    if (packet == "qAttached")
    {
        return "1";  // Attached to existing process
    }
    if (packet.starts_with("qThreadExtraInfo,"))
    {
        // Return hex-encoded description of thread
        return GDBPacket::hexEncode("Z80 CPU");
    }
    if (packet.starts_with("qRcmd,"))
    {
        std::string cmd = GDBPacket::hexDecode(packet.substr(6));
        return handleMonitor(cmd);
    }

    // Trace packets - empty response means "not supported"
    if (packet == "qTStatus" || packet.starts_with("qTfV") || packet.starts_with("qTsV") ||
        packet.starts_with("qTfP") || packet.starts_with("qTsP") || packet.starts_with("qTV:") ||
        packet.starts_with("qTBuffer"))
    {
        return "\x7f_EMPTY_";
    }

    // Symbol lookup
    if (packet.starts_with("qSymbol"))
    {
        return "OK";  // No symbol lookup needed
    }

    // Section offsets - not applicable for bare metal
    if (packet == "qOffsets")
    {
        return "\x7f_EMPTY_";
    }

    // Old-style thread queries (qL, qP)
    if (packet.starts_with("qL") || packet.starts_with("qP"))
    {
        return "\x7f_EMPTY_";
    }

    return "";  // Unsupported query
}

std::string GDBSession::handleQSupported(const std::string& /*params*/)
{
    std::string caps = "PacketSize=4000;";
    caps += "QStartNoAckMode+;";
    caps += "qXfer:features:read+;";
    caps += "qXfer:osdata:read+;";
    caps += "qXfer:threads:read+;";
    caps += "swbreak+;";
    caps += "hwbreak+;";
    caps += "vContSupported+;";
    caps += "multiprocess-;";

    // Advertise reverse debugging support when TTD is available
    if (_context && _context->pTimeTravelManager)
    {
        caps += "ReverseStep+;";
        caps += "ReverseContinue+;";
    }

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

    // Provide minimal threads XML to help GDB
    if (params.starts_with(":threads:read:"))
    {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\"?>\n";
        xml << "<threads>\n";
        xml << "  <thread id=\"01\" core=\"0\" name=\"Z80\"/>\n";
        xml << "</threads>\n";
        return "l" + xml.str();
    }

    // These qXfer types need empty response (not silence) to avoid timeouts
    if (params.starts_with(":exec-file:read:") ||
        params.starts_with(":auxv:read:") ||
        params.starts_with(":libraries:read:") ||
        params.starts_with(":memory-map:read:") ||
        params.starts_with(":spu:read:") ||
        params.starts_with(":siginfo:read:"))
    {
        return "\x7f_EMPTY_";  // Not supported, but respond
    }

    return "";
}

std::string GDBSession::handleVCommand(const std::string& packet)
{
    if (packet == "vMustReplyEmpty")
    {
        // GDB spec: must reply with empty packet $#00, not silence
        return "\x7f_EMPTY_";  // Marker to send empty packet
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
        response += "  monitor bankinfo  - show memory bank info\n";
        response += "  monitor load <path> - load snap/tape/disk (paused only)\n";
        response += "  monitor ttd status  - show TTD session info\n";
        response += "  monitor ttd start   - start TTD recording\n";
        response += "  monitor ttd stop    - stop TTD recording\n";
        response += "  monitor ttd seek <frame> - seek to frame\n";
        response += "  monitor ttd findlast <w|r|x> <addr> - find last access\n";
        response += "  monitor bport <in|out> <port> - set port breakpoint\n";
        response += "  monitor bport clear <id> - remove port breakpoint\n";
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
    else if (cmd == "bankinfo")
    {
        if (_context && _context->pMemory)
        {
            Memory* mem = _context->pMemory;
            std::ostringstream ss;
            ss << "Memory banks:\n";
            ss << std::hex << std::setfill('0');
            ss << "  0000-3FFF: " << (mem->IsBank0ROM() ? "ROM" : "RAM")
               << " page " << std::setw(2) << (mem->IsBank0ROM() ? mem->GetROMPage() : mem->GetRAMPageForBank0()) << "\n";
            ss << "  4000-7FFF: RAM page " << std::setw(2) << mem->GetRAMPageForBank1() << "\n";
            ss << "  8000-BFFF: RAM page " << std::setw(2) << mem->GetRAMPageForBank2() << "\n";
            ss << "  C000-FFFF: RAM page " << std::setw(2) << mem->GetRAMPageForBank3() << "\n";
            response = ss.str();
        }
        else
        {
            response = "No emulator attached\n";
        }
    }
    else if (cmd == "load" || cmd.starts_with("load "))
    {
        std::string path = cmd.length() > 5 ? cmd.substr(5) : "";
        if (_emulator)
        {
            if (!_emulator->IsPaused())
            {
                response = "Error: emulator must be paused to load\n";
            }
            else if (path.empty())
            {
                response = "Error: path required\n";
            }
            else
            {
                std::string ext = path.substr(path.find_last_of('.') + 1);
                for (auto& c : ext) c = static_cast<char>(std::tolower(c));

                bool ok = false;
                if (ext == "sna" || ext == "z80" || ext == "szx")
                {
                    ok = _emulator->LoadSnapshot(path);
                }
                else if (ext == "tap" || ext == "tzx")
                {
                    ok = _emulator->LoadTape(path);
                }
                else if (ext == "trd" || ext == "scl" || ext == "fdi")
                {
                    ok = _emulator->LoadDisk(path);
                }
                else
                {
                    response = "Error: unsupported file type '" + ext + "'\n";
                }

                if (ok)
                {
                    response = "Loaded: " + path + "\n";
                }
                else if (response.empty())
                {
                    response = "Error: failed to load '" + path + "'\n";
                }
            }
        }
        else
        {
            response = "No emulator attached\n";
        }
    }
    else if (cmd.starts_with("ttd "))
    {
        std::string subcmd = cmd.substr(4);
        if (!_context || !_context->pTimeTravelManager)
        {
            response = "TTD not available\n";
        }
        else
        {
            auto* ttd = _context->pTimeTravelManager;

            if (subcmd == "status")
            {
                auto info = ttd->GetSessionInfo();
                std::ostringstream ss;
                ss << "TTD state: " << ttd::TTDSessionStateToString(info.state) << "\n";
                ss << "Session start frame: " << info.sessionStartFrame << "\n";
                ss << "Current end frame: " << info.currentEndFrame << "\n";
                ss << "Checkpoints: " << info.checkpointCount << "\n";
                ss << "Page store: " << (info.pageStoreUsedBytes / 1024) << " KB used\n";

                auto pos = ttd->CurrentPosition();
                ss << "Current position: frame " << pos.frame << ", t=" << pos.tInFrame << "\n";
                response = ss.str();
            }
            else if (subcmd == "start")
            {
                if (!_emulator->IsPaused())
                {
                    response = "Error: emulator must be paused to start TTD\n";
                }
                else
                {
                    if (ttd->StartRecording())
                    {
                        response = "TTD recording started\n";
                    }
                    else
                    {
                        response = "Error: TTD recording failed to start\n";
                    }
                }
            }
            else if (subcmd == "stop")
            {
                ttd->StopRecording();
                response = "TTD recording stopped\n";
            }
            else if (subcmd.starts_with("seek "))
            {
                std::string frameStr = subcmd.substr(5);
                try
                {
                    uint64_t frame = std::stoull(frameStr);
                    if (!_emulator->IsPaused())
                    {
                        _emulator->Pause();
                    }
                    ttd::TTDTimePoint target{frame, 0};
                    ttd::TimeTravelManager::TTDSeekResult result;
                    if (ttd->SeekTo(target, &result))
                    {
                        std::ostringstream ss;
                        ss << "Seeked to frame " << result.arrivedAt.frame << "\n";
                        response = ss.str();
                    }
                    else
                    {
                        response = "Error: seek failed\n";
                    }
                }
                catch (...)
                {
                    response = "Error: invalid frame number\n";
                }
            }
            else if (subcmd.starts_with("findlast "))
            {
                // findlast <w|r|x> <addr>
                std::string args = subcmd.substr(9);
                auto spacePos = args.find(' ');
                if (spacePos == std::string::npos || args.empty())
                {
                    response = "Usage: ttd findlast <w|r|x> <addr>\n";
                }
                else
                {
                    char accessType = args[0];
                    std::string addrStr = args.substr(spacePos + 1);

                    ttd::TTDAccessType access;
                    if (accessType == 'w' || accessType == 'W')
                        access = ttd::TTDAccessType::Write;
                    else if (accessType == 'r' || accessType == 'R')
                        access = ttd::TTDAccessType::Read;
                    else if (accessType == 'x' || accessType == 'X')
                        access = ttd::TTDAccessType::Execute;
                    else
                    {
                        response = "Error: access type must be w, r, or x\n";
                        goto done_ttd;
                    }

                    auto addrOpt = GDBPacket::parseHex(addrStr);
                    if (!addrOpt)
                    {
                        response = "Error: invalid address\n";
                        goto done_ttd;
                    }

                    uint16_t addr = static_cast<uint16_t>(*addrOpt);

                    ttd::TTDSearchQuery query;
                    query.addrFrom = addr;
                    query.addrTo = addr;
                    query.access = access;

                    ttd::TTDExternalEvent marker;
                    auto result = ttd->FindLastAccess(query, &marker);

                    if (result)
                    {
                        std::ostringstream ss;
                        ss << "Found at frame " << result->time.frame
                           << ", t=" << result->time.tInFrame;
                        if (access == ttd::TTDAccessType::Write)
                            ss << ", value=0x" << std::hex << static_cast<int>(result->value);
                        ss << ", pc=0x" << std::hex << result->pc << "\n";
                        response = ss.str();
                    }
                    else if (marker.reason[0] != '\0')
                    {
                        response = "Blocked by barrier: " + std::string(marker.reason) + "\n";
                    }
                    else
                    {
                        response = "Not found in recorded history\n";
                    }
                }
                done_ttd:;
            }
            else
            {
                response = "Unknown TTD command: " + subcmd + "\n";
            }
        }
    }
    else if (cmd.starts_with("bport "))
    {
        std::string args = cmd.substr(6);
        auto* debugMgr = _context ? _context->pDebugManager : nullptr;
        auto* bpMgr = debugMgr ? debugMgr->GetBreakpointsManager() : nullptr;

        if (!bpMgr)
        {
            response = "Error: breakpoint manager not available\n";
        }
        else if (args.starts_with("clear "))
        {
            auto idOpt = GDBPacket::parseHex(args.substr(6));
            if (idOpt)
            {
                if (bpMgr->RemoveBreakpointByID(static_cast<uint16_t>(*idOpt)))
                {
                    response = "Port breakpoint removed\n";
                }
                else
                {
                    response = "Error: breakpoint not found\n";
                }
            }
            else
            {
                response = "Error: invalid breakpoint ID\n";
            }
        }
        else if (args.starts_with("in ") || args.starts_with("out "))
        {
            bool isIn = args.starts_with("in ");
            std::string portStr = args.substr(isIn ? 3 : 4);
            auto portOpt = GDBPacket::parseHex(portStr);

            if (portOpt)
            {
                uint16_t port = static_cast<uint16_t>(*portOpt);
                BreakpointDescriptor* bp = new BreakpointDescriptor();
                bp->type = BRK_IO;
                bp->ioType = isIn ? BRK_IO_IN : BRK_IO_OUT;
                bp->z80address = port;
                bp->owner = "gdb";

                uint16_t id = bpMgr->AddBreakpoint(bp);
                if (id != BRK_INVALID)
                {
                    std::ostringstream ss;
                    ss << "Port breakpoint set: ID " << id << " on port 0x"
                       << std::hex << port << " (" << (isIn ? "IN" : "OUT") << ")\n";
                    response = ss.str();
                }
                else
                {
                    delete bp;
                    response = "Error: failed to add breakpoint\n";
                }
            }
            else
            {
                response = "Error: invalid port number\n";
            }
        }
        else
        {
            response = "Usage: bport <in|out> <port> | bport clear <id>\n";
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

    // Refuse writes in TTD detached state (read-only historical view)
    if (_context->pTimeTravelManager)
    {
        auto state = _context->pTimeTravelManager->GetSessionInfo().state;
        if (state == ttd::TTDSessionState::Detached)
        {
            return "E0D";  // Read-only in detached state
        }
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

    // Refuse writes in TTD detached state
    if (_context->pTimeTravelManager)
    {
        auto state = _context->pTimeTravelManager->GetSessionInfo().state;
        if (state == ttd::TTDSessionState::Detached)
        {
            return "E0D";
        }
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

    // Check for physical memory access (0x01PPAAAA format)
    // High byte 0x01 = RAM page access, PP = page number, AAAA = 16KB offset
    if ((*addr & 0xFF000000) == 0x01000000)
    {
        uint8_t page = static_cast<uint8_t>((*addr >> 16) & 0xFF);
        uint16_t offset = static_cast<uint16_t>(*addr & 0x3FFF);

        uint8_t* pageAddr = memory->RAMPageAddress(page);
        if (!pageAddr)
        {
            return "E01";  // Invalid page
        }

        for (size_t i = 0; i < *len; i++)
        {
            uint16_t pageOffset = static_cast<uint16_t>((offset + i) & 0x3FFF);
            uint8_t byte = pageAddr[pageOffset];
            result += GDBPacket::toHex(byte, 2);
        }
    }
    else
    {
        // Standard Z80 address space
        for (size_t i = 0; i < *len; i++)
        {
            uint16_t address = static_cast<uint16_t>((*addr + i) & 0xFFFF);
            uint8_t byte = memory->DirectReadFromZ80Memory(address);
            result += GDBPacket::toHex(byte, 2);
        }
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

    // Refuse writes in TTD detached state
    if (_context->pTimeTravelManager)
    {
        auto state = _context->pTimeTravelManager->GetSessionInfo().state;
        if (state == ttd::TTDSessionState::Detached)
        {
            return "E0D";
        }
    }

    std::string data = params.substr(colonPos + 1);
    Memory* memory = _context->pMemory;

    // Decode hex data
    auto bytes = GDBPacket::hexToBytes(data);
    if (bytes.size() != *len)
    {
        return "E01";
    }

    // Check for physical memory access (0x01PPAAAA format)
    if ((*addr & 0xFF000000) == 0x01000000)
    {
        uint8_t page = static_cast<uint8_t>((*addr >> 16) & 0xFF);
        uint16_t offset = static_cast<uint16_t>(*addr & 0x3FFF);

        uint8_t* pageAddr = memory->RAMPageAddress(page);
        if (!pageAddr)
        {
            return "E01";  // Invalid page
        }

        for (size_t i = 0; i < bytes.size(); i++)
        {
            uint16_t pageOffset = static_cast<uint16_t>((offset + i) & 0x3FFF);
            pageAddr[pageOffset] = bytes[i];
        }
    }
    else
    {
        for (size_t i = 0; i < bytes.size(); i++)
        {
            uint16_t address = static_cast<uint16_t>((*addr + i) & 0xFFFF);
            memory->DirectWriteToZ80Memory(address, bytes[i]);
        }
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

    // Parse 'kind' (length for watchpoints) - default to 1
    uint64_t len = 1;
    if (secondComma != std::string::npos)
    {
        auto lenOpt = GDBPacket::parseHex(params.substr(secondComma + 1));
        if (lenOpt && *lenOpt > 0)
        {
            len = *lenOpt;
        }
    }

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

    // Limit range size to prevent excessive breakpoints
    constexpr uint64_t MAX_RANGE = 256;
    if (len > MAX_RANGE)
    {
        len = MAX_RANGE;
    }

    switch (*type)
    {
        case 0:  // SW breakpoint
        case 1:  // HW breakpoint (same for emulator)
            bpManager->AddExecutionBreakpoint(static_cast<uint16_t>(*addr & 0xFFFF), "gdb");
            break;

        case 2:  // Write watchpoint (supports ranges)
            for (uint64_t i = 0; i < len; i++)
            {
                uint16_t address = static_cast<uint16_t>((*addr + i) & 0xFFFF);
                bpManager->AddMemWriteBreakpoint(address, "gdb");
            }
            break;

        case 3:  // Read watchpoint (supports ranges)
            for (uint64_t i = 0; i < len; i++)
            {
                uint16_t address = static_cast<uint16_t>((*addr + i) & 0xFFFF);
                bpManager->AddMemReadBreakpoint(address, "gdb");
            }
            break;

        case 4:  // Access watchpoint (supports ranges)
            for (uint64_t i = 0; i < len; i++)
            {
                uint16_t address = static_cast<uint16_t>((*addr + i) & 0xFFFF);
                bpManager->AddMemReadBreakpoint(address, "gdb");
                bpManager->AddMemWriteBreakpoint(address, "gdb");
            }
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

    // Parse 'kind' (length for watchpoints) - default to 1
    uint64_t len = 1;
    if (secondComma != std::string::npos)
    {
        auto lenOpt = GDBPacket::parseHex(params.substr(secondComma + 1));
        if (lenOpt && *lenOpt > 0)
        {
            len = *lenOpt;
        }
    }

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

    // Limit range size to match set
    constexpr uint64_t MAX_RANGE = 256;
    if (len > MAX_RANGE)
    {
        len = MAX_RANGE;
    }

    // RemoveBreakpointByAddress removes all breakpoints at that address
    switch (*type)
    {
        case 0:
        case 1:
            bpManager->RemoveBreakpointByAddress(static_cast<uint16_t>(*addr & 0xFFFF));
            break;

        case 2:
        case 3:
        case 4:
            // Remove watchpoints for the entire range
            for (uint64_t i = 0; i < len; i++)
            {
                uint16_t address = static_cast<uint16_t>((*addr + i) & 0xFFFF);
                bpManager->RemoveBreakpointByAddress(address);
            }
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

    _waitingForStop = true;
    _externalStopPending = false;
    _emulator->Resume();

    // Wait for stop (breakpoint, external pause, or interrupt)
    while (_active && !_emulator->IsPaused() && !_externalStopPending)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    _waitingForStop = false;

    // Determine stop reason
    if (_externalStopPending)
    {
        _externalStopPending = false;
        _lastStopReason = StopReason::Interrupt;
    }
    else
    {
        _lastStopReason = StopReason::Breakpoint;
    }

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
    return "T02thread:01;";  // SIGINT
}

std::string GDBSession::handleBackwardStep()
{
    if (!_context || !_context->pTimeTravelManager)
    {
        return "E01";  // TTD not available
    }

    auto* ttd = _context->pTimeTravelManager;
    auto state = ttd->GetSessionInfo().state;

    if (state == ttd::TTDSessionState::Idle)
    {
        return "E01";  // No TTD session active
    }

    if (!_emulator->IsPaused())
    {
        _emulator->Pause();
    }

    // Stop recording to allow reverse execution (transitions to Detached)
    if (state == ttd::TTDSessionState::Recording)
    {
        ttd->StopRecording();
    }

    if (!ttd->StepBackInstruction())
    {
        // At beginning of history
        _lastStopReason = StopReason::Step;
        return "T05replaylog:begin;thread:01;";
    }

    _lastStopReason = StopReason::Step;
    return formatStopReply();
}

std::string GDBSession::handleBackwardContinue()
{
    if (!_context || !_context->pTimeTravelManager)
    {
        return "E01";  // TTD not available
    }

    auto* ttd = _context->pTimeTravelManager;
    auto state = ttd->GetSessionInfo().state;

    if (state == ttd::TTDSessionState::Idle)
    {
        return "E01";  // No TTD session active
    }

    if (!_emulator->IsPaused())
    {
        _emulator->Pause();
    }

    // Stop recording to allow reverse execution
    if (state == ttd::TTDSessionState::Recording)
    {
        ttd->StopRecording();
    }

    // Gather active execution breakpoints
    std::vector<uint16_t> breakpoints;
    auto* debugMgr = _context->pDebugManager;
    if (debugMgr)
    {
        auto* bpMgr = debugMgr->GetBreakpointsManager();
        if (bpMgr)
        {
            const auto& bpMap = bpMgr->GetAllBreakpoints();
            for (const auto& [id, bp] : bpMap)
            {
                if (bp && bp->active && (bp->memoryType & BRK_MEM_EXECUTE))
                {
                    breakpoints.push_back(bp->z80address);
                }
            }
        }
    }

    if (breakpoints.empty())
    {
        // No breakpoints to search for - step back one instruction instead
        if (!ttd->StepBackInstruction())
        {
            return "T05replaylog:begin;thread:01;";
        }
        _lastStopReason = StopReason::Step;
        return formatStopReply();
    }

    auto result = ttd->ReverseContinue(breakpoints);

    if (result.matched)
    {
        _lastStopReason = StopReason::Breakpoint;
        return "T05swbreak:;thread:01;";
    }
    else if (result.blockingMarker.reason[0] != '\0')
    {
        // Hit a barrier (tape, disk, etc)
        _lastStopReason = StopReason::Step;
        return "T05replaylog:begin;thread:01;";
    }
    else
    {
        // At beginning of history
        _lastStopReason = StopReason::Step;
        return "T05replaylog:begin;thread:01;";
    }
}

std::string GDBSession::formatStopReply()
{
    // Thread ID must be hex. Use "01" for thread 1.
    switch (_lastStopReason)
    {
        case StopReason::Breakpoint:
            return "T05swbreak:;thread:01;";

        case StopReason::Watchpoint:
            return "T05watch:" + GDBPacket::toHex(_lastWatchAddress, 4) + ";thread:01;";

        case StopReason::Interrupt:
            return "T02thread:01;";

        case StopReason::Step:
        case StopReason::Attached:
        case StopReason::None:
        default:
            return "T05thread:01;";
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
    _attachedEmulatorUuid = _emulator->GetId();

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

    // Subscribe to state change notifications (1A.7.3)
    subscribeToStateChanges();

    _lastStopReason = StopReason::Attached;
    return "T05thread:01;";
}

std::string GDBSession::handleDetach()
{
    // Unsubscribe from state changes (1A.7.3)
    unsubscribeFromStateChanges();

    // Release run-control claim (TDD §3.3)
    if (_context)
    {
        _context->ReleaseRunControl(_sessionUuid);
    }

    _emulator.reset();
    _context = nullptr;
    _attachedInstanceId.clear();
    _attachedEmulatorUuid.clear();
    _active = false;
    return "OK";
}

void GDBSession::subscribeToStateChanges()
{
    if (_subscribedToStateChanges)
        return;

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    mc.AddObserver(NC_EMULATOR_STATE_CHANGE, this,
        static_cast<ObserverCallbackMethod>(&GDBSession::onEmulatorStateChange));
    _subscribedToStateChanges = true;
}

void GDBSession::unsubscribeFromStateChanges()
{
    if (!_subscribedToStateChanges)
        return;

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    mc.RemoveObserver(NC_EMULATOR_STATE_CHANGE, this,
        static_cast<ObserverCallbackMethod>(&GDBSession::onEmulatorStateChange));
    _subscribedToStateChanges = false;
}

void GDBSession::onEmulatorStateChange(int /*id*/, Message* message)
{
    if (!message || !message->obj)
        return;

    auto* payload = dynamic_cast<EmulatorStateChangePayload*>(message->obj);
    if (!payload)
        return;

    // Filter by our attached emulator
    if (payload->emulatorId.toString() != _attachedEmulatorUuid)
        return;

    // Only interested in pause events while we're waiting for stop
    if (payload->_payloadNumber == StatePaused && _waitingForStop)
    {
        _externalStopPending = true;
        _lastStopReason = StopReason::Interrupt;
    }
}
