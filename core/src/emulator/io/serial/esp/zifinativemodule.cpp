#include "emulator/io/serial/esp/zifinativemodule.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/serial/esp/zififtpserver.h"
#include "emulator/io/serial/esp/zifistate.h"
#include "emulator/io/serial/esp/zifiweather.h"

// Every behavior here is the firmware's (file:line in the two repositories named in the header):
//  S3  = ZiFi-ESP32-S3-Zero 2e5ba83: src/main.cpp, src/net_client.cpp, src/ntp_client.cpp, src/config.cpp
//  E01 = ZiFi-ESP-01S-Native-C-Project 90834e4: the same files

namespace
{
constexpr uint64_t kWifiTimeoutUs = 10000000;     // kWifiTimeoutMs (S3 main.cpp:41, E01 main.cpp:23)
constexpr uint64_t kConnectTimeoutUs = 8000000;   // NetClient::open: connect(host, port, 8000)
constexpr uint64_t kHeaderTimeoutUs = 10000000;   // readHttpHeader: 10 s
constexpr uint64_t kProbeTimeoutUs = 3000000;     // NetClient::probe: connect(host, port, 3000)
constexpr uint64_t kNtpDnsTimeoutUs = 3000000;    // E01 ntp_client.cpp:38 hostByName(.., 3000)
constexpr uint64_t kNtpReplyTimeoutUs = 3000000;  // queryNtp: 3 s for the answer
constexpr uint64_t kBodyIdleUs = 30000000;        // S3 NetClient::receive kHttpBodyIdleTimeoutMs
constexpr uint64_t kResetUs = 100000;             // handleReset: delay(100) before ESP.restart()
constexpr uint64_t kBootUs = 300000;              // [inferred] the restart to the firmware's loop
constexpr size_t kHeaderMax = 2048;               // httpHeader_[2049]
constexpr size_t kHostMax = 254;                  // char host[254]
constexpr size_t kPathMax = 384;                  // char path[384]
constexpr uint32_t kNtpUnixOffset = 2208988800u;
constexpr size_t kIniLine = 160;                  // IniConfig::parse: char line[160]
constexpr size_t kIniEntries = 16, kIniKey = 24, kIniValue = 96;   // config.hpp
constexpr size_t kEventQueue = 8;                 // S3 eventQueue_ = xQueueCreate(8, ...)
constexpr uint64_t kWifiSignalIntervalUs = 2000000;   // S3 kWifiSignalIntervalMs
constexpr size_t kTxLimit = 4096;                 // reply bytes queued for the UART before the rest waits
constexpr uint64_t kSntpRetryUs = 15000000;       // lwIP SNTP_RETRY_TIMEOUT
constexpr uint64_t kSntpUpdateUs = 3600000000ull; // CONFIG_LWIP_SNTP_UPDATE_DELAY
constexpr uint64_t kSntpReplyUs = 3000000;        // [inferred] how long one SNTP exchange may take
constexpr uint64_t kNoDeadlineUs = 1000000000000ull;

bool Ipv4(const std::string& s, uint32_t& out)
{
    unsigned a = 0, b = 0, c = 0, d = 0;
    char tail = 0;
    if (std::sscanf(s.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
        return false;
    out = NetIp(static_cast<uint8_t>(a), static_cast<uint8_t>(b), static_cast<uint8_t>(c), static_cast<uint8_t>(d));
    return true;
}

uint32_t Fnv1a(const std::string& s)
{
    uint32_t h = 2166136261u;
    for (char c : s)
        h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    return h;
}

/// CRC-32/ISO-HDLC (protocol.cpp crc32IsoHdlc; ConfigStore::checksum)
uint32_t Crc32(const uint8_t* data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/// copyString (S3 main.cpp:345, E01 main.cpp:168): a NUL-ended (or, unless required, payload-ended) string that
/// fits `capacity` with its NUL
bool CopyString(const std::vector<uint8_t>& data, size_t offset, size_t capacity, std::string& out, size_t& next,
                bool requireTerminator)
{
    if (offset > data.size())
        return false;
    size_t end = offset;
    while (end < data.size() && data[end] != 0)
        ++end;
    if ((requireTerminator && end == data.size()) || end - offset >= capacity)
        return false;
    out.assign(data.begin() + static_cast<std::ptrdiff_t>(offset), data.begin() + static_cast<std::ptrdiff_t>(end));
    next = end < data.size() ? end + 1 : end;
    return true;
}

void Le16(std::vector<uint8_t>& v, uint16_t x)
{
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}

void Le32(std::vector<uint8_t>& v, uint32_t x)
{
    Le16(v, static_cast<uint16_t>(x));
    Le16(v, static_cast<uint16_t>(x >> 16));
}

bool StartsNoCase(const std::string& s, size_t at, const char* prefix)
{
    for (size_t i = 0; prefix[i]; ++i)
    {
        if (at + i >= s.size() || std::tolower(static_cast<unsigned char>(s[at + i])) != prefix[i])
            return false;
    }
    return true;
}

/// formatNtpTimestamp (S3 ntp_time.cpp): YYYYMMDDhhmmss in the zone, era-extended
bool FormatNtp(uint32_t ntpSeconds, int tz, std::string& out)
{
    uint64_t extended = ntpSeconds;
    if (extended < kNtpUnixOffset)
        extended += 1ull << 32;
    int64_t local = static_cast<int64_t>(extended - kNtpUnixOffset) + static_cast<int64_t>(tz) * 3600;
    int64_t days = local / 86400;
    int64_t secs = local % 86400;
    if (secs < 0)
    {
        secs += 86400;
        --days;
    }
    // civil_from_days (Howard Hinnant)
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int year = static_cast<int>(yoe) + static_cast<int>(era * 400);
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned day = doy - (153 * mp + 2) / 5 + 1;
    const unsigned month = mp < 10 ? mp + 3 : mp - 9;
    year += month <= 2;
    if (year < 0 || year > 9999)
        return false;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04d%02u%02u%02u%02u%02u", year, month, day, static_cast<unsigned>(secs / 3600),
                  static_cast<unsigned>((secs / 60) % 60), static_cast<unsigned>(secs % 60));
    out.assign(buf, 14);
    return true;
}

using IniEntries = std::vector<std::pair<std::string, std::string>>;

/// IniConfig::parse (S3 config.cpp:60, E01 the same): key: value lines, ';' / '#' comments, quotes, the last key
/// wins, keys lower-cased, at most 16 keys
bool ParseIniEntries(const std::vector<uint8_t>& data, IniEntries& entries, std::string& error)
{
    entries.clear();
    size_t pos = data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF ? 3 : 0;
    while (pos < data.size())
    {
        std::string line;
        bool tooLong = false;
        while (pos < data.size() && data[pos] != '\r' && data[pos] != '\n')
        {
            if (line.size() + 1 < kIniLine)
                line.push_back(static_cast<char>(data[pos]));
            else
                tooLong = true;
            ++pos;
        }
        while (pos < data.size() && (data[pos] == '\r' || data[pos] == '\n'))
            ++pos;
        if (tooLong)
        {
            error = "ini line too long";
            return false;
        }
        size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos || line[b] == ';' || line[b] == '#')
            continue;
        const size_t colon = line.find(':', b);
        if (colon == std::string::npos)
            continue;
        size_t keyEnd = colon;
        while (keyEnd > b && (line[keyEnd - 1] == ' ' || line[keyEnd - 1] == '\t'))
            --keyEnd;
        std::string key = line.substr(b, keyEnd - b);
        if (key.empty())
            continue;
        bool ascii = true;
        for (char& c : key)
        {
            if (static_cast<unsigned char>(c) >= 0x80)
                ascii = false;
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (!ascii)
            continue;
        std::string value = line.substr(colon + 1);
        const size_t v = value.find_first_not_of(" \t");
        value = v == std::string::npos ? std::string() : value.substr(v);
        if (!value.empty() && value[0] == '"')
        {
            const size_t quote = value.find('"', 1);
            value = value.substr(1, quote == std::string::npos ? std::string::npos : quote - 1);
        }
        else
        {
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
                value.pop_back();
        }
        if (key.size() >= kIniKey || value.size() >= kIniValue)
        {
            error = "too many/long ini keys";
            return false;
        }
        auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.first == key; });
        if (it != entries.end())
            it->second = value;
        else if (entries.size() == kIniEntries)
        {
            error = "too many/long ini keys";
            return false;
        }
        else
            entries.emplace_back(key, value);
    }
    return true;
}

std::string IniGet(const IniEntries& entries, const char* key)
{
    for (const auto& e : entries)
        if (e.first == key)
            return e.second;
    return {};
}

std::string Hex2(uint8_t v)
{
    char buf[4];
    std::snprintf(buf, sizeof(buf), "%02X", v);
    return buf;
}
}  // namespace

bool ZiFiNativeModule::ParseVariant(const std::string& text, Variant& out)
{
    std::string t;
    for (char c : text)
    {
        if (c != ' ' && c != '\t' && c != '-' && c != '_')
            t.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    if (t == "S3" || t == "ESP32S3" || t == "ESP32S3ZERO")
        out = Variant::S3;
    else if (t == "ESP01S" || t == "ESP01" || t == "ESP8266")
        out = Variant::Esp01s;
    else
        return false;
    return true;
}

const char* ZiFiNativeModule::VariantName(Variant variant)
{
    return variant == Variant::Esp01s ? "ESP01S" : "S3";
}

const char* ZiFiNativeModule::FirmwareVersion(Variant variant)
{
    // platformio.ini: custom_zifi_version (S3) / -DZIFI_BUILD_VERSION (E01)
    return variant == Variant::Esp01s ? "native-0.2.2" : "s3-native-0.6.94";
}

std::vector<uint8_t> ZiFiNativeModule::Frame(uint8_t cmd, const uint8_t* data, size_t length)
{
    std::vector<uint8_t> f = {kSync, cmd, static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8)};
    uint8_t sum = static_cast<uint8_t>(cmd ^ f[2] ^ f[3]);
    for (size_t i = 0; i < length; ++i)
    {
        f.push_back(data[i]);
        sum ^= data[i];
    }
    f.push_back(sum);
    return f;
}

ZiFiNativeModule::ZiFiNativeModule(VirtualNetwork* network, Variant variant, const std::array<uint8_t, 6>* mac)
    : EspModule(network, variant == Variant::Esp01s ? Chip::Esp8266 : Chip::Esp32, kSlots, mac), _variant(variant),
      _vfs(variant == Variant::S3)
{
    _vfs.now = [this]() { return Now(); };
    _vfs.micros = [this](uint64_t us) { return MicrosToT(us); };
    _vfs.sendFrame = [this](uint8_t cmd, const std::vector<uint8_t>& data) { SendFrame(cmd, data); };
    ZiFiBridgeHost& host = *this;
    _ftp = std::make_unique<ZiFiFtpServer>(host, variant == Variant::S3);
    // Silent at power-up: UART first, no boot text on the protocol line (S3 begin(), E01 begin()); the saved
    // zifi.ini joins the access point on its own
}

ZiFiNativeModule::~ZiFiNativeModule() = default;

