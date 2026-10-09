#pragma once

/// @file timetravelengine.h
/// @brief TimeTravelEngine: the time-travel engine that replaces TimeTravelManager (v1).
///
/// Built next to v1 and checked against it byte for byte; users switch to it
/// in Phase 5 (docs/inprogress/2026-09-25-ttd-v2-migration/README.md). The
/// engine is fed one frame input per frame boundary (live capture, or the v1
/// file feeder in verification) and restores any recorded position.
///
/// Phase 1, Steps 1-3 (this version): time, positions, regions, checkpoints
/// with their parent, optional streams, memory accounting, the piece store
/// (each change stored once, chain limit per piece, encode once) and the
/// reference tables: each checkpoint records only the pieces that got a new
/// version (8 bytes each), and every S-th checkpoint also holds a full table
/// as copy-on-write blocks shared with the previous one. A restore starts
/// from the nearest full table and applies the recorded changes forward.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <deque>
#include <map>
#include <vector>

#include "debugger/ttd/engine/ttdconfigfingerprint.h"
#include "debugger/ttd/engine/ttddeltabase.h"
#include "debugger/ttd/engine/ttddevicetable.h"
#include "debugger/ttd/engine/ttdeventlog.h"
#include "debugger/ttd/engine/ttdmediajournal.h"
#include "debugger/ttd/engine/ttdpayloadstore.h"
#include "debugger/ttd/ttdportjournal.h"
#include "debugger/ttd/engine/ttdframeinput.h"
#include "debugger/ttd/engine/ttdpiecestore.h"
#include "debugger/ttd/engine/ttdreftable.h"
#include "debugger/ttd/engine/ttdrestoreresult.h"
#include "debugger/ttd/engine/ttdregion.h"
#include "debugger/ttd/engine/ttdstreamregistry.h"
#include "debugger/ttd/engine/ttdtime.h"
#include "debugger/ttd/engine/ttdwriteindex.h"

namespace ttd
{

/// A capture at which a device that runs behind the CPU was not synced to the
/// frame boundary (TTDSerializable::TTDSyncedTime, FR-19)
struct TTDSyncMiss
{
    uint64_t frame = 0;
    TTDDeviceKey device;
    int64_t offset = 0;   ///< the device's distance from the frame start, in its own units
};

/// One recorded frame boundary
/// What a recording keeps in memory (D41, owner decision 2026-10-04): a ring
/// of the last windowFrames, or every frame (Growable). History is kept in
/// segments of segmentFrames; each starts with a baseline (every piece stored
/// whole, fresh reference tables), so a segment needs nothing from earlier
/// ones and the ring drops the oldest whole
enum class TTDHistoryMode : uint8_t
{
    Ring,
    Growable,
};

struct TTDHistoryPolicy
{
    TTDHistoryMode mode = TTDHistoryMode::Ring;
    uint32_t windowFrames = 5 * 60 * 50;   ///< Ring: the frames kept seekable (5 minutes at 50 fps)
    uint32_t segmentFrames = 60 * 50;      ///< a baseline every minute; 0: one segment
};

/// Where a segment starts
struct TTDSegmentInfo
{
    size_t firstCheckpoint = 0;   ///< its baseline
    size_t firstChange = 0;       ///< its first change record
    uint64_t firstFrame = 0;
};

/// One piece of a region taking a new version (TimeTravelEngine::ImportCheckpoint)
struct TTDImportedChange
{
    uint32_t region = 0;
    uint32_t piece = 0;
    uint32_t id = 0;   ///< TTDPieceId
};

struct TTDEngineCheckpoint
{
    static constexpr uint32_t kNoParent = 0xFFFFFFFFu;

