#include "stdafx.h"

#include "vdac4espfirmware.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "esp/espelfloader.h"
#include "vdac4espframes.h"
#include "vdac4esplink.h"
#include "vdac4espstate.h"
#include "vdac4mediarange.h"

// Port of VDAC4 Firmware src/main.cpp (Application) and src/apps/module_app.cpp (ModuleApp)
// (MIT, andrewinsidelazarev); see the header.

namespace
{
// protocol.hpp
enum : uint8_t
{
    kEcho = 0x00,
    kWifiConnect = 0x01,
    kSysInfo = 0x02,
    kWifiIni = 0x03,
    kPing = 0x04,
    kGetStep = 0x05,
    kFtpStart = 0x06,
    kFtpStop = 0x07,
    kUpdateStart = 0x08,
    kUpdateStop = 0x09,
    kFtpRamStats = 0x0A,
    kSmbStart = 0x0B,
    kSmbStop = 0x0C,
    kOnlineUpdate = 0x0D,
    kOnlineUpdateCheck = 0x0E,
    kSysReset = 0x0F,
    kNetOpen = 0x10,
    kNetSend = 0x11,
    kNetRecv = 0x12,
    kNetClose = 0x13,
    kNetHttpGet = 0x14,
    kVdiskInfo = 0x15,
    kVdiskRead = 0x16,
    kVdiskReset = 0x17,
    kVdiskWrite = 0x18,
    kVdiskMode = 0x19,
    kVupStart = 0x1A,
    kVupSync = 0x1D,
    kVdiskReadN = 0x1E,
    kNetPing = 0x20,
    kNetIpConfig = 0x21,
    kNetNtp = 0x22,
    kNetProxyStatus = 0x23,
    kWeatherGet = 0x24,
    kWcuStart = 0x25,
    kWcuSync = 0x28,
    kSysClock = 0x29,
    kPathGet = 0x2A,
    kPathSet = 0x2B,
    kCoreList = 0x2C,
    kCoreApp = 0x2D,
    kLinkProbe = 0x3E,
    kTangRead = 0x45,
    kTangWrite = 0x46,
    kModQuery = 0x69,
    kModPut = 0x6A,
    kModCommit = 0x6B,
    kAppStart = 0x6C,
    kAppStop = 0x6D,

    kRespWifiConnect = 0x81,
    kRespSysInfo = 0x82,
    kRespWifiIni = 0x83,
    kRespGetStep = 0x85,
    kRespFtpStart = 0x86,
    kRespFtpStop = 0x87,
    kRespUpdateStart = 0x88,
    kRespUpdateStop = 0x89,
    kRespFtpRamStats = 0x8A,
    kRespSmbStart = 0x8B,
    kRespSmbStop = 0x8C,
    kRespOnlineUpdate = 0x8D,
    kRespOnlineUpdateCheck = 0x8E,
    kRespNetOpen = 0x90,
    kRespNetSend = 0x91,
    kRespNetRecv = 0x92,
    kRespNetClose = 0x93,
    kRespNetHttpGet = 0x94,
    kRespVdiskInfo = 0x95,
    kRespVdiskRead = 0x96,
    kRespVdiskReset = 0x97,
    kRespVdiskWrite = 0x98,
    kRespVdiskMode = 0x99,
    kRespVupStart = 0x9A,
    kRespVdiskData = 0x9E,
    kRespNetIpConfig = 0xA0,
    kRespNetPing = 0xA1,
    kRespNetNtp = 0xA2,
    kRespNetProxyStatus = 0xA3,
    kRespWeatherGet = 0xA4,
    kRespWcuStart = 0xA5,
    kRespSysClock = 0xA9,
    kRespPathGet = 0xAA,
    kRespPathSet = 0xAB,
    kRespCoreList = 0xAC,
    kRespCoreApp = 0xAD,
    kRespLinkProbe = 0xBE,
    kRespTangRead = 0xC5,
    kRespTangWrite = 0xC6,
    kRespModQuery = 0xE9,
    kRespModPut = 0xEA,
    kRespModCommit = 0xEB,
    kRespAppStart = 0xEC,
    kRespAppStop = 0xED,
    kReady = 0xF0,
};

// tang_link.hpp registers the Z80 may write (TANG_WRITE)
constexpr uint32_t kRegScratch = 0x3C;
constexpr uint32_t kRegCtrl = 0x40;
constexpr uint32_t kRegAdjust = 0x44;
constexpr uint32_t kRegUiKey = 0x50;
constexpr uint32_t kRegAdjTable = 0x60;

// vdisk_service.hpp Status
constexpr uint8_t kVdiskNoCard = 4;

const char* const kNoNetwork = "network not available";

uint32_t ReadLe32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void WriteLe32(uint8_t* p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
}

void WriteLe16(uint8_t* p, uint16_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
}

void Upper(std::string& text)
{
    for (char& c : text)
    {
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    }
}

/// strnlen over a packet: the name at `at` up to its zero or the end of the data
size_t NameLength(const EspPacket& packet, size_t at)
{
    if (packet.length <= at)
        return 0;
    const void* zero = std::memchr(packet.data + at, 0, packet.length - at);
    return zero != nullptr ? static_cast<size_t>(static_cast<const uint8_t*>(zero) - (packet.data + at))
                           : packet.length - at;
}

bool TangSettingRegister(uint32_t address)
{
    return address == kRegScratch || address == kRegCtrl || address == kRegAdjust || address == kRegUiKey ||
           (address >= kRegAdjTable && address < kRegAdjTable + 32);
}
}  // namespace

