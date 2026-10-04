#include "emulator/io/serial/esp/zififtpserver.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/network/nettypes.h"
#include "emulator/io/serial/esp/espstack.h"
#include "emulator/io/serial/esp/zifibridgehost.h"
#include "emulator/io/serial/esp/zififattime.h"
#include "emulator/io/serial/esp/zifistate.h"

// Every behavior here is the firmwares' (file:function in the two repositories named in the header):
//  S3  = ZiFi-ESP32-S3-Zero 2e5ba83 src/ftp_server.cpp
//  E01 = ZiFi-ESP-01S-Native-C-Project 90834e4 src/ftp_server.cpp
// The firmwares run each command to its end in one call (blocking on the VFS and the sockets); here a command is a
// job whose step waits for the VFS result, the data connection or more TCP bytes.

namespace
{
constexpr uint64_t kControlIdleTimeoutUs = 300000000;  // kControlIdleTimeoutMs
constexpr uint64_t kDataConnectTimeoutUs = 10000000;   // kDataConnectTimeoutMs
constexpr uint64_t kStorIdleTimeoutUs = 60000000;      // kStorIdleTimeoutMs
constexpr uint64_t kVfsNormalTimeoutUs = 10000000;     // S3 kVfsNormalTimeoutMs
constexpr uint64_t kVfsMutateTimeoutUs = 65000000;     // S3 kVfsMutateTimeoutMs
constexpr uint64_t kVfsCloseTimeoutUs = 185000000;     // S3 kVfsCloseTimeoutMs
constexpr uint64_t kNoTimeout = ~0ull;
constexpr size_t kMaxLine = 512;
constexpr size_t kMaxPath = 255;
constexpr size_t kDataChunk = 1024;                    // S3 ioBuffer_[kDataChunk]
constexpr size_t kE01DataChunk = 512;                  // E01 ioBuffer_[kDataChunk]
constexpr size_t kE01StorSlotSize = 256;
constexpr size_t kE01StorSlots = 4;
constexpr int64_t kClockValidSince = 1577836800;       // 2020-01-01
constexpr int64_t kListRecentSeconds = 183LL * 86400;
constexpr int64_t kListFutureSkewSeconds = 3600;
constexpr uint8_t kMetadataWriteTime = 0x04;
// A fresh module's free memory (the firmware prints ESP.getFreeHeap(), ESP.getFreePsram(); fixed here as in SYS_INFO)
constexpr uint32_t kS3FreeHeap = 214528, kS3FreePsram = 1918476, kE01FreeHeap = 27464;
constexpr uint8_t kEventFtpClient = 0x60, kEventFtpCommand = 0x61;

std::string Upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string Printf(const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

std::string Printf(const char* format, ...)
{
    char text[400];
    va_list arguments;
    va_start(arguments, format);
    const int length = std::vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    if (length < 0)
        return {};
    return std::string(text, std::min<size_t>(static_cast<size_t>(length), sizeof(text) - 1));
}

zififat::Stamp WriteStamp(const ZiFiVfsBridge::Result& entry)
{
    zififat::Stamp s;
    s.date = entry.writeDate;
    s.time = entry.writeTime;
    return s;
}
}  // namespace

ZiFiFtpServer::ZiFiFtpServer(ZiFiBridgeHost& host, bool s3) : _host(host), _s3(s3)
{
    for (int i = 0; i < 3; ++i)
        _sessions[i].passivePort = static_cast<uint16_t>(kPassivePort + (s3 ? i : 0));
}

uint64_t ZiFiFtpServer::Now() const
{
    return _host.BridgeNow();
}

uint64_t ZiFiFtpServer::Us(uint64_t us) const
{
    return _host.BridgeMicros(us);
}

int32_t ZiFiFtpServer::TimezoneSeconds() const
{
    return static_cast<int32_t>(_host.BridgeTimeZone()) * 3600;
}

// --- Start / stop --------------------------------------------------------------------------------------------------

bool ZiFiFtpServer::Start(const std::vector<uint8_t>& payload, uint16_t& actualPort, std::string& error)
{
    Stop();
    const size_t length = payload.size();
    _port = length >= 2 ? static_cast<uint16_t>(payload[0] | (payload[1] << 8)) : 21;
    if (_port == 0)
    {
        error = "ftp port zero";
        return false;
    }
    _user = "zx";
    _password = "zx";
    // copyCredential: up to the NUL or the payload's end; user[33], password[65]
    size_t offset = length >= 2 ? 2 : length;
    const auto credential = [&](size_t capacity, std::string& out) {
        std::string parsed;
        while (offset < length && payload[offset] != 0)
        {
            if (parsed.size() + 1 >= capacity)
                return false;
            parsed.push_back(static_cast<char>(payload[offset++]));
        }
        if (offset < length && payload[offset] == 0)
            ++offset;
        if (!parsed.empty())
            out = parsed;
        return true;
    };
    if (offset < length && !credential(33, _user))
    {
        error = "ftp user too long";
        return false;
    }
    if (offset < length && !credential(65, _password))
    {
        error = "ftp password too long";
        return false;
    }
    if (!_host.BridgeWifiUp())
    {
        error = "ftp no wifi";
        return false;
    }
    EspStack& stack = _host.BridgeStack();
    stack.Close(kListenSlot);
    if (stack.OpenAt(kListenSlot, true) < 0)
    {
        error = "ftp listen failed";
        return false;
    }
    stack.Bind(kListenSlot, _port);
    stack.Listen(kListenSlot);
    _running = true;
    actualPort = _port;
    error.clear();
    return true;
}

void ZiFiFtpServer::Stop()
{
    _vfsOwner = -1;
    _job = Job();
    for (int i = 0; i < MaxSessions(); ++i)
        DropControl(i);
    _host.BridgeStack().Close(kListenSlot);
    _host.BridgeStack().Close(kRefuseSlot);
    _running = false;
    _ramStatCount = 0;
    _storSlots.clear();
    _storSlotActive = false;
}

void ZiFiFtpServer::LinkLost()
{
    // The sockets died with the association: each session ends the way receiveControl finds a closed client
    _vfsOwner = -1;
    _job = Job();
    EspStack& stack = _host.BridgeStack();
    for (int i = 0; i < MaxSessions(); ++i)
    {
        if (_sessions[i].active)
            DropControl(i);
    }
    if (_running)
    {
        stack.Close(kListenSlot);
        stack.OpenAt(kListenSlot, true);
        stack.Bind(kListenSlot, _port);
        stack.Listen(kListenSlot);
    }
}

void ZiFiFtpServer::Forget()
{
    _running = false;
    _port = 21;
    _user = "zx";
    _password = "zx";
    for (int i = 0; i < 3; ++i)
    {
        _sessions[i] = Session();
        _sessions[i].passivePort = static_cast<uint16_t>(kPassivePort + (_s3 ? i : 0));
    }
    _vfsOwner = -1;
    _job = Job();
    _lastVfsError = "none";
    _vfsResult = ZiFiVfsBridge::Result();
    _storEof = _storError = false;
    _storReceived = 0;
    _storLastProgress = 0;
    _storSlots.clear();
    _storSlotActive = false;
    _ramStatCount = 0;
}

std::vector<uint8_t> ZiFiFtpServer::RamStats() const
{
    // makeRamStats: [count][at LE32][free LE32]...
    std::vector<uint8_t> out;
    if (_ramStatCount == 0)
        return out;
    out.push_back(_ramStatCount);
    for (uint8_t i = 0; i < _ramStatCount && i < 2; ++i)
    {
        for (int b = 0; b < 4; ++b)
            out.push_back(static_cast<uint8_t>(_ramStatAt[i] >> (8 * b)));
        for (int b = 0; b < 4; ++b)
            out.push_back(static_cast<uint8_t>(_ramStatFree[i] >> (8 * b)));
    }
    return out;
}

// --- Events --------------------------------------------------------------------------------------------------------

void ZiFiFtpServer::SendClientEvent(uint8_t state)
{
    _host.BridgeEvent(kEventFtpClient, {state});
}

void ZiFiFtpServer::SendClientState()
{
    // S3 sendClientState: 2 when any session is logged in, 1 when one is connected
    uint8_t state = 0;
    for (int i = 0; i < MaxSessions(); ++i)
    {
        const Session& s = _sessions[i];
        if (!s.active)
            continue;
        state = s.loggedIn ? 2 : (state == 0 ? 1 : state);
        if (state == 2)
            break;
    }
    SendClientEvent(state);
}

void ZiFiFtpServer::SendCommandEvent(const std::string& command, const std::string& argument)
{
    std::string shown = command.substr(0, 30);
    if (!argument.empty() && shown != "USER" && shown != "PASS" && shown.size() < 30)
    {
        shown.push_back(' ');
        shown += argument.substr(0, 30 - shown.size());
    }
    _lastCommand = shown;
    _host.BridgeEvent(kEventFtpCommand, std::vector<uint8_t>(shown.begin(), shown.end()));
}

// --- Sockets -------------------------------------------------------------------------------------------------------

bool ZiFiFtpServer::SendAll(int slot, const uint8_t* data, size_t length)
{
    EspStack& stack = _host.BridgeStack();
    if (!stack.Valid(slot) || stack.GetSlot(slot).state != EspStack::State::Tcp || stack.GetSlot(slot).vnetId == 0)
        return false;
    if (length)
        stack.Send(slot, data, static_cast<uint32_t>(length));
    return true;
}

bool ZiFiFtpServer::SendAll(int slot, const std::string& text)
{
    return SendAll(slot, reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

bool ZiFiFtpServer::Reply(int index, const std::string& text)
{
    Session& s = _sessions[index];
    if (!s.active)
        return false;
    _lastReply = text.substr(0, text.find('\r'));
    if (text.size() >= 384)
        return Reply(index, "451 Reply is too long\r\n");
    if (!SendAll(kControlSlot + index, text))
    {
        DropControl(index);
        return false;
    }
    return true;
}

void ZiFiFtpServer::ClosePassive(int index)
{
    _host.BridgeStack().Close(kPassiveSlot + index);
    _sessions[index].passiveListening = false;
}

void ZiFiFtpServer::CloseData(int index)
{
    _host.BridgeStack().Close(kDataSlot + index);
}

void ZiFiFtpServer::DropControl(int index)
{
    Session& s = _sessions[index];
    EspStack& stack = _host.BridgeStack();
    // S3: the session was in use; E01: the WiFiClient is still connected (or has bytes)
    const int slot = kControlSlot + index;
    const bool e01Connected = stack.Valid(slot) && stack.GetSlot(slot).state == EspStack::State::Tcp &&
                              (stack.Established(slot) || !stack.GetSlot(slot).rx.empty());
    const bool hadClient = _s3 ? s.active : e01Connected;
    ClosePassive(index);
    CloseData(index);
    stack.Close(slot);
    const uint16_t passivePort = s.passivePort;
    s = Session();
    s.passivePort = passivePort;
    if (hadClient)
    {
        if (_s3)
            SendClientState();
        else
            SendClientEvent(0);
    }
}

void ZiFiFtpServer::AcceptControl()
{
    EspStack& stack = _host.BridgeStack();
    while (stack.Valid(kListenSlot) && stack.PendingClients(kListenSlot) > 0)
    {
        int free = -1;
        for (int i = 0; i < MaxSessions(); ++i)
        {
            // E01: a client that is still there blocks the next one; a stale one is dropped
            const bool busy = _s3 ? _sessions[i].active
                                  : _sessions[i].active && (stack.Established(kControlSlot + i) ||
                                                            !stack.GetSlot(kControlSlot + i).rx.empty());
            if (!busy)
            {
                free = i;
                break;
            }
        }
        if (free < 0)
        {
            stack.Close(kRefuseSlot);
            if (stack.AcceptInto(kListenSlot, kRefuseSlot) < 0)
                return;
            SendAll(kRefuseSlot, _s3 ? std::string("421 Too many FTP sessions (maximum 3)\r\n")
                                     : std::string("421 Only one FTP session is allowed\r\n"));
            stack.Close(kRefuseSlot);
            if (!_s3)
                return;   // E01 accepts one per poll
            continue;
        }
        DropControl(free);
        if (stack.AcceptInto(kListenSlot, kControlSlot + free) < 0)
            return;
        Session& s = _sessions[free];
        s.active = true;
        s.cwd = "/";
        s.lastControlActivity = Now();
        if (_s3)
        {
            SendClientState();
            Reply(free, Printf("220 ZiFi ESP32-S3 FTP ready %s ram=%u psram=%u\r\n", "s3-native-0.6.94", kS3FreeHeap,
                               kS3FreePsram));
        }
        else
        {
            SendClientEvent(1);
            Reply(free, Printf("220 ZiFi native FTP ready %s ram=%u\r\n", "native-0.2.2", kE01FreeHeap));
            return;
        }
    }
}

bool ZiFiFtpServer::CommandUsesVfs(const std::string& line)
{
    size_t p = line.find_first_not_of(" \t");
    if (p == std::string::npos)
        return false;
    std::string command;
    while (p < line.size() && line[p] != ' ' && line[p] != '\t' && command.size() + 1 < 8)
        command.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(line[p++]))));
    static const char* const kVfsCommands[] = {"CWD", "XCWD", "CDUP", "SIZE", "LIST", "NLST", "RETR", "STOR",
                                               "DELE", "MKD", "XMKD", "MDTM", "MFMT", "MLSD", "MLST"};
    for (const char* c : kVfsCommands)
    {
        if (command == c)
            return true;
    }
    return false;
}

