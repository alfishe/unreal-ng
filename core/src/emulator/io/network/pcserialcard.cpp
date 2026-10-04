#include "emulator/io/network/pcserialcard.h"

#include <cstdio>

#include "emulator/io/serial/esp/atmodule.h"
#include "emulator/io/serial/esp/espmodule.h"
#include "emulator/io/serial/serialpeer.h"
#include "emulator/io/sprinter/isa/isaslotconfig.h"

namespace
{
/// A genuine TL16C550C at 14.7456 MHz. Only CTS comes from the ESP; DSR / DCD / RI are not connected
Uart16550::Params SprinterEspUart()
{
    Uart16550::Params params = Uart16550::DefaultParams(Uart16550::Flavor::Chip16550);
    params.uartClockHz = sprinterisa::kSprinterEspUartClock;
    params.ctsOnly = true;
    return params;
}

std::string Hex(unsigned value, int digits)
{
    char text[16];
    std::snprintf(text, sizeof(text), "#%0*X", digits, value);
    return text;
}

std::string MacText(const std::array<uint8_t, 6>& m)
{
    char text[24];
    std::snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    return text;
}
}  // namespace

PcSerialCard::PcSerialCard(EmulatorContext* context, Preset preset, std::unique_ptr<ISerialPeer> peer, std::string slotKey)
    : _preset(preset),
      _slotKey(std::move(slotKey)),
      _com(context, SprinterEspUart(), std::move(peer), [](uint16_t port) { return static_cast<int>(port & 0x07); })
{
    _com.Uart().onAuxLines = [this](bool out1, bool out2) { OnAuxLines(out1, out2); };
}

PcSerialCard::~PcSerialCard()
{
    _com.Uart().onAuxLines = nullptr;
    _com.Uart().onAdvance = nullptr;
}

EspModule* PcSerialCard::Esp() const
{
    return dynamic_cast<EspModule*>(_com.Peer());
}

void PcSerialCard::RestoreUart(const Uart16550::State& state)
{
    _com.Uart().LoadState(state);
    OnAuxLines(_com.Uart().Out1(), _com.Uart().Out2());
}

void PcSerialCard::OnAuxLines(bool out1, bool out2)
{
    // -OUT2 -> GPIO0 first: the boot ROM samples it when RST rises
    if (EspModule* esp = Esp())
    {
        esp->SetFlashPin(out2);
        esp->SetResetPin(out1);
    }
}

bool PcSerialCard::Decodes(uint32_t address, uint16_t& offset) const
{
    // 74HC30 (A9-A6, A5, NOR(A4, A10), NOR(A11-A13)) and CS0 = A3: A13-A3 == #3E8 >> 3
    if ((address & 0x3FF8u) != sprinterisa::kSprinterEspBase)
        return false;
    offset = static_cast<uint16_t>(address & 0x07);
    return true;
}

std::string PcSerialCard::DecodeNote() const
{
    return "A13-A3 decoded (74HC30 + 74HC27), AEN and A19-A14 not: no mirror inside the window, every #9FBD page";
}

uint8_t PcSerialCard::Read(uint16_t offset)
{
    return _com.portDeviceInMethod(offset & 0x07);
}

void PcSerialCard::Write(uint16_t offset, uint8_t value)
{
    _com.portDeviceOutMethod(offset & 0x07, value);
}

uint8_t PcSerialCard::Peek(uint16_t offset) const
{
    // The registers as the UART shows them (the receive buffer is not popped: its byte is not in the view)
    const Uart16550::View v = _com.Uart().GetView();
    const bool dlab = (v.lcr & 0x80) != 0;
    switch (offset & 0x07)
    {
        case 0: return dlab ? static_cast<uint8_t>(v.divisor & 0xFF) : 0xFF;
        case 1: return dlab ? static_cast<uint8_t>(v.divisor >> 8) : v.ier;
        case 2: return v.iir;
        case 3: return v.lcr;
        case 4: return v.mcr;
        case 5: return v.lsr;
        case 6: return v.msr;
        default: return v.scr;
    }
}

void PcSerialCard::Reset()
{
    // RESET DRV reaches the 16550's MR only: the UART to its reset values (MCR 0 releases the ESP's RST, which
    // then boots). The line's other end keeps its state: ComPort::Reset would also reset the peer
    _com.Uart().Reset();
}

bool PcSerialCard::IoRange(uint32_t& first, uint32_t& last) const
{
    first = sprinterisa::kSprinterEspBase;
    last = sprinterisa::kSprinterEspBase + 7;
    return true;
}

int PcSerialCard::IrqLine() const
{
    return sprinterisa::kSprinterEspIrq;
}

bool PcSerialCard::Irq() const
{
    return _com.Uart().IntrPin();
}

std::string PcSerialCard::IrqCause() const
{
    // The 16550's priority order (IIR bits 3-1): line status, received data, character timeout, THRE, modem status
    const Uart16550::View v = _com.Uart().GetView();
    const char* what = "none pending";
    switch (v.iir & 0x0F)
    {
        case 0x06: what = "line status (overrun)"; break;
        case 0x04: what = "received data at the FIFO trigger level"; break;
        case 0x0C: what = "received data below the trigger level (character timeout)"; break;
        case 0x02: what = "transmitter holding register empty"; break;
        case 0x00: what = "modem status"; break;
        default: break;
    }
    return "IIR " + Hex(v.iir, 2) + " (" + what + "), IER " + Hex(v.ier, 2) + "; INTR wired to IRQ3, OUT2 does not gate it";
}