Vdac4EspFirmware::Vdac4EspFirmware(EspFrameTransport& transport, EspSpiLink& link, Vdac4MediaRange& media,
                                   std::filesystem::path cacheDirectory, size_t cacheCapacity)
    : _transport(transport), _link(link), _media(media), _directory(std::move(cacheDirectory)),
      _cache(_directory / "zm", cacheCapacity)
{
}

Vdac4EspFirmware::~Vdac4EspFirmware()
{
    Shutdown();
}

void Vdac4EspFirmware::Boot(uint32_t nowMs)
{
    Shutdown();
    _nowMs = nowMs;
    _cache.Load();
}

void Vdac4EspFirmware::Shutdown()
{
    StopApp();
    DropDelivery();
    _resources.clear();
    _media.Clear();
    _lastStep = 0;
    ClearError();
    _resetRequested = false;
    _clockSet = false;
    std::memset(_clock, 0, sizeof(_clock));
    _appStale = false;
    _inCommand = false;
    _appDone = false;
    _ftDetachState = 0;
}

void Vdac4EspFirmware::Pass(uint32_t nowMs)
{
    _nowMs = nowMs;
    if (_resetRequested)
        return;
    // pollCore1 and the network core's poll: once per pass
    PollFtDetach();
    if (_appRunning && _runtime != nullptr)
        _runtime->Poll(nowMs);
    for (size_t pass = 0; pass < kMaxPacketsPerPass && !_resetRequested; ++pass)
    {
        _transport.Drain(nowMs);
        if (_transport.Blocked())
            return;  // the firmware's loop sits inside send()
        _transport.SendNextEvent(nowMs);
        if (_transport.Blocked())
            return;
        EspPacket packet;
        if (!_transport.Poll(packet, nowMs))
            return;
        Handle(packet);
    }
}

void Vdac4EspFirmware::Send(uint8_t command, const uint8_t* data, size_t length)
{
    _transport.Send(command, data, length, _nowMs);
}

void Vdac4EspFirmware::ReportError(const char* text)
{
    std::snprintf(_lastError, sizeof(_lastError), "%s", text);
    _transport.SendError(_lastError, _nowMs);
}

// --- Application::handle ---------------------------------------------------------------------------------------

void Vdac4EspFirmware::Handle(const EspPacket& packet)
{
    if (_packetObserver)
        _packetObserver(packet.command, packet.length, _nowMs);
    if (packet.command != kGetStep)
        _lastStep = packet.command;
    switch (packet.command)
    {
        case kEcho:
            Send(kEcho, packet.data, packet.length);
            ClearError();
            return;
        case kPing:
            Send(kReady);
            return;
        case kGetStep: {
            uint8_t reply[1 + sizeof(_lastError)];
            reply[0] = _lastStep;
            const size_t length = std::strlen(_lastError);
            std::memcpy(reply + 1, _lastError, length);
            Send(kRespGetStep, reply, length + 1);
            return;
        }
        case kSysInfo:
            HandleSysInfo();
            return;
        case kSysReset:
            // handleReset: ACK, flush, 100 ms, restart - the waits are Vdac4Esp's
            _transport.SendAck(_nowMs);
            _resetRequested = true;
            return;
        case kSysClock:
            HandleSysClock(packet);
            return;
        case kPathGet:
            HandlePathGet(packet);
            return;
        case kPathSet:
            HandlePathSet(packet);
            return;
        case kTangRead:
            HandleTangRead(packet);
            return;
        case kTangWrite:
            HandleTangWrite(packet);
            return;
        case kLinkProbe:
            HandleLinkProbe(packet);
            return;
        case kVdiskInfo:
        case kVdiskRead:
        case kVdiskReset:
        case kVdiskWrite:
        case kVdiskMode:
        case kVdiskReadN:
            HandleVdisk(packet.command);
            return;
        case kWifiConnect:
        case kWifiIni:
        case kFtpStart:
        case kFtpStop:
        case kFtpRamStats:
        case kSmbStart:
        case kSmbStop:
        case kUpdateStart:
        case kUpdateStop:
        case kOnlineUpdateCheck:
        case kOnlineUpdate:
        case kNetOpen:
        case kNetSend:
        case kNetRecv:
        case kNetClose:
        case kNetHttpGet:
        case kNetPing:
        case kNetIpConfig:
        case kNetProxyStatus:
            HandleNetwork(packet.command);
            return;
        default:
            break;
    }
    if (!DispatchToApps(packet))
    {
        char text[24];
        std::snprintf(text, sizeof(text), "unsupported:%02X", packet.command);
        ReportError(text);
    }
}

