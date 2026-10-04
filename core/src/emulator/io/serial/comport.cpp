#include "emulator/io/serial/comport.h"
#include "emulator/io/network/vnet/ethernetgateway.h"

#include "emulator/io/network/atm2ioesp.h"
#include "emulator/io/network/zifi.h"
#include "emulator/io/network/networkmanager.h"

#include <cstring>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/serial/esp/espmodule.h"
#include "emulator/io/serial/hayesmodempeer.h"

ComPort::ComPort(EmulatorContext* context, const Uart16550::Params& params, std::unique_ptr<ISerialPeer> peer,
                 RegisterOf registerOf)
    : _context(context),
      _peer(std::move(peer)),
      _uart(params, context ? context->emulatorState.base_z80_frequency : 3500000),
      _registerOf(std::move(registerOf))
{
    _uart.SetPeer(_peer.get());
    _uart.Reset();
    if (_peer)
    {
        _peer->onReceive = [this]() { _uart.Advance(Now()); };
        _peer->SetClock([this]() { return Now(); },
                        context ? context->emulatorState.base_z80_frequency : 3500000);
    }
}

ComPort::~ComPort()
{
    DetachFromPorts();
    if (_peer)
        _peer->onReceive = nullptr;
    _uart.SetPeer(nullptr);
}

bool ComPort::AttachToPorts(PortDecoder* decoder)
{
    DetachFromPorts();
    if (!decoder || !decoder->RegisterFullDecodeLowBytePort(kPortLowByte, this))
        return false;
    _decoder = decoder;
    return true;
}

void ComPort::DetachFromPorts()
{
    if (_decoder)
    {
        _decoder->UnregisterFullDecodeLowBytePort(kPortLowByte, this);
        _decoder = nullptr;
    }
}

uint64_t ComPort::Now() const
{
    if (!_context || !_context->pCore || !_context->pCore->GetZ80())
        return 0;
    const uint32_t multiplier = _context->emulatorState.current_z80_frequency_multiplier
                                    ? _context->emulatorState.current_z80_frequency_multiplier
                                    : 1u;
    return _context->emulatorState.t_states + _context->pCore->GetZ80()->t / multiplier;
}

void ComPort::AddAccessWait(uint8_t reg, bool read)
{
    if (!_context || !_context->pCore || !_context->pCore->GetZ80())
        return;
    const uint32_t cycles = _uart.AccessCycles(reg, read, Now());
    if (!cycles)
        return;
    // The AVR's time in the CPU's clocks at its current speed (turbo waits longer in T-states)
    const uint64_t cpuHz = _context->emulatorState.current_z80_frequency ? _context->emulatorState.current_z80_frequency
                                                                          : _context->emulatorState.base_z80_frequency;
    const uint32_t avrHz = _uart.GetParams().avrClockHz;
    const uint32_t clocks = static_cast<uint32_t>((static_cast<uint64_t>(cycles) * cpuHz + avrHz - 1) / avrHz);
    _context->pCore->GetZ80()->AddWaitStates(clocks);
}

void ComPort::Reset()
{
    // ZX-Evo: the AVR keeps its UART through a Z80 reset (only its own hard
    // reset clears it). ZX-WiFi: ZX-Bus /RESET resets the 16550
    if (_uart.GetParams().flavor == Uart16550::Flavor::EvoAvr)
        return;
    _uart.Reset();
    if (_peer)
        _peer->Reset();
}

INetGuest* ComPort::NetGuestOf(ISerialPeer* peer)
{
    if (auto* esp = dynamic_cast<EspModule*>(peer))
        return &esp->Stack();
    if (auto* modem = dynamic_cast<HayesModemPeer*>(peer))
        return modem;   // its link's sockets and its listener are the modem's
    return dynamic_cast<StreamPeer*>(peer);
}

INetGuest* ComPort::NetGuest() const
{
    return NetGuestOf(_peer.get());
}

