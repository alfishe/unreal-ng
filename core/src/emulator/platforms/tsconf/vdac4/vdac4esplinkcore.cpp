#include "stdafx.h"

#include "vdac4esplinkcore.h"

#include <algorithm>
#include <cstring>

#include "vdac4espstate.h"
#include "vdac4mediarange.h"

// Port of VDAC4 Firmware src/spi_link_core.cpp (MIT, andrewinsidelazarev); see the header.

namespace
{
constexpr bool HalvesAreInverse(uint8_t value)
{
    return ((value >> 4) ^ (value & 0x0F)) == 0x0F;
}

// The Z80 driver finds the start of data by these bytes (zifi_spi.asm SpiLink_OpenRead)
static_assert(HalvesAreInverse(EspSpiLinkCore::kRequestMarker) && HalvesAreInverse(EspSpiLinkCore::kResponseMarker) &&
                  HalvesAreInverse(EspSpiLinkCore::kStateEmpty) && HalvesAreInverse(EspSpiLinkCore::kStatePending),
              "the Z80 driver finds the data start by these bytes");

uint16_t ReadLe16(const uint8_t* data)
{
    return static_cast<uint16_t>(data[0] | (data[1] << 8));
}
}  // namespace

// --- EspByteRing -----------------------------------------------------------------------------------------------

void EspByteRing::Attach(size_t capacity)
{
    _data.assign(capacity, 0);
    Reset();
}

size_t EspByteRing::Write(const uint8_t* data, size_t length)
{
    const size_t capacity = Capacity();
    if (capacity == 0 || data == nullptr || length == 0)
        return 0;
    const size_t count = std::min(length, FreeSpace());
    const size_t index = _write & (capacity - 1);
    const size_t first = std::min(count, capacity - index);
    std::memcpy(_data.data() + index, data, first);
    if (first != count)
        std::memcpy(_data.data(), data + first, count - first);
    _write += static_cast<uint32_t>(count);
    return count;
}

size_t EspByteRing::Read(uint8_t* out, size_t capacity)
{
    const size_t size = Capacity();
    if (size == 0 || out == nullptr || capacity == 0)
        return 0;
    const size_t count = std::min(capacity, Available());
    const size_t index = _read & (size - 1);
    const size_t first = std::min(count, size - index);
    std::memcpy(out, _data.data() + index, first);
    if (first != count)
        std::memcpy(out + first, _data.data(), count - first);
    _read += static_cast<uint32_t>(count);
    return count;
}

size_t EspByteRing::Discard(size_t length)
{
    const size_t count = std::min(length, Available());
    _read += static_cast<uint32_t>(count);
    return count;
}

void EspByteRing::Save(Vdac4EspStateWriter& out) const
{
    // Only the bytes in the ring; positions restart at 0 on load (only their difference matters)
    std::vector<uint8_t> bytes(Available());
    const size_t size = Capacity();
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = _data[(_read + i) & (size - 1)];
    out.Blob(bytes);
}

bool EspByteRing::Load(Vdac4EspStateReader& in)
{
    std::vector<uint8_t> bytes;
    if (!in.Blob(bytes, Capacity()))
        return false;
    Reset();
    Write(bytes.data(), bytes.size());
    return true;
}

// --- EspSpiLinkCore --------------------------------------------------------------------------------------------

void EspSpiLinkCore::Begin(size_t fromZ80Capacity, size_t toZ80Capacity)
{
    _haveLast = false;
    _lastLength = 0;
    _blockOut = false;
    _bulkGaveUp = false;
    _blockTries = 0;
    _blockInline = 0;
    _putArmed = false;
    _fromZ80.Attach(fromZ80Capacity);
    _toZ80.Attach(toZ80Capacity);
}

uint8_t EspSpiLinkCore::Checksum(const uint8_t* data, size_t length)
{
    uint8_t sum = 0;
    for (size_t index = 0; index < length; ++index)
        sum = static_cast<uint8_t>(sum + data[index]);
    // The whole message with this byte sums to #FF, so a buffer of zeros (the ESP just restarted) is no message
    return static_cast<uint8_t>(~sum);
}