void ZiFiFtpServer::DispatchCommand(int index, std::string line)
{
    Session& s = _sessions[index];
    if (!_s3 || !CommandUsesVfs(line))
        return ExecuteCommand(index, std::move(line));
    if (_vfsOwner >= 0 && _vfsOwner != index)
    {
        if (s.pendingCommand)
        {
            Reply(index, "503 Another command is already queued\r\n");
            return;
        }
        s.pendingLine = line.substr(0, kMaxLine);
        s.pendingCommand = true;
        return;
    }
    _vfsOwner = index;
    ExecuteCommand(index, std::move(line));
    if (!JobActive())
    {
        if (_vfsOwner == index)
            _vfsOwner = -1;
        s.lastControlActivity = Now();
    }
}

void ZiFiFtpServer::RunPendingCommand()
{
    if (_vfsOwner >= 0 || JobActive())
        return;
    for (int i = 0; i < MaxSessions(); ++i)
    {
        Session& s = _sessions[i];
        if (!s.active || !s.pendingCommand)
            continue;
        std::string line = s.pendingLine;
        s.pendingCommand = false;
        s.pendingLine.clear();
        DispatchCommand(i, line);
        return;
    }
}

void ZiFiFtpServer::ReceiveControl(int index)
{
    Session& s = _sessions[index];
    if (!s.active)
        return;
    EspStack& stack = _host.BridgeStack();
    const int slot = kControlSlot + index;
    const auto connected = [&]() { return stack.Established(slot); };
    const auto available = [&]() { return stack.Valid(slot) ? stack.GetSlot(slot).rx.size() : 0; };
    if (s.pendingCommand)
    {
        if (available() == 0 && !connected())
            DropControl(index);
        return;
    }
    while (available() > 0)
    {
        const std::vector<uint8_t> one = stack.Read(slot, 1);
        const char byte = static_cast<char>(one[0]);
        s.lastControlActivity = Now();
        if (byte == '\n')
        {
            if (s.discardLine)
            {
                s.discardLine = false;
                s.line.clear();
                Reply(index, "500 Line too long\r\n");
                continue;
            }
            while (!s.line.empty() && (s.line.back() == '\r' || s.line.back() == ' ' || s.line.back() == '\t'))
                s.line.pop_back();
            std::string line = s.line;
            s.line.clear();
            if (!line.empty())
                DispatchCommand(index, line);
            if (!s.active || s.pendingCommand || JobActive())
                return;   // the firmware is inside the command; the rest waits in the socket
            continue;
        }
        if (s.discardLine)
            continue;
        if (s.line.size() >= kMaxLine)
        {
            s.discardLine = true;
            s.line.clear();
            continue;
        }
        s.line.push_back(byte);
    }
    if (available() == 0 && !connected())
        DropControl(index);
    else if (!s.pendingCommand && Now() - s.lastControlActivity >= Us(kControlIdleTimeoutUs))
    {
        Reply(index, "421 Control connection timed out\r\n");
        DropControl(index);
    }
}

void ZiFiFtpServer::ServiceSessions(int excluded)
{
    for (int i = 0; i < MaxSessions(); ++i)
    {
        if (i != excluded)
            ReceiveControl(i);
    }
    AcceptControl();
}

void ZiFiFtpServer::Poll()
{
    if (!_running)
        return;
    if (JobActive())
    {
        RunJob();
        if (JobActive())
        {
            // S3: the other sessions are served while the owner's command waits (awaitVfs, openData); E01: one loop
            if (_s3)
                ServiceSessions(_job.session);
            return;
        }
    }
    if (_s3)
    {
        ServiceSessions(-1);
        RunPendingCommand();
    }
    else
    {
        AcceptControl();
        ReceiveControl(0);
    }
}

// --- Paths and arguments -------------------------------------------------------------------------------------------

bool ZiFiFtpServer::NormalizePath(const Session& session, const std::string& argument, std::string& output) const
{
    if (argument.empty())
    {
        output = session.cwd;
        return true;
    }
    output = (argument[0] == '/' || argument[0] == '\\') ? std::string("/") : session.cwd;
    size_t cursor = 0;
    while (cursor < argument.size())
    {
        while (cursor < argument.size() && (argument[cursor] == '/' || argument[cursor] == '\\'))
            ++cursor;
        if (cursor >= argument.size())
            break;
        std::string component;
        while (cursor < argument.size() && argument[cursor] != '/' && argument[cursor] != '\\')
        {
            if (component.size() >= kMaxPath)
                return false;
            component.push_back(argument[cursor++]);
        }
        if (component == "." || component.empty())
            continue;
        if (component == "..")
        {
            size_t length = output.size();
            while (length > 1 && output[length - 1] != '/')
                --length;
            if (length > 1)
                --length;
            output.resize(length);
            continue;
        }
        const size_t slash = output.size() > 1 ? 1 : 0;
        if (output.size() + slash + component.size() > kMaxPath)
            return false;
        if (slash)
            output.push_back('/');
        output += component;
    }
    return true;
}

