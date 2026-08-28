#include "dzrpserver.h"
#include "platform-sockets.h"
#include <cstring>
#include <iostream>

namespace dzrp {

Server::Server(IDebugInterface* debug, const ServerConfig& config)
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
        std::cerr << "[DZRP] Failed to create socket\n";
        return false;
    }

    // Allow address reuse
    int opt = 1;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_config.port);
    inet_pton(AF_INET, m_config.bindAddress.c_str(), &addr.sin_addr);

    if (bind(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        std::cerr << "[DZRP] Failed to bind to port " << m_config.port << "\n";
        closeSocket(m_listenSocket);
        return false;
    }

    // Get actual port (useful if port was 0)
    socklen_t addrLen = sizeof(addr);
    getsockname(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), &addrLen);
    m_actualPort = ntohs(addr.sin_port);

    if (listen(m_listenSocket, 1) < 0)
    {
        std::cerr << "[DZRP] Failed to listen\n";
        closeSocket(m_listenSocket);
        return false;
    }

    m_running.store(true);
    m_stopRequested.store(false);
    m_acceptThread = std::thread(&Server::acceptLoop, this);

    std::cout << "[DZRP] Server listening on " << m_config.bindAddress
              << ":" << m_actualPort << "\n";
    return true;
}

void Server::stop()
{
    if (!m_running.load())
        return;

    m_stopRequested.store(true);

    // Close listen socket to unblock accept()
    if (m_listenSocket != INVALID_SOCKET)
    {
        closeSocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }

    // Close client socket
    {
        std::lock_guard<std::mutex> lock(m_sessionMutex);
        if (m_clientSocket != INVALID_SOCKET)
        {
            closeSocket(m_clientSocket);
            m_clientSocket = INVALID_SOCKET;
        }
    }

    if (m_acceptThread.joinable())
        m_acceptThread.join();

    m_running.store(false);
    cleanupSockets();

    std::cout << "[DZRP] Server stopped\n";
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
        std::cout << "[DZRP] Client connected from " << clientIp << "\n";

        {
            std::lock_guard<std::mutex> lock(m_sessionMutex);
            m_clientSocket = clientSock;
        }

        sessionLoop(clientSock);

        {
            std::lock_guard<std::mutex> lock(m_sessionMutex);
            if (m_clientSocket == clientSock)
            {
                closeSocket(m_clientSocket);
                m_clientSocket = INVALID_SOCKET;
            }
        }

        std::cout << "[DZRP] Client disconnected\n";
    }
}

void Server::sessionLoop(int clientSocket)
{
    std::vector<uint8_t> recvBuffer;
    recvBuffer.reserve(65536);

    uint8_t tempBuf[4096];

    while (!m_stopRequested.load())
    {
        // Wait for data with timeout
        int pollResult = waitForSocketRead(clientSocket, 100);
        if (pollResult < 0)
            break;
        if (pollResult == 0)
            continue;

        ssize_t bytesRead = recv(clientSocket, reinterpret_cast<char*>(tempBuf),
                                  sizeof(tempBuf), 0);
        if (bytesRead <= 0)
            break;

        recvBuffer.insert(recvBuffer.end(), tempBuf, tempBuf + bytesRead);

        // Process complete messages
        while (recvBuffer.size() >= 4)
        {
            std::vector<uint8_t> payload;
            size_t consumed = Protocol::readFramedMessage(
                recvBuffer.data(), recvBuffer.size(), payload);

            if (consumed == 0)
                break;

            // Parse and handle command
            auto cmdOpt = Protocol::parseCommand(payload.data(), payload.size());
            if (cmdOpt)
            {
                m_postResponseAction = nullptr;
                Response resp = handleCommand(*cmdOpt);
                auto respData = Protocol::serializeResponse(resp);

                if (!sendAll(clientSocket, respData))
                    return;

                // Deferred side effects (e.g. pause → NTF_PAUSE) run only after the ACK is on the wire
                if (m_postResponseAction)
                {
                    auto action = std::move(m_postResponseAction);
                    m_postResponseAction = nullptr;
                    action();
                }
            }

            recvBuffer.erase(recvBuffer.begin(), recvBuffer.begin() + consumed);
        }
    }
}

