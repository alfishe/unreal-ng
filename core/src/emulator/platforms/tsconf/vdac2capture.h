#pragma once

/// @file vdac2capture.h
/// @brief Writes the VDAC2 card's FT812 bus traffic as an .evr replay stream:
/// every chip select change and every byte exchanged, each stamped with the
/// FT812 system clock it happened at, plus a record at every FT812 frame end.
/// Replaying the stream drives the eve-emu library alone, with no machine
/// around it: the reference workload for the library's tests and its
/// performance work (vdac2-test-corpus.md §4, format there).
///
/// Turned on by [VDAC2] CaptureFile; a debug tool, off by default.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class Vdac2Capture
{
public:
    /// Record kinds (vdac2-test-corpus.md §4)
    enum Kind : uint8_t
    {
        kSelect = 1,   ///< u8 level (1 = selected)
        kByte = 2,     ///< u8 mosi, u8 miso (the chip's answer)
        kFrame = 3,    ///< u64 completed frames, u8 drawn, u16 width, u16 height, u64 FNV-1a of the ARGB picture
        kPowerOn = 4,  ///< the chip was reset as at power-on
        kEnd = 5,      ///< last record: the clock the capture ends at
        kState = 6,    ///< the chip's whole state: a capture started on a running chip begins with it
    };

    /// The chip's state at the start of a capture on a running chip: the
    /// library's state blob (EveSaveState) and its memory regions
    struct ChipState
    {
        struct Region
        {
            std::string name;
            std::vector<uint8_t> data;
        };
        std::vector<uint8_t> state;
        std::vector<Region> regions;
    };

    /// What a capture wrote so far
    struct Stats
    {
        std::string path;
        uint64_t bytesWritten = 0;
        uint64_t selects = 0;
        uint64_t exchanges = 0;
        uint64_t frames = 0;
        uint64_t startClock = 0;
        uint64_t lastClock = 0;
    };

    static constexpr uint32_t kHeaderSize = 64;
    static constexpr uint32_t kChipModel = 812;

    Vdac2Capture() = default;
    ~Vdac2Capture() { Close(_lastClock); }

    Vdac2Capture(const Vdac2Capture&) = delete;
    Vdac2Capture& operator=(const Vdac2Capture&) = delete;

    /// Start a stream. Without `state` the chip is at power-on at clock 0;
    /// with it, the stream starts at `clock` with the chip's whole state
    bool Open(const std::string& path, uint32_t externalClockHz, const std::vector<uint8_t>& rom,
              const ChipState* state = nullptr, uint64_t clock = 0);
    bool IsOpen() const { return _file != nullptr; }
    /// Counters so far, including what is still buffered
    Stats GetStats() const
    {
        Stats stats = _stats;
        stats.bytesWritten += _buffer.size();
        stats.lastClock = _lastClock;
        return stats;
    }

    void Select(uint64_t clock, bool selected);
    void Byte(uint64_t clock, uint8_t mosi, uint8_t miso);
    void Frame(uint64_t clock, uint64_t frames, bool drawn, uint16_t width, uint16_t height, uint64_t pictureHash);
    void PowerOn(uint64_t clock);
    void Close(uint64_t clock);

    /// FNV-1a 64 of a picture, the hash the frame records carry
    static uint64_t HashPicture(const uint32_t* pixels, size_t count);

private:
    void Record(uint64_t clock, Kind kind);
    void Put8(uint8_t value) { _buffer.push_back(value); }
    void Put16(uint16_t value);
    void Put64(uint64_t value);
    void Flush();

    std::FILE* _file = nullptr;
    std::vector<uint8_t> _buffer;
    uint64_t _lastClock = 0;
    Stats _stats;
};