void Vdac4EspFirmware::HandleNetwork(uint8_t command)
{
    // The network task is not emulated: every request takes the firmware's "could not submit" path
    if (command != kNetRecv)
        _transport.SendAck(_nowMs);  // NET_RECV answers at once, without ACK
    ReportError(kNoNetwork);
    uint8_t failed[16] = {};
    switch (command)
    {
        case kWifiConnect:
            Send(kRespWifiConnect, failed, 5);
            break;
        case kWifiIni:
            Send(kRespWifiIni, failed, 5);
            break;
        case kFtpStart:
            Send(kRespFtpStart, failed, 3);
            break;
        case kFtpStop:
            Send(kRespFtpStop, failed, 1);
            break;
        case kFtpRamStats:
            Send(kRespFtpRamStats);
            break;
        case kSmbStart:
            Send(kRespSmbStart, failed, 4);
            break;
        case kSmbStop:
            Send(kRespSmbStop, failed, 1);
            break;
        case kUpdateStart:
            Send(kRespUpdateStart, failed, 7);
            break;
        case kUpdateStop:
            Send(kRespUpdateStop, failed, 1);
            break;
        case kOnlineUpdateCheck:
            Send(kRespOnlineUpdateCheck, failed, 1);
            break;
        case kOnlineUpdate:
            Send(kRespOnlineUpdate, failed, 1);
            break;
        case kNetOpen:
            Send(kRespNetOpen, failed, 1);
            break;
        case kNetSend:
            Send(kRespNetSend, failed, 1);
            break;
        case kNetRecv:
            failed[0] = 1;
            Send(kRespNetRecv, failed, 1);
            break;
        case kNetClose:
            Send(kRespNetClose, failed, 1);
            break;
        case kNetHttpGet:
            Send(kRespNetHttpGet, failed, 7);
            break;
        case kNetPing:
            Send(kRespNetPing, failed, 3);
            break;
        case kNetIpConfig:
            Send(kRespNetIpConfig, failed, 16);
            break;
        case kNetProxyStatus:
            Send(kRespNetProxyStatus, failed, 1);
            break;
        default:
            break;
    }
}

void Vdac4EspFirmware::HandleVdisk(uint8_t command)
{
    // vdisk_service with neither a VFS volume nor a Tang card behind it
    uint8_t reply[11] = {kVdiskNoCard};
    switch (command)
    {
        case kVdiskInfo:
            Send(kRespVdiskInfo, reply, 11);  // [status][flags][generation LE32][sectors LE32][sources]
            break;
        case kVdiskRead:
            Send(kRespVdiskRead, reply, 1);
            break;
        case kVdiskReset:
            Send(kRespVdiskReset, reply, 5);
            break;
        case kVdiskWrite:
            Send(kRespVdiskWrite, reply, 1);
            break;
        case kVdiskMode:
            Send(kRespVdiskMode, reply, 6);
            break;
        default:
            Send(kRespVdiskData, reply, 1);  // READN: a one-byte frame ends the series with its status
            break;
    }
}

bool Vdac4EspFirmware::DispatchToApps(const EspPacket& packet)
{
    switch (packet.command)
    {
        case kModQuery:
            HandleQuery(packet);
            return true;
        case kModPut:
            HandlePut(packet);
            return true;
        case kModCommit:
            HandleCommit();
            return true;
        case kAppStart:
            HandleStart(packet);
            return true;
        case kCoreList: {
            // The firmware built without the LINK: there is no Tang SD to list cores from
            static const uint8_t reply[] = "\0no LINK to the Tang";
            Send(kRespCoreList, reply, sizeof(reply));
            return true;
        }
        case kCoreApp: {
            static const uint8_t reply[] = "\xFF\0no LINK to the Tang";  // replyApp(ZM_ERROR, text)
            Send(kRespCoreApp, reply, sizeof(reply));
            return true;
        }
        case kAppStop: {
            StopApp();
            const uint8_t ok = 1;
            Send(kRespAppStop, &ok, 1);
            return true;
        }
        default:
            break;
    }
    // Apps that take network commands (VUP, WCU, NTP, weather): the network task cannot take them
    if ((packet.command >= kVupStart && packet.command <= kVupSync) || packet.command == kNetNtp ||
        (packet.command >= kWeatherGet && packet.command <= kWcuSync))
    {
        _transport.SendAck(_nowMs);
        ReportError(kNoNetwork);
        uint8_t response = 0;
        if (packet.command <= kVupSync)
            response = static_cast<uint8_t>(kRespVupStart + (packet.command - kVupStart));
        else if (packet.command == kNetNtp)
            response = kRespNetNtp;
        else if (packet.command == kWeatherGet)
            response = kRespWeatherGet;
        else
            response = static_cast<uint8_t>(kRespWcuStart + (packet.command - kWcuStart));
        const uint8_t failed = 0;
        Send(response, &failed, 1);
        return true;
    }
    // ModuleApp::handleCommand: the running app's range
    if (!_appRunning || _runtime == nullptr || packet.command < _appInfo.commandFirst ||
        packet.command > _appInfo.commandLast)
        return false;
    _appCommandMs = _nowMs;
    _inCommand = true;
    const int result = _runtime->Command(packet.command, packet.data, packet.length);
    _inCommand = false;
    if (_appDone)
    {
        _appDone = false;
        StopApp();
    }
    return result == kZmOk;
}