bool ZiFiFtpServer::ParsePort(const std::string& argument, uint32_t& address, uint16_t& port)
{
    unsigned v[6];
    char extra = 0;
    if (std::sscanf(argument.c_str(), "%u,%u,%u,%u,%u,%u%c", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &extra) != 6)
        return false;
    for (unsigned x : v)
    {
        if (x > 255)
            return false;
    }
    const uint32_t full = v[4] * 256u + v[5];
    if (full == 0)
        return false;
    address = NetIp(static_cast<uint8_t>(v[0]), static_cast<uint8_t>(v[1]), static_cast<uint8_t>(v[2]),
                    static_cast<uint8_t>(v[3]));
    port = static_cast<uint16_t>(full);
    return true;
}

bool ZiFiFtpServer::ParseEprt(const std::string& argument, uint32_t& address, uint16_t& port)
{
    // |1|a.b.c.d|port| - IPv4 only
    if (argument.empty())
        return false;
    const char delimiter = argument[0];
    const size_t familyEnd = argument.find(delimiter, 1);
    if (familyEnd == std::string::npos || familyEnd != 2 || argument[1] != '1')
        return false;
    const size_t addressEnd = argument.find(delimiter, familyEnd + 1);
    if (addressEnd == std::string::npos || addressEnd == familyEnd + 1)
        return false;
    const size_t portEnd = argument.find(delimiter, addressEnd + 1);
    if (portEnd == std::string::npos || portEnd == addressEnd + 1 || portEnd + 1 != argument.size())
        return false;
    const std::string addressText = argument.substr(familyEnd + 1, addressEnd - familyEnd - 1);
    if (addressText.size() >= 16)
        return false;
    unsigned a = 0, b = 0, c = 0, d = 0;
    char tail = 0;
    if (std::sscanf(addressText.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 ||
        c > 255 || d > 255)
        return false;
    const std::string portText = argument.substr(addressEnd + 1, portEnd - addressEnd - 1);
    if (portText.size() >= 6)
        return false;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(portText.c_str(), &end, 10);
    if (end == portText.c_str() || *end != 0 || parsed == 0 || parsed > 65535)
        return false;
    address = NetIp(static_cast<uint8_t>(a), static_cast<uint8_t>(b), static_cast<uint8_t>(c), static_cast<uint8_t>(d));
    port = static_cast<uint16_t>(parsed);
    return true;
}

// --- Data connection -----------------------------------------------------------------------------------------------

void ZiFiFtpServer::EnterPassive(int index, bool extended)
{
    Session& s = _sessions[index];
    ClosePassive(index);
    if (_s3)
        CloseData(index);
    s.activeEndpointSet = false;
    EspStack& stack = _host.BridgeStack();
    const int slot = kPassiveSlot + index;
    if (stack.OpenAt(slot, true) < 0)
    {
        Reply(index, "425 Cannot enter passive mode\r\n");
        return;
    }
    stack.Bind(slot, s.passivePort);
    stack.Listen(slot);
    s.passiveListening = true;
    if (extended)
        Reply(index, Printf("229 Entering Extended Passive Mode (|||%u|)\r\n", s.passivePort));
    else
    {
        const uint32_t ip = _host.BridgeIp();
        Reply(index, Printf("227 Entering Passive Mode (%u,%u,%u,%u,%u,%u)\r\n", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                            (ip >> 8) & 0xFF, ip & 0xFF, s.passivePort >> 8, s.passivePort & 0xFF));
    }
}

void ZiFiFtpServer::SetActive(int index, const std::string& argument, bool extended)
{
    Session& s = _sessions[index];
    ClosePassive(index);
    if (_s3)
        CloseData(index);
    s.activeEndpointSet = false;
    uint32_t address = 0;
    uint16_t port = 0;
    if (!(extended ? ParseEprt(argument, address, port) : ParsePort(argument, address, port)))
    {
        Reply(index, "501 Bad PORT argument\r\n");
        return;
    }
    s.activeAddress = address;
    s.activePort = port;
    s.activeEndpointSet = true;
    Reply(index, "200 PORT command successful\r\n");
}

int ZiFiFtpServer::OpenData()
{
    // openData: the passive client (10 s) or the active connect (10 s); the job's `connecting` / `dataDeadline`
    Job& j = _job;
    const int index = j.session;
    Session& s = _sessions[index];
    EspStack& stack = _host.BridgeStack();
    const int data = kDataSlot + index;
    if (j.dataDeadline == 0)
    {
        CloseData(index);
        if (s.passiveListening)
        {
            j.connecting = false;
            j.dataDeadline = Now() + Us(kDataConnectTimeoutUs);
        }
        else if (s.activeEndpointSet)
        {
            s.activeEndpointSet = false;   // PORT / EPRT serve one transfer
            stack.OpenAt(data, true);
            stack.Connect(data, NetEndpoint{s.activeAddress, s.activePort});
            j.connecting = true;
            j.dataDeadline = Now() + Us(kDataConnectTimeoutUs);
        }
        else
        {
            Reply(index, "425 Use PASV/EPSV or PORT/EPRT first\r\n");
            return -1;
        }
    }
    if (!j.connecting)
    {
        if (stack.Valid(kPassiveSlot + index) && stack.PendingClients(kPassiveSlot + index) > 0)
        {
            stack.AcceptInto(kPassiveSlot + index, data);
            ClosePassive(index);
            j.dataDeadline = 0;
            return 1;
        }
        if (Now() >= j.dataDeadline)
        {
            ClosePassive(index);
            j.dataDeadline = 0;
            Reply(index, "425 Passive data connection timed out\r\n");
            return -1;
        }
        return 0;
    }
    const EspStack::Slot& d = stack.GetSlot(data);
    if (d.state == EspStack::State::Tcp && !d.connecting && d.vnetId)
    {
        j.connecting = false;
        j.dataDeadline = 0;
        return 1;
    }
    const bool failed = d.state != EspStack::State::Tcp && !d.connecting;
    if (failed || Now() >= j.dataDeadline)
    {
        CloseData(index);
        j.connecting = false;
        j.dataDeadline = 0;
        Reply(index, "425 Cannot open data connection\r\n");
        return -1;
    }
    return 0;
}

// --- Commands ------------------------------------------------------------------------------------------------------

void ZiFiFtpServer::ExecuteCommand(int index, std::string line)
{
    Session& s = _sessions[index];
    size_t p = line.find_first_not_of(" \t");
    if (p == std::string::npos)
        p = line.size();
    size_t e = p;
    while (e < line.size() && line[e] != ' ' && line[e] != '\t')
        ++e;
    const std::string command = Upper(line.substr(p, e - p));
    size_t a = e;
    while (a < line.size() && (line[a] == ' ' || line[a] == '\t'))
        ++a;
    const std::string argument = line.substr(a);
    SendCommandEvent(command, argument);

    if (command == "USER")
    {
        s.loggedIn = false;
        s.userAccepted = argument == _user;
        if (_s3)
            SendClientState();
        Reply(index, "331 Please specify the password\r\n");
        return;
    }
    if (command == "PASS")
    {
        if (s.userAccepted && argument == _password)
        {
            s.loggedIn = true;
            if (_s3)
                SendClientState();
            else
                SendClientEvent(2);
            Reply(index, "230 Login successful\r\n");
        }
        else
            Reply(index, "530 Login incorrect\r\n");
        return;
    }
    if (!s.loggedIn)
    {
        Reply(index, "530 Please login first\r\n");
        return;
    }
    if (command == "SYST")
        Reply(index, "215 ZX Spectrum\r\n");
    else if (command == "FEAT")
        Reply(index, _s3 ? "211-Features:\r\n EPSV\r\n UTF8\r\n SIZE\r\n MDTM\r\n MFMT\r\n MLST type*;size*;modify*;\r\n211 End\r\n"
                         : "211-Features:\r\n EPSV\r\n UTF8\r\n SIZE\r\n211 End\r\n");
    else if (command == "OPTS" || command == "NOOP")
        Reply(index, "200 OK\r\n");
    else if (command == "TYPE")
        Reply(index, "200 Switching to Binary mode\r\n");
    else if (command == "PWD" || command == "XPWD")
        Reply(index, "257 \"" + s.cwd + "\"\r\n");
    else if (command == "CWD" || command == "XCWD")
        StartJob(Job::Cwd, index, argument);
    else if (command == "CDUP")
        StartJob(Job::Cwd, index, "..");
    else if (command == "PORT")
        SetActive(index, argument, false);
    else if (command == "EPRT")
        SetActive(index, argument, true);
    else if (command == "PASV")
        EnterPassive(index, false);
    else if (command == "EPSV")
        EnterPassive(index, true);
    else if (command == "SIZE")
        StartJob(Job::Size, index, argument);
    else if (command == "LIST")
        StartJob(Job::List, index, argument, ListFormat::Long);
    else if (command == "NLST")
        StartJob(Job::List, index, argument, ListFormat::Names);
    else if (_s3 && command == "MLSD")
        StartJob(Job::List, index, argument, ListFormat::Machine);
    else if (_s3 && command == "MLST")
        StartJob(Job::Mlst, index, argument);
    else if (_s3 && command == "MDTM")
        StartJob(Job::Mdtm, index, argument);
    else if (_s3 && command == "MFMT")
        StartJob(Job::Mfmt, index, argument);
    else if (command == "RETR")
        StartJob(Job::Retr, index, argument);
    else if (command == "STOR")
        StartJob(Job::Stor, index, argument);
    else if (command == "DELE")
        StartJob(Job::Dele, index, argument);
    else if (command == "MKD" || command == "XMKD")
        StartJob(Job::Mkd, index, argument);
    else if (command == "QUIT")
    {
        Reply(index, "221 Goodbye\r\n");
        DropControl(index);
    }
    else
        Reply(index, "502 Command not implemented\r\n");
}

void ZiFiFtpServer::StartJob(Job::Kind kind, int index, const std::string& argument, ListFormat format)
{
    _job = Job();
    _job.kind = kind;
    _job.session = index;
    _job.rest = argument;
    _job.format = format;
    RunJob();
}

void ZiFiFtpServer::EndJob()
{
    const int index = _job.session;
    _job = Job();
    if (_s3)
    {
        // dispatchCommand's epilogue
        if (_vfsOwner == index)
            _vfsOwner = -1;
        _sessions[index].lastControlActivity = Now();
    }
}

void ZiFiFtpServer::RunJob()
{
    bool done = false;
    switch (_job.kind)
    {
        case Job::None: return;
        case Job::Cwd: done = StepCwd(_job); break;
        case Job::Size: done = StepSize(_job); break;
        case Job::List: done = StepList(_job); break;
        case Job::Mlst: done = StepMlst(_job); break;
        case Job::Mdtm: done = StepMdtm(_job); break;
        case Job::Mfmt: done = StepMfmt(_job); break;
        case Job::Retr: done = StepRetr(_job); break;
        case Job::Stor: done = _s3 ? StepStor(_job) : StepStorE01(_job); break;
        case Job::Dele: done = StepDele(_job); break;
        case Job::Mkd: done = StepMkd(_job); break;
    }
    if (done)
        EndJob();
}

bool ZiFiFtpServer::RequestVfs(ZiFiVfsBridge::Op op, const std::string& path, uint32_t value, uint64_t timeoutUs)
{
    if (!_host.BridgeVfs().Submit(op, path, value))
    {
        _lastVfsError = "bridge-busy";
        return false;
    }
    _job.vfsOp = static_cast<uint8_t>(op);
    _job.vfsDeadline = _s3 ? Now() + Us(timeoutUs) : kNoTimeout;
    return true;
}

bool ZiFiFtpServer::RequestMetadata(const ZiFiVfsBridge::Metadata& metadata)
{
    if (!_host.BridgeVfs().SubmitMetadata(metadata))
    {
        _lastVfsError = "bridge-busy";
        return false;
    }
    _job.vfsOp = static_cast<uint8_t>(ZiFiVfsBridge::Op::SetMetadata);
    _job.vfsDeadline = Now() + Us(kVfsMutateTimeoutUs);
    return true;
}

int ZiFiFtpServer::AwaitVfs(ZiFiVfsBridge::Result& result)
{
    if (_host.BridgeVfs().TakeResult(result))
    {
        _lastVfsError = result.success ? std::string("none") : result.error;
        return result.success ? 1 : -1;
    }
    if (_job.vfsDeadline != kNoTimeout && Now() >= _job.vfsDeadline)
    {
        // The request stays in the bridge (VfsBridge::requestPending_): the next one is "bridge-busy" until it ends
        _lastVfsError = "bridge-timeout-" + std::to_string(_job.vfsOp);
        result = ZiFiVfsBridge::Result();
        return -1;
    }
    return 0;
}

// Each step function runs its command until it has to wait; true when the command is done.
// The firmware's VFS calls with their FTP-level timeouts (S3; E01 has none above the client's own).

namespace
{
enum : uint8_t
{
    kBegin = 0,
    kStat,
    kOpen,
    kData,
    kRead,
    kSend,
    kClose,
    kWrite,
    kAbort,
    kDelete,
    kReset,
    kMeta,
    kOpenRandom,
    kFinish,
};
}  // namespace

bool ZiFiFtpServer::StepCwd(Job& j)
{
    const int i = j.session;
    if (j.step == kBegin)
    {
        if (!NormalizePath(_sessions[i], j.rest, j.path) || !RequestVfs(ZiFiVfsBridge::Op::Stat, j.path, 0, kVfsNormalTimeoutUs))
        {
            Reply(i, "550 Failed to change directory\r\n");
            return true;
        }
        j.step = kStat;
    }
    const int a = AwaitVfs(_vfsResult);
    if (a == 0)
        return false;
    if (a < 0 || !_vfsResult.isDirectory)
    {
        Reply(i, "550 Failed to change directory\r\n");
        return true;
    }
    _sessions[i].cwd = j.path;
    Reply(i, "250 Directory changed\r\n");
    return true;
}

bool ZiFiFtpServer::StepSize(Job& j)
{
    const int i = j.session;
    if (j.step == kBegin)
    {
        if (!NormalizePath(_sessions[i], j.rest, j.path) || !RequestVfs(ZiFiVfsBridge::Op::Stat, j.path, 0, kVfsNormalTimeoutUs))
        {
            Reply(i, "550 Could not get file size\r\n");
            return true;
        }
        j.step = kStat;
    }
    const int a = AwaitVfs(_vfsResult);
    if (a == 0)
        return false;
    if (a < 0 || _vfsResult.isDirectory)
        Reply(i, "550 Could not get file size\r\n");
    else
        Reply(i, Printf("213 %lu\r\n", static_cast<unsigned long>(_vfsResult.size)));
    return true;
}

std::string ZiFiFtpServer::FormatListDate(const ZiFiVfsBridge::Result& entry) const
{
    // "ls -l": "Mar 15 14:30" for files younger than half a year, "Mar 15  2024" else (and while the clock is unset)
    static const char* const kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!zififat::StampToCivil(WriteStamp(entry), year, month, day, hour, minute, second))
        return "Jan 01 00:00";
    bool recent = false;
    int64_t now = 0;
    if (_host.BridgeClock(now) && now >= kClockValidSince)
    {
        const int64_t age = now + TimezoneSeconds() - zififat::CivilToUnix(year, month, day, hour, minute, second);
        recent = age >= -kListFutureSkewSeconds && age < kListRecentSeconds;
    }
    if (recent)
        return Printf("%s %2d %02d:%02d", kMonths[month - 1], day, hour, minute);
    return Printf("%s %2d  %4d", kMonths[month - 1], day, year);
}

