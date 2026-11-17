#pragma once

// The Evo side of the VDAC4 SPI link for tests: a C++ mirror of zifi_spi.asm (R-Type VDAC4 launcher,
// src-R-Type-VDAC4/Source/Launcher/zifi_spi.asm, ZIFI_SPI_DMA build) and of the raw media put of
// VDAC4 Cores screen.asm (Scr_RawPut). It drives the ESP only through SpiDevice::select / exchange, with the
// Z-Controller's read timing: IN returns the previous exchange's MISO and starts an exchange of #FF.
//
// On top: the ZiFi frame layer the Z80 programs use (5A CMD LEN16 DATA XOR), sent through PutChar / Flush and
// read through ReadBurst.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <utility>
#include <vector>

#include "emulator/io/spi/spidevice.h"

namespace Vdac4EspTest
{
struct Frame
{
    uint8_t command = 0;
    std::vector<uint8_t> data;
};

class EspZ80Driver
{
public:
    // zifi_spi.asm
    static constexpr uint8_t kCmdWrBuf = 0x01;
    static constexpr uint8_t kCmdRdBuf = 0x02;
    static constexpr uint8_t kCmdWrDma = 0x03;
    static constexpr uint8_t kCmdRdDma = 0x04;
    static constexpr uint8_t kCmdWrDone = 0x07;
    static constexpr uint8_t kCmdRdDone = 0x08;
    static constexpr uint8_t kRequest = 0xC3;
    static constexpr uint8_t kResponse = 0x3C;
    static constexpr uint8_t kHeader = 5;
    static constexpr uint8_t kChunk = 54;
    static constexpr uint8_t kStateAddress = 60;
    static constexpr uint8_t kTxEmpty = 0x5A;
    static constexpr uint8_t kTxPending = 0xA5;
    static constexpr uint8_t kFlagDrop = 0x01;
    static constexpr uint8_t kFlagBulk = 0x02;
    static constexpr uint8_t kFlagPut = 0x04;
    static constexpr uint8_t kFlagPutDone = 0x08;
    static constexpr uint8_t kFlagMedia = 0x10;
    static constexpr uint8_t kFlagSession = 0x80;
    static constexpr uint8_t kStatusMore = 0x01;
    static constexpr uint8_t kStatusBulk = 0x02;
    static constexpr uint8_t kStatusStale = 0x04;
    static constexpr uint8_t kStatusEchoBad = 0x08;
    static constexpr uint8_t kStatusPutReady = 0x10;
    static constexpr uint8_t kStatusPutOk = 0x20;
    static constexpr uint8_t kStatusPutCapable = 0x80;
    static constexpr size_t kBulk = 512;
    static constexpr uint32_t kPolls = 2048;
    static constexpr uint32_t kAttempts = 16;
    static constexpr uint32_t kPushTries = 2048;
    static constexpr uint32_t kPutTries = 3;
    static constexpr uint32_t kInitTries = 16384;

    /// `tick` runs before every SPI byte (the test's clock), `idle` when a reader finds nothing (a frame wait)
    EspZ80Driver(SpiDevice& spi, std::function<void()> tick, std::function<void()> idle)
        : _spi(spi), _tick(std::move(tick)), _idle(std::move(idle))
    {
    }

    // --- Bus (#77 / #57 through the Z-Controller) ---------------------------------------------------------------
    void Select() { _spi.select(true); }
    void Deselect() { _spi.select(false); }
    void Out(uint8_t value)
    {
        _tick();
        _miso = _spi.exchange(value);
    }
    uint8_t In()
    {
        _tick();
        const uint8_t value = _miso;
        _miso = _spi.exchange(0xFF);
        return value;
    }

    /// SpiLink_OpenRead: RDBUF at `address`; the first read is junk, then a marker-looking byte or the next one.
    /// Stays selected (the caller closes)
    uint8_t OpenRead(uint8_t address)
    {
        Select();
        Out(kCmdRdBuf);
        Out(address);
        In();
        uint8_t value = In();
        if (Marker(value))
            return value;
        value = In();
        return value;
    }

    /// SpiLink_ReadState: byte 60 of the buffer (the state word's flag)
    uint8_t ReadState()
    {
        const uint8_t value = OpenRead(kStateAddress);
        Deselect();
        return value;
    }

    /// The whole state word (5A/A5, D7, version, nonce) by RDBUF from 60
    std::vector<uint8_t> ReadStateWord()
    {
        std::vector<uint8_t> word(4);
        word[0] = OpenRead(kStateAddress);
        for (size_t i = 1; i < 4; ++i)
            word[i] = In();
        Deselect();
        return word;
    }

    /// A raw WRBUF window from address 0
    void WriteBuffer(const uint8_t* data, size_t length)
    {
        Select();
        Out(kCmdWrBuf);
        Out(0);
        Out(0);
        for (size_t i = 0; i < length; ++i)
            Out(data[i]);
        Deselect();
    }

