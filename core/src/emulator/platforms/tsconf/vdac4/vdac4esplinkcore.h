#pragma once

/// @file vdac4esplinkcore.h
/// @brief The link logic of the VDAC4 board's ESP32-S3 on the Evo SPI (no hardware), ported from the board
/// firmware's SpiLinkCore.
///
/// Port of `ESP32 S3 Zero N8R8/Firmware/include/zifi/spi_link_core.hpp` and `src/spi_link_core.cpp` of
/// VDAC4 (https://github.com/andrewinsidelazarev/VDAC4), MIT License, Copyright (c) andrewinsidelazarev.
/// Logic kept byte for byte; the firmware's atomics are plain fields (everything here runs on the emulation
/// thread) and the SpscByteRing is EspByteRing.
///
/// The 64-byte shared buffer of the SPI Slave HD (Vdac4EspSpi) carries the exchange:
///
///   request:  [C3][tid][flags][len][room][data <= 54][sum]
///   response: [3C][tid][status][len][accepted][data <= 54][sum]
///   bytes 60..63: state word [5A empty / A5 pending][D7][version 3][boot nonce]
///
/// sum = ~(sum of the previous bytes): the whole message sums to #FF. A repeated tid gets the previous response
/// again (no data taken or given twice); flag NEW_SESSION (#80) overrides that. Bulk (512 bytes by the slave's
/// DMA, echo-verified, kBulkAttempts failures turn it off for the session), put (512 bytes Z80 -> ESP checked by
/// XOR), media (up to 8192 bytes, no check, becomes an internal A6 frame in the input ring).
///
/// Not ported: the VDAC4 VFS sector queue (flag #20, only the Wild Commander VFS resident uses it). No sector
/// storage is ever attached, so a sector request answers as the firmware does without one (BULK_STALE).

#include <cstddef>
#include <cstdint>
#include <vector>

class Vdac4EspStateWriter;
class Vdac4EspStateReader;
class Vdac4MediaRange;

/// Single-threaded byte ring with power-of-two capacity and 32-bit monotonic positions (as SpscByteRing)
class EspByteRing
{
public:
    void Attach(size_t capacity);
    void Reset() { _write = _read = 0; }
    size_t Capacity() const { return _data.size(); }
    size_t Available() const { return _write - _read; }
    size_t FreeSpace() const { return Capacity() - Available(); }
    /// As much as fits; returns the count
    size_t Write(const uint8_t* data, size_t length);
    size_t Read(uint8_t* out, size_t capacity);
    size_t Discard(size_t length);

    void Save(Vdac4EspStateWriter& out) const;
    bool Load(Vdac4EspStateReader& in);

private:
    std::vector<uint8_t> _data;
    uint32_t _write = 0;
    uint32_t _read = 0;
};

/// The input (Z80 -> ESP) side, put before the request's data
struct EspSpiLinkPutInput
{
    bool available = false;            ///< the DMA receive can be opened
    const uint8_t* received = nullptr; ///< what came after PUT_READY, if anything
    size_t receivedLength = 0;
    size_t capacity = 8192;            ///< receive DMA transaction size available
};

class EspSpiLinkCore
{
public:
    static constexpr size_t kBufferSize = 64;
    static constexpr size_t kTurnSize = 60;
    static constexpr size_t kHeaderSize = 5;
    static constexpr size_t kMaxChunk = kTurnSize - kHeaderSize - 1;  // 54
    static constexpr size_t kStateOffset = 60;
    static constexpr size_t kStateSize = 4;

    static constexpr uint8_t kRequestMarker = 0xC3;
    static constexpr uint8_t kResponseMarker = 0x3C;
    static constexpr uint8_t kStateEmpty = 0x5A;
    static constexpr uint8_t kStatePending = 0xA5;
    static constexpr uint8_t kMagic = 0xD7;
    static constexpr uint8_t kVersion = 3;

    // Request flags
    static constexpr uint8_t kFlagDropToZ80 = 0x01;
    static constexpr uint8_t kFlagBulk = 0x02;
    static constexpr uint8_t kFlagPut = 0x04;
    static constexpr uint8_t kFlagPutDone = 0x08;
    static constexpr uint8_t kFlagMedia = 0x10;
    static constexpr uint8_t kFlagSector = 0x20;
    static constexpr uint8_t kFlagRawOut = 0x40;
    static constexpr uint8_t kFlagNewSession = 0x80;
    static constexpr size_t kMediaMax = 8192;

    // Response status
    static constexpr uint8_t kStatusMore = 0x01;
    static constexpr uint8_t kStatusBulk = 0x02;
    static constexpr uint8_t kStatusBulkStale = 0x04;
    static constexpr uint8_t kStatusEchoBad = 0x08;
    static constexpr uint8_t kStatusPutReady = 0x10;
    static constexpr uint8_t kStatusPutOk = 0x20;
    static constexpr uint8_t kStatusPutBad = 0x40;
    static constexpr uint8_t kStatusPutCapable = 0x80;

