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
#include <vector>

#include "debugger/ttd/engine/ttddevicetable.h"
#include "debugger/ttd/engine/ttdeventlog.h"
#include "debugger/ttd/engine/ttdmediajournal.h"
#include "debugger/ttd/engine/ttdpayloadstore.h"
#include "debugger/ttd/ttdportjournal.h"
#include "debugger/ttd/engine/ttdframeinput.h"
#include "debugger/ttd/engine/ttdpiecestore.h"
#include "debugger/ttd/engine/ttdreftable.h"
#include "debugger/ttd/engine/ttdregion.h"
#include "debugger/ttd/engine/ttdstreamregistry.h"
#include "debugger/ttd/engine/ttdtime.h"

namespace ttd
{

/// How exact a restore was (engine decision D7). Phase 1 restores are always
/// exact or damaged; "not bit-exact" and "degraded" come with Phases 2-3
enum class TTDRestoreStatus : uint8_t
{
    Exact = 0,
    NotBitExact = 1,   ///< state restored, but the session was recorded with other settings
    Degraded = 2,      ///< some items could not be restored; see the message
    Damaged = 3,       ///< stored data failed its integrity check
};

/// What one item of a restore lacked (Phase 2, Step 3; FR-7)
enum class TTDRestoreIssueKind : uint8_t
{
    DeviceMissingState,   ///< the device exists here, the checkpoint has no state for it
    DeviceNotPresent,     ///< the checkpoint has state for a device this machine lacks
    LayoutUnsupported,
    SizeMismatch,
    DeviceSetDiffers,
    FirmwareDiffers,      ///< restored exactly, but a replay may differ
    ConfigurationDiffers,
    DataDamaged,
    AfterRestoreFailed,
};

/// What a device not restored holds now
enum class TTDLiveStateAction : uint8_t
{
    NotApplicable,
    KeptLive,         ///< what it held before the restore (D38: within a session every checkpoint holds
                      ///< every device; this happens only on a machine whose devices differ, or on damage)
};

struct TTDRestoreIssue
{
    TTDRestoreIssueKind kind = TTDRestoreIssueKind::DeviceMissingState;
    TTDRestoreStatus severity = TTDRestoreStatus::Degraded;
    TTDDeviceKey device;   ///< empty instance for machine-wide issues
    TTDLiveStateAction action = TTDLiveStateAction::NotApplicable;
    std::string detail;
    /// DataDamaged, DeviceMissingState in CheckSession: the frames the issue
    /// reaches (a damaged version: from the change that stored it to the
    /// piece's next change that does not depend on it)
    uint64_t firstFrame = 0;
    uint64_t lastFrame = 0;
};

struct TTDRestoreResult
{
    TTDRestoreStatus status = TTDRestoreStatus::Exact;   ///< the worst issue
    std::string message;
    std::vector<TTDRestoreIssue> issues;
    bool Ok() const { return status == TTDRestoreStatus::Exact || status == TTDRestoreStatus::NotBitExact; }

    void Add(TTDRestoreIssue issue)
    {
        if (static_cast<uint8_t>(issue.severity) > static_cast<uint8_t>(status))
            status = issue.severity;
        if (!message.empty())
            message += "; ";
        message += issue.device.instance.empty() ? issue.detail : issue.device.instance + ": " + issue.detail;
        issues.push_back(std::move(issue));
    }
};

/// A capture at which a device that runs behind the CPU was not synced to the
/// frame boundary (TTDSerializable::TTDSyncedTime, FR-19)
struct TTDSyncMiss
{
    uint64_t frame = 0;
    TTDDeviceKey device;
    int64_t offset = 0;   ///< the device's distance from the frame start, in its own units
};

/// One recorded frame boundary
struct TTDEngineCheckpoint
{
    static constexpr uint32_t kNoParent = 0xFFFFFFFFu;

    TTDPosition position;
    TTDMachineTime start = 0;
    uint32_t parent = kNoParent;   ///< the checkpoint this one continues (branches share their parent's past)
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

    size_t Total() const
    {
        return pieceVersions + piecePayload + arenaSlack + referenceTables + deltaBase + checkpoints + deviceBlobs +
               frameTable + eventLog + portReads + portWrites + portJournalSlack + mediaReads + busVectors;
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

    /// endregion </Session>

    /// region <Capture>

    /// Record one frame boundary. The first frame of a session must list every
    /// piece the session should know; later frames list the pieces whose
    /// content changed. Frames must increase
    bool CaptureFrame(const TTDFrameInput& input, std::string& error);

    /// Work and time of the last CaptureFrame
    const TTDEngineCaptureWork& LastCaptureWork() const { return _lastWork; }

    /// Captures at which a device that runs behind the CPU was not at the
    /// frame boundary, this session: the count, and the first ones (up to 16)
    uint64_t SyncMissCount() const { return _syncMissCount; }
    const std::vector<TTDSyncMiss>& SyncMisses() const { return _syncMisses; }
    uint64_t LastCaptureNs() const { return _lastCaptureNs; }

    /// endregion </Capture>

    /// region <Restore>

    size_t CheckpointCount() const { return _checkpoints.size(); }
    const TTDEngineCheckpoint* Checkpoint(size_t index) const;
    /// Checkpoint index of @p position's frame boundary, or -1
    int64_t CheckpointIndexOf(const TTDPosition& position) const;

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
    bool _open = false;
    struct PieceChange
    {
        uint32_t piece;
        TTDPieceId id;   ///< the record holds one reference to it
    };

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
        for (const TTDEngineCheckpoint::RegionRefs& r : _checkpoints[index].regions)
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
    /// Per region: its latest contents (allocated when the region's first
    /// piece arrives), the base each new difference is computed against
    std::vector<std::vector<uint8_t>> _deltaBase;
    TTDFrameTable _frames;
    TTDPayloadStore _payloads;
    TTDEventLog _events{_payloads};   ///< after _payloads: it releases into it
    TTDPortJournal _busReads{TTDPortJournal::Direction::Read};
    TTDPortJournal _busWrites{TTDPortJournal::Direction::Write};
    TTDMediaJournal _mediaReads;
    TTDPortJournal _busVectors{TTDPortJournal::Direction::Read};
    TTDStreamRegistry _streams;
    /// A deque: no growth reserve (a vector held up to twice the records), and
    /// records never move
    std::vector<uint32_t> _offeredByRegion;   ///< the last capture's pieces, by region
    std::deque<TTDEngineCheckpoint> _checkpoints;
};

}  // namespace ttd