Response Server::handleCommand(const Command& cmd)
{
    switch (cmd.cmdId)
    {
        case CommandId::CMD_INIT:
            return handleInit(cmd);
        case CommandId::CMD_CLOSE:
            return handleClose(cmd);
        case CommandId::CMD_GET_REGISTERS:
            return handleGetRegisters(cmd);
        case CommandId::CMD_SET_REGISTER:
            return handleSetRegister(cmd);
        case CommandId::CMD_CONTINUE:
            return handleContinue(cmd);
        case CommandId::CMD_PAUSE:
            return handlePause(cmd);
        case CommandId::CMD_READ_MEM:
            return handleReadMem(cmd);
        case CommandId::CMD_WRITE_MEM:
            return handleWriteMem(cmd);
        case CommandId::CMD_ADD_BREAKPOINT:
            return handleAddBreakpoint(cmd);
        case CommandId::CMD_REMOVE_BREAKPOINT:
            return handleRemoveBreakpoint(cmd);
        case CommandId::CMD_ADD_WATCHPOINT:
            return handleAddWatchpoint(cmd);
        case CommandId::CMD_REMOVE_WATCHPOINT:
            return handleRemoveWatchpoint(cmd);
        case CommandId::CMD_GET_SUPPORTED_COMMANDS:
            return handleGetSupportedCommands(cmd);
        case CommandId::CMD_SET_SLOT:
            return handleSetSlot(cmd);
        case CommandId::CMD_WRITE_BANK:
            return handleWriteBank(cmd);
        case CommandId::CMD_SET_BORDER:
            return handleSetBorder(cmd);
        case CommandId::CMD_READ_STATE:
            return handleReadState(cmd);
        case CommandId::CMD_WRITE_STATE:
            return handleWriteState(cmd);
        default:
            return makeNak(cmd.seqNo);
    }
}

Response Server::makeNak(uint8_t seqNo)
{
    // NOTE: DeZog client doesn't handle NAK bit (compares seq bytes raw).
    // Return empty ACK instead to avoid session-fatal "wrong SeqNo" error.
    // Keep NAK code path dormant until DeZog implements NAK receiving.
    Response resp;
    resp.seqNo = seqNo;
    resp.nak = false;  // Empty ACK, not NAK
    std::cerr << "[DZRP] Unknown command, returning empty ACK\n";
    return resp;
}

bool Server::sendAll(int socket, const std::vector<uint8_t>& data)
{
    std::lock_guard<std::mutex> lock(m_sessionMutex);

    size_t totalSent = 0;
    while (totalSent < data.size())
    {
        ssize_t sent = send(socket,
                            reinterpret_cast<const char*>(data.data() + totalSent),
                            data.size() - totalSent, 0);
        if (sent <= 0)
            return false;
        totalSent += static_cast<size_t>(sent);
    }
    return true;
}

Response Server::handleInit(const Command& cmd)
{
    Response resp;
    resp.seqNo = cmd.seqNo;

    // Parse client version and name from payload
    // payload: version(3) + name(nul-terminated)

    // Build response: error(1) + version(3) + machine(1) + name(nul)
    resp.payload.push_back(0);  // No error

    resp.payload.push_back(VERSION_MAJOR);
    resp.payload.push_back(VERSION_MINOR);
    resp.payload.push_back(VERSION_PATCH);

    resp.payload.push_back(static_cast<uint8_t>(m_debug->getMachineType()));

    std::string fullName = m_config.serverName + " v" + m_config.serverVersion;
    Protocol::writeNulString(resp.payload, fullName);

    std::cout << "[DZRP] CMD_INIT - handshake complete\n";
    return resp;
}

Response Server::handleClose(const Command& cmd)
{
    Response resp;
    resp.seqNo = cmd.seqNo;
    std::cout << "[DZRP] CMD_CLOSE - session ending\n";
    return resp;
}

Response Server::handleGetRegisters(const Command& cmd)
{
    Response resp;
    resp.seqNo = cmd.seqNo;

    auto regs = m_debug->getRegisters();
    auto slots = m_debug->getSlots();

    // 29 bytes of registers + Nslots + slot data
    resp.payload.resize(29 + 1 + slots.size());

    uint8_t* p = resp.payload.data();
    Protocol::writeU16LE(p + 0, regs.pc);
    Protocol::writeU16LE(p + 2, regs.sp);
    Protocol::writeU16LE(p + 4, regs.af);
    Protocol::writeU16LE(p + 6, regs.bc);
    Protocol::writeU16LE(p + 8, regs.de);
    Protocol::writeU16LE(p + 10, regs.hl);
    Protocol::writeU16LE(p + 12, regs.ix);
    Protocol::writeU16LE(p + 14, regs.iy);
    Protocol::writeU16LE(p + 16, regs.af2);
    Protocol::writeU16LE(p + 18, regs.bc2);
    Protocol::writeU16LE(p + 20, regs.de2);
    Protocol::writeU16LE(p + 22, regs.hl2);
    p[24] = regs.r;
    p[25] = regs.i;
    p[26] = regs.im;
    p[27] = 0;  // reserved
    p[28] = static_cast<uint8_t>(slots.size());

    for (size_t i = 0; i < slots.size(); ++i)
    {
        p[29 + i] = slots[i];
    }

    return resp;
}