    static constexpr size_t kBulkSize = 512;
    static constexpr uint8_t kBulkAttempts = 3;

    // Internal framing of raw media DMA (protocol.hpp)
    static constexpr uint8_t kMediaSync = 0xA6;
    static constexpr size_t kAppMediaPrefixMax = 8;
    static constexpr size_t kVfsReadMediaPrefix = 6;
    static constexpr uint8_t kAudioData = 0x31;
    static constexpr uint8_t kModPut = 0x6A;
    static constexpr uint8_t kVfsReadMedia = 0x5F;
    /// A piece of an app data file from the Evo card (0.1.166): [request][offset LE32] before the data
    static constexpr uint8_t kDataPiece = 0x6F;
    static constexpr size_t kDataPiecePrefix = 5;

    enum BulkState : uint8_t
    {
        kBulkOff,     ///< no DMA: offer no blocks
        kBulkFree,    ///< free; no good echo of the last block
        kBulkLoaded,  ///< a block is loaded and not read yet
        kBulkEchoed,  ///< free; the last block came back intact
    };

    using PutInput = EspSpiLinkPutInput;

    struct Stats
    {
        uint32_t turns = 0;
        uint32_t repeats = 0;
        uint32_t badRequests = 0;
        uint32_t shortAccepts = 0;
        uint32_t bytesFromZ80 = 0;
        uint32_t bytesToZ80 = 0;
        uint32_t bulkBlocks = 0;
        uint32_t bulkBad = 0;
        uint32_t bulkGaveUp = 0;
        uint32_t putBlocks = 0;
        uint32_t putBad = 0;
    };

    /// `media` decides which app commands may come as raw media (setAppMediaRange)
    explicit EspSpiLinkCore(const Vdac4MediaRange& media) : _media(media) {}

    /// Rings: Z80 -> ESP and ESP -> Z80 capacities (powers of two)
    void Begin(size_t fromZ80Capacity, size_t toZ80Capacity);

    /// `request` is bytes 0..59 of the shared buffer. A valid request fills `response` (`responseLength`) and
    /// returns true. With kStatusBulk and no kStatusBulkStale in the status, the block is in `bulkBuffer`
    /// (kBulkSize bytes) and the caller must load it into the slave's DMA
    bool HandleRequest(const uint8_t* request, uint8_t* response, size_t& responseLength, BulkState bulk = kBulkOff,
                       uint8_t* bulkBuffer = nullptr, const PutInput& put = PutInput{});

    void StateWord(uint8_t word[kStateSize], uint8_t bootNonce) const;

    // The protocol loop's side
    size_t Read(uint8_t* out, size_t capacity) { return _fromZ80.Read(out, capacity); }
    size_t Write(const uint8_t* data, size_t length) { return _toZ80.Write(data, length); }
    size_t WriteSpace() const { return _toZ80.FreeSpace(); }
    size_t WriteCapacity() const { return _toZ80.Capacity(); }
    size_t PendingToZ80() const;
    size_t PendingFromZ80() const { return _fromZ80.Available(); }
    uint32_t Activity() const { return _stats.turns + _stats.repeats; }
    const Stats& GetStats() const { return _stats; }
    size_t PutDmaSize() const { return (_putSize + 511u) & ~size_t(511u); }
    /// A turn served outside the core (kFlagRawOut): the last tid is no longer "previous"
    void ForgetLastTurn() { _haveLast = false; }

    static uint8_t Checksum(const uint8_t* data, size_t length);

    void Save(Vdac4EspStateWriter& out) const;
    bool Load(Vdac4EspStateReader& in);

private:
    const Vdac4MediaRange& _media;
    EspByteRing _fromZ80;
    EspByteRing _toZ80;
    uint8_t _lastResponse[kTurnSize] = {};
    size_t _lastLength = 0;
    uint8_t _lastTid = 0;
    bool _haveLast = false;
    bool _blockOut = false;    // a block was taken from the ring and is not confirmed yet
    bool _bulkGaveUp = false;  // DMA off until a new session
    uint8_t _blockTries = 0;
    uint32_t _blockInline = 0; // bytes of the unconfirmed block still to go by plain responses
    bool _putArmed = false;
    bool _putMedia = false;
    uint8_t _putCommand = kAudioData;
    uint8_t _putPrefix[kAppMediaPrefixMax] = {};
    size_t _putPrefixSize = 0;
    size_t _putSize = kBulkSize;
    uint8_t _putXor = 0;
    Stats _stats;
};
