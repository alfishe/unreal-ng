#include "stdafx.h"

#include "vdac4esplink.h"

#include <algorithm>
#include <cstring>

#include "vdac4espspi.h"
#include "vdac4espstate.h"

// Port of VDAC4 Firmware src/spi_link.cpp (MIT, andrewinsidelazarev); see the header.

EspSpiLink::EspSpiLink(Vdac4EspSpi& spi, const Vdac4MediaRange& media)
    : _spi(spi), _core(media), _bulkBuffer(EspSpiLinkCore::kBulkSize + 4, 0),
      _echoBuffer(EspSpiLinkCore::kMediaMax + 4, 0)
{
}

void EspSpiLink::Begin(uint8_t bootNonce, uint32_t nowMs)
{
    _core.Begin(kInputRingBytes, kRingBytes);
    _bootNonce = bootNonce;
    _lastActivityMs = nowMs;
    std::fill(_bulkBuffer.begin(), _bulkBuffer.end(), 0);
    std::fill(_echoBuffer.begin(), _echoBuffer.end(), 0);
    _sendPending = _echoPending = _receiveIsPut = _echoGood = _putReceived = false;
    _putLength = 0;
    _rawHaveLast = false;
    _probeAction = 0;
    _ready = true;
    // First the exchange area, then the state word: seeing the magic, the Z80 may write a request at once
    const uint8_t zero[EspSpiLinkCore::kTurnSize] = {};
    _spi.WriteBuffer(0, zero, sizeof(zero));
    PublishState();
}

void EspSpiLink::PublishState()
{
    uint8_t word[EspSpiLinkCore::kStateSize];
    _core.StateWord(word, _bootNonce);
    _spi.WriteBuffer(EspSpiLinkCore::kStateOffset, word, sizeof(word));
    // The firmware re-checks after writing "empty" (an app write may have raced it); single-threaded here the
    // check cannot fire, kept for the same result
    if (word[0] == EspSpiLinkCore::kStateEmpty && _core.PendingToZ80() != 0)
    {
        word[0] = EspSpiLinkCore::kStatePending;
        _spi.WriteBuffer(EspSpiLinkCore::kStateOffset, word, sizeof(word));
    }
}

size_t EspSpiLink::Write(const uint8_t* data, size_t length)
{
    if (!_ready)
        return 0;
    const size_t written = _core.Write(data, length);
    if (written != 0)
    {
        // The flag goes up after the bytes are in the ring: a Z80 that sees it gets them
        const uint8_t word[EspSpiLinkCore::kStateSize] = {EspSpiLinkCore::kStatePending, EspSpiLinkCore::kMagic,
                                                          EspSpiLinkCore::kVersion, _bootNonce};
        _spi.WriteBuffer(EspSpiLinkCore::kStateOffset, word, sizeof(word));
    }
    return written;
}