Response Server::handleSetRegister(const Command& cmd)
{
    Response resp;
    resp.seqNo = cmd.seqNo;

    if (cmd.payload.size() >= 3)
    {
        auto regId = static_cast<RegisterId>(cmd.payload[0]);
        uint16_t value = Protocol::readU16LE(cmd.payload.data() + 1);
        m_debug->setRegister(regId, value);
    }

    return resp;
}

Response Server::handleContinue(const Command& cmd)
{
    Response resp;
    resp.seqNo = cmd.seqNo;

    // Clear any previous temp breakpoints via the debug interface
    m_debug->clearTemporaryBreakpoints();

    // payload: bp1_enable(1) + bp1_addr(2) + bp2_enable(1) + bp2_addr(2) +
    //          alternate(1) + range_start(2) + range_end(2)
    if (cmd.payload.size() >= 11)
    {
        bool bp1Enable = cmd.payload[0] != 0;
        uint16_t bp1Addr = Protocol::readU16LE(cmd.payload.data() + 1);
        bool bp2Enable = cmd.payload[3] != 0;
        uint16_t bp2Addr = Protocol::readU16LE(cmd.payload.data() + 4);
        // uint8_t alternate = cmd.payload[6];  // 0=none, 1=step-over, 2=step-out (not impl)

        // Set temporary breakpoints for step-into/step-over (marked temporary=true)
        if (bp1Enable)
        {
            uint16_t id = m_debug->addBreakpoint(bp1Addr, 0, "", true);
            if (id == 0)
                std::cerr << "[DZRP] Failed to set temp BP at 0x" << std::hex << bp1Addr << std::dec << "\n";
        }
        if (bp2Enable)
        {
            uint16_t id = m_debug->addBreakpoint(bp2Addr, 0, "", true);
            if (id == 0)
                std::cerr << "[DZRP] Failed to set temp BP at 0x" << std::hex << bp2Addr << std::dec << "\n";
        }
    }

    m_debug->resume();

    return resp;
}

Response Server::handlePause(const Command& cmd)
{
    Response resp;
    resp.seqNo = cmd.seqNo;

    // The debug interface emits NTF_PAUSE synchronously from pause(); defer it
    // until the ACK has been sent so the wire order is always ACK → NTF_PAUSE.
    m_postResponseAction = [this]() { m_debug->pause(); };

    return resp;
}

Response Server::handleReadMem(const Command& cmd)
{
    Response resp;
    resp.seqNo = cmd.seqNo;

    if (cmd.payload.size() >= 5)
    {
        // reserved(1) + addr(2) + size(2)
        uint16_t addr = Protocol::readU16LE(cmd.payload.data() + 1);
        uint16_t size = Protocol::readU16LE(cmd.payload.data() + 3);

        resp.payload = m_debug->readMemory(addr, size);
    }

    return resp;
}

Response Server::handleWriteMem(const Command& cmd)
{
    Response resp;
    resp.seqNo = cmd.seqNo;

    if (cmd.payload.size() >= 3)
    {
        // reserved(1) + addr(2) + data...
        uint16_t addr = Protocol::readU16LE(cmd.payload.data() + 1);
        std::vector<uint8_t> data(cmd.payload.begin() + 3, cmd.payload.end());
        m_debug->writeMemory(addr, data);
    }

    return resp;
}

Response Server::handleAddBreakpoint(const Command& cmd)
{
    Response resp;
    resp.seqNo = cmd.seqNo;

    uint16_t bpId = 0;

    if (cmd.payload.size() >= 4)
    {
        // addr(2) + bank(1) + condition(nul-terminated)
        uint16_t addr = Protocol::readU16LE(cmd.payload.data());
        uint8_t bank = cmd.payload[2];
        std::string condition = Protocol::readNulString(
            cmd.payload.data() + 3, cmd.payload.size() - 3);

        bpId = m_debug->addBreakpoint(addr, bank, condition);
        m_breakpoints[bpId] = addr;
    }

    resp.payload.resize(2);
    Protocol::writeU16LE(resp.payload.data(), bpId);

    return resp;
}

Response Server::handleRemoveBreakpoint(const Command& cmd)
{
    Response resp;
    resp.seqNo = cmd.seqNo;

    if (cmd.payload.size() >= 2)
    {
        uint16_t bpId = Protocol::readU16LE(cmd.payload.data());
        m_debug->removeBreakpoint(bpId);
        m_breakpoints.erase(bpId);
    }

    return resp;
}

