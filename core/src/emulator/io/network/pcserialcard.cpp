#include "emulator/io/network/pcserialcard.h"

#include <cstdio>

#include "emulator/io/serial/esp/atmodule.h"
#include "emulator/io/serial/esp/espmodule.h"
#include "emulator/io/serial/hayesmodempeer.h"
#include "emulator/io/serial/serialpeer.h"
#include "emulator/io/sprinter/isa/isaslotconfig.h"

namespace
{
/// The UART of a preset's channel
Uart16550::Params UartParams(PcSerialCard::Preset preset, int channel)
{
    Uart16550::Params params = Uart16550::DefaultParams(Uart16550::Flavor::Chip16550);
    switch (preset)
    {
        case PcSerialCard::Preset::SprinterEsp:
            // A genuine TL16C550C at 14.7456 MHz. Only CTS comes from the ESP; DSR / DCD / RI are not connected
            params.uartClockHz = sprinterisa::kSprinterEspUartClock;
            params.ctsOnly = true;
            break;
        case PcSerialCard::Preset::Modem:
            // A 16550A (no auto flow control: MCR bits 7-5 read 0), every modem line from the modem
            params.uartClockHz = sprinterisa::kPcUartClock;
            params.mcrMask = 0x1F;
            break;
        case PcSerialCard::Preset::Dual16552:
            // PC16552D: a PC16550D per channel (no AFE) plus the AFR. COM1's modem inputs meet only the CH340's
            // inputs, COM2's DB-9 brings CTS alone: the others read inactive
            params.uartClockHz = sprinterisa::kPcUartClock;
            params.mcrMask = 0x1F;
            params.afr = true;
            params.msrWired = channel == 0 ? 0x00 : Uart16550::kMsrCts;
            params.msrUnwired = 0x00;
            break;
    }
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

PcSerialCard::PcSerialCard(EmulatorContext* context, const Settings& settings, std::unique_ptr<ISerialPeer> peer,
                           std::unique_ptr<ISerialPeer> peerB, std::string slotKey)
    : _settings(settings), _slotKey(std::move(slotKey))
{
    auto registerOf = [](uint16_t port) { return static_cast<int>(port & 0x07); };
    _com[0] = std::make_unique<ComPort>(context, UartParams(settings.preset, 0), std::move(peer), registerOf);
    if (settings.preset == Preset::Dual16552)
        _com[1] = std::make_unique<ComPort>(context, UartParams(settings.preset, 1), std::move(peerB), registerOf);
    if (settings.preset == Preset::SprinterEsp)
        _com[0]->Uart().onAuxLines = [this](bool out1, bool out2) { OnAuxLines(out1, out2); };
}

PcSerialCard::PcSerialCard(EmulatorContext* context, Preset preset, std::unique_ptr<ISerialPeer> peer, std::string slotKey)
    : PcSerialCard(context, Settings{preset, 0x3F8, 4, 0, false}, std::move(peer), nullptr, std::move(slotKey))
{
}

PcSerialCard::~PcSerialCard()
{
    for (auto& com : _com)
    {
        if (com)
        {
            com->Uart().onAuxLines = nullptr;
            com->Uart().onAdvance = nullptr;
        }
    }
}

EspModule* PcSerialCard::Esp() const
{
    return dynamic_cast<EspModule*>(_com[0]->Peer());
}

HayesModemPeer* PcSerialCard::Modem(int channel) const
{
    return dynamic_cast<HayesModemPeer*>(Com(channel).Peer());
}

const char* PcSerialCard::Kind() const
{
    switch (_settings.preset)
    {
        case Preset::Modem: return "modem";
        case Preset::Dual16552: return "dual16552";
        default: return "sprinteresp";
    }
}

void PcSerialCard::RestoreUart(int channel, const Uart16550::State& state)
{
    ComPort& com = Com(channel);
    com.Uart().LoadState(state);
    if (_settings.preset == Preset::SprinterEsp)
        OnAuxLines(com.Uart().Out1(), com.Uart().Out2());
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

uint16_t PcSerialCard::Base(int channel) const
{
    switch (_settings.preset)
    {
        case Preset::Modem: return _settings.base;
        case Preset::Dual16552: return channel == 0 ? sprinterisa::kSerialCom1Base : sprinterisa::kSerialCom2Base;
        default: return sprinterisa::kSprinterEspBase;
    }
}

int PcSerialCard::ChannelIrq(int channel) const
{
    switch (_settings.preset)
    {
        case Preset::Modem: return _settings.irq;
        case Preset::Dual16552:
        {
            const uint8_t jumper = channel == 0 ? _settings.irq : _settings.irqB;
            return jumper ? jumper : -1;
        }
        default: return sprinterisa::kSprinterEspIrq;
    }
}

bool PcSerialCard::Decodes(uint32_t address, uint16_t& offset) const
{
    switch (_settings.preset)
    {
        case Preset::Modem:
            // A PC card: A9-A3 against the base (A15-A10 not decoded: mirrors every #400); AEN is checked by the wrapper
            if ((address & 0x3F8u) != _settings.base)
                return false;
            offset = static_cast<uint16_t>(address & 0x07);
            return true;
        case Preset::Dual16552:
        {
            // 74ALS30: A9, A7-A3 high and (D3) A15-A10 low; A8 = CHSEL (1: channel A, COM1)
            const uint32_t mask = _settings.partialDecode ? 0x02F8u : 0xFEF8u;
            if ((address & mask) != 0x02F8u)
                return false;
            offset = static_cast<uint16_t>((address & 0x07) | ((address & 0x100) ? 0 : 0x08));
            return true;
        }
        default:
            // 74HC30 (A9-A6, A5, NOR(A4, A10), NOR(A11-A13)) and CS0 = A3: A13-A3 == #3E8 >> 3
            if ((address & 0x3FF8u) != sprinterisa::kSprinterEspBase)
                return false;
            offset = static_cast<uint16_t>(address & 0x07);
            return true;
    }
}

std::string PcSerialCard::DecodeNote() const
{
    switch (_settings.preset)
    {
        case Preset::Modem:
            return "A9-A3 against the COM base with AEN (a PC card): mirrored every #400 inside the window";
        case Preset::Dual16552:
            return _settings.partialDecode
                       ? "A9, A7-A3 (74ALS30; D3 left out, J1 + J2 closed: A15-A10 not decoded, mirrors every #400); A8 = "
                         "CHSEL (#3F8 COM1, #2F8 COM2); AEN and A19-A16 not connected: every #9FBD page"
                       : "A15-A10, A9, A7-A3 (74ALS30 + 74ALS27 D3); A8 = CHSEL (#3F8 COM1, #2F8 COM2); AEN and A19-A16 "
                         "not connected: every #9FBD page";
        default:
            return "A13-A3 decoded (74HC30 + 74HC27), AEN and A19-A14 not: no mirror inside the window, every #9FBD page";
    }
}

uint8_t PcSerialCard::Read(uint16_t offset)
{
    return Com((offset >> 3) & 1).portDeviceInMethod(offset & 0x07);
}

void PcSerialCard::Write(uint16_t offset, uint8_t value)
{
    const int channel = (offset >> 3) & 1;
    const uint8_t reg = static_cast<uint8_t>(offset & 0x07);
    if (_com[1])
    {
        // PC16552D: AFR bit 0 set = every write reaches both channels (CHSEL still picks the one a read sees)
        const bool both = (_com[0]->Uart().Afr() & 0x01) != 0;
        if (both)
        {
            _com[0]->portDeviceOutMethod(reg, value);
            _com[1]->portDeviceOutMethod(reg, value);
        }
        else
            Com(channel).portDeviceOutMethod(reg, value);
        // The AFR is one register whichever channel's set reaches it
        const uint8_t afr = Com(channel).Uart().Afr();
        _com[0]->Uart().SetAfr(afr);
        _com[1]->Uart().SetAfr(afr);
        return;
    }
    _com[0]->portDeviceOutMethod(reg, value);
}

uint8_t PcSerialCard::Peek(uint16_t offset) const
{
    // The registers as the UART shows them (the receive buffer is not popped: its byte is not in the view)
    const Uart16550& uart = Com((offset >> 3) & 1).Uart();
    const Uart16550::View v = uart.GetView();
    const bool dlab = (v.lcr & 0x80) != 0;
    switch (offset & 0x07)
    {
        case 0: return dlab ? static_cast<uint8_t>(v.divisor & 0xFF) : 0xFF;
        case 1: return dlab ? static_cast<uint8_t>(v.divisor >> 8) : v.ier;
        case 2: return (dlab && uart.GetParams().afr) ? v.afr : v.iir;
        case 3: return v.lcr;
        case 4: return v.mcr;
        case 5: return v.lsr;
        case 6: return v.msr;
        default: return v.scr;
    }
}

void PcSerialCard::Reset()
{
    // RESET DRV reaches the UARTs' MR only (SprinterESP: MCR 0 releases the ESP's RST, which then boots). The lines'
    // other ends keep their state: ComPort::Reset would also reset the peer
    for (auto& com : _com)
    {
        if (com)
            com->Uart().Reset();
    }
}

bool PcSerialCard::IoRange(uint32_t& first, uint32_t& last) const
{
    if (_settings.preset == Preset::Dual16552)
    {
        // Two windows (#2F8-#2FF COM2, #3F8-#3FF COM1): the report names both; the range spans them
        first = sprinterisa::kSerialCom2Base;
        last = sprinterisa::kSerialCom1Base + 7;
        return true;
    }
    first = Base(0);
    last = Base(0) + 7u;
    return true;
}

int PcSerialCard::IrqLine() const
{
    const int a = ChannelIrq(0);
    return a >= 0 || !_com[1] ? a : ChannelIrq(1);
}

bool PcSerialCard::ChannelIntr(int channel) const
{
    return Com(channel).Uart().IntrPin();
}

bool PcSerialCard::ChannelDriven(int channel) const
{
    switch (_settings.preset)
    {
        case Preset::Modem:
            // The PC convention: OUT2 enables the IRQ pin's tri-state driver (inactive in loopback mode, as the pin)
            return Com(0).Uart().Out2();
        case Preset::Dual16552:
            return ChannelIrq(channel) >= 0;   // push-pull INTR through its jumper, or nothing
        default:
            return true;   // SprinterESP: INTR straight to IRQ3
    }
}

bool PcSerialCard::Irq() const
{
    // The Sprinter joins every IRQ pin of a slot: two driving INTR outputs fight; the higher level is taken
    bool level = false;
    for (int ch = 0; ch < Channels(); ++ch)
    {
        if (ChannelDriven(ch) && ChannelIntr(ch))
            level = true;
    }
    return level;
}

bool PcSerialCard::IrqDriven() const
{
    for (int ch = 0; ch < Channels(); ++ch)
    {
        if (ChannelDriven(ch))
            return true;
    }
    return false;
}

bool PcSerialCard::IrqContention() const
{
    return Channels() == 2 && ChannelDriven(0) && ChannelDriven(1) && ChannelIntr(0) != ChannelIntr(1);
}

void PcSerialCard::SetIrqListener(std::function<void()> changed)
{
    for (auto& com : _com)
    {
        if (com)
            com->Uart().onAdvance = changed;
    }
}

uint64_t PcSerialCard::NextIrqEventAt() const
{
    uint64_t at = UINT64_MAX;
    for (const auto& com : _com)
    {
        if (com && com->Uart().NextEventAt() < at)
            at = com->Uart().NextEventAt();
    }
    return at;
}

void PcSerialCard::CatchUp()
{
    for (auto& com : _com)
    {
        if (com)
            com->Uart().Advance(com->Now());
    }
}

void PcSerialCard::OnFrame()
{
    for (auto& com : _com)
    {
        if (com)
            com->OnFrame();
    }
}

std::string PcSerialCard::IrqCause() const
{
    std::string text;
    for (int ch = 0; ch < Channels(); ++ch)
    {
        // The 16550's priority order (IIR bits 3-1): line status, received data, character timeout, THRE, modem status
        const Uart16550::View v = Com(ch).Uart().GetView();
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
        if (!text.empty())
            text += "; ";
        if (Channels() == 2)
            text += ch == 0 ? "COM1 " : "COM2 ";
        text += "IIR " + Hex(v.iir, 2) + " (" + what + "), IER " + Hex(v.ier, 2);
        switch (_settings.preset)
        {
            case Preset::Modem:
                text += "; IRQ " + std::to_string(_settings.irq) +
                        (Com(0).Uart().Out2() ? " driven (MCR OUT2 set)" : " not driven (MCR OUT2 clear: tri-state)");
                break;
            case Preset::Dual16552:
                text += ChannelIrq(ch) >= 0 ? "; INTR through J" + std::to_string(ch == 0 ? 5 : 6) + " to IRQ " +
                                                  std::to_string(ChannelIrq(ch))
                                            : std::string("; jumper J") + (ch == 0 ? "5" : "6") + " open";
                break;
            default:
                text += "; INTR wired to IRQ3, OUT2 does not gate it";
                break;
        }
    }
    if (IrqContention())
        text += "; both INTR outputs drive the slot's one IRQ line with opposite levels (contention)";
    return text;
}

const char* PcSerialCard::RegisterName(uint16_t offset, bool write) const
{
    const Uart16550& uart = Com((offset >> 3) & 1).Uart();
    const bool dlab = (uart.GetView().lcr & 0x80) != 0;
    switch (offset & 0x07)
    {
        case 0: return dlab ? "DLL" : (write ? "THR" : "RBR");
        case 1: return dlab ? "DLM" : "IER";
        case 2: return (dlab && uart.GetParams().afr) ? "AFR" : (write ? "FCR" : "IIR");
        case 3: return "LCR";
        case 4: return "MCR";
        case 5: return "LSR";
        case 6: return "MSR";
        default: return "SCR";
    }
}

void PcSerialCard::DescribeChannel(int channel, StateNode& out) const
{
    const ComPort& com = Com(channel);
    const Uart16550& uart = com.Uart();
    const Uart16550::View v = uart.GetView();
    out["port_key"] = PortKey(channel);
    out["base"] = Hex(Base(channel), 3);
    const int irq = ChannelIrq(channel);
    out["irq"] = irq;

    StateNode u = StateNode::Object();
    u["ier"] = Hex(v.ier, 2);
    u["iir"] = Hex(v.iir, 2);
    u["fcr"] = Hex(v.fcr, 2);
    u["lcr"] = Hex(v.lcr, 2);
    u["mcr"] = Hex(v.mcr, 2);
    u["lsr"] = Hex(v.lsr, 2);
    u["msr"] = Hex(v.msr, 2);
    u["scr"] = Hex(v.scr, 2);
    if (uart.GetParams().afr)
        u["afr"] = Hex(v.afr, 2);
    u["divisor"] = static_cast<int>(v.divisor);
    u["baud"] = static_cast<uint64_t>(uart.Baud());
    u["afe"] = (v.mcr & Uart16550::kMcrAfe) != 0;
    u["rts"] = (v.mcr & Uart16550::kMcrRts) != 0;
    u["dtr"] = (v.mcr & Uart16550::kMcrDtr) != 0;
    u["out2"] = (v.mcr & Uart16550::kMcrOut2) != 0;
    u["cts"] = (v.msr & Uart16550::kMsrCts) != 0;
    u["dsr"] = (v.msr & Uart16550::kMsrDsr) != 0;
    u["dcd"] = (v.msr & Uart16550::kMsrDcd) != 0;
    u["ri"] = (v.msr & Uart16550::kMsrRi) != 0;
    u["loopback"] = (v.mcr & Uart16550::kMcrLoop) != 0;
    u["intr"] = uart.IntrPin();
    u["rx_fifo"] = static_cast<int>(v.rxCount);
    u["tx_fifo"] = static_cast<int>(v.txCount);
    u["bytes_in"] = v.bytesIn;
    u["bytes_out"] = v.bytesOut;
    u["overruns"] = v.overruns;
    out["uart"] = u;

    StateNode peer = StateNode::Object();
    const ISerialPeer* p = com.Peer();
    peer["kind"] = p ? std::string(p->Kind()) : std::string("none");
    peer["target"] = p ? p->Target() : std::string();
    peer["connected"] = p ? p->Connected() : false;
    peer["pending"] = static_cast<uint64_t>(p ? p->Pending() : 0);
    out["peer"] = peer;

    if (const HayesModemPeer* modem = Modem(channel))
    {
        StateNode m = StateNode::Object();
        modem->Describe(m);
        out["modem"] = m;
    }
}

void PcSerialCard::Describe(StateNode& out) const
{
    switch (_settings.preset)
    {
        case Preset::Modem:
            out["board"] = "ISA internal Hayes modem (16550A UART + modem controller)";
            out["chip"] = "16550A";
            out["uart_clock_hz"] = static_cast<uint64_t>(sprinterisa::kPcUartClock);
            out["irq_gating"] = "MCR OUT2 enables the IRQ pin's tri-state driver (PC convention)";
            break;
        case Preset::Dual16552:
            out["board"] = "SprinterSerial rev 1.1.1 (Roman Boykov): COM1 USB (CH340), COM2 RS-232 (MAX232, DB-9)";
            out["chip"] = "PC16552D";
            out["uart_clock_hz"] = static_cast<uint64_t>(sprinterisa::kPcUartClock);
            out["irq_gating"] = "INTA / INTB push-pull through J5 / J6 (OUT2 not connected); the Sprinter joins every "
                                "IRQ pin of the slot";
            out["irq_contention"] = IrqContention();
            out["decode_jumpers"] = _settings.partialDecode ? "D3 not fitted, J1 + J2 closed" : "D3 fitted";
            out["modem_inputs"] = "COM1: none wired (the CH340's modem pins are inputs too): CTS / DSR / DCD / RI read "
                                  "inactive; COM2: CTS from the DB-9, DSR / DCD / RI read inactive";
            break;
        default:
            out["board"] = "SprinterESP rev 1.0.5 (Roman Boykov)";
            out["chip"] = "TL16C550C";
            out["uart_clock_hz"] = static_cast<uint64_t>(sprinterisa::kSprinterEspUartClock);
            out["irq_gating"] = "INTR straight to IRQ3 (OUT2 drives the ESP's GPIO0, not the interrupt)";
            break;
    }
    out["decode"] = DecodeNote();
    DescribeChannel(0, out);
    if (_com[1])
    {
        StateNode b = StateNode::Object();
        DescribeChannel(1, b);
        out["channel_b"] = b;
    }

    if (_settings.preset != Preset::SprinterEsp)
        return;
    const Uart16550& uart = _com[0]->Uart();
    StateNode pins = StateNode::Object();
    pins["out1_esp_reset"] = uart.Out1();
    pins["out2_esp_gpio0_low"] = uart.Out2();
    out["pins"] = pins;

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
