#pragma once

/// @file vdac4espspi.h
/// @brief The ESP32-S3's SPI2 in Slave HD mode on the VDAC4 board, as the Evo's Z-Controller SPI sees it
/// (research-z80-esp-interface.md §1-2; firmware src/spi_link.cpp startDriver).
///
/// Slave configuration (firmware): 8-bit command, 8-bit address, 8 dummy bits, mode 0, queue size 1. Every
/// transaction is one chip-select window (#77 = #13 ... #77 = #03), decoded byte by byte:
///
///   byte 0     command
///   byte 1     address          (01, 02, 03, 04 only)
///   byte 2     dummy
///   byte 3..   data
///
///   01 WRBUF   data written into the 64-byte shared buffer from `address` (wrapping inside the buffer); the end
///              of the window is the firmware's cb_buffer_rx (IHost::OnBufferWritten)
///   02 RDBUF   data read from the shared buffer from `address`
///   03 WRDMA   data into the queued receive DMA transaction (dropped past its length or with none queued)
///   04 RDDMA   data from the queued send DMA transaction (#FF past its end or with none queued)
///   07 WR_DONE (own window) ends the receive transaction: its result is the count of bytes that came
///   08 RD_DONE (own window) ends the send transaction
///   anything else is ignored for the rest of the window
///
/// A segment transaction spans windows: two 04 windows before one 08 continue the same send buffer.
///
/// MISO. During the command, address and dummy bytes, and in every byte of a write, the slave drives nothing
/// useful: the device returns #FF (the driver only needs it not to look like a marker, zifi_spi.asm
/// SpiLink_OpenRead: "#FF or #00"). A data byte of RDBUF / RDDMA is shifted out during that exchange, so with
/// the Z-Controller's "previous exchange" read (zcontrollerspi.h) the driver's 14 MHz path runs: two junk bytes
/// before RDBUF data, `04` + three reads before the DMA (zifi_spi.asm SpiLink_BulkRead).
///
/// Power: until the firmware has started the driver (Vdac4Esp boot, EspBootMs) the slave is not configured:
/// every byte reads #FF and writes are lost. Starting the driver clears nothing by itself; the link writes the
/// zero exchange area and the state word (SpiLink::startDriver).
///
/// Bus position: hub slot 2, #77 bit 4, active high in register terms (#13 selects, #03 deselects).

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "emulator/io/spi/spidevice.h"

class Vdac4EspStateWriter;
class Vdac4EspStateReader;

class Vdac4EspSpi final : public SpiDevice
{
public:
    static constexpr uint8_t kSpiSlot = 2;
    static constexpr uint8_t kSpiSelectMask = 0x10;  // #77 D4
    static constexpr bool kSpiSelectActiveHigh = true;

    static constexpr size_t kBufferSize = 64;
    static constexpr uint8_t kCmdWriteBuffer = 0x01;
    static constexpr uint8_t kCmdReadBuffer = 0x02;
    static constexpr uint8_t kCmdWriteDma = 0x03;
    static constexpr uint8_t kCmdReadDma = 0x04;
    static constexpr uint8_t kCmdWriteDone = 0x07;
    static constexpr uint8_t kCmdReadDone = 0x08;
    static constexpr uint8_t kIdleMiso = 0xFF;
    /// Largest DMA transaction (bus max_transfer_sz = kMediaMax + 4, the raw window adds alignment)
    static constexpr size_t kMaxTransaction = 8192 + 512 + 64;

    /// The firmware side of the slave
    class IHost
    {
    public:
        virtual ~IHost() = default;
        /// Before every chip-select change and byte: the owner brings its time up to now
        virtual void OnBusAccess() = 0;
        /// A WRBUF window ended (cb_buffer_rx)
        virtual void OnBufferWritten() = 0;
    };

    explicit Vdac4EspSpi(IHost* host) : _host(host) {}

    // SpiDevice
    void select(bool selected) override;
    uint8_t exchange(uint8_t mosi) override;

    /// The driver is running (after boot) or not (power-on, restart)
    void SetPowered(bool powered);
    bool IsPowered() const { return _powered; }

    // --- The ESP side (spi_slave_hd_* of ESP-IDF) ------------------------------------------------------------

    void WriteBuffer(size_t address, const uint8_t* data, size_t length);
    void ReadBuffer(size_t address, uint8_t* out, size_t length) const;
    const std::array<uint8_t, kBufferSize>& Buffer() const { return _buffer; }

    /// spi_slave_hd_queue_trans on the send channel (a copy is taken: the firmware never changes the buffer
    /// while it is queued). False: one is already queued (queue size 1)
    bool QueueSend(const uint8_t* data, size_t length);
    /// spi_slave_hd_queue_trans on the receive channel, `length` bytes at most
    bool QueueReceive(size_t length);
    /// spi_slave_hd_get_trans_res, no wait: the send transaction ended (RD_DONE)
    bool TakeSendResult();
    /// The receive transaction ended (WR_DONE): what came, `received` its trans_len
    bool TakeReceiveResult(const uint8_t*& data, size_t& received);
    bool SendQueued() const { return _send.queued; }
    bool ReceiveQueued() const { return _receive.queued; }

    /// Diagnostics: windows with a command the slave ignores
    uint32_t BadCommands() const { return _badCommands; }

    void Save(Vdac4EspStateWriter& out) const;
    bool Load(Vdac4EspStateReader& in);

private:
    struct Channel
    {
        std::vector<uint8_t> data;  // send: the bytes to shift out; receive: what came
        uint32_t length = 0;        // transaction length
        uint32_t position = 0;      // bytes done
        bool queued = false;
        bool done = false;          // ended by 07 / 08, result not taken yet
    };
    void SaveChannel(Vdac4EspStateWriter& out, const Channel& channel) const;
    bool LoadChannel(Vdac4EspStateReader& in, Channel& channel);

    IHost* _host;
    std::array<uint8_t, kBufferSize> _buffer{};
    Channel _send;
    Channel _receive;
    bool _powered = false;
    bool _selected = false;
    uint32_t _byteIndex = 0;  // bytes of the current window
    uint8_t _command = 0;
    uint8_t _address = 0;
    uint32_t _badCommands = 0;
};