void EspSpiLink::OnRequestWritten(uint32_t nowMs)
{
    if (!_ready)
        return;
    uint8_t request[EspSpiLinkCore::kTurnSize];
    uint8_t response[EspSpiLinkCore::kTurnSize] = {};
    _spi.ReadBuffer(0, request, sizeof(request));
    // The Z80 ends a block read with 08, the echo with 07
    if (_sendPending && _spi.TakeSendResult())
        _sendPending = false;
    const uint8_t* received = nullptr;
    size_t receivedLength = 0;
    if (_echoPending && _spi.TakeReceiveResult(received, receivedLength))
    {
        _echoPending = false;
        const size_t kept = std::min(receivedLength, _echoBuffer.size());
        std::memcpy(_echoBuffer.data(), received, kept);
        if (_receiveIsPut)
        {
            _putReceived = true;
            _putLength = receivedLength;
        }
        else
        {
            _echoGood = receivedLength >= EspSpiLinkCore::kBulkSize &&
                        std::memcmp(_echoBuffer.data(), _bulkBuffer.data(), EspSpiLinkCore::kBulkSize) == 0;
        }
    }
    // While a DMA probe runs, the DMA channels are its own
    const EspSpiLinkCore::BulkState bulk =
        _probeAction != 0 ? EspSpiLinkCore::kBulkOff
                          : (_sendPending ? EspSpiLinkCore::kBulkLoaded
                                          : (_echoGood ? EspSpiLinkCore::kBulkEchoed : EspSpiLinkCore::kBulkFree));
    EspSpiLinkCore::PutInput put;
    put.available = bulk != EspSpiLinkCore::kBulkOff;
    put.capacity = _echoPending ? EspSpiLinkCore::kBulkSize : EspSpiLinkCore::kMediaMax;
    if (_putReceived)
    {
        put.received = _echoBuffer.data();
        put.receivedLength = _putLength;
    }
    size_t length = 0;
    if (HandleRawAsk(request, response, length))
    {
        _spi.WriteBuffer(4, response + 4, length - 4);
        _spi.WriteBuffer(0, response, 4);
        _lastActivityMs = nowMs;
        PublishState();
        return;
    }
    if (!_core.HandleRequest(request, response, length, bulk, _bulkBuffer.data(), put))
        return;
    if ((response[2] & EspSpiLinkCore::kStatusBulk) != 0 && (response[2] & EspSpiLinkCore::kStatusBulkStale) == 0)
    {
        // The block in the bulk buffer is new, given again, or the same on a repeat: load it and open the echo
        // receive if not done yet, before the response goes out
        _echoGood = false;
        _receiveIsPut = false;
        if (!_sendPending)
            _sendPending = _spi.QueueSend(_bulkBuffer.data(), EspSpiLinkCore::kBulkSize + 4);
        if (!_echoPending)
            _echoPending = _spi.QueueReceive(EspSpiLinkCore::kBulkSize + 4);
    }
    if ((response[2] & (EspSpiLinkCore::kStatusPutOk | EspSpiLinkCore::kStatusPutBad)) != 0)
    {
        _putReceived = false;  // the verdict is out: what came is no longer needed
        _putLength = 0;
    }
    if ((response[2] & EspSpiLinkCore::kStatusPutReady) != 0)
    {
        // The Z80 writes a block now: open the receive unless it is open (then it was opened for an echo and
        // takes the block now)
        _receiveIsPut = true;
        _putReceived = false;
        if (!_echoPending)
            _echoPending = _spi.QueueReceive(_core.PutDmaSize() + 4);
    }
    // The response from its end: the head 0..3 with the 3C marker last
    _spi.WriteBuffer(4, response + 4, length - 4);
    _spi.WriteBuffer(0, response, 4);
    _lastActivityMs = nowMs;
    PublishState();
}

bool EspSpiLink::HandleRawAsk(const uint8_t* request, uint8_t* response, size_t& length)
{
    constexpr size_t kHead = EspSpiLinkCore::kHeaderSize;
    if (request[0] != EspSpiLinkCore::kRequestMarker || (request[2] & EspSpiLinkCore::kFlagRawOut) == 0 ||
        (request[2] & EspSpiLinkCore::kFlagNewSession) != 0 || request[3] != 0 ||
        EspSpiLinkCore::Checksum(request, kHead) != request[kHead])
    {
        _rawHaveLast = false;
        return false;
    }
    const uint8_t tid = request[1];
    if (_rawHaveLast && tid == _rawLastTid)
    {
        std::memcpy(response, _rawLast, kHead + 1);
    }
    else
    {
        // No raw window is ever offered here (total 0): "no portion yet"
        response[0] = EspSpiLinkCore::kResponseMarker;
        response[1] = tid;
        response[2] = 0;
        response[3] = 0;
        response[4] = 0;
        response[kHead] = EspSpiLinkCore::Checksum(response, kHead);
        std::memcpy(_rawLast, response, kHead + 1);
        _rawLastTid = tid;
        _rawHaveLast = true;
    }
    length = kHead + 1;
    _core.ForgetLastTurn();
    return true;
}