bool EspSpiLinkCore::HandleRequest(const uint8_t* request, uint8_t* response, size_t& responseLength,
                                   BulkState bulk, uint8_t* bulkBuffer, const PutInput& put)
{
    responseLength = 0;
    if (request == nullptr || response == nullptr || request[0] != kRequestMarker)
        return false;  // our own last response, or nothing yet: not a request
    const uint8_t tid = request[1];
    const uint8_t flags = request[2];
    const size_t length = request[3];
    size_t room = request[4];
    if (length > kMaxChunk || Checksum(request, kHeaderSize + length) != request[kHeaderSize + length])
    {
        // Read while the Z80 was still writing it, or damaged: no response, the Z80 sends it again
        ++_stats.badRequests;
        return false;
    }
    if (room > kMaxChunk)
        room = kMaxChunk;

    if ((flags & kFlagNewSession) == 0 && _haveLast && tid == _lastTid)
    {
        std::memcpy(response, _lastResponse, _lastLength);
        responseLength = _lastLength;
        ++_stats.repeats;
        return true;
    }

    if ((flags & kFlagNewSession) != 0)
    {
        _bulkGaveUp = false;
        _putArmed = false;
    }
    if ((flags & kFlagDropToZ80) != 0)
    {
        _toZ80.Discard(_toZ80.Available());
        _blockOut = false;
        _blockInline = 0;
    }
    // A block from the Z80 written after the last PUT_READY goes into the ring before this request's data
    uint8_t putStatus = put.available ? kStatusPutCapable : 0;
    if ((flags & kFlagPutDone) != 0)
    {
        bool good = false;
        const size_t needed = _putSize + (_putMedia ? 4 + _putPrefixSize : 0);
        if (_putArmed && put.received != nullptr && put.receivedLength >= PutDmaSize() && _fromZ80.FreeSpace() >= needed)
        {
            uint8_t sum = 0;
            if (!_putMedia)
            {
                for (size_t index = 0; index < _putSize; ++index)
                    sum = static_cast<uint8_t>(sum ^ put.received[index]);
            }
            if (_putMedia || sum == _putXor)
            {
                if (_putMedia)
                {
                    const size_t total = _putSize + _putPrefixSize;
                    const uint8_t header[] = {kMediaSync, _putCommand, static_cast<uint8_t>(total),
                                              static_cast<uint8_t>(total >> 8)};
                    _fromZ80.Write(header, sizeof(header));
                    _fromZ80.Write(_putPrefix, _putPrefixSize);
                }
                _fromZ80.Write(put.received, _putSize);
                good = true;
            }
        }
        _putArmed = false;
        if (good)
        {
            putStatus |= kStatusPutOk;
            ++_stats.putBlocks;
            _stats.bytesFromZ80 += static_cast<uint32_t>(_putSize);
        }
        else
        {
            putStatus |= kStatusPutBad;
            ++_stats.putBad;
        }
    }
    const bool media = (flags & kFlagMedia) != 0;
    const size_t accepted = media ? length : (length != 0 ? _fromZ80.Write(request + kHeaderSize, length) : 0);
    if (media)
    {
        room = 0;
        const uint8_t command = length != 0 ? request[kHeaderSize] : 0;
        const bool resource = command == kModPut && length == 7;
        // An app command it allowed (setAppMediaRange): its own control bytes go before the data
        const bool app = !resource && command != kAudioData && _media.Contains(command) && length >= 3 &&
                         length <= 3 + kAppMediaPrefixMax;
        const bool vfs = command == kVfsReadMedia && length == 3 + kVfsReadMediaPrefix;
        // A piece of an app data file from the Evo card (0.1.166): [request][offset] before the data
        const bool piece = command == kDataPiece && length == 3 + kDataPiecePrefix;
        const bool valid = resource || app || vfs || piece || (command == kAudioData && length == 3);
        const size_t size = valid ? ReadLe16(request + kHeaderSize + 1) : 0;
        const size_t prefix = resource ? 4
                              : app    ? length - 3
                              : vfs    ? kVfsReadMediaPrefix
                              : piece  ? kDataPiecePrefix
                                       : 0;
        if (put.available && size != 0 && size <= kMediaMax && ((size + 511u) & ~size_t(511u)) <= put.capacity &&
            _fromZ80.FreeSpace() >= size + prefix + 4)
        {
            _putArmed = true;
            _putMedia = true;
            _putCommand = command;
            _putPrefixSize = prefix;
            if (prefix != 0)
                std::memcpy(_putPrefix, request + kHeaderSize + 3, prefix);
            _putSize = size;
            putStatus |= kStatusPutReady;
        }
    }
    if (!media && (flags & kFlagPut) != 0)
    {
        // Byte 4 is the block's XOR, not room: the Z80 expects no data back
        room = 0;
        if (put.available && _fromZ80.FreeSpace() >= kBulkSize)
        {
            _putArmed = true;
            _putMedia = false;
            _putSize = kBulkSize;
            _putXor = request[4];
            putStatus |= kStatusPutReady;
        }
    }

    const bool wantsSector = (flags & kFlagSector) != 0 && bulkBuffer != nullptr;
    const bool wantsBulk = !wantsSector && (flags & kFlagBulk) != 0 && bulkBuffer != nullptr;
    size_t inlineLeft = _blockInline;
    uint8_t bulkStatus = 0;
    // A block given earlier and not confirmed yet
    if (_blockOut && inlineLeft == 0)
    {
        if (bulk == kBulkEchoed)
        {
            _blockOut = false;
        }
        else if (wantsBulk)
        {
            if (bulk == kBulkLoaded)
            {
                bulkStatus = kStatusBulk;  // the Z80 has not read it yet: still loaded
            }
            else
            {
                ++_stats.bulkBad;
                ++_blockTries;
                if (bulk == kBulkFree && _blockTries < kBulkAttempts)
                {
                    bulkStatus = kStatusBulk | kStatusEchoBad;
                }
                else
                {
                    // The DMA does not work: the block goes by plain responses, no new ones
                    _bulkGaveUp = true;
                    ++_stats.bulkGaveUp;
                    inlineLeft = kBulkSize;
                    bulkStatus = kStatusEchoBad;
                }
            }
        }
    }
    // A new block when the ring holds one. An unread block of an old session must leave the DMA first
    if (bulkStatus == 0 && wantsBulk && !_blockOut && !_bulkGaveUp)
    {
        if (bulk == kBulkLoaded)
        {
            bulkStatus = kStatusBulk | kStatusBulkStale;
        }
        else if (bulk != kBulkOff && _toZ80.Available() >= kBulkSize)
        {
            _toZ80.Read(bulkBuffer, kBulkSize);
            _blockOut = true;
            _blockTries = 0;
            bulkStatus = kStatusBulk;
            ++_stats.bulkBlocks;
            _stats.bytesToZ80 += static_cast<uint32_t>(kBulkSize);
        }
    }
    // Sector (kFlagSector): no sector storage in the emulation (see the header), as the firmware without one
    if (wantsSector)
    {
        room = 0;
        if (_blockOut)
        {
            _blockOut = false;  // a frame-ring block of a program that left the link
            inlineLeft = 0;
        }
        bulkStatus = bulk == kBulkLoaded ? static_cast<uint8_t>(kStatusBulk | kStatusBulkStale) : kStatusBulkStale;
    }
    // Data in the response: first the rest of a block the DMA gave up on, then the ring. The ring waits behind
    // an unconfirmed block
    size_t sent = 0;
    if (room != 0 && (bulkStatus & kStatusBulk) == 0)
    {
        if (inlineLeft != 0 && bulkBuffer != nullptr)
        {
            sent = room < inlineLeft ? room : inlineLeft;
            std::memcpy(response + kHeaderSize, bulkBuffer + (kBulkSize - inlineLeft), sent);
            inlineLeft -= sent;
            if (inlineLeft == 0)
                _blockOut = false;
        }
        else if (!_blockOut)
        {
            sent = _toZ80.Read(response + kHeaderSize, room);
        }
    }
    _blockInline = static_cast<uint32_t>(inlineLeft);

    response[0] = kResponseMarker;
    response[1] = tid;
    response[2] = static_cast<uint8_t>((_toZ80.Available() != 0 || _blockOut ? kStatusMore : 0) | bulkStatus | putStatus);
    response[3] = static_cast<uint8_t>(sent);
    response[4] = static_cast<uint8_t>(accepted);
    response[kHeaderSize + sent] = Checksum(response, kHeaderSize + sent);
    responseLength = kHeaderSize + sent + 1;

    std::memcpy(_lastResponse, response, responseLength);
    _lastLength = responseLength;
    _lastTid = tid;
    _haveLast = true;

    ++_stats.turns;
    if (accepted != length)
        ++_stats.shortAccepts;
    _stats.bytesFromZ80 += static_cast<uint32_t>(media ? 0 : accepted);
    _stats.bytesToZ80 += static_cast<uint32_t>(sent);
    return true;
}

