#pragma once

/// @file ttdsessionfile.h
/// @brief A TimeTravelEngine session in the session container (Phase 4,
/// Step 2; design: phase-4-session-file-tdd.md §5.2). Save writes the
/// session's parts; Load rebuilds a session from them, read-only.
///
/// What goes where:
///
///   header tables  the memory regions, the device table, the snapshot interval
///   stream 1       piece versions in the order they appeared: encoding, base
///                  (by its number in the file), depth, CRC, payload as stored
///   stream 3       checkpoints: frame, start, CPU, chipset, journal cursors and,
///                  per region, the pieces taking the next versions
///   stream 5       events with their payloads
///   stream 6       configuration entries and media-version changes
///   stream 7       the write journal and its segments (ancillary, D40)
///   streams 13-16  the bus journals (IN, OUT, interrupt vectors) and the
///                  sector reads
///
/// Versions are numbered in the file as they appear: the store reuses its
/// ids, the file does not. Reference tables are not stored: Load rebuilds them
/// the way CaptureFrame builds them. A part depends on the parts holding the
/// current version of every piece at its end, and the bases of its own
/// versions, so a damaged part makes exactly the frames that need it
/// unreachable.
///
/// Load reads the parts in order and stops before the first unreachable one;
/// the report says how many frames came in and why it stopped. The engine's
/// own state for continuing a capture (delta base, time lines) is not in the
/// file: a loaded session is read-only.
///
/// Worked example: a 300-frame session saved with 50 checkpoints per part
/// gives six parts. Damage in part 3's piece record: Load brings frames of
/// parts 0-2 back (150 frames) and reports part 3 as the reason it stopped.

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "debugger/ttd/engine/ttdcontainer.h"
#include "debugger/ttd/engine/ttddevicetable.h"
#include "debugger/ttd/engine/ttdregion.h"