    TTDPosition position;
    TTDMachineTime start = 0;
    uint32_t parent = kNoParent;   ///< the checkpoint this one continues (branches share their parent's past)
    bool baseline = false;         ///< a segment starts here: every piece stored whole (D41)
    TTDCpuState cpu;
    TTDChipsetState chipset;
    /// v1 ids of device states this frame offered for devices the table
    /// does not have (reported as DeviceNotPresent on restore); usually empty
    std::vector<uint8_t> unclaimedDevices;
    /// Where the bus journals stood at this boundary: a replay from here
    /// starts reading there (Phase 3, Step 1)
    uint64_t busReadCursor = 0;
    uint64_t busWriteCursor = 0;
    uint64_t mediaReadCursor = 0;   ///< ... and the media read journal
    uint64_t busVectorCursor = 0;   ///< ... and the interrupt-vector journal
    /// Only for the regions that changed at this checkpoint (and, on every
    /// S-th checkpoint, for every region): its range of the engine's change
    /// records and, on those checkpoints, a full reference table (shared
    /// blocks; one reference, held here). A region with no change has no entry,
    /// so an idle region costs nothing per frame (PR-10)
    struct RegionRefs
    {
        TTDRefTables::Table* snapshot = nullptr;
        uint32_t region = 0;
        uint32_t firstChange = 0;
        uint32_t changeCount = 0;
    };
    std::vector<RegionRefs> regions;   ///< sorted by region
};

/// The settings in force from a frame on (Phase 3, Step 4): the session's
/// first entry, then one per ConfigChange cut
struct TTDConfigEntry
{
    uint64_t frame = 0;
    TTDConfigFingerprint fingerprint;
};

/// A removable or writable medium as the session knows it (Phase 3, Step 4)
struct TTDMediaVersion
{
    uint64_t contentId = 0;   ///< the source's identity (0: no medium)
    uint64_t version = 0;     ///< the media layer's version; until it keeps versions, its write count
    bool operator==(const TTDMediaVersion& o) const { return contentId == o.contentId && version == o.version; }
    bool operator!=(const TTDMediaVersion& o) const { return !(*this == o); }
};

struct TTDMediaSlot
{
    std::string slot;      ///< "fdd.a", "sd.zc", "ide0.master"
    std::string format;    ///< as the media manager reports it ("trd", "img", ...)
    bool hasVersions = false;   ///< the media layer can set its head to a recorded version
    /// The checkpoints at which its version changed, and the version from there on
    std::vector<std::pair<uint32_t, TTDMediaVersion>> changes;
};

/// Where the engine's memory goes (FR-16; mirrors the benchmark's bm4_heap split)
struct TTDEngineHeapBreakdown
{
    size_t pieceVersions = 0;   ///< the piece store's version table
    size_t piecePayload = 0;    ///< compressed piece bytes (live)
    size_t arenaSlack = 0;      ///< arena chunks allocated beyond the live payload
    size_t referenceTables = 0; ///< region tables (each distinct table once)
    size_t deltaBase = 0;       ///< each region's latest contents, kept to compute differences
    size_t checkpoints = 0;     ///< checkpoint records
    size_t deviceBlobs = 0;
    size_t frameTable = 0;
    size_t eventLog = 0;            ///< event records and their payloads (Phase 3)
    size_t portReads = 0;           ///< the IN bus journal (compressed blocks + the open block)
    size_t portWrites = 0;          ///< the OUT bus journal
    size_t portJournalSlack = 0;    ///< allocated, not holding records
    size_t mediaReads = 0;          ///< sectors read from media images (v1 has no such journal)
    size_t busVectors = 0;          ///< interrupt vectors taken (v1 has no such journal)
    size_t writeJournal = 0;        ///< the write journal (D40): compressed blocks and segments
    size_t frameStreams = 0;        ///< frame-boundary stream copies not written out yet (D19)
    /// The session's own tables: regions, what memory holds now, per-region
    /// counters and scratch, time lines, configuration entries, media slots,
    /// segments (FR-16: the report covers everything the engine allocates)
    size_t bookkeeping = 0;

