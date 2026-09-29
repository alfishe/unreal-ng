#pragma once

/// @file vs10xx.h
/// @brief VS1001 / VS1011-class MP3 decoder (the NeoGS board's MA8201 clones)
/// on two SPI ports: SCI (control registers, with chip select) and SDI (data,
/// no chip select) - neogs-tdd.md §5.6.
///
/// Registers (16-bit): 0 MODE, 1 STATUS, 2 INT_FCTLH (VS1001) / BASS (VS1011),
/// 3 CLOCKF, 4 DECODE_TIME, 5 AUDATA, 6 WRAM, 7 WRAMADDR, 8 HDAT0, 9 HDAT1,
/// 10 AIADDR, 11 VOL. SCI frame: opcode (#02 write, #03 read), address, then
/// 16 bits MSB first.
///
/// Data path: SDI bytes -> 2,048-byte input FIFO (DREQ = 1 while >= 32 bytes
/// are free) -> our own frame parser (skips ID3v2 and junk, hands minimp3
/// exactly one frame) -> PCM queue, played at the stream's rate in emulated
/// time. When the queue runs below 512 stereo samples (the chip's audio FIFO)
/// the next frame is decoded. DECODE_TIME counts seconds of decoded audio
/// played; HDAT holds the current frame header; AUDATA the stream format.
///
/// Resets: XRESET low holds the chip (all registers 0, DREQ 0) and DREQ stays
/// 0 for 50,000 crystal clocks after it rises; MODE bit 2 (SM_RESET) is a
/// software reset (DREQ 0 for 6,000 clocks, VOL kept). Either clears the
/// FIFOs, HDAT and DECODE_TIME.
///
/// Everything the emulated machine can see (DREQ, registers, FIFO levels)
/// depends only on the bytes fed and on time, so replays are exact on every
/// platform. The PCM itself may differ in the last bits between x64 and arm64
/// builds (minimp3's SIMD paths); it never feeds back into the machine.

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "emulator/io/spi/spidevice.h"

struct Vs10xxMp3State; // minimp3's decoder state (vs10xx.cpp)

class Vs10xxDecoder
{
public:
    enum class Chip : uint8_t { VS1001, VS1011 };
    enum class Level : uint8_t { Stub, Software }; // "none" = no decoder object at all

    // Registers
    static constexpr uint8_t SCI_MODE = 0;
    static constexpr uint8_t SCI_STATUS = 1;
    static constexpr uint8_t SCI_REG2 = 2; // INT_FCTLH (VS1001) / BASS (VS1011)
    static constexpr uint8_t SCI_CLOCKF = 3;
    static constexpr uint8_t SCI_DECODE_TIME = 4;
    static constexpr uint8_t SCI_AUDATA = 5;
    static constexpr uint8_t SCI_WRAM = 6;
    static constexpr uint8_t SCI_WRAMADDR = 7;
    static constexpr uint8_t SCI_HDAT0 = 8;
    static constexpr uint8_t SCI_HDAT1 = 9;
    static constexpr uint8_t SCI_AIADDR = 10;
    static constexpr uint8_t SCI_VOL = 11;
    static constexpr int REGISTERS = 16;

    static constexpr uint16_t SM_RESET = 0x0004;

    static constexpr size_t INPUT_FIFO = 2048;
    static constexpr size_t DREQ_FREE = 32;
    static constexpr size_t PCM_LOW_WATER = 512; // stereo samples: the chip's audio FIFO

    static constexpr double CRYSTAL_HZ = 14.318e6; // board crystal (the doubler does not change reset timing)
    static constexpr double HARD_RESET_CLOCKS = 50000;
    static constexpr double SOFT_RESET_CLOCKS = 6000;

    Vs10xxDecoder(Chip chip, Level level, double unitsPerSecond);
    ~Vs10xxDecoder();

    Vs10xxDecoder(const Vs10xxDecoder&) = delete;
    Vs10xxDecoder& operator=(const Vs10xxDecoder&) = delete;

    /// The two SPI ports, for the card's masters
    SpiDevice* sci() { return &_sciPort; }
    SpiDevice* sdi() { return &_sdiPort; }

