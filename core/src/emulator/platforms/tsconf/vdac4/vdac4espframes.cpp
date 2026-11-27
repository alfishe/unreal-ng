#include "stdafx.h"

#include "vdac4espframes.h"

#include <algorithm>
#include <cstring>

#include "vdac4esplink.h"
#include "vdac4espstate.h"
#include "vdac4mediarange.h"

// Port of VDAC4 Firmware src/protocol.cpp and src/z80_transport.cpp (MIT, andrewinsidelazarev); see the header.

namespace
{
constexpr uint8_t kAudioData = 0x31;
constexpr uint8_t kModPut = 0x6A;
constexpr uint8_t kVfsReadMedia = 0x5F;
/// A piece of an app data file read from the Evo card (0.1.166): [request][offset LE32] before the data
constexpr uint8_t kDataPiece = 0x6F;
constexpr uint16_t kDataPiecePrefix = 5;
}  // namespace

// --- EspFrameParser --------------------------------------------------------------------------------------------

EspFrameParser::EspFrameParser(const Vdac4MediaRange& media) : _media(media), _payload(kMaxMediaPayload, 0)
{
}

void EspFrameParser::Reset()
{
    _state = kWaitSync;
    _rawMedia = false;
    _length = 0;
    _position = 0;
    _checksum = 0;
}

bool EspFrameParser::CheckTimeout(uint32_t nowMs)
{
    // A frame the Z80 broke off: the next 5A must start a frame again
    if (_state == kWaitSync || static_cast<uint32_t>(nowMs - _lastByteMs) < kFrameTimeoutMs)
        return false;
    ++_resyncs;
    Reset();
    return true;
}

bool EspFrameParser::Feed(uint8_t byte, uint32_t nowMs, EspPacket& packet)
{
    _lastByteMs = nowMs;
    switch (_state)
    {
        case kWaitSync:
            if (byte == kSync || byte == kMediaSync)
            {
                _rawMedia = byte == kMediaSync;
                _state = kCommand;
            }
            return false;

        case kCommand:
            if (_rawMedia && byte != kAudioData && byte != kModPut && byte != kVfsReadMedia && byte != kDataPiece &&
                !_media.Contains(byte))
            {
                ++_resyncs;
                Reset();
                return false;
            }
            _command = byte;
            _checksum = byte;
            _state = kLengthLow;
            return false;

        case kLengthLow:
            _length = byte;
            _checksum ^= byte;
            _state = kLengthHigh;
            return false;

        case kLengthHigh:
            _length = static_cast<uint16_t>(_length | (byte << 8));
            _checksum ^= byte;
            _position = 0;
            if (_length > (_rawMedia ? kMaxMediaPayload : kMaxPayload) ||
                (_rawMedia && (_length <= (_command == kModPut ? 4 : _command == kDataPiece ? kDataPiecePrefix : 0) ||
                               (_command == kAudioData && _length > 8192))))
            {
                // A large length means lost sync: do not skip that many bytes
                ++_resyncs;
                Reset();
                return false;
            }
            _state = _length ? kData : kChecksum;
            return false;

        case kData:
            _payload[_position++] = byte;
            if (!_rawMedia)
                _checksum ^= byte;
            if (_position == _length && _rawMedia)
            {
                packet.command = _command;
                packet.data = _payload.data();
                packet.length = _length;
                Reset();
                return true;
            }
            if (_position == _length)
                _state = kChecksum;
            return false;

        case kChecksum:
            if (byte != _checksum)
            {
                ++_badChecksums;
                Reset();
                return false;
            }
            packet.command = _command;
            packet.data = _payload.data();
            packet.length = _length;
            Reset();
            return true;
    }
    Reset();
    return false;
}

void EspFrameParser::Save(Vdac4EspStateWriter& out) const
{
    out.U8(_state);
    out.Bool(_rawMedia);
    out.U8(_command);
    out.U16(_length);
    out.U16(_position);
    out.U8(_checksum);
    out.U32(_lastByteMs);
    out.U32(_badChecksums);
    out.U32(_resyncs);
    // The frame in progress only (a finished frame's payload is never read again)
    out.Blob(_payload.data(), _state == kWaitSync ? 0 : _position);
}

bool EspFrameParser::Load(Vdac4EspStateReader& in)
{
    const uint8_t state = in.U8();
    _rawMedia = in.Bool();
    _command = in.U8();
    _length = in.U16();
    _position = in.U16();
    _checksum = in.U8();
    _lastByteMs = in.U32();
    _badChecksums = in.U32();
    _resyncs = in.U32();
    std::vector<uint8_t> partial;
    if (!in.Blob(partial, kMaxMediaPayload) || state > kChecksum || _length > kMaxMediaPayload ||
        _position > _length || partial.size() > _position)
        return false;
    _state = static_cast<State>(state);
    std::copy(partial.begin(), partial.end(), _payload.begin());
    return in.Ok();
}

// --- EspFrameTransport -----------------------------------------------------------------------------------------

EspFrameTransport::EspFrameTransport(EspSpiLink& link, const Vdac4MediaRange& media) : _link(link), _parser(media)
{
}

void EspFrameTransport::Reset()
{
    _parser.Reset();
    _rxChunkOffset = 0;
    _rxChunkLength = 0;
    _backlog.clear();
    _backlogBytes = 0;
    _events.clear();
}

bool EspFrameTransport::Poll(EspPacket& packet, uint32_t nowMs)
{
    for (;;)
    {
        if (_rxChunkOffset == _rxChunkLength)
        {
            _rxChunkOffset = 0;
            _rxChunkLength = _link.Read(_rxChunk, sizeof(_rxChunk));
            if (_rxChunkLength == 0)
            {
                _parser.CheckTimeout(nowMs);
                return false;
            }
        }
        while (_rxChunkOffset < _rxChunkLength)
        {
            if (_parser.Feed(_rxChunk[_rxChunkOffset++], nowMs, packet))
                return true;
        }
    }
}