    size_t Total() const
    {
        return pieceVersions + piecePayload + arenaSlack + referenceTables + deltaBase + checkpoints + deviceBlobs +
               frameTable + eventLog + portReads + portWrites + portJournalSlack + mediaReads + busVectors +
               writeJournal + frameStreams + bookkeeping;
    }
};

/// Whether piece @p piece of region @p region was written since the engine last
/// knew its content (a capture or a restore); used by RestoreToMemory
using TTDWrittenFn = std::function<bool(uint32_t region, uint32_t piece)>;

/// Work of one RestoreToMemory (deterministic)
struct TTDRestoreStats
{
    uint64_t piecesDecoded = 0;   ///< written into live memory
    uint64_t piecesSkipped = 0;   ///< already held the target's content
    uint64_t linksDecoded = 0;    ///< difference versions applied while decoding
};

/// Counted work of one capture (deterministic; the CI gate compares it with v1's, D33)
struct TTDEngineCaptureWork
{
    uint64_t piecesOffered = 0;       ///< pieces handed to the capture (dirty or rescanned)
    uint64_t versionsStored = 0;      ///< new versions (content really changed)
    uint64_t deltaBaseBytes = 0;      ///< bytes copied into the delta base
    uint64_t compressCalls = 0;
    uint64_t compressInputBytes = 0;
    uint64_t deviceBlobBytes = 0;     ///< device state copied into the checkpoint
    uint64_t deviceStateBytes = 0;    ///< device state serialized for it (raw; TTDFrameInput)
    /// Pieces of the device state regions compared this capture: the ones the
    /// states reach now or reached last time, and the time-field anchors - not
    /// the region's declared maximum (a network card declares 16 MB)
    uint64_t devicePiecesOffered = 0;
};

class TimeTravelEngine
{
public:
    /// A piece the session has not seen yet
    static constexpr uint32_t kAbsent = 0xFFFFFFFFu;

    /// Full reference table every this many checkpoints (default; the rest record changes only)
    static constexpr uint32_t kDefaultSnapshotInterval = 64;

    /// @p store: the piece store, shareable by several sessions (D22); a store
    /// of its own when null
    explicit TimeTravelEngine(std::shared_ptr<TTDPieceStore> store = nullptr);

    /// Full reference table every @p interval checkpoints; set before BeginSession
    void SetSnapshotInterval(uint32_t interval) { _snapshotInterval = interval ? interval : 1; }
    ~TimeTravelEngine();
    TimeTravelEngine(const TimeTravelEngine&) = delete;
    TimeTravelEngine& operator=(const TimeTravelEngine&) = delete;

    /// region <Session>

    /// Start a session over @p regions (fixed for the session in Phase 1)
    bool BeginSession(const std::vector<TTDRegionDesc>& regions, std::string& error)
    {
        return BeginSession(regions, {}, error);
    }
    /// A session with memory @p regions and @p devices (Phase 2): the device
    /// table is checked and put in restore order, and each device gets a
    /// region of its own holding its state (4 bytes of length, then the
    /// state), stored like memory: only when it changes, as a difference
    bool BeginSession(const std::vector<TTDRegionDesc>& regions, std::vector<TTDDeviceEntry> devices,
                      std::string& error);
    /// Drop the session and everything it holds
    void EndSession();

    /// The recorded machine's devices (Phase 2), in restore order
    const TTDDeviceTable& Devices() const { return _devices; }
    /// The state of the device with v1 id @p id at checkpoint @p index; false
    /// when the device is not in the table or had no state there
    bool DeviceState(size_t index, uint8_t id, std::vector<uint8_t>& out) const;
    /// Whether region @p region holds a device's state (not memory)
    bool IsDeviceStateRegion(uint32_t region) const
    {
        return region < _regions.size() && static_cast<uint16_t>(_regions[region].id) >=
                                               static_cast<uint16_t>(TTDRegionId::DeviceStateFirst);
    }

    /// Restore every device of the table from checkpoint @p index, in restore
    /// order, then call their after-restore hooks. Run after the memory regions
    /// are restored (RestoreToMemory): a device whose state the engine keeps
    /// without its region memory loads only that state. A device without state
    /// in the checkpoint keeps its live state; state for a device this
    /// machine lacks, damaged state, a state of the wrong size
    /// and a different firmware are reported. Each problem is an issue of the
    /// result, naming the device and what it holds now
    TTDRestoreResult RestoreDevices(size_t index, const TTDRestoreContext& context);
    bool IsSessionOpen() const { return _open; }

    const std::vector<TTDRegionDesc>& Regions() const { return _regions; }
    const TTDFrameTable& Frames() const { return _frames; }

    /// region <Events (Phase 3)>