int ZiFiFtpServer::FormatFacts(const ZiFiVfsBridge::Result& entry, const std::string& name, std::string& output) const
{
    // RFC 3659 facts without the trailing space and name; modify in UTC, left out without a date
    const char* type = entry.isDirectory ? "dir" : "file";
    if (name == ".")
        type = "cdir";
    else if (name == "..")
        type = "pdir";
    output = std::string("type=") + type + ";";
    if (!entry.isDirectory)
        output += Printf("size=%lu;", static_cast<unsigned long>(entry.size));
    int64_t modified = 0;
    if (zififat::StampToUnix(WriteStamp(entry), TimezoneSeconds(), modified))
    {
        char value[15];
        zififat::FormatFtpTimeVal(modified, value);
        output += std::string("modify=") + value + ";";
    }
    return static_cast<int>(output.size());
}

bool ZiFiFtpServer::StepList(Job& j)
{
    const int i = j.session;
    Session& s = _sessions[i];
    for (;;)
    {
        switch (j.step)
        {
            case kBegin:
            {
                // LIST and NLST ignore their argument ("-la"); MLSD takes a directory (S3)
                std::string directory = s.cwd;
                if (_s3 && j.format == ListFormat::Machine && !j.rest.empty())
                {
                    if (!NormalizePath(s, j.rest, directory))
                    {
                        ClosePassive(i);
                        Reply(i, "550 Cannot list directory\r\n");
                        return true;
                    }
                }
                j.path = directory;
                if (!RequestVfs(ZiFiVfsBridge::Op::OpenDirectory, directory, 0, kVfsNormalTimeoutUs))
                {
                    ClosePassive(i);
                    Reply(i, "550 Cannot list directory\r\n");
                    return true;
                }
                j.step = kOpen;
                break;
            }
            case kOpen:
            {
                const int a = AwaitVfs(_vfsResult);
                if (a == 0)
                    return false;
                if (a < 0)
                {
                    ClosePassive(i);
                    Reply(i, "550 Cannot list directory\r\n");
                    return true;
                }
                Reply(i, "150 Here comes the directory listing\r\n");
                j.step = kData;
                break;
            }
            case kData:
            {
                const int d = OpenData();
                if (d == 0)
                    return false;
                if (d < 0)
                    return true;
                j.step = kRead;
                if (!RequestVfs(ZiFiVfsBridge::Op::ReadDirectory, {}, 0, kVfsNormalTimeoutUs))
                {
                    j.failure = _lastVfsError;
                    j.failed = true;
                    j.step = kClose;
                }
                break;
            }
            case kRead:
            {
                const int a = AwaitVfs(_vfsResult);
                if (a == 0)
                    return false;
                if (a < 0)
                {
                    j.failure = _s3 ? _lastVfsError : _host.BridgeVfs().LastError();
                    j.failed = true;
                    j.step = kClose;
                    break;
                }
                if (_vfsResult.atEnd)
                {
                    j.step = kClose;
                    break;
                }
                const ZiFiVfsBridge::Result& e = _vfsResult;
                bool failed = false;
                if (j.format == ListFormat::Names)
                    failed = !SendAll(kDataSlot + i, e.name) || !SendAll(kDataSlot + i, std::string("\r\n"));
                else
                {
                    std::string listing;
                    if (j.format == ListFormat::Long)
                    {
                        const std::string date = _s3 ? FormatListDate(e) : std::string("Jan 01 00:00");
                        listing = Printf("%crwxr-xr-x 1 zx zx %lu %s ", e.isDirectory ? 'd' : '-',
                                         static_cast<unsigned long>(e.size), date.c_str()) +
                                  e.name + "\r\n";
                    }
                    else
                    {
                        FormatFacts(e, e.name, listing);
                        listing += " " + e.name + "\r\n";
                    }
                    failed = listing.size() >= 384 || !SendAll(kDataSlot + i, listing);
                }
                if (failed)
                {
                    j.failure = _s3 ? std::string("data-send") : _host.BridgeVfs().LastError();
                    j.failed = true;
                    j.step = kClose;
                    break;
                }
                if (!RequestVfs(ZiFiVfsBridge::Op::ReadDirectory, {}, 0, kVfsNormalTimeoutUs))
                {
                    j.failure = _lastVfsError;
                    j.failed = true;
                    j.step = kClose;
                }
                break;
            }
            case kClose:
                CloseData(i);
                if (j.failed)
                    Reply(i, "426 Transfer aborted (" + j.failure + ")\r\n");
                else
                    Reply(i, "226 Directory send OK\r\n");
                return true;
            default: return true;
        }
    }
}

