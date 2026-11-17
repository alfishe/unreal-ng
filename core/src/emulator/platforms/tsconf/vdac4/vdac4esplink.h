#pragma once

/// @file vdac4esplink.h
/// @brief The ESP's link task on the slave HD buffer: what the firmware's SpiLink does around SpiLinkCore.
///
/// Port of the Z80-visible parts of VDAC4 `ESP32 S3 Zero N8R8/Firmware/src/spi_link.cpp` (MIT License,
/// Copyright (c) andrewinsidelazarev): startDriver (zero exchange area, then the state word), publishState
/// (with its re-check against a racing write), taskLoop (one pass per WRBUF: read bytes 0..59, take DMA results,
/// bulk state, HandleRequest, load the bulk block and its echo receive, open the put receive, write the
/// response bytes 4..59 then the head 0..3, republish the state word), write (state word "pending" after the
/// bytes are in), handleRawAsk (flag #40) and dmaProbe (LINK_PROBE).
///
/// Left out: the line counters, the self test and the bring-up log (none is visible to the Z80). RAWOUT is the
/// firmware's code path with no raw window ever offered (only the SMB server offers one): a valid RAWOUT ask
/// answers status 0, "no portion yet".
///
/// Rings: 16384 bytes Z80 -> ESP (a full raw media DMA plus header), 4096 bytes ESP -> Z80 (spi_link.hpp).

#include <cstddef>
#include <cstdint>
#include <vector>

#include "vdac4esplinkcore.h"

class Vdac4EspSpi;
class Vdac4MediaRange;
class Vdac4EspStateWriter;
class Vdac4EspStateReader;

class EspSpiLink
{
public:
    static constexpr size_t kRingBytes = 4096;
    static constexpr size_t kInputRingBytes = 16384;
    static constexpr uint32_t kPeerQuietMs = 5000;
    static constexpr uint32_t kReserveLimitMs = 30000;
    static constexpr uint32_t kFlushTimeoutMs = 1000;

    // LINK_PROBE (SpiLink::dmaProbe)
    static constexpr size_t kDmaProbeBytes = 2048;
    enum DmaProbe : uint8_t
    {
        kDmaProbeSend = 1,
        kDmaProbeReceive = 2,
        kDmaProbeFinish = 3,
    };
    enum DmaProbeStatus : uint8_t
    {
        kDmaProbeFailed = 0,
        kDmaProbeOk = 1,
        kDmaProbePending = 2,
    };
    struct DmaProbeResult
    {
        uint8_t status = kDmaProbeFailed;
        uint16_t length = 0;
        uint16_t errors = 0;
        uint16_t firstBad = 0;
    };
    static uint8_t DmaProbeByte(size_t index, uint8_t seed)
    {
        return static_cast<uint8_t>(index + 3 * (index >> 8) + seed);
    }

    EspSpiLink(Vdac4EspSpi& spi, const Vdac4MediaRange& media);

    /// The firmware started the driver (boot): rings empty, exchange area zero, state word published
    void Begin(uint8_t bootNonce, uint32_t nowMs);
    bool Ready() const { return _ready; }
    /// The chip went down (restart, power-off)
    void Stop() { _ready = false; }

    /// One pass of the link task after a WRBUF (cb_buffer_rx)
    void OnRequestWritten(uint32_t nowMs);

    // The protocol loop's side (ByteLink)
    size_t Read(uint8_t* out, size_t capacity) { return _ready ? _core.Read(out, capacity) : 0; }
    size_t Write(const uint8_t* data, size_t length);
    size_t WriteSpace() const { return _core.WriteSpace(); }
    size_t WriteCapacity() const { return _core.WriteCapacity(); }
    size_t PendingToZ80() const { return _core.PendingToZ80(); }
    size_t PendingFromZ80() const { return _core.PendingFromZ80(); }
    /// millis() of the last request served (reserve(): the Z80 is still exchanging)
    uint32_t LastActivityMs() const { return _lastActivityMs; }
    const EspSpiLinkCore::Stats& GetStats() const { return _core.GetStats(); }
    uint8_t BootNonce() const { return _bootNonce; }
    /// DMA busy with a block (it is loaded or its echo receive is open)
    bool BulkBusy() const { return _sendPending || _echoPending; }

    DmaProbeResult RunDmaProbe(uint8_t action, size_t length, uint8_t seed);

    void Save(Vdac4EspStateWriter& out) const;
    bool Load(Vdac4EspStateReader& in);

private:
    void PublishState();
    bool HandleRawAsk(const uint8_t* request, uint8_t* response, size_t& length);

    Vdac4EspSpi& _spi;
    EspSpiLinkCore _core;
    bool _ready = false;
    uint8_t _bootNonce = 0;
    uint32_t _lastActivityMs = 0;
    std::vector<uint8_t> _bulkBuffer;  // kBulkSize + 4
    std::vector<uint8_t> _echoBuffer;  // kMediaMax + 4
    bool _sendPending = false;   // a block is loaded and not read yet
    bool _echoPending = false;   // the receive DMA is open (echo of a block or a put)
    bool _receiveIsPut = false;
    bool _echoGood = false;
    bool _putReceived = false;
    size_t _putLength = 0;
    // handleRawAsk's memory (no raw window is ever offered)
    bool _rawHaveLast = false;
    uint8_t _rawLastTid = 0;
    uint8_t _rawLast[EspSpiLinkCore::kHeaderSize + 1] = {};
    // dmaProbe
    std::vector<uint8_t> _probeBuffer;
    uint8_t _probeAction = 0;
    uint8_t _probeSeed = 0;
    uint16_t _probeLength = 0;
};