    /// Append an event at (@p frame, @p tInFrame): its machine time is the
    /// frame's start plus the offset. A frame not recorded yet (an event after
    /// the last checkpoint) is placed by the last frame's length. False, and
    /// the payload released, when there is no session or the event lies
    /// before the last one
    bool AppendEvent(uint64_t frame, uint64_t tInFrame, TTDEvent ev);
    const TTDEventLog& Events() const { return _events; }
    TTDPayloadStore& Payloads() { return _payloads; }
    const TTDPayloadStore& Payloads() const { return _payloads; }
    /// The position of machine time @p t (frame, offset); false before the first frame
    bool PositionOf(TTDMachineTime t, TTDPosition& out) const;

    /// RZX playback (Phase 3, Step 2): where the session reached RZX frame
    /// @p rzxFrame - the end of the step that ended the frame before it, from
    /// the InterruptFrame facts. A frame of 0 fetches has no end of its own
    /// (the player skips it): the next frame end that reached it, as the
    /// player's own seek does. False outside the session or without RZX
    bool RzxFrameTime(uint64_t rzxFrame, TTDMachineTime& at) const;
    /// What drives the history at @p t (the last ReplaySourceChange at or before it)
    TTDReplaySource ReplaySourceAt(TTDMachineTime t) const;

    /// region <Configuration and media (Phase 3, Step 4)>

    /// The settings in force from @p frame on. The first call of a session
    /// sets its configuration; a later one that differs from the last adds an
    /// entry and a ConfigChange cut at the frame's start (args: the entry
    /// index, u32). Equal settings change nothing. False without a session
    bool SetConfiguration(uint64_t frame, TTDConfigFingerprint fingerprint);
    const std::vector<TTDConfigEntry>& Configurations() const { return _configs; }
    /// The settings checkpoint @p index was recorded with (null: none set)
    const TTDConfigFingerprint* ConfigurationAt(size_t index) const;
    /// Compare checkpoint @p index's settings with @p live: one
    /// ConfigurationDiffers issue per setting that differs. For a replay
    /// (@p forReplay) every difference makes the result NotBitExact; for a
    /// restore only those that change what a checkpoint restores (the model,
    /// the RAM size: Degraded) count. The session stays open for inspection
    TTDRestoreResult CheckConfiguration(size_t index, const TTDConfigFingerprint& live, bool forReplay) const;

    /// A medium's version now (called before the capture of the frame it
    /// belongs to): kept with the next checkpoint when it differs from the
    /// slot's last one. A new slot joins the session's media table
    void NoteMediaVersion(const std::string& slot, const std::string& format, bool hasVersions,
                          const TTDMediaVersion& version);
    const std::vector<TTDMediaSlot>& MediaSlots() const { return _mediaSlots; }
    /// Slot @p slot's version at checkpoint @p index; false before its first
    /// recorded version
    bool MediaVersionAt(size_t index, size_t slot, TTDMediaVersion& out) const;

    /// Bind a session that was not recorded from this machine (fed from a v1
    /// file; Phase 4: read from its own file) to the live machine it restores
    /// into: each memory region to the live region of the same id and size,
    /// each device to the live device of the same v1 id. Returns how many
    /// regions and devices stay unbound; @p unbound names them
    size_t BindLive(const std::vector<TTDRegionDesc>& liveRegions, const std::vector<TTDDeviceEntry>& liveDevices,
                    std::string* unbound = nullptr);

