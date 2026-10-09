#include "emulator/io/serial/comport.h"
#include "emulator/io/network/vnet/ethernetgateway.h"

#include "emulator/io/network/atm2ioesp.h"
#include "emulator/io/network/zifi.h"
#include "emulator/io/network/networkmanager.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <vector>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/atm/evoavrwait.h"
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
    // ZX-Evo: the AVR serves the access from its main loop, the one the Gluk clock port waits on too
    // (reference-evo-com-port.md §3); the AVR's time in the CPU's clocks at its current speed
    if (!_avrWait || !_context || !_context->pCore || !_context->pCore->GetZ80())
        return;
    const uint32_t service = _uart.ServiceCycles(reg, read);
    if (!service)
        return;
    const uint32_t baseHz = _context->emulatorState.base_z80_frequency ? _context->emulatorState.base_z80_frequency
                                                                        : 3500000;
    _avrWait->HoldCpu(_context, _avrWait->Access(service, 0, Now(), baseHz));
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

void ComPort::SaveState(netstate::Com& out, netstate::Tail& tail) const
{
    std::memset(&out, 0, sizeof(out));
    out.present = 1;
    _uart.SaveState(out.uart);
    SavePeer(_peer.get(), out, tail);
}

bool ComPort::LoadState(const netstate::Com& in, const netstate::Tail& tail, const ByteSource& bytes)
{
    if (!in.present)
        return false;
    _uart.LoadState(in.uart);
    return LoadPeer(_peer.get(), in, tail, bytes);
}

namespace
{
/// The first `max` bytes of `data` into `fixed`, the rest as one tail record of `list`; returns the fixed length
template <typename Container>
uint32_t SaveBytes(const Container& data, uint8_t* fixed, size_t max, netstate::Tail& tail, netstate::Tail::List list)
{
    const size_t n = data.size() < max ? data.size() : max;
    std::copy(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(n), fixed);
    if (data.size() > max)
    {
        const std::vector<uint8_t> rest(data.begin() + static_cast<std::ptrdiff_t>(max), data.end());
        tail.AddBytes(list, 0, 0, rest.data(), rest.size());
    }
    return static_cast<uint32_t>(n);
}

/// Back from SaveBytes
std::vector<uint8_t> LoadBytes(const uint8_t* fixed, uint32_t length, size_t max, const netstate::Tail& tail,
                               netstate::Tail::List list)
{
    std::vector<uint8_t> data(fixed, fixed + (length < max ? length : max));
    if (const std::vector<uint8_t>* rest = tail.Find(list, 0, 0))
        data.insert(data.end(), rest->begin(), rest->end());
    return data;
}
}  // namespace

void ComPort::SaveStream(const StreamPeer& stream, netstate::Com& out, netstate::Tail& tail)
{
    using List = netstate::Tail::List;
    stream.SaveLink(out.link);
    // Received bytes as runs of one journal record
    out.runCount = netstate::SaveRuns(stream.Received().begin(), stream.Received().end(), out.runs, netstate::kMaxComRuns,
                                      tail, List::ComRun, List::ComRunBytes, 0);
    out.unsentLength = SaveBytes(stream.Unsent(), out.unsent, netstate::kMaxComBytes, tail, List::ComUnsent);
}

bool ComPort::LoadStream(StreamPeer& stream, const netstate::Com& in, const netstate::Tail& tail, const ByteSource& bytes)
{
    using List = netstate::Tail::List;
    std::deque<StreamPeer::RxByte> rx;
    const bool complete = netstate::LoadRuns(in.runs, in.runCount, netstate::kMaxComRuns, tail, List::ComRun,
                                             List::ComRunBytes, 0, bytes, rx);
    stream.SetReceived(std::move(rx));
    const std::vector<uint8_t> unsent = LoadBytes(in.unsent, in.unsentLength, netstate::kMaxComBytes, tail, List::ComUnsent);
    stream.SetUnsent(unsent.data(), static_cast<uint32_t>(unsent.size()));
    stream.LoadLink(in.link);
    return complete;
}

void ComPort::SavePeer(const ISerialPeer* peer, netstate::Com& out, netstate::Tail& tail)
{
    if (const auto* loop = dynamic_cast<const LoopbackPeer*>(peer))
    {
        out.peerKind = 1;
        out.loopbackLength = SaveBytes(loop->Queue(), out.loopback, netstate::kMaxComBytes, tail,
                                       netstate::Tail::List::ComLoopback);
    }
    else if (const auto* esp = dynamic_cast<const EspModule*>(peer))
    {
        // 4 ESPNET, 5 AT, 7 the ZiFi native firmware (netstate::Com::peerKind)
        out.peerKind = std::strcmp(esp->Kind(), "espnet") == 0 ? 4 : std::strcmp(esp->Kind(), "zifi-native") == 0 ? 7 : 5;
        esp->SaveState(out.esp, tail);
    }
    else if (const auto* stream = dynamic_cast<const StreamPeer*>(peer))
    {
        out.peerKind = std::strcmp(stream->Kind(), "tcp") == 0 ? 2 : 3;
        SaveStream(*stream, out, tail);
    }
    else if (const auto* modem = dynamic_cast<const HayesModemPeer*>(peer))
    {
        out.peerKind = 6;
        modem->SaveState(out.modem);
        SaveStream(modem->Link(), out, tail);
    }
}

bool ComPort::LoadPeer(ISerialPeer* peer, const netstate::Com& in, const netstate::Tail& tail, const ByteSource& bytes)
{
    bool complete = true;
    if (auto* loop = dynamic_cast<LoopbackPeer*>(peer))
    {
        const std::vector<uint8_t> queue =
            LoadBytes(in.loopback, in.loopbackLength, netstate::kMaxComBytes, tail, netstate::Tail::List::ComLoopback);
        loop->SetQueue(queue.data(), static_cast<uint32_t>(queue.size()));
    }
    else if (auto* esp = dynamic_cast<EspModule*>(peer))
    {
        if (in.esp.present && (in.peerKind == 4 || in.peerKind == 5 || in.peerKind == 7))
            complete = esp->LoadState(in.esp, tail, bytes) && complete;
    }
    else if (auto* stream = dynamic_cast<StreamPeer*>(peer))
    {
        complete = LoadStream(*stream, in, tail, bytes) && complete;
    }
    else if (auto* modem = dynamic_cast<HayesModemPeer*>(peer))
    {
        if (in.peerKind == 6)
        {
            modem->LoadState(in.modem);   // first: it gives the link its dial target
            complete = LoadStream(modem->Link(), in, tail, bytes) && complete;
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