    /// XRESET pin (SCTRL bit 2; 1 = run). `now` in the owner's time unit.
    void setReset(bool running, int64_t now);

    /// Advance the decoder to `now`: plays PCM at the stream rate, decoding
    /// frames as the queue runs low. Cheap to call often.
    void advance(int64_t now);

    /// DREQ pin at `now` (advance() first)
    bool dreq(int64_t now);

    /// Audio produced between the last renderFrame call and `now`,
    /// resampled to `outRate` into `out` (interleaved stereo int16,
    /// `samples` frames). Applies VOL and `gain`. Returns true when anything
    /// but silence was written.
    bool renderFrame(int16_t* out, int samples, double gain);

    /// Raw stream samples played since the last render/drain (interleaved
    /// stereo, the stream's rate) - tests and diagnostics; clears them
    std::vector<int16_t> drainPlayed()
    {
        std::vector<int16_t> out;
        out.swap(_played);
        return out;
    }

    // Introspection (automation, tests)
    uint16_t reg(int index) const { return _regs[index & 0x0F]; }
    Chip chip() const { return _chip; }
    size_t inputFill() const { return _fifo.size(); }
    size_t pcmQueued() const { return (_pcm.size() - _pcmRead) / 2; }
    uint32_t streamRate() const { return _rate; }
    int streamChannels() const { return _channels; }
    uint64_t framesDecoded() const { return _framesDecoded; }
    uint64_t samplesPlayed() const { return _samplesPlayed; }
    uint64_t bytesReceived() const { return _bytesReceived; }
    bool running() const { return _running; }

    /// Snapshot (TTD). Everything but the output resampler's pending samples,
    /// which are audio only. `machineVisibleOnly` zeroes the decoded PCM and
    /// minimp3's float state - bytes that may differ between x64 and arm64 -
    /// for state hashes; a load needs the full form.
    static constexpr size_t STATE_SIZE = 17408;
    static constexpr size_t STATE_PCM_MAX = 4096; // int16 values: the low-water mark plus one frame fits
    void saveState(uint8_t* dst, bool machineVisibleOnly = false) const;
    void loadState(const uint8_t* src);

private:
    class SciPort : public SpiDevice
    {
    public:
        explicit SciPort(Vs10xxDecoder& owner) : _owner(owner) {}
        void select(bool selected) override;
        uint8_t exchange(uint8_t mosi) override;
        void truncatedByte() override { _count = 0; }

        uint8_t _op = 0, _addr = 0, _hi = 0;
        int _count = 0;
        bool _selected = false;

    private:
        Vs10xxDecoder& _owner;
    };

    class SdiPort : public SpiDevice
    {
    public:
        explicit SdiPort(Vs10xxDecoder& owner) : _owner(owner) {}
        void select(bool) override {}
        uint8_t exchange(uint8_t mosi) override;

    private:
        Vs10xxDecoder& _owner;
    };

    void writeRegister(uint8_t index, uint16_t value);
    uint16_t readRegister(uint8_t index);
    void softReset();
    void clearStream();
    bool decodeNextFrame();
    int parseFrameLength(size_t at, uint32_t& header) const;
    void updateAudata();

    Chip _chip;
    Level _level;
    double _unitsPerSecond;
    SciPort _sciPort;
    SdiPort _sdiPort;

    std::array<uint16_t, REGISTERS> _regs{};
    bool _running = false;
    int64_t _now = 0;
    int64_t _busyUntil = 0;        // DREQ held low after a reset

    std::deque<uint8_t> _fifo;
    std::vector<int16_t> _pcm;     // interleaved stereo, not yet played
    size_t _pcmRead = 0;           // play cursor into _pcm
    std::unique_ptr<Vs10xxMp3State> _dec;
    uint32_t _rate = 0;
    int _channels = 0;
    int _bitrateKbps = 0;
    double _playFraction = 0;      // stream samples owed to time
    uint64_t _framesDecoded = 0;
    uint64_t _samplesPlayed = 0;
    uint64_t _bytesReceived = 0;
    uint64_t _decodedSamplesSecondBase = 0;

    // Output resampler: stream samples played since the last render, and the
    // last one for interpolation across frame edges
    std::vector<int16_t> _played;
    int16_t _lastL = 0, _lastR = 0;
};