void Vdac4EspFirmware::HandleSysInfo()
{
    _transport.SendAck(_nowMs);
    const EspSpiLinkCore::Stats& link = _link.GetStats();
    char tang[24] = "none";
    uint32_t version = 0;
    if (_tang != nullptr && _tang->LinkRead(4, version))
        std::snprintf(tang, sizeof(tang), "VDC4/%u.%u@40MHz", static_cast<unsigned>((version >> 8) & 0xFF),
                      static_cast<unsigned>(version & 0xFF));
    char extra[160];
    std::snprintf(extra, sizeof(extra), "BOARD:VDAC4-A3.1 IDF:%s LINK:%u/%u/%u/%u/%u/%u/%u AUDIO:off TANG:%s ",
                  kVersion, static_cast<unsigned>(link.turns), static_cast<unsigned>(link.repeats),
                  static_cast<unsigned>(link.badRequests), static_cast<unsigned>(link.bulkBlocks),
                  static_cast<unsigned>(link.bulkBad), static_cast<unsigned>(link.putBlocks),
                  static_cast<unsigned>(link.putBad), tang);
    char text[EspFrameParser::kMaxPayload];
    const size_t psramFree = kPsramBytes - std::min(kPsramBytes, PsramUsed());
    const int length = std::snprintf(
        text, sizeof(text),
        "RAM:%uB PSRAM:%u/%uB Flash:%uB Sketch:%uB RST:%u CORE:%s+VFS1/NET+EVT0 NETSTK:%uB CTRL:%s "
        "VFSBUF:%s:%u+%u %sPROXY:%s FW:%s",
        180u * 1024u, static_cast<unsigned>(psramFree), 8u * 1024u * 1024u, 8u * 1024u * 1024u, 0x180000u, 1u,
        "SPI", 0u, "PSRAM", "PSRAM", 16384u, 16384u, extra, "OFF", kVersion);
    const size_t used = length <= 0 ? 0 : std::min(static_cast<size_t>(length), sizeof(text) - 1);
    Send(kRespSysInfo, reinterpret_cast<const uint8_t*>(text), used);
    _lastStep = 8;  // native plugins read step 8 as "SYS_INFO received"
    ClearError();
}

void Vdac4EspFirmware::HandleSysClock(const EspPacket& packet)
{
    uint8_t ok = 0;
    if (packet.length >= 7)
    {
        const uint8_t* d = packet.data;
        const unsigned year = static_cast<unsigned>(d[0] | (d[1] << 8));
        const uint32_t date = year * 10000u + d[2] * 100u + d[3];
        const bool valid = year <= 2099 && d[2] >= 1 && d[2] <= 12 && d[3] >= 1 && d[3] <= 31 && d[4] <= 23 &&
                           d[5] <= 59 && d[6] <= 59 && date > 20261001u;
        if (valid)
        {
            std::memcpy(_clock, d, sizeof(_clock));
            _clockSet = true;
            ok = 1;
        }
    }
    Send(kRespSysClock, &ok, 1);
}

void Vdac4EspFirmware::HandlePathGet(const EspPacket& packet)
{
    uint8_t reply[5] = {};
    char name[13];
    const size_t nameLength = NameLength(packet, 4);
    if (packet.length >= 6 &&
        EspPathTable::NormalizeName(reinterpret_cast<const char*>(packet.data + 4), nameLength, name))
    {
        const uint32_t serial = ReadLe32(packet.data);
        uint32_t cluster = 0;
        EspPathTable table;
        table.LoadFile(_directory);
        if (table.Find(name, serial, cluster))
        {
            reply[0] = 1;
            WriteLe32(reply + 1, cluster);
        }
    }
    Send(kRespPathGet, reply, sizeof(reply));
}

void Vdac4EspFirmware::HandlePathSet(const EspPacket& packet)
{
    uint8_t ok = 0;
    char name[13];
    const size_t nameLength = NameLength(packet, 8);
    if (packet.length >= 10 &&
        EspPathTable::NormalizeName(reinterpret_cast<const char*>(packet.data + 8), nameLength, name))
    {
        EspPathTable table;
        table.LoadFile(_directory);
        ok = table.Set(name, ReadLe32(packet.data), ReadLe32(packet.data + 4)) && table.SaveFile(_directory) ? 1 : 0;
    }
    Send(kRespPathSet, &ok, 1);
}