namespace ttd
{

class TimeTravelEngine;

/// Part-end extra, bit 0: a file starts here (the loader restarts the
/// numbering of versions and the journal positions)
constexpr uint8_t kPartFileStart = 1;

namespace sessionstream
{
constexpr uint16_t kPieces = 1;
constexpr uint16_t kCheckpoints = 3;
constexpr uint16_t kEvents = 5;
constexpr uint16_t kConfiguration = 6;
constexpr uint16_t kWriteJournal = 7;
constexpr uint16_t kBusReads = 13;
constexpr uint16_t kBusWrites = 14;
constexpr uint16_t kBusVectors = 15;
constexpr uint16_t kMediaReads = 16;
/// Frame-boundary streams (D19): 0x0100 + the engine's stream id (0..63); ancillary
constexpr uint16_t kFrameStreamFirst = 0x0100;
constexpr uint16_t kFrameStreamLast = 0x013F;
/// A full copy at least every this many frames: reading one decodes at most this many differences
constexpr uint32_t kFrameStreamFullEvery = 50;
/// The holder's own data (TTDHolderStream): ancillary, opaque to the engine.
/// The stream table (phase-4-session-file-tdd.md §5.3) names 8-10 coverage
/// and 12 bookmarks; 17-0xFF are free for the holder. 0x0200-0x02FF stay
/// reserved for branches, 0x0300-0x03FF for groups
constexpr uint16_t kCoverage = 8;
constexpr uint16_t kBookmarks = 12;
constexpr uint16_t kHolderFirstFree = 17;
inline bool IsHolderStream(uint16_t id)
{
    return (id >= 8 && id <= 12) || (id >= kHolderFirstFree && id <= 0x00FF);
}
/// Branches (docs/inprogress/2026-11-08-ttd-branches/storage.md §3): the branch table, then each trunk stream's
/// twin for a branch's records (0x0200 + id for ids 1..0x3F, 0x0240 + the engine id for frame streams). All
/// ancillary: a reader without branches skips them and loads the trunk
constexpr uint16_t kBranchTable = 0x0200;
constexpr uint16_t kBranchFirst = 0x0201;
constexpr uint16_t kBranchLast = 0x023F;
constexpr uint16_t kBranchFrameFirst = 0x0240;
constexpr uint16_t kBranchFrameLast = 0x027F;
/// A trunk stream's id -> its branch twin (0 when it has none)
inline uint16_t BranchTwin(uint16_t id)
{
    if (id >= kFrameStreamFirst && id <= kFrameStreamLast)
        return static_cast<uint16_t>(kBranchFrameFirst + (id - kFrameStreamFirst));
    return id >= 1 && id <= 0x3F ? static_cast<uint16_t>(kBranchTable + id) : 0;
}
/// A branch twin's id -> the trunk stream it mirrors (0 when @p id is not a twin)
inline uint16_t TrunkOfTwin(uint16_t id)
{
    if (id >= kBranchFrameFirst && id <= kBranchFrameLast)
        return static_cast<uint16_t>(kFrameStreamFirst + (id - kBranchFrameFirst));
    return id >= kBranchFirst && id <= kBranchLast ? static_cast<uint16_t>(id - kBranchTable) : 0;
}
}  // namespace sessionstream

/// Data the engine's holder keeps with a session (the controller's coverage
/// index, bookmarks, its own facts): an ancillary stream written whole with
/// the file's last part and handed back by Load as it was
struct TTDHolderStream
{
    uint16_t id = sessionstream::kHolderFirstFree;   ///< sessionstream::IsHolderStream
    std::string name;
    std::vector<uint8_t> bytes;
};

/// Header flags of a session file
constexpr uint16_t kSessionConvertedFromV1 = 1;   ///< converted from a schema-1 (v1) file (D31): what v1 lacked is absent

struct TTDSessionSaveParams
{
    uint16_t headerFlags = 0;
    uint32_t checkpointsPerPart = 50;   ///< about a second of recording
    uint64_t createdMicros = 0;         ///< the header's creation time (tests fix it)
    std::array<uint8_t, 16> uuid{};     ///< the session's identity
    /// Declared in the header; their bytes go with the session's last part
    /// (a writer recording as it goes takes them at Finish: SetHolderStreams)
    std::vector<TTDHolderStream> holderStreams;
    /// Save: every live line (branches); false writes one lineage as a linear file (storage.md §5, "save
    /// lineage"): the trunk up to the first fork, each branch up to the next, all on trunk streams
    bool allLines = true;
    /// allLines false: the line the lineage ends at (engine line id); UINT32_MAX the active line
    uint32_t lineageOf = UINT32_MAX;
    /// A recording folder's file of one line (storage.md §4): the active line's engine id, which is also its id in
    /// the file. A branch's file declares the branch streams; its first file starts with the branch's creation and
    /// the fork's copy whole (every piece), so the file needs no other. UINT32_MAX: not a folder file
    uint32_t fileLine = UINT32_MAX;
    /// Per branch line (engine line id): the holder's record of the branch (identity, metadata), kept with the
    /// branch's creation in the branch table, and its own holder streams (ids <= 0x3F), with its last part
    std::map<uint32_t, std::vector<uint8_t>> lineMeta;
    std::map<uint32_t, std::vector<TTDHolderStream>> lineHolderStreams;
};

struct TTDSessionLoadReport
{
    size_t checkpoints = 0;      ///< loaded
    size_t partsLoaded = 0;
    size_t partsInFile = 0;
    bool complete = false;       ///< every part came in
    bool convertedFromV1 = false;   ///< kSessionConvertedFromV1: no fingerprint, media versions or device regions v1 lacked
    std::string stoppedAt;       ///< why the load stopped early
    std::vector<std::string> notes;   ///< the container's notes (scan, skipped streams)
    std::map<uint16_t, std::vector<uint8_t>> holderStreams;   ///< by id, as saved (absent: none, or not reached)
    /// Branches: the lines loaded (the trunk is line 0, branches in file order 1..), per line the holder's record
    /// and its holder streams (by the trunk's ids)
    size_t lines = 1;
    std::map<uint32_t, std::vector<uint8_t>> lineMeta;
    std::map<uint32_t, std::map<uint16_t, std::vector<uint8_t>>> lineHolderStreams;
};

/// How far the writer thread may fall behind (TTDSessionWriter)
struct TTDSessionWriterLimits
{
    uint64_t lagSoftBytes = 64ull << 20;    ///< above: reported as "behind"
    uint64_t lagHardBytes = 512ull << 20;   ///< above: no more parts are taken
};

/// Writes a session as it records (Phase 4, §5.2.4). The thread that owns the
/// engine calls Collect() after its captures: every part that is complete
/// (its next part's first checkpoint exists) is laid out there, quickly, and
/// handed to the writer thread, which compresses, checks, appends and makes
/// each part durable. Capture never waits for the disk: when the queued bytes
/// pass the hard limit the writer stops taking parts and reports why; the file
/// stays valid up to its last part. Finish() writes the rest and the index.
///
/// Worked example: 50 checkpoints per part; after capture 51 Collect() queues
/// part 0 (checkpoints 0-49); the writer thread appends its records and its
/// part end and syncs. At stop, Finish() queues checkpoints 50-... with the
/// write journal, then the index and the trailer, and waits for the thread.
/// The branch table's create record (storage.md §3.1): {U8 1, local, parent local, fork frame, fork T, Varint size,
/// the holder's record}
std::vector<uint8_t> TTDBranchCreateRecord(uint32_t local, uint32_t parentLocal, uint64_t forkFrame, uint32_t forkT,
                                           const std::vector<uint8_t>& meta);

class TTDSessionWriter
{
public:
    using Limits = TTDSessionWriterLimits;

