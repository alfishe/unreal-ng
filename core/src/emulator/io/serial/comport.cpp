#include "emulator/io/serial/comport.h"

#include <cstring>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"

ComPort::ComPort(EmulatorContext* context, const Uart16550::Params& params, std::unique_ptr<ISerialPeer> peer)
    : _context(context),
      _peer(std::move(peer)),
      _uart(params, context ? context->emulatorState.base_z80_frequency : 3500000)
{
    _uart.SetPeer(_peer.get());
    _uart.Reset();
    if (_peer)
        _peer->onReceive = [this]() { _uart.Advance(Now()); };
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

void ComPort::AddAccessWait()
{
    const uint32_t wait = _uart.GetParams().accessWaitT;
    if (wait && _context && _context->pCore && _context->pCore->GetZ80())
        _context->pCore->GetZ80()->AddWaitStates(wait);
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

INetGuest* ComPort::NetGuest() const
{
    return dynamic_cast<StreamPeer*>(_peer.get());
}

bool ComPort::SaveState(netstate::Com& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.present = 1;
    _uart.SaveState(out.uart);
    bool complete = true;
    if (const auto* loop = dynamic_cast<const LoopbackPeer*>(_peer.get()))
    {
        out.peerKind = 1;
        const auto& q = loop->Queue();
        const size_t n = q.size() < static_cast<size_t>(netstate::kMaxComBytes) ? q.size() : netstate::kMaxComBytes;
        for (size_t i = 0; i < n; ++i)
            out.loopback[i] = q[i];
        out.loopbackLength = static_cast<uint32_t>(n);
        complete = n == q.size();
    }
    else if (const auto* stream = dynamic_cast<const StreamPeer*>(_peer.get()))
    {
        out.peerKind = std::strcmp(stream->Kind(), "tcp") == 0 ? 2 : 3;
        stream->SaveLink(out.link);
        // Received bytes as runs of one journal record
        for (const StreamPeer::RxByte& b : stream->Received())
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
        const auto& tx = stream->Unsent();
        const size_t n = tx.size() < static_cast<size_t>(netstate::kMaxComBytes) ? tx.size() : netstate::kMaxComBytes;
        std::memcpy(out.unsent, tx.data(), n);
        out.unsentLength = static_cast<uint32_t>(n);
        complete = complete && n == tx.size();
    }
    return complete;
}

bool ComPort::LoadState(const netstate::Com& in, const ByteSource& bytes)
{
    if (!in.present)
        return false;
    _uart.LoadState(in.uart);
    bool complete = true;
    if (auto* loop = dynamic_cast<LoopbackPeer*>(_peer.get()))
    {
        loop->SetQueue(in.loopback, in.loopbackLength < static_cast<uint32_t>(netstate::kMaxComBytes)
                                        ? in.loopbackLength
                                        : static_cast<uint32_t>(netstate::kMaxComBytes));
    }
    else if (auto* stream = dynamic_cast<StreamPeer*>(_peer.get()))
    {
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
        stream->SetReceived(std::move(rx));
        stream->SetUnsent(in.unsent, in.unsentLength < static_cast<uint32_t>(netstate::kMaxComBytes)
                                          ? in.unsentLength
                                          : static_cast<uint32_t>(netstate::kMaxComBytes));
        stream->LoadLink(in.link);
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
    AddAccessWait();
    return _uart.Read(static_cast<uint8_t>((port >> 8) & 0x07), Now());
}

void ComPort::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    AddAccessWait();
    _uart.Write(static_cast<uint8_t>((port >> 8) & 0x07), value, Now());
}