    /// Bus data (Phase 3, Step 1): every IN result and every OUT of the main
    /// CPU, in execution order, in v1's block format. Appended while
    /// recording; each checkpoint keeps where both stood
    void AppendBusRead(const TTDPortRecord& r) { _busReads.OnRead(r.port, r.value, r.frame, r.tInFrame, r.pc); }
    void AppendBusWrite(const TTDPortRecord& r) { _busWrites.OnWrite(r.port, r.value, r.frame, r.tInFrame, r.pc); }
    /// Sectors read from media images while recording (Phase 3)
    TTDMediaJournal& MediaReads() { return _mediaReads; }
    const TTDMediaJournal& MediaReads() const { return _mediaReads; }
    const TTDPortJournal& BusReads() const { return _busReads; }
    const TTDPortJournal& BusWrites() const { return _busWrites; }
    /// The bus journals' positions, for a caller that puts a replay's position back (a throwaway replay)
    TTDPortJournal& BusReadsMutable() { return _busReads; }
    TTDPortJournal& BusWritesMutable() { return _busWrites; }
    /// A replay reads the bus journals from these cursors (a checkpoint's):
    /// reads hand the CPU the recorded values, writes are checked
    void PlayBus(uint64_t readCursor, uint64_t writeCursor, uint64_t vectorCursor = 0)
    {
        _busReads.StartPlayback(readCursor);
        _busWrites.StartPlayback(writeCursor);
        _busVectors.StartPlayback(vectorCursor);
    }
    /// Interrupt vectors the CPU took (machines with their own INT logic)
    TTDPortJournal& BusVectors() { return _busVectors; }
    /// The write journal (D40): memory writes where it was recorded or built,
    /// and the spans it covers
    TTDWriteIndex& Writes() { return _writes; }
    const TTDWriteIndex& Writes() const { return _writes; }
    const TTDPortJournal& BusVectors() const { return _busVectors; }
    /// The journals while they play (null when a journal has nothing left): the CPU's IN / OUT hooks
    TTDPortJournal* BusReadsForPlayback()
    {
        return _busReads.GetMode() == TTDPortJournal::Mode::Play ? &_busReads : nullptr;
    }
    TTDPortJournal* BusWritesForPlayback()
    {
        return _busWrites.GetMode() == TTDPortJournal::Mode::Play ? &_busWrites : nullptr;
    }

    /// endregion </Events (Phase 3)>
    TTDStreamRegistry& Streams() { return _streams; }

    /// Optional frame-boundary streams (D19, Phase 4 Step 4): a stream's
    /// capture hands its copy of @p frame here. Copies are written through:
    /// the session writer takes them into the file and they leave memory
    /// (DropFrameStreamCopiesBefore); without a file they stay
    void AddFrameStreamCopy(uint32_t stream, uint64_t frame, const uint8_t* data, size_t size);
    /// The copy of @p frame still in memory; false when it was written out or never captured
    bool FrameStreamCopy(uint32_t stream, uint64_t frame, std::vector<uint8_t>& out) const;
    /// Copies of frames before @p frame leave memory (written to the file, or dropped with the ring)
    void DropFrameStreamCopiesBefore(uint64_t frame);
    struct FrameStreamCopy_
    {
        uint64_t frame;
        std::vector<uint8_t> bytes;
    };
    const std::map<uint32_t, std::deque<FrameStreamCopy_>>& FrameStreamCopies() const { return _streamCopies; }

    /// endregion </Session>

    /// region <Capture>

    /// Record one frame boundary. The first frame of a session must list every
    /// piece the session should know; later frames list the pieces whose
    /// content changed. Frames must increase
    bool CaptureFrame(const TTDFrameInput& input, std::string& error);

    /// A checkpoint read from a session file (Phase 4): its fields, and the
    /// pieces whose version changes there, sorted by region, each with a
    /// version already in the store (its reference passes to the session).
    /// The reference tables are rebuilt as CaptureFrame builds them. A
    /// session that imported a checkpoint is read-only: CaptureFrame refuses
    bool ImportCheckpoint(TTDEngineCheckpoint cp, const std::vector<TTDImportedChange>& changes, std::string& error);
    bool IsReadOnly() const { return _readOnly; }

    /// Work and time of the last CaptureFrame
    const TTDEngineCaptureWork& LastCaptureWork() const { return _lastWork; }

    /// Captures at which a device that runs behind the CPU was not at the
    /// frame boundary, this session: the count, and the first ones (up to 16)
    uint64_t SyncMissCount() const { return _syncMissCount; }
    const std::vector<TTDSyncMiss>& SyncMisses() const { return _syncMisses; }
    uint64_t LastCaptureNs() const { return _lastCaptureNs; }

    /// endregion </Capture>

    /// region <Restore>

    /// Checkpoints recorded, counted from the session's start (dropped ones included)
    size_t CheckpointCount() const { return _cpBase + _checkpoints.size(); }
    /// The first checkpoint still held: a ring drops the oldest segments (D41)
    size_t FirstCheckpoint() const { return _cpBase; }

