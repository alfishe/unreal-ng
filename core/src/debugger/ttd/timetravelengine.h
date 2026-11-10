#pragma once

/// @file timetravelengine.h
/// @brief TimeTravelEngine: the time-travel engine that replaced v1 (deleted in Phase 6).
///
/// Built next to v1 and checked against it byte for byte until users switched
/// to it in Phase 5 (docs/inprogress/2026-09-25-ttd-v2-migration/README.md). The
/// engine is fed one frame input per frame boundary (live capture) and
/// restores any recorded position.
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
#include <utility>
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
    /// Legacy ids of device states this frame offered for devices the table
    /// does not have (reported as DeviceNotPresent on restore); usually empty
    std::vector<uint8_t> unclaimedDevices;
    /// Where the bus journals stood at this boundary: a replay from here
    /// starts reading there (Phase 3, Step 1)
    uint64_t busReadCursor = 0;
    uint64_t busWriteCursor = 0;
    uint64_t mediaReadCursor = 0;   ///< ... and the media read journal
    uint64_t busVectorCursor = 0;   ///< ... and the interrupt-vector journal
    /// Interrupts accepted while halted, up to this boundary (TTDFrameInput::haltedInterrupts): the frame before
    /// was HALT-paced when it grew. Live captures only, not in the session file: haltKnown false after a load
    uint64_t haltedInterrupts = 0;
    bool haltKnown = false;
    /// The bytes the picture is read from (TTDFrameInput::screen), as an index into the session's screen layouts
    /// plus one (ScreenLayoutOf); 0: not known. Live captures only, not in the session file
    uint32_t screenLayout = 0;
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
    size_t mediaReads = 0;          ///< sectors read from media images
    size_t busVectors = 0;          ///< interrupt vectors taken
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