bool ZiFiNativeModule::ClientOpen() const
{
    return Stack().Valid(kClientSlot) && Stack().GetSlot(kClientSlot).state == EspStack::State::Tcp;
}

std::string ZiFiNativeModule::Activity() const
{
    static const char* const kOps[] = {"idle", "wifi join", "net_open", "net_http_get", "net_ping", "net_ntp", "restart",
                                       "deferred (an FTP command runs)", "weather"};
    static const char* const kPhases[] = {"", " resolving", " connecting", " reading the header", " waiting for the AP",
                                          " probing the proxy", " waiting for the answer", " reading the body",
                                          " waiting to retry"};
    std::string text = kOps[static_cast<int>(_op)];
    if (_op != Op::None)
        text += kPhases[static_cast<int>(_phase)];
    return text;
}

// --- The frame layer -------------------------------------------------------------------------------------------

void ZiFiNativeModule::SendFrame(uint8_t cmd, const uint8_t* data, size_t length)
{
    std::vector<uint8_t> f = Frame(cmd, data, length);
    // The UART driver's buffer: a 16 KiB write window waits here, not all at once in the output
    if (!_txBacklog.empty() || Pending() + f.size() > kTxLimit)
        _txBacklog.push_back(std::move(f));
    else
        Send(f);
    if (cmd != kAck)
    {
        std::string text = Hex2(cmd);
        if (length)
            text += " +" + std::to_string(length);
        if (cmd == kError)
            text += " \"" + std::string(reinterpret_cast<const char*>(data), length) + "\"";
        LogReply(text);
    }
}

void ZiFiNativeModule::ReportError(const std::string& text)
{
    _lastError = text.substr(0, 63);   // lastError_[64]
    const size_t n = std::min(_lastError.size(), kErrorText);   // sendError: at most 48 bytes
    SendFrame(kError, reinterpret_cast<const uint8_t*>(_lastError.data()), n);
}

void ZiFiNativeModule::Process()
{
    if (!Running())
        return;
    // New bytes: a partial frame older than 500 ms was dropped by the parser before them (checkTimeout)
    if (_rx.size() > _rxSeen)
    {
        const bool parsing = !(Busy() && _variant == Variant::Esp01s) && _op != Op::Boot;
        if (parsing && _rxSeen > _held && Now() >= _lastByteAt + MicrosToT(kFrameTimeoutUs))
        {
            _rx.erase(_rx.begin() + _held, _rx.begin() + static_cast<std::ptrdiff_t>(_rxSeen));
            ++_resyncs;
        }
        _lastByteAt = Now();
    }
    struct SeenGuard
    {
        ZiFiNativeModule& m;
        ~SeenGuard() { m._rxSeen = m._rx.size(); }
    } seen{*this};

    while (true)
    {
        if (_op == Op::Boot)
        {
            _rx.clear();   // restarting: the UART takes nothing
            return;
        }
        if (Busy() && _variant == Variant::Esp01s)
            return;   // one loop: the command waits for the network, later bytes wait in the UART
        if (_variant == Variant::Esp01s && _ftp->JobActive() && !_vfs.Waiting())
            return;   // one loop: inside an FTP command the UART is read only by its VFS waits
        const size_t pos = _held;
        while (_rx.size() > pos && _rx[pos] != kSync)
            _rx.erase(_rx.begin() + static_cast<std::ptrdiff_t>(pos));   // kWaitSync
        if (_rx.size() - pos < 4)
            return;
        const uint16_t length = static_cast<uint16_t>(_rx[pos + 2] | (_rx[pos + 3] << 8));
        if (length > kMaxPayload)
        {
            // Lost sync: the parser resets after LEN_H, the length is not skipped
            _rx.erase(_rx.begin() + static_cast<std::ptrdiff_t>(pos), _rx.begin() + static_cast<std::ptrdiff_t>(pos + 4));
            ++_resyncs;
            continue;
        }
        const size_t frameLength = 5u + length;
        if (_rx.size() - pos < frameLength)
            return;
        uint8_t sum = 0;
        for (size_t i = pos + 1; i < pos + 4 + length; ++i)
            sum ^= _rx[i];
        if (sum != _rx[pos + 4 + length])
        {
            _rx.erase(_rx.begin() + static_cast<std::ptrdiff_t>(pos),
                      _rx.begin() + static_cast<std::ptrdiff_t>(pos + frameLength));
            ++_badChecksums;
            continue;
        }
        const uint8_t cmd = _rx[pos + 1];
        const std::vector<uint8_t> payload(_rx.begin() + static_cast<std::ptrdiff_t>(pos + 4),
                                           _rx.begin() + static_cast<std::ptrdiff_t>(pos + 4 + length));
        if (_vfs.Waiting())
        {
            // waitFor: the awaited answer; PING and SYS_RESET stay served (handleUnexpected); the rest is lost
            _rx.erase(_rx.begin() + static_cast<std::ptrdiff_t>(pos), _rx.begin() + static_cast<std::ptrdiff_t>(pos + frameLength));
            if (cmd == _vfs.AwaitedCommand())
            {
                _vfs.OnResponse(payload);
                if (!_vfs.Waiting())
                    BridgePoll();
            }
            else if (cmd == kPing)
                SendFrame(kReady);
            else if (cmd == kSysReset)
            {
                Ack();
                SystemReset();
            }
            else
                ++_droppedWhileWaiting;
            continue;
        }
        Handle(cmd, payload, frameLength);
    }
}

// --- Commands ---------------------------------------------------------------------------------------------------

void ZiFiNativeModule::Handle(uint8_t cmd, const std::vector<uint8_t>& payload, size_t frameLength)
{
    // The frame leaves the buffer unless a command keeps it while it waits for the network (Start)
    const auto drop = [this, frameLength]() {
        _rx.erase(_rx.begin() + _held, _rx.begin() + static_cast<std::ptrdiff_t>(_held + frameLength));
    };
    if (cmd != kGetStep)
        _lastStep = cmd;   // the breadcrumb GET_STEP reports
    ++_requests;
    LogRequest(Hex2(cmd) + (payload.empty() ? std::string() : " +" + std::to_string(payload.size())));

    const bool s3 = _variant == Variant::S3;
    switch (cmd)
    {
        case kEcho:
            drop();
            SendFrame(kEcho, payload);
            ClearError();
            return;
        case kPing:
            drop();
            SendFrame(kReady);
            return;
        case kGetStep:
        {
            drop();
            std::vector<uint8_t> r = {_lastStep};
            r.insert(r.end(), _lastError.begin(), _lastError.end());
            SendFrame(kRespGetStep, r);
            return;
        }
        case kSysInfo:
            drop();
            Ack();
            SysInfo();
            return;
        case kSysReset:
            drop();
            Ack();
            return SystemReset();
        default: break;
    }

    const bool known = cmd == kWifiConnect || cmd == kWifiIni || cmd == kFtpStart || cmd == kFtpStop ||
                       cmd == kUpdateStart || cmd == kUpdateStop || cmd == kFtpRamStats || cmd == kNetOpen ||
                       cmd == kNetSend || cmd == kNetRecv || cmd == kNetClose || cmd == kNetHttpGet || cmd == kNetPing ||
                       cmd == kNetIpConfig || cmd == kNetNtp ||
                       (s3 && (cmd == kSmbStart || cmd == kSmbStop || cmd == kOnlineUpdate || cmd == kOnlineUpdateCheck ||
                               (cmd >= kNetProxyStatus && cmd <= kWcuSync)));
    if (!known)
    {
        drop();
        ReportError(s3 ? "unsupported:" + Hex2(cmd) : "unknown cmd " + Hex2(cmd));
        return;
    }
    Network(cmd, payload, frameLength);
}

void ZiFiNativeModule::SystemReset()
{
    // The ACK leaves, then ESP.restart(): the links, the Wi-Fi association, the servers and the session go
    Stack().Close(-1);
    Leave();
    _bodyActive = false;
    _rx.clear();   // a request the network core held goes with the restart
    _held = 0;
    _request.clear();
    ForgetBridge();
    ResetWeather();
    _op = Op::Boot;
    _phase = Phase::None;
    _opStart = Now();
    _opDeadline = Now() + MicrosToT(kResetUs + kBootUs);
}