void EspSpiLinkCore::StateWord(uint8_t word[kStateSize], uint8_t bootNonce) const
{
    word[0] = PendingToZ80() != 0 ? kStatePending : kStateEmpty;
    word[1] = kMagic;
    word[2] = kVersion;
    word[3] = bootNonce;
}

size_t EspSpiLinkCore::PendingToZ80() const
{
    const size_t queued = _toZ80.Available();
    if (!_blockOut)
        return queued;
    // The block outside the ring: all of it on check at the Z80, or its rest
    return queued + (_blockInline != 0 ? _blockInline : kBulkSize);
}

void EspSpiLinkCore::Save(Vdac4EspStateWriter& out) const
{
    _fromZ80.Save(out);
    _toZ80.Save(out);
    out.Bytes(_lastResponse, sizeof(_lastResponse));
    out.U8(static_cast<uint8_t>(_lastLength));
    out.U8(_lastTid);
    out.Bool(_haveLast);
    out.Bool(_blockOut);
    out.Bool(_bulkGaveUp);
    out.U8(_blockTries);
    out.U32(_blockInline);
    out.Bool(_putArmed);
    out.Bool(_putMedia);
    out.U8(_putCommand);
    out.Bytes(_putPrefix, sizeof(_putPrefix));
    out.U8(static_cast<uint8_t>(_putPrefixSize));
    out.U16(static_cast<uint16_t>(_putSize));
    out.U8(_putXor);
    const uint32_t stats[] = {_stats.turns,      _stats.repeats,  _stats.badRequests, _stats.shortAccepts,
                              _stats.bytesFromZ80, _stats.bytesToZ80, _stats.bulkBlocks, _stats.bulkBad,
                              _stats.bulkGaveUp, _stats.putBlocks, _stats.putBad};
    for (uint32_t value : stats)
        out.U32(value);
}

