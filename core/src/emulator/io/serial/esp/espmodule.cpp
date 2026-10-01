#include "emulator/io/serial/esp/espmodule.h"

#include <algorithm>
#include <cstring>

#include "emulator/io/network/virtualnetwork.h"

EspModule::EspModule(VirtualNetwork* network, Chip chip, int slots)
    : _network(network), _chip(chip), _stack(std::make_unique<EspStack>(network, slots))
{
    // A fixed station MAC per chip family (Espressif OUIs): leases stay stable
    _mac = chip == Chip::Esp8266 ? std::array<uint8_t, 6>{0x5C, 0xCF, 0x7F, 0x5A, 0x00, 0x01}
                                 : std::array<uint8_t, 6>{0x24, 0x6F, 0x28, 0x5A, 0x00, 0x01};
    _stack->onDone = [this](const EspStack::Done& done) {
        OnStackDone(done);
        Process();
        if (onReceive)
            onReceive();
    };
    _stack->onData = [this](int slot) {
        OnStackData(slot);
        if (onReceive)
            onReceive();
    };
    // Powered up with the saved access point: the virtual one, joined
    _ssid = kVirtualSsid;
    _wifi = Wifi::GotIp;
    _ip = _network ? _network->LeaseFor(_mac) : 0;
}

EspModule::~EspModule() = default;

std::string EspModule::Target() const
{
    std::string text = std::string(_chip == Chip::Esp8266 ? "ESP8266" : "ESP32") + ", " +
                       (_wifi == Wifi::GotIp ? "Wi-Fi " + _ssid + " " + NetIpToString(_ip)
                                             : (_wifi == Wifi::Connecting ? "Wi-Fi connecting" : "Wi-Fi down"));
    if (_lineMismatch)
        text += ", UART line mismatch (module " + std::to_string(_baud) + " 8N1)";
    return text;
}

uint32_t EspModule::DnsServer() const
{
    return _network ? _network->Config().dnsServer : 0;
}

uint32_t EspModule::Gateway() const
{
    return _network ? _network->Config().gateway : 0;
}

uint32_t EspModule::Netmask() const
{
    return _network ? _network->Config().mask : 0;
}

void EspModule::OnLineSettings(const SerialLine& line)
{
    _zxLine = line;
    _lineMismatch = !(line.baud == _baud && line.dataBits == 8 && line.parity == 'N');
}

void EspModule::Transmit(uint8_t byte)
{
    UpdateWifi();
    // At another rate or format the module sees framing errors, not bytes
    if (_lineMismatch)
        return;
    if (_rx.size() >= kRxBuffer + 2100)
        return;   // the ring overflowed (the ZX ignored CTS)
    _rx.push_back(byte);
    Process();
}

bool EspModule::HasByte() const
{
    return !_out.empty() && !_lineMismatch && Now() >= _outReadyAt;
}

uint8_t EspModule::TakeByte()
{
    const uint8_t b = _out.front();
    _out.pop_front();
    return b;
}

void EspModule::Send(const uint8_t* data, size_t length, uint64_t turnaroundUs)
{
    if (!data || !length)
        return;
    if (_out.empty())
        _outReadyAt = Now() + MicrosToT(turnaroundUs);
    _out.insert(_out.end(), data, data + length);
    if (_textLog && !_log.empty() && _log.back().reply.size() < 160)
    {
        std::string& r = _log.back().reply;
        for (size_t i = 0; i < length && r.size() < 160; ++i)
        {
            const char c = static_cast<char>(data[i]);
            r += c == '\r' ? std::string("\\r") : c == '\n' ? std::string("\\n")
                 : (data[i] >= 32 && data[i] < 127) ? std::string(1, c) : std::string(".");
        }
    }
}

void EspModule::Send(const std::string& text, uint64_t turnaroundUs)
{
    Send(reinterpret_cast<const uint8_t*>(text.data()), text.size(), turnaroundUs);
}

void EspModule::LogRequest(std::string text)
{
    _log.push_back({std::move(text), {}});
    while (_log.size() > kExchangeLog)
        _log.pop_front();
}