void ZiFiNativeModule::Network(uint8_t cmd, const std::vector<uint8_t>& payload, size_t frameLength)
{
    const auto drop = [this, frameLength]() {
        _rx.erase(_rx.begin() + _held, _rx.begin() + static_cast<std::ptrdiff_t>(_held + frameLength));
    };
    // ACK as soon as the command is in; NET_RECV answers directly (the historical contract); a deferred command
    // had its ACK when it came
    if (cmd != kNetRecv && !_deferredRun)
        Ack();
    if (Busy())
    {
        // S3: one network request at a time (submitNetwork); ESP01S never gets here (its loop waits)
        drop();
        ReportError("network busy");
        SendFailure(cmd);
        return;
    }
    if (_variant == Variant::S3 && _ftp->JobActive())
    {
        // The network core is inside the FTP server: the request waits in its queue until the command is done
        _request = payload;
        return Start(Op::Deferred, Phase::None, kNoDeadlineUs, frameLength);
    }
    const bool wifi = GetWifi() == Wifi::GotIp;
    // An answer that needs no network
    const auto reply = [&](uint8_t resp, const std::vector<uint8_t>& data, const std::string& error) {
        drop();
        if (error.empty())
            ClearError();
        else
            ReportError(error);
        SendFrame(resp, data);
    };
    const bool s3 = _variant == Variant::S3;
    switch (cmd)
    {
        case kWifiConnect:
        {
            std::string ssid, password;
            size_t next = 0, ignored = 0;
            if (!CopyString(payload, 0, 33, ssid, next, true) || !CopyString(payload, next, 64, password, ignored, false))
                return reply(kRespWifiConnect, WifiResult(false), "wifi payload");
            _request = payload;
            Start(Op::Join, Phase::Join, kWifiTimeoutUs, frameLength);
            return StartJoin(ssid, password, false);
        }
        case kWifiIni:
        {
            std::string ssid, password, error;
            const size_t maxIni = s3 ? 1024 : 512;   // ConfigStore::kMaxIniSize
            if (payload.empty() || payload.size() > maxIni || !ParseIni(payload, ssid, password, error))
                return reply(kRespWifiIni, WifiResult(false), "ini:" + (error.empty() ? std::string("invalid") : error));
            _iniCrc = Crc32(payload.data(), payload.size());   // saved to flash: SYS_RESET keeps it
            _iniText = payload;
            _request = payload;
            Start(Op::Join, Phase::Join, kWifiTimeoutUs, frameLength);
            return StartJoin(ssid, password, true);
        }
        case kNetOpen:
        {
            size_t next = 0;
            const std::string host = HostOf(payload, next);
            if (host.empty())
                return reply(0x90, {0}, "open:no host");
            Stack().Close(kClientSlot);   // NetClient::open: close() first; both firmwares stop the FTP server
            _bodyActive = false;
            StopFileServers();
            if (!wifi)
                return reply(0x90, {0}, "open:no wifi");
            _request = payload;
            Start(Op::Open, Phase::Resolve, kConnectTimeoutUs, frameLength);
            return StartOpen(payload);
        }
        case kNetSend:
            drop();
            return NetSend(payload);
        case kNetRecv:
            drop();
            return NetRecv(payload);
        case kNetClose:
            Stack().Close(kClientSlot);
            _bodyActive = false;
            return reply(0x93, {1}, {});
        case kNetHttpGet:
        {
            std::string host, path;
            size_t next = 0, after = 0;
            bool valid = CopyString(payload, 0, kHostMax, host, next, true) && !host.empty() && next + 2 <= payload.size();
            if (valid)
                valid = CopyString(payload, next + 2, kPathMax, path, after, false);
            if (!valid)
                return reply(0x94, std::vector<uint8_t>(7, 0), "get:bad payload");
            StopFileServers();   // processHttpGet / handleHttpGet
            if (!wifi)
            {
                Stack().Close(kClientSlot);
                _bodyActive = false;
                return reply(0x94, std::vector<uint8_t>(7, 0), "get:no wifi");
            }
            _request = payload;
            _redirects = 0;
            Start(Op::HttpGet, Phase::Resolve, kConnectTimeoutUs, frameLength);
            return StartHttpGet(payload);
        }
        case kNetPing:
        {
            size_t next = 0;
            const std::string host = HostOf(payload, next);
            if (host.empty())
                return reply(0xA1, {0, 0, 0}, "ping:no host");
            StopFileServers();   // processNetProbe / handleNetProbe
            if (!wifi)
                return reply(0xA1, {0, 0, 0}, {});   // a negative probe is no error
            _request = payload;
            Start(Op::Probe, Phase::Resolve, kProbeTimeoutUs, frameLength);
            return StartProbe(host, 80, Op::Probe);
        }
        case kNetIpConfig:
        {
            std::vector<uint8_t> r(16, 0);
            if (wifi)
            {
                const uint32_t v[4] = {Ip(), Netmask(), Gateway(), DnsServer()};
                for (int i = 0; i < 4; ++i)
                    for (int b = 0; b < 4; ++b)
                        r[static_cast<size_t>(i * 4 + b)] = static_cast<uint8_t>(v[i] >> (24 - 8 * b));
            }
            return reply(0xA0, r, {});
        }
        case kNetNtp:
            if (!wifi)
                return reply(kRespNetNtp, std::vector<uint8_t>(14, '0'), "ntp:no wifi");
            _request = payload;
            Start(Op::Ntp, Phase::Resolve, s3 ? kNtpDnsTimeoutUs + 1000000 : kNtpDnsTimeoutUs, frameLength);
            return StartNtp();
        case kNetProxyStatus:
        {
            std::vector<uint8_t> r = {_proxyStatus};
            if (!_proxyHost.empty())
            {
                const std::string endpoint = _proxyHost + ":" + std::to_string(_proxyPort);
                r.insert(r.end(), endpoint.begin(), endpoint.end());
            }
            return reply(0xA3, r, {});
        }
        // The file bridge
        case kFtpStart:
            drop();
            return StartFtp(payload);
        case kFtpStop:
            StopFileServers();
            return reply(0x87, {1}, {});
        case kFtpRamStats: return reply(0x8A, _ftp->RamStats(), {});
        // The services the emulation leaves out: the firmware's own answer when they cannot start
        case kUpdateStart:
            StopFileServers();
            if (!wifi)
                return reply(0x88, std::vector<uint8_t>(7, 0), s3 ? "ota:no wifi config" : "update:no wifi");
            return reply(0x88, std::vector<uint8_t>(7, 0), s3 ? "ota:not emulated" : "update:not emulated");
        case kUpdateStop: return reply(0x89, {1}, {});
        case kSmbStart:
            Stack().Close(kClientSlot);
            _bodyActive = false;
            StopFileServers();
            return reply(0x8B, {0, 0, 0, 0}, "smb:not emulated");
        case kSmbStop: return reply(0x8C, {1}, {});
        case kOnlineUpdateCheck:
            return reply(0x8E, {0}, wifi ? "update-check:not emulated" : "update-check:no wifi");
        case kOnlineUpdate:
            StopFileServers();
            return reply(0x8D, {0}, wifi ? "update:not emulated" : "update:no wifi");
        case kWeatherGet:
            // WeatherService::get: the place from the saved zifi.ini, then the network
            _request.clear();
            Start(Op::Weather, Phase::None, zifiweather::kBudgetUs, frameLength);
            return StartWeather();
        case kWcuStart:
            StopFileServers();
            return reply(0xA5, {0}, wifi ? "wcu:not emulated" : "wcu:no wifi");
        case kWcuApply: return reply(0xA6, {0}, {});
        case kWcuStop: return reply(0xA7, {1}, {});
        case kWcuSync: return reply(0xA8, {0}, {});
        default: break;
    }
    drop();
}

void ZiFiNativeModule::SendFailure(uint8_t cmd)
{
    // sendNetworkFailure / sendWifiFailure / sendNtpFailure (S3 main.cpp:1325-1400)
    switch (cmd)
    {
        case kWifiConnect: return SendFrame(kRespWifiConnect, std::vector<uint8_t>(5, 0));
        case kWifiIni: return SendFrame(kRespWifiIni, std::vector<uint8_t>(5, 0));
        case kNetNtp: return SendFrame(kRespNetNtp, std::vector<uint8_t>(14, 0));
        case kFtpStart: return SendFrame(0x86, std::vector<uint8_t>(3, 0));
        case kFtpStop: return SendFrame(0x87, {0});
        case kFtpRamStats: return SendFrame(0x8A);
        case kSmbStart: return SendFrame(0x8B, std::vector<uint8_t>(4, 0));
        case kSmbStop: return SendFrame(0x8C, {0});
        case kWcuStart: return SendFrame(0xA5, {0});
        case kWcuApply: return SendFrame(0xA6, {0});
        case kWcuStop: return SendFrame(0xA7, {0});
        case kWcuSync: return SendFrame(0xA8, {0});
        case kUpdateStart: return SendFrame(0x88, std::vector<uint8_t>(7, 0));
        case kUpdateStop: return SendFrame(0x89, {0});
        case kOnlineUpdateCheck: return SendFrame(0x8E, {0});
        case kOnlineUpdate: return SendFrame(0x8D, {0});
        case kNetOpen: return SendFrame(0x90, {0});
        case kNetSend: return SendFrame(0x91, {0});
        case kNetRecv: return SendFrame(kRespNetRecv, {1});
        case kNetClose: return SendFrame(0x93, {0});
        case kNetHttpGet: return SendFrame(0x94, std::vector<uint8_t>(7, 0));
        case kNetPing: return SendFrame(0xA1, std::vector<uint8_t>(3, 0));
        case kNetIpConfig: return SendFrame(0xA0, std::vector<uint8_t>(16, 0));
        case kNetProxyStatus: return SendFrame(0xA3, {0});
        case kWeatherGet: return SendFrame(0xA4, {0, 1});
        default: break;
    }
}

void ZiFiNativeModule::Start(Op op, Phase phase, uint64_t timeoutUs, size_t frameLength)
{
    // The request stays at the head of the buffer (nothing precedes it: one command at a time) until Finish
    if (_sntpPhase)
    {
        // The command takes the resolver / query socket: the background SNTP tries again later
        _sntpPhase = 0;
        _sntpNextAt = Now() + MicrosToT(kSntpRetryUs);
    }
    _op = op;
    _phase = phase;
    _opCmd = _rx.size() > 1 ? _rx[1] : 0;
    _held = static_cast<uint16_t>(frameLength);
    _opStart = Now();
    _opDeadline = Now() + MicrosToT(timeoutUs);
}

void ZiFiNativeModule::Finish(uint8_t respCmd, const std::vector<uint8_t>& data, const std::string& error)
{
    _rx.erase(_rx.begin(), _rx.begin() + std::min<size_t>(_held, _rx.size()));
    _rxSeen = _rxSeen > _held ? _rxSeen - _held : 0;
    _held = 0;
    _op = Op::None;
    _phase = Phase::None;
    _request.clear();
    // ESP01S: the loop reads the UART again; what waited there reaches the parser now
    if (_variant == Variant::Esp01s)
        _lastByteAt = Now();
    if (error.empty())
        ClearError();
    else
        ReportError(error);
    SendFrame(respCmd, data);
}

std::string ZiFiNativeModule::HostOf(const std::vector<uint8_t>& payload, size_t& next) const
{
    std::string host;
    if (!CopyString(payload, 0, kHostMax, host, next, false))
        return {};
    return host;
}

std::vector<uint8_t> ZiFiNativeModule::WifiResult(bool connected) const
{
    std::vector<uint8_t> r(5, 0);
    if (connected)
    {
        r[0] = 1;
        for (int b = 0; b < 4; ++b)
            r[static_cast<size_t>(1 + b)] = static_cast<uint8_t>(Ip() >> (24 - 8 * b));
    }
    return r;
}

// --- Wi-Fi ------------------------------------------------------------------------------------------------------

bool ZiFiNativeModule::ParseIni(const std::vector<uint8_t>& data, std::string& ssid, std::string& password,
                                std::string& error)
{
    IniEntries entries;
    if (!ParseIniEntries(data, entries, error))
        return false;
    const auto get = [&](const char* key) { return IniGet(entries, key); };
    ssid = get("ssid");
    password = get("password");
    if (ssid.empty())
    {
        error = "no ssid";
        return false;
    }
    if (ssid.size() > 32)
    {
        error = "ssid too long";
        return false;
    }
    if (password.size() > 63)
    {
        error = "password too long";
        return false;
    }
    // time: whole hours -14..+14 (anything else, "none" of old files included, is UTC)
    {
        const std::string t = get("time");
        size_t i = 0;
        int sign = 1;
        if (i < t.size() && (t[i] == '+' || t[i] == '-'))
            sign = t[i++] == '-' ? -1 : 1;
        int hours = 0;
        bool digits = false;
        while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i])))
        {
            hours = hours * 10 + (t[i++] - '0');
            digits = true;
            if (hours > 99)
                break;
        }
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t'))
            ++i;
        const bool tail = i < t.size() && t[i] != ';' && t[i] != '#';
        _timeZone = static_cast<int8_t>(digits && !tail && hours <= 14 ? sign * hours : 0);
    }
    if (_variant == Variant::S3)
    {
        _proxyHost = get("proxy_ip");
        if (_proxyHost.empty())
            _proxyHost = get("proxy_host");
        if (_proxyHost.size() > 63)
            _proxyHost.resize(63);   // what the emulation keeps (the firmware's buffer is 96)
        const unsigned long port = std::strtoul(get("proxy_port").c_str(), nullptr, 10);
        _proxyPort = port == 0 || port > 65535 ? 49281 : static_cast<uint16_t>(port);
    }
    return true;
}