bool EspFrameTransport::Send(uint8_t command, const uint8_t* data, size_t length, uint32_t nowMs)
{
    if (length > EspFrameParser::kMaxPayload || (length != 0 && data == nullptr))
        return false;
    Pending frame;
    frame.bytes.reserve(length + kFrameOverhead + 1);
    frame.bytes = {EspFrameParser::kSync, command, static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8)};
    // SYNC is not in the XOR: the contract of the existing zifi.spg
    uint8_t checksum = static_cast<uint8_t>(command ^ frame.bytes[2] ^ frame.bytes[3]);
    for (size_t i = 0; i < length; ++i)
    {
        frame.bytes.push_back(data[i]);
        checksum ^= data[i];
    }
    frame.bytes.push_back(checksum);
    if ((frame.bytes.size() & 1U) != 0)
        frame.bytes.push_back(0);  // padsFrames: the next frame starts on an even byte
    if (frame.bytes.size() > _link.WriteCapacity())
        return false;  // reserve(): longer than the ring
    Drain(nowMs);
    if (_backlog.empty() && _link.WriteSpace() >= frame.bytes.size())
    {
        _link.Write(frame.bytes.data(), frame.bytes.size());
        return true;
    }
    if (_backlogBytes + frame.bytes.size() > kMaxBacklogBytes)
    {
        ++_droppedFrames;
        return false;
    }
    frame.waitStartMs = nowMs;
    _backlogBytes += frame.bytes.size();
    _backlog.push_back(std::move(frame));
    return true;
}

bool EspFrameTransport::SendError(const char* text, uint32_t nowMs)
{
    if (text == nullptr)
        text = "error";
    const size_t length = std::min(std::strlen(text), kErrorText);
    return Send(kError, reinterpret_cast<const uint8_t*>(text), length, nowMs);
}

void EspFrameTransport::Drain(uint32_t nowMs)
{
    while (!_backlog.empty())
    {
        Pending& head = _backlog.front();
        if (_link.WriteSpace() >= head.bytes.size())
        {
            _link.Write(head.bytes.data(), head.bytes.size());
        }
        else
        {
            // reserve(): quiet counts from the start of the wait or the last exchange, whichever is later
            const uint32_t waited = nowMs - head.waitStartMs;
            const uint32_t sinceActivity = nowMs - _link.LastActivityMs();
            const uint32_t quiet = std::min(sinceActivity, waited);
            if (quiet < EspSpiLink::kPeerQuietMs && waited < EspSpiLink::kReserveLimitMs)
                return;
            ++_droppedFrames;  // the send failed: the frame is lost
        }
        _backlogBytes -= head.bytes.size();
        _backlog.pop_front();
        if (!_backlog.empty())
            _backlog.front().waitStartMs = nowMs;  // the next send starts waiting now
    }
}

bool EspFrameTransport::QueueEvent(uint8_t command, const uint8_t* data, size_t length)
{
    if (length > kMaxEventPayload || (length != 0 && data == nullptr) || _events.size() >= kEventQueueDepth)
        return false;
    Event event;
    event.command = command;
    if (length != 0)
        event.data.assign(data, data + length);
    _events.push_back(std::move(event));
    return true;
}

bool EspFrameTransport::SendNextEvent(uint32_t nowMs)
{
    if (_events.empty())
        return false;
    const Event event = std::move(_events.front());
    _events.pop_front();
    Send(event.command, event.data.data(), event.data.size(), nowMs);
    return true;
}

void EspFrameTransport::Save(Vdac4EspStateWriter& out) const
{
    _parser.Save(out);
    out.Blob(_rxChunk + _rxChunkOffset, _rxChunkLength - _rxChunkOffset);
    out.U32(static_cast<uint32_t>(_backlog.size()));
    for (const Pending& frame : _backlog)
    {
        out.Blob(frame.bytes);
        out.U32(frame.waitStartMs);
    }
    out.U8(static_cast<uint8_t>(_events.size()));
    for (const Event& event : _events)
    {
        out.U8(event.command);
        out.Blob(event.data);
    }
    out.U32(_droppedFrames);
}

bool EspFrameTransport::Load(Vdac4EspStateReader& in)
{
    Reset();
    if (!_parser.Load(in))
        return false;
    std::vector<uint8_t> chunk;
    if (!in.Blob(chunk, kRxChunkSize))
        return false;
    std::copy(chunk.begin(), chunk.end(), _rxChunk);
    _rxChunkLength = chunk.size();
    const uint32_t frames = in.U32();
    for (uint32_t i = 0; i < frames && in.Ok(); ++i)
    {
        Pending frame;
        if (!in.Blob(frame.bytes, EspFrameParser::kMaxPayload + kFrameOverhead + 1))
            return false;
        frame.waitStartMs = in.U32();
        _backlogBytes += frame.bytes.size();
        if (_backlogBytes > kMaxBacklogBytes)
            return false;
        _backlog.push_back(std::move(frame));
    }
    const uint8_t events = in.U8();
    if (events > kEventQueueDepth)
        return false;
    for (uint8_t i = 0; i < events && in.Ok(); ++i)
    {
        Event event;
        event.command = in.U8();
        if (!in.Blob(event.data, kMaxEventPayload))
            return false;
        _events.push_back(std::move(event));
    }
    _droppedFrames = in.U32();
    return in.Ok();
}
