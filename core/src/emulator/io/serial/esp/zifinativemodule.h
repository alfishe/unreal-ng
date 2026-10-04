#pragma once

/// @file zifinativemodule.h
/// @brief The "new ZiFi" (2026) ESP firmware: a binary protocol on the ZiFi line instead of AT
/// (docs/inprogress/2026-10-02-tsconf-zifi/tdd.md §7, reference-sprinter-wifi-driver.md "New ZiFi").
///
/// Frame, both directions: 5A CMD LEN_L LEN_H DATA[LEN] CSUM; CSUM = XOR of CMD, LEN_L, LEN_H and DATA (not the
/// 5A); LEN <= 1024. The parser drops a frame with a bad checksum, a length over 1024 (after LEN_H) and a frame
/// left unfinished for 500 ms. Replies: FE (ACK) as soon as a long command is in, then the result (the command
/// with bit 7 set: 90..94, A0..A4, 81..8E); NET_RECV answers 92 without an ACK; PING answers F0; a failure reports
/// EE <text, at most 48 bytes> before the result; GET_STEP gives the last command and that text.
///
/// Two real firmwares, both on the unchanged ZiFi AVR pipe:
///  - S3: ESP32-S3-Zero on an ESP-01 adapter, s3-native-0.6.94
///    (https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero, docs/PROTOCOL.md, src/main.cpp). The network
///    work runs on the other core: while one network command runs, the module still answers ECHO, PING,
///    GET_STEP, SYS_INFO and SYS_RESET; another network command gets its ACK, EE "network busy" and an empty
///    result. HTTP GET follows up to 4 redirects, refuses chunked bodies, ends the body at Content-Length, can go
///    through the zifi.ini proxy; HTTPS (port 443) needs TLS, which the virtual network does not do
///  - ESP01S: ESP-01S (ESP8266), native-0.2.2 (https://github.com/andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project,
///    src/main.cpp): one loop - a command that waits for the network holds every later byte in the UART until
///    it is done. Plain HTTP only, no redirects, no proxy; FTP / OTA commands, no SMB / weather / GitHub update
///
/// What the emulation leaves out (they answer as the firmware does when the service cannot start): the file
/// servers (FTP, SMB, WebDAV) that call back into the Z80 (VFS requests 40..5E), the OTA listener, the online
/// update, the Wild Commander updater and the weather service.
///
/// The Wi-Fi is the virtual network's access point (SSID UnrealNG, as for every ESP module); a zifi.ini naming
/// another network does not connect (EE "wifi timeout" after 10 s, like a real module out of range).
///
/// TTD: the state rides in netstate::EspModuleState's firmware part (no blob of its own); a request that waits
/// for the network stays at the head of the receive buffer until it is answered, so a checkpoint holds it.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "emulator/io/serial/esp/espmodule.h"

class ZiFiNativeModule final : public EspModule
{
public:
    /// The firmware (ZiFi=ZIFI-NATIVE,<variant>; the numbers are ComPortSpec values: never renumber)
    enum class Variant : uint8_t
    {
        S3 = 0,       ///< ESP32-S3-Zero, s3-native-0.6.94
        Esp01s = 1,   ///< ESP-01S, native-0.2.2
    };
    /// S3 | ESP32-S3 | ESP01S | ESP-01S (case-insensitive)
    static bool ParseVariant(const std::string& text, Variant& out);
    static const char* VariantName(Variant variant);
    /// The build identifier SYS_INFO reports after "FW:"
    static const char* FirmwareVersion(Variant variant);

    // Frame
    static constexpr uint8_t kSync = 0x5A;
    static constexpr uint16_t kMaxPayload = 1024;
    static constexpr uint64_t kFrameTimeoutUs = 500000;
    static constexpr size_t kErrorText = 48;

    // Commands, Z80 -> ESP (protocol.hpp)
    static constexpr uint8_t kEcho = 0x00, kWifiConnect = 0x01, kSysInfo = 0x02, kWifiIni = 0x03, kPing = 0x04,
                             kGetStep = 0x05, kFtpStart = 0x06, kFtpStop = 0x07, kUpdateStart = 0x08, kUpdateStop = 0x09,
                             kFtpRamStats = 0x0A, kSmbStart = 0x0B, kSmbStop = 0x0C, kOnlineUpdate = 0x0D,
                             kOnlineUpdateCheck = 0x0E, kSysReset = 0x0F, kNetOpen = 0x10, kNetSend = 0x11, kNetRecv = 0x12,
                             kNetClose = 0x13, kNetHttpGet = 0x14, kNetPing = 0x20, kNetIpConfig = 0x21, kNetNtp = 0x22,
                             kNetProxyStatus = 0x23, kWeatherGet = 0x24, kWcuStart = 0x25, kWcuApply = 0x26, kWcuStop = 0x27,
                             kWcuSync = 0x28;
    // Replies
    static constexpr uint8_t kRespWifiConnect = 0x81, kRespSysInfo = 0x82, kRespWifiIni = 0x83, kRespGetStep = 0x85,
                             kRespNetRecv = 0x92, kRespNetNtp = 0xA2, kReady = 0xF0, kError = 0xEE, kAck = 0xFE;