SerialGuests ComPort::SerialNetGuests(const EmulatorContext* context)
{
    SerialGuests guests;
    if (!context)
        return guests;
    if (context->pComPort)
        guests.com = context->pComPort->NetGuest();
    guests.machine = NetGuestOf(context->pMachineSerialPeer);
    if (context->pAtm2IoEsp)
        guests.atmIo = context->pAtm2IoEsp->Com().NetGuest();
    if (context->pZiFi)
        guests.zifi = context->pZiFi->Line().NetGuest();
    guests.ethernet = context->pEthernetGateway;
    if (context->pCore)
    {
        if (const NetworkManager* manager = context->pCore->GetNetworkManager())
        {
            for (int n = 0; n < 2; ++n)
            {
                if (const PcSerialCard* card = manager->SerialCard("isa" + std::to_string(n + 1)))
                {
                    guests.slotUart[n] = card->Com(0).NetGuest();
                    if (card->Channels() > 1)
                        guests.slotUartB[n] = card->Com(1).NetGuest();
                }
            }
        }
    }
    return guests;
}

bool ComPort::SaveState(netstate::Com& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.present = 1;
    _uart.SaveState(out.uart);
    return SavePeer(_peer.get(), out);
}

bool ComPort::LoadState(const netstate::Com& in, const ByteSource& bytes)
{
    if (!in.present)
        return false;
    _uart.LoadState(in.uart);
    return LoadPeer(_peer.get(), in, bytes);
}

bool ComPort::SaveStream(const StreamPeer& stream, netstate::Com& out)
{
    bool complete = true;
    stream.SaveLink(out.link);
    // Received bytes as runs of one journal record
    for (const StreamPeer::RxByte& b : stream.Received())
    {
        netstate::Reference* last = out.runCount ? &out.runs[out.runCount - 1] : nullptr;
        if (last && last->source == b.source && last->sourceOffset + last->length == b.offset && b.source != 0)
        {
            ++last->length;
            continue;
        }
        if (out.runCount >= static_cast<uint32_t>(netstate::kMaxComRuns) || b.source == 0)
        {
            complete = false;   // too fragmented, or bytes that were not journaled
            break;
        }
        out.runs[out.runCount++] = {b.source, b.offset, 1};
    }
    const auto& tx = stream.Unsent();
    const size_t n = tx.size() < static_cast<size_t>(netstate::kMaxComBytes) ? tx.size() : netstate::kMaxComBytes;
    std::memcpy(out.unsent, tx.data(), n);
    out.unsentLength = static_cast<uint32_t>(n);
    return complete && n == tx.size();
}

bool ComPort::LoadStream(StreamPeer& stream, const netstate::Com& in, const ByteSource& bytes)
{
    bool complete = true;
    std::deque<StreamPeer::RxByte> rx;
    std::vector<uint8_t> chunk;
    for (uint32_t r = 0; r < in.runCount && r < static_cast<uint32_t>(netstate::kMaxComRuns); ++r)
    {
        const netstate::Reference& ref = in.runs[r];
        if (!bytes || !bytes(ref.source, ref.sourceOffset, ref.length, chunk) || chunk.size() != ref.length)
        {
            complete = false;
            continue;
        }
        for (uint32_t i = 0; i < ref.length; ++i)
            rx.push_back({chunk[i], ref.source, ref.sourceOffset + i});
    }
    stream.SetReceived(std::move(rx));
    stream.SetUnsent(in.unsent, in.unsentLength < static_cast<uint32_t>(netstate::kMaxComBytes)
                                    ? in.unsentLength
                                    : static_cast<uint32_t>(netstate::kMaxComBytes));
    stream.LoadLink(in.link);
    return complete;
}

