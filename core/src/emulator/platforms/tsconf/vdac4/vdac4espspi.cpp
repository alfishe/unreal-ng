#include "stdafx.h"

#include "vdac4espspi.h"

#include <algorithm>

#include "vdac4espstate.h"

namespace
{
constexpr uint32_t kDataStart = 3;  // command, address, dummy

bool HasAddress(uint8_t command)
{
    return command == Vdac4EspSpi::kCmdWriteBuffer || command == Vdac4EspSpi::kCmdReadBuffer ||
           command == Vdac4EspSpi::kCmdWriteDma || command == Vdac4EspSpi::kCmdReadDma;
}
}  // namespace

void Vdac4EspSpi::select(bool selected)
{
    if (_host)
        _host->OnBusAccess();
    if (selected == _selected)
        return;
    _selected = selected;
    if (selected)
    {
        _byteIndex = 0;
        _command = 0;
        _address = 0;
        return;
    }
    // End of the window: the transaction completes
    if (!_powered || _byteIndex == 0)
        return;
    switch (_command)
    {
        case kCmdWriteBuffer:
            if (_host)
                _host->OnBufferWritten();
            break;
        case kCmdWriteDone:
            if (_receive.queued && !_receive.done)
                _receive.done = true;
            break;
        case kCmdReadDone:
            if (_send.queued && !_send.done)
                _send.done = true;
            break;
        default:
            break;
    }
}

uint8_t Vdac4EspSpi::exchange(uint8_t mosi)
{
    if (_host)
        _host->OnBusAccess();
    if (!_selected || !_powered)
        return kIdleMiso;
    const uint32_t index = _byteIndex++;
    if (index == 0)
    {
        _command = mosi;
        if (!HasAddress(mosi) && mosi != kCmdWriteDone && mosi != kCmdReadDone)
            ++_badCommands;
        return kIdleMiso;
    }
    if (!HasAddress(_command))
        return kIdleMiso;
    if (index == 1)
    {
        _address = mosi;
        return kIdleMiso;
    }
    if (index < kDataStart)
        return kIdleMiso;
    const uint32_t data = index - kDataStart;
    switch (_command)
    {
        case kCmdWriteBuffer:
            _buffer[(_address + data) % kBufferSize] = mosi;
            return kIdleMiso;
        case kCmdReadBuffer:
            return _buffer[(_address + data) % kBufferSize];
        case kCmdWriteDma:
            if (_receive.queued && !_receive.done && _receive.position < _receive.length)
                _receive.data[_receive.position++] = mosi;
            return kIdleMiso;
        case kCmdReadDma:
            if (_send.queued && !_send.done && _send.position < _send.length)
                return _send.data[_send.position++];
            return kIdleMiso;
        default:
            return kIdleMiso;
    }
}

void Vdac4EspSpi::SetPowered(bool powered)
{
    _powered = powered;
    if (!powered)
    {
        // The peripheral resets with the chip: no transactions survive
        _send = Channel{};
        _receive = Channel{};
    }
}

void Vdac4EspSpi::WriteBuffer(size_t address, const uint8_t* data, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        _buffer[(address + i) % kBufferSize] = data[i];
}

void Vdac4EspSpi::ReadBuffer(size_t address, uint8_t* out, size_t length) const
{
    for (size_t i = 0; i < length; ++i)
        out[i] = _buffer[(address + i) % kBufferSize];
}

bool Vdac4EspSpi::QueueSend(const uint8_t* data, size_t length)
{
    if (_send.queued || length == 0 || length > kMaxTransaction)
        return false;
    _send.data.assign(data, data + length);
    _send.length = static_cast<uint32_t>(length);
    _send.position = 0;
    _send.queued = true;
    _send.done = false;
    return true;
}

bool Vdac4EspSpi::QueueReceive(size_t length)
{
    if (_receive.queued || length == 0 || length > kMaxTransaction)
        return false;
    _receive.data.assign(length, 0);
    _receive.length = static_cast<uint32_t>(length);
    _receive.position = 0;
    _receive.queued = true;
    _receive.done = false;
    return true;
}

bool Vdac4EspSpi::TakeSendResult()
{
    if (!_send.queued || !_send.done)
        return false;
    _send.queued = false;
    _send.done = false;
    return true;
}

bool Vdac4EspSpi::TakeReceiveResult(const uint8_t*& data, size_t& received)
{
    if (!_receive.queued || !_receive.done)
        return false;
    _receive.queued = false;
    _receive.done = false;
    data = _receive.data.data();
    received = _receive.position;
    return true;
}

void Vdac4EspSpi::SaveChannel(Vdac4EspStateWriter& out, const Channel& channel) const
{
    out.Bool(channel.queued);
    out.Bool(channel.done);
    out.U32(channel.length);
    out.U32(channel.position);
    out.Blob(channel.queued ? channel.data : std::vector<uint8_t>{});
}

bool Vdac4EspSpi::LoadChannel(Vdac4EspStateReader& in, Channel& channel)
{
    channel.queued = in.Bool();
    channel.done = in.Bool();
    channel.length = in.U32();
    channel.position = in.U32();
    if (!in.Blob(channel.data, kMaxTransaction))
        return false;
    if (channel.queued && (channel.data.size() != channel.length || channel.position > channel.length))
        return false;
    return in.Ok();
}

void Vdac4EspSpi::Save(Vdac4EspStateWriter& out) const
{
    out.Bytes(_buffer.data(), _buffer.size());
    SaveChannel(out, _send);
    SaveChannel(out, _receive);
    out.Bool(_powered);
    // The chip-select window: the hub restores the select line itself (ZControllerSpi::SetState)
    out.Bool(_selected);
    out.U32(_byteIndex);
    out.U8(_command);
    out.U8(_address);
    out.U32(_badCommands);
}

bool Vdac4EspSpi::Load(Vdac4EspStateReader& in)
{
    in.Bytes(_buffer.data(), _buffer.size());
    if (!LoadChannel(in, _send) || !LoadChannel(in, _receive))
        return false;
    _powered = in.Bool();
    _selected = in.Bool();
    _byteIndex = in.U32();
    _command = in.U8();
    _address = in.U8();
    _badCommands = in.U32();
    return in.Ok();
}