    ZiFiNativeModule(VirtualNetwork* network, Variant variant, const std::array<uint8_t, 6>* mac = nullptr);

    const char* Kind() const override { return "zifi-native"; }
    void OnFrame() override;

    Variant GetVariant() const { return _variant; }
    /// GET_STEP: the last command (8 after a SYS_INFO) and the last error text
    uint8_t LastStep() const { return _lastStep; }
    const std::string& LastError() const { return _lastError; }
    /// A network command is running (S3: on the network core; ESP01S: the loop waits for it)
    bool Busy() const { return _op != Op::None; }
    /// What it is doing, for status views ("idle", "net_open example.test:80 connecting", ...)
    std::string Activity() const;
    /// The TCP client (NET_OPEN / NET_HTTP_GET) is open
    bool ClientOpen() const;
    /// zifi.ini as the module keeps it (time:, proxy)
    int8_t TimeZone() const { return _timeZone; }
    const std::string& ProxyHost() const { return _proxyHost; }
    uint16_t ProxyPort() const { return _proxyPort; }
    uint8_t ProxyStatus() const { return _proxyStatus; }   ///< 0 off, 1 on (it answered), 2 unreachable
    /// The saved zifi.ini (WIFI_INI), as the module keeps it in flash
    const std::vector<uint8_t>& IniText() const { return _iniText; }
    /// WEATHER_GET's place: "city:<country>/<name>" or "zip:<country>/<code>" (empty before the first call)
    const std::string& WeatherLocation() const { return _wxKey; }
    bool WeatherHaveCoords() const { return _wxHaveCoords; }
    bool WeatherUnknown() const { return _wxUnknown; }   ///< the geocoder does not know it: not asked again
    const std::string& WeatherPlace() const { return _wxPlace; }   ///< UTF-8
    float WeatherLatitude() const { return _wxLatitude; }
    float WeatherLongitude() const { return _wxLongitude; }
    /// Frames the parser dropped (status only, not TTD state)
    uint32_t BadChecksums() const { return _badChecksums; }
    uint32_t Resyncs() const { return _resyncs; }

    /// One frame (tests, tools): 5A cmd len data csum
    static std::vector<uint8_t> Frame(uint8_t cmd, const uint8_t* data, size_t length);
    static std::vector<uint8_t> Frame(uint8_t cmd, const std::vector<uint8_t>& data = {}) { return Frame(cmd, data.data(), data.size()); }

protected:
    void Process() override;
    void OnStackDone(const EspStack::Done& done) override;
    void OnStackData(int slot) override;
    void SaveFirmware(netstate::EspModuleState& out) const override;
    void LoadFirmware(const netstate::EspModuleState& in) override;
    void OnHardwareReset() override;
    void OnHardwareBoot() override;

private:
    /// A network command waiting for the network
    enum class Op : uint8_t
    {
        None,
        Join,       ///< WIFI_CONNECT / WIFI_INI: the join, then (S3) the proxy probe
        Open,       ///< NET_OPEN
        HttpGet,    ///< NET_HTTP_GET: connect, request, header
        Probe,      ///< NET_PING: a TCP connect to port 80
        Ntp,        ///< NET_NTP
        Boot,       ///< SYS_RESET: the module restarts
        Weather,    ///< WEATHER_GET (S3): geocoder / zippopotam, then the forecast, each an HTTP GET with retries
    };
    enum class Phase : uint8_t
    {
        None,
        Resolve,
        Connect,
        Header,     ///< HttpGet: the response header
        Join,       ///< Join: waiting for the access point
        Proxy,      ///< Join (S3): probing the proxy
        Query,      ///< Ntp: the UDP answer
        Body,       ///< Weather: the HTTP body
        Retry,      ///< Weather: the pause before the next attempt
    };
    /// Weather: the request it runs
    enum class WeatherStage : uint8_t
    {
        City,       ///< geocoding-api.open-meteo.com
        Zip,        ///< api.zippopotam.us
        Forecast,   ///< api.open-meteo.com
    };

    static constexpr int kClientSlot = 0;
    static constexpr int kProbeSlot = 1;

    void Handle(uint8_t cmd, const std::vector<uint8_t>& payload, size_t frameLength);
    /// A network command: S3 queues it (busy: ACK, "network busy", an empty result), ESP01S runs it
    void Network(uint8_t cmd, const std::vector<uint8_t>& payload, size_t frameLength);
    void SendFrame(uint8_t cmd, const uint8_t* data, size_t length);
    void SendFrame(uint8_t cmd, const std::vector<uint8_t>& data = {}) { SendFrame(cmd, data.data(), data.size()); }
    void Ack() { SendFrame(kAck); }
    void ReportError(const std::string& text);
    void ClearError() { _lastError.clear(); }
    /// The firmware's own reply when a command fails before it starts (sendNetworkFailure)
    void SendFailure(uint8_t cmd);
    /// Finish the running command: its error (if any), the result, the held request leaves the buffer
    void Finish(uint8_t respCmd, const std::vector<uint8_t>& data, const std::string& error = {});
    void Start(Op op, Phase phase, uint64_t timeoutUs, size_t frameLength);