void ZiFiNativeModule::StartJoin(const std::string& ssid, const std::string& password, bool ini)
{
    _iniJoin = ini;
    if (ssid.empty())
        return JoinDone(false);
    // connectWifi: the same network, already up, keeps its sockets. A module fresh from the box is already on the
    // virtual access point (every emulated ESP is), as a module that booted from its saved zifi.ini would be: the
    // first credentials for that network are the ones it joined with (no 1.5 s rejoin the plugins do not wait for)
    if (GetWifi() == Wifi::GotIp && ssid == Ssid() && (!_passwordKnown || Fnv1a(password) == _passwordHash))
    {
        _passwordHash = Fnv1a(password);
        _passwordKnown = true;
        return JoinDone(true);
    }
    _passwordHash = Fnv1a(password);
    _passwordKnown = true;
    // The association drops: ESP01S stopAllNetworkServices (the FTP server stops); S3 WiFi.disconnect (the FTP
    // server keeps listening, its connections end)
    if (_variant == Variant::Esp01s)
        StopFileServers();
    const bool ftpListening = _ftp->Running();
    Stack().Close(-1);
    if (ftpListening)
        _ftp->LinkLost();
    _bodyActive = false;
    Leave();
    Join(ssid);
}

void ZiFiNativeModule::JoinDone(bool connected)
{
    const uint8_t resp = _iniJoin ? kRespWifiIni : kRespWifiConnect;
    if (connected && _variant == Variant::S3 && _phase != Phase::Proxy)
        _sntpNextAt = Now();   // connectWifi: configTime starts SNTP
    if (connected && _variant == Variant::S3 && _phase != Phase::Proxy)
    {
        // updateProxyState: a configured proxy is probed (3 s) before the answer
        if (!_proxyHost.empty())
        {
            _opDeadline = Now() + MicrosToT(kProbeTimeoutUs);
            _phase = Phase::Proxy;
            return StartProbe(_proxyHost, _proxyPort, Op::Join);
        }
        _proxyStatus = 0;
    }
    Finish(resp, WifiResult(connected), connected ? std::string() : std::string("wifi timeout"));
}

// --- TCP --------------------------------------------------------------------------------------------------------

void ZiFiNativeModule::StartOpen(const std::vector<uint8_t>& payload)
{
    size_t next = 0;
    const std::string host = HostOf(payload, next);
    _opPort = next + 2 <= payload.size() ? static_cast<uint16_t>(payload[next] | (payload[next + 1] << 8)) : 80;
    uint32_t addr = 0;
    if (Ipv4(host, addr))
    {
        EspStack::Done done;
        done.kind = EspStack::Done::Kind::Resolve;
        done.addr = addr;
        return OnStackDone(done);
    }
    Stack().Resolve(host);
}

void ZiFiNativeModule::StartProbe(const std::string& host, uint16_t port, Op op)
{
    (void)op;
    _opPort = port;
    _opStart = Now();
    uint32_t addr = 0;
    if (Ipv4(host, addr))
    {
        EspStack::Done done;
        done.kind = EspStack::Done::Kind::Resolve;
        done.addr = addr;
        return OnStackDone(done);
    }
    Stack().Resolve(host);
}

void ZiFiNativeModule::StartHttpGet(const std::vector<uint8_t>& payload)
{
    std::string host;
    size_t next = 0;
    CopyString(payload, 0, kHostMax, host, next, true);
    _httpPort = static_cast<uint16_t>(payload[next] | (payload[next + 1] << 8));
    Stack().Close(kClientSlot);
    _bodyActive = false;
    // S3: port 443 is HTTPS (WiFiClientSecure); the virtual network has no TLS (the firmware's own failure)
    if (_variant == Variant::S3 && _httpPort == 443)
        return HttpFail("tls connect failed");
    _viaProxy = _variant == Variant::S3 && _proxyStatus == 1 && !_proxyHost.empty();
    _opPort = _viaProxy ? _proxyPort : _httpPort;
    const std::string target = _viaProxy ? _proxyHost : host;
    uint32_t addr = 0;
    if (Ipv4(target, addr))
    {
        EspStack::Done done;
        done.kind = EspStack::Done::Kind::Resolve;
        done.addr = addr;
        return OnStackDone(done);
    }
    Stack().Resolve(target);
}

void ZiFiNativeModule::HttpConnect()
{
    // The request (NetClient::httpGet, HTTP/1.0, Connection: close)
    std::string host, path;
    size_t next = 0, after = 0;
    CopyString(_request, 0, kHostMax, host, next, true);
    CopyString(_request, next + 2, kPathMax, path, after, false);
    if (path.empty())
        path = "/";
    std::string text;
    const std::string port = std::to_string(_httpPort);
    if (_variant == Variant::Esp01s)
        text = "GET " + path + " HTTP/1.0\r\nHost: " + host + "\r\nUser-Agent: ZiFi (ZX Evo)\r\nAccept: */*\r\nConnection: close\r\n\r\n";
    else
    {
        const bool otherPort = _httpPort != 80 && _httpPort != 0;
        const std::string hostField = otherPort ? host + ":" + port : host;
        if (_viaProxy)
            text = "GET http://" + hostField + path + " HTTP/1.0\r\nHost: " + hostField +
                   "\r\nProxy-Authorization: Basic eng6eng=\r\n";
        else
            text = "GET " + path + " HTTP/1.0\r\nHost: " + hostField + "\r\n";
        text += "User-Agent: ZiFi (ZX Evo)\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
    }
    Stack().Send(kClientSlot, reinterpret_cast<const uint8_t*>(text.data()), static_cast<uint32_t>(text.size()));
    _phase = Phase::Header;
    _opDeadline = Now() + MicrosToT(kHeaderTimeoutUs);
    HttpHeader();
}

void ZiFiNativeModule::HttpHeader()
{
    if (!HttpOp() || _phase != Phase::Header || !Stack().Valid(kClientSlot))
        return;
    const EspStack::Slot& slot = Stack().GetSlot(kClientSlot);
    size_t end = 0;
    for (size_t i = 3; i < slot.rx.size() && i < kHeaderMax; ++i)
    {
        if (slot.rx[i - 3].value == '\r' && slot.rx[i - 2].value == '\n' && slot.rx[i - 1].value == '\r' &&
            slot.rx[i].value == '\n')
        {
            end = i + 1;
            break;
        }
    }
    const std::vector<uint8_t> none(7, 0);
    if (!end)
    {
        if (slot.rx.size() >= kHeaderMax)
        {
            Stack().Close(kClientSlot);
            return HttpFail("header too long");
        }
        if (slot.finSeen)
        {
            Stack().Close(kClientSlot);
            return HttpFail("closed in header");
        }
        return;   // more to come
    }
    const std::vector<uint8_t> raw = Stack().Read(kClientSlot, static_cast<uint32_t>(end));
    const std::string header(raw.begin(), raw.end());
    uint16_t status = 0;
    const size_t space = header.find(' ');
    if (space != std::string::npos)
        status = static_cast<uint16_t>(std::strtoul(header.c_str() + space + 1, nullptr, 10));
    uint32_t contentLength = 0;
    bool lengthKnown = false, chunked = false;
    std::string location;
    for (size_t line = 0; line < header.size();)
    {
        const size_t eol = header.find("\r\n", line);
        if (eol == std::string::npos)
            break;
        if (StartsNoCase(header, line, "content-length:"))
        {
            size_t v = line + 15;
            while (v < eol && (header[v] == ' ' || header[v] == '\t'))
                ++v;
            char* parsedEnd = nullptr;
            const unsigned long parsed = std::strtoul(header.c_str() + v, &parsedEnd, 10);
            size_t e = static_cast<size_t>(parsedEnd - header.c_str());
            while (e < eol && (header[e] == ' ' || header[e] == '\t'))
                ++e;
            if (_variant == Variant::S3 && (e == v || e != eol))
            {
                Stack().Close(kClientSlot);
                return HttpFail("bad content length");
            }
            contentLength = static_cast<uint32_t>(parsed);
            lengthKnown = true;
            if (_variant == Variant::Esp01s)
                break;   // E01 stops at the first Content-Length
        }
        else if (_variant == Variant::S3 && StartsNoCase(header, line, "transfer-encoding:") &&
                 header.substr(line, eol - line).find("hunked") != std::string::npos)
            chunked = true;
        else if (_variant == Variant::S3 && StartsNoCase(header, line, "location:"))
        {
            size_t v = line + 9;
            while (v < eol && (header[v] == ' ' || header[v] == '\t'))
                ++v;
            size_t e = eol;
            while (e > v && (header[e - 1] == ' ' || header[e - 1] == '\t'))
                --e;
            location = header.substr(v, e - v);
        }
        line = eol + 2;
    }
    const bool redirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
    if (_variant == Variant::S3 && redirect)
    {
        // Up to 4 redirects (kMaxRedirects = 5); the new target rewrites the held request
        Stack().Close(kClientSlot);
        if (location.empty())
            return HttpFail("redirect no location");
        if (_redirects >= 4)
            return HttpFail("too many redirects");
        std::string host, path;
        size_t next = 0, after = 0;
        CopyString(_request, 0, kHostMax, host, next, true);
        CopyString(_request, next + 2, kPathMax, path, after, false);
        uint16_t port = _httpPort;
        if (StartsNoCase(location, 0, "https://"))
            return HttpFail("tls connect failed");
        const bool absolute = StartsNoCase(location, 0, "http://") || location.rfind("//", 0) == 0;
        if (absolute)
        {
            const size_t a = location.find("//") + 2;
            size_t ae = a;
            while (ae < location.size() && location[ae] != '/' && location[ae] != '?' && location[ae] != '#')
                ++ae;
            const std::string authority = location.substr(a, ae - a);
            const size_t colon = authority.rfind(':');
            host = colon == std::string::npos ? authority : authority.substr(0, colon);
            port = 80;
            if (colon != std::string::npos)
            {
                const unsigned long p = std::strtoul(authority.c_str() + colon + 1, nullptr, 10);
                if (p == 0 || p > 65535)
                    return HttpFail("redirect port");
                port = static_cast<uint16_t>(p);
            }
            if (host.empty() || host.size() >= kHostMax)
                return HttpFail("redirect host");
            path = ae < location.size() && location[ae] == '/' ? location.substr(ae)
                   : ae < location.size() && location[ae] == '?' ? "/" + location.substr(ae)
                                                                 : "/";
        }
        else if (!location.empty() && location[0] == '/')
            path = location;
        else if (!location.empty() && location[0] == '?')
            path = path.substr(0, path.find('?')) + location;
        else
        {
            const size_t slash = path.rfind('/');
            path = (slash == std::string::npos ? std::string("/") : path.substr(0, slash + 1)) + location;
        }
        path = path.substr(0, path.find('#'));
        if (path.empty())
            path = "/";
        if (path.size() >= kPathMax)
            return HttpFail("redirect path");
        // The held frame becomes the new request: a checkpoint keeps it
        std::vector<uint8_t> payload(host.begin(), host.end());
        payload.push_back(0);
        Le16(payload, port);
        payload.insert(payload.end(), path.begin(), path.end());
        payload.push_back(0);
        if (_op == Op::HttpGet)
        {
            const std::vector<uint8_t> frame = Frame(kNetHttpGet, payload);
            _rx.erase(_rx.begin(), _rx.begin() + _held);
            _rx.insert(_rx.begin(), frame.begin(), frame.end());
            _rxSeen = _rxSeen + frame.size() - _held;
            _held = static_cast<uint16_t>(frame.size());
        }
        _request = payload;
        ++_redirects;
        _phase = Phase::Resolve;
        _opDeadline = Now() + MicrosToT(kConnectTimeoutUs);
        return StartHttpGet(payload);
    }
    if (_variant == Variant::S3 && chunked)
    {
        Stack().Close(kClientSlot);
        return HttpFail("chunked unsupported");
    }
    if (_op == Op::Weather)
        return WeatherHeader(status, contentLength, lengthKnown);
    if (_variant == Variant::S3)
    {
        _bodyActive = true;
        _bodyLengthKnown = lengthKnown;
        _bodyExpected = contentLength;
        _bodyReceived = 0;
        _bodyActivityAt = Now();
    }
    std::vector<uint8_t> r = {1};
    Le16(r, status);
    Le32(r, contentLength);
    Finish(0x94, r);
}

