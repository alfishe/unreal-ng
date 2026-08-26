#include "gdbserver.h"
#include "gdbpacket.h"

#include <iostream>

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
{
}

GDBSession::~GDBSession()
{
    stop();
}

void GDBSession::run()
{
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
    // caps += "ReverseStep+;ReverseContinue+;";

    return caps;
}

std::string GDBSession::handleQXfer(const std::string& params)
{
    // Parse qXfer:object:operation:annex:offset,length
    // TODO: Implement target.xml serving
    if (params.starts_with(":features:read:target.xml:"))
    {
        // TODO: Return actual target XML
        return "l" + std::string(R"(<?xml version="1.0"?>
<!DOCTYPE target SYSTEM "gdb-target.dtd">
<target version="1.0">
  <architecture>z80</architecture>
  <feature name="org.gnu.gdb.z80.cpu">
    <reg name="af"  bitsize="16" regnum="0"/>
    <reg name="bc"  bitsize="16"/>
    <reg name="de"  bitsize="16"/>
    <reg name="hl"  bitsize="16"/>
    <reg name="sp"  bitsize="16" type="data_ptr"/>
    <reg name="pc"  bitsize="16" type="code_ptr"/>
    <reg name="ix"  bitsize="16" type="data_ptr"/>
    <reg name="iy"  bitsize="16" type="data_ptr"/>
    <reg name="af'" bitsize="16"/>
    <reg name="bc'" bitsize="16"/>
    <reg name="de'" bitsize="16"/>
    <reg name="hl'" bitsize="16"/>
    <reg name="ir"  bitsize="16"/>
  </feature>
</target>
)");
    }

    if (params.starts_with(":osdata:read:processes:"))
    {
        // TODO: Return actual instance list
        return "l<osdata type=\"processes\"></osdata>";
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
        // Parse vCont actions - for single thread, degenerates to c/s
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
    // TODO: Implement monitor commands
    std::string response = "Monitor command not implemented: " + cmd + "\n";
    return GDBPacket::hexEncode(response);
}

std::string GDBSession::handleReadRegisters()
{
    // TODO: Read actual Z80 registers
    // Return placeholder for now (14 registers * 2 bytes * 2 hex chars = 56 chars)
    return "0000000000000000000000000000000000000000000000000000";
}

std::string GDBSession::handleWriteRegisters(const std::string& /*data*/)
{
    // TODO: Write Z80 registers
    return "OK";
}

std::string GDBSession::handleReadRegister(const std::string& params)
{
    auto regnum = GDBPacket::parseHex(params);
    if (!regnum)
    {
        return "E02";
    }

    // TODO: Read specific register
    return "0000";
}

std::string GDBSession::handleWriteRegister(const std::string& params)
{
    auto eqPos = params.find('=');
    if (eqPos == std::string::npos)
    {
        return "E01";
    }

    // TODO: Write specific register
    return "OK";
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

    // TODO: Read actual memory
    std::string result;
    for (size_t i = 0; i < *len; i++)
    {
        result += "00";
    }

    return result;
}

std::string GDBSession::handleWriteMemory(const std::string& /*params*/)
{
    // TODO: Write memory
    return "OK";
}

std::string GDBSession::handleSetBreakpoint(const std::string& params)
{
    // Z type,addr,kind
    // TODO: Set breakpoint
    (void)params;
    return "OK";
}

std::string GDBSession::handleRemoveBreakpoint(const std::string& params)
{
    // z type,addr,kind
    // TODO: Remove breakpoint
    (void)params;
    return "OK";
}

std::string GDBSession::handleContinue()
{
    // TODO: Resume emulator and wait for stop event
    return formatStopReply();
}

std::string GDBSession::handleStep()
{
    // TODO: Single step and return stop reply
    return formatStopReply();
}

std::string GDBSession::handleInterrupt()
{
    // TODO: Pause emulator
    return "T02thread:1;";  // SIGINT
}

std::string GDBSession::handleBackwardStep()
{
    // TODO: TTD reverse step
    return formatStopReply();
}

std::string GDBSession::handleBackwardContinue()
{
    // TODO: TTD reverse continue
    return formatStopReply();
}

std::string GDBSession::formatStopReply()
{
    // TODO: Format proper stop reply based on actual stop reason
    return "T05thread:1;";
}

std::string GDBSession::handleAttach(const std::string& pid)
{
    // TODO: Attach to emulator instance
    _attachedInstanceId = pid;
    return "T05thread:1;";
}

std::string GDBSession::handleDetach()
{
    _attachedInstanceId.clear();
    _hasRunControlClaim = false;
    _active = false;
    return "OK";
}