    // --- ZiFi_Init ------------------------------------------------------------------------------------------------
    bool Init()
    {
        _txLen = 0;
        _more = 0;
        _putCan = false;
        uint32_t tries = kInitTries;
        for (;;)
        {
            const uint8_t state = ReadState();
            if (state == kTxEmpty || state == kTxPending)
                break;
            if (--tries == 0)
                return false;
        }
        const bool ok = Send(kFlagSession | kFlagDrop);
        _putCan = (_rsp[0] & kStatusPutCapable) != 0;
        return ok;
    }

    // --- SpiLink_Turn / SpiLink_Send ------------------------------------------------------------------------------
    /// One exchange: the request with the pending TX bytes, polled for its response, re-sent on timeout
    bool Turn(uint8_t flags, uint8_t room, uint8_t* dest)
    {
        _req[2] = flags;
        ++_req[1];
        _req[0] = kRequest;
        _req[3] = _txLen;
        _req[4] = room;
        std::memcpy(_req + kHeader, _tx, _txLen);
        uint8_t sum = 0;
        for (size_t i = 0; i < kHeader + _txLen; ++i)
            sum = static_cast<uint8_t>(sum + _req[i]);
        _req[kHeader + _txLen] = static_cast<uint8_t>(~sum);
        _lastRequestLength = kHeader + _txLen + 1;
        for (uint32_t attempt = 0; attempt < kAttempts; ++attempt)
        {
            ++turnsSent;
            WriteBuffer(_req, _lastRequestLength);
            for (uint32_t poll = 0; poll < kPolls; ++poll)
            {
                if (PollResponse(room, dest))
                {
                    _more = static_cast<uint8_t>(_rsp[0] & (kStatusMore | kStatusBulk));
                    const uint8_t accepted = _rsp[2];
                    const uint8_t left = _txLen > accepted ? static_cast<uint8_t>(_txLen - accepted) : 0;
                    std::memmove(_tx, _tx + accepted, left);
                    _txLen = left;
                    return true;
                }
            }
        }
        _txLen = 0;
        _more = 0;
        return false;
    }

    bool Send(uint8_t flags)
    {
        uint8_t sum[2] = {};
        return Turn(flags, 0, sum);
    }

    /// The last request again, byte for byte (the same transaction number), and its response
    bool ResendLast(uint8_t room, uint8_t* dest)
    {
        WriteBuffer(_req, _lastRequestLength);
        return PollResponse(room, dest);
    }

    // --- ZiFi_PutChar / ZiFi_Flush --------------------------------------------------------------------------------
    bool PutChar(uint8_t value)
    {
        if (_txLen >= kChunk && !Push(kChunk))
            return false;
        _tx[_txLen++] = value;
        return true;
    }

    bool Flush() { return Push(1); }

    bool PutBytes(const uint8_t* data, size_t length)
    {
        for (size_t i = 0; i < length; ++i)
        {
            if (!PutChar(data[i]))
                return false;
        }
        return true;
    }

    // --- ZiFi_ReadBurst (DMA build) -------------------------------------------------------------------------------
    /// Up to 512 bytes into `buffer` (room for 512 + 1)
    size_t ReadBurst(uint8_t* buffer)
    {
        if ((_more & kStatusBulk) != 0)
            return Fetch(buffer);
        for (;;)
        {
            if (_txLen == 0 && _more == 0 && ReadState() != kTxPending)
                return 0;
            if (!Turn(kFlagBulk, kChunk, buffer))
                return 0;
            const uint8_t status = _rsp[0] & (kStatusBulk | kStatusStale);
            if (status == 0)
                return _rsp[1];
            if (status == kStatusBulk)
                return Fetch(buffer);
            // A stale block: read it out of the DMA and drop it
            BulkRead(buffer);
            _more = static_cast<uint8_t>(_more & ~kStatusBulk);
            return 0;
        }
    }

    // --- DMA ------------------------------------------------------------------------------------------------------
    /// SpiLink_BulkRead: 04, three reads, 512 by the DMA, 08
    void BulkRead(uint8_t* buffer)
    {
        Select();
        Out(kCmdRdDma);
        In();
        In();
        In();
        for (size_t i = 0; i < kBulk; ++i)
            buffer[i] = In();
        Deselect();
        BulkDone(kCmdRdDone);
    }

    /// SpiLink_BulkEcho (and Scr_RawPut): 03, two reads, `length` bytes by the DMA, 07
    void BulkWrite(const uint8_t* data, size_t length)
    {
        Select();
        Out(kCmdWrDma);
        In();
        In();
        for (size_t i = 0; i < length; ++i)
            Out(data[i]);
        Deselect();
        BulkDone(kCmdWrDone);
    }