void Vdac4EspFirmware::HandleTangRead(const EspPacket& packet)
{
    uint8_t reply[1 + 16 * 4] = {};
    size_t count = packet.length >= 3 ? packet.data[2] : 0;
    const uint32_t address = packet.length >= 2 ? static_cast<uint32_t>(packet.data[0] | (packet.data[1] << 8)) : 0;
    bool ok = _tang != nullptr && count >= 1 && count <= 16 && (address & 3) == 0 && address + count * 4 <= 0x100;
    uint32_t words[16] = {};
    for (size_t i = 0; ok && i < count; ++i)
        ok = _tang->LinkRead(static_cast<uint32_t>(address + i * 4), words[i]);
    if (!ok)
        count = 0;
    reply[0] = ok ? 1 : 0;
    for (size_t i = 0; i < count; ++i)
        WriteLe32(reply + 1 + i * 4, words[i]);
    Send(kRespTangRead, reply, 1 + count * 4);
}

void Vdac4EspFirmware::HandleTangWrite(const EspPacket& packet)
{
    const size_t count = packet.length >= 3 ? packet.data[2] : 0;
    const uint32_t address = packet.length >= 2 ? static_cast<uint32_t>(packet.data[0] | (packet.data[1] << 8)) : 0;
    bool ok = _tang != nullptr && count >= 1 && count <= 16 && (address & 3) == 0 && packet.length == 3 + count * 4;
    uint32_t words[16] = {};
    for (size_t i = 0; ok && i < count; ++i)
    {
        ok = TangSettingRegister(static_cast<uint32_t>(address + i * 4));
        words[i] = ReadLe32(packet.data + 3 + i * 4);
        if (address + i * 4 == kRegCtrl)
            words[i] &= ~(1U << 9);  // CTRL bit 9 starts the SD READ calibration: never from a plugin
    }
    for (size_t i = 0; ok && i < count; ++i)
        ok = _tang->LinkWrite(static_cast<uint32_t>(address + i * 4), words[i]);
    const uint8_t reply = ok ? 1 : 0;
    Send(kRespTangWrite, &reply, 1);
}

void Vdac4EspFirmware::HandleLinkProbe(const EspPacket& packet)
{
    EspSpiLink::DmaProbeResult probe;
    const uint8_t action = packet.length >= 1 ? packet.data[0] : 0;
    if (packet.length >= 4)
        probe = _link.RunDmaProbe(action, static_cast<size_t>(packet.data[1] | (packet.data[2] << 8)), packet.data[3]);
    uint8_t reply[8];
    reply[0] = probe.status;
    reply[1] = action;
    WriteLe16(reply + 2, probe.length);
    WriteLe16(reply + 4, probe.errors);
    WriteLe16(reply + 6, probe.firstBad);
    Send(kRespLinkProbe, reply, sizeof(reply));
}

// --- ModuleApp -------------------------------------------------------------------------------------------------

size_t Vdac4EspFirmware::PsramUsed() const
{
    size_t used = _deliveryActive ? _delivery.size() : 0;
    for (const HeldResource& item : _resources)
        used += item.data.size();
    return used;
}

size_t Vdac4EspFirmware::ResourceCount() const
{
    return _resources.size();
}

void Vdac4EspFirmware::ReplyQuery(uint8_t state)
{
    uint8_t reply[5];
    reply[0] = state;
    WriteLe32(reply + 1, static_cast<uint32_t>(_cache.FreeBytes()));
    Send(kRespModQuery, reply, sizeof(reply));
}

// [flags][size LE32][FAT time LE32][name, 0] -> [0 have / 1 send / 2 no room / 3 bad name][free LE32]
void Vdac4EspFirmware::HandleQuery(const EspPacket& packet)
{
    DropDelivery();
    if (packet.length < 10)
    {
        ReplyQuery(3);
        return;
    }
    const bool resource = (packet.data[0] & 0x01) != 0;
    const uint32_t size = ReadLe32(packet.data + 1);
    const uint32_t stamp = ReadLe32(packet.data + 5);
    const size_t nameLength = NameLength(packet, 9);
    if (nameLength == 0 || nameLength >= 13)
    {
        ReplyQuery(3);
        return;
    }
    std::string name(reinterpret_cast<const char*>(packet.data + 9), nameLength);
    Upper(name);
    if (!EspModuleCache::ValidName(name.c_str()) || size == 0)
    {
        ReplyQuery(3);
        return;
    }
    if (resource)
    {
        for (const HeldResource& item : _resources)
        {
            if (item.name == name && item.data.size() == size && item.stamp == stamp)
            {
                ReplyQuery(0);
                return;
            }
        }
    }
    else if (_cache.Has(name.c_str(), size, stamp))
    {
        ReplyQuery(0);
        return;
    }
    // An app nobody sends commands to (its Z80 program left without APP_STOP) does not hold the cache
    if (!resource && size <= kMaxModuleBytes && _appRunning && !_cache.CanFit(size, _appModule.c_str()) &&
        _cache.CanFit(size, nullptr) && _nowMs - _appCommandMs >= kAbandonedMs)
        StopApp();
    if (size > (resource ? kMaxResourceBytes : kMaxModuleBytes) ||
        (!resource && !_cache.CanFit(size, _appRunning ? _appModule.c_str() : nullptr)))
    {
        ReplyQuery(2);
        return;
    }
    // A running app may read the old resource of this name: stop it first, start it again after
    std::string restart;
    if (resource)
    {
        const bool held = std::any_of(_resources.begin(), _resources.end(),
                                      [&name](const HeldResource& item) { return item.name == name; });
        if (held && _appRunning)
        {
            restart = _appModule;
            StopApp();
        }
        ReleaseResource(name.c_str());
    }
    const bool allocated = AllocateDelivery(size, resource);
    if (!restart.empty())
        StartApp(restart);
    if (!allocated)
    {
        ReplyQuery(2);
        return;
    }
    _deliveryName = name;
    _deliverySize = size;
    _deliveryStamp = stamp;
    _deliveryReceived = 0;
    _deliveryResource = resource;
    ReplyQuery(1);
}