void ZiFiNativeModule::NetSend(const std::vector<uint8_t>& payload)
{
    // NetClient::sendAll: the client must be open (or still hold bytes)
    const bool valid = Stack().Valid(kClientSlot) && Stack().GetSlot(kClientSlot).state == EspStack::State::Tcp;
    const bool active = valid && (!Stack().GetSlot(kClientSlot).finSeen || !Stack().GetSlot(kClientSlot).rx.empty());
    if (!active)
    {
        ReportError("send:not open");
        return SendFrame(0x91, {0});
    }
    if (Stack().GetSlot(kClientSlot).finSeen && !payload.empty())
    {
        ReportError("send:short send");   // write() takes nothing from a closed connection
        return SendFrame(0x91, {0});
    }
    if (!payload.empty())
        Stack().Send(kClientSlot, payload.data(), static_cast<uint32_t>(payload.size()));
    ClearError();
    SendFrame(0x91, {1});
}

void ZiFiNativeModule::NetRecv(const std::vector<uint8_t>& payload)
{
    size_t limit = kMaxPayload - 1;
    if (payload.size() >= 2)
        limit = std::clamp<size_t>(static_cast<size_t>(payload[0] | (payload[1] << 8)), 1, kMaxPayload - 1);
    const bool s3 = _variant == Variant::S3;
    const auto close = [this]() {
        Stack().Close(kClientSlot);
        _bodyActive = false;
    };
    // S3: Content-Length is the end of an HTTP body
    if (s3 && _bodyActive && _bodyLengthKnown && _bodyReceived >= _bodyExpected)
    {
        close();
        ClearError();
        return SendFrame(kRespNetRecv, {1});
    }
    const bool valid = Stack().Valid(kClientSlot) && Stack().GetSlot(kClientSlot).state == EspStack::State::Tcp;
    const size_t have = valid ? Stack().GetSlot(kClientSlot).rx.size() : 0;
    if (have)
    {
        size_t wanted = std::min(limit, have);
        if (s3 && _bodyActive && _bodyLengthKnown)
            wanted = std::min<size_t>(wanted, _bodyExpected - _bodyReceived);
        const std::vector<uint8_t> data = Stack().Read(kClientSlot, static_cast<uint32_t>(wanted));
        if (_bodyActive)
        {
            _bodyReceived += static_cast<uint32_t>(data.size());
            _bodyActivityAt = Now();
        }
        std::vector<uint8_t> r = {0};
        r.insert(r.end(), data.begin(), data.end());
        ClearError();
        return SendFrame(kRespNetRecv, r);
    }
    // Nothing waiting: a closed connection is EOF, a live one "nothing yet"
    if (!valid || Stack().GetSlot(kClientSlot).finSeen)
    {
        close();
        ClearError();
        return SendFrame(kRespNetRecv, {1});
    }
    if (s3 && _bodyActive && Now() >= _bodyActivityAt + MicrosToT(kBodyIdleUs))
    {
        close();
        ReportError("recv:body timeout");
        return SendFrame(kRespNetRecv, {1});
    }
    ClearError();
    SendFrame(kRespNetRecv, {0});
}

// --- NTP --------------------------------------------------------------------------------------------------------

void ZiFiNativeModule::StartNtp()
{
    Stack().Resolve("pool.ntp.org");
}

// --- Weather (S3 weather_service.cpp) ---------------------------------------------------------------------------

std::string ZiFiNativeModule::IniValue(const char* key) const
{
    IniEntries entries;
    std::string error;
    if (_iniText.empty() || !ParseIniEntries(_iniText, entries, error))
        return {};
    return IniGet(entries, key);
}

void ZiFiNativeModule::HttpFail(const std::string& reason)
{
    if (_op == Op::Weather)
    {
        Stack().Close(kClientSlot);
        return WeatherAttemptFailed(reason, true);   // a failed httpGet is worth another try
    }
    Finish(0x94, std::vector<uint8_t>(7, 0), "get:" + reason);
}

void ZiFiNativeModule::WeatherFail(const std::string& reason)
{
    Stack().Close(kClientSlot);
    _wxBody.clear();
    Finish(0xA4, {0, zifiweather::kRecordVersion}, "weather:" + reason);
}

void ZiFiNativeModule::StartWeather()
{
    // WeatherService::get
    const std::string city = IniValue("city");
    const std::string country = IniValue("country");
    const std::string zip = IniValue("zip");
    const bool byCity = !city.empty();
    if (!byCity && (zip.empty() || country.empty()))
        return WeatherFail("no city in ini");
    const std::string key = byCity ? "city:" + country + "/" + city : "zip:" + country + "/" + zip;
    if (key.size() >= 96 * 2 + 8)   // char locationKey_[kIniValueSize * 2 + 8]
        return WeatherFail("city too long");
    if (key != _wxKey)
    {
        _wxKey = key;
        _wxHaveCoords = false;
        _wxUnknown = false;
    }
    if (_wxUnknown)
        return WeatherFail(byCity ? "city: not found" : "zip: not found");
    if (GetWifi() != Wifi::GotIp)
        return WeatherFail("no wifi");
    _wxBudgetEnd = Now() + MicrosToT(zifiweather::kBudgetUs);
    _wxStage = _wxHaveCoords ? WeatherStage::Forecast : byCity ? WeatherStage::City : WeatherStage::Zip;
    // The geocoder paths are built (and may not fit) before any download
    std::string path;
    if (_wxStage == WeatherStage::City && !zifiweather::CityPath(city, country, path))
        return WeatherFail("city too long");
    if (_wxStage == WeatherStage::Zip && !zifiweather::ZipPath(country, zip, path))
        return WeatherFail("zip path too long");
    _wxAttempt = 0;
    WeatherRequest();
}

void ZiFiNativeModule::WeatherRequest()
{
    // download(): a spent budget starts no request at all
    if (_wxAttempt == 0 && Now() >= _wxBudgetEnd)
    {
        _wxStatus = 0;
        _wxAttempt = zifiweather::kHttpAttempts;
        return WeatherAttemptFailed("timeout", false);
    }
    std::string host, path;
    switch (_wxStage)
    {
        case WeatherStage::City:
            host = zifiweather::kCityHost;
            zifiweather::CityPath(IniValue("city"), IniValue("country"), path);
            break;
        case WeatherStage::Zip:
            host = zifiweather::kZipHost;
            zifiweather::ZipPath(IniValue("country"), IniValue("zip"), path);
            break;
        case WeatherStage::Forecast:
            host = zifiweather::kMeteoHost;
            path = zifiweather::ForecastPath(_wxLatitude, _wxLongitude);
            break;
    }
    ++_wxAttempt;
    _wxStatus = 0;
    _wxBody.clear();
    _wxContentLength = 0;
    // The NET_HTTP_GET payload shape: host\0 port path\0 (the HTTP part reads it from _request)
    _request.assign(host.begin(), host.end());
    _request.push_back(0);
    Le16(_request, zifiweather::kHttpPort);
    _request.insert(_request.end(), path.begin(), path.end());
    _request.push_back(0);
    _redirects = 0;
    _phase = Phase::Resolve;
    _opDeadline = Now() + MicrosToT(kConnectTimeoutUs);
    StartHttpGet(_request);
}

void ZiFiNativeModule::WeatherHeader(uint16_t status, uint32_t contentLength, bool lengthKnown)
{
    // downloadOnce: the status, then the body into a 4 KiB buffer
    _wxStatus = status;
    if (status < 200 || status >= 300)
    {
        Stack().Close(kClientSlot);
        return WeatherAttemptFailed("http " + std::to_string(status), status >= 500 || status == 429);
    }
    const uint32_t length = lengthKnown ? contentLength : 0;
    if (length >= zifiweather::kBodyCapacity)
    {
        Stack().Close(kClientSlot);
        return WeatherAttemptFailed("reply too long", false);
    }
    _wxContentLength = length;
    _bodyLengthKnown = lengthKnown;
    _bodyExpected = contentLength;
    _bodyReceived = 0;
    _phase = Phase::Body;
    _wxBodyEnd = Now() + MicrosToT(zifiweather::kBodyTimeoutUs);
    _opDeadline = _wxBodyEnd;
    WeatherBody();
}

void ZiFiNativeModule::WeatherBody()
{
    if (_op != Op::Weather || _phase != Phase::Body)
        return;
    while (true)
    {
        // NetClient::receive: Content-Length ends the body without waiting for the close
        if (_bodyLengthKnown && _bodyReceived >= _bodyExpected)
            return WeatherBodyDone();
        const bool valid = Stack().Valid(kClientSlot);
        const size_t have = valid ? Stack().GetSlot(kClientSlot).rx.size() : 0;
        if (have == 0)
        {
            if (!valid || Stack().GetSlot(kClientSlot).finSeen)
                return WeatherBodyDone();   // EOF
            return;                         // more to come (or the 15 s timeout)
        }
        size_t wanted = std::min(have, zifiweather::kBodyCapacity - 1 - _wxBody.size());
        if (_bodyLengthKnown)
            wanted = std::min<size_t>(wanted, _bodyExpected - _bodyReceived);
        const std::vector<uint8_t> data = Stack().Read(kClientSlot, static_cast<uint32_t>(wanted));
        _wxBody.append(data.begin(), data.end());
        _bodyReceived += static_cast<uint32_t>(data.size());
        if (_wxBody.size() >= zifiweather::kBodyCapacity - 1)
        {
            Stack().Close(kClientSlot);
            return WeatherAttemptFailed("reply too long", false);
        }
        if (_wxContentLength != 0 && _wxBody.size() >= _wxContentLength)
            return WeatherBodyDone();
    }
}

