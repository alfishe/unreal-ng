#pragma once

/// @file ttdportjournal.h
/// @brief Port journals: every IN result and every OUT of the main CPU while a
/// session was recorded, in execution order (ttd-port-read-journal.md).
///
/// Reads (IN): the outside world reaches a classic machine's CPU only through
/// IN (tape, disks, keyboard, mouse, RTC, sound cards...). With every IN result
/// recorded, a replay feeds the CPU the recorded values instead of the live
/// device answers, so it no longer depends on media files, host devices or
/// anything else outside the session.
///
/// Writes (OUT): the machine's output, logged as facts. A replay checks every
/// OUT against the recording (a changed output means the execution diverged).
///
/// Both answer "when" questions without replaying anything: every record
/// carries the machine time of the access and the PC of the instruction, so
/// "when did the program first see key A", "when did it write AY register 7"
/// are a scan of the journal (ttdportsearch.h).
///
/// A checkpoint carries each journal's position at its capture
/// (TTDCheckpoint::portReadCursor / portWriteCursor); a replay from it starts
/// there.
///
/// Storage: blocks of kBlockRecords records, zstd-compressed and CRC32C-checked
/// once full; the newest block stays raw. A block's raw layout is five columns
/// - ports (u16), values (u8), PCs (u16), frame deltas (u32), T-states (u32:
/// absolute when the frame changed, else the delta from the previous record) -
/// so a polling loop, which repeats the same port, PC and T-state step,
/// compresses to almost nothing.
///
/// Not thread-safe: used on the emulation thread only, like the rest of a
/// recording's state.

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "ttdcheckpoint.h"

namespace ttd
{

/// One IN or OUT of the main CPU
struct TTDPortRecord
{
    uint64_t frame = 0;     ///< frame counter at the access
    uint32_t tInFrame = 0;  ///< TTD time in the frame (T-states at the model's top clock)
    uint16_t port = 0;
    uint16_t pc = 0;        ///< PC of the IN / OUT instruction (m1_pc)
    uint8_t value = 0;

    TTDTimePoint Time() const { return TTDTimePoint{frame, tInFrame}; }
    bool SameAccess(const TTDPortRecord& o) const
    {
        return frame == o.frame && tInFrame == o.tInFrame && port == o.port && pc == o.pc;
    }
};

class TTDPortJournal
{
public:
    enum class Direction : uint8_t
    {
        Read,   ///< IN results
        Write   ///< OUT values
    };

    /// Records per sealed block
    static constexpr uint32_t kBlockRecords = 32768;
    /// Raw bytes per record in a block (the five columns)
    static constexpr uint32_t kRawRecordBytes = 2 + 1 + 2 + 4 + 4;

    /// Record: append every access. Play: replay against the records (reads
    /// get the recorded value). Off: accesses pass untouched
    enum class Mode : uint8_t
    {
        Off,
        Record,
        Play
    };

    /// A replayed access that differed from the recording
    struct Mismatch
    {
        uint64_t index = 0;
        TTDPortRecord recorded;
        TTDPortRecord live;
    };

    explicit TTDPortJournal(Direction direction = Direction::Read) : _direction(direction) {}

    Direction GetDirection() const { return _direction; }

    /// Drop every record and the replay statistics; mode Off
    void Clear();

    Mode GetMode() const { return _mode; }
    uint64_t Cursor() const { return _cursor; }

    /// Start recording at the end of the journal
    void StartRecording();
    /// Replay from `cursor` on. A cursor at or past the end leaves the journal
    /// Off (nothing recorded to replay)
    void StartPlayback(uint64_t cursor);
    /// Stop recording or playback; accesses pass through
    void Stop();
    /// Restore a mode and position saved with GetMode()/Cursor() (live-state
    /// snapshots around a throwaway replay)
    void RestorePosition(Mode mode, uint64_t cursor);

    /// The CPU's IN hook (Z80::in): returns the value the CPU gets
    uint8_t OnRead(uint16_t port, uint8_t liveValue, uint64_t frame, uint32_t tInFrame, uint16_t pc)
    {
        if (_mode == Mode::Record)
        {
            Append(TTDPortRecord{frame, tInFrame, port, pc, liveValue});
            return liveValue;
        }
        if (_mode == Mode::Play)
            return PlayNext(TTDPortRecord{frame, tInFrame, port, pc, liveValue});
        return liveValue;
    }

    /// The CPU's OUT hook (Z80::out): recorded, or checked against the record
    void OnWrite(uint16_t port, uint8_t value, uint64_t frame, uint32_t tInFrame, uint16_t pc)
    {
        if (_mode == Mode::Record)
            Append(TTDPortRecord{frame, tInFrame, port, pc, value});
        else if (_mode == Mode::Play)
            PlayNext(TTDPortRecord{frame, tInFrame, port, pc, value});
    }

    /// Records in the journal
    uint64_t Size() const { return _sealedRecords + _open.size(); }