    void SetHistoryPolicy(const TTDHistoryPolicy& policy) { _policy = policy; }
    const TTDHistoryPolicy& HistoryPolicy() const { return _policy; }
    const std::deque<TTDSegmentInfo>& Segments() const { return _segments; }
    const TTDEngineCheckpoint* Checkpoint(size_t index) const;
    /// Checkpoint index of @p position's frame boundary, or -1
    int64_t CheckpointIndexOf(const TTDPosition& position) const;
    /// The last checkpoint held whose frame is at or before @p frame, or -1
    /// when the held history starts after it
    int64_t CheckpointAtOrBefore(uint64_t frame) const;
    /// Drop the oldest segment - its checkpoints, versions and journal records -
    /// when a later one is held; false when one segment is left. A ring does
    /// this by itself; a holder with a limit of its own (bytes) calls it
    bool DropOldestHeldSegment();
    /// A recording resumed from the past (Phase 5, C2): forget everything after
    /// checkpoint @p index - later checkpoints, their versions, the events,
    /// bus, vector and media records after @p cut (records at it stay) - and
    /// continue capturing from @p index as if it were the last capture. @p cut
    /// lies in @p index's frame. Live memory is unknown afterwards (the next
    /// RestoreToMemory writes every piece). The write journal is the holder's
    /// to rebuild (Writes()). A session loaded from a file continues the same
    /// way (it is no longer read-only). Branches that keep the old future come
    /// with Phase 5, Step 2
    bool TruncateAfter(size_t index, const TTDPosition& cut, std::string& error);

    /// Write region @p region as it was at checkpoint @p index into @p out
    /// (the region's pieces × 4 KB). Pieces the session had not seen at that
    /// point are left untouched and marked 0 in @p present (optional)
    TTDRestoreResult RestoreRegion(size_t index, uint32_t region, uint8_t* out,
                                   std::vector<uint8_t>* present = nullptr) const;

    /// The stored version of @p piece of @p region at checkpoint @p index, or kAbsent
    uint32_t VersionAt(size_t index, uint32_t region, uint32_t piece) const;

    /// Pieces of @p region that got a new version at checkpoint @p index
    uint32_t ChangeCount(size_t index, uint32_t region) const
    {
        const TTDEngineCheckpoint::RegionRefs* r = RefsOf(index, region);
        return r ? r->changeCount : 0;
    }
    /// Whether checkpoint @p index holds a full table of @p region
    bool HasFullTable(size_t index, uint32_t region) const
    {
        const TTDEngineCheckpoint::RegionRefs* r = RefsOf(index, region);
        return r && r->snapshot;
    }

    /// Restore every region with live memory to checkpoint @p index, writing
    /// only the pieces whose content differs (Step 5): a piece is decoded when
    /// the version in live memory is not the target's, or when @p written says
    /// it was written since (null = nothing written). Pieces the session had
    /// not seen at that point are left alone, as v1 does
    TTDRestoreResult RestoreToMemory(size_t index, const TTDWrittenFn& written = nullptr,
                                     TTDRestoreStats* stats = nullptr);

    /// Live memory changed behind the engine's back: the next RestoreToMemory
    /// writes every piece
    void ForgetMemory();

    /// endregion </Restore>

    /// The whole session checked without touching the machine: every stored
    /// version against its checksum (DataDamaged, with the frames it reaches),
    /// every device with frames that hold no state (DeviceMissingState, with
    /// the frames), state for devices this machine lacks (DeviceNotPresent),
    /// and each live device's firmware against the recorded one. At most 64
    /// issues are listed; the message counts the rest
    TTDRestoreResult CheckSession() const;

    /// The state device @p id (its v1 id) holds at checkpoint @p index, without
    /// restoring anything (a load's guards read the baseline's); false when the
    /// checkpoint holds none or it fails its integrity check
    bool DeviceStateAt(size_t index, uint8_t id, std::vector<uint8_t>& out) const
    {
        return ReadDeviceState(index, id, out) == DeviceStateRead::Ok;
    }