EspSpiLink::DmaProbeResult EspSpiLink::RunDmaProbe(uint8_t action, size_t length, uint8_t seed)
{
    DmaProbeResult result;
    if (!_ready)
        return result;
    if (action == kDmaProbeFinish)
    {
        if (_probeAction == 0)
            return result;
        // The master ends the transaction: 08 after a read, 07 after a write. No wait here (the firmware's
        // 200 ms are emulated time the Z80 does not spend inside this command)
        const uint8_t* received = nullptr;
        size_t receivedLength = 0;
        const bool done = _probeAction == kDmaProbeSend ? _spi.TakeSendResult()
                                                        : _spi.TakeReceiveResult(received, receivedLength);
        if (!done)
        {
            result.status = kDmaProbePending;
            return result;
        }
        result.status = kDmaProbeOk;
        result.length = _probeLength;
        if (_probeAction == kDmaProbeReceive)
        {
            result.length = static_cast<uint16_t>(receivedLength);
            const size_t checked = std::min<size_t>(receivedLength, _probeLength);
            for (size_t index = 0; index < checked; ++index)
            {
                if (received[index] != DmaProbeByte(index, _probeSeed))
                {
                    if (result.errors == 0)
                        result.firstBad = static_cast<uint16_t>(index);
                    ++result.errors;
                }
            }
        }
        _probeAction = 0;
        return result;
    }
    if ((action != kDmaProbeSend && action != kDmaProbeReceive) || length == 0 || length > kDmaProbeBytes ||
        (length & 1) != 0 || _probeAction != 0 || BulkBusy())
        return result;
    bool queued = false;
    if (action == kDmaProbeSend)
    {
        // Four spare bytes: the TS-Conf DMA starts one more exchange after the last byte it needs
        _probeBuffer.resize(length + 4);
        for (size_t index = 0; index < length + 4; ++index)
            _probeBuffer[index] = DmaProbeByte(index, seed);
        queued = _spi.QueueSend(_probeBuffer.data(), length + 4);
    }
    else
    {
        queued = _spi.QueueReceive(kDmaProbeBytes);
    }
    if (!queued)
        return result;
    _probeAction = action;
    _probeSeed = seed;
    _probeLength = static_cast<uint16_t>(length);
    result.status = kDmaProbeOk;
    result.length = _probeLength;
    return result;
}

void EspSpiLink::Save(Vdac4EspStateWriter& out) const
{
    out.Bool(_ready);
    out.U8(_bootNonce);
    out.U32(_lastActivityMs);
    _core.Save(out);
    out.Bytes(_bulkBuffer.data(), _bulkBuffer.size());
    out.Bytes(_echoBuffer.data(), _echoBuffer.size());
    out.Bool(_sendPending);
    out.Bool(_echoPending);
    out.Bool(_receiveIsPut);
    out.Bool(_echoGood);
    out.Bool(_putReceived);
    out.U32(static_cast<uint32_t>(_putLength));
    out.Bool(_rawHaveLast);
    out.U8(_rawLastTid);
    out.Bytes(_rawLast, sizeof(_rawLast));
    out.U8(_probeAction);
    out.U8(_probeSeed);
    out.U16(_probeLength);
}

bool EspSpiLink::Load(Vdac4EspStateReader& in)
{
    _ready = in.Bool();
    _bootNonce = in.U8();
    _lastActivityMs = in.U32();
    _core.Begin(kInputRingBytes, kRingBytes);  // the rings exist (a device that never booted has none yet)
    if (!_core.Load(in))
        return false;
    in.Bytes(_bulkBuffer.data(), _bulkBuffer.size());
    in.Bytes(_echoBuffer.data(), _echoBuffer.size());
    _sendPending = in.Bool();
    _echoPending = in.Bool();
    _receiveIsPut = in.Bool();
    _echoGood = in.Bool();
    _putReceived = in.Bool();
    _putLength = in.U32();
    _rawHaveLast = in.Bool();
    _rawLastTid = in.U8();
    in.Bytes(_rawLast, sizeof(_rawLast));
    _probeAction = in.U8();
    _probeSeed = in.U8();
    _probeLength = in.U16();
    return in.Ok();
}
