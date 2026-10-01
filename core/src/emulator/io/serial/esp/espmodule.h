#pragma once

/// @file espmodule.h
/// @brief An emulated ESP8266 / ESP32 module on the COM port (network TDD
/// §7.2, step N3): the part every firmware shares. The UART side (bytes from
/// the ZX into a 4 KB receive buffer, replies out after a turnaround), the
/// line format the module runs at, the Wi-Fi link to the virtual network's
/// access point, and the socket stack (EspStack). A firmware (ESPNET, AT)
/// derives from it and parses / answers.
///
/// Deterministic: the module acts on bytes from the ZX, on journaled network
/// events and on the emulated clock only.

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "common/network/nettypes.h"
#include "emulator/io/network/netstate.h"
#include "emulator/io/serial/esp/espstack.h"
#include "emulator/io/serial/serialpeer.h"

class VirtualNetwork;

class EspModule : public ISerialPeer
{
public:
    enum class Chip : uint8_t
    {
        Esp32,
        Esp8266
    };

    /// Wi-Fi link state, as the ESPNET INFO byte numbers it
    enum class Wifi : uint8_t
    {
        Idle = 0,
        Connecting = 1,
        GotIp = 2
    };

    /// The access point the virtual network offers
    static constexpr const char* kVirtualSsid = "UnrealNG";
    static constexpr int8_t kVirtualRssi = -48;
    static constexpr uint8_t kVirtualChannel = 6;
    /// The time a join takes (CONNECTING, then GOT_IP)
    static constexpr uint64_t kJoinUs = 1500000;
    /// The receive buffer from the ZX (the firmwares' UART RX ring)
    static constexpr size_t kRxBuffer = 4096;

    /// @param slots the firmware's socket count (ESPNET: 8 / 4 by chip; AT: 5 links)
    EspModule(VirtualNetwork* network, Chip chip, int slots);
    ~EspModule() override;

    // ISerialPeer
    void Transmit(uint8_t byte) override;
    bool HasByte() const override;
    uint8_t TakeByte() override;
    bool Cts() const override { return _rx.size() < kRxBuffer; }
    bool Dsr() const override { return true; }
    bool Dcd() const override { return true; }
    void OnLineSettings(const SerialLine& line) override;
    void OnFrame() override;
    void Reset() override {}   // a Z80 reset does not reach the module
    std::string Target() const override;
    size_t Pending() const override { return _out.size(); }
    bool Connected() const override { return _wifi == Wifi::GotIp; }
    bool HonorsRts() const override { return _flowControl; }

    Chip GetChip() const { return _chip; }
    Wifi GetWifi() const { return _wifi; }
    uint32_t Ip() const { return _ip; }
    /// The DNS server the access point's DHCP gave (the virtual network's)
    uint32_t DnsServer() const;
    const std::array<uint8_t, 6>& Mac() const { return _mac; }
    const std::string& Ssid() const { return _ssid; }
    uint32_t Baud() const { return _baud; }
    /// The ZX's UART does not run at the module's line (bytes are lost both ways)
    bool LineMismatch() const { return _lineMismatch; }
    EspStack& Stack() { return *_stack; }
    const EspStack& Stack() const { return *_stack; }
    uint64_t RequestsServed() const { return _requests; }

    /// The last requests and their answers (status views; not TTD state)
    struct Exchange
    {
        std::string request;   ///< e.g. "CONNECT s0 seq 5 len 15" / "AT+CIPSTART=..."
        std::string reply;     ///< e.g. "ok result 0 len 0" / "OK"
    };
    static constexpr size_t kExchangeLog = 32;
    const std::deque<Exchange>& RecentExchanges() const { return _log; }

    /// TTD state of the shared part (netstate::EspModuleState); the firmware's
    /// own state rides in the same struct (SaveFirmware / LoadFirmware)
    bool SaveState(netstate::EspModuleState& out) const;
    bool LoadState(const netstate::EspModuleState& in, const EspStack::ByteSource& bytes);
    void RebindSockets() { _stack->RebindAll(); }

protected:
    /// The firmware parses what is in the receive buffer (called whenever
    /// bytes arrived or a pending operation finished)
    virtual void Process() = 0;
    virtual void OnStackDone(const EspStack::Done& done) = 0;
    virtual void OnStackData(int slot) { (void)slot; }
    virtual void SaveFirmware(netstate::EspModuleState& out) const = 0;
    virtual void LoadFirmware(const netstate::EspModuleState& in) = 0;

    /// Queue a reply; it reaches the ZX after the turnaround
    void Send(const uint8_t* data, size_t length, uint64_t turnaroundUs = kTurnaroundUs);
    void Send(const std::vector<uint8_t>& data, uint64_t turnaroundUs = kTurnaroundUs) { Send(data.data(), data.size(), turnaroundUs); }
    void Send(const std::string& text, uint64_t turnaroundUs = kTurnaroundUs);

    /// Wi-Fi control (WIFI_CONNECT / AT+CWJAP): joining the virtual AP
    /// succeeds after kJoinUs; any other name never comes up
    void Join(const std::string& ssid);
    void Leave();
    /// Change the module's own line (ESPNET UART SET, AT+UART_CUR) at `atT`
    void SetBaud(uint32_t baud, uint64_t atT);
    /// Hardware flow control on the module's side (AT+UART_CUR flow field)
    void SetFlowControl(bool on) { _flowControl = on; }
    /// The access point the module joined, its address on the virtual network
    uint32_t Gateway() const;
    uint32_t Netmask() const;

    std::deque<uint8_t> _rx;            ///< bytes from the ZX, not parsed yet
    uint64_t _requests = 0;
    void LogRequest(std::string text);
    void LogReply(std::string text);
    std::deque<Exchange> _log;
    bool _textLog = false;   ///< a text firmware: what it sends is the reply log

    /// Time from a complete request to the first byte of its reply
    static constexpr uint64_t kTurnaroundUs = 200;

private:
    void UpdateWifi();

    VirtualNetwork* _network = nullptr;
    Chip _chip = Chip::Esp32;
    std::unique_ptr<EspStack> _stack;
    std::deque<uint8_t> _out;           ///< reply bytes for the ZX
    uint64_t _outReadyAt = 0;           ///< the first queued byte may go from this time
    std::array<uint8_t, 6> _mac{};
    std::string _ssid;
    Wifi _wifi = Wifi::GotIp;
    uint32_t _ip = 0;
    uint64_t _wifiAt = 0;               ///< Connecting: the join finishes then
    uint32_t _baud = 115200;
    uint32_t _pendingBaud = 0;          ///< a line change waiting for its time
    uint64_t _pendingBaudAt = 0;
    SerialLine _zxLine;
    bool _lineMismatch = false;
    bool _flowControl = true;
};