void ZiFiNativeModule::WeatherBodyDone()
{
    Stack().Close(kClientSlot);
    if (_wxBody.empty())
        return WeatherAttemptFailed("empty reply", true);
    const std::string body = std::move(_wxBody);
    _wxBody.clear();
    std::string error;
    zifiweather::GeoResult geo;
    switch (_wxStage)
    {
        case WeatherStage::City:
        {
            bool notFound = false;
            if (!zifiweather::ParseCitySearch(body, geo, notFound, error))
            {
                _wxUnknown = notFound;   // no such name: not asked again
                return WeatherFail(error);
            }
            break;
        }
        case WeatherStage::Zip:
            if (!zifiweather::ParseZippopotam(body, geo, error))
                return WeatherFail(error);
            break;
        case WeatherStage::Forecast:
        {
            std::vector<uint8_t> record;
            if (!zifiweather::ParseOpenMeteo(body, _wxPlace, record, error))
                return WeatherFail(error);
            return Finish(0xA4, record);
        }
    }
    // setPlace, then the forecast (a new download: its own budget check, its own attempts)
    _wxLatitude = geo.latitude;
    _wxLongitude = geo.longitude;
    _wxPlace = geo.place;
    _wxHaveCoords = true;
    _wxStage = WeatherStage::Forecast;
    _wxAttempt = 0;
    WeatherRequest();
}

void ZiFiNativeModule::WeatherAttemptFailed(const std::string& reason, bool retryable)
{
    // download(): up to three attempts, 1 s then 2 s apart, none started after the budget
    if (retryable && _wxAttempt < zifiweather::kHttpAttempts && Now() < _wxBudgetEnd)
    {
        _phase = Phase::Retry;
        _opDeadline = Now() + MicrosToT(1000000ull * _wxAttempt);
        return;
    }
    switch (_wxStage)
    {
        case WeatherStage::City: return WeatherFail("city: " + reason);
        case WeatherStage::Zip:
            if (_wxStatus == 404)
            {
                _wxUnknown = true;   // the directory does not know the code: asking again will not help
                return WeatherFail("zip: not found");
            }
            return WeatherFail("zip: " + reason);
        case WeatherStage::Forecast: return WeatherFail("meteo: " + reason);
    }
}

// --- System -----------------------------------------------------------------------------------------------------

void ZiFiNativeModule::SysInfo()
{
    // handleSysInfo: the same fields as the board reports; the numbers are a fresh module's (fixed: no heap here)
    char text[400];
    if (_variant == Variant::S3)
    {
        const std::string proxy = _proxyStatus == 1   ? _proxyHost + ":" + std::to_string(_proxyPort)
                                  : _proxyStatus == 2 ? std::string("UNREACHABLE")
                                                      : std::string("OFF");
        std::snprintf(text, sizeof(text),
                      "RAM:%uB PSRAM:%u/%uB Flash:%uB Sketch:%uB RST:%u CORE:UART+VFS1/NET+EVT0 NETSTK:%uB CTRL:%s "
                      "VFSBUF:%s:%u+%u PROXY:%s FW:%s",
                      214528u, 1918476u, 2097152u, 4194304u, 1384272u, _softRestart ? 3u : 1u, 9172u, "PSRAM",
                      "PSRAM", 65536u, 65536u, proxy.c_str(), FirmwareVersion(_variant));
    }
    else
    {
        std::snprintf(text, sizeof(text), "RAM:%uB Flash:%uB Sketch:%uB OTA:%uB CFG:%08lX RST:%u FW:%s", 27464u,
                      1048576u, 431824u, 81920u, static_cast<unsigned long>(_iniCrc), _softRestart ? 4u : 0u,
                      FirmwareVersion(_variant));
    }
    const std::string s(text);
    SendFrame(kRespSysInfo, std::vector<uint8_t>(s.begin(), s.end()));
    _lastStep = 8;   // native plugins take step 8 as "SYS_INFO received"
    ClearError();
}

// --- Events -----------------------------------------------------------------------------------------------------

void ZiFiNativeModule::OnStackDone(const EspStack::Done& done)
{
    if (_sntpPhase && (_op == Op::None || _op == Op::Deferred))
    {
        // The background SNTP (S3)
        if (done.kind == EspStack::Done::Kind::Resolve && _sntpPhase == 1)
        {
            if (!done.addr)
            {
                _sntpPhase = 0;
                _sntpNextAt = Now() + MicrosToT(kSntpRetryUs);
                return;
            }
            std::vector<uint8_t> request(48, 0);
            request[0] = 0x23;   // lwIP sntp: LI 0, version 4, client
            Stack().Query(NetEndpoint{done.addr, 123}, request);
            _sntpPhase = 2;
            _sntpDeadline = Now() + MicrosToT(kSntpReplyUs);
            return;
        }
        if (done.kind == EspStack::Done::Kind::Query && _sntpPhase == 2)
        {
            const std::vector<uint8_t>& r = done.data;
            _sntpPhase = 0;
            const bool valid = r.size() >= 48 && (r[0] >> 6) != 3 && (r[0] & 7) == 4 && r[1] != 0;
            if (!valid)
            {
                _sntpNextAt = Now() + MicrosToT(kSntpRetryUs);
                return;
            }
            uint64_t ntp = (static_cast<uint32_t>(r[40]) << 24) | (static_cast<uint32_t>(r[41]) << 16) |
                           (static_cast<uint32_t>(r[42]) << 8) | r[43];
            if (ntp < kNtpUnixOffset)
                ntp += 1ull << 32;
            _clockUnix = static_cast<int64_t>(ntp - kNtpUnixOffset);
            _clockAt = Now();
            _clockValid = true;
            _sntpNextAt = Now() + MicrosToT(kSntpUpdateUs);
            return;
        }
    }
    if (_op == Op::None || _op == Op::Deferred)
        return;
    const std::vector<uint8_t> none7(7, 0);
    if (done.kind == EspStack::Done::Kind::Resolve)
    {
        if (_phase != Phase::Resolve && _phase != Phase::Proxy)
            return;
        if (!done.addr)
        {
            switch (_op)
            {
                case Op::Open: return Finish(0x90, {0}, "open:connect failed");
                case Op::HttpGet:
                case Op::Weather: return HttpFail("connect failed");
                case Op::Probe: return Finish(0xA1, {0, 0, 0});
                case Op::Ntp: return Finish(kRespNetNtp, std::vector<uint8_t>(14, '0'), "ntp:ntp dns");
                case Op::Join:
                    _proxyStatus = 2;
                    return Finish(_iniJoin ? kRespWifiIni : kRespWifiConnect, WifiResult(true));
                default: return;
            }
        }
        _opAddr = done.addr;
        if (_op == Op::Ntp)
        {
            std::vector<uint8_t> request(48, 0);
            request[0] = 0x1B;   // LI 0, version 3, client
            Stack().Query(NetEndpoint{done.addr, 123}, request);
            _phase = Phase::Query;
            _opDeadline = Now() + MicrosToT(kNtpReplyTimeoutUs);
            return;
        }
        const int slot = _op == Op::Probe || _op == Op::Join ? kProbeSlot : kClientSlot;
        Stack().Close(slot);
        if (Stack().OpenAt(slot, true) < 0)
            return;
        Stack().Connect(slot, NetEndpoint{done.addr, _opPort});
        if (_phase == Phase::Resolve)
            _phase = Phase::Connect;
        return;
    }
    if (done.kind == EspStack::Done::Kind::Connect)
    {
        const bool ok = done.status == NetEventStatus::Ok;
        if (_op == Op::Probe || _op == Op::Join)
        {
            if (done.slot != kProbeSlot)
                return;
            Stack().Close(kProbeSlot);
            if (_op == Op::Join)
            {
                _proxyStatus = ok ? 1 : 2;
                return Finish(_iniJoin ? kRespWifiIni : kRespWifiConnect, WifiResult(true));
            }
            const uint64_t ms = (Now() - _opStart) * 1000 / std::max<uint64_t>(1, MicrosToT(1000000));
            const uint16_t elapsed = static_cast<uint16_t>(std::min<uint64_t>(ms, 0xFFFF));
            return Finish(0xA1, ok ? std::vector<uint8_t>{1, static_cast<uint8_t>(elapsed), static_cast<uint8_t>(elapsed >> 8)}
                                   : std::vector<uint8_t>{0, 0, 0});
        }
        if (done.slot != kClientSlot || _phase != Phase::Connect)
            return;
        if (!ok)
        {
            Stack().Close(kClientSlot);
            return _op == Op::Open ? Finish(0x90, {0}, "open:connect failed") : HttpFail("connect failed");
        }
        if (_op == Op::Open)
            return Finish(0x90, {1});
        return HttpConnect();
    }
    if (done.kind == EspStack::Done::Kind::Query && _op == Op::Ntp && _phase == Phase::Query)
    {
        const std::vector<uint8_t>& r = done.data;
        std::string digits(14, '0');
        if (r.size() < 48)
            return Finish(kRespNetNtp, std::vector<uint8_t>(digits.begin(), digits.end()), "ntp:ntp short");
        const uint8_t leap = r[0] >> 6, mode = r[0] & 7;
        if (_variant == Variant::S3 && (leap == 3 || (mode != 4 && mode != 5) || r[1] == 0))
            return Finish(kRespNetNtp, std::vector<uint8_t>(digits.begin(), digits.end()), "ntp:ntp invalid");
        const uint32_t ntp = (static_cast<uint32_t>(r[40]) << 24) | (static_cast<uint32_t>(r[41]) << 16) |
                             (static_cast<uint32_t>(r[42]) << 8) | r[43];
        if (!FormatNtp(ntp, _timeZone, digits))
            return Finish(kRespNetNtp, std::vector<uint8_t>(14, '0'), "ntp:ntp year");
        return Finish(kRespNetNtp, std::vector<uint8_t>(digits.begin(), digits.end()));
    }
}

void ZiFiNativeModule::OnStackData(int slot)
{
    if (slot >= ZiFiFtpServer::kListenSlot)
    {
        _bridgePoll = true;   // the file servers read it at the frame boundary (never inside the delivery)
        return;
    }
    if (slot != kClientSlot || !HttpOp())
        return;
    if (_op == Op::Weather && _phase == Phase::Body)
        WeatherBody();
    else
        HttpHeader();
    if (_op == Op::None)
        Process();   // ESP01S: the requests that waited behind it
}

void ZiFiNativeModule::Timeout()
{
    const std::vector<uint8_t> none7(7, 0);
    switch (_op)
    {
        case Op::Join:
            if (_phase == Phase::Proxy || _phase == Phase::Resolve || _phase == Phase::Connect)
            {
                Stack().Close(kProbeSlot);
                _proxyStatus = 2;   // probe timeout: the proxy is off
                return Finish(_iniJoin ? kRespWifiIni : kRespWifiConnect, WifiResult(true));
            }
            return JoinDone(false);
        case Op::Open:
            Stack().Close(kClientSlot);
            return Finish(0x90, {0}, "open:connect failed");
        case Op::HttpGet:
            Stack().Close(kClientSlot);
            return HttpFail(_phase == Phase::Header ? "header timeout" : "connect failed");
        case Op::Weather:
            if (_phase == Phase::Retry)
                return WeatherRequest();
            if (_phase == Phase::Body)
            {
                Stack().Close(kClientSlot);
                return WeatherAttemptFailed("reply timeout", true);
            }
            Stack().Close(kClientSlot);
            return HttpFail(_phase == Phase::Header ? "header timeout" : "connect failed");
        case Op::Probe:
            Stack().Close(kProbeSlot);
            return Finish(0xA1, {0, 0, 0});
        case Op::Ntp:
            return Finish(kRespNetNtp, std::vector<uint8_t>(14, '0'),
                          _phase == Phase::Query ? "ntp:ntp timeout" : "ntp:ntp dns");
        case Op::Boot:
            _op = Op::None;
            _rx.clear();
            _rxSeen = 0;
            _held = 0;
            _lastStep = 0;
            _lastError.clear();
            _proxyStatus = 0;
            _softRestart = true;
            if (!Ssid().empty())
                Join(Ssid());   // the saved zifi.ini joins again (WiFi.begin at setup)
            return;
        case Op::Deferred:
        case Op::None: return;
    }
}

