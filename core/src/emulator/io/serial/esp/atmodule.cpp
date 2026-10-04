#include "emulator/io/serial/esp/atmodule.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "emulator/io/network/virtualnetwork.h"

namespace
{
constexpr uint64_t kBootUs = 300000;       // RST: "ready" this long after the OK
constexpr uint64_t kPowerOnBootUs = 400000; // the RST pin released: ROM, second stage, AT start (the ROM's log runs at
                                            // 74880 baud and is not modeled)
constexpr uint64_t kScanUs = 1500000;
constexpr uint64_t kJoinFailUs = 15000000; // CWJAP to an access point that is not there
constexpr uint64_t kPingTimeoutUs = 2000000;
constexpr uint64_t kTransparentIdleUs = 20000;   // a transparent-mode packet: 20 ms without bytes
constexpr uint64_t kPlusGuardUs = 1000000;       // "+++" needs a second of silence around it
constexpr uint32_t kNtpUnixOffset = 2208988800u;  // 1900 -> 1970

std::string Trim(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
        ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        --e;
    return s.substr(b, e - b);
}

/// Split "a,"b,c",d" into fields; quotes removed, commas inside quotes kept
std::vector<std::string> Fields(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        if (c == '"')
        {
            quoted = !quoted;
            continue;
        }
        if (c == '\\' && quoted && i + 1 < s.size())
        {
            cur.push_back(s[++i]);
            continue;
        }
        if (c == ',' && !quoted)
        {
            out.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

bool Number(const std::string& s, uint32_t& out, uint32_t max = 0xFFFFFFFFu)
{
    const std::string t = Trim(s);
    if (t.empty() || t.size() > 10)
        return false;
    uint64_t v = 0;
    for (char c : t)
    {
        if (c < '0' || c > '9')
            return false;
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    if (v > max)
        return false;
    out = static_cast<uint32_t>(v);
    return true;
}

bool Ipv4(const std::string& s, uint32_t& out)
{
    unsigned a = 0, b = 0, c = 0, d = 0;
    char tail = 0;
    if (std::sscanf(s.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
        return false;
    out = NetIp(static_cast<uint8_t>(a), static_cast<uint8_t>(b), static_cast<uint8_t>(c), static_cast<uint8_t>(d));
    return true;
}

std::string MacText(const std::array<uint8_t, 6>& m)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    return buf;
}

/// An ICMP echo request with its checksum
std::vector<uint8_t> EchoRequest()
{
    std::vector<uint8_t> echo = {8, 0, 0, 0, 0x45, 0x53, 0, 1, 'u', 'n', 'r', 'e', 'a', 'l'};
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < echo.size(); i += 2)
        sum += static_cast<uint32_t>((echo[i] << 8) | echo[i + 1]);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    echo[2] = static_cast<uint8_t>(~sum >> 8);
    echo[3] = static_cast<uint8_t>(~sum);
    return echo;
}

/// Where AtModule's 2.x settings start in the firmware state (after the SNTP server name of up to 63 bytes)
constexpr size_t kExtraAt = 210;
static_assert(kExtraAt + 17 <= static_cast<size_t>(netstate::kEspFirmware), "the AT state fits its blob part");

std::string Upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

AtModule::AtModule(VirtualNetwork* network, Chip chip)
    : AtModule(network, chip == Chip::Esp8266 ? Firmware::Esp8266NonOs174 : Firmware::Esp32At220)
{
}

AtModule::AtModule(VirtualNetwork* network, Firmware firmware, const std::array<uint8_t, 6>* mac)
    : EspModule(network, ChipOf(firmware), kLinks + 1, mac), _firmware(firmware), _traits(&atdialect::TraitsOf(firmware))
{
    _textLog = true;
    // Power-up: the boot banner (the ROM log runs at 74880 baud and is not
    // modeled), "ready", then the saved access point joins
    Send("\r\nready\r\n", 200000);
    if (GetWifi() == Wifi::GotIp)
        Send("WIFI CONNECTED\r\nWIFI GOT IP\r\n", 0);
}

void AtModule::Error(uint32_t code)
{
    if (_traits->errCodes && _sysLog)
        Send(atdialect::ErrCodeLine(code));
    Send("\r\nERROR\r\n");
}

bool AtModule::LinkOpen(int n) const
{
    return n >= 0 && n < kLinks && Stack().Valid(n);
}

void AtModule::ResetSession()
{
    // Every link closes, the radio restarts; settings that are not saved reset
    Stack().Close(-1);
    for (Link& l : _links)
        l = Link();
    _echo = true;
    _mux = false;
    _dinfo = false;
    _passive = false;
    _transparentMode = false;
    _transparent = false;
    _sending = false;
    _holdUrc = false;
    _serverPort = 0;
    _rx.clear();
    _echoed = 0;
    _sysStore = true;
    _dnsManual = false;
    _dns[0] = _dns[1] = 0;
    _lapOptMask = 0x7FF;
    _maxConn = 5;
    _uartFlow = 3;
    _op = Op::None;
}

void AtModule::Boot(uint64_t afterUs)
{
    ResetSession();
    Leave();
    _op = Op::Boot;
    _opDeadline = Now() + MicrosToT(afterUs);
}

void AtModule::OnHardwareReset()
{
    ResetSession();
}

void AtModule::OnHardwareBoot()
{
    Boot(kPowerOnBootUs);
}

void AtModule::BootDone()
{
    _op = Op::None;
    _rx.clear();   // what came in while restarting is lost
    _echoed = 0;
    Send("\r\nready\r\n", 0);
    if (_autoConnect)
    {
        Join(kVirtualSsid);
        _op = Op::Join;
        _opLink = -1;   // the join after a boot: URCs only, no OK
        _opDeadline = Now() + MicrosToT(kJoinUs);
    }
}

void AtModule::OnFrame()
{
    if (!Running())
        return;
    EspModule::OnFrame();
    const uint64_t now = Now();
    if (_op == Op::Boot && now >= _opDeadline)
        BootDone();
    if (_op == Op::Join && now >= _opDeadline)
    {
        _op = Op::None;
        if (GetWifi() == Wifi::GotIp)
        {
            Send("WIFI CONNECTED\r\nWIFI GOT IP\r\n", 0);
            if (_opLink >= 0)
                Ok();
        }
        else if (_opLink >= 0)
        {
            // <error code> 3: cannot find the target AP
            Send("+CWJAP:3\r\n", 0);
            if (_traits->joinErrorForm)
                Error(atdialect::ErrorCode(atdialect::kSubExecFail));
            else
                Send("\r\nFAIL\r\n", 0);
        }
    }
    if (_op == Op::Scan && now >= _opDeadline)
    {
        _op = Op::None;
        // The fields AT+CWLAPOPT selects, in the firmware's order: ecn, ssid, rssi, mac, channel, freq offset,
        // freq calibration (then cipher / bgn / wps on 2.x: not listed here)
        // ESP-AT 2.x adds pairwise cipher, group cipher (4: CCMP), bgn (7: b/g/n), wps (0)
        const std::string fields[] = {"3", "\"" + std::string(kVirtualSsid) + "\"", std::to_string(kVirtualRssi),
                                      "\"52:54:00:12:35:02\"", std::to_string(kVirtualChannel), "-11", "0", "4", "4", "7", "0"};
        std::string line;
        for (int i = 0; i < (_traits->lapAllFields ? 11 : 7); ++i)
        {
            if (_lapOptMask & (1u << i))
                line += (line.empty() ? "" : ",") + fields[i];
        }
        Send("+CWLAP:(" + line + ")\r\n", 0);
        Ok();
    }
    if (_op == Op::Ping && now >= _opDeadline)
    {
        _op = Op::None;
        Send(_traits->ping2x ? "+PING:TIMEOUT\r\n\r\nERROR\r\n" : "+timeout\r\n\r\nERROR\r\n", 0);
    }
    if (_transparent && now >= _lastRxAt + MicrosToT(kTransparentIdleUs))
        FlushTransparent();
    if (_sntpEnabled && !_sntpSeconds && !_sntpResolving && _op == Op::None && GetWifi() == Wifi::GotIp)
        SntpQuery();
    Process();
    EmitUrcs();
}

void AtModule::Process()
{
    if (!Running())
        return;
    // Echo what arrived (the module echoes characters as it receives them)
    if (_echo && !_sending && !_transparent && _op != Op::Boot)
    {
        while (_echoed < _rx.size())
        {
            const uint8_t b = _rx[_echoed++];
            Send(&b, 1, 0);
        }
    }
    if (_rx.size() > _rxSeen)
        _lastRxAt = Now();   // new bytes (the buffer only grows by arrival)
    struct SeenGuard
    {
        AtModule& m;
        ~SeenGuard() { m._rxSeen = m._rx.size(); }
    } seen{*this};

    while (true)
    {
        if (_op == Op::Boot)
        {
            _rx.clear();
            _echoed = 0;
            return;
        }
        if (_transparent)
        {
            // "+++" alone after a second of silence ends the data phase
            if (_rx.size() >= kMaxSend)
                FlushTransparent();
            return;
        }
        if (_sending)
        {
            if (_rx.size() < _sendLength)
                return;
            FinishSend();
            continue;
        }
        // A command line ends with LF (CR LF from every client)
        const auto lf = std::find(_rx.begin(), _rx.end(), static_cast<uint8_t>('\n'));
        if (lf == _rx.end())
        {
            if (_rx.size() > 2048)
            {
                _rx.clear();   // a line that never ends
                _echoed = 0;
            }
            return;
        }
        std::string line(_rx.begin(), lf);
        const size_t used = static_cast<size_t>(lf - _rx.begin()) + 1;
        _rx.erase(_rx.begin(), _rx.begin() + static_cast<std::ptrdiff_t>(used));
        _echoed = _echoed > used ? _echoed - used : 0;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (_op != Op::None)
        {
            Send("busy p...\r\n", 0);   // the firmware drops a command while it works
            continue;
        }
        ++_requests;
        LogRequest(line);
        HandleLine(line);
    }
}

void AtModule::HandleLine(const std::string& raw)
{
    const std::string line = Trim(raw);
    if (line.empty())
        return Error(atdialect::ErrorCode(atdialect::kSubNoAt));
    const std::string upper = Upper(line);
    if (upper.rfind("AT", 0) != 0)
        return Error(atdialect::ErrorCode(atdialect::kSubNoAt));
    const size_t eq = upper.find('=');
    const std::string cmd = eq == std::string::npos ? upper : upper.substr(0, eq);
    const std::string args = eq == std::string::npos ? std::string() : line.substr(eq + 1);
    // _CUR / _DEF forms act like the plain one (nothing is stored in flash here); "AT+CIPSTA_CUR?" is a query too
    std::string base = cmd;
    const bool query = !base.empty() && base.back() == '?';
    if (query)
        base.pop_back();
    bool suffixed = false;
    for (const char* suffix : {"_CUR", "_DEF"})
    {
        const size_t n = std::strlen(suffix);
        if (base.size() > n && base.compare(base.size() - n, n, suffix) == 0)
        {
            base.erase(base.size() - n);
            suffixed = true;
        }
    }
    // ESP-AT has no _CUR / _DEF forms but UART_CUR / UART_DEF ("AT Command Set Comparison"); AT+SYSSTORE decides
    // what goes to flash
    if (suffixed && !_traits->suffixForms && base != "AT+UART")
        return Unsupported();

    if (base == "AT")
        return Ok();
    if (base == "ATE0" || base == "ATE1")
    {
        _echo = base == "ATE1";
        return Ok();
    }
    if (base == "AT+RST")
    {
        Ok();
        return Boot(kBootUs);
    }
    if (base == "AT+RESTORE")
    {
        Ok();
        _autoConnect = true;
        return Boot(kBootUs);
    }
    if (base == "AT+GMR")
    {
        Send(atdialect::Identity(_firmware, _flash));
        return Ok();
    }
    if (base == "AT+CWMODE")
    {
        if (query)
        {
            Send("+CWMODE:" + std::to_string(_cwMode) + "\r\n");
            return Ok();
        }
        // ESP-AT 2.x: AT+CWMODE=<mode>[,<auto_connect>]
        const std::vector<std::string> f = Fields(args);
        uint32_t m = 0, autoConnect = 0;
        if (f.empty() || f.size() > (At2() ? 2u : 1u) || !Number(f[0], m, 3) || m == 0 ||
            (f.size() == 2 && !Number(f[1], autoConnect, 1)))
            return Error();
        _cwMode = static_cast<uint8_t>(m);
        return Ok();
    }
    if (base == "AT+CWAUTOCONN")
    {
        uint32_t v = 0;
        if (!Number(args, v, 1))
            return Error();
        _autoConnect = v != 0;
        return Ok();
    }
    if (base == "AT+CWJAP")
    {
        if (query)
        {
            // ESP-AT 2.x adds <pci_en>,<reconn_interval>,<listen_interval>,<scan_mode>,<pmf> (their defaults)
            if (GetWifi() == Wifi::GotIp)
                Send("+CWJAP:\"" + Ssid() + "\",\"52:54:00:12:35:02\"," + std::to_string(kVirtualChannel) + "," +
                     std::to_string(kVirtualRssi) + (_traits->joinQuery2x ? ",0,1,3,0,0" : "") + "\r\n");
            else
                Send("No AP\r\n");
            return Ok();
        }
        return DoCwJap(args, eq == std::string::npos);
    }
    if (base == "AT+CWSTATE" && _traits->cwState)
    {
        if (!query)
            return Error(atdialect::ErrorCode(atdialect::kSubOpError));
        // 0 never started, 2 got an IPv4 address, 3 connecting, 4 disconnected
        const int state = GetWifi() == Wifi::GotIp ? 2 : GetWifi() == Wifi::Connecting ? 3 : Ssid().empty() ? 0 : 4;
        Send("+CWSTATE:" + std::to_string(state) + ",\"" + Ssid() + "\"\r\n");
        return Ok();
    }
    if (base == "AT+CWQAP")
    {
        Leave();
        Ok();
        return Send("WIFI DISCONNECT\r\n");
    }
    if (base == "AT+CWLAP")
    {
        _op = Op::Scan;
        _opDeadline = Now() + MicrosToT(kScanUs);
        return;
    }
    if (base == "AT+SYSSTORE")
    {
        // ESP-AT 2.1.0.0+; the kit's 2.2.1 binary lacks it (the kit tells 2.2.1 from 2.2.2 by this ERROR)
        if (!_traits->sysStore)
            return Unsupported();
        if (query)
        {
            Send("+SYSSTORE:" + std::string(_sysStore ? "1" : "0") + "\r\n");
            return Ok();
        }
        uint32_t v = 0;
        if (!Number(args, v, 1))
            return Error();
        _sysStore = v == 1;
        return Ok();
    }
    if (base == "AT+SYSLOG")
    {
        if (!_traits->sysLog)
            return Unsupported();
        if (query)
        {
            Send("+SYSLOG:" + std::string(_sysLog ? "1" : "0") + "\r\n");
            return Ok();
        }
        uint32_t v = 0;
        if (!Number(args, v, 1))
            return Error();
        _sysLog = v == 1;
        return Ok();
    }
    if (base == "AT+CWLAPOPT")
    {
        const std::vector<std::string> f = Fields(args);
        uint32_t sort = 0, mask = 0;
        if (f.size() < 2 || !Number(f[0], sort, 1) || !Number(f[1], mask, 0x7FF))
            return Error();
        _lapOptMask = static_cast<uint16_t>(mask);
        return Ok();
    }
    if (base == "AT+CIPTCPOPT")
    {
        if (!At2())
            return Unsupported();
        if (query)
        {
            std::string text;
            for (int i = 0; i < kLinks; ++i)
                text += "+CIPTCPOPT:" + std::to_string(i) + ",-1,0,1\r\n";
            Send(text);
        }
        return Ok();   // SO_LINGER / TCP_NODELAY / SO_SNDTIMEO: the virtual network's sockets do not need them
    }
    if (base == "AT+CIPSERVERMAXCONN")
    {
        if (query)
        {
            Send("+CIPSERVERMAXCONN:" + std::to_string(_maxConn) + "\r\n");
            return Ok();
        }
        uint32_t v = 0;
        if (!Number(args, v, kLinks) || v == 0)
            return Error();
        _maxConn = static_cast<uint8_t>(v);
        return Ok();
    }
    if (base == "AT+CIPDNS")
    {
        if (query)
        {
            const uint32_t d0 = _dnsManual ? _dns[0] : DnsServer();
            std::string text = (At2() ? "+CIPDNS:" + std::string(_dnsManual ? "1" : "0") + ",\"" + NetIpToString(d0) + "\""
                                      : "+CIPDNS_CUR:" + NetIpToString(d0));
            if (_dnsManual && _dns[1])
                text += At2() ? ",\"" + NetIpToString(_dns[1]) + "\"" : "\r\n+CIPDNS_CUR:" + NetIpToString(_dns[1]);
            Send(text + "\r\n");
            return Ok();
        }
        const std::vector<std::string> f = Fields(args);
        uint32_t enable = 0;
        if (f.empty() || !Number(f[0], enable, 1))
            return Error();
        uint32_t d[2] = {0, 0};
        for (size_t i = 1; i < f.size() && i <= 2; ++i)
        {
            if (!Ipv4(Trim(f[i]), d[i - 1]))
                return Error();
        }
        // The servers are stored and reported; the virtual network answers a lookup whichever server is named
        _dnsManual = enable == 1;
        _dns[0] = _dnsManual ? d[0] : 0;
        _dns[1] = _dnsManual ? d[1] : 0;
        return Ok();
    }
    if (base == "AT+CWDHCP" || base == "AT+CWHOSTNAME" || base == "AT+CIPSTO" || base == "AT+SLEEP" ||
        base == "AT+CWCOUNTRY" || base == "AT+CIPSSLSIZE")
        return Ok();
    if (base == "AT+CIFSR")
    {
        Send("+CIFSR:STAIP,\"" + NetIpToString(Ip()) + "\"\r\n+CIFSR:STAMAC,\"" + MacText(Mac()) + "\"\r\n");
        return Ok();
    }
    if (base == "AT+CIPSTA")
    {
        if (!query)
            return Ok();   // a static address: the virtual network keeps its lease
        Send("+CIPSTA:ip:\"" + NetIpToString(Ip()) + "\"\r\n+CIPSTA:gateway:\"" +
             NetIpToString(GetWifi() == Wifi::GotIp ? Gateway() : 0) + "\"\r\n+CIPSTA:netmask:\"" +
             NetIpToString(GetWifi() == Wifi::GotIp ? Netmask() : 0) + "\"\r\n");
        return Ok();
    }
    if (base == "AT+CIPMUX")
    {
        if (query)
        {
            Send("+CIPMUX:" + std::string(_mux ? "1" : "0") + "\r\n");
            return Ok();
        }
        uint32_t v = 0;
        if (!Number(args, v, 1))
            return Error();
        // Not while a link or the server is up, not in transparent mode
        bool busyLinks = _serverPort != 0;
        for (int i = 0; i < kLinks; ++i)
            busyLinks = busyLinks || Stack().Valid(i);
        if (busyLinks || (v == 1 && _transparentMode))
        {
            Send("link is builded\r\n");
            return ExecFail();
        }
        _mux = v == 1;
        return Ok();
    }
    if (base == "AT+CIPDINFO")
    {
        uint32_t v = 0;
        if (!Number(args, v, 1))
            return Error();
        _dinfo = v == 1;
        return Ok();
    }
    if (base == "AT+CIPMODE")
    {
        if (query)
        {
            Send("+CIPMODE:" + std::string(_transparentMode ? "1" : "0") + "\r\n");
            return Ok();
        }
        uint32_t v = 0;
        if (!Number(args, v, 1) || (v == 1 && _mux))
            return Error();
        _transparentMode = v == 1;
        return Ok();
    }
    if ((base == "AT+CIPRECVMODE" || base == "AT+CIPRECVDATA" || base == "AT+CIPRECVLEN") && !_traits->passiveReceive)
        return Unsupported();   // the kit's 2.2.1 binary: active +IPD receive only (the kit's 2.2.1 profile)
    if (base == "AT+CIPRECVMODE")
    {
        if (query)
        {
            Send("+CIPRECVMODE:" + std::string(_passive ? "1" : "0") + "\r\n");
            return Ok();
        }
        uint32_t v = 0;
        if (!Number(args, v, 1))
            return Error();
        _passive = v == 1;
        return Ok();
    }
    if (base == "AT+CIPRECVDATA")
        return DoCipRecvData(args);
    if (base == "AT+CIPRECVLEN")
    {
        // ESP-AT 2.x leaves a link that is not open empty ("+CIPRECVLEN:100,,,,")
        std::string text = "+CIPRECVLEN:";
        for (int i = 0; i < kLinks; ++i)
            text += (i ? "," : "") + (Stack().Valid(i) || !_traits->recvLenBlanks
                                          ? std::to_string(Stack().Valid(i) ? Stack().GetSlot(i).rx.size() : 0)
                                          : std::string());
        Send(text + "\r\n");
        return Ok();
    }
    if (base == "AT+CIPSERVER")
        return DoCipServer(args);
    if (base == "AT+CIPSTART")
        return DoCipStart(args);
    if (base == "AT+CIPSEND")
        return DoCipSend(args);
    if (base == "AT+CIPCLOSE")
        return DoCipClose(args);
    if (base == "AT+CIPSTATUS")
        return DoCipStatus();
    if (base == "AT+CIPSTATE" && _traits->cipState)
    {
        if (!query)
            return Error(atdialect::ErrorCode(atdialect::kSubOpError));
        return DoCipState();
    }
    if (base == "AT+CIPDOMAIN")
    {
        const std::vector<std::string> f = Fields(args);
        if (f.empty() || f[0].empty() || GetWifi() != Wifi::GotIp)
        {
            Send("DNS Fail\r\n");
            return ExecFail();
        }
        uint32_t addr = 0;
        if (Ipv4(f[0], addr))
        {
            Send(_traits->quotedDomain ? "+CIPDOMAIN:\"" + NetIpToString(addr) + "\"\r\n"
                                       : "+CIPDOMAIN:" + NetIpToString(addr) + "\r\n");
            return Ok();
        }
        _op = Op::Domain;
        Stack().Resolve(f[0]);
        return;
    }
    if (base == "AT+PING")
    {
        const std::vector<std::string> f = Fields(args);
        if (f.empty() || f[0].empty() || GetWifi() != Wifi::GotIp)
            return Error();
        _op = Op::Ping;
        _opStart = Now();
        _opDeadline = Now() + MicrosToT(kPingTimeoutUs);
        uint32_t addr = 0;
        if (Ipv4(f[0], addr))
            Stack().Ping(addr, EchoRequest());
        else
            Stack().Resolve(f[0]);   // the echo goes out once the name resolved
        return;
    }
    if (base == "AT+CIPSNTPCFG")
    {
        if (query)
        {
            Send("+CIPSNTPCFG:" + std::string(_sntpEnabled ? "1" : "0") + "," + std::to_string(_sntpZone) + ",\"" +
                 _sntpServer + "\"\r\n");
            return Ok();
        }
        return DoSntpCfg(args);
    }
    if (base == "AT+CIPSNTPTIME")
        return DoSntpTime();
    if (base == "AT+UART")
    {
        if (query)
        {
            Send((cmd.find("_DEF") != std::string::npos ? "+UART_DEF:" : "+UART_CUR:") + std::to_string(Baud()) +
                 ",8,1,0," + std::to_string(HonorsRts() ? _uartFlow : 0) + "\r\n");
            return Ok();
        }
        return DoUart(args);
    }
    if (base == "AT+CIUPDATE")
    {
        // The ESP-AT 1 MB build (ESP-01 / ESP-01S) has no OTA: no such command
        if (_flash == atdialect::Flash::OneMb && _traits->errCodes)
            return Unsupported();
        // The cloud update needs Espressif's server: it fails as without Internet
        Send("+CIPUPDATE:1\r\n");
        return Error();
    }
    Unsupported();
}

void AtModule::DoCwJap(const std::string& args, bool execute)
{
    // ESP-AT 2.x: AT+CWJAP alone joins the last access point; <jap_timeout> (8th, 3..600 s, default 15) bounds a
    // failing join
    if (execute && !_traits->joinErrorForm)
        return Unsupported();
    const std::vector<std::string> f = execute ? std::vector<std::string>{Ssid()} : Fields(args);
    if (f.empty() || f[0].empty() || f[0].size() > 32)
        return Error(atdialect::ErrorCode(atdialect::kSubParaInvalid, 0));
    uint64_t failUs = kJoinFailUs;
    if (_traits->joinErrorForm && f.size() >= 8 && !Trim(f[7]).empty())
    {
        uint32_t seconds = 0;
        if (!Number(f[7], seconds, 600) || seconds < 3)
            return Error(atdialect::ErrorCode(atdialect::kSubParaInvalid, 7));
        failUs = static_cast<uint64_t>(seconds) * 1000000;
    }
    if (GetWifi() == Wifi::GotIp)
        Send("WIFI DISCONNECT\r\n");
    Join(f[0]);
    _op = Op::Join;
    _opLink = 0;   // answer with OK / FAIL (ERROR on ESP-AT 2.x)
    _opDeadline = Now() + MicrosToT(f[0] == kVirtualSsid ? kJoinUs : failUs);
}

void AtModule::StartLink(int link, bool udp, const std::string& host, uint16_t port, uint16_t localPort)
{
    _op = Op::Start;
    _opLink = link;
    _opUdp = udp;
    _opPort = port;
    _opLocalPort = localPort;
    uint32_t addr = 0;
    if (Ipv4(host, addr))
    {
        EspStack::Done done;
        done.kind = EspStack::Done::Kind::Resolve;
        done.addr = addr;
        OnStackDone(done);
        return;
    }
    Stack().Resolve(host);
}

void AtModule::DoCipStart(const std::string& args)
{
    std::vector<std::string> f = Fields(args);
    int link = 0;
    if (_mux)
    {
        uint32_t id = 0;
        if (f.empty() || !Number(f[0], id, kLinks - 1))
            return Error();
        link = static_cast<int>(id);
        f.erase(f.begin());
    }
    if (f.size() < 3)
        return Error();
    const std::string type = Upper(Trim(f[0]));
    uint32_t port = 0;
    if ((type != "TCP" && type != "UDP") || !Number(f[2], port, 65535) || port == 0)
        return Error();   // SSL needs a TLS stack the virtual network does not have
    if (Stack().Valid(link))
    {
        Send("ALREADY CONNECTED\r\n");
        return ExecFail();
    }
    if (GetWifi() != Wifi::GotIp)
    {
        Send("no ip\r\n");
        return ExecFail();
    }
    uint32_t localPort = 0;
    if (type == "UDP" && f.size() >= 4)
        Number(f[3], localPort, 65535);
    StartLink(link, type == "UDP", Trim(f[1]), static_cast<uint16_t>(port), static_cast<uint16_t>(localPort));
}

void AtModule::OnStackDone(const EspStack::Done& done)
{
    if (done.kind == EspStack::Done::Kind::Query)
    {
        // SNTP: the transmit timestamp of the server's reply (bytes 40..43)
        if (done.data.size() >= 48)
        {
            const uint32_t ntp = (static_cast<uint32_t>(done.data[40]) << 24) | (static_cast<uint32_t>(done.data[41]) << 16) |
                                 (static_cast<uint32_t>(done.data[42]) << 8) | done.data[43];
            if (ntp > kNtpUnixOffset)
            {
                _sntpSeconds = ntp - kNtpUnixOffset;
                _sntpAt = Now();
                if (GetChip() == Chip::Esp32)
                    Send("+TIME_UPDATED\r\n", 0);
            }
        }
        return;
    }
    if (done.kind == EspStack::Done::Kind::Ping)
    {
        if (_op != Op::Ping)
            return;
        _op = Op::None;
        const uint64_t t = Now() - _opStart;
        const uint64_t ms = t * 1000 / std::max<uint64_t>(1, MicrosToT(1000000));
        return Send((_traits->ping2x ? "+PING:" : "+") + std::to_string(ms) + "\r\n\r\nOK\r\n", 0);
    }
    if (done.kind == EspStack::Done::Kind::Resolve)
    {
        if (_sntpResolving && _op != Op::Start && _op != Op::Domain && _op != Op::Ping)
        {
            _sntpResolving = false;
            if (done.addr)
            {
                std::vector<uint8_t> request(48, 0);
                request[0] = 0x1B;   // LI 0, version 3, client
                Stack().Query(NetEndpoint{done.addr, 123}, request);
            }
            return;
        }
        if (_op == Op::Domain)
        {
            _op = Op::None;
            if (!done.addr)
            {
                Send("DNS Fail\r\n");
                return ExecFail();
            }
            Send(_traits->quotedDomain ? "+CIPDOMAIN:\"" + NetIpToString(done.addr) + "\"\r\n"
                                       : "+CIPDOMAIN:" + NetIpToString(done.addr) + "\r\n");
            return Ok();
        }
        if (_op == Op::Ping)
        {
            if (!done.addr)
            {
                _op = Op::None;
                return Send(_traits->ping2x ? "+PING:TIMEOUT\r\n\r\nERROR\r\n" : "+timeout\r\n\r\nERROR\r\n", 0);
            }
            Stack().Ping(done.addr, EchoRequest());
            return;
        }
        if (_op != Op::Start)
            return;
        if (!done.addr)
        {
            _op = Op::None;
            Send("DNS Fail\r\n");
            return ExecFail();
        }
        const int link = _opLink;
        if (Stack().OpenAt(link, !_opUdp) < 0)
        {
            _op = Op::None;
            return ExecFail();
        }
        _links[link] = Link();
        _links[link].udp = _opUdp;
        _links[link].remote = NetEndpoint{done.addr, _opPort};
        _links[link].localPort = _opLocalPort;
        if (_opUdp)
        {
            if (_opLocalPort)
                Stack().Bind(link, _opLocalPort);
            _op = Op::None;
            _links[link].closedReported = false;
            Send(LinkPrefix(link) + "CONNECT\r\n");
            return Ok();
        }
        Stack().Connect(link, NetEndpoint{done.addr, _opPort});
        return;
    }
    // Connect
    if (_op != Op::Start || done.slot != _opLink)
        return;
    _op = Op::None;
    if (done.status == NetEventStatus::Ok)
    {
        _links[done.slot].closedReported = false;
        Send(LinkPrefix(done.slot) + "CONNECT\r\n");
        return Ok();
    }
    Stack().Close(done.slot);
    Send(LinkPrefix(done.slot) + "CLOSED\r\n");
    ExecFail();
}

void AtModule::DoCipSend(const std::string& args)
{
    std::vector<std::string> f = Fields(args);
    if (args.empty())
    {
        // Transparent mode: everything after "> " goes to the link until "+++"
        if (!_transparentMode || !Stack().Valid(0))
            return Error();
        Send("\r\nOK\r\n\r\n>");
        _transparent = true;
        _sendLink = 0;
        _lastRxAt = Now();
        return;
    }
    int link = 0;
    if (_mux)
    {
        uint32_t id = 0;
        if (!Number(f[0], id, kLinks - 1))
            return Error();
        link = static_cast<int>(id);
        f.erase(f.begin());
    }
    uint32_t length = 0;
    if (f.empty() || !Number(f[0], length) || length == 0 || length > kMaxSend)
        return Error();
    if (!Stack().Valid(link) || (!_links[link].udp && !Stack().Established(link)))
    {
        Send("link is not valid\r\n");
        return ExecFail();
    }
    _sendTo = _links[link].remote;
    if (_links[link].udp && f.size() >= 3)
    {
        uint32_t addr = 0, port = 0;
        if (Ipv4(Trim(f[1]), addr) && Number(f[2], port, 65535))
            _sendTo = NetEndpoint{addr, static_cast<uint16_t>(port)};
    }
    _sending = true;
    _sendLink = link;
    _sendLength = length;
    _holdUrc = true;
    Send("\r\nOK\r\n> ");
}

void AtModule::FinishSend()
{
    std::vector<uint8_t> data(_rx.begin(), _rx.begin() + _sendLength);
    _rx.erase(_rx.begin(), _rx.begin() + _sendLength);
    _echoed = 0;
    _sending = false;
    const int link = _sendLink;
    if (Stack().Valid(link))
    {
        if (_links[link].udp)
            Stack().SendTo(link, _sendTo, data.data(), static_cast<uint32_t>(data.size()));
        else
            Stack().Send(link, data.data(), static_cast<uint32_t>(data.size()));
        Send("\r\nRecv " + std::to_string(data.size()) + " bytes\r\n\r\nSEND OK\r\n");
    }
    else
        Send("\r\nSEND FAIL\r\n");
    _holdUrc = false;
}

void AtModule::FlushTransparent()
{
    // "+++" alone, after a second of silence, leaves the data phase
    if (_rx.size() == 3 && std::equal(_rx.begin(), _rx.end(), "+++") && Now() >= _lastRxAt + MicrosToT(kPlusGuardUs))
    {
        _rx.clear();
        _transparent = false;
        return;
    }
    if (_rx.size() == 3 && std::equal(_rx.begin(), _rx.end(), "+++"))
        return;   // maybe the escape: wait for the guard time
    if (_rx.empty())
        return;
    const size_t n = std::min<size_t>(_rx.size(), kMaxSend);
    std::vector<uint8_t> data(_rx.begin(), _rx.begin() + n);
    _rx.erase(_rx.begin(), _rx.begin() + n);
    if (Stack().Valid(0))
    {
        if (_links[0].udp)
            Stack().SendTo(0, _links[0].remote, data.data(), static_cast<uint32_t>(data.size()));
        else
            Stack().Send(0, data.data(), static_cast<uint32_t>(data.size()));
    }
}

void AtModule::DoCipClose(const std::string& args)
{
    if (_mux)
    {
        uint32_t id = 0;
        if (!Number(args, id, kLinks))
            return Error();
        if (id == kLinks)
        {
            for (int i = 0; i < kLinks; ++i)
            {
                if (Stack().Valid(i))
                {
                    Stack().Close(i);
                    _links[i].closedReported = true;
                    Send(std::to_string(i) + ",CLOSED\r\n");
                }
            }
            return Ok();
        }
        if (!Stack().Valid(static_cast<int>(id)))
            return ExecFail();
        Stack().Close(static_cast<int>(id));
        _links[id].closedReported = true;
        Send(std::to_string(id) + ",CLOSED\r\n");
        return Ok();
    }
    if (!Stack().Valid(0))
        return ExecFail();
    Stack().Close(0);
    _links[0].closedReported = true;
    Send("CLOSED\r\n");
    Ok();
}

void AtModule::DoCipStatus()
{
    bool any = false;
    std::string lines;
    for (int i = 0; i < kLinks; ++i)
    {
        if (!Stack().Valid(i))
            continue;
        any = true;
        const EspStack::Slot& s = Stack().GetSlot(i);
        lines += "+CIPSTATUS:" + std::to_string(i) + ",\"" + (_links[i].udp ? "UDP" : "TCP") + "\",\"" +
                 NetIpToString(s.remote.addr) + "\"," + std::to_string(s.remote.port) + "," +
                 std::to_string(_links[i].localPort ? _links[i].localPort : 0xC100 + i) + ",0\r\n";
    }
    const int status = GetWifi() != Wifi::GotIp ? 5 : (any ? 3 : 2);
    Send("STATUS:" + std::to_string(status) + "\r\n" + lines);
    Ok();
}

void AtModule::DoCipState()
{
    // AT+CIPSTATE? (ESP-AT 2.2.2.0+): the connections only, no STATUS line
    std::string lines;
    for (int i = 0; i < kLinks; ++i)
    {
        if (!Stack().Valid(i))
            continue;
        const EspStack::Slot& s = Stack().GetSlot(i);
        lines += "+CIPSTATE:" + std::to_string(i) + ",\"" + (_links[i].udp ? "UDP" : "TCP") + "\",\"" +
                 NetIpToString(s.remote.addr) + "\"," + std::to_string(s.remote.port) + "," +
                 std::to_string(_links[i].localPort ? _links[i].localPort : 0xC100 + i) + ",0\r\n";
    }
    Send(lines);
    Ok();
}

void AtModule::DoCipServer(const std::string& args)
{
    const std::vector<std::string> f = Fields(args);
    uint32_t mode = 0;
    if (f.empty() || !Number(f[0], mode, 1))
        return Error();
    if (mode == 0)
    {
        if (_serverPort)
            Stack().Close(kServerSlot);
        _serverPort = 0;
        return Ok();
    }
    if (!_mux)
        return ExecFail();   // a server needs CIPMUX=1
    uint32_t port = 333;
    if (f.size() >= 2 && !Number(f[1], port, 65535))
        return Error();
    if (_serverPort)
    {
        Send("no change\r\n");
        return Ok();
    }
    if (Stack().OpenAt(kServerSlot, true) < 0)
        return Error();
    Stack().Bind(kServerSlot, static_cast<uint16_t>(port));
    Stack().Listen(kServerSlot);
    _serverPort = static_cast<uint16_t>(port);
    Ok();
}

void AtModule::DoCipRecvData(const std::string& args)
{
    std::vector<std::string> f = Fields(args);
    int link = 0;
    if (_mux)
    {
        uint32_t id = 0;
        if (f.empty() || !Number(f[0], id, kLinks - 1))
            return Error();
        link = static_cast<int>(id);
        f.erase(f.begin());
    }
    uint32_t length = 0;
    if (f.empty() || !Number(f[0], length) || length == 0)
        return Error(atdialect::ErrorCode(atdialect::kSubParaInvalid, _mux ? 1 : 0));
    if (!_passive || !Stack().Valid(link))
        return ExecFail();
    const std::vector<uint8_t> data = Stack().Read(link, std::min(length, kMaxSend));
    _links[link].notified = static_cast<uint32_t>(Stack().GetSlot(link).rx.size());
    _links[link].ipdOwed = false;   // the next arrival is announced again
    // NonOS: "+CIPRECVDATA,<len>:<data>"; the ESP8266 ESP-AT 2.2 builds: "+CIPRECVDATA:<len>,<data>"
    const std::string head = _traits->recvData2x ? "+CIPRECVDATA:" + std::to_string(data.size()) + ","
                                                 : "+CIPRECVDATA," + std::to_string(data.size()) + ":";
    Send(head);
    Send(data.data(), data.size(), 0);
    if (_traits->recvData2x)
        Send("\r\n");
    Ok();
}

void AtModule::DoUart(const std::string& args)
{
    const std::vector<std::string> f = Fields(args);
    uint32_t baud = 0, bits = 8, stop = 1, parity = 0, flow = 0;
    if (f.size() < 5 || !Number(f[0], baud) || !Number(f[1], bits) || !Number(f[2], stop) || !Number(f[3], parity) ||
        !Number(f[4], flow, 3) || baud < 110 || baud > 5000000 || bits < 5 || bits > 8 || stop < 1 || stop > 3 ||
        parity > 2)
        return Error();
    Ok();
    // The answer goes out at the old line; the module honors the ZX's RTS
    // when CTS flow control (bit 1) is on
    SetBaud(baud, Now() + MicrosToT(kTurnaroundUs + 2000));
    SetFlowControl((flow & 0x02) != 0);
    _uartFlow = static_cast<uint8_t>(flow);
}

void AtModule::DoSntpCfg(const std::string& args)
{
    const std::vector<std::string> f = Fields(args);
    uint32_t enable = 0;
    if (f.empty() || !Number(f[0], enable, 1))
        return Error();
    int zone = 0;
    if (f.size() >= 2)
    {
        const std::string z = Trim(f[1]);
        uint32_t v = 0;
        const bool negative = !z.empty() && z[0] == '-';
        if (!Number(negative ? z.substr(1) : z, v, 14))
            return Error();
        zone = negative ? -static_cast<int>(v) : static_cast<int>(v);
    }
    _sntpEnabled = enable == 1;
    _sntpZone = static_cast<int8_t>(zone);
    if (f.size() >= 3 && !Trim(f[2]).empty())
        _sntpServer = Trim(f[2]);
    _sntpSeconds = 0;   // a new configuration syncs again
    Ok();
    if (_sntpEnabled && _op == Op::None && GetWifi() == Wifi::GotIp)
        SntpQuery();
}

void AtModule::SntpQuery()
{
    uint32_t addr = 0;
    if (Ipv4(_sntpServer, addr))
    {
        std::vector<uint8_t> request(48, 0);
        request[0] = 0x1B;
        Stack().Query(NetEndpoint{addr, 123}, request);
        return;
    }
    _sntpResolving = true;
    Stack().Resolve(_sntpServer);
}

void AtModule::DoSntpTime()
{
    // Before the first answer the clock reads 1970 (software waits for that to change)
    uint64_t t = 0;
    if (_sntpSeconds)
        t = _sntpSeconds + (Now() - _sntpAt) / std::max<uint64_t>(1, MicrosToT(1000000)) +
            static_cast<int64_t>(_sntpZone) * 3600;
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm {};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    static const char* const kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "+CIPSNTPTIME:%s %s %02d %02d:%02d:%02d %d\r\n", kDays[tm.tm_wday % 7],
                  kMonths[tm.tm_mon % 12], tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_year + 1900);
    Send(buf);
    Ok();
}

void AtModule::OnStackData(int slot)
{
    (void)slot;
    EmitUrcs();
}

void AtModule::EmitUrcs()
{
    // Nothing between a command and its answer, inside "> ", before SEND OK
    if (_op != Op::None || _sending || _holdUrc)
        return;

    // A client for the server: the lowest free link takes it
    if (_serverPort && Stack().Valid(kServerSlot))
    {
        while (!Stack().GetSlot(kServerSlot).pending.empty())
        {
            const int link = Stack().Accept(kServerSlot);
            if (link < 0 || link >= kLinks)
            {
                if (link >= kLinks)
                    Stack().Close(link);
                break;
            }
            _links[link] = Link();
            _links[link].closedReported = false;
            Send(std::to_string(link) + ",CONNECT\r\n", 0);
        }
    }

    for (int i = 0; i < kLinks; ++i)
    {
        if (!Stack().Valid(i))
            continue;
        const EspStack::Slot& s = Stack().GetSlot(i);
        Link& l = _links[i];
        const std::string id = _mux ? std::to_string(i) + "," : std::string();
        auto info = [&](const NetEndpoint& from) {
            return _dinfo ? "," + NetIpToString(from.addr) + "," + std::to_string(from.port) : std::string();
        };
        if (_transparent && i == 0)
        {
            // Transparent mode: the bytes as they are
            const std::vector<uint8_t> data = Stack().Read(i, 0xFFFF);
            Send(data.data(), data.size(), 0);
        }
        else if (l.udp)
        {
            EspStack::Datagram d;
            while (Stack().PopDatagram(i, d))
            {
                std::string head = "\r\n+IPD," + id + std::to_string(d.data.size()) + info(d.from) + ":";
                std::vector<uint8_t> bytes;
                for (const EspStack::RxByte& b : d.data)
                    bytes.push_back(b.value);
                Send(head, 0);
                Send(bytes.data(), bytes.size(), 0);
            }
        }
        else if (_passive)
        {
            // <len> is all the link holds; ESP-AT 2.x says it once until AT+CIPRECVDATA reads
            const uint32_t have = static_cast<uint32_t>(s.rx.size());
            if (have > l.notified && !(l.ipdOwed && _traits->ipdOneShot))
            {
                Send("+IPD," + id + std::to_string(have) + "\r\n", 0);
                l.notified = have;
                l.ipdOwed = true;
            }
        }
        else
        {
            while (!Stack().GetSlot(i).rx.empty())
            {
                const std::vector<uint8_t> data = Stack().Read(i, kIpdChunk);
                Send("\r\n+IPD," + id + std::to_string(data.size()) + info(s.remote) + ":", 0);
                Send(data.data(), data.size(), 0);
            }
        }
        // The peer closed and everything was delivered: CLOSED, the link is free
        const bool drained = _passive ? Stack().GetSlot(i).rx.empty() : true;
        if (!l.udp && s.finSeen && drained && Stack().GetSlot(i).rx.empty() && !l.closedReported)
        {
            l.closedReported = true;
            Stack().Close(i);
            if (_transparent && i == 0)
                _transparent = false;
            Send(id + "CLOSED\r\n", 0);
        }
    }
}

void AtModule::SaveFirmware(netstate::EspModuleState& out) const
{
    uint8_t* f = out.firmware;
    size_t p = 0;
    auto put8 = [&](uint8_t v) { f[p++] = v; };
    auto put16 = [&](uint16_t v) { put8(static_cast<uint8_t>(v)); put8(static_cast<uint8_t>(v >> 8)); };
    auto put32 = [&](uint32_t v) { put16(static_cast<uint16_t>(v)); put16(static_cast<uint16_t>(v >> 16)); };
    auto put64 = [&](uint64_t v) { put32(static_cast<uint32_t>(v)); put32(static_cast<uint32_t>(v >> 32)); };
    put8(static_cast<uint8_t>((_echo ? 1 : 0) | (_mux ? 2 : 0) | (_dinfo ? 4 : 0) | (_passive ? 8 : 0) |
                              (_transparentMode ? 16 : 0) | (_autoConnect ? 32 : 0) | (_sending ? 64 : 0) |
                              (_holdUrc ? 128 : 0)));
    put8(static_cast<uint8_t>((_transparent ? 1 : 0) | (_opUdp ? 2 : 0) | (_sntpEnabled ? 4 : 0) |
                              (_sntpResolving ? 8 : 0)));
    put8(_cwMode);
    put8(static_cast<uint8_t>(_op));
    put8(static_cast<uint8_t>(_opLink));
    put8(static_cast<uint8_t>(_sendLink));
    put8(static_cast<uint8_t>(_sntpZone));
    put8(0);
    put16(_serverPort);
    put16(_opPort);
    put16(_opLocalPort);
    put16(static_cast<uint16_t>(_echoed));
    put16(static_cast<uint16_t>(_rxSeen));
    put32(_sendLength);
    put32(_sendTo.addr);
    put16(_sendTo.port);
    put16(0);
    put64(_opDeadline);
    put64(_opStart);
    put64(_lastRxAt);
    put32(_sntpSeconds);
    put64(_sntpAt);
    for (const Link& l : _links)
    {
        put8(static_cast<uint8_t>((l.udp ? 1 : 0) | (l.closedReported ? 2 : 0)));
        put8(0);
        put16(l.localPort);
        put32(l.remote.addr);
        put16(l.remote.port);
        put16(0);
        put32(l.notified);
    }
    const size_t n = std::min<size_t>(_sntpServer.size(), 63);
    put8(static_cast<uint8_t>(n));
    std::memcpy(f + p, _sntpServer.data(), n);
    // The 2.x session settings at a fixed place after the longest server name (zeros in older blobs = defaults)
    p = kExtraAt;
    put8(static_cast<uint8_t>((_sysStore ? 0 : 1) | (_sysLog ? 2 : 0) | (_dnsManual ? 4 : 0)));
    put8(static_cast<uint8_t>(5 - _maxConn));
    put8(static_cast<uint8_t>(3 - _uartFlow));
    put8(0);
    put16(static_cast<uint16_t>(0x7FF - _lapOptMask));
    put32(_dns[0]);
    put32(_dns[1]);
    uint8_t owed = 0;
    for (int i = 0; i < kLinks; ++i)
        owed = static_cast<uint8_t>(owed | (_links[i].ipdOwed ? 1u << i : 0u));
    put8(owed);
}

void AtModule::LoadFirmware(const netstate::EspModuleState& in)
{
    const uint8_t* f = in.firmware;
    size_t p = 0;
    auto get8 = [&]() { return f[p++]; };
    auto get16 = [&]() { const uint16_t v = static_cast<uint16_t>(f[p] | (f[p + 1] << 8)); p += 2; return v; };
    auto get32 = [&]() { const uint32_t lo = get16(); return lo | (static_cast<uint32_t>(get16()) << 16); };
    auto get64 = [&]() { const uint64_t lo = get32(); return lo | (static_cast<uint64_t>(get32()) << 32); };
    const uint8_t a = get8();
    _echo = a & 1;
    _mux = a & 2;
    _dinfo = a & 4;
    _passive = a & 8;
    _transparentMode = a & 16;
    _autoConnect = a & 32;
    _sending = a & 64;
    _holdUrc = a & 128;
    const uint8_t b = get8();
    _transparent = b & 1;
    _opUdp = b & 2;
    _sntpEnabled = b & 4;
    _sntpResolving = b & 8;
    _cwMode = get8();
    _op = static_cast<Op>(get8());
    _opLink = static_cast<int8_t>(get8());
    _sendLink = get8();
    _sntpZone = static_cast<int8_t>(get8());
    get8();
    _serverPort = get16();
    _opPort = get16();
    _opLocalPort = get16();
    _echoed = get16();
    _rxSeen = get16();
    _sendLength = get32();
    _sendTo.addr = get32();
    _sendTo.port = get16();
    get16();
    _opDeadline = get64();
    _opStart = get64();
    _lastRxAt = get64();
    _sntpSeconds = get32();
    _sntpAt = get64();
    for (Link& l : _links)
    {
        const uint8_t flags = get8();
        l.udp = flags & 1;
        l.closedReported = flags & 2;
        get8();
        l.localPort = get16();
        l.remote.addr = get32();
        l.remote.port = get16();
        get16();
        l.notified = get32();
    }
    const uint8_t n = get8();
    _sntpServer.assign(reinterpret_cast<const char*>(f + p), std::min<uint8_t>(n, 63));
    p = kExtraAt;
    const uint8_t flags = get8();
    _sysStore = !(flags & 1);
    _sysLog = (flags & 2) != 0;
    _dnsManual = (flags & 4) != 0;
    _maxConn = static_cast<uint8_t>(5 - get8());
    _uartFlow = static_cast<uint8_t>(3 - get8());
    get8();
    _lapOptMask = static_cast<uint16_t>(0x7FF - get16());
    _dns[0] = get32();
    _dns[1] = get32();
    const uint8_t owed = get8();
    for (int i = 0; i < kLinks; ++i)
        _links[i].ipdOwed = (owed >> i) & 1;
}