bool ComPort::SavePeer(const ISerialPeer* peer, netstate::Com& out)
{
    bool complete = true;
    if (const auto* loop = dynamic_cast<const LoopbackPeer*>(peer))
    {
        out.peerKind = 1;
        const auto& q = loop->Queue();
        const size_t n = q.size() < static_cast<size_t>(netstate::kMaxComBytes) ? q.size() : netstate::kMaxComBytes;
        for (size_t i = 0; i < n; ++i)
            out.loopback[i] = q[i];
        out.loopbackLength = static_cast<uint32_t>(n);
        complete = n == q.size();
    }
    else if (const auto* esp = dynamic_cast<const EspModule*>(peer))
    {
        // 4 ESPNET, 5 AT, 7 the ZiFi native firmware (netstate::Com::peerKind)
        out.peerKind = std::strcmp(esp->Kind(), "espnet") == 0 ? 4 : std::strcmp(esp->Kind(), "zifi-native") == 0 ? 7 : 5;
        complete = esp->SaveState(out.esp) && complete;
    }
    else if (const auto* stream = dynamic_cast<const StreamPeer*>(peer))
    {
        out.peerKind = std::strcmp(stream->Kind(), "tcp") == 0 ? 2 : 3;
        complete = SaveStream(*stream, out) && complete;
    }
    else if (const auto* modem = dynamic_cast<const HayesModemPeer*>(peer))
    {
        out.peerKind = 6;
        modem->SaveState(out.modem);
        complete = SaveStream(modem->Link(), out) && complete;
    }
    return complete;
}

bool ComPort::LoadPeer(ISerialPeer* peer, const netstate::Com& in, const ByteSource& bytes)
{
    bool complete = true;
    if (auto* loop = dynamic_cast<LoopbackPeer*>(peer))
    {
        loop->SetQueue(in.loopback, in.loopbackLength < static_cast<uint32_t>(netstate::kMaxComBytes)
                                        ? in.loopbackLength
                                        : static_cast<uint32_t>(netstate::kMaxComBytes));
    }
    else if (auto* esp = dynamic_cast<EspModule*>(peer))
    {
        if (in.esp.present && (in.peerKind == 4 || in.peerKind == 5 || in.peerKind == 7))
            complete = esp->LoadState(in.esp, bytes) && complete;
    }
    else if (auto* stream = dynamic_cast<StreamPeer*>(peer))
    {
        complete = LoadStream(*stream, in, bytes) && complete;
    }
    else if (auto* modem = dynamic_cast<HayesModemPeer*>(peer))
    {
        if (in.peerKind == 6)
        {
            modem->LoadState(in.modem);   // first: it gives the link its dial target
            complete = LoadStream(modem->Link(), in, bytes) && complete;
        }
    }
    return complete;
}

void ComPort::OnFrame()
{
    _uart.Advance(Now());
    if (_peer)
        _peer->OnFrame();
}

uint8_t ComPort::portDeviceInMethod(uint16_t port)
{
    const int reg = Register(port);
    AddAccessWait(static_cast<uint8_t>(reg < 0 ? 5 : reg), true);   // every #xxEF access waits for the AVR
    if (reg == kDataRegion)
        return _zifi ? _zifi->Read(ZiFi::kData) : 0xFF;
    if (ComPortRegister::IsZiFi(reg))
        return _zifi ? _zifi->Read(static_cast<uint8_t>(ZiFi::kZifr + (reg - ComPortRegister::kZiFiBase))) : 0xFF;
    if (reg == kNothing)
        return 0x00;
    return _uart.Read(static_cast<uint8_t>(reg), Now());
}

void ComPort::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    const int reg = Register(port);
    AddAccessWait(static_cast<uint8_t>(reg < 0 ? 5 : reg), false);
    if (reg == kDataRegion && _zifi)
        _zifi->Write(ZiFi::kData, value);
    else if (ComPortRegister::IsZiFi(reg) && _zifi)
        _zifi->Write(static_cast<uint8_t>(ZiFi::kZifr + (reg - ComPortRegister::kZiFiBase)), value);
    if (reg < 0)
        return;
    _uart.Write(static_cast<uint8_t>(reg), value, Now());
}