void ZiFiNativeModule::OnFrame()
{
    if (!Running())
        return;
    EspModule::OnFrame();
    if (_op == Op::Join && _phase == Phase::Join && GetWifi() == Wifi::GotIp)
        JoinDone(true);
    if (_op != Op::None && Now() >= _opDeadline)
        Timeout();
    _vfs.Tick();
    BridgePoll();   // S3: the network core runs while the UART core waits for the Z80; E01: only the STOR hook
    SntpTick();
    if (_variant == Variant::S3 && _ftp->Running() && Now() >= _nextSignalAt)
    {
        // The Wi-Fi signal bar while FTP runs (formatWifiSignal; the virtual AP's RSSI)
        const int rssi = GetWifi() == Wifi::GotIp ? kVirtualRssi : -127;
        const int percent = GetWifi() != Wifi::GotIp ? 0 : rssi <= -90 ? 0 : rssi >= -50 ? 100 : (rssi + 90) * 100 / 40;
        const int filled = (percent * 16 + 50) / 100;
        std::string bar = "Wi-Fi [";
        for (int i = 0; i < 16; ++i)
            bar.push_back(i < filled ? '#' : '.');
        char tail[16];
        std::snprintf(tail, sizeof(tail), "] %3d%%", percent);
        bar += tail;
        BridgeEvent(kEventWifiSignal, std::vector<uint8_t>(bar.begin(), bar.end()));
        _nextSignalAt = Now() + MicrosToT(kWifiSignalIntervalUs);
    }
    FlushEvents();
    DrainTx();
    Process();
    // A partial frame left for 500 ms is dropped (checkTimeout)
    if (!(Busy() && _variant == Variant::Esp01s) && _op != Op::Boot && _rx.size() > _held &&
        Now() >= _lastByteAt + MicrosToT(kFrameTimeoutUs))
    {
        _rx.erase(_rx.begin() + _held, _rx.end());
        _rxSeen = _rx.size();
        ++_resyncs;
    }
}

void ZiFiNativeModule::OnHardwareReset()
{
    ForgetBridge();
    _op = Op::None;
    _phase = Phase::None;
    _held = 0;
    _rxSeen = 0;
    _request.clear();
    _bodyActive = false;
    _lastStep = 0;
    _lastError.clear();
    _proxyStatus = 0;
    ResetWeather();
}

void ZiFiNativeModule::ResetWeather()
{
    // WeatherService lives in RAM: a restart forgets the place (zifi.ini stays in flash)
    _wxKey.clear();
    _wxHaveCoords = false;
    _wxUnknown = false;
    _wxLatitude = _wxLongitude = 0.0f;
    _wxPlace.clear();
    _wxBody.clear();
    _wxAttempt = 0;
    _wxStatus = 0;
}

void ZiFiNativeModule::OnHardwareBoot()
{
    _softRestart = false;   // the RST pin: a power-on style reset
    _op = Op::Boot;
    _opStart = Now();
    _opDeadline = Now() + MicrosToT(kBootUs);
}

// --- The file bridge (Z3b) --------------------------------------------------------------------------------------

uint8_t ZiFiNativeModule::TakeByte()
{
    const uint8_t b = EspModule::TakeByte();
    DrainTx();
    return b;
}

void ZiFiNativeModule::DrainTx()
{
    while (!_txBacklog.empty() && Pending() + _txBacklog.front().size() <= kTxLimit)
    {
        Send(_txBacklog.front());
        _txBacklog.pop_front();
    }
}

void ZiFiNativeModule::BridgeEvent(uint8_t cmd, const std::vector<uint8_t>& data)
{
    // S3: events cross from the network core through an 8-deep queue (a full queue drops the event: they only
    // indicate); E01: transport_.send at once
    std::vector<uint8_t> f = {cmd};
    f.insert(f.end(), data.begin(), data.end());
    if (_variant == Variant::S3)
    {
        if (_events.size() < kEventQueue)
            _events.push_back(std::move(f));
        FlushEvents();
        return;
    }
    SendFrame(cmd, data);
}

void ZiFiNativeModule::FlushEvents()
{
    // pollNetworkEvent runs between UART polls: not while the VFS client is inside waitFor
    while (!_events.empty() && !_vfs.Waiting() && _op != Op::Boot)
    {
        const std::vector<uint8_t> f = std::move(_events.front());
        _events.pop_front();
        SendFrame(f[0], f.data() + 1, f.size() - 1);
    }
}

bool ZiFiNativeModule::BridgeClock(int64_t& unixNow) const
{
    if (!_clockValid)
        return false;
    unixNow = _clockUnix + static_cast<int64_t>((Now() - _clockAt) / std::max<uint64_t>(1, MicrosToT(1000000)));
    return true;
}

void ZiFiNativeModule::SntpTick()
{
    if (_variant != Variant::S3 || GetWifi() != Wifi::GotIp || _op != Op::None)
        return;
    if (_sntpPhase && Now() >= _sntpDeadline)
    {
        _sntpPhase = 0;
        _sntpNextAt = Now() + MicrosToT(kSntpRetryUs);
    }
    if (_sntpPhase || _sntpNextAt == 0 || Now() < _sntpNextAt)
        return;
    _sntpPhase = 1;
    _sntpDeadline = Now() + MicrosToT(kSntpReplyUs);
    Stack().Resolve("pool.ntp.org");
}

uint16_t ZiFiNativeModule::HostPortFor(uint16_t guestPort) const
{
    const VirtualNetwork* network = Stack().Network();
    return network ? network->HostPortFor(guestPort) : 0;
}

void ZiFiNativeModule::BridgePoll()
{
    _bridgePoll = false;
    // S3: the network core runs the servers between its requests (a request in progress holds them); E01: the
    // loop is inside a blocking command
    if (_op != Op::None && _op != Op::Deferred)
        return;
    if (_ftp->Running())
        _ftp->Poll();
    FlushEvents();
    if (_op == Op::Deferred && !_ftp->JobActive())
        RunDeferred();
}

void ZiFiNativeModule::RunDeferred()
{
    // The network core takes the queued request: the frame is at the head of the buffer, its ACK went out
    if (_held < 5 || _rx.size() < _held)
    {
        _op = Op::None;
        _held = 0;
        return;
    }
    const uint8_t cmd = _rx[1];
    const std::vector<uint8_t> payload(_rx.begin() + 4, _rx.begin() + (_held - 1));
    const size_t frameLength = _held;
    _op = Op::None;
    _phase = Phase::None;
    _held = 0;
    _deferredRun = true;
    Network(cmd, payload, frameLength);
    _deferredRun = false;
    _rxSeen = std::min(_rxSeen, _rx.size());
}

void ZiFiNativeModule::StartFtp(const std::vector<uint8_t>& payload)
{
    // processFtpStart (S3) / handleFtpStart (E01): the TCP client closes, the file services restart
    Stack().Close(kClientSlot);
    _bodyActive = false;
    StopFileServers();
    uint16_t port = 0;
    std::string error;
    if (!_ftp->Start(payload, port, error))
    {
        ReportError((_variant == Variant::S3 ? "ftp:" : "ftp/webdav:") + error);
        return SendFrame(0x86, {0, 0, 0});
    }
    // ESP01S: the WebDAV server on port 80 starts with FTP here (webdav_.start; the zifi-plugins work)
    _nextSignalAt = Now();
    ClearError();
    SendFrame(0x86, {1, static_cast<uint8_t>(port), static_cast<uint8_t>(port >> 8)});
}

void ZiFiNativeModule::StopFileServers()
{
    if (_ftp->Running())
        _ftp->Stop();
    // ESP01S stopStorageServices: the WebDAV server stops here too (the zifi-plugins work)
}

void ZiFiNativeModule::ForgetBridge()
{
    _ftp->Forget();
    _vfs.Reset();
    _events.clear();
    _txBacklog.clear();
    _bridgePoll = false;
    _sntpPhase = 0;
    _sntpNextAt = 0;
}

// --- TTD --------------------------------------------------------------------------------------------------------

namespace
{
constexpr uint8_t kStateVersion = 1;
constexpr size_t kProxyBytes = 64;
}  // namespace

void ZiFiNativeModule::SaveFirmware(netstate::EspModuleState& out) const
{
    uint8_t* f = out.firmware;
    size_t p = 0;
    auto put8 = [&](uint8_t v) { f[p++] = v; };
    auto put16 = [&](uint16_t v) { put8(static_cast<uint8_t>(v)); put8(static_cast<uint8_t>(v >> 8)); };
    auto put32 = [&](uint32_t v) { put16(static_cast<uint16_t>(v)); put16(static_cast<uint16_t>(v >> 16)); };
    auto put64 = [&](uint64_t v) { put32(static_cast<uint32_t>(v)); put32(static_cast<uint32_t>(v >> 32)); };
    put8(kStateVersion);
    put8(static_cast<uint8_t>(_op));
    put8(static_cast<uint8_t>(_phase));
    put8(_opCmd);
    put16(_held);
    put8(_lastStep);
    put8(static_cast<uint8_t>((_bodyActive ? 1 : 0) | (_bodyLengthKnown ? 2 : 0) | (_viaProxy ? 4 : 0) | (_iniJoin ? 8 : 0) |
                              (_softRestart ? 16 : 0) | (_passwordKnown ? 32 : 0)));
    put64(_opDeadline);
    put64(_opStart);
    put64(_lastByteAt);
    put32(_bodyExpected);
    put32(_bodyReceived);
    put64(_bodyActivityAt);
    put8(static_cast<uint8_t>(_timeZone));
    put8(_proxyStatus);
    put16(_proxyPort);
    put32(_passwordHash);
    put32(_iniCrc);
    put16(static_cast<uint16_t>(_rxSeen));
    put8(_redirects);
    put8(static_cast<uint8_t>(std::min<size_t>(_lastError.size(), 63)));
    put32(_opAddr);
    put16(_opPort);
    put16(_httpPort);
    // 80 bytes so far
    std::memcpy(f + p, _lastError.data(), std::min<size_t>(_lastError.size(), 63));
    p += 64;
    const size_t proxy = std::min(_proxyHost.size(), kProxyBytes - 1);
    put8(static_cast<uint8_t>(proxy));
    std::memcpy(f + p, _proxyHost.data(), proxy);
    p += kProxyBytes;
    static_assert(80 + 64 + 1 + kProxyBytes <= static_cast<size_t>(netstate::kEspFirmware), "the native state fits");
}

