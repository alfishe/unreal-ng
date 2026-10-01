#pragma once

/// @file espnetmodule.h
/// @brief An ESP module running NedoOS's ESPNET firmware 1.27 (network TDD
/// step N3; protocol: NedoOS src/kapps/common/espnet/protocol.h and
/// PROTOCOL.md, research notes in reference-esp-modules.md Part 1).
///
/// Binary frames over the UART. Request: SOF #A5, cmd, sock, arg, seq,
/// len16 LE, payload (+ an XOR CRC byte when cmd bit 7 is set). Reply: SOF,
/// cmd, sock, status (errno), seq, result16 LE, len16 LE, payload. One request
/// at a time, exactly one reply each; nothing unsolicited. A request that
/// waits for the network (CONNECT, DNSRESOLVE) holds the next ones in the
/// receive buffer until its reply is out, as the firmware's single loop does.

#include "emulator/io/serial/esp/espmodule.h"

class EspnetModule final : public EspModule
{
public:
    // Commands (protocol.h)
    static constexpr uint8_t kSof = 0xA5;
    static constexpr uint8_t kSocket = 0x01, kShutdown = 0x02, kConnect = 0x03, kAccept = 0x04, kBind = 0x05,
                             kListen = 0x06, kRead = 0x07, kWrite = 0x08, kGetDns = 0x09, kDnsResolve = 0x0A,
                             kInfo = 0x10, kWifiScan = 0x11, kWifiConnect = 0x12, kWifiDisc = 0x13,
                             kWifiStatus = 0x14, kUart = 0x15, kEcho = 0x7E;
    static constexpr uint8_t kCrcFlag = 0x80;
    static constexpr uint8_t kSockNone = 0xFF;
    static constexpr uint16_t kMaxPayload = 2048;

    // Status codes (NedoOS errno)
    static constexpr uint8_t kOk = 0, kIntr = 4, kNfile = 23, kAgain = 35, kAlready = 37, kNotSock = 38,
                             kMsgSize = 40, kProtoType = 41, kAfNoSupport = 47, kConnAborted = 53, kNotConn = 57,
                             kHostUnreach = 65;

    static constexpr uint8_t kVersionMajor = 1, kVersionMinor = 27;

    EspnetModule(VirtualNetwork* network, Chip chip);

    const char* Kind() const override { return "espnet"; }

protected:
    void Process() override;
    void OnStackDone(const EspStack::Done& done) override;
    void SaveFirmware(netstate::EspModuleState& out) const override;
    void LoadFirmware(const netstate::EspModuleState& in) override;

private:
    struct Request
    {
        uint8_t cmd = 0;
        uint8_t sock = 0;
        uint8_t arg = 0;
        uint8_t seq = 0;
        bool crc = false;
        std::vector<uint8_t> payload;
    };

    /// One complete request from the receive buffer, if there is one
    bool NextRequest(Request& out);
    void Handle(const Request& r);
    void Reply(const Request& r, uint8_t sock, uint8_t status, uint16_t result, const std::vector<uint8_t>& payload = {},
               uint64_t turnaroundUs = kTurnaroundUs);
    void Error(const Request& r, uint8_t sock, uint8_t status) { Reply(r, sock, status, 0); }

    void DoSocket(const Request& r);
    void DoShutdown(const Request& r);
    void DoConnect(const Request& r);
    void DoBind(const Request& r);
    void DoListen(const Request& r);
    void DoAccept(const Request& r);
    void DoRead(const Request& r);
    void DoWrite(const Request& r);
    void DoGetDns(const Request& r);
    void DoDnsResolve(const Request& r);
    void DoInfo(const Request& r);
    void DoWifiScan(const Request& r);
    void DoWifiConnect(const Request& r);
    void DoWifiStatus(const Request& r);
    void DoUart(const Request& r);

    static NetEndpoint ParseSockaddr(const uint8_t* p);
    static void PutSockaddr(std::vector<uint8_t>& out, const NetEndpoint& e);

    /// A request waiting for the network (CONNECT, DNSRESOLVE)
    bool _busy = false;
    Request _pending;
    bool _held = false;   ///< WIFI_DISC holds the link down until WIFI_CONNECT
};