bool ZiFiFtpServer::StepMlst(Job& j)
{
    const int i = j.session;
    if (j.step == kBegin)
    {
        if (!NormalizePath(_sessions[i], j.rest, j.path) || !RequestVfs(ZiFiVfsBridge::Op::Stat, j.path, 0, kVfsNormalTimeoutUs))
        {
            Reply(i, "550 No such file or directory\r\n");
            return true;
        }
        j.step = kStat;
    }
    const int a = AwaitVfs(_vfsResult);
    if (a == 0)
        return false;
    std::string facts;
    if (a < 0 || FormatFacts(_vfsResult, {}, facts) >= 96)
    {
        Reply(i, "550 No such file or directory\r\n");
        return true;
    }
    Reply(i, "250-Listing " + j.path + "\r\n " + facts + " " + j.path + "\r\n250 End\r\n");
    return true;
}

bool ZiFiFtpServer::StepMdtm(Job& j)
{
    const int i = j.session;
    if (j.step == kBegin)
    {
        if (!NormalizePath(_sessions[i], j.rest, j.path) || !RequestVfs(ZiFiVfsBridge::Op::Stat, j.path, 0, kVfsNormalTimeoutUs))
        {
            Reply(i, "550 File not found\r\n");
            return true;
        }
        j.step = kStat;
    }
    const int a = AwaitVfs(_vfsResult);
    if (a == 0)
        return false;
    if (a < 0 || _vfsResult.isDirectory)
    {
        Reply(i, "550 File not found\r\n");
        return true;
    }
    int64_t modified = 0;
    if (!zififat::StampToUnix(WriteStamp(_vfsResult), TimezoneSeconds(), modified))
    {
        Reply(i, "550 Modification time not available\r\n");
        return true;
    }
    char value[15];
    zififat::FormatFtpTimeVal(modified, value);
    Reply(i, std::string("213 ") + value + "\r\n");
    return true;
}

bool ZiFiFtpServer::StepMfmt(Job& j)
{
    // MFMT YYYYMMDDhhmmss path: the write time into the WC entry (UTC to local by the zone)
    const int i = j.session;
    for (;;)
    {
        switch (j.step)
        {
            case kBegin:
            {
                const char* rest = nullptr;
                if (!zififat::ParseFtpTimeVal(j.rest.c_str(), j.requested, &rest) || *rest != ' ')
                {
                    Reply(i, "501 Usage: MFMT YYYYMMDDhhmmss path\r\n");
                    return true;
                }
                while (*rest == ' ')
                    ++rest;
                j.rest = rest;
                if (j.rest.empty() || !NormalizePath(_sessions[i], j.rest, j.path) ||
                    !RequestVfs(ZiFiVfsBridge::Op::Stat, j.path, 0, kVfsNormalTimeoutUs))
                {
                    Reply(i, "550 File not found\r\n");
                    return true;
                }
                j.step = kStat;
                break;
            }
            case kStat:
            {
                const int a = AwaitVfs(_vfsResult);
                if (a == 0)
                    return false;
                if (a < 0)
                {
                    Reply(i, "550 File not found\r\n");
                    return true;
                }
                // The old plugin took any non-zero OPEN mode for a write (it would delete the file on mode 3); only
                // the new one answers STAT with metadata
                if (!_vfsResult.hasMetadata)
                {
                    Reply(i, "550 Setting dates needs newer ZIFIFTP.WMF and WC Improved\r\n");
                    return true;
                }
                zififat::Stamp stamp;
                if (!zififat::UnixToStamp(j.requested, TimezoneSeconds(), stamp))
                {
                    Reply(i, "501 Time is outside the FAT range 1980-2107\r\n");
                    return true;
                }
                j.stampDate = stamp.date;
                j.stampTime = stamp.time;
                if (!RequestVfs(ZiFiVfsBridge::Op::OpenRandom, j.path, 0, kVfsNormalTimeoutUs))
                {
                    Reply(i, "550 Cannot open for metadata (" + _lastVfsError + ")\r\n");
                    return true;
                }
                j.step = kOpenRandom;
                break;
            }
            case kOpenRandom:
            {
                const int a = AwaitVfs(_vfsResult);
                if (a == 0)
                    return false;
                if (a < 0)
                {
                    Reply(i, "550 Cannot open for metadata (" + _lastVfsError + ")\r\n");
                    return true;
                }
                ZiFiVfsBridge::Metadata m;
                m.timeMask = kMetadataWriteTime;
                m.writeDate = j.stampDate;
                m.writeTime = j.stampTime;
                if (!RequestMetadata(m))
                {
                    j.set = false;
                    j.failure = _lastVfsError;
                    j.step = kClose;
                    if (!RequestVfs(ZiFiVfsBridge::Op::CloseCommit, {}, 0, kVfsCloseTimeoutUs))
                        j.step = kSend;
                    break;
                }
                j.step = kMeta;
                break;
            }
            case kMeta:
            {
                const int a = AwaitVfs(_vfsResult);
                if (a == 0)
                    return false;
                j.set = a > 0;
                j.failure = _lastVfsError;
                j.step = kClose;
                if (!RequestVfs(ZiFiVfsBridge::Op::CloseCommit, {}, 0, kVfsCloseTimeoutUs))
                    j.step = kSend;
                break;
            }
            case kClose:
            {
                if (AwaitVfs(_vfsResult) == 0)
                    return false;
                j.step = kSend;
                break;
            }
            case kSend:
            {
                if (!j.set)
                {
                    Reply(i, "550 Cannot set modification time (" + j.failure + ")\r\n");
                    return true;
                }
                // FAT keeps seconds in steps of 2: the answer says what is stored
                int64_t stored = j.requested;
                zififat::Stamp stamp;
                stamp.date = j.stampDate;
                stamp.time = j.stampTime;
                zififat::StampToUnix(stamp, TimezoneSeconds(), stored);
                char value[15];
                zififat::FormatFtpTimeVal(stored, value);
                Reply(i, std::string("213 Modify=") + value + "; " + j.rest + "\r\n");
                return true;
            }
            default: return true;
        }
    }
}