bool Vdac4EspFirmware::AllocateDelivery(uint32_t size, bool resource)
{
    auto fits = [this, size]() { return PsramUsed() + size <= kPsramBytes; };
    if (!fits())
    {
        // Room in PSRAM, in order: the app goes, then resources, largest first
        std::string stopped;
        if (_appRunning)
        {
            stopped = _appModule;
            StopApp();
        }
        while (!fits() && !_resources.empty())
        {
            auto largest = std::max_element(
                _resources.begin(), _resources.end(),
                [](const HeldResource& a, const HeldResource& b) { return a.data.size() < b.data.size(); });
            _resources.erase(largest);
        }
        if (resource && !stopped.empty())
            StartApp(stopped);
        if (!fits())
            return false;
    }
    _delivery.assign(size, 0);
    _deliveryActive = true;
    return true;
}

// [offset LE32][data] -> [1 taken / 0 wrong place][received LE32]
void Vdac4EspFirmware::HandlePut(const EspPacket& packet)
{
    uint8_t reply[5] = {};
    if (_deliveryActive && packet.length >= 4)
    {
        const uint32_t offset = ReadLe32(packet.data);
        const uint32_t length = packet.length - 4u;
        if (offset == _deliveryReceived && length <= _deliverySize - _deliveryReceived)
        {
            std::memcpy(_delivery.data() + offset, packet.data + 4, length);
            _deliveryReceived += length;
            reply[0] = 1;
        }
    }
    WriteLe32(reply + 1, _deliveryReceived);
    Send(kRespModPut, reply, sizeof(reply));
}

// -> [1 stored / 0 not]
void Vdac4EspFirmware::HandleCommit()
{
    uint8_t ok = 0;
    if (_deliveryActive && _deliveryReceived == _deliverySize)
    {
        if (_deliveryResource)
        {
            if (_resources.size() < kMaxResources)
            {
                HeldResource item;
                item.name = _deliveryName;
                item.stamp = _deliveryStamp;
                item.data = std::move(_delivery);
                _resources.push_back(std::move(item));
                ok = 1;
            }
        }
        else if (vdac4::EspElfImageProblem(_delivery.data(), _deliverySize) == nullptr)
        {
            ok = _cache.Store(_deliveryName.c_str(), _deliveryStamp, _delivery.data(), _deliverySize,
                              _appRunning ? _appModule.c_str() : nullptr)
                     ? 1
                     : 0;
            // A new copy of the app or of a module it loaded: the next APP_START restarts it
            if (ok != 0 && ModuleInUse(_deliveryName))
                _appStale = true;
        }
    }
    DropDelivery();
    Send(kRespModCommit, &ok, 1);
}

void Vdac4EspFirmware::DropDelivery()
{
    _deliveryActive = false;
    _delivery.clear();
    _delivery.shrink_to_fit();
    _deliveryName.clear();
    _deliverySize = 0;
    _deliveryReceived = 0;
}

// [name, 0] -> [result int8][app name, 0][version, 0]
void Vdac4EspFirmware::HandleStart(const EspPacket& packet)
{
    const size_t length = NameLength(packet, 0);
    int result = kZmError;
    if (length > 0 && length < 13)
    {
        std::string name(reinterpret_cast<const char*>(packet.data), length);
        Upper(name);
        result = StartApp(name);
    }
    uint8_t reply[64];
    size_t used = 0;
    reply[used++] = static_cast<uint8_t>(static_cast<int8_t>(result));
    const std::string appName = _appRunning ? _appInfo.name.substr(0, 24) : std::string();
    const std::string version = _appRunning ? _appInfo.version.substr(0, 24) : std::string();
    std::memcpy(reply + used, appName.data(), appName.size());
    used += appName.size();
    reply[used++] = 0;
    std::memcpy(reply + used, version.data(), version.size());
    used += version.size();
    reply[used++] = 0;
    Send(kRespAppStart, reply, used);
}

bool Vdac4EspFirmware::ModuleInUse(const std::string& name) const
{
    if (_appRunning && _appModule == name)
        return true;
    return _appRunning && _runtime != nullptr && _runtime->UsesModule(name);
}