void ZiFiNativeModule::LoadFirmware(const netstate::EspModuleState& in)
{
    const uint8_t* f = in.firmware;
    size_t p = 0;
    auto get8 = [&]() { return f[p++]; };
    auto get16 = [&]() { const uint16_t v = static_cast<uint16_t>(f[p] | (f[p + 1] << 8)); p += 2; return v; };
    auto get32 = [&]() { const uint32_t lo = get16(); return lo | (static_cast<uint32_t>(get16()) << 16); };
    auto get64 = [&]() { const uint64_t lo = get32(); return lo | (static_cast<uint64_t>(get32()) << 32); };
    ForgetBridge();   // the ZiFi blob (id 40, loaded next) brings the file bridge back
    if (get8() != kStateVersion)
        return;
    _op = static_cast<Op>(get8());
    _phase = static_cast<Phase>(get8());
    _opCmd = get8();
    _held = get16();
    _lastStep = get8();
    const uint8_t flags = get8();
    _bodyActive = flags & 1;
    _bodyLengthKnown = flags & 2;
    _viaProxy = flags & 4;
    _iniJoin = flags & 8;
    _softRestart = flags & 16;
    _passwordKnown = flags & 32;
    _opDeadline = get64();
    _opStart = get64();
    _lastByteAt = get64();
    _bodyExpected = get32();
    _bodyReceived = get32();
    _bodyActivityAt = get64();
    _timeZone = static_cast<int8_t>(get8());
    _proxyStatus = get8();
    _proxyPort = get16();
    _passwordHash = get32();
    _iniCrc = get32();
    _rxSeen = get16();
    _redirects = get8();
    const uint8_t errorLength = get8();
    _opAddr = get32();
    _opPort = get16();
    _httpPort = get16();
    _lastError.assign(reinterpret_cast<const char*>(f + p), std::min<uint8_t>(errorLength, 63));
    p += 64;
    const uint8_t proxy = get8();
    _proxyHost.assign(reinterpret_cast<const char*>(f + p), std::min<size_t>(proxy, kProxyBytes - 1));
    ReloadRequest();
}

void ZiFiNativeModule::ReloadRequest()
{
    _request.clear();
    if (_held < 5 || _rx.size() < _held)
        return;
    _request.assign(_rx.begin() + 4, _rx.begin() + (_held - 1));
}

// --- TTD: the file bridge (the ZiFi blob's variable part) ---------------------------------------------------------

namespace
{
constexpr uint8_t kBridgeVersion = 1;

/// Received bytes by run: a journal reference (kind 1) or, for bytes that never came through the journal (tests),
/// the bytes themselves (kind 0)
void SaveRx(ZiFiStateWriter& w, const std::deque<EspStack::RxByte>& rx)
{
    std::vector<std::pair<size_t, size_t>> runs;   // [begin, end)
    for (size_t i = 0; i < rx.size();)
    {
        size_t e = i + 1;
        while (e < rx.size() && rx[e].source == rx[i].source &&
               (rx[i].source == 0 || rx[e].offset == rx[e - 1].offset + 1))
            ++e;
        runs.emplace_back(i, e);
        i = e;
    }
    w.U32(static_cast<uint32_t>(runs.size()));
    for (const auto& [b, e] : runs)
    {
        if (rx[b].source == 0)
        {
            w.U8(0);
            std::vector<uint8_t> bytes;
            for (size_t i = b; i < e; ++i)
                bytes.push_back(rx[i].value);
            w.Bytes(bytes);
        }
        else
        {
            w.U8(1);
            w.U32(rx[b].source);
            w.U32(rx[b].offset);
            w.U32(static_cast<uint32_t>(e - b));
        }
    }
}

bool LoadRx(ZiFiStateReader& r, const EspStack::ByteSource& bytes, std::deque<EspStack::RxByte>& rx)
{
    bool complete = true;
    const uint32_t runs = r.U32();
    for (uint32_t i = 0; i < runs && r.Ok(); ++i)
    {
        if (r.U8() == 0)
        {
            for (uint8_t b : r.Bytes())
                rx.push_back({b, 0, 0});
            continue;
        }
        const uint32_t source = r.U32(), offset = r.U32(), length = r.U32();
        std::vector<uint8_t> chunk;
        if (!bytes || !bytes(source, offset, length, chunk) || chunk.size() != length)
        {
            complete = false;
            continue;
        }
        for (uint32_t k = 0; k < length; ++k)
            rx.push_back({chunk[k], source, offset + k});
    }
    return complete;
}
}  // namespace

void ZiFiNativeModule::SaveBridge(std::vector<uint8_t>& out) const
{
    ZiFiStateWriter w(out);
    w.U8(kBridgeVersion);
    _vfs.Save(w);
    _ftp->Save(w);
    w.U32(static_cast<uint32_t>(_events.size()));
    for (const std::vector<uint8_t>& e : _events)
        w.Bytes(e);
    w.U32(static_cast<uint32_t>(_txBacklog.size()));
    for (const std::vector<uint8_t>& f : _txBacklog)
        w.Bytes(f);
    w.Bool(_bridgePoll);
    w.U32(_droppedWhileWaiting);
    w.U64(_nextSignalAt);
    w.Bool(_clockValid);
    w.I64(_clockUnix);
    w.U64(_clockAt);
    w.U8(_sntpPhase);
    w.U64(_sntpNextAt);
    w.U64(_sntpDeadline);
    // The socket slots beyond the eight of netstate::EspStackState
    const EspStack& stack = Stack();
    const int first = netstate::kEspSlots;
    w.U8(static_cast<uint8_t>(std::max(0, stack.SlotCount() - first)));
    for (int i = first; i < stack.SlotCount(); ++i)
    {
        const EspStack::Slot& s = stack.GetSlot(i);
        w.U8(static_cast<uint8_t>(s.state));
        w.Bool(s.connecting), w.Bool(s.finSeen);
        w.U16(s.vnetId), w.U16(s.localPort), w.U16(s.waitingId);
        w.U32(s.remote.addr), w.U16(s.remote.port);
        SaveRx(w, s.rx);
        w.U32(static_cast<uint32_t>(s.pending.size()));
        for (const EspStack::Pending& p : s.pending)
        {
            w.U16(p.vnetId);
            w.U32(p.peer.addr), w.U16(p.peer.port);
            w.Bool(p.finSeen);
            SaveRx(w, p.rx);
        }
        w.Bool(stack.RearmQueued(i));
    }
    SaveWeather(w);   // keep last: the weather / zifi.ini section
}

bool ZiFiNativeModule::LoadBridge(const uint8_t* data, size_t length, const EspStack::ByteSource& bytes)
{
    ForgetBridge();
    ResetWeather();
    _iniText.clear();
    if (!data || !length)
        return true;
    ZiFiStateReader r(data, length);
    if (r.U8() != kBridgeVersion)
        return false;
    bool ok = _vfs.Load(r) && _ftp->Load(r);
    const uint32_t events = r.U32();
    for (uint32_t i = 0; i < events && i < kEventQueue && r.Ok(); ++i)
        _events.push_back(r.Bytes(64));
    const uint32_t tx = r.U32();
    for (uint32_t i = 0; i < tx && i < 256 && r.Ok(); ++i)
        _txBacklog.push_back(r.Bytes(kMaxPayload + 5));
    _bridgePoll = r.Bool();
    _droppedWhileWaiting = r.U32();
    _nextSignalAt = r.U64();
    _clockValid = r.Bool();
    _clockUnix = r.I64();
    _clockAt = r.U64();
    _sntpPhase = r.U8();
    _sntpNextAt = r.U64();
    _sntpDeadline = r.U64();
    EspStack& stack = Stack();
    const int extra = r.U8();
    for (int k = 0; k < extra && r.Ok(); ++k)
    {
        const int i = netstate::kEspSlots + k;
        EspStack::Slot s;
        const uint8_t state = r.U8();
        s.state = state <= static_cast<uint8_t>(EspStack::State::Listen) ? static_cast<EspStack::State>(state)
                                                                         : EspStack::State::Free;
        s.connecting = r.Bool(), s.finSeen = r.Bool();
        s.vnetId = r.U16(), s.localPort = r.U16(), s.waitingId = r.U16();
        s.remote.addr = r.U32(), s.remote.port = r.U16();
        ok = LoadRx(r, bytes, s.rx) && ok;
        const uint32_t pending = r.U32();
        for (uint32_t p = 0; p < pending && p < 64 && r.Ok(); ++p)
        {
            EspStack::Pending pd;
            pd.vnetId = r.U16();
            pd.peer.addr = r.U32(), pd.peer.port = r.U16();
            pd.finSeen = r.Bool();
            ok = LoadRx(r, bytes, pd.rx) && ok;
            s.pending.push_back(std::move(pd));
        }
        const bool rearm = r.Bool();
        if (i < stack.SlotCount())
        {
            stack.RestoreSlot(i, std::move(s));
            if (rearm)
                stack.QueueRearm(i);
        }
    }
    ok = LoadWeather(r) && ok;   // keep last: the weather / zifi.ini section
    RebindSockets();
    return ok && r.Ok();
}

void ZiFiNativeModule::SaveWeather(ZiFiStateWriter& w) const
{
    // The saved zifi.ini (flash), WeatherService's place (RAM) and a WEATHER_GET in progress
    w.Bytes(_iniText);
    w.Str(_wxKey);
    w.Bool(_wxHaveCoords);
    w.Bool(_wxUnknown);
    uint32_t lat = 0, lon = 0;
    std::memcpy(&lat, &_wxLatitude, 4);
    std::memcpy(&lon, &_wxLongitude, 4);
    w.U32(lat);
    w.U32(lon);
    w.Str(_wxPlace);
    w.U8(static_cast<uint8_t>(_wxStage));
    w.U8(_wxAttempt);
    w.U16(_wxStatus);
    w.U64(_wxBudgetEnd);
    w.U64(_wxBodyEnd);
    w.U32(_wxContentLength);
    w.Str(_wxBody);
    w.Bytes(_op == Op::Weather ? _request : std::vector<uint8_t>());   // the request is not the held frame
}

bool ZiFiNativeModule::LoadWeather(ZiFiStateReader& r)
{
    _iniText = r.Bytes(4096);
    _wxKey = r.Str(256);
    _wxHaveCoords = r.Bool();
    _wxUnknown = r.Bool();
    const uint32_t lat = r.U32(), lon = r.U32();
    std::memcpy(&_wxLatitude, &lat, 4);
    std::memcpy(&_wxLongitude, &lon, 4);
    _wxPlace = r.Str(256);
    const uint8_t stage = r.U8();
    _wxStage = stage <= static_cast<uint8_t>(WeatherStage::Forecast) ? static_cast<WeatherStage>(stage) : WeatherStage::City;
    _wxAttempt = r.U8();
    _wxStatus = r.U16();
    _wxBudgetEnd = r.U64();
    _wxBodyEnd = r.U64();
    _wxContentLength = r.U32();
    _wxBody = r.Str(zifiweather::kBodyCapacity);
    std::vector<uint8_t> request = r.Bytes(kMaxPayload);
    if (_op == Op::Weather)
        _request = std::move(request);
    return r.Ok();
}