    /// Tests only: damage the stored version of @p piece of @p region at
    /// checkpoint @p index (one flipped bit)
    bool DamageForTesting(size_t index, uint32_t region, uint32_t piece)
    {
        const uint32_t id = VersionAt(index, region, piece);
        return id != kAbsent && _store->FlipBitForTesting(id, 1);
    }

    TTDEngineHeapBreakdown HeapBreakdown() const;

    /// Bytes the reference tables store: change records plus full tables
    /// (what a file would hold; HeapBreakdown counts the allocations)
    size_t ReferenceBytes() const;

    const TTDPieceStore& PieceStore() const { return *_store; }

    /// Compressed bytes stored for region @p region's new versions so far (a
    /// stream size: what this region adds to the recording)
    /// Pieces of @p region the last capture was handed (counted work)
    uint32_t LastPiecesOffered(uint32_t region) const
    {
        return region < _offeredByRegion.size() ? _offeredByRegion[region] : 0;
    }

    uint64_t RegionPayloadBytes(uint32_t region) const
    {
        return region < _regionPayload.size() ? _regionPayload[region] : 0;
    }
    /// New versions stored for region @p region so far
    uint64_t RegionVersionCount(uint32_t region) const
    {
        return region < _regionVersions.size() ? _regionVersions[region] : 0;
    }

private:
    friend class TTDSessionFile;     // reads and rebuilds a session as a whole (Phase 4)
    friend class TTDSessionWriter;   // writes a session as it records

    bool _open = false;
    bool _readOnly = false;   ///< loaded from a file: CaptureFrame refuses
    /// An event whose machine time is set: the log, and the RZX frame index
    bool AppendTimedEvent(const TTDEvent& ev);
    void NoteChange(uint32_t region, uint32_t piece, TTDPieceId next);
    void CloseRegion(TTDEngineCheckpoint& cp, TTDEngineCheckpoint::RegionRefs& refs, bool snapshot, bool fresh = false);
    /// Checkpoints and change records by their index from the session's start
    bool HasCheckpoint(size_t index) const { return index >= _cpBase && index - _cpBase < _checkpoints.size(); }
    const TTDEngineCheckpoint& CpAt(size_t index) const { return _checkpoints[index - _cpBase]; }
    /// Start a segment at the checkpoint about to be added (D41)
    void BeginSegment(uint64_t frame);
    /// Ring: drop the oldest segment while the next one alone covers the window
    void ReleaseHistory();
    void DropOldestSegment();
    size_t _cpBase = 0;       ///< checkpoints dropped from the front
    size_t _changeBase = 0;   ///< change records dropped from the front
    TTDHistoryPolicy _policy;
    std::deque<TTDSegmentInfo> _segments;
    std::map<uint32_t, std::deque<FrameStreamCopy_>> _streamCopies;   ///< by stream, oldest first
    struct PieceChange
    {
        uint32_t piece;
        TTDPieceId id;   ///< the record holds one reference to it
    };
    const PieceChange& ChangeAt(size_t index) const { return _changes[index - _changeBase]; }

    /// The version of every piece of @p region at checkpoint @p index
    void BuildMap(size_t index, uint32_t region, std::vector<TTDPieceId>& map) const;
    /// A DataDamaged issue for @p piece of @p region, which failed to decode
    /// at checkpoint @p index: the frames around it whose version fails too
    TTDRestoreIssue DamageIssue(size_t index, uint32_t region, uint32_t piece) const;
    /// The device table entry whose state region is @p region, or null
    const TTDDeviceEntry* DeviceOfRegion(uint32_t region) const;
    enum class DeviceStateRead : uint8_t
    {
        Ok,
        Missing,   ///< no state in this checkpoint (length 0), or no such device
        Damaged,   ///< a piece failed its integrity check: *damagedPiece
    };
    DeviceStateRead ReadDeviceState(size_t index, uint8_t id, std::vector<uint8_t>& out,
                                    uint32_t* damagedPiece = nullptr) const;
    /// Checkpoint @p index's entry for @p region, or null when it has none
    const TTDEngineCheckpoint::RegionRefs* RefsOf(size_t index, uint32_t region) const
    {
        for (const TTDEngineCheckpoint::RegionRefs& r : CpAt(index).regions)
            if (r.region == region)
                return &r;
        return nullptr;
    }