void EspModule::LogReply(std::string text)
{
    if (!_log.empty() && _log.back().reply.empty())
        _log.back().reply = std::move(text);
    else
    {
        _log.push_back({{}, std::move(text)});
        while (_log.size() > kExchangeLog)
            _log.pop_front();
    }
}

void EspModule::Join(const std::string& ssid)
{
    _ssid = ssid;
    _wifi = Wifi::Connecting;
    _ip = 0;
    // Only the virtual access point exists: another name keeps connecting
    _wifiAt = ssid == kVirtualSsid ? Now() + MicrosToT(kJoinUs) : 0;
}

void EspModule::Leave()
{
    _wifi = Wifi::Idle;
    _ip = 0;
    _wifiAt = 0;
    _stack->StopAll();
}

void EspModule::SetBaud(uint32_t baud, uint64_t atT)
{
    _pendingBaud = baud;
    _pendingBaudAt = atT;
}

void EspModule::UpdateWifi()
{
    const uint64_t now = Now();
    if (_wifi == Wifi::Connecting && _wifiAt && now >= _wifiAt)
    {
        _wifi = Wifi::GotIp;
        _wifiAt = 0;
        _ip = _network ? _network->LeaseFor(_mac) : 0;
    }
    // The new rate after the reply that announced it left at the old one
    if (_pendingBaud && now >= _pendingBaudAt && _out.empty())
    {
        _baud = _pendingBaud;
        _pendingBaud = 0;
        OnLineSettings(_zxLine);
    }
}

void EspModule::OnFrame()
{
    UpdateWifi();
    _stack->OnFrame();
    Process();
}

bool EspModule::SaveState(netstate::EspModuleState& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.present = 1;
    out.chip = static_cast<uint8_t>(_chip);
    out.wifi = static_cast<uint8_t>(_wifi);
    out.lineMismatch = _lineMismatch ? 1 : 0;
    out.flowControl = _flowControl ? 1 : 0;
    std::memcpy(out.mac, _mac.data(), 6);
    std::strncpy(out.ssid, _ssid.c_str(), sizeof(out.ssid) - 1);
    out.ip = _ip;
    out.baud = _baud;
    out.pendingBaud = _pendingBaud;
    out.wifiAt = _wifiAt;
    out.pendingBaudAt = _pendingBaudAt;
    out.outReadyAt = _outReadyAt;
    out.requests = _requests;
    bool complete = true;
    out.rxLength = static_cast<uint32_t>(std::min<size_t>(_rx.size(), netstate::kEspRxBytes));
    std::copy(_rx.begin(), _rx.begin() + out.rxLength, out.rx);
    complete = complete && _rx.size() <= static_cast<size_t>(netstate::kEspRxBytes);
    out.outLength = static_cast<uint32_t>(std::min<size_t>(_out.size(), netstate::kEspOutBytes));
    std::copy(_out.begin(), _out.begin() + out.outLength, out.out);
    complete = complete && _out.size() <= static_cast<size_t>(netstate::kEspOutBytes);
    SaveFirmware(out);
    complete = _stack->SaveState(out.stack) && complete;
    return complete;
}

bool EspModule::LoadState(const netstate::EspModuleState& in, const EspStack::ByteSource& bytes)
{
    if (!in.present)
        return false;
    _wifi = in.wifi <= static_cast<uint8_t>(Wifi::GotIp) ? static_cast<Wifi>(in.wifi) : Wifi::Idle;
    _lineMismatch = in.lineMismatch != 0;
    _flowControl = in.flowControl != 0;
    std::memcpy(_mac.data(), in.mac, 6);
    _ssid.assign(in.ssid, strnlen(in.ssid, sizeof(in.ssid)));
    _ip = in.ip;
    _baud = in.baud;
    _pendingBaud = in.pendingBaud;
    _wifiAt = in.wifiAt;
    _pendingBaudAt = in.pendingBaudAt;
    _outReadyAt = in.outReadyAt;
    _requests = in.requests;
    _rx.assign(in.rx, in.rx + std::min<uint32_t>(in.rxLength, netstate::kEspRxBytes));
    _out.assign(in.out, in.out + std::min<uint32_t>(in.outLength, netstate::kEspOutBytes));
    LoadFirmware(in);
    return _stack->LoadState(in.stack, bytes);
}
