#pragma once

/// @file vdac4espframes.h
/// @brief ZiFi frames in the VDAC4 ESP's link rings: the firmware's FrameParser and Z80Transport.
///
/// Port of VDAC4 `ESP32 S3 Zero N8R8/Firmware/src/protocol.cpp` (FrameParser, writeFrame) and
/// `src/z80_transport.cpp` (MIT License, Copyright (c) andrewinsidelazarev).
///
///   checked frame: 5A CMD LEN_L LEN_H DATA[LEN <= 1024] XOR(CMD, LEN_L, LEN_H, DATA)
///   media frame:   A6 CMD LEN_L LEN_H DATA[LEN <= 8200]   (made by the link from a raw media DMA, no XOR;
///                  CMD 31, 6A, 5F or a command in the app's media range)
///
/// A frame left unfinished for 500 ms is dropped (checkTimeout). Frames to the Z80 are padded with one 00 to an
/// even length (the SPI link's padsFrames: frames start on even bytes for the Z80's word DMA). EE errors carry at
/// most 48 bytes of text; FE is the ACK.
///
/// Sending waits for room (SpiLink::reserve): a frame goes into the ring whole or not at all. The firmware's
/// sender blocks; here a frame without room waits in a backlog, sent in order as the Z80 makes room, and while
/// the backlog is not empty the firmware's main loop takes no new command (it would be blocked inside send).
/// The wait fails as reserve() does: the Z80 quiet for 5 s (counted from the start of the wait) or 30 s in all;
/// the frame is then lost. The backlog holds at most kMaxBacklogBytes; a frame beyond that is refused at once.
///
/// Events (core->event, network events): at most 63 bytes, an 8-deep queue the main loop sends one per pass
/// (Application::sendEvent / pollNetworkEvent).

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

class EspSpiLink;
class Vdac4MediaRange;
class Vdac4EspStateWriter;
class Vdac4EspStateReader;

struct EspPacket
{
    uint8_t command = 0;
    const uint8_t* data = nullptr;  ///< valid until the next Poll
    uint16_t length = 0;
};

class EspFrameParser
{
public:
    static constexpr uint8_t kSync = 0x5A;
    static constexpr uint8_t kMediaSync = 0xA6;
    static constexpr size_t kMaxPayload = 1024;
    static constexpr size_t kMaxMediaPayload = 8192 + 8;
    static constexpr uint32_t kFrameTimeoutMs = 500;

    explicit EspFrameParser(const Vdac4MediaRange& media);

    bool Feed(uint8_t byte, uint32_t nowMs, EspPacket& packet);
    bool CheckTimeout(uint32_t nowMs);
    void Reset();

    uint32_t BadChecksums() const { return _badChecksums; }
    uint32_t Resyncs() const { return _resyncs; }

    void Save(Vdac4EspStateWriter& out) const;
    bool Load(Vdac4EspStateReader& in);

private:
    enum State : uint8_t
    {
        kWaitSync,
        kCommand,
        kLengthLow,
        kLengthHigh,
        kData,
        kChecksum
    };

    const Vdac4MediaRange& _media;
    State _state = kWaitSync;
    bool _rawMedia = false;
    uint8_t _command = 0;
    uint16_t _length = 0;
    uint16_t _position = 0;
    uint8_t _checksum = 0;
    uint32_t _lastByteMs = 0;
    uint32_t _badChecksums = 0;
    uint32_t _resyncs = 0;
    std::vector<uint8_t> _payload;
};

class EspFrameTransport
{
public:
    static constexpr uint8_t kError = 0xEE;
    static constexpr uint8_t kAck = 0xFE;
    static constexpr size_t kErrorText = 48;
    static constexpr size_t kFrameOverhead = 5;
    static constexpr size_t kRxChunkSize = 256;
    static constexpr size_t kMaxEventPayload = 63;
    static constexpr size_t kEventQueueDepth = 8;
    static constexpr size_t kMaxBacklogBytes = 64 * 1024;

    EspFrameTransport(EspSpiLink& link, const Vdac4MediaRange& media);

    /// Boot / restart: parser, chunk, backlog and events empty
    void Reset();

    /// One packet from the Z80, if a whole one is in (Z80Transport::poll)
    bool Poll(EspPacket& packet, uint32_t nowMs);

    /// A frame to the Z80 (Z80Transport::send). False: refused (backlog full) - the firmware's failed send
    bool Send(uint8_t command, const uint8_t* data, size_t length, uint32_t nowMs);
    bool SendAck(uint32_t nowMs) { return Send(kAck, nullptr, 0, nowMs); }
    bool SendError(const char* text, uint32_t nowMs);

    /// Move backlog frames into the ring while there is room; drop a frame whose wait failed
    void Drain(uint32_t nowMs);
    /// The sender is waiting for room (the firmware's main loop would be blocked)
    bool Blocked() const { return !_backlog.empty(); }

    /// The event queue (sendEvent): false when the payload is too long or the queue is full
    bool QueueEvent(uint8_t command, const uint8_t* data, size_t length);
    /// Send the oldest event (pollNetworkEvent); false: none
    bool SendNextEvent(uint32_t nowMs);
    size_t QueuedEvents() const { return _events.size(); }

    const EspFrameParser& Parser() const { return _parser; }
    uint32_t DroppedFrames() const { return _droppedFrames; }

    void Save(Vdac4EspStateWriter& out) const;
    bool Load(Vdac4EspStateReader& in);

private:
    struct Pending
    {
        std::vector<uint8_t> bytes;  // the whole frame with its pad
        uint32_t waitStartMs = 0;
    };
    struct Event
    {
        uint8_t command = 0;
        std::vector<uint8_t> data;
    };

    EspSpiLink& _link;
    EspFrameParser _parser;
    uint8_t _rxChunk[kRxChunkSize] = {};
    size_t _rxChunkOffset = 0;
    size_t _rxChunkLength = 0;
    std::deque<Pending> _backlog;
    size_t _backlogBytes = 0;
    std::deque<Event> _events;
    uint32_t _droppedFrames = 0;
};