    /// @p background: a writer thread; false writes on the caller's thread (Save, tests)
    explicit TTDSessionWriter(bool background = true) : _background(background) {}
    ~TTDSessionWriter();
    TTDSessionWriter(const TTDSessionWriter&) = delete;
    TTDSessionWriter& operator=(const TTDSessionWriter&) = delete;

    /// Write the header (on this thread) and start the writer
    /// @p first: the file's first checkpoint (default: the first one held)
    bool Begin(const TimeTravelEngine& engine, ITTDByteSink& sink, const TTDSessionSaveParams& params,
               std::string& error, const Limits& limits = {}, size_t first = SIZE_MAX);
    /// Queue every complete part; false once writing failed or fell too far behind (Error() says why).
    /// The frame-boundary stream copies of the queued parts leave the engine's memory
    bool Collect(TimeTravelEngine& engine);
    /// Queue the checkpoints up to @p end (all by default; a segment's file
    /// ends at the next segment's baseline), the write journal with the
    /// session's last part, write the index, wait for the writer
    bool Finish(const TimeTravelEngine& engine, size_t end = SIZE_MAX);
    /// The holder streams' bytes for the last part (ids declared at Begin)
    void SetHolderStreams(std::vector<TTDHolderStream> streams) { _params.holderStreams = std::move(streams); }
    /// Save with branches: every live line one after another (the trunk first, as a file without branches), each
    /// branch in its streams' twins, then the index. Instead of Finish; the engine's active line is kept
    bool WriteAllLines(TimeTravelEngine& e);
    /// Write the lineage ending at line @p tip as one linear file (TTDSessionSaveParams::lineageOf), after Begin
    bool WriteLineage(TimeTravelEngine& e, uint32_t tip);

    bool Failed() const { return _failed.load(); }
    std::string Error() const;
    uint64_t QueuedBytes() const { return _queuedBytes.load(); }
    bool Behind() const { return QueuedBytes() > _limits.lagSoftBytes; }
    uint32_t PartsQueued() const { return _part; }
    size_t CheckpointsWritten() const { return _next; }

private:
    struct PartJob
    {
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> records;
        TTDPartEnd end;
        bool finalize = false;
        uint64_t bytes = 0;
    };
    bool BuildPart(const TimeTravelEngine& e, size_t first, size_t last, bool final, PartJob& job);
    /// The stream a record of the line being written goes to
    uint16_t StreamOf(uint16_t trunkId) const { return _local == 0 ? trunkId : sessionstream::BranchTwin(trunkId); }
    bool CollectParts(const TimeTravelEngine& e);
    /// The branch table's create record of @p line (engine id): {U8 1, local, parent local, fork frame, fork T, meta}
    std::vector<uint8_t> CreateRecord(uint32_t local, uint32_t parentLocal, uint64_t forkFrame, uint32_t forkT,
                                      uint32_t line) const;
    void Queue(PartJob&& job);
    void Write(PartJob& job);
    void Run();
    void Fail(const std::string& why);