    void BulkDone(uint8_t command)
    {
        Select();
        Out(command);
        In();
        Deselect();
    }

    /// SpiLink_PutBlock: a 512-byte block by PUT, the mailbox when the ESP does not take it
    bool PutBlock(const uint8_t* block)
    {
        uint8_t blockXor = 0;
        for (size_t i = 0; i < kBulk; ++i)
            blockXor ^= block[i];
        for (uint32_t attempt = 0; attempt < kPutTries; ++attempt)
        {
            uint8_t sum[2] = {};
            if (!Turn(kFlagPut, blockXor, sum))
                return false;
            if ((_rsp[0] & kStatusPutReady) == 0 || _txLen != 0)
                break;
            BulkWrite(corruptPut ? CorruptCopy(block, kBulk).data() : block, kBulk);
            if (!Send(kFlagPutDone))
                return false;
            if ((_rsp[0] & kStatusPutOk) != 0)
            {
                ++putBlocksSent;
                return true;
            }
        }
        return PutBytes(block, kBulk);
    }

    /// Scr_RawPut: the control bytes ([command][size LE16][prefix]) ask for a raw media DMA; when the ESP opens
    /// the receive, whole sectors go by the DMA and PUT_DONE closes it. False: not taken (no PUT_READY or not OK)
    bool MediaPut(const std::vector<uint8_t>& control, const uint8_t* data, size_t size)
    {
        std::memcpy(_tx, control.data(), control.size());
        _txLen = static_cast<uint8_t>(control.size());
        uint8_t sum[2] = {};
        if (!Turn(kFlagMedia, 0, sum))
            return false;
        if ((_rsp[0] & kStatusPutReady) == 0)
            return false;
        std::vector<uint8_t> sectors((size + 511) / 512 * 512, 0);
        std::memcpy(sectors.data(), data, size);
        BulkWrite(sectors.data(), sectors.size());
        if (!Send(kFlagPutDone))
            return false;
        return (_rsp[0] & kStatusPutOk) != 0;
    }

    // --- Frames ---------------------------------------------------------------------------------------------------
    bool SendFrame(uint8_t command, const std::vector<uint8_t>& data = {})
    {
        const uint8_t head[4] = {0x5A, command, static_cast<uint8_t>(data.size()), static_cast<uint8_t>(data.size() >> 8)};
        uint8_t checksum = static_cast<uint8_t>(command ^ head[2] ^ head[3]);
        for (uint8_t value : data)
            checksum ^= value;
        return PutBytes(head, sizeof(head)) && PutBytes(data.data(), data.size()) && PutChar(checksum) && Flush();
    }

    /// A frame whose body goes in PUT blocks where it can (Proto_Body with SpiLink_PutBlock)
    bool SendFrameWithPut(uint8_t command, const std::vector<uint8_t>& data)
    {
        const uint8_t head[4] = {0x5A, command, static_cast<uint8_t>(data.size()), static_cast<uint8_t>(data.size() >> 8)};
        uint8_t checksum = static_cast<uint8_t>(command ^ head[2] ^ head[3]);
        for (uint8_t value : data)
            checksum ^= value;
        if (!PutBytes(head, sizeof(head)) || !Flush())
            return false;
        size_t at = 0;
        while (data.size() - at >= kBulk)
        {
            if (!PutBlock(data.data() + at))
                return false;
            at += kBulk;
        }
        return PutBytes(data.data() + at, data.size() - at) && PutChar(checksum) && Flush();
    }

    /// The next frame from the ESP; `waits` idle calls at most while nothing comes
    bool ReceiveFrame(Frame& frame, uint32_t waits = 50)
    {
        for (;;)
        {
            if (TakeFrame(frame))
                return true;
            uint8_t buffer[kBulk + 1];
            const size_t got = ReadBurst(buffer);
            if (got == 0)
            {
                if (waits-- == 0)
                    return false;
                _idle();
                continue;
            }
            _stream.insert(_stream.end(), buffer, buffer + got);
        }
    }

    /// Frames until one with `command` (the ones before it are dropped)
    bool ReceiveCommand(uint8_t command, Frame& frame, uint32_t waits = 50)
    {
        for (uint32_t i = 0; i < 64; ++i)
        {
            if (!ReceiveFrame(frame, waits))
                return false;
            if (frame.command == command)
                return true;
        }
        return false;
    }

    const uint8_t* LastResponse() const { return _rsp; }
    uint8_t LastTid() const { return _req[1]; }
    uint8_t TxLength() const { return _txLen; }
    bool PutCapable() const { return _putCan; }
    size_t BadFrames() const { return _badFrames; }

    void SetTx(const uint8_t* data, uint8_t length)
    {
        std::memcpy(_tx, data, length);
        _txLen = length;
    }

