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

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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

struct TTDRestoreResult
{
    TTDRestoreStatus status = TTDRestoreStatus::Exact;
    std::string message;
    bool Ok() const { return status == TTDRestoreStatus::Exact || status == TTDRestoreStatus::NotBitExact; }
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
    std::unordered_map<uint8_t, std::vector<uint8_t>> deviceBlobs;
    /// Per region: the pieces that got a new version at this checkpoint (a
    /// range of the engine's change records) and, on every S-th checkpoint, a
    /// full reference table (shared blocks; one reference, held here)
    struct RegionRefs
    {
        TTDRefTables::Table* snapshot = nullptr;
        uint32_t firstChange = 0;
        uint32_t changeCount = 0;
    };
    std::vector<RegionRefs> regions;
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

    size_t Total() const
    {
        return pieceVersions + piecePayload + arenaSlack + referenceTables + deltaBase + checkpoints + deviceBlobs +
               frameTable;
    }
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
    bool BeginSession(const std::vector<TTDRegionDesc>& regions, std::string& error);
    /// Drop the session and everything it holds
    void EndSession();
    bool IsSessionOpen() const { return _open; }

    const std::vector<TTDRegionDesc>& Regions() const { return _regions; }
    const TTDFrameTable& Frames() const { return _frames; }
    TTDStreamRegistry& Streams() { return _streams; }

    /// endregion </Session>

    /// region <Capture>

    /// Record one frame boundary. The first frame of a session must list every
    /// piece the session should know; later frames list the pieces whose
    /// content changed. Frames must increase
    bool CaptureFrame(const TTDFrameInput& input, std::string& error);

    /// Work and time of the last CaptureFrame
    const TTDEngineCaptureWork& LastCaptureWork() const { return _lastWork; }
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

    /// endregion </Restore>

    TTDEngineHeapBreakdown HeapBreakdown() const;

    /// Bytes the reference tables store: change records plus full tables
    /// (what a file would hold; HeapBreakdown counts the allocations)
    size_t ReferenceBytes() const;

    const TTDPieceStore& PieceStore() const { return *_store; }

private:
    bool _open = false;
    struct PieceChange
    {
        uint32_t piece;
        TTDPieceId id;   ///< the record holds one reference to it
    };

    /// The version of every piece of @p region at checkpoint @p index
    void BuildMap(size_t index, uint32_t region, std::vector<TTDPieceId>& map) const;

    std::shared_ptr<TTDPieceStore> _store;
    std::unique_ptr<TTDRefTables> _tables;
    uint32_t _snapshotInterval = kDefaultSnapshotInterval;
    std::vector<PieceChange> _changes;
    /// Per region: the current version of every piece, and the pieces changed
    /// since the last full table
    std::vector<std::vector<TTDPieceId>> _live;
    std::vector<std::vector<uint32_t>> _sinceSnapshot;
    std::vector<std::vector<uint8_t>> _sinceSnapshotFlag;
    std::vector<TTDRefTables::Table*> _lastSnapshot;
    TTDEngineCaptureWork _lastWork;
    uint64_t _lastCaptureNs = 0;
    std::vector<TTDRegionDesc> _regions;
    /// Per region: its latest contents (allocated when the region's first
    /// piece arrives), the base each new difference is computed against
    std::vector<std::vector<uint8_t>> _deltaBase;
    TTDFrameTable _frames;
    TTDStreamRegistry _streams;
    std::vector<TTDEngineCheckpoint> _checkpoints;
};

}  // namespace ttd