int Vdac4EspFirmware::StartApp(const std::string& name)
{
    // The plugin sends APP_START before every file: the same app, nothing of it changed - no restart
    if (_appRunning && !_appStale && _appModule == name)
        return kZmOk;
    StopApp();
    _appStale = false;
    const EspModuleCacheEntry* entry = _cache.Find(name.c_str());
    if (entry == nullptr)
        return kZmNotFound;
    std::vector<uint8_t> file(entry->size);
    if (!_cache.Read(name.c_str(), file.data(), static_cast<uint32_t>(file.size())))
        return kZmNotFound;
    // Again after reading it back: the image in flash may have gone bad
    if (vdac4::EspElfImageProblem(file.data(), file.size()) != nullptr)
        return kZmError;
    if (_runtime == nullptr)
        return kZmError;  // nothing here can run the image
    _appRunning = true;   // app_ is set before start(): link_media inside start() works
    _appModule = name;
    _appInfo = Vdac4AppInfo{};
    // The runtime fills the info first (zm_app_t is static on the board), so link_media works inside start()
    const int result = _runtime->StartApp(name, file, *this, _appInfo);
    if (result != kZmOk)
    {
        StopApp();
        return result;
    }
    _appCommandMs = _nowMs;
    return kZmOk;
}

void Vdac4EspFirmware::StopApp()
{
    if (!_appRunning)
        return;
    _appRunning = false;
    _media.Clear();  // raw media DMA for no command any more
    if (_runtime != nullptr)
        _runtime->StopApp();
    _ftDetachState = 0;
    _appModule.clear();
    _appInfo = Vdac4AppInfo{};
}

void Vdac4EspFirmware::PollFtDetach()
{
    // FtDetachGuard: the app took the FT812 from the Evo; the Tang went back to the TS-Conf personality with the
    // FT not selected (M_SEL=0) - the app is unloaded
    if (!_appRunning || _tang == nullptr)
        return;
    const uint32_t state = _ftDetachState;
    if ((state & 1) == 0 || static_cast<uint32_t>(_nowMs - (state & ~3u)) < kFtDetachPeriodMs)
        return;
    _ftDetachState = (state & 3u) | (_nowMs & ~3u);
    uint32_t id = 0;
    uint32_t version = 0;
    uint32_t status = 0;
    if (!_tang->LinkRead(0, id) || !_tang->LinkRead(4, version) || !_tang->LinkRead(8, status))
        return;
    if (id != 0x56444334u || ((version >> 16) & 255) != 1)
        return;
    const unsigned personality = version >> 24;
    const bool selected = (status & (1u << 12)) != 0;
    if (personality == 2 && selected)
        _ftDetachState |= 2;
    if ((_ftDetachState & 2) != 0 && personality == 1 && !selected)
        StopApp();
}

// --- IVdac4AppHost (zm_core_t) ---------------------------------------------------------------------------------

bool Vdac4EspFirmware::SendFrame(uint8_t command, const uint8_t* data, size_t size)
{
    if (!_link.Ready())
        return false;
    return _transport.Send(command, data, size, _nowMs);
}

int Vdac4EspFirmware::SendEvent(uint8_t code, const uint8_t* data, size_t size)
{
    return _transport.QueueEvent(code, data, size) ? kZmOk : kZmBusy;
}

size_t Vdac4EspFirmware::EventQueueRoom() const
{
    const size_t queued = _transport.QueuedEvents();
    return queued < EspFrameTransport::kEventQueueDepth ? EspFrameTransport::kEventQueueDepth - queued : 0;
}

int Vdac4EspFirmware::Reply(uint8_t response, const uint8_t* data, size_t size)
{
    if (!_inCommand)
        return kZmError;  // a reply only to the current command
    return _transport.Send(response, data, size, _nowMs) ? kZmOk : kZmError;
}

int Vdac4EspFirmware::SetMediaRange(uint8_t first, uint8_t last)
{
    if (first > last)
    {
        _media.Clear();
        return kZmOk;
    }
    if (!_appRunning || first < _appInfo.commandFirst || last > _appInfo.commandLast)
        return kZmError;
    _media.Set(first, last);
    return kZmOk;
}

void Vdac4EspFirmware::AppDone()
{
    if (_inCommand)
        _appDone = true;
}

int Vdac4EspFirmware::Resource(const char* name, const uint8_t*& data, size_t& size)
{
    for (const HeldResource& item : _resources)
    {
        if (name != nullptr && item.name == name)
        {
            data = item.data.data();
            size = item.data.size();
            return kZmOk;
        }
    }
    return kZmNotFound;
}

void Vdac4EspFirmware::ReleaseResource(const char* name)
{
    _resources.erase(std::remove_if(_resources.begin(), _resources.end(),
                                    [name](const HeldResource& item) { return name == nullptr || item.name == name; }),
                     _resources.end());
}