    bool corruptEcho = false;  ///< send the bulk echo with a flipped byte
    bool corruptPut = false;
    uint32_t turnsSent = 0;
    uint32_t bulkBlocksRead = 0;
    uint32_t putBlocksSent = 0;

private:
    static bool Marker(uint8_t value)
    {
        const uint8_t swapped = static_cast<uint8_t>((value >> 4) | (value << 4));
        return static_cast<uint8_t>(swapped ^ value) == 0xFF;
    }

    static std::vector<uint8_t> CorruptCopy(const uint8_t* data, size_t length)
    {
        std::vector<uint8_t> copy(data, data + length);
        copy[length / 2] ^= 0x55;
        return copy;
    }

    /// One poll of SpiLink_Turn: marker, tid, head, length within room, body + sum, the whole sums to #FF
    bool PollResponse(uint8_t room, uint8_t* dest)
    {
        if (OpenRead(0) != kResponse)
        {
            Deselect();
            return false;
        }
        if (In() != _req[1])
        {
            Deselect();
            return false;
        }
        _rsp[0] = In();
        _rsp[1] = In();
        _rsp[2] = In();
        if (room < _rsp[1])
        {
            Deselect();
            return false;
        }
        for (size_t i = 0; i <= _rsp[1]; ++i)
            dest[i] = In();
        Deselect();
        uint8_t sum = static_cast<uint8_t>(_req[1] + kResponse + _rsp[0] + _rsp[1] + _rsp[2]);
        for (size_t i = 0; i <= _rsp[1]; ++i)
            sum = static_cast<uint8_t>(sum + dest[i]);
        return sum == 0xFF;
    }

    bool Push(uint8_t threshold)
    {
        uint32_t tries = kPushTries;
        while (_txLen >= threshold)
        {
            if (!Send(0))
                return false;
            if (--tries == 0)
            {
                _txLen = 0;
                return false;
            }
        }
        return true;
    }

    size_t Fetch(uint8_t* buffer)
    {
        for (;;)
        {
            BulkRead(buffer);
            BulkWrite(corruptEcho ? CorruptCopy(buffer, kBulk).data() : buffer, kBulk);
            if (!Send(kFlagBulk))
                return 0;
            if ((_rsp[0] & kStatusEchoBad) == 0)
            {
                ++bulkBlocksRead;
                return kBulk;
            }
            if ((_more & kStatusBulk) != 0)
                continue;
            // Given up: the block comes by plain responses
            for (;;)
            {
                if (_txLen == 0 && _more == 0 && ReadState() != kTxPending)
                    return 0;
                if (!Turn(kFlagBulk, kChunk, buffer))
                    return 0;
                const uint8_t status = _rsp[0] & (kStatusBulk | kStatusStale);
                if (status == 0)
                    return _rsp[1];
                if (status == kStatusBulk)
                    break;  // a new block by DMA: fetch it
                BulkRead(buffer);
                _more = static_cast<uint8_t>(_more & ~kStatusBulk);
                return 0;
            }
        }
    }

    bool TakeFrame(Frame& frame)
    {
        for (;;)
        {
            // Wait for sync: the ESP's pad bytes (00) and anything else before 5A are skipped
            while (!_stream.empty() && _stream.front() != 0x5A)
                _stream.pop_front();
            if (_stream.size() < 4)
                return false;
            const size_t length = static_cast<size_t>(_stream[2] | (_stream[3] << 8));
            if (length > 1024)
            {
                _stream.pop_front();
                ++_badFrames;
                continue;
            }
            if (_stream.size() < length + 5)
                return false;
            uint8_t checksum = static_cast<uint8_t>(_stream[1] ^ _stream[2] ^ _stream[3]);
            for (size_t i = 0; i < length; ++i)
                checksum ^= _stream[4 + i];
            if (checksum != _stream[4 + length])
            {
                _stream.pop_front();
                ++_badFrames;
                continue;
            }
            frame.command = _stream[1];
            frame.data.assign(_stream.begin() + 4, _stream.begin() + 4 + static_cast<std::ptrdiff_t>(length));
            _stream.erase(_stream.begin(), _stream.begin() + static_cast<std::ptrdiff_t>(length + 5));
            return true;
        }
    }

    SpiDevice& _spi;
    std::function<void()> _tick;
    std::function<void()> _idle;
    uint8_t _miso = 0xFF;
    uint8_t _req[kHeader + kChunk + 1] = {};
    size_t _lastRequestLength = 0;
    uint8_t _tx[kChunk + 8] = {};
    uint8_t _txLen = 0;
    uint8_t _rsp[4] = {};
    uint8_t _more = 0;
    bool _putCan = false;
    std::deque<uint8_t> _stream;
    size_t _badFrames = 0;
};
}  // namespace Vdac4EspTest
