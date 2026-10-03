#pragma once

/// @file atmodule.h
/// @brief An ESP module running Espressif's AT firmware (network TDD step
/// N3; dialect: Espressif's AT instruction set and what ZX software sends,
/// research notes in reference-esp-modules.md Part 2). ESP8266 answers like NonOS AT
/// 1.7.x, ESP32 like ESP32 AT 2.x.
///
/// Text commands ending CR LF; replies framed CR LF; echo on until ATE0. One
/// link (CIPMUX=0) or five (CIPMUX=1) plus a server; incoming data as +IPD
/// (active) or held for CIPRECVDATA (passive); transparent mode with +++.
/// The order clients rely on is kept: CONNECT before OK, OK before "> ",
/// SEND OK before the next +IPD, CLOSED after the last data, no other text
/// while a link is busy.

#include "emulator/io/serial/esp/espmodule.h"

class AtModule final : public EspModule
{
public:
    static constexpr int kLinks = 5;
    static constexpr int kServerSlot = 5;
    static constexpr uint32_t kMaxSend = 2048;
    static constexpr uint32_t kIpdChunk = 1460;   ///< one TCP segment per +IPD

    /// ESP8266: NonOS AT 1.7.4, ESP32: AT 2.2.0
    AtModule(VirtualNetwork* network, Chip chip);
    /// A firmware preset (network tdd §8.3) and the station MAC (nullptr: the chip family's)
    AtModule(VirtualNetwork* network, Firmware firmware, const std::array<uint8_t, 6>* mac = nullptr);

    const char* Kind() const override { return "at"; }
    void OnFrame() override;
    Firmware GetFirmware() const { return _firmware; }

    /// Session settings a status view shows (AT+CIPMUX, AT+CIPRECVMODE, AT+SYSSTORE, AT+CIPDNS)
    bool Mux() const { return _mux; }
    bool Passive() const { return _passive; }
    bool Echo() const { return _echo; }
    bool SysStore() const { return _sysStore; }
    uint32_t ManualDns(int n) const { return n == 0 ? _dns[0] : _dns[1]; }
    /// Link `n` (0..4) as AT+CIPSTATUS shows it: open, UDP, remote
    bool LinkOpen(int n) const;
    bool LinkUdp(int n) const { return n >= 0 && n < kLinks && _links[n].udp; }

protected:
    void Process() override;
    void OnStackDone(const EspStack::Done& done) override;
    void OnStackData(int slot) override;
    void SaveFirmware(netstate::EspModuleState& out) const override;
    void LoadFirmware(const netstate::EspModuleState& in) override;

private:
    enum class Op : uint8_t
    {
        None,
        Start,      ///< CIPSTART: resolving, then connecting
        Join,       ///< CWJAP
        Scan,       ///< CWLAP
        Domain,     ///< CIPDOMAIN
        Ping,       ///< PING
        Boot,       ///< RST / RESTORE: the module restarts
    };

    struct Link
    {
        bool udp = false;
        bool closedReported = true;   ///< no CLOSED owed
        NetEndpoint remote;           ///< UDP: where CIPSEND goes
        uint16_t localPort = 0;
        uint32_t notified = 0;        ///< passive mode: bytes announced with +IPD,<len>
    };

    void HandleLine(const std::string& line);
    void Reply(const std::string& text) { Send(text); }
    void Ok() { Send("\r\nOK\r\n"); }
    /// ESP-AT 2.x (ESP32 AT 2.2.0, ESP8266 2.2.1 / 2.2.2) rather than NonOS 1.7
    bool At2() const { return _firmware != Firmware::Esp8266NonOs174; }
    /// The ESP8266 ESP-AT 2.2 builds (their reply forms: +PING:, +CIPRECVDATA:<len>,)
    bool Esp8266At2() const { return _firmware == Firmware::Esp8266At221 || _firmware == Firmware::Esp8266At222; }
    void OnHardwareReset() override;
    void OnHardwareBoot() override;
    void ResetSession();
    void Error() { Send("\r\nERROR\r\n"); }
    void EmitUrcs();
    void FinishSend();
    void Boot(uint64_t afterUs);
    void BootDone();
    void StartLink(int link, bool udp, const std::string& host, uint16_t port, uint16_t localPort);
    void DoCipStart(const std::string& args);
    void DoCipSend(const std::string& args);
    void DoCipClose(const std::string& args);
    void DoCipStatus();
    void DoCipServer(const std::string& args);
    void DoCipRecvData(const std::string& args);
    void DoCwJap(const std::string& args);
    void DoUart(const std::string& args);
    void DoSntpCfg(const std::string& args);
    void DoSntpTime();
    void SntpQuery();
    int LinkOf(int slot) const { return slot >= 0 && slot < kLinks ? slot : -1; }
    std::string LinkPrefix(int link) const { return _mux ? std::to_string(link) + "," : std::string(); }
    void FlushTransparent();

    Firmware _firmware = Firmware::Esp32At220;

    // Settings
    bool _echo = true;
    bool _sysStore = true;           ///< AT+SYSSTORE (2.x): settings go to flash
    bool _sysLog = false;            ///< AT+SYSLOG
    bool _dnsManual = false;         ///< AT+CIPDNS=1,...: DNS servers set by the ZX
    uint32_t _dns[2] = {0, 0};
    uint16_t _lapOptMask = 0x7FF;    ///< AT+CWLAPOPT: which +CWLAP fields
    uint8_t _maxConn = 5;            ///< AT+CIPSERVERMAXCONN
    uint8_t _uartFlow = 3;           ///< the flow field of the last AT+UART_CUR (query)
    bool _mux = false;
    bool _dinfo = false;
    bool _passive = false;
    bool _transparentMode = false;   ///< CIPMODE=1
    bool _autoConnect = true;
    uint8_t _cwMode = 1;
    uint16_t _serverPort = 0;

    // Activity
    Op _op = Op::None;
    int _opLink = 0;
    bool _opUdp = false;
    uint16_t _opPort = 0;
    uint16_t _opLocalPort = 0;
    uint64_t _opDeadline = 0;
    uint64_t _opStart = 0;
    size_t _echoed = 0;              ///< bytes of the receive buffer already echoed
    bool _sending = false;           ///< CIPSEND: collecting the payload
    int _sendLink = 0;
    uint32_t _sendLength = 0;
    NetEndpoint _sendTo;             ///< UDP CIPSEND to an explicit peer
    bool _holdUrc = false;           ///< no +IPD until SEND OK is out
    bool _transparent = false;       ///< in the transparent data phase
    uint64_t _lastRxAt = 0;          ///< when the last byte from the ZX arrived
    size_t _rxSeen = 0;              ///< receive buffer size at the last look (new bytes = more)
    Link _links[kLinks];

    // SNTP
    bool _sntpEnabled = false;
    int8_t _sntpZone = 0;
    std::string _sntpServer = "pool.ntp.org";
    uint32_t _sntpSeconds = 0;       ///< Unix time at _sntpAt (0: not synced)
    uint64_t _sntpAt = 0;
    bool _sntpResolving = false;
};