bool EspSpiLinkCore::Load(Vdac4EspStateReader& in)
{
    if (!_fromZ80.Load(in) || !_toZ80.Load(in))
        return false;
    in.Bytes(_lastResponse, sizeof(_lastResponse));
    _lastLength = std::min<size_t>(in.U8(), kTurnSize);
    _lastTid = in.U8();
    _haveLast = in.Bool();
    _blockOut = in.Bool();
    _bulkGaveUp = in.Bool();
    _blockTries = in.U8();
    _blockInline = std::min<uint32_t>(in.U32(), kBulkSize);
    _putArmed = in.Bool();
    _putMedia = in.Bool();
    _putCommand = in.U8();
    in.Bytes(_putPrefix, sizeof(_putPrefix));
    _putPrefixSize = std::min<size_t>(in.U8(), kAppMediaPrefixMax);
    _putSize = std::min<size_t>(in.U16(), kMediaMax);
    _putXor = in.U8();
    uint32_t* stats[] = {&_stats.turns,      &_stats.repeats,  &_stats.badRequests, &_stats.shortAccepts,
                         &_stats.bytesFromZ80, &_stats.bytesToZ80, &_stats.bulkBlocks, &_stats.bulkBad,
                         &_stats.bulkGaveUp, &_stats.putBlocks, &_stats.putBad};
    for (uint32_t* value : stats)
        *value = in.U32();
    return in.Ok();
}