bool ZiFiFtpServer::StepRetr(Job& j)
{
    const int i = j.session;
    ZiFiVfsBridge& vfs = _host.BridgeVfs();
    for (;;)
    {
        switch (j.step)
        {
            case kBegin:
                if (!NormalizePath(_sessions[i], j.rest, j.path) ||
                    !RequestVfs(ZiFiVfsBridge::Op::Stat, j.path, 0, kVfsNormalTimeoutUs))
                {
                    ClosePassive(i);
                    Reply(i, "550 File not found\r\n");
                    return true;
                }
                j.step = kStat;
                break;
            case kStat:
            {
                const int a = AwaitVfs(j.stat);
                if (a == 0)
                    return false;
                if (a < 0 || j.stat.isDirectory)
                {
                    ClosePassive(i);
                    Reply(i, "550 File not found\r\n");
                    return true;
                }
                if (_s3)
                {
                    if (!RequestVfs(ZiFiVfsBridge::Op::ResetBuffers, {}, 0, kVfsNormalTimeoutUs))
                    {
                        ClosePassive(i);
                        Reply(i, "451 Cannot reset transfer buffer (" + _lastVfsError + ")\r\n");
                        return true;
                    }
                    j.step = kReset;
                    break;
                }
                if (!RequestVfs(ZiFiVfsBridge::Op::OpenRead, j.path, 0, kVfsNormalTimeoutUs))
                {
                    ClosePassive(i);
                    Reply(i, "550 Cannot open file\r\n");
                    return true;
                }
                j.step = kOpen;
                break;
            }
            case kReset:
            {
                const int a = AwaitVfs(_vfsResult);
                if (a == 0)
                    return false;
                if (a < 0)
                {
                    ClosePassive(i);
                    Reply(i, "451 Cannot reset transfer buffer (" + _lastVfsError + ")\r\n");
                    return true;
                }
                if (!RequestVfs(ZiFiVfsBridge::Op::OpenRead, j.path, 0, kVfsNormalTimeoutUs))
                {
                    ClosePassive(i);
                    Reply(i, "550 Cannot open file\r\n");
                    return true;
                }
                j.step = kOpen;
                break;
            }
            case kOpen:
            {
                const int a = AwaitVfs(_vfsResult);
                if (a == 0)
                    return false;
                if (a < 0)
                {
                    ClosePassive(i);
                    Reply(i, "550 Cannot open file\r\n");
                    return true;
                }
                Reply(i, "150 Opening data connection\r\n");
                j.step = kData;
                break;
            }
            case kData:
            {
                const int d = OpenData();
                if (d == 0)
                    return false;
                if (d < 0)
                {
                    // the file is closed again; openData already replied
                    j.failed = true;
                    j.step = kAbort;
                    if (!RequestVfs(ZiFiVfsBridge::Op::CloseCommit, {}, 0, kVfsCloseTimeoutUs))
                        return true;
                    break;
                }
                j.step = kSend;
                break;
            }
            case kAbort:
                if (AwaitVfs(_vfsResult) == 0)
                    return false;
                return true;
            case kSend:
            {
                // Send what the ring holds; read the next window when it is empty
                while (vfs.ToNetAvailable() > 0 && j.sent < j.stat.size)
                {
                    uint8_t buffer[kDataChunk];
                    const size_t received = vfs.ReadForNetwork(buffer, _s3 ? kDataChunk : kE01DataChunk);
                    if (received == 0 || !SendAll(kDataSlot + i, buffer, received))
                    {
                        j.failure = "data-send";
                        j.failed = true;
                        break;
                    }
                    j.sent += static_cast<uint32_t>(received);
                    _bytesSent += received;
                }
                if (j.failed || j.sent >= j.stat.size)
                {
                    j.step = kClose;
                    CloseData(i);
                    if (!RequestVfs(ZiFiVfsBridge::Op::CloseCommit, {}, 0, kVfsCloseTimeoutUs))
                    {
                        if (!j.failed)
                        {
                            j.failure = _lastVfsError;
                            j.failed = true;
                        }
                        j.step = kWrite;   // the reply
                    }
                    break;
                }
                const uint32_t remaining = j.stat.size - j.sent;
                const uint32_t wanted = _s3 ? std::min<uint32_t>(remaining, ZiFiVfsBridge::kTransferWindow)
                                            : std::min<uint32_t>(remaining, kE01DataChunk);
                if (!RequestVfs(ZiFiVfsBridge::Op::Read, {}, wanted, kVfsNormalTimeoutUs))
                {
                    j.failure = _lastVfsError;
                    j.failed = true;
                    break;
                }
                j.step = kRead;
                break;
            }
            case kRead:
            {
                const int a = AwaitVfs(_vfsResult);
                if (a == 0)
                    return false;
                if (a < 0)
                {
                    j.failure = _s3 ? _lastVfsError : vfs.LastError();
                    j.failed = true;
                }
                else if (_vfsResult.transferred == 0 || _vfsResult.atEnd)
                {
                    j.failure = _s3 ? std::string("early-eof") : vfs.LastError();
                    j.failed = true;
                }
                j.step = kSend;
                break;
            }
            case kClose:
            {
                const int a = AwaitVfs(_vfsResult);
                if (a == 0)
                    return false;
                if (a < 0 && !j.failed)
                {
                    j.failure = _lastVfsError;
                    j.failed = true;
                }
                j.step = kWrite;
                break;
            }
            case kWrite:
                if (j.failed || j.sent != j.stat.size)
                    Reply(i, Printf("426 Transfer aborted (sent=%lu size=%lu %s)\r\n", static_cast<unsigned long>(j.sent),
                                    static_cast<unsigned long>(j.stat.size),
                                    (_s3 ? j.failure : vfs.LastError()).c_str()));
                else
                {
                    ++_filesSent;
                    Reply(i, "226 Transfer complete\r\n");
                }
                return true;
            default: return true;
        }
    }
}

bool ZiFiFtpServer::PrefetchStore(int index)
{
    // prefetchStore: TCP into the ring while it has room
    if (_storEof || _storError)
        return false;
    EspStack& stack = _host.BridgeStack();
    ZiFiVfsBridge& vfs = _host.BridgeVfs();
    const int slot = kDataSlot + index;
    bool progressed = false;
    while (vfs.FromNetFree() != 0 && stack.Valid(slot) && !stack.GetSlot(slot).rx.empty())
    {
        const size_t wanted = std::min(std::min(vfs.FromNetFree(), stack.GetSlot(slot).rx.size()), kDataChunk);
        const std::vector<uint8_t> bytes = stack.Read(slot, static_cast<uint32_t>(wanted));
        if (bytes.empty() || vfs.WriteFromNetwork(bytes.data(), bytes.size()) != bytes.size())
        {
            _storError = true;
            return progressed;
        }
        _storReceived += static_cast<uint32_t>(bytes.size());
        _bytesReceived += bytes.size();
        _storLastProgress = Now();
        progressed = true;
    }
    const bool empty = !stack.Valid(slot) || stack.GetSlot(slot).rx.empty();
    if (empty && !stack.Established(slot))
        _storEof = true;
    return progressed;
}

bool ZiFiFtpServer::StepStor(Job& j)
{
    const int i = j.session;
    ZiFiVfsBridge& vfs = _host.BridgeVfs();
    for (;;)
    {
        switch (j.step)
        {
            case kBegin:
                if (!NormalizePath(_sessions[i], j.rest, j.path))
                {
                    ClosePassive(i);
                    Reply(i, "550 Bad file name\r\n");
                    return true;
                }
                if (!RequestVfs(ZiFiVfsBridge::Op::ResetBuffers, {}, 0, kVfsNormalTimeoutUs))
                {
                    ClosePassive(i);
                    Reply(i, "451 Cannot reset transfer buffer (" + _lastVfsError + ")\r\n");
                    return true;
                }
                j.step = kReset;
                break;
            case kReset:
            {
                const int a = AwaitVfs(_vfsResult);
                if (a == 0)
                    return false;
                if (a < 0)
                {
                    ClosePassive(i);
                    Reply(i, "451 Cannot reset transfer buffer (" + _lastVfsError + ")\r\n");
                    return true;
                }
                j.step = kData;
                break;
            }
            case kData:
            {
                // The data connection is taken before the slow create, so TCP can fill the ring meanwhile
                const int d = OpenData();
                if (d == 0)
                    return false;
                if (d < 0)
                    return true;
                _storEof = false;
                _storError = false;
                _storReceived = 0;
                _storLastProgress = Now();
                _ramStatCount = 1;
                _ramStatAt[0] = 0;
                _ramStatFree[0] = kS3FreeHeap;
                if (!RequestVfs(ZiFiVfsBridge::Op::OpenWrite, j.path, 0, kVfsMutateTimeoutUs))
                {
                    CloseData(i);
                    Reply(i, "550 Cannot create file (" + _lastVfsError + ")\r\n");
                    return true;
                }
                j.step = kOpen;
                break;
            }
            case kOpen:
            {
                PrefetchStore(i);   // the wait hook
                const int a = AwaitVfs(_vfsResult);
                if (a == 0)
                    return false;
                if (a < 0)
                {
                    CloseData(i);
                    Reply(i, "550 Cannot create file (" + _lastVfsError + ")\r\n");
                    return true;
                }
                Reply(i, "150 Opening data connection\r\n");
                j.step = kSend;
                break;
            }
            case kSend:
            {
                PrefetchStore(i);
                if (_storError)
                {
                    j.failure = "data-recv";
                    j.failed = true;
                    j.step = kClose;
                    break;
                }
                const size_t queued = vfs.FromNetAvailable();
                const size_t window = std::min(ZiFiVfsBridge::kTransferWindow, ZiFiVfsBridge::kRingCapacity);
                // A whole 16 KiB window, the tail only after the real EOF
                if (queued >= window || (_storEof && queued != 0))
                {
                    const uint32_t wanted = static_cast<uint32_t>(std::min(queued, ZiFiVfsBridge::kTransferWindow));
                    if (!RequestVfs(ZiFiVfsBridge::Op::Write, {}, wanted, kVfsMutateTimeoutUs))
                    {
                        j.failure = _lastVfsError;
                        j.failed = true;
                        j.step = kClose;
                        break;
                    }
                    j.step = kWrite;
                    break;
                }
                if (_storEof)
                {
                    j.step = kClose;
                    break;
                }
                if (Now() - _storLastProgress >= Us(kStorIdleTimeoutUs))
                {
                    j.failure = "data-timeout";
                    j.failed = true;
                    j.step = kClose;
                    break;
                }
                return false;
            }
            case kWrite:
            {
                PrefetchStore(i);   // the wait hook
                const int a = AwaitVfs(_vfsResult);
                if (a == 0)
                    return false;
                if (a < 0)
                {
                    j.failure = _lastVfsError;
                    j.failed = true;
                    j.step = kClose;
                    break;
                }
                if (_vfsResult.transferred == 0)
                {
                    j.failure = "vfs-no-progress";
                    j.failed = true;
                    j.step = kClose;
                    break;
                }
                _storLastProgress = Now();
                j.step = kSend;
                break;
            }
            case kClose:
                if (!j.failed && _storReceived == 0)
                {
                    j.failure = "data-empty";
                    j.failed = true;
                }
                CloseData(i);
                if (j.failed)
                {
                    // The aborted close drops the unwritten tail; DELETE removes the entry made by the create
                    if (RequestVfs(ZiFiVfsBridge::Op::CloseAbort, {}, 0, kVfsCloseTimeoutUs))
                        j.step = kAbort;
                    else if (RequestVfs(ZiFiVfsBridge::Op::Delete, j.path, 0, kVfsMutateTimeoutUs))
                        j.step = kDelete;
                    else
                        j.step = kFinish;
                }
                else
                {
                    if (!RequestVfs(ZiFiVfsBridge::Op::CloseCommit, {}, 0, kVfsCloseTimeoutUs))
                    {
                        j.failure = _lastVfsError;
                        j.failed = true;
                        if (!RequestVfs(ZiFiVfsBridge::Op::Delete, j.path, 0, kVfsMutateTimeoutUs))
                        {
                            j.step = kFinish;
                            break;
                        }
                        j.step = kDelete;
                        break;
                    }
                    j.step = kMeta;   // the commit's answer
                }
                break;
            case kMeta:
            {
                const int a = AwaitVfs(_vfsResult);
                if (a == 0)
                    return false;
                if (a < 0)
                {
                    j.failure = _lastVfsError;
                    j.failed = true;
                    if (!RequestVfs(ZiFiVfsBridge::Op::Delete, j.path, 0, kVfsMutateTimeoutUs))
                    {
                        j.step = kFinish;
                        break;
                    }
                    j.step = kDelete;
                    break;
                }
                j.step = kFinish;
                break;
            }
            case kAbort:
                if (AwaitVfs(_vfsResult) == 0)
                    return false;
                if (!RequestVfs(ZiFiVfsBridge::Op::Delete, j.path, 0, kVfsMutateTimeoutUs))
                {
                    j.step = kFinish;
                    break;
                }
                j.step = kDelete;
                break;
            case kDelete:
                if (AwaitVfs(_vfsResult) == 0)
                    return false;
                j.step = kFinish;
                break;
            case kFinish:
                _ramStatAt[1] = _storReceived;
                _ramStatFree[1] = kS3FreeHeap;
                _ramStatCount = 2;
                if (j.failed)
                {
                    Reply(i, Printf("426 Transfer aborted (%s bytes=%lu)\r\n", j.failure.c_str(),
                                    static_cast<unsigned long>(_storReceived)));
                    DropControl(i);
                }
                else
                {
                    ++_filesReceived;
                    Reply(i, "226 Transfer complete\r\n");
                }
                return true;
            default: return true;
        }
    }
}

