#pragma once

/// @file sdcardspi.h
/// @brief SD card in SPI mode over a raw image file (neogs-tdd.md §5.5).
///
/// A device for any SD/SPI host: the NeoGS card's SD master today, and meant
/// for Z-Controller and TS-Conf too. SD physical layer, SPI mode, versions 1
/// and 2, SDSC and SDHC.
///
/// Commands: CMD0 CMD8 CMD55+ACMD41 CMD59 CMD16 CMD58 CMD9 CMD10 CMD13 CMD17
/// CMD18 CMD12 CMD24 CMD25 (#FC/#FD tokens); everything else answers
/// "illegal command". CRC is off by default in SPI mode and checked only for
/// CMD0 and CMD8, and for every command after CMD59 turns it on.
///
/// Timing is counted in exchanged bytes, not time: a read's data token comes
/// after 8 poll bytes, a write stays busy for 64. Deterministic, and short
/// enough for every driver's polling loop.
///
/// Writes (`WriteMode`): Session keeps written sectors in memory (discarded
/// with the object), Persist writes through to the file, Off answers with a
/// write-protect data response (#0D) and sets WP_VIOLATION in CMD13's status.

#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <string>

#include "emulator/io/spi/spidevice.h"

class SdCardSpi : public SpiDevice
{
public:
    enum class WriteMode : uint8_t { Session, Persist, Off };
    enum class Type : uint8_t { Auto, SDSC, SDHC };

    static constexpr size_t BLOCK = 512;
    static constexpr int READ_LATENCY_BYTES = 8;
    static constexpr int WRITE_BUSY_BYTES = 64;
    static constexpr int ACMD41_IDLE_RESPONSES = 4; // ACMD41 answers "idle" this many times first

    SdCardSpi() = default;
    ~SdCardSpi() override;

    SdCardSpi(const SdCardSpi&) = delete;
    SdCardSpi& operator=(const SdCardSpi&) = delete;

    /// Insert an image. Up to 2 GB is SDSC, bigger SDHC, unless `type` says
    /// otherwise. A size that is not a multiple of 512 is padded (reads as 0).
    bool open(const std::string& path, WriteMode mode = WriteMode::Session, Type type = Type::Auto);
    /// Remove the card (detect switch open)
    void close();
    bool present() const { return _file != nullptr; }
    const std::string& path() const { return _path; }
    bool isSdhc() const { return _sdhc; }
    uint64_t sizeBytes() const { return _blocks * BLOCK; }
    WriteMode writeMode() const { return _writeMode; }

    /// Power cycle: back to the pre-CMD0 state
    void powerOn();

    // SpiDevice
    void select(bool selected) override;
    uint8_t exchange(uint8_t mosi) override;
    void truncatedByte() override;

    /// Diagnostics
    uint64_t blocksRead() const { return _blocksRead; }
    uint64_t blocksWritten() const { return _blocksWritten; }
    uint8_t lastCommand() const { return _lastCommand; }
    uint32_t lastArgument() const { return _lastArgument; }
    bool initialized() const { return _state == State::Ready; }

    /// Protocol state snapshot (TTD). Not the sectors: the image and the
    /// session's written sectors stay outside, so a card write is a replay
    /// barrier for the owner to mark (see `setWriteListener`).
    static constexpr size_t STATE_SIZE = 2688;
    static constexpr size_t STATE_OUT_MAX = 2048; // a streaming read keeps up to two blocks queued
    void saveState(uint8_t* dst) const;
    void loadState(const uint8_t* src);

    /// Called after every block the card writes (not the refused ones)
    void setWriteListener(std::function<void(uint64_t block)> listener) { _onWrite = std::move(listener); }

    /// Direct sector access (tests, automation); returns false beyond the card
    bool readBlock(uint64_t block, uint8_t* out) const;
    bool writeBlock(uint64_t block, const uint8_t* data);

private:
    enum class State : uint8_t { PowerOn, Idle, Ready };
    enum class Mode : uint8_t
    {
        Command,      // collecting a command from MOSI
        ReadMulti,    // CMD18: streaming blocks until CMD12
        WaitToken,    // CMD24/25: waiting for the data token
        ReceiveData,  // 512 data + 2 CRC bytes coming in
    };

    void onCommand();
    void respondR1(uint8_t r1);
    void queueBlock(uint64_t block);
    void finishWrite();
    uint8_t r1Flags() const;
    uint64_t blockOfArgument(uint32_t arg) const { return _sdhc ? arg : arg / BLOCK; }
    void buildCsd(uint8_t out[16]) const;
    void buildCid(uint8_t out[16]) const;
    static uint8_t crc7(const uint8_t* data, size_t length);
    static uint16_t crc16(const uint8_t* data, size_t length);

    // Image
    FILE* _file = nullptr;
    std::string _path;
    uint64_t _blocks = 0;
    bool _sdhc = false;
    WriteMode _writeMode = WriteMode::Session;
    std::map<uint64_t, std::array<uint8_t, BLOCK>> _overlay; // session writes

    // Protocol
    State _state = State::PowerOn;
    Mode _mode = Mode::Command;
    bool _selected = false;
    bool _crcOn = false;
    bool _appCommand = false;       // CMD55 seen
    int _acmd41Count = 0;
    bool _wpViolation = false;

    uint8_t _cmd[6] = {};
    int _cmdLength = 0;
    uint8_t _lastCommand = 0xFF;
    uint32_t _lastArgument = 0;

    std::deque<uint8_t> _out;       // bytes the card will send
    uint64_t _nextReadBlock = 0;    // CMD18 streaming position

    bool _multiWrite = false;
    uint64_t _writeBlock = 0;
    std::array<uint8_t, BLOCK + 2> _rx{};
    size_t _rxCount = 0;

    uint64_t _blocksRead = 0;
    uint64_t _blocksWritten = 0;

    std::function<void(uint64_t)> _onWrite;
};