    /// One decoded block. A reader on another thread (a search while the
    /// emulation thread replays) brings its own, so it never touches the cache
    /// playback uses
    struct ReadCache
    {
        int64_t block = -1;
        std::vector<TTDPortRecord> records;
    };

    /// Record at `index`; false past the end or on a damaged block. Sequential
    /// access is cheap (one decoded block is cached)
    bool Get(uint64_t index, TTDPortRecord& out, ReadCache& cache) const;
    bool Get(uint64_t index, TTDPortRecord& out) const { return Get(index, out, _cache); }

    /// Index of the first record at or after `time` (Size() when none)
    uint64_t LowerBound(const TTDTimePoint& time, ReadCache& cache) const;
    uint64_t LowerBound(const TTDTimePoint& time) const { return LowerBound(time, _cache); }

    /// Keep the first `count` records. Recording continues after them
    void TruncateTo(uint64_t count);

    /// Index of the first record still held: the history limit drops whole
    /// sealed blocks from the front (DropBefore); indices stay absolute, so the
    /// cursors of the remaining checkpoints are unchanged
    uint64_t FirstIndex() const { return _droppedBlocks * kBlockRecords; }
    /// Drop the sealed blocks whose records all lie before `index` (the oldest
    /// checkpoint's cursor). Records at and after it are kept
    void DropBefore(uint64_t index);

    /// Replay statistics since the last Clear / ResetStatistics.
    /// ValueMismatches: reads the live device answered differently (the CPU got
    /// the recorded value). Divergences: accesses at another time, from another
    /// instruction, to another port - or an OUT of another value: the execution
    /// itself left the recording
    uint64_t ValueMismatches() const { return _valueMismatches; }
    uint64_t Divergences() const { return _divergences; }
    bool HasMismatch() const { return _hasFirstMismatch; }
    const Mismatch& FirstMismatch() const { return _firstMismatch; }
    bool HasDivergence() const { return _hasFirstDivergence; }
    const Mismatch& FirstDivergence() const { return _firstDivergence; }
    void ResetStatistics();

    /// Heap held by the records (compressed blocks + the raw open block)
    size_t HeapBytes() const;
    /// Part of HeapBytes() allocated but not holding data: unused capacity of
    /// the compressed blocks
    size_t CompressedSlackBytes() const;
    /// Bytes the records take in a .ttd file (the open block compressed)
    size_t SerializedBytes() const;

    /// Section body (ttd.ksy port journal). Cursors: one per checkpoint, in
    /// timeline order, relative to FirstIndex() (a file starts at its first record)
    bool Serialize(std::ostream& out, const std::vector<uint64_t>& cursors, std::string& err) const;
    /// Reads into `this` (cleared first) and `cursors`; every block is
    /// decompressed and CRC-checked and the records must be in time order.
    /// On failure `this` is left cleared
    bool Deserialize(std::istream& in, uint32_t checkpointCount, std::vector<uint64_t>& cursors, std::string& err);

    /// Test hook: the number of sealed blocks
    size_t SealedBlockCount() const { return _blocks.size(); }

private:
    struct Block
    {
        uint32_t records = 0;
        uint64_t baseFrame = 0;   ///< frame of the block's first record
        uint64_t lastFrame = 0;   ///< frame of its last record (not stored; recomputed on load)
        uint32_t crc = 0;         ///< CRC32C of the raw layout
        std::vector<uint8_t> compressed;
    };

    void Append(const TTDPortRecord& record)
    {
        _open.push_back(record);
        if (_open.size() == kBlockRecords)
            SealOpenBlock();
    }
    uint8_t PlayNext(const TTDPortRecord& live);
    void SealOpenBlock();
    static std::vector<uint8_t> RawLayout(const std::vector<TTDPortRecord>& records);
    static bool DecodeRaw(const uint8_t* raw, uint32_t records, uint64_t baseFrame, std::vector<TTDPortRecord>& out);
    static Block MakeBlock(const std::vector<TTDPortRecord>& records);
    /// `block`: absolute block number (dropped blocks counted)
    bool DecodeBlock(uint64_t block, ReadCache& cache) const;

    Direction _direction;
    Mode _mode = Mode::Off;
    uint64_t _cursor = 0;  ///< next record replayed (Play)

    std::vector<Block> _blocks;      ///< the sealed blocks still held, oldest first
    uint64_t _droppedBlocks = 0;     ///< sealed blocks dropped from the front (DropBefore)
    uint64_t _sealedRecords = 0;     ///< records in sealed blocks, dropped ones included (absolute)
    std::vector<TTDPortRecord> _open;

    // Decoded copy of one sealed block (playback reads sequentially)
    mutable ReadCache _cache;

    uint64_t _valueMismatches = 0;
    uint64_t _divergences = 0;
    bool _hasFirstMismatch = false;
    Mismatch _firstMismatch;
    bool _hasFirstDivergence = false;
    Mismatch _firstDivergence;
};

}  // namespace ttd