    std::shared_ptr<TTDPieceStore> _store;
    std::unique_ptr<TTDRefTables> _tables;
    uint32_t _snapshotInterval = kDefaultSnapshotInterval;
    std::vector<PieceChange> _changes;
    /// Per region: the current version of every piece, and the pieces changed
    /// since the last full table
    std::vector<std::vector<TTDPieceId>> _live;
    /// Per region: the version whose content is in live memory now, or kUnknown
    std::vector<std::vector<TTDPieceId>> _inMemory;
    static constexpr TTDPieceId kUnknown = 0xFFFFFFFEu;
    std::vector<std::vector<uint32_t>> _sinceSnapshot;
    std::vector<std::vector<uint8_t>> _sinceSnapshotFlag;
    std::vector<TTDRefTables::Table*> _lastSnapshot;
    std::array<int32_t, 256> _deviceRegionOf{};          ///< v1 id -> region index, -1 = none
    std::vector<std::vector<uint8_t>> _deviceScratch;   ///< per region: the state laid out for capture
    /// Per region: where the last laid-out state ended (4 + its length); kExtentUnknown: the whole region
    std::vector<uint32_t> _deviceExtent;
    static constexpr uint32_t kExtentUnknown = 0xFFFFFFFFu;
    /// Per region: a variable-size device state without time fields. Its scratch and delta base cover the pieces
    /// the state reached, not the largest size (a network adapter declares 64 MiB and usually uses a few KiB)
    std::vector<uint8_t> _growsWithState;
    /// `v` covering pieces [0, pieces): a growing region's buffer grows to them, any other is the whole region
    void CoverPieces(uint32_t region, std::vector<uint8_t>& v, uint32_t pieces) const;
    /// A time field's line, kept per device region and field while recording
    struct TimeLine
    {
        uint64_t value = 0;      ///< the anchor value
        uint64_t frame = 0;      ///< its frame
        uint64_t step = 0;       ///< per frame
        uint64_t last = 0;       ///< the value of the previous capture
        bool valid = false;
    };
    std::vector<std::vector<TimeLine>> _timeLines;      ///< per region (device regions only)
    std::vector<std::vector<TTDTimeField>> _timeFields; ///< per region: the device's declared fields
    TTDDeviceTable _devices;
    std::vector<uint64_t> _regionPayload;
    std::vector<uint64_t> _regionVersions;
    TTDEngineCaptureWork _lastWork;
    uint64_t _syncMissCount = 0;
    std::vector<TTDSyncMiss> _syncMisses;
    uint64_t _lastCaptureNs = 0;
    std::vector<TTDRegionDesc> _regions;
    /// Per region: its latest contents, the base each new difference is
    /// computed against (sparse: a uniform piece keeps only its value)
    TTDDeltaBase _deltaBase;
    TTDFrameTable _frames;
    TTDPayloadStore _payloads;
    TTDEventLog _events{_payloads};   ///< after _payloads: it releases into it
    /// The InterruptFrame facts as (RZX frames done, machine time), in order
    std::vector<std::pair<uint64_t, TTDMachineTime>> _rzxFrames;
    TTDPortJournal _busReads{TTDPortJournal::Direction::Read};
    TTDPortJournal _busWrites{TTDPortJournal::Direction::Write};
    TTDMediaJournal _mediaReads;
    TTDPortJournal _busVectors{TTDPortJournal::Direction::Read};
    TTDWriteIndex _writes;
    TTDStreamRegistry _streams;
    /// A deque: no growth reserve (a vector held up to twice the records), and
    /// records never move
    std::vector<uint32_t> _offeredByRegion;   ///< the last capture's pieces, by region
    std::deque<TTDEngineCheckpoint> _checkpoints;
    std::vector<TTDConfigEntry> _configs;
    std::vector<TTDMediaSlot> _mediaSlots;
    std::vector<std::pair<uint32_t, TTDMediaVersion>> _pendingMedia;   ///< slot, version: for the next checkpoint
};

}  // namespace ttd