Response Server::handleAddWatchpoint(const Command& cmd)
{
    Response resp;
    resp.seqNo = cmd.seqNo;

    uint8_t error = 1;  // Default error

    if (cmd.payload.size() >= 6)
    {
        // addr(2) + bank(1) + size(2) + access(1)
        uint16_t addr = Protocol::readU16LE(cmd.payload.data());
        uint8_t bank = cmd.payload[2];
        uint16_t size = Protocol::readU16LE(cmd.payload.data() + 3);
        auto access = static_cast<WatchAccess>(cmd.payload[5]);

        if (m_debug->addWatchpoint(addr, bank, size, access))
            error = 0;
    }

    resp.payload.push_back(error);
    return resp;
}

Response Server::handleRemoveWatchpoint(const Command& cmd)
{
    Response resp;
    resp.seqNo = cmd.seqNo;

    if (cmd.payload.size() >= 6)
    {
        uint16_t addr = Protocol::readU16LE(cmd.payload.data());
        uint8_t bank = cmd.payload[2];
        uint16_t size = Protocol::readU16LE(cmd.payload.data() + 3);
        auto access = static_cast<WatchAccess>(cmd.payload[5]);

        m_debug->removeWatchpoint(addr, bank, size, access);
    }

    return resp;
}

Response Server::handleGetSupportedCommands(const Command& cmd)
{
    Response resp;
    resp.seqNo = cmd.seqNo;
    resp.payload = Protocol::buildSupportedCommandsBitfield();
    return resp;
}

Response Server::handleSetSlot(const Command& cmd)
{
    Response resp;
    resp.seqNo = cmd.seqNo;

    uint8_t error = 0;

    if (cmd.payload.size() >= 2)
    {
        uint8_t slot = cmd.payload[0];
        uint8_t bank = cmd.payload[1];
        m_debug->setSlot(slot, bank);
    }

    resp.payload.push_back(error);
    return resp;
}

Response Server::handleWriteBank(const Command& cmd)
{
    Response resp;
    resp.seqNo = cmd.seqNo;

    if (cmd.payload.size() >= 2)
    {
        uint8_t bank = cmd.payload[0];
        std::vector<uint8_t> data(cmd.payload.begin() + 1, cmd.payload.end());
        m_debug->writeBank(bank, data);
    }

    // error(1) + message(nul)
    resp.payload.push_back(0);  // No error
    resp.payload.push_back(0);  // Empty message

    return resp;
}

Response Server::handleSetBorder(const Command& cmd)
{
    Response resp;
    resp.seqNo = cmd.seqNo;

    if (cmd.payload.size() >= 1)
    {
        uint8_t color = cmd.payload[0] & 0x07;
        m_debug->setBorder(color);
    }

    return resp;
}

Response Server::handleReadState(const Command& cmd)
{
    Response resp;
    resp.seqNo = cmd.seqNo;
    resp.payload = m_debug->captureState();
    return resp;
}

Response Server::handleWriteState(const Command& cmd)
{
    Response resp;
    resp.seqNo = cmd.seqNo;

    if (!cmd.payload.empty())
    {
        m_debug->restoreState(cmd.payload);
    }

    return resp;
}

void Server::notifyPause(BreakReason reason, uint16_t addr, uint8_t bank,
                         const std::string& message)
{
    // Clear temporary breakpoints on pause
    m_debug->clearTemporaryBreakpoints();

    std::lock_guard<std::mutex> lock(m_sessionMutex);
    if (m_clientSocket == INVALID_SOCKET)
        return;

    Notification notif;
    notif.notifyId = NotificationId::NTF_PAUSE;

    // reason(1) + addr(2) + bank(1) + message(nul)
    notif.payload.push_back(static_cast<uint8_t>(reason));

    size_t addrOffset = notif.payload.size();
    notif.payload.resize(addrOffset + 2);
    Protocol::writeU16LE(notif.payload.data() + addrOffset, addr);

    notif.payload.push_back(bank);

    Protocol::writeNulString(notif.payload, message);

    auto data = Protocol::serializeNotification(notif);

    // Send all bytes
    size_t totalSent = 0;
    while (totalSent < data.size())
    {
        ssize_t sent = send(m_clientSocket,
                            reinterpret_cast<const char*>(data.data() + totalSent),
                            data.size() - totalSent, 0);
        if (sent <= 0)
            return;
        totalSent += static_cast<size_t>(sent);
    }
}

} // namespace dzrp