bool ZiFiFtpServer::PrefetchStoreE01(int index)
{
    // prefetchStor: one 256-byte slot from TCP; four slots, the one the VFS writes is not refilled
    if (_storEof || _storError)
        return false;
    if (_storSlots.size() >= kE01StorSlots || (_storSlotActive && _storSlots.size() >= kE01StorSlots - 1))
        return false;
    EspStack& stack = _host.BridgeStack();
    const int slot = kDataSlot + index;
    const size_t available = stack.Valid(slot) ? stack.GetSlot(slot).rx.size() : 0;
    if (available == 0)
    {
        if (!stack.Established(slot))
            _storEof = true;
        return false;
    }
    std::vector<uint8_t> bytes = stack.Read(slot, static_cast<uint32_t>(std::min(available, kE01StorSlotSize)));
    _storReceived += static_cast<uint32_t>(bytes.size());
    _bytesReceived += bytes.size();
    _storLastProgress = Now();
    _storSlots.push_back(std::move(bytes));
    return true;
}

bool ZiFiFtpServer::StepStorE01(Job& j)
{
    // E01 store: data connection first, create, then slot by slot through writeFile (the hook reads TCP meanwhile)
    const int i = j.session;
    ZiFiVfsBridge& vfs = _host.BridgeVfs();
    static const char* const kKinds[] = {"recv", "zvfs", "recv-timeout", "recv-empty", "zvfs-close"};
    for (;;)
    {
        switch (j.step)
        {
            case kBegin:
                if (!NormalizePath(_sessions[i], j.rest, j.path))
                {
                    ClosePassive(i);
                    Reply(i, "550 Bad file name\r\n");
                    return true;
                }
                j.step = kData;
                break;
            case kData:
            {
                const int d = OpenData();
                if (d == 0)
                    return false;
                if (d < 0)
                    return true;
                _ramStatCount = 1;
                _ramStatAt[0] = 0;
                _ramStatFree[0] = kE01FreeHeap;
                RequestVfs(ZiFiVfsBridge::Op::OpenWrite, j.path, 0, 0);
                j.step = kOpen;
                break;
            }
            case kOpen:
            {
                const int a = AwaitVfs(_vfsResult);
                if (a == 0)
                    return false;
                if (a < 0)
                {
                    CloseData(i);
                    Reply(i, "550 Cannot create file\r\n");
                    return true;
                }
                Reply(i, "150 Opening data connection\r\n");
                // resetStorRing, then the first TCP flight into three slots
                _storSlots.clear();
                _storSlotActive = false;
                _storEof = false;
                _storError = false;
                _storReceived = 0;
                _storLastProgress = Now();
                while (_storSlots.size() < kE01StorSlots - 1 && !_storEof && !_storError && PrefetchStoreE01(i))
                {
                }
                j.step = kSend;
                break;
            }
            case kSend:
                if (!_storSlots.empty())
                {
                    std::vector<uint8_t> bytes = std::move(_storSlots.front());
                    _storSlots.pop_front();
                    _storSlotActive = true;
                    vfs.WriteFromNetwork(bytes.data(), bytes.size());
                    RequestVfs(ZiFiVfsBridge::Op::Write, {}, static_cast<uint32_t>(bytes.size()), 0);
                    j.step = kWrite;
                    break;
                }
                if (_storError)
                {
                    j.failed = true;
                    j.failureKind = 0;
                    j.step = kClose;
                    break;
                }
                if (_storEof)
                {
                    j.step = kClose;
                    break;
                }
                if (!PrefetchStoreE01(i))
                {
                    if (Now() - _storLastProgress >= Us(kStorIdleTimeoutUs))
                    {
                        j.failed = true;
                        j.failureKind = 2;
                        j.step = kClose;
                        break;
                    }
                    return false;
                }
                break;
            case kWrite:
            {
                PrefetchStoreE01(i);   // the idle hook during the BLOCK exchanges
                const int a = AwaitVfs(_vfsResult);
                if (a == 0)
                    return false;
                if (a < 0)
                {
                    j.failed = true;
                    j.failureKind = 1;
                }
                _storSlotActive = false;
                _storLastProgress = Now();
                j.step = j.failed ? kClose : kSend;
                break;
            }
            case kClose:
                if (!j.failed && _storReceived == 0)
                {
                    j.failed = true;
                    j.failureKind = 3;
                }
                CloseData(i);
                RequestVfs(j.failed ? ZiFiVfsBridge::Op::CloseAbort : ZiFiVfsBridge::Op::CloseCommit, {}, 0, 0);
                j.step = kMeta;
                break;
            case kMeta:
            {
                const int a = AwaitVfs(_vfsResult);
                if (a == 0)
                    return false;
                // closeFile(!failed): a failed commit is a failure of its own
                if (a < 0 && !j.failed)
                {
                    j.failed = true;
                    j.failureKind = 4;
                }
                _ramStatAt[1] = _storReceived;
                _ramStatFree[1] = kE01FreeHeap;
                _ramStatCount = 2;
                if (!j.failed)
                {
                    ++_filesReceived;
                    Reply(i, "226 Transfer complete\r\n");
                    return true;
                }
                j.failure = vfs.LastError();
                RequestVfs(ZiFiVfsBridge::Op::Delete, j.path, 0, 0);
                j.step = kDelete;
                break;
            }
            case kDelete:
                if (AwaitVfs(_vfsResult) == 0)
                    return false;
                // The message carries remove()'s own last error, as the firmware prints vfs_.lastError() after it
                Reply(i, Printf("426 Transfer aborted (%s:%s bytes=%lu)\r\n", kKinds[j.failureKind % 5],
                                vfs.LastError().c_str(), static_cast<unsigned long>(_storReceived)));
                DropControl(i);
                return true;
            default: return true;
        }
    }
}

