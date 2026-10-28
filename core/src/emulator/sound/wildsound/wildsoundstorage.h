#pragma once

/// @file wildsoundstorage.h
/// @brief The Wild Sound card's flash store: what the stream commands upload (the track pack, the instrument table,
/// the samples), with the card's limits (docs/inprogress/2026-10-09-wildsound/design.md §4.3, reference-firmware.md
/// §B.1, §C).
///
/// The card keeps uploads in the STM32's flash after its firmware: 999 424 bytes, handed out by a bump allocator
/// with 64 descriptors. The track takes one, the instrument table one, the blank instrument 0 one, every instrument
/// one: 61 instruments at most. Each instrument costs 16 + 8 + Length + 24 bytes, as on the card (its info block and
/// the zero runs its mixer needs); the sample bytes sit at +24 of that block here.
///
/// Worked example (PROSIAK): XMAST SIZE 19107 allocates the track at 0; XMBST SIZE 441 (21 records) allocates the
/// table (441 + 21 bytes, the card's blank record 0 included) and, when the table closes, 21 sample blocks of
/// Length + 48 bytes; XMBDO bytes 0..3309 go to instrument 1 (its record's OFS 441 minus the table size 441 = 0).

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wildsound
{

/// One XM pattern of the track pack: where its packed rows are (offset into the pack) and how many rows it has
struct WildSoundPattern
{
    uint32_t offset = 0;  ///< into the pack (header included)
    uint32_t size = 0;    ///< bytes of packed rows (0 = empty pattern)
    uint16_t rows = 0;
};

/// The parsed track pack header (reference-firmware.md §C.1)
struct WildSoundTrack
{
    bool valid = false;
    std::string name;            ///< up to 20 characters
    uint16_t channels = 0;       ///< 1..32
    uint16_t speed = 0;          ///< initial ticks per row
    uint16_t bpm = 0;            ///< initial BPM
    bool linearFrequencies = false;  ///< XM flag bit 0
    uint16_t songLength = 0;     ///< order entries used
    uint16_t restart = 0;        ///< order position the song loops to
    std::array<uint8_t, 256> order{};
    std::vector<WildSoundPattern> patterns;
};

/// One instrument (sample 0 of an XM instrument, reference-firmware.md §C.3)
struct WildSoundInstrument
{
    uint32_t streamOffset = 0;  ///< where its bytes start in the XMBDO sample stream
    uint32_t length = 0;
    uint32_t loopStart = 0;
    uint32_t loopEnd = 0;       ///< absolute
    uint8_t volume = 0;         ///< 0..64
    int8_t fineTune = 0;
    uint8_t type = 0;           ///< raw XM sample type byte; loop = type & 3 (0 none, 1 forward, 2 ping-pong)
    uint8_t panning = 0x80;
    int8_t relativeNote = 0;
    uint32_t dataOffset = 0;    ///< flash offset of its sample bytes (0 = none)
    uint32_t received = 0;      ///< sample bytes stored so far

    uint8_t LoopType() const { return static_cast<uint8_t>(type & 3); }
};

class WildSoundStorage
{
public:
    static constexpr uint32_t kFlashSize = 999424;  ///< #0800C000..#08100000 of the STM32F405
    static constexpr uint32_t kDescriptors = 64;
    static constexpr uint32_t kRecordSize = 21;
    static constexpr uint32_t kHeaderSize = 292;     ///< track pack header
    static constexpr uint32_t kPatternRecordSize = 10;
    static constexpr uint32_t kInstrumentOverhead = 48;  ///< 16 info + 8 + 24 zero bytes around the samples
    static constexpr uint32_t kSampleDataOffset = 24;    ///< sample bytes inside an instrument block
    static constexpr uint32_t kMaxChannels = 32;

    WildSoundStorage();

    /// ERASE / RESET: every descriptor free; the flash bytes stay as they are (the card erases on ERASE only, and an
    /// emulator does not need the erased value: nothing reads a freed area)
    void Clear();

    /// XMAST: room for a pack of `size` bytes; false (with Error()) when the store or the descriptors are full
    bool BeginTrack(uint32_t size);
    /// Track bytes in stream order; bytes past the allocation are dropped
    void AppendTrack(const uint8_t* data, size_t count);
    /// The closing empty packet: parse and validate the pack
    bool EndTrack();

    /// XMBST: the instrument table of `size` bytes
    bool BeginInstruments(uint32_t size);
    void AppendInstruments(const uint8_t* data, size_t count);
    /// The closing packet: parse the records, allocate every sample block
    bool EndInstruments();

    /// XMBDO: one sample-stream byte at the global sample position (survives across XMBDO commands)
    void PutSampleByte(uint8_t value);
    uint32_t SamplePosition() const { return _samplePos; }

    /// region <Read access>
    const WildSoundTrack& Track() const { return _track; }
    const std::vector<WildSoundInstrument>& Instruments() const { return _instruments; }  ///< index 0 = instrument 1
    /// The pack's bytes (header included); null when no pack is stored
    const uint8_t* TrackData() const;
    uint32_t TrackSize() const { return _trackSize; }
    uint32_t TrackReceived() const { return _trackReceived; }
    /// An instrument's sample bytes as uploaded (unsigned, #80 = silence)
    const uint8_t* SampleData(const WildSoundInstrument& ins) const;
    bool InstrumentsComplete() const;
    /// True when DIGPLAY may start: a valid track and an instrument table
    bool Playable() const { return _track.valid && _instrumentsParsed; }
    /// endregion </Read access>

    uint32_t Used() const { return _used; }
    uint32_t DescriptorsUsed() const { return _descriptors; }
    const std::string& Error() const { return _error; }
    void ClearError() { _error.clear(); }

    /// The whole flash (TTD region, memory_access)
    std::vector<uint8_t>& Flash() { return _flash; }
    const std::vector<uint8_t>& Flash() const { return _flash; }
    /// Called with (offset, length) for every write into the flash (the TTD region's dirty pieces)
    void SetWriteHook(std::function<void(size_t, size_t)> hook) { _writeHook = std::move(hook); }
    /// Rebuild the parsed track and instruments from the flash (after the flash was restored)
    void Reparse();

    /// region <State>
    /// Allocation state without the flash bytes and the parsed tables (they are derived from the flash on load)
    static constexpr size_t kStateSize = 64;
    void SaveState(uint8_t* dst) const;
    void LoadState(const uint8_t* src);
    /// endregion </State>

private:
    bool Allocate(uint32_t size, uint32_t& offset);
    bool ParseTrack();
    bool ParseInstruments();

    std::vector<uint8_t> _flash;
    uint32_t _used = 0;
    uint32_t _descriptors = 0;

    // The track pack
    uint32_t _trackOffset = 0;
    uint32_t _trackSize = 0;
    uint32_t _trackReceived = 0;
    bool _trackAllocated = false;
    bool _trackEnded = false;
    WildSoundTrack _track;

    // The instrument table
    uint32_t _tableOffset = 0;
    uint32_t _tableSize = 0;
    uint32_t _tableReceived = 0;
    bool _tableAllocated = false;
    bool _instrumentsParsed = false;
    uint32_t _instrumentsBase = 0;  ///< flash offset of instrument 1's block
    std::vector<WildSoundInstrument> _instruments;

    uint32_t _samplePos = 0;
    std::string _error;
    std::function<void(size_t, size_t)> _writeHook;
};

}  // namespace wildsound
