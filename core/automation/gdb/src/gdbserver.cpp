#include "gdbserver.h"
#include "gdbpacket.h"
#include "gdbtarget_z80.h"
#include "gdbmonitormemory.h"
#include "gdbagentexpr.h"

#include <common/uuid.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/emulatorcontext.h>
#include <emulator/config.h>
#include <emulator/machinenames.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <emulator/notifications.h>
#include <base/featuremanager.h>
#include <debugger/debugmanager.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/ttd/timetravelmanager.h>
#include <emulator/coordinator/machineop.h>
#include <debugger/ttd/ttdcontrol.h>
#include <debugger/ttd/ttdsession.h>
#include <debugger/ttd/ttdexternalevents.h>
#include <debugger/ttd/ttdprobe.h>
#include <3rdparty/message-center/messagecenter.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <map>
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
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{
/// Where the GDB client is ("192.168.1.5:51234"): the GUI shows it while the session holds run control
std::string PeerAddressOf(int socket)
{
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    if (getpeername(socket, reinterpret_cast<sockaddr*>(&address), &length) != 0)
        return {};
    char host[INET6_ADDRSTRLEN] = {};
    if (address.ss_family == AF_INET)
    {
        const auto* v4 = reinterpret_cast<const sockaddr_in*>(&address);
        inet_ntop(AF_INET, &v4->sin_addr, host, sizeof(host));
        return std::string(host) + ":" + std::to_string(ntohs(v4->sin_port));
    }
    if (address.ss_family == AF_INET6)
    {
        const auto* v6 = reinterpret_cast<const sockaddr_in6*>(&address);
        inet_ntop(AF_INET6, &v6->sin6_addr, host, sizeof(host));
        return "[" + std::string(host) + "]:" + std::to_string(ntohs(v6->sin6_port));
    }
    return "local socket";
}
}  // namespace

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

    // The accept loop polls with a timeout and re-checks the flags, so it ends within one poll quantum; the socket
    // is closed after the join. Closing it under a thread blocked in accept() does not wake it on Linux, and
    // deadlocks on Darwin (the DeZog and ZRCP servers, dzrpserver.cpp)
    if (_acceptThread && _acceptThread->joinable())
    {
        _acceptThread->join();
    }
    _acceptThread.reset();

    if (_serverSocket >= 0)
    {
#ifdef _WIN32
        closesocket(_serverSocket);
#else
        close(_serverSocket);
#endif
        _serverSocket = -1;
    }

    // End all sessions: the shutdown wakes each session's poll, its own thread (handleClient) ends it and closes the
    // socket, holding the session alive meanwhile
    {
        std::lock_guard<std::mutex> lock(_sessionsMutex);
        for (auto& [socket, session] : _sessions)
        {
            session->stop();
#ifdef _WIN32
            shutdown(socket, SD_BOTH);
#else
            shutdown(socket, SHUT_RDWR);
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

        // Wait at most 100 ms for a client, then re-check the flags: stop() joins this thread before it closes the
        // socket
#ifdef _WIN32
        WSAPOLLFD pfd{static_cast<SOCKET>(_serverSocket), POLLIN, 0};
        const int ready = WSAPoll(&pfd, 1, 100);
#else
        struct pollfd pfd{_serverSocket, POLLIN, 0};
        const int ready = poll(&pfd, 1, 100);
#endif
        if (ready < 0)
            break;   // listen socket error
        if (ready == 0)
            continue;

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
    coordinator::SetThreadSource(coordinator::OpSource::Gdb);   // the operation log names this session's operations
    auto session = std::make_shared<GDBSession>(clientSocket);
    session->setAutoAttach(_autoAttach);

    {
        std::lock_guard<std::mutex> lock(_sessionsMutex);
        _sessions[clientSocket] = session;
    }

    // Run session WITHOUT holding mutex (allows stop() to work)
    session->run();

    // Cleanup (stop() may have cleared the map already: then the entry under this socket is not this session's)
    {
        std::lock_guard<std::mutex> lock(_sessionsMutex);
        auto it = _sessions.find(clientSocket);
        if (it != _sessions.end() && it->second == session)
            _sessions.erase(it);
    }
    session.reset();

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
    , _sessionUuid(unreal::UUID::Generate())
{
}

GDBSession::~GDBSession()
{
    _active = false;
    endSession();   // a session whose run() never started (or already ended: then nothing is left)
}

void GDBSession::run()
{
    // Auto-attach if enabled and single instance exists: an attach as vAttach makes it (the machine stops, as gdb
    // expects of a target it connects to), without taking run control from the surfaces
    if (_autoAttach && !_emulator)
    {
        auto* mgr = EmulatorManager::GetInstance();
        if (mgr)
        {
            auto ids = mgr->GetEmulatorIds();
            if (ids.size() == 1)
            {
                if (std::shared_ptr<Emulator> emulator = mgr->GetEmulator(ids[0]))
                    attachTo(emulator, ids[0], /*claim=*/false);
            }
        }
    }

    while (_active)
    {
        if (_incoming.empty() && !pumpInput(_running ? 5 : 50))
            break;   // the socket closed

        if (_running)
        {
            // All-stop mode: while the target runs only an interrupt is expected; packets wait for the stop
            for (auto it = _incoming.begin(); it != _incoming.end();)
            {
                if (*it == "\x03")
                {
                    it = _incoming.erase(it);
                    _interruptRequested = true;
                    if (_emulator && !_emulator->IsPaused())
                        _emulator->Pause(execution::Requester{execution::Origin::Gdb, _sessionUuid.toString()}, "gdb");
                }
                else
                    ++it;
            }
            const std::string stop = pollRunningStop();
            if (!stop.empty())
                sendPacket(stop);
            else if (!_incoming.empty() && !pumpInput(5))
                break;   // packets wait: keep reading for an interrupt
            continue;
        }

        if (_incoming.empty())
            continue;
        const std::string packet = std::move(_incoming.front());
        _incoming.pop_front();

        const std::string response = handlePacket(packet);
        // An empty response is the empty packet $#00: "not supported" (RSP); a deferred one comes with the stop
        if (response != kDeferredReply)
            sendPacket(response);
    }

    endSession();
}

void GDBSession::stop()
{
    _active = false;   // run() sees it within one poll quantum and ends the session on its own thread
}

void GDBSession::endSession()
{
    if (_context)
    {
        forgetAllBreakpoints();
        _context->ReleaseRunControl(_sessionUuid);
    }
    _emulator.reset();
    _context = nullptr;
}

bool GDBSession::RefuseAfterTakeBack(const std::string& packet)
{
    if (!_context || !_context->IsRunControlRevoked(_sessionUuid))
        return false;
    // Reads, queries, breakpoints and detach stay; whatever runs, steps or writes the machine is refused
    const char c = packet[0];
    const bool controls = c == 'c' || c == 'C' || c == 's' || c == 'S' || c == 'b' || c == 'G' || c == 'P' ||
                          c == 'M' || c == 'X' || packet.starts_with("vCont;");
    if (!controls)
        return false;
    sendPacket(
        "O" + GDBPacket::hexEncode("revoked_by_user: the user took control back (at the GUI or from a companion); this "
                                   "session no longer controls the machine. Do not retry - detach, and ask "
                                   "the user before attaching again.\n"));
    return true;
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

bool GDBSession::pumpInput(int timeoutMs)
{
#ifdef _WIN32
    WSAPOLLFD pfd{static_cast<SOCKET>(_socket), POLLIN, 0};
    const int ready = WSAPoll(&pfd, 1, timeoutMs);
#else
    struct pollfd pfd{_socket, POLLIN, 0};
    const int ready = poll(&pfd, 1, timeoutMs);
#endif
    if (ready == 0)
        return true;
    if (ready < 0)
    {
#ifndef _WIN32
        if (errno == EINTR)
            return true;
#endif
        _active = false;
        return false;
    }

    char buf[4096];
#ifdef _WIN32
    const int n = recv(_socket, buf, static_cast<int>(sizeof(buf)), 0);
#else
    const auto n = recv(_socket, buf, sizeof(buf), 0);
#endif
    if (n <= 0)
    {
        _active = false;
        return false;
    }
    for (size_t i = 0; i < static_cast<size_t>(n); ++i)
        feedByte(buf[i]);
    return true;
}

void GDBSession::feedByte(char c)
{
    if (_reader->feed(c))
    {
        if (_reader->isChecksumValid())
        {
            if (!_noAckMode)
                sendRaw("+");
            _incoming.push_back(_reader->extractData());
        }
        else
        {
            if (!_noAckMode)
                sendRaw("-");
            _reader->reset();
        }
        return;
    }
    if (_reader->hasInterrupt())
    {
        _reader->clearInterrupt();
        _incoming.emplace_back("\x03");
        return;
    }
    if (_reader->hasError())
    {
        // A malformed checksum: ask for the packet again
        if (!_noAckMode)
            sendRaw("-");
        _reader->reset();
    }
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

    if (RefuseAfterTakeBack(packet))
        return "E5E";

    char cmd = packet[0];
    std::string args = packet.substr(1);

    switch (cmd)
    {
        case '?':
            return _lastStopReply;

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
            return handleWriteMemory(args, /*binary=*/false);

        case 'X':
            return handleWriteMemory(args, /*binary=*/true);

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
            return "";  // Unknown 'b' command: not supported

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

    // Symbol lookup
    if (packet.starts_with("qSymbol"))
    {
        return "OK";  // No symbol lookup needed
    }

    // Everything else (trace packets, qOffsets, the old qL / qP thread queries, unknown queries): the empty packet,
    // "not supported"
    return "";
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
    // Z0 / Z1 carry the condition as agent expressions: the core checks it without stopping (gdbagentexpr.h)
    caps += "ConditionalBreakpoints+;";

    // Advertise reverse debugging support when TTD is available
    if (_context && _context->pTimeTravelHooks)
    {
        caps += "ReverseStep+;";
        caps += "ReverseContinue+;";
    }

    return caps;
}

namespace
{
/// One chunk of a qXfer object for "offset,length" (hex): 'm' + the bytes when more follow, 'l' + the bytes for the
/// last chunk, "l" alone at or past the end; binary-escaped. E01 for a malformed range
std::string XferChunk(const std::string& document, const std::string& range)
{
    const size_t comma = range.find(',');
    if (comma == std::string::npos)
        return "E01";
    const auto offset = GDBPacket::parseHex(range.substr(0, comma));
    const auto length = GDBPacket::parseHex(range.substr(comma + 1));
    if (!offset || !length)
        return "E01";
    if (*offset >= document.size())
        return "l";
    const size_t count = static_cast<size_t>(std::min<uint64_t>(*length, document.size() - *offset));
    const bool last = *offset + count >= document.size();
    const std::vector<uint8_t> bytes(document.begin() + static_cast<std::ptrdiff_t>(*offset),
                                     document.begin() + static_cast<std::ptrdiff_t>(*offset + count));
    return (last ? "l" : "m") + GDBPacket::escapeBinary(bytes);
}
}  // namespace

std::string GDBSession::handleQXfer(const std::string& params)
{
    // :<object>:read:<annex>:<offset>,<length>
    auto readOf = [&params](const std::string& prefix, std::string& range) {
        if (!params.starts_with(prefix))
            return false;
        range = params.substr(prefix.size());
        return true;
    };
    std::string range;

    if (readOf(":features:read:target.xml:", range))
    {
        return XferChunk(GDBTargetZ80::generateFlatTargetXML(_context), range);
    }

    if (readOf(":osdata:read:processes:", range))
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
        return XferChunk(xml.str(), range);
    }

    // Provide minimal threads XML to help GDB
    if (readOf(":threads:read::", range))
    {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\"?>\n";
        xml << "<threads>\n";
        xml << "  <thread id=\"01\" core=\"0\" name=\"Z80\"/>\n";
        xml << "</threads>\n";
        return XferChunk(xml.str(), range);
    }

    // Other objects (exec-file, auxv, libraries, memory-map, spu, siginfo, ...) and other annexes: not supported
    return "";
}

std::string GDBSession::handleVCommand(const std::string& packet)
{
    if (packet == "vMustReplyEmpty")
    {
        // GDB spec: must reply with empty packet $#00, not silence
        return "";
    }

    if (packet.starts_with("vCont?"))
    {
        return "vCont;c;C;s;S";
    }

    if (packet.starts_with("vCont;") && packet.size() > 6)
    {
        // One thread: the first action decides (vCont;c, vCont;C05, vCont;s:1, vCont;S05:01); the signal is ignored
        switch (packet[6])
        {
            case 'c':
            case 'C':
                return handleContinue();
            case 's':
            case 'S':
                return handleStep();
            default:
                return "";   // t, r, ...: not advertised, not supported
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
        response += "  monitor regions   - list the device memories (video RAM, sound card RAM, ...)\n";
        response += "  monitor mem <space:offset> [len] - dump any memory (cpu, ram5, rom2, cache0, a region)\n";
        response += "  monitor load <path> - load snap/tape/disk (paused only)\n";
        response += "  monitor ttd status  - show TTD session info\n";
        response += "  monitor ttd start   - start TTD recording\n";
        response += "  monitor ttd stop    - stop TTD recording\n";
        response += "  monitor ttd seek <frame> - seek to frame\n";
        response += "  monitor ttd findlast <w|r|x> <addr|vram:offset|cache:offset> - find last access\n";
        response += "  monitor bport <in|out> <port> - set port breakpoint\n";
        response += "  monitor bport clear <id> - remove port breakpoint (the decimal id 'set' printed)\n";
    }
    else if (cmd == "model")
    {
        if (_emulator && _context)
        {
            // The name the machine was created as (a variant's or the model's short name) and the model's full name
            const std::string name = machines::NameOf(_context->config);
            const TMemModel* model = Config::FindModelByEnum(_context->config.mem_model);
            std::ostringstream ss;
            ss << "Model: " << (model && model->FullName ? model->FullName : "unknown");
            if (!name.empty())
                ss << " (" << name << ")";
            ss << ", RAM " << _context->config.ramsize << " KB\n";
            response = ss.str();
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
    else if (cmd == "regions")
    {
        response = _context ? GdbMonitorMemory::Regions(_context) : "No emulator attached\n";
    }
    else if (cmd.starts_with("mem ") || cmd == "mem")
    {
        response = _context ? GdbMonitorMemory::Dump(_context, cmd.size() > 4 ? cmd.substr(4) : std::string())
                            : "No emulator attached\n";
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
                std::string refusal;  // B9: a TTD recording refuses a medium load; report why
                if (ext == "sna" || ext == "snx" || ext == "z80" || ext == "szx" || ext == "spg" || ext == "rzx")
                {
                    ok = _emulator->LoadSnapshot(path);   // ends a TTD recording session, never refused by it
                }
                else if (Emulator::IsTapeExtension(ext))
                {
                    refusal = _emulator->AskMediaChange(Emulator::MediaChange::LoadTape, 0, path);
                    ok = refusal.empty() && _emulator->LoadTape(path);
                }
                else if (ext == "trd" || ext == "scl" || ext == "fdi")
                {
                    refusal = _emulator->AskMediaChange(Emulator::MediaChange::LoadDisk, 0, path);
                    ok = refusal.empty() && _emulator->LoadDisk(path);
                }
                else
                {
                    response = "Error: unsupported file type '" + ext + "'\n";
                }

                if (!refusal.empty())
                {
                    response = "Error: " + refusal + "\n";
                }
                else if (ok)
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
        if (!_context || !_context->pTimeTravelHooks)
        {
            response = "TTD not available\n";
        }
        else
        {
            const ttd::TTDSessionView view(_context);
            const ttd::TTDSessionView* ttd = &view;

            if (subcmd == "status")
            {
                auto info = ttd->ReadSessionInfo();
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
                    const ttd::TTDReply started = ttd::TTDControl(_context).Execute({"start", {}});
                    if (started.Ok() && (started.body.find("started")->b || started.body.find("already_active")->b))
                    {
                        response = "TTD recording started\n";
                    }
                    else
                    {
                        response = "Error: TTD recording failed to start" +
                                   (started.message.empty() ? "" : ": " + started.message) + "\n";
                    }
                }
            }
            else if (subcmd == "stop")
            {
                // In the recorded past the machine first comes back to live where it stands (the history after it is
                // cut): a stopped recording leaves no history to browse, and the machine must be writable again
                std::string note;
                if (inHistory())
                {
                    const ttd::TTDReply resumed = ttd::TTDControl(_context).Execute({"resume", {}});
                    const StateNode* ok = resumed.Ok() ? resumed.body.find("resumed") : nullptr;
                    note = ok && ok->b ? "live again at the position in the history (the history after it is cut)\n"
                                       : "Warning: could not leave the recorded past" +
                                             (resumed.message.empty() ? std::string() : ": " + resumed.message) +
                                             "\n";
                }
                (void)ttd::TTDControl(_context).Execute({"stop", {}});
                response = "TTD recording stopped\n" + note;
            }
            else if (subcmd.starts_with("seek "))
            {
                std::string frameStr = subcmd.substr(5);
                try
                {
                    const uint64_t frame = std::stoull(frameStr);
                    const ttd::TTDReply sought =
                        ttd::TTDControl(_context).Execute({"seek", {{"frame", std::to_string(frame)}}});
                    if (sought.Ok() && sought.body.find("reached")->b)
                    {
                        std::ostringstream ss;
                        ss << "Seeked to frame " << sought.body.find("arrived_at")->find("frame")->i << "\n";
                        response = ss.str();
                    }
                    else
                    {
                        response = "Error: seek failed" + (sought.message.empty() ? "" : ": " + sought.message) + "\n";
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
                    const char accessType = static_cast<char>(std::tolower(static_cast<unsigned char>(args[0])));
                    const char* access = accessType == 'w' ? "write" : accessType == 'r' ? "read"
                                                             : accessType == 'x' ? "execute" : nullptr;
                    if (!access)
                    {
                        response = "Error: access type must be w, r, or x\n";
                        goto done_ttd;
                    }

                    // The address is hex without a prefix here (GDB's convention); "vram:4805" / "cache:123" name a byte
                    // of the Sprinter's video / fast RAM
                    std::string space;
                    uint32_t address = 0;
                    if (!GdbMonitorMemory::ParseLocation(args.substr(spacePos + 1), space, address))
                    {
                        response = "Error: invalid address\n";
                        goto done_ttd;
                    }
                    std::map<std::string, std::string> options{{"addr", std::to_string(address)}, {"access", access}};
                    if (space != "cpu")
                        options["space"] = space;
                    const ttd::TTDReply found = ttd::TTDControl(_context).Execute({"find-last", options});
                    const StateNode& b = found.body;
                    if (!found.Ok())
                    {
                        response = "Error: " + found.message + "\n";
                    }
                    else if (b.find("found")->b)
                    {
                        std::ostringstream ss;
                        ss << "Found at frame " << b.find("frame")->i << ", t=" << b.find("tinframe")->i;
                        if (accessType == 'w')
                            ss << ", value=0x" << std::hex << b.find("value")->i;
                        ss << ", pc=0x" << std::hex << b.find("pc")->i;
                        if (const StateNode* offset = b.find("offset"))
                            ss << ", " << b.find("space")->s << " offset 0x" << offset->i;
                        ss << "\n";
                        response = ss.str();
                    }
                    else if (const StateNode* reason = b.find("marker_reason"))
                    {
                        response = "Blocked by barrier: " + reason->s + "\n";
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
            // The id as 'set' prints it: decimal, as every other surface shows breakpoint ids
            const std::string text = args.substr(6);
            uint64_t id = 0;
            bool valid = !text.empty() && text.size() <= 5;
            for (char c : text)
            {
                valid = valid && std::isdigit(static_cast<unsigned char>(c));
                id = id * 10 + static_cast<uint64_t>(c - '0');
            }
            if (!valid || id > 0xFFFF)
            {
                response = "Error: invalid breakpoint ID\n";
            }
            else
            {
                // Only a port breakpoint gdb set: never another surface's
                const BreakpointSnapshot bp = bpMgr->GetBreakpointById(static_cast<uint16_t>(id));
                if (!bp || bp->type != BRK_IO || bp->owner != "gdb")
                    response = "Error: breakpoint not found (not a gdb port breakpoint)\n";
                else if (bpMgr->RemoveBreakpointByID(static_cast<uint16_t>(id)))
                    response = "Port breakpoint removed\n";
                else
                    response = "Error: breakpoint not found\n";
            }
        }
        else if (args.starts_with("in ") || args.starts_with("out "))
        {
            bool isIn = args.starts_with("in ");
            std::string portStr = args.substr(isIn ? 3 : 4);
            auto portOpt = GDBPacket::parseHex(portStr);

            if (portOpt && *portOpt <= 0xFFFF)
            {
                const uint16_t port = static_cast<uint16_t>(*portOpt);
                // A breakpoint of gdb's own, never merged with an equal one of another surface (its clear would
                // remove that one)
                BreakpointSpec spec;
                spec.type = BRK_IO;
                spec.access = isIn ? BRK_IO_IN : BRK_IO_OUT;
                spec.address = port;
                spec.owner = "gdb";
                spec.shared = false;
                std::string error;
                const uint16_t id = bpMgr->AddBreakpoint(spec, error);
                if (id != BRK_INVALID)
                {
                    ensureDebugEnabled();
                    std::ostringstream ss;
                    ss << "Port breakpoint set: ID " << id << " on port 0x"
                       << std::hex << port << " (" << (isIn ? "IN" : "OUT") << ")\n";
                    response = ss.str();
                }
                else
                {
                    response = "Error: failed to add breakpoint" + (error.empty() ? "" : ": " + error) + "\n";
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
    if (_context->pTimeTravelHooks)
    {
        auto state = _context->pTimeTravelHooks->ReadSessionInfo().state;
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
    if (_context->pTimeTravelHooks)
    {
        auto state = _context->pTimeTravelHooks->ReadSessionInfo().state;
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

    // At most what fits a reply (PacketSize=4000 hex: two digits a byte); gdb reads the rest with the next m
    const size_t length = static_cast<size_t>(std::min<uint64_t>(*len, (0x4000 - 4) / 2));

    Memory* memory = _context->pMemory;
    std::string result;
    result.reserve(length * 2);

    // Check for physical memory access (0x01PPAAAA format)
    // High byte 0x01 = RAM page access, PP = page number, AAAA = 16KB offset
    if ((*addr & 0xFF000000) == 0x01000000)
    {
        uint8_t page = static_cast<uint8_t>((*addr >> 16) & 0xFF);
        uint16_t offset = static_cast<uint16_t>(*addr & 0x3FFF);

        uint8_t* pageAddr = hasRamPage(*addr) ? memory->RAMPageAddress(page) : nullptr;
        if (!pageAddr)
        {
            return "E03";  // a page the machine does not have
        }

        for (size_t i = 0; i < length; i++)
        {
            uint16_t pageOffset = static_cast<uint16_t>((offset + i) & 0x3FFF);
            uint8_t byte = pageAddr[pageOffset];
            result += GDBPacket::toHex(byte, 2);
        }
    }
    else
    {
        // Standard Z80 address space
        for (size_t i = 0; i < length; i++)
        {
            uint16_t address = static_cast<uint16_t>((*addr + i) & 0xFFFF);
            uint8_t byte = memory->DirectReadFromZ80Memory(address);
            result += GDBPacket::toHex(byte, 2);
        }
    }

    return result;
}

bool GDBSession::hasRamPage(uint64_t address) const
{
    const uint32_t ramKb = _context ? _context->config.ramsize : 0;
    const uint32_t pages = ramKb ? ramKb / 16 : MAX_RAM_PAGES;
    return ((address >> 16) & 0xFF) < pages;
}

std::string GDBSession::handleWriteMemory(const std::string& params, bool binary)
{
    // M addr,length:hex or X addr,length:binary (escaped)
    auto commaPos = params.find(',');
    auto colonPos = params.find(':');

    if (commaPos == std::string::npos || colonPos == std::string::npos || colonPos < commaPos)
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
    if (inHistory())
    {
        return "E0D";
    }

    const std::string data = params.substr(colonPos + 1);
    std::vector<uint8_t> bytes;
    if (binary)
    {
        bytes = GDBPacket::unescapeBinary(data);
    }
    else
    {
        if (data.size() != *len * 2)
            return "E01";
        bytes = GDBPacket::hexToBytes(data);
    }
    if (bytes.size() != *len)
    {
        return "E01";
    }
    if (bytes.empty())
    {
        return "OK";   // gdb's "X addr,0:" probe
    }
    if ((*addr & 0xFF000000) == 0x01000000 && !hasRamPage(*addr))
    {
        return "E03";
    }

    return GDBTargetZ80::writeMemory(_context, *addr, bytes) ? "OK" : "E01";
}

std::string GDBSession::handleSetBreakpoint(const std::string& packet)
{
    // Z type,addr,kind[;X len,bytes]...: the conditions (ConditionalBreakpoints+) after the first ';'
    const size_t semicolon = packet.find(';');
    const std::string params = packet.substr(0, semicolon);
    const std::string conditions = semicolon != std::string::npos ? packet.substr(semicolon + 1) : "";

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

    switch (*type)
    {
        case 0:  // SW breakpoint
        case 1:  // HW breakpoint (the same for an emulator; its stop says hwbreak)
        {
            const uint16_t address = static_cast<uint16_t>(*addr & 0xFFFF);
            // gdb sends Z0 again when the conditions change: the new breakpoint replaces this session's one there
            forgetExecutionBreakpoint(*bpManager, address);

            BreakpointSpec spec;
            spec.address = address;
            spec.owner = "gdb";
            spec.shared = false;
            std::string reason;
            // gdb checks the condition again on every stop: what does not translate is set without one
            if (!conditions.empty() && !GDBAgentExpression::listToCondition(conditions, spec.condition, reason))
                std::cerr << "GDBServer: the condition at " << address << " is checked by gdb only (" << reason << ")\n";
            std::string error;
            uint16_t id = bpManager->AddBreakpoint(spec, error);
            if (id == BRK_INVALID && !spec.condition.empty())
            {
                std::cerr << "GDBServer: the core refused condition '" << spec.condition << "': " << error << "\n";
                spec.condition.clear();
                id = bpManager->AddBreakpoint(spec, error);
            }
            if (id == BRK_INVALID)
                return "E01";
            _executionBreakpoints[address] = OwnBreakpoint{id, *type == 1};
            break;
        }

        case 2:  // Write watchpoint
        case 3:  // Read watchpoint
        case 4:  // Access watchpoint
        {
            // One core breakpoint over the whole range, gdb's own (unshared: never merged with another surface's
            // breakpoint at the address, whose id would come back and whose access would decide)
            if (*addr > 0xFFFF || *addr + len > 0x10000)
                return "E22";   // past the 64K view
            const uint16_t address = static_cast<uint16_t>(*addr);
            const auto key = std::make_tuple(static_cast<uint8_t>(*type), address, static_cast<uint32_t>(len));
            if (auto it = _watchpoints.find(key); it != _watchpoints.end())
            {
                bpManager->RemoveBreakpointByID(it->second);   // set again: replaced
                _watchpoints.erase(it);
            }
            BreakpointSpec spec;
            spec.type = BRK_MEMORY;
            spec.access = *type == 2 ? BRK_MEM_WRITE : *type == 3 ? BRK_MEM_READ : (BRK_MEM_READ | BRK_MEM_WRITE);
            spec.address = address;
            if (len > 1)
            {
                spec.hasEnd = true;
                spec.addressEnd = static_cast<uint16_t>(address + len - 1);
            }
            spec.owner = "gdb";
            spec.shared = false;
            std::string error;
            const uint16_t id = bpManager->AddBreakpoint(spec, error);
            if (id == BRK_INVALID)
            {
                std::cerr << "GDBServer: watchpoint at " << address << " refused: " << error << "\n";
                return "E01";
            }
            _watchpoints[key] = id;
            break;
        }

        default:
            return "";  // Unsupported
    }

    ensureDebugEnabled();
    return "OK";
}

bool GDBSession::forgetExecutionBreakpoint(BreakpointManager& breakpoints, uint16_t address)
{
    const auto it = _executionBreakpoints.find(address);
    if (it == _executionBreakpoints.end())
        return false;
    // The breakpoint is gdb's own (unshared): never another surface's
    breakpoints.RemoveBreakpointByID(it->second.id);
    _executionBreakpoints.erase(it);
    return true;
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

    // Only gdb's own: a breakpoint another surface set at the address stays
    switch (*type)
    {
        case 0:
        case 1:
            forgetExecutionBreakpoint(*bpManager, static_cast<uint16_t>(*addr & 0xFFFF));
            break;

        case 2:
        case 3:
        case 4:
        {
            const auto key =
                std::make_tuple(static_cast<uint8_t>(*type), static_cast<uint16_t>(*addr & 0xFFFF), static_cast<uint32_t>(len));
            if (auto it = _watchpoints.find(key); it != _watchpoints.end())
            {
                bpManager->RemoveBreakpointByID(it->second);
                _watchpoints.erase(it);
            }
            break;
        }

        default:
            return "";
    }

    return "OK";
}

void GDBSession::forgetAllBreakpoints()
{
    DebugManager* debugManager = _context ? _context->pDebugManager : nullptr;
    BreakpointManager* bpManager = debugManager ? debugManager->GetBreakpointsManager() : nullptr;
    if (bpManager)
    {
        BreakpointManager::Batch batch(*bpManager);
        for (const auto& [address, own] : _executionBreakpoints)
            bpManager->RemoveBreakpointByID(own.id);
        for (const auto& [key, id] : _watchpoints)
            bpManager->RemoveBreakpointByID(id);
    }
    _executionBreakpoints.clear();
    _watchpoints.clear();
}

void GDBSession::ensureDebugEnabled()
{
    if (!_emulator)
        return;
    if (!_emulator->IsDebug())
        _emulator->DebugOn();
    if (_context && _context->pFeatureManager)
    {
        _context->pFeatureManager->setFeature(Features::kDebugMode, true);
        _context->pFeatureManager->setFeature(Features::kBreakpoints, true);
    }
}

std::string GDBSession::handleContinue()
{
    if (!_emulator)
    {
        return "E31";
    }

    // Already running (another surface resumed it): the stop reply comes when it stops
    if (_emulator->IsPaused())
    {
        // In the recorded past: the run goes on live from here (the history after it is cut)
        if (inHistory() && !leaveHistory())
            return "E0D";

        const execution::Result resumed =
            _emulator->Resume(execution::Requester{execution::Origin::Gdb, _sessionUuid.toString()});
        if (resumed.outcome != execution::Outcome::Applied)
        {
            // Refused (another client holds run control, the machine does not run) or not served in time: nothing
            // ran, so no stop reply - an error, with the reason on the client's console
            sendPacket("O" + GDBPacket::hexEncode("run_refused: the machine was not resumed (" +
                                                  std::string(execution::OutcomeName(resumed.outcome)) +
                                                  (resumed.detail.empty() ? "" : ": " + resumed.detail) + ")\n"));
            return "E0D";
        }
    }

    _running = true;
    _interruptRequested = false;
    return kDeferredReply;
}

std::string GDBSession::pollRunningStop()
{
    if (!_emulator)
    {
        _running = false;
        return "W00";
    }
    const EmulatorStateEnum state = _emulator->GetState();
    if (state == StateStopped || state == StateDestroying || state == StateRetired)
    {
        _running = false;   // the instance is gone
        _lastStopReply = "W00";
        return _lastStopReply;
    }
    if (_context && _context->IsRunControlRevoked(_sessionUuid))
    {
        // The user took control back: the run is no longer gdb's
        _running = false;
        _resumeOnDetach = false;
        sendPacket("O" + GDBPacket::hexEncode("revoked_by_user: the user took control back; this session no longer "
                                              "controls the machine.\n"));
        _lastStopReply = "T02thread:01;";
        return _lastStopReply;
    }
    if (!_emulator->IsPaused())
        return {};

    _running = false;
    _lastStopReply = classifyStop();
    _interruptRequested = false;
    return _lastStopReply;
}

std::string GDBSession::classifyStop()
{
    const Emulator::DebugStop stop = _emulator->LastStop();
    if (stop.reason == Emulator::DebugStop::Reason::Breakpoint && stop.breakpoint.hit)
    {
        const uint16_t id = stop.breakpoint.breakpointId;
        for (const auto& [address, own] : _executionBreakpoints)
        {
            if (own.id == id)
            {
                _resumeOnDetach = true;
                return own.hardware ? "T05hwbreak:;thread:01;" : "T05swbreak:;thread:01;";
            }
        }
        for (const auto& [key, ownId] : _watchpoints)
        {
            if (ownId != id)
                continue;
            _resumeOnDetach = true;
            const uint8_t type = std::get<0>(key);
            const char* name = type == 2 ? "watch" : type == 3 ? "rwatch" : "awatch";
            return std::string("T05") + name + ":" + GDBPacket::toHex(stop.breakpoint.address, 4) + ";thread:01;";
        }
        // Another surface's breakpoint: a stop, not one of gdb's breakpoints
        _resumeOnDetach = false;
        return "T05thread:01;";
    }
    if (_interruptRequested)
    {
        _resumeOnDetach = true;
        return "T02thread:01;";
    }
    // Paused by somebody else (the GUI, the WebAPI): a plain stop (gdb-reverse-debugging TDD §4.7.1)
    _resumeOnDetach = false;
    return "T05thread:01;";
}

std::string GDBSession::handleStep()
{
    if (!_emulator)
    {
        return "E31";
    }

    // In the recorded past a step replays the next instruction; at the end of the history it goes on live
    if (inHistory())
    {
        const ttd::TTDReply stepped = ttd::TTDControl(_context).Execute({"step-instruction", {{"dir", "forward"}}});
        const StateNode* ok = stepped.Ok() ? stepped.body.find("stepped") : nullptr;
        if (ok && ok->b)
        {
            _lastStopReply = "T05thread:01;";
            return _lastStopReply;
        }
        if (!leaveHistory())
            return "E0D";
    }

    if (!_emulator->IsPaused())
    {
        _emulator->Pause(execution::Requester{execution::Origin::Gdb, _sessionUuid.toString()}, "gdb");
        _resumeOnDetach = true;
    }

    // Execute single instruction
    _emulator->RunSingleCPUCycle(true);

    _lastStopReply = "T05thread:01;";
    return _lastStopReply;
}

std::string GDBSession::handleInterrupt()
{
    if (_emulator && !_emulator->IsPaused())
    {
        _emulator->Pause(execution::Requester{execution::Origin::Gdb, _sessionUuid.toString()}, "gdb");
        _resumeOnDetach = true;
    }

    _lastStopReply = "T02thread:01;";  // SIGINT
    return _lastStopReply;
}

bool GDBSession::inHistory() const
{
    return _context && _context->pTimeTravelHooks &&
           _context->pTimeTravelHooks->ReadSessionInfo().state == ttd::TTDSessionState::Detached;
}

bool GDBSession::leaveHistory()
{
    // The TTD resume verb: the recording goes on from the machine's position, the history after it is dropped
    const ttd::TTDReply resumed = ttd::TTDControl(_context).Execute({"resume", {}});
    const StateNode* ok = resumed.Ok() ? resumed.body.find("resumed") : nullptr;
    if (!ok || !ok->b)
    {
        sendPacket("O" + GDBPacket::hexEncode("ttd: the machine can not go on from the recorded past" +
                                              (resumed.message.empty() ? std::string() : ": " + resumed.message) +
                                              " (monitor ttd status)\n"));
        return false;
    }
    if (!_cutWarned)
    {
        _cutWarned = true;
        sendPacket("O" + GDBPacket::hexEncode("ttd: running on from the recorded past - the history after this point "
                                              "is discarded, the recording goes on from here\n"));
    }
    return true;
}

namespace
{
/// The engine's controller browses a recording by pausing it (D8); TTD v1 refuses to browse while it records, so
/// the recording stops first (it keeps the history)
void PrepareReverse(EmulatorContext* context, ttd::TTDControl& control)
{
    if (!context->pTimeTravelController &&
        context->pTimeTravelHooks->ReadSessionInfo().state == ttd::TTDSessionState::Recording)
        (void)control.Execute({"stop", {}});
}
}  // namespace

std::string GDBSession::handleBackwardStep()
{
    if (!_context || !_context->pTimeTravelHooks)
    {
        return "E01";  // TTD not available
    }

    auto* ttd = _context->pTimeTravelHooks;
    auto state = ttd->ReadSessionInfo().state;

    if (state == ttd::TTDSessionState::Idle)
    {
        return "E01";  // No TTD session active
    }

    ttd::TTDControl control(_context);
    PrepareReverse(_context, control);

    const ttd::TTDReply stepped = control.Execute({"step-instruction", {{"dir", "back"}}});
    const StateNode* ok = stepped.Ok() ? stepped.body.find("stepped") : nullptr;
    _lastStopReply = ok && ok->b ? "T05thread:01;" : "T05replaylog:begin;thread:01;";   // else: the start of history
    return _lastStopReply;
}

std::string GDBSession::handleBackwardContinue()
{
    if (!_context || !_context->pTimeTravelHooks)
    {
        return "E01";  // TTD not available
    }

    auto* ttd = _context->pTimeTravelHooks;
    auto state = ttd->ReadSessionInfo().state;

    if (state == ttd::TTDSessionState::Idle)
    {
        return "E01";  // No TTD session active
    }

    ttd::TTDControl control(_context);
    PrepareReverse(_context, control);

    // gdb's own execution breakpoints are the targets
    std::string pcs;
    for (const auto& [address, own] : _executionBreakpoints)
        pcs += (pcs.empty() ? "" : ",") + std::to_string(address);

    if (pcs.empty())
    {
        // No breakpoints to search for - step back one instruction instead
        const ttd::TTDReply stepped = control.Execute({"step-instruction", {{"dir", "back"}}});
        const StateNode* ok = stepped.Ok() ? stepped.body.find("stepped") : nullptr;
        _lastStopReply = ok && ok->b ? "T05thread:01;" : "T05replaylog:begin;thread:01;";
        return _lastStopReply;
    }

    const ttd::TTDReply result = control.Execute({"reverse-continue", {{"pcs", pcs}}});
    const StateNode* matched = result.Ok() ? result.body.find("matched") : nullptr;
    if (matched && matched->b)
    {
        const StateNode* pc = result.body.find("pc");
        const auto it = pc ? _executionBreakpoints.find(static_cast<uint16_t>(pc->i)) : _executionBreakpoints.end();
        _lastStopReply = it != _executionBreakpoints.end() && it->second.hardware ? "T05hwbreak:;thread:01;"
                                                                                  : "T05swbreak:;thread:01;";
        return _lastStopReply;
    }
    // The start of history, or a barrier (tape, disk, ...: blocked_by_marker)
    _lastStopReply = "T05replaylog:begin;thread:01;";
    return _lastStopReply;
}

void GDBSession::attachTo(const std::shared_ptr<Emulator>& emulator, const std::string& instanceId, bool claim)
{
    // A previous attach: its breakpoints and run control go first (the ids were that machine's)
    if (_context)
    {
        forgetAllBreakpoints();
        _context->ReleaseRunControl(_sessionUuid);
    }

    _emulator = emulator;
    _context = _emulator->GetContext();
    _attachedInstanceId = instanceId;

    // A target gdb attaches to is stopped; a detach lets it run again
    _resumeOnDetach = false;
    if (!_emulator->IsPaused())
    {
        _emulator->Pause(execution::Requester{execution::Origin::Gdb, _sessionUuid.toString()}, "gdb");
        _resumeOnDetach = _emulator->IsPaused();
    }

    // Take run-control claim (TDD §3.3)
    if (claim && _context)
    {
        std::string errorReason;
        if (!_context->TakeRunControl(_sessionUuid, "gdb", &errorReason, "GDB", std::chrono::milliseconds(0),
                                      PeerAddressOf(_socket)))
        {
            // Another surface holds the claim - report but don't fail attach
            // (read-only operations still allowed)
        }
    }

    _lastStopReply = "T05thread:01;";
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

    if (*pidNum == 0 || index >= ids.size())
    {
        return "E31";
    }

    std::shared_ptr<Emulator> emulator = mgr->GetEmulator(ids[index]);
    if (!emulator)
    {
        return "E31";
    }

    attachTo(emulator, ids[index], /*claim=*/true);
    return _lastStopReply;
}

std::string GDBSession::handleDetach()
{
    // gdb's breakpoints go; a machine gdb stopped runs again (as a detached process does)
    if (_context)
        forgetAllBreakpoints();
    if (_emulator && _resumeOnDetach && _emulator->IsPaused() && !inHistory())
        (void)_emulator->Resume(execution::Requester{execution::Origin::Gdb, _sessionUuid.toString()});

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