bool ZiFiFtpServer::StepDele(Job& j)
{
    const int i = j.session;
    if (j.step == kBegin)
    {
        if (!NormalizePath(_sessions[i], j.rest, j.path) ||
            !RequestVfs(ZiFiVfsBridge::Op::Delete, j.path, 0, kVfsMutateTimeoutUs))
        {
            Reply(i, "550 Delete failed\r\n");
            return true;
        }
        j.step = kDelete;
    }
    const int a = AwaitVfs(_vfsResult);
    if (a == 0)
        return false;
    Reply(i, a > 0 ? "250 File deleted\r\n" : "550 Delete failed\r\n");
    return true;
}

bool ZiFiFtpServer::StepMkd(Job& j)
{
    const int i = j.session;
    if (j.step == kBegin)
    {
        if (j.rest.empty())
        {
            Reply(i, "501 Missing directory name\r\n");
            return true;
        }
        if (!NormalizePath(_sessions[i], j.rest, j.path) ||
            !RequestVfs(ZiFiVfsBridge::Op::Mkdir, j.path, 0, kVfsMutateTimeoutUs))
        {
            Reply(i, "550 Cannot create directory\r\n");
            return true;
        }
        j.step = kDelete;
    }
    const int a = AwaitVfs(_vfsResult);
    if (a == 0)
        return false;
    Reply(i, a > 0 ? "257 \"" + j.path + "\" created\r\n" : std::string("550 Cannot create directory\r\n"));
    return true;
}

// --- Status --------------------------------------------------------------------------------------------------------

std::vector<ZiFiFtpServer::SessionInfo> ZiFiFtpServer::Sessions() const
{
    std::vector<SessionInfo> out;
    const EspStack& stack = const_cast<ZiFiBridgeHost&>(_host).BridgeStack();
    for (int i = 0; i < MaxSessions(); ++i)
    {
        const Session& s = _sessions[i];
        if (!s.active)
            continue;
        SessionInfo info;
        info.index = i;
        const EspStack::Slot& slot = stack.GetSlot(kControlSlot + i);
        info.remote = NetIpToString(slot.remote.addr) + ":" + std::to_string(slot.remote.port);
        info.loggedIn = s.loggedIn;
        info.cwd = s.cwd;
        info.mode = s.passiveListening ? "passive" : s.activeEndpointSet ? "active" : "";
        out.push_back(info);
    }
    return out;
}

std::string ZiFiFtpServer::JobText() const
{
    static const char* const kKinds[] = {"", "CWD", "SIZE", "LIST", "MLST", "MDTM", "MFMT", "RETR", "STOR", "DELE", "MKD"};
    if (_job.kind == Job::None)
        return {};
    std::string text = std::string(kKinds[_job.kind]) + " " + (_job.path.empty() ? _job.rest : _job.path);
    if (_job.kind == Job::Retr)
        text += " " + std::to_string(_job.sent) + "/" + std::to_string(_job.stat.size);
    if (_job.kind == Job::Stor)
        text += " " + std::to_string(_storReceived) + " received";
    return text;
}

// --- TTD -----------------------------------------------------------------------------------------------------------

namespace
{
constexpr uint8_t kStateVersion = 1;

void SaveResult(ZiFiStateWriter& w, const ZiFiVfsBridge::Result& x)
{
    w.Bool(x.success), w.Bool(x.atEnd), w.Bool(x.wouldBlock), w.Bool(x.isDirectory);
    w.U8(x.status), w.U8(x.appliedAttributes);
    w.U32(x.size);
    w.U16(x.writeDate), w.U16(x.writeTime);
    w.Bool(x.hasMetadata);
    w.U8(x.attributes), w.U8(x.createTenth);
    w.U16(x.createTime), w.U16(x.createDate), w.U16(x.accessDate);
    w.U32(x.transferred);
    w.Str(x.name);
    w.Str(x.error);
}

void LoadResult(ZiFiStateReader& r, ZiFiVfsBridge::Result& x)
{
    x.success = r.Bool(), x.atEnd = r.Bool(), x.wouldBlock = r.Bool(), x.isDirectory = r.Bool();
    x.status = r.U8(), x.appliedAttributes = r.U8();
    x.size = r.U32();
    x.writeDate = r.U16(), x.writeTime = r.U16();
    x.hasMetadata = r.Bool();
    x.attributes = r.U8(), x.createTenth = r.U8();
    x.createTime = r.U16(), x.createDate = r.U16(), x.accessDate = r.U16();
    x.transferred = r.U32();
    x.name = r.Str(256);
    x.error = r.Str(64);
}
}  // namespace

void ZiFiFtpServer::Save(ZiFiStateWriter& w) const
{
    w.U8(kStateVersion);
    w.Bool(_running);
    w.U16(_port);
    w.Str(_user);
    w.Str(_password);
    for (const Session& s : _sessions)
    {
        w.Bool(s.active), w.Bool(s.passiveListening), w.Bool(s.loggedIn), w.Bool(s.userAccepted), w.Bool(s.discardLine),
            w.Bool(s.activeEndpointSet), w.Bool(s.pendingCommand);
        w.U16(s.passivePort), w.U16(s.activePort);
        w.U32(s.activeAddress);
        w.U64(s.lastControlActivity);
        w.Str(s.cwd), w.Str(s.line), w.Str(s.pendingLine);
    }
    w.U8(static_cast<uint8_t>(_vfsOwner + 1));
    const Job& j = _job;
    w.U8(j.kind), w.U8(j.step), w.U8(static_cast<uint8_t>(j.session));
    w.Str(j.path), w.Str(j.rest);
    w.U8(static_cast<uint8_t>(j.format));
    SaveResult(w, j.stat);
    w.U32(j.sent);
    w.Bool(j.failed), w.Bool(j.set);
    w.Str(j.failure);
    w.I64(j.requested);
    w.U16(j.stampDate), w.U16(j.stampTime);
    w.U64(j.vfsDeadline);
    w.U8(j.vfsOp);
    w.U64(j.dataDeadline);
    w.Bool(j.connecting);
    w.U8(j.failureKind);
    w.Str(_lastVfsError);
    SaveResult(w, _vfsResult);
    w.Bool(_storEof), w.Bool(_storError);
    w.U32(_storReceived);
    w.U64(_storLastProgress);
    w.U32(static_cast<uint32_t>(_storSlots.size()));
    for (const std::vector<uint8_t>& slot : _storSlots)
        w.Bytes(slot);
    w.Bool(_storSlotActive);
    w.U8(_ramStatCount);
    w.U32(_ramStatAt[0]), w.U32(_ramStatAt[1]), w.U32(_ramStatFree[0]), w.U32(_ramStatFree[1]);
    w.Str(_lastCommand), w.Str(_lastReply);
    w.U64(_bytesSent), w.U64(_bytesReceived);
    w.U32(_filesSent), w.U32(_filesReceived);
}

bool ZiFiFtpServer::Load(ZiFiStateReader& r)
{
    if (r.U8() != kStateVersion)
        return false;
    _running = r.Bool();
    _port = r.U16();
    _user = r.Str(64);
    _password = r.Str(128);
    for (Session& s : _sessions)
    {
        s.active = r.Bool(), s.passiveListening = r.Bool(), s.loggedIn = r.Bool(), s.userAccepted = r.Bool(),
        s.discardLine = r.Bool(), s.activeEndpointSet = r.Bool(), s.pendingCommand = r.Bool();
        s.passivePort = r.U16(), s.activePort = r.U16();
        s.activeAddress = r.U32();
        s.lastControlActivity = r.U64();
        s.cwd = r.Str(512), s.line = r.Str(1024), s.pendingLine = r.Str(1024);
    }
    _vfsOwner = static_cast<int>(r.U8()) - 1;
    Job& j = _job;
    j.kind = static_cast<Job::Kind>(r.U8()), j.step = r.U8(), j.session = r.U8() % 3;
    j.path = r.Str(512), j.rest = r.Str(1024);
    j.format = static_cast<ListFormat>(r.U8());
    LoadResult(r, j.stat);
    j.sent = r.U32();
    j.failed = r.Bool(), j.set = r.Bool();
    j.failure = r.Str(128);
    j.requested = r.I64();
    j.stampDate = r.U16(), j.stampTime = r.U16();
    j.vfsDeadline = r.U64();
    j.vfsOp = r.U8();
    j.dataDeadline = r.U64();
    j.connecting = r.Bool();
    j.failureKind = r.U8();
    _lastVfsError = r.Str(128);
    LoadResult(r, _vfsResult);
    _storEof = r.Bool(), _storError = r.Bool();
    _storReceived = r.U32();
    _storLastProgress = r.U64();
    _storSlots.clear();
    const uint32_t slots = r.U32();
    for (uint32_t i = 0; i < slots && i < kE01StorSlots && r.Ok(); ++i)
        _storSlots.push_back(r.Bytes(kE01StorSlotSize));
    _storSlotActive = r.Bool();
    _ramStatCount = r.U8();
    _ramStatAt[0] = r.U32(), _ramStatAt[1] = r.U32(), _ramStatFree[0] = r.U32(), _ramStatFree[1] = r.U32();
    _lastCommand = r.Str(64), _lastReply = r.Str(512);
    _bytesSent = r.U64(), _bytesReceived = r.U64();
    _filesSent = r.U32(), _filesReceived = r.U32();
    return r.Ok();
}