/// Counted work of one capture (deterministic; the benchmark reports it, D33)
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
    /// The state of device @p key at checkpoint @p index; false when the
    /// device is not in the table or had no state there
    bool DeviceState(size_t index, const TTDDeviceKey& key, std::vector<uint8_t>& out) const;
    /// The same for the device with legacy id @p v1Id
    bool DeviceStateOfV1Id(size_t index, uint8_t v1Id, std::vector<uint8_t>& out) const;
    /// The device table index of @p key, or -1
    int32_t DeviceIndex(const TTDDeviceKey& key) const;
    /// The device table index of the device with legacy id @p v1Id, or -1
    int32_t DeviceIndexOfV1Id(uint8_t v1Id) const;
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
    const TTDFrameTable& Frames() const { return _line->frames; }

    /// region <Events (Phase 3)>

    /// Append an event at (@p frame, @p tInFrame): its machine time is the
    /// frame's start plus the offset. A frame not recorded yet (an event after
    /// the last checkpoint) is placed by the last frame's length. False, and
    /// the payload released, when there is no session or the event lies
    /// before the last one
    bool AppendEvent(uint64_t frame, uint64_t tInFrame, TTDEvent ev);
    const TTDEventLog& Events() const { return _line->events; }
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
    const std::vector<TTDConfigEntry>& Configurations() const { return _line->configs; }
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
    const std::vector<TTDMediaSlot>& MediaSlots() const { return _line->mediaSlots; }

    /// The bytes checkpoint @p cp's picture is read from (sorted); null: not known (a session loaded from a file)
    const std::vector<TTDScreenRange>* ScreenLayoutOf(const TTDEngineCheckpoint& cp) const
    {
        return cp.screenLayout == 0 || cp.screenLayout > _screenLayouts.size() ? nullptr
                                                                                : &_screenLayouts[cp.screenLayout - 1];
    }
    /// Slot @p slot's version at checkpoint @p index; false before its first
    /// recorded version
    bool MediaVersionAt(size_t index, size_t slot, TTDMediaVersion& out) const;

    /// Bind a session that was not recorded from this machine (read from its
    /// file, Phase 4) to the live machine it restores into: each memory region
    /// to the live region of the same id and size, each device to the live
    /// device of the same key, else of the same legacy id. Returns how many
    /// regions and devices stay unbound; @p unbound names them
    size_t BindLive(const std::vector<TTDRegionDesc>& liveRegions, const std::vector<TTDDeviceEntry>& liveDevices,
                    std::string* unbound = nullptr);

    /// Bus data (Phase 3, Step 1): every IN result and every OUT of the main
    /// CPU, in execution order, in the bus journal's block format (kept from v1). Appended while
    /// recording; each checkpoint keeps where both stood
    void AppendBusRead(const TTDPortRecord& r) { _line->busReads.OnRead(r.port, r.value, r.frame, r.tInFrame, r.pc); }
    void AppendBusWrite(const TTDPortRecord& r) { _line->busWrites.OnWrite(r.port, r.value, r.frame, r.tInFrame, r.pc); }
    /// Sectors read from media images while recording (Phase 3)
    TTDMediaJournal& MediaReads() { return _line->mediaReads; }
    const TTDMediaJournal& MediaReads() const { return _line->mediaReads; }
    const TTDPortJournal& BusReads() const { return _line->busReads; }
    const TTDPortJournal& BusWrites() const { return _line->busWrites; }
    /// The bus journals' positions, for a caller that puts a replay's position back (a throwaway replay)
    TTDPortJournal& BusReadsMutable() { return _line->busReads; }
    TTDPortJournal& BusWritesMutable() { return _line->busWrites; }
    /// A replay reads the bus journals from these cursors (a checkpoint's):
    /// reads hand the CPU the recorded values, writes are checked
    void PlayBus(uint64_t readCursor, uint64_t writeCursor, uint64_t vectorCursor = 0)
    {
        _line->busReads.StartPlayback(readCursor);
        _line->busWrites.StartPlayback(writeCursor);
        _line->busVectors.StartPlayback(vectorCursor);
    }
    /// Interrupt vectors the CPU took (machines with their own INT logic)
    TTDPortJournal& BusVectors() { return _line->busVectors; }
    /// The write journal (D40): memory writes where it was recorded or built,
    /// and the spans it covers
    TTDWriteIndex& Writes() { return _line->writes; }
    const TTDWriteIndex& Writes() const { return _line->writes; }
    const TTDPortJournal& BusVectors() const { return _line->busVectors; }
    /// The journals while they play (null when a journal has nothing left): the CPU's IN / OUT hooks
    TTDPortJournal* BusReadsForPlayback()
    {
        return _line->busReads.GetMode() == TTDPortJournal::Mode::Play ? &_line->busReads : nullptr;
    }
    TTDPortJournal* BusWritesForPlayback()
    {
        return _line->busWrites.GetMode() == TTDPortJournal::Mode::Play ? &_line->busWrites : nullptr;
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
    const std::map<uint32_t, std::deque<FrameStreamCopy_>>& FrameStreamCopies() const { return _line->streamCopies; }

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
    size_t CheckpointCount() const { return _line->cpBase + _line->checkpoints.size(); }
    /// The first checkpoint still held: a ring drops the oldest segments (D41)
    size_t FirstCheckpoint() const { return _line->cpBase; }

    void SetHistoryPolicy(const TTDHistoryPolicy& policy) { _policy = policy; }
    const TTDHistoryPolicy& HistoryPolicy() const { return _policy; }
    const std::deque<TTDSegmentInfo>& Segments() const { return _line->segments; }
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

    /// region <Branch lines>

    /// A branch (docs/inprogress/2026-11-08-ttd-branches/architecture.md §4): a new line leaves the active one
    /// inside checkpoint @p index's frame at @p cut and becomes the active line; capture continues on it as after
    /// TruncateAfter, while the line it leaves keeps all of its history. The new line starts with a copy of
    /// checkpoint @p index (full tables, the pieces shared) and of the frame's records up to @p cut, so its own
    /// part needs nothing of the parent's; what lies before the fork is the parent's. @p line: the new line's id
    /// (ids are never reused). Live memory is unknown afterwards. The journals' modes pass to the new line
    /// (recording stays recording; a replay is stopped)
    bool ForkAt(size_t index, const TTDPosition& cut, uint32_t& line, std::string& error);
    /// Load (TTDSessionFile): a branch line read from a file. It forks from @p parent at @p cut, in the parent's
    /// checkpoint of frame @p forkFrame; its first checkpoint is built as ForkAt builds it, the machine state from
    /// @p first (the file's copy). The new line becomes the one the import fills (ImportCheckpoint and the
    /// journals); the session is read-only, as any loaded one
    /// @p line: in, the id it takes (at least LineIds(); the ids between stay empty: a file's branches left out),
    /// out, the id it took
    bool ImportForkLine(uint32_t parent, uint64_t forkFrame, const TTDPosition& cut, const TTDEngineCheckpoint& first,
                        uint32_t& line, std::string& error);
    /// Load: a branch line whose file starts it with a whole checkpoint (a recording folder's branch file, the
    /// fork's copy with every piece): the line forks from @p parent at @p cut as ImportForkLine's, empty; the
    /// import fills it from its first checkpoint (ImportCheckpoint: a baseline)
    bool ImportBranchLine(uint32_t parent, uint64_t forkFrame, const TTDPosition& cut, uint32_t& line,
                          std::string& error);
    /// Load: line @p line's first checkpoint (the fork's copy, imported whole) holds what its parent's checkpoint at
    /// the fork holds - the same frame and every piece's CRC. False with the reason: the file's branch names
    /// another parent or fork than the one its copy was taken from
    bool ForkCopyMatchesParent(uint32_t line, std::string& error);
    /// The next capture on @p line starts a segment (every piece whole): a recording's folder starts a file there
    /// when the line records again after a switch
    void RequestBaseline(uint32_t line)
    {
        if (HasLine(line))
            _lines[line]->baselineNext = true;
    }
    /// Make @p line active, capture continuing from its checkpoint @p index (one of its own: at or after its
    /// fork). Live memory is unknown afterwards
    bool SwitchLine(uint32_t line, size_t index, std::string& error);
    /// Read @p line from now on (a seek into the part of the lineage it owns): checkpoints, records and journals
    /// are its own, the capture head stays where it was until a capture on this line needs it (rebuilt then from
    /// the line's last checkpoint). Live memory stays known (versions are the store's). Cheap: no decode
    bool ViewLine(uint32_t line);
    /// Release a line that is not active: its checkpoints, versions, records (its ancestors keep theirs). Its
    /// children are the holder's to release first
    bool ReleaseLine(uint32_t line, std::string& error);
    uint32_t ActiveLine() const { return _lineId; }
    /// Lines ever made, released ones included (ids 0 .. LineIds()-1)
    size_t LineIds() const { return _lines.size(); }
    bool HasLine(uint32_t line) const { return line < _lines.size() && _lines[line] != nullptr; }
    /// The line @p line forks from (kNoLine for the trunk) and the checkpoint it forks at
    uint32_t LineParent(uint32_t line) const { return HasLine(line) ? _lines[line]->parent : kNoLine; }
    /// Another line's frame table and events (a branch's history before its fork lies in its ancestors'); null for
    /// a line not held
    const TTDFrameTable* LineFrames(uint32_t line) const { return HasLine(line) ? &_lines[line]->frames : nullptr; }
    const TTDEventLog* LineEvents(uint32_t line) const { return HasLine(line) ? &_lines[line]->events : nullptr; }
    size_t LineForkCheckpoint(uint32_t line) const { return HasLine(line) ? _lines[line]->forkIndex : 0; }
    /// The index of the line's first checkpoint (a branch: its fork checkpoint; the trunk: the first one held)
    size_t LineFirstCheckpoint(uint32_t line) const { return HasLine(line) ? _lines[line]->cpBase : 0; }
    TTDPosition LineFork(uint32_t line) const { return HasLine(line) ? _lines[line]->fork : TTDPosition{}; }
    static constexpr uint32_t kNoLine = 0xFFFFFFFFu;
    /// @p ancestor is @p line or one of the lines it forks from
    bool IsAncestorLine(uint32_t ancestor, uint32_t line) const
    {
        for (uint32_t l = line; HasLine(l); l = LineParent(l))
            if (l == ancestor)
                return true;
        return false;
    }

    /// endregion </Branch lines>

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
    /// Those pieces with their new versions (PieceStore().Decode reads one)
    std::vector<std::pair<uint32_t, TTDPieceId>> ChangesAt(size_t index, uint32_t region) const
    {
        std::vector<std::pair<uint32_t, TTDPieceId>> changes;
        if (const TTDEngineCheckpoint::RegionRefs* r = HasCheckpoint(index) ? RefsOf(index, region) : nullptr)
            for (uint32_t k = 0; k < r->changeCount; ++k)
                changes.emplace_back(ChangeAt(r->firstChange + k).piece, ChangeAt(r->firstChange + k).id);
        return changes;
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
    /// not seen at that point are left alone
    TTDRestoreResult RestoreToMemory(size_t index, const TTDWrittenFn& written = nullptr,
                                     TTDRestoreStats* stats = nullptr);

    /// Live memory changed behind the engine's back: the next RestoreToMemory
    /// writes every piece
    void ForgetMemory();

    /// The version whose content the last RestoreToMemory left in live memory for @p piece of @p region (writes
    /// since are the caller's to know), or kAbsent when not known
    TTDPieceId VersionInMemory(uint32_t region, uint32_t piece) const
    {
        if (region >= _inMemory.size() || piece >= _inMemory[region].size())
            return kAbsent;
        const TTDPieceId id = _inMemory[region][piece];
        return id == kUnknown ? kAbsent : id;
    }

    /// endregion </Restore>

    /// The whole session checked without touching the machine: every stored
    /// version against its checksum (DataDamaged, with the frames it reaches),
    /// every device with frames that hold no state (DeviceMissingState, with
    /// the frames), state for devices this machine lacks (DeviceNotPresent),
    /// and each live device's firmware against the recorded one. At most 64
    /// issues are listed; the message counts the rest
    TTDRestoreResult CheckSession() const;

    /// The state device @p device (its index in Devices()) holds at checkpoint
    /// @p index, without restoring anything (a load's guards read the
    /// baseline's); false when the checkpoint holds none or it fails its
    /// integrity check
    bool DeviceStateAt(size_t index, uint32_t device, std::vector<uint8_t>& out) const
    {
        return ReadDeviceState(index, device, out) == DeviceStateRead::Ok;
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
    bool HasCheckpoint(size_t index) const { return index >= _line->cpBase && index - _line->cpBase < _line->checkpoints.size(); }
    const TTDEngineCheckpoint& CpAt(size_t index) const { return _line->checkpoints[index - _line->cpBase]; }
    /// Start a segment at the checkpoint about to be added (D41)
    void BeginSegment(uint64_t frame);
    /// Ring: drop the oldest segment while the next one alone covers the window
    void ReleaseHistory();
    void DropOldestSegment();
    TTDHistoryPolicy _policy;
    struct PieceChange
    {
        uint32_t piece;
        TTDPieceId id;   ///< the record holds one reference to it
    };
    const PieceChange& ChangeAt(size_t index) const { return _line->changes[index - _line->changeBase]; }

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
    /// A device state region's pieces as last decoded (RestoreDevices): a restore decodes only the pieces whose
    /// version differs from the ones held (a 256 KB state that hardly changes costs a copy, not 64 decodes)
    struct DecodedState
    {
        uint64_t generation = 0;           ///< the store's ReuseGeneration the ids are good for
        const TTDPieceStore* store = nullptr;
        std::vector<TTDPieceId> ids;       ///< per piece: the version in bytes, kUnknown when none
        std::vector<uint8_t> bytes;
    };
    DeviceStateRead ReadDeviceState(size_t index, uint32_t device, std::vector<uint8_t>& out,
                                    uint32_t* damagedPiece = nullptr, DecodedState* decoded = nullptr) const;
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
    /// Per region: the current version of every piece, and the pieces changed
    /// since the last full table
    std::vector<std::vector<TTDPieceId>> _live;
    /// Per region: the version whose content is in live memory now, or kUnknown
    std::vector<std::vector<TTDPieceId>> _inMemory;
    static constexpr TTDPieceId kUnknown = 0xFFFFFFFEu;
    std::vector<DecodedState> _decodedStates;   ///< RestoreDevices: per device of the table
    std::vector<std::vector<uint32_t>> _sinceSnapshot;
    std::vector<std::vector<uint8_t>> _sinceSnapshotFlag;
    std::vector<TTDRefTables::Table*> _lastSnapshot;
    std::vector<int32_t> _deviceRegion;                 ///< device table index -> its state region
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
    TTDPayloadStore _payloads;
    TTDStreamRegistry _streams;
    std::vector<uint32_t> _offeredByRegion;   ///< the last capture's pieces, by region
    /// One branch's own history (docs/inprogress/2026-11-08-ttd-branches/architecture.md §4.1): what it
    /// recorded from its fork on. The capture head, the store, the tables and the session's declarations stay
    /// in the engine. A session that never forks holds one line, the trunk
    struct BranchLine
    {
        explicit BranchLine(TTDPayloadStore& payloads) : events(payloads) {}

        uint32_t parent = kNoLine;   ///< the line it forks from
        size_t forkIndex = 0;        ///< the parent's checkpoint it forks at (its own first one is a copy)
        bool baselineNext = false;   ///< RequestBaseline: the next capture starts a segment
        TTDPosition fork;            ///< the fork moment, in that checkpoint's frame

        size_t cpBase = 0;       ///< checkpoints dropped from the front
        size_t changeBase = 0;   ///< change records dropped from the front
        std::deque<TTDSegmentInfo> segments;
        std::map<uint32_t, std::deque<FrameStreamCopy_>> streamCopies;   ///< by stream, oldest first
        std::vector<PieceChange> changes;
        TTDFrameTable frames;
        TTDEventLog events;   ///< releases into the engine's payload store
        /// The InterruptFrame facts as (RZX frames done, machine time), in order
        std::vector<std::pair<uint64_t, TTDMachineTime>> rzxFrames;
        TTDPortJournal busReads{TTDPortJournal::Direction::Read};
        TTDPortJournal busWrites{TTDPortJournal::Direction::Write};
        TTDMediaJournal mediaReads;
        TTDPortJournal busVectors{TTDPortJournal::Direction::Read};
        TTDWriteIndex writes;
        /// A deque: no growth reserve (a vector held up to twice the records), and records never move
        std::deque<TTDEngineCheckpoint> checkpoints;
        std::vector<TTDConfigEntry> configs;
        std::vector<std::pair<uint32_t, TTDMediaVersion>> pendingMedia;   ///< slot, version: for the next checkpoint
        std::vector<TTDMediaSlot> mediaSlots;   ///< the slots and their version changes along this lineage
    };
    /// Every line, by branch id (the trunk first), and the active one: capture writes through it
    std::vector<std::unique_ptr<BranchLine>> _lines;
    BranchLine* _line = nullptr;
    uint32_t _lineId = 0;
    uint32_t _headLine = 0;   ///< the line the capture head was built for
    /// Release a line's change records and full tables (EndSession, ReleaseLine)
    void ReleaseLineData(BranchLine& line);
    /// The active line's journal modes pass to @p to: recording stays recording (ForkAt, SwitchLine)
    void HandOverJournals(BranchLine& from, BranchLine& to);
    /// The fork checkpoint's tables for @p line from the active line's checkpoint @p index (ForkAt, ImportForkLine)
    void ForkCheckpointTables(size_t index, BranchLine& line, TTDEngineCheckpoint& cp);
    /// An imported branch's fork: @p parent's checkpoint @p index of frame @p forkFrame, the id @p wanted free
    bool ImportLineHead(uint32_t parent, uint64_t forkFrame, const TTDPosition& cut, uint32_t wanted, size_t& index,
                        std::string& error);
    /// An imported branch's line, empty, at id @p wanted (the ids before it padded): the one the import fills
    BranchLine& AddImportedLine(uint32_t parent, size_t index, const TTDPosition& cut, uint32_t wanted);
    /// Capture continues from the active line's checkpoint @p index (TruncateAfter, ForkAt, SwitchLine)
    bool RebuildHeadFrom(size_t index, std::string& error);

    /// Every distinct screen layout of the session, once (a mode switch adds one; most sessions have one or two)
    std::vector<std::vector<TTDScreenRange>> _screenLayouts;
};

}  // namespace ttd
