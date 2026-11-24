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

#include <algorithm>
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

    /// One port's accesses inside a run of records (Tally): how many, the first and the last value, and how often bit 4
    /// (the ULA's beeper / MIC bit of #FE) changed from one of them to the next
    struct PortTally
    {
        uint16_t port = 0;
        uint8_t first = 0;
        uint8_t last = 0;
        uint32_t count = 0;
        uint32_t bit4Toggles = 0;
    };
    /// The records [from, to) as per-port tallies, in record order: a sealed block's summary gives a run of records
    /// (a frame's, or the part of it in the block) as one call with its ports in the order they first appear; elsewhere
    /// (the open block, a range that cuts a run, a block without a summary) each record is a call of its own with
    /// count 1. @p fn(const PortTally* tallies, size_t n) returns false to have a run visited record by record instead
    /// (a caller that needs the order of two ports inside it). The timeline counts a long session's accesses without
    /// decoding them (a polling loop reads #FE thousands of times a frame; P1.S10 seek tails, 2026-11-22). False when a
    /// block does not decode
    template <typename Fn>
    bool Tally(uint64_t from, uint64_t to, ReadCache& cache, Fn&& fn) const;
    /// Blocks Tally decoded since the journal was made (tests)
    uint64_t BlocksDecoded() const { return _blocksDecoded; }

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
    /// A sealed block's tallies (built at seal and at load, not stored in files): a run starts where the record frame
    /// changes. Left empty when it would not be much smaller than the records (few accesses a frame to many ports)
    struct Summary
    {
        std::vector<uint32_t> runStart;     ///< per run: its first record in the block; then the block's size
        std::vector<uint32_t> runTallies;   ///< per run: its first tally; then the tallies' count
        std::vector<PortTally> tallies;
        bool Empty() const { return runStart.empty(); }
    };
    struct Block
    {
        uint32_t records = 0;
        uint64_t baseFrame = 0;   ///< frame of the block's first record
        uint64_t lastFrame = 0;   ///< frame of its last record (not stored; recomputed on load)
        uint32_t crc = 0;         ///< CRC32C of the raw layout
        std::vector<uint8_t> compressed;
        Summary summary;
    };
    static Summary Summarize(const std::vector<TTDPortRecord>& records);

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
    /// The sealed blocks' sizes, summed as blocks come and go (CountBlock): the session's status asks for the three
    /// of them at every seek's publish, and each walked every block (P1.S10 seek format)
    size_t _blocksHeap = 0;          ///< compressed and summary capacities
    size_t _blocksSlack = 0;         ///< compressed capacity past its size
    size_t _blocksSerialized = 0;    ///< compressed sizes and block headers, as SerializedBytes counts them
    void CountBlock(const Block& b, bool add);
    uint64_t _droppedBlocks = 0;     ///< sealed blocks dropped from the front (DropBefore)
    uint64_t _sealedRecords = 0;     ///< records in sealed blocks, dropped ones included (absolute)
    std::vector<TTDPortRecord> _open;
    /// Counts the open block's edits other than an append (a seal, a cut, a clear): with its size it names the
    /// block's records, so SerializedBytes compresses the open block once for a session publish after another
    uint64_t _openEdits = 0;
    mutable uint64_t _openBytesEdits = UINT64_MAX;
    mutable size_t _openBytesRecords = 0;
    mutable size_t _openBytes = 0;

    // Decoded copy of one sealed block (playback reads sequentially)
    mutable ReadCache _cache;
    mutable uint64_t _blocksDecoded = 0;

    uint64_t _valueMismatches = 0;
    uint64_t _divergences = 0;
    bool _hasFirstMismatch = false;
    Mismatch _firstMismatch;
    bool _hasFirstDivergence = false;
    Mismatch _firstDivergence;
};

template <typename Fn>
bool TTDPortJournal::Tally(uint64_t from, uint64_t to, ReadCache& cache, Fn&& fn) const
{
    from = std::max(from, FirstIndex());
    to = std::min(to, Size());
    TTDPortRecord rec;
    auto one = [&](uint64_t k) {
        if (!Get(k, rec, cache))
            return false;
        const PortTally t{rec.port, rec.value, rec.value, 1, 0};
        fn(&t, size_t{1});
        return true;
    };
    while (from < to)
    {
        if (from >= _sealedRecords)
        {
            // The open block: its records are at hand
            for (; from < to; ++from)
            {
                const TTDPortRecord& r = _open[static_cast<size_t>(from - _sealedRecords)];
                const PortTally t{r.port, r.value, r.value, 1, 0};
                fn(&t, size_t{1});
            }
            break;
        }
        const uint64_t blockNumber = from / kBlockRecords;
        const uint64_t blockStart = blockNumber * kBlockRecords;
        const uint64_t blockEnd = std::min<uint64_t>(to, blockStart + kBlockRecords);
        const Summary& summary = _blocks[static_cast<size_t>(blockNumber - _droppedBlocks)].summary;
        uint32_t at = static_cast<uint32_t>(from - blockStart);
        const uint32_t end = static_cast<uint32_t>(blockEnd - blockStart);
        if (!summary.Empty())
        {
            // The runs [at, end) holds whole; a run it cuts record by record
            const std::vector<uint32_t>& starts = summary.runStart;
            size_t run = static_cast<size_t>(std::upper_bound(starts.begin(), starts.end(), at) - starts.begin()) - 1;
            while (at < end)
            {
                const uint32_t runEnd = starts[run + 1];
                if (starts[run] == at && runEnd <= end)
                {
                    const uint32_t t0 = summary.runTallies[run];
                    const uint32_t t1 = summary.runTallies[run + 1];
                    if (!fn(summary.tallies.data() + t0, static_cast<size_t>(t1 - t0)))
                        for (uint32_t k = at; k < runEnd; ++k)
                            if (!one(blockStart + k))
                                return false;
                }
                else
                    for (uint32_t k = at, stop = std::min(runEnd, end); k < stop; ++k)
                        if (!one(blockStart + k))
                            return false;
                at = std::min(runEnd, end);
                ++run;
            }
        }
        else
            for (; at < end; ++at)
                if (!one(blockStart + at))
                    return false;
        from = blockEnd;
    }
    return true;
}

}  // namespace ttd