    bool _background;
    Limits _limits;
    ITTDByteSink* _sink = nullptr;
    TTDContainerWriter _writer;
    TTDSessionSaveParams _params;

    // The layout's state (the engine's thread)
    std::unordered_map<uint32_t, uint32_t> _itemOf;   ///< store id -> number in the file
    std::vector<uint32_t> _itemPart;                  ///< number -> part
    std::vector<std::vector<uint32_t>> _liveItem;     ///< region, piece -> current number
    size_t _next = 0;                                 ///< the first checkpoint not laid out yet
    uint32_t _part = 0;
    size_t _nextConfig = 0;
    bool _finished = false;
    size_t _end = SIZE_MAX;   ///< the file's last checkpoint + 1 (known at Finish)
    uint64_t _base[4] = {};   ///< the journals' positions at the file's start: IN, OUT, sectors, vectors
    std::string _buildError;   ///< the engine's thread only
    std::map<uint32_t, std::vector<uint8_t>> _streamPrevious;   ///< frame streams: the last copy written
    std::map<uint32_t, uint32_t> _streamSinceFull;
    // Branches (WriteAllLines): the line being written, its number in the file, and each written line's layout
    struct LineLayout
    {
        uint32_t local = 0;
        uint32_t parent = 0xFFFFFFFFu;   ///< engine line id
        std::unordered_map<uint32_t, uint32_t> itemOf;
        std::vector<uint32_t> itemPart;
        std::vector<std::pair<size_t, uint32_t>> parts;   ///< (first checkpoint, part index), in order
    };
    std::map<uint32_t, LineLayout> _layouts;   ///< by engine line id
    uint32_t _lineId = 0;
    uint32_t _local = 0;
    uint32_t _linePart = 0;   ///< parts written for the line so far
    bool _lineage = false;    ///< WriteLineage: every line on trunk streams, no write journal (derived, per line)
    /// A version's number in the file: on the line being written, else on its nearest ancestor that has it
    bool FindItem(uint32_t storeId, uint32_t& local, uint32_t& item, uint32_t& part) const;

    // The writer thread
    std::thread _thread;
    mutable std::mutex _mutex;
    std::condition_variable _wake;
    std::condition_variable _drained;
    std::deque<PartJob> _queue;
    bool _stop = false;
    std::atomic<bool> _failed{false};     ///< no more parts are taken (behind, or a write error)
    std::atomic<bool> _ioFailed{false};   ///< a write failed: nothing more is written
    std::atomic<uint64_t> _queuedBytes{0};
    std::string _error;
};

class TTDSessionFile
{
public:
    static bool Save(const TimeTravelEngine& engine, ITTDByteSink& sink, std::string& error,
                     const TTDSessionSaveParams& params = {});
    /// Replace @p engine's session by the file's. False with the reason when
    /// nothing could be loaded (not a session file, damaged header or tables)
    /// @p append: the next file of the same recording (the next segment), added to @p engine's session
    static bool Load(TimeTravelEngine& engine, const ITTDByteSource& source, std::string& error,
                     TTDSessionLoadReport* report = nullptr, bool append = false);

    /// The stream ids this version reads
    static bool KnownStream(uint16_t id);
    /// Tests: Load as a reader without branch support would (the branch table and the twins unknown, every part
    /// read as the trunk's), the old reader of storage.md §5; true again restores the normal reading
    static void SetBranchReadingForTesting(bool on);
    /// The recorded machine's devices (the session's device table), without loading the session (file-info)
    static bool ReadDevices(const ITTDByteSource& source, std::vector<TTDDeviceDescriptor>& out, std::string& error);

    /// The copy frame-boundary stream @p stream (the engine's id) took at
    /// @p frame, from a session file; false with the reason when the stream
    /// was off then ("not recorded") or the file lacks it
    static bool ReadFrameStream(const ITTDByteSource& source, uint32_t stream, uint64_t frame,
                                std::vector<uint8_t>& out, std::string& error);

private:
    static bool ReadTables(const std::vector<uint8_t>& tables, uint32_t& interval, std::vector<TTDRegionDesc>& regions,
                           std::vector<TTDDeviceEntry>& devices, std::string& error);
};

}  // namespace ttd