const char* PcSerialCard::RegisterName(uint16_t offset, bool write) const
{
    const bool dlab = (_com.Uart().GetView().lcr & 0x80) != 0;
    switch (offset & 0x07)
    {
        case 0: return dlab ? "DLL" : (write ? "THR" : "RBR");
        case 1: return dlab ? "DLM" : "IER";
        case 2: return write ? "FCR" : "IIR";
        case 3: return "LCR";
        case 4: return "MCR";
        case 5: return "LSR";
        case 6: return "MSR";
        default: return "SCR";
    }
}

void PcSerialCard::Describe(StateNode& out) const
{
    const Uart16550& uart = _com.Uart();
    const Uart16550::View v = uart.GetView();
    out["board"] = "SprinterESP rev 1.0.5 (Roman Boykov)";
    out["chip"] = "TL16C550C";
    out["uart_clock_hz"] = static_cast<uint64_t>(sprinterisa::kSprinterEspUartClock);
    out["base"] = Hex(sprinterisa::kSprinterEspBase, 3);
    out["irq"] = static_cast<int>(sprinterisa::kSprinterEspIrq);
    out["irq_gating"] = "INTR straight to IRQ3 (OUT2 drives the ESP's GPIO0, not the interrupt)";
    out["port_key"] = PortKey();
    out["decode"] = DecodeNote();

    StateNode u = StateNode::Object();
    u["ier"] = Hex(v.ier, 2);
    u["iir"] = Hex(v.iir, 2);
    u["fcr"] = Hex(v.fcr, 2);
    u["lcr"] = Hex(v.lcr, 2);
    u["mcr"] = Hex(v.mcr, 2);
    u["lsr"] = Hex(v.lsr, 2);
    u["msr"] = Hex(v.msr, 2);
    u["scr"] = Hex(v.scr, 2);
    u["divisor"] = static_cast<int>(v.divisor);
    u["baud"] = static_cast<uint64_t>(uart.Baud());
    u["afe"] = (v.mcr & Uart16550::kMcrAfe) != 0;
    u["rts"] = (v.mcr & Uart16550::kMcrRts) != 0;
    u["cts"] = (v.msr & Uart16550::kMsrCts) != 0;
    u["loopback"] = (v.mcr & Uart16550::kMcrLoop) != 0;
    u["intr"] = uart.IntrPin();
    u["rx_fifo"] = static_cast<int>(v.rxCount);
    u["tx_fifo"] = static_cast<int>(v.txCount);
    u["bytes_in"] = v.bytesIn;
    u["bytes_out"] = v.bytesOut;
    u["overruns"] = v.overruns;
    out["uart"] = u;

    StateNode pins = StateNode::Object();
    pins["out1_esp_reset"] = uart.Out1();
    pins["out2_esp_gpio0_low"] = uart.Out2();
    out["pins"] = pins;

    StateNode peer = StateNode::Object();
    const ISerialPeer* p = _com.Peer();
    peer["kind"] = p ? std::string(p->Kind()) : std::string("none");
    peer["target"] = p ? p->Target() : std::string();
    peer["connected"] = p ? p->Connected() : false;
    peer["pending"] = static_cast<uint64_t>(p ? p->Pending() : 0);
    out["peer"] = peer;

    const EspModule* esp = Esp();
    if (!esp)
        return;
    StateNode e = StateNode::Object();
    const auto* at = dynamic_cast<const AtModule*>(esp);
    e["module"] = "ESP-12F (ESP8266)";
    e["firmware"] = at ? std::string(EspModule::FirmwareName(at->GetFirmware())) : std::string(esp->Kind());
    e["state"] = esp->ResetHeld() ? "reset held (MCR OUT1)" : esp->DownloadMode() ? "ROM download mode (GPIO0 was low)" : "running";
    e["hardware_resets"] = static_cast<uint64_t>(esp->HardwareResets());
    static const char* const kWifi[] = {"idle", "connecting", "got_ip"};
    e["wifi"] = kWifi[static_cast<int>(esp->GetWifi()) % 3];
    e["ssid"] = esp->Ssid();
    e["ip"] = NetIpToString(esp->Ip());
    e["mac"] = MacText(esp->Mac());
    e["baud"] = static_cast<uint64_t>(esp->Baud());
    e["factory_baud"] = static_cast<uint64_t>(esp->FactoryBaud());
    e["flow_control"] = esp->HonorsRts();
    e["line_mismatch"] = esp->LineMismatch();
    e["requests"] = esp->RequestsServed();
    if (at)
    {
        StateNode s = StateNode::Object();
        s["echo"] = at->Echo();
        s["mux"] = at->Mux();
        s["passive_receive"] = at->Passive();
        s["sysstore"] = at->SysStore();
        if (at->ManualDns(0))
            s["dns"] = NetIpToString(at->ManualDns(0)) + (at->ManualDns(1) ? "," + NetIpToString(at->ManualDns(1)) : "");
        StateNode links = StateNode::Array();
        for (int i = 0; i < AtModule::kLinks; ++i)
        {
            if (!at->LinkOpen(i))
                continue;
            const EspStack::Slot& slot = at->Stack().GetSlot(i);
            StateNode l = StateNode::Object();
            l["link"] = i;
            l["proto"] = at->LinkUdp(i) ? "udp" : "tcp";
            l["remote"] = NetIpToString(slot.remote.addr) + ":" + std::to_string(slot.remote.port);
            l["rx_pending"] = static_cast<uint64_t>(slot.rx.size());
            l["peer_closed"] = slot.finSeen;
            links.push(l);
        }
        s["links"] = links;
        e["at_session"] = s;
    }
    StateNode ex = StateNode::Array();
    for (const EspModule::Exchange& x : esp->RecentExchanges())
    {
        StateNode one = StateNode::Object();
        one["request"] = x.request;
        one["reply"] = x.reply;
        ex.push(one);
    }
    e["exchanges"] = ex;
    out["esp"] = e;
}