// The ESP runtime's worker thread calls this (module_open inside a job): one locked step of the cache
int Vdac4EspFirmware::ReadModule(const char* name, std::vector<uint8_t>& out)
{
    if (!_cache.ReadModule(name, out))
    {
        out.clear();
        return kZmNotFound;
    }
    return vdac4::EspElfImageProblem(out.data(), out.size()) == nullptr ? kZmOk : kZmError;
}

void Vdac4EspFirmware::FtOpened(bool selected)
{
    _ftDetachState |= selected ? 3u : 1u;
}

// --- State -----------------------------------------------------------------------------------------------------

void Vdac4EspFirmware::Save(Vdac4EspStateWriter& out) const
{
    out.U32(_nowMs);
    out.U8(_lastStep);
    out.String(_lastError);
    out.Bool(_resetRequested);
    out.Bytes(_clock, sizeof(_clock));
    out.Bool(_clockSet);
    out.U8(_media.First());
    out.U8(_media.Last());
    // Delivery: only what came so far
    out.Bool(_deliveryActive);
    out.String(_deliveryName);
    out.U32(_deliverySize);
    out.U32(_deliveryStamp);
    out.U32(_deliveryReceived);
    out.Bool(_deliveryResource);
    out.Blob(_delivery.data(), _deliveryActive ? _deliveryReceived : 0);
    out.U8(static_cast<uint8_t>(_resources.size()));
    for (const HeldResource& item : _resources)
    {
        out.String(item.name);
        out.U32(item.stamp);
        out.Blob(item.data);
    }
    out.Bool(_appRunning);
    out.Bool(_appStale);
    out.U32(_appCommandMs);
    out.String(_appModule);
    out.String(_appInfo.name);
    out.String(_appInfo.version);
    out.U8(_appInfo.commandFirst);
    out.U8(_appInfo.commandLast);
    out.U32(_ftDetachState);
    std::vector<uint8_t> appState;
    if (_appRunning && _runtime != nullptr)
        _runtime->SaveState(appState);
    out.Blob(appState);
}

bool Vdac4EspFirmware::Load(Vdac4EspStateReader& in)
{
    // The firmware's view of the app only: the runtime keeps its own state (the Xtensa host is restored apart,
    // after this), so it is not stopped (no app->stop, no LINK writes to a Tang that is restored already)
    _appRunning = false;
    _media.Clear();
    _ftDetachState = 0;
    _appModule.clear();
    _appInfo = Vdac4AppInfo{};
    DropDelivery();
    _resources.clear();
    _nowMs = in.U32();
    _lastStep = in.U8();
    std::string error;
    in.String(error, sizeof(_lastError) - 1);
    std::snprintf(_lastError, sizeof(_lastError), "%s", error.c_str());
    _resetRequested = in.Bool();
    in.Bytes(_clock, sizeof(_clock));
    _clockSet = in.Bool();
    const uint8_t mediaFirst = in.U8();
    const uint8_t mediaLast = in.U8();
    _deliveryActive = in.Bool();
    in.String(_deliveryName, 12);
    _deliverySize = in.U32();
    _deliveryStamp = in.U32();
    _deliveryReceived = in.U32();
    _deliveryResource = in.Bool();
    std::vector<uint8_t> received;
    if (!in.Blob(received, kMaxResourceBytes) || received.size() != (_deliveryActive ? _deliveryReceived : 0) ||
        _deliverySize > kMaxResourceBytes || _deliveryReceived > _deliverySize)
        return false;
    if (_deliveryActive)
    {
        _delivery.assign(_deliverySize, 0);
        std::copy(received.begin(), received.end(), _delivery.begin());
    }
    const uint8_t resources = in.U8();
    if (resources > kMaxResources)
        return false;
    for (uint8_t i = 0; i < resources; ++i)
    {
        HeldResource item;
        in.String(item.name, 12);
        item.stamp = in.U32();
        if (!in.Blob(item.data, kMaxResourceBytes))
            return false;
        _resources.push_back(std::move(item));
    }
    if (PsramUsed() > kPsramBytes)
        return false;
    const bool appRunning = in.Bool();
    _appStale = in.Bool();
    _appCommandMs = in.U32();
    std::string module;
    in.String(module, 12);
    Vdac4AppInfo info;
    in.String(info.name, 64);
    in.String(info.version, 64);
    info.commandFirst = in.U8();
    info.commandLast = in.U8();
    const uint32_t ftDetach = in.U32();
    std::vector<uint8_t> appState;
    in.Blob(appState, 64 * 1024 * 1024);
    if (!in.Ok())
        return false;
    // The running app comes back only if the runtime can restore it; otherwise it is stopped (as after a crash)
    if (appRunning && _runtime != nullptr)
    {
        _appRunning = true;
        _appModule = module;
        _appInfo = info;
        if (_runtime->LoadState(module, info, *this, appState))
        {
            _media.Set(mediaFirst, mediaLast);
            _ftDetachState = ftDetach;
        }
        else
        {
            _appRunning = false;
            _appModule.clear();
            _appInfo = Vdac4AppInfo{};
        }
    }
    return true;
}