    void StartJoin(const std::string& ssid, const std::string& password, bool ini);
    void JoinDone(bool connected);
    void StartOpen(const std::vector<uint8_t>& payload);
    void StartHttpGet(const std::vector<uint8_t>& payload);
    void HttpConnect();
    void HttpHeader();
    void StartProbe(const std::string& host, uint16_t port, Op op);
    void StartNtp();
    /// The HTTP GET of NET_HTTP_GET or of the weather failed before the body (`reason` as NetClient::httpGet words
    /// it: "connect failed", "header timeout", ...)
    void HttpFail(const std::string& reason);
    bool HttpOp() const { return _op == Op::HttpGet || _op == Op::Weather; }
    void StartWeather();
    /// The current weather request: GET of its host / path (the held request becomes it)
    void WeatherRequest();
    void WeatherHeader(uint16_t status, uint32_t contentLength, bool lengthKnown);
    void WeatherBody();
    void WeatherAttemptFailed(const std::string& reason, bool retryable);
    void WeatherBodyDone();
    void WeatherFail(const std::string& reason);
    void ResetWeather();
    /// zifi.ini key (lower case) as the module saved it; empty when absent
    std::string IniValue(const char* key) const;
    void NetRecv(const std::vector<uint8_t>& payload);
    void NetSend(const std::vector<uint8_t>& payload);
    void SysInfo();
    std::vector<uint8_t> WifiResult(bool connected) const;
    void Timeout();
    bool ParseIni(const std::vector<uint8_t>& data, std::string& ssid, std::string& password, std::string& error);
    /// The request at the head of the receive buffer (after a TTD load)
    void ReloadRequest();
    std::string HostOf(const std::vector<uint8_t>& payload, size_t& next) const;

    Variant _variant = Variant::S3;

    // Session (zifi.ini lives in the module's flash: it survives SYS_RESET)
    uint8_t _lastStep = 0;
    std::string _lastError;
    int8_t _timeZone = 0;
    uint32_t _passwordHash = 0;     ///< FNV-1a of the joined network's password (a new one rejoins)
    bool _passwordKnown = false;    ///< false: on the virtual AP since the box, the first password is taken as its own
    uint32_t _iniCrc = 0;           ///< CRC-32 of the saved zifi.ini (ESP01S SYS_INFO "CFG:")
    std::string _proxyHost;         ///< S3: proxy_ip / proxy_host
    uint16_t _proxyPort = 49281;
    uint8_t _proxyStatus = 0;
    std::vector<uint8_t> _iniText;  ///< the saved zifi.ini (S3 ConfigStore: the whole file, kept in flash)

    // Weather: the place (kept between commands until city: / country: / zip: change) and the running request
    std::string _wxKey;             ///< "city:<country>/<city>" or "zip:<country>/<zip>"
    bool _wxHaveCoords = false;
    bool _wxUnknown = false;        ///< the geocoder does not know the place: not asked again
    float _wxLatitude = 0.0f;
    float _wxLongitude = 0.0f;
    std::string _wxPlace;           ///< UTF-8
    WeatherStage _wxStage = WeatherStage::City;
    uint8_t _wxAttempt = 0;
    uint16_t _wxStatus = 0;         ///< the HTTP status of the last attempt
    uint64_t _wxBudgetEnd = 0;      ///< no new attempt after it
    uint64_t _wxBodyEnd = 0;        ///< the body must be in by then
    uint32_t _wxContentLength = 0;
    std::string _wxBody;

    // The TCP client's HTTP body (S3: it ends at Content-Length)
    bool _bodyActive = false;
    bool _bodyLengthKnown = false;
    uint32_t _bodyExpected = 0;
    uint32_t _bodyReceived = 0;
    uint64_t _bodyActivityAt = 0;

    // The running command
    Op _op = Op::None;
    Phase _phase = Phase::None;
    uint8_t _opCmd = 0;
    uint16_t _held = 0;             ///< bytes of the request frame kept at the head of the receive buffer
    uint64_t _opDeadline = 0;
    uint64_t _opStart = 0;
    uint32_t _opAddr = 0;           ///< the resolved address
    uint16_t _opPort = 0;           ///< the port it connects to
    uint16_t _httpPort = 0;         ///< HttpGet: the server's port (the proxy is _opPort)
    uint8_t _redirects = 0;
    bool _viaProxy = false;
    bool _iniJoin = false;          ///< Join came from WIFI_INI (reply 83, then the proxy)
    std::vector<uint8_t> _request;  ///< the held request's payload (rebuilt from the buffer after a load)
    bool _softRestart = false;      ///< the last start was SYS_RESET (SYS_INFO "RST:")

    // The parser
    size_t _rxSeen = 0;
    uint64_t _lastByteAt = 0;
    uint32_t _badChecksums = 0;
    uint32_t _resyncs = 0;
};
