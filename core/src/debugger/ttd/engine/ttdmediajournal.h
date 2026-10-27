#pragma once

/// @file ttdmediajournal.h
/// @brief The engine's media read journal (Phase 3, owner decision 2026-10-03):
/// every sector a machine read from a medium's image while recording - time,
/// slot, LBA, bytes - in read order. A replay hands the reads back in the same
/// order; each checkpoint keeps where the journal stood, so a replay from it
/// starts there. A read the recording does not have (another slot or LBA at
/// that point: the execution left the recording) goes to the image and is
/// counted.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ttd
{

class TTDMediaJournal
{
public:
    struct Record
    {
        uint64_t frame = 0;
        uint32_t tInFrame = 0;
        uint16_t slot = 0;     ///< index into SlotNames()
        uint64_t lba = 0;
        uint64_t offset = 0;   ///< of the bytes in the data store
        uint32_t size = 0;
    };

    enum class Mode : uint8_t
    {
        Off,
        Record,
        Play
    };

    void Clear();
    Mode GetMode() const { return _mode; }
    void StartRecording() { _mode = Mode::Record; }
    /// Replay from @p cursor (a checkpoint's) on
    void StartPlayback(uint64_t cursor)
    {
        _cursor = cursor;
        _mode = Mode::Play;
    }
    void Stop() { _mode = Mode::Off; }

    void Append(uint64_t frame, uint32_t tInFrame, const std::string& slot, uint64_t lba, const uint8_t* bytes,
                size_t size);
    /// Playing: the next read is @p slot / @p lba -> its bytes into @p out, true.
    /// Anything else: false (a divergence, counted)
    bool PlayNext(const std::string& slot, uint64_t lba, uint8_t* out, size_t size);

    /// Keep the first @p count records (a recording resumed from the past)
    void TruncateTo(uint64_t count);
    /// The number of records at or before frame @p frame, T-state @p tInFrame
    uint64_t CountUpTo(uint64_t frame, uint32_t tInFrame) const;

    uint64_t Size() const { return _records.size(); }
    uint64_t Cursor() const { return _cursor; }
    const Record& At(uint64_t index) const { return _records[index]; }
    const uint8_t* Bytes(const Record& r) const { return _data.data() + r.offset; }
    const std::vector<std::string>& SlotNames() const { return _slots; }
    uint64_t Divergences() const { return _divergences; }
    size_t HeapBytes() const;

private:
    uint16_t SlotIndex(const std::string& slot);

    Mode _mode = Mode::Off;
    uint64_t _cursor = 0;
    uint64_t _divergences = 0;
    std::vector<Record> _records;
    std::vector<uint8_t> _data;
    std::vector<std::string> _slots;
};

}  // namespace ttd
