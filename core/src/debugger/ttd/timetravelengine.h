#pragma once

/// @file timetravelengine.h
/// @brief TimeTravelEngine: the time-travel engine that replaces TimeTravelManager (v1).
///
/// Built next to v1 and checked against it byte for byte; users switch to it
/// in Phase 5 (docs/inprogress/2026-09-25-ttd-v2-migration/README.md). The
/// engine is fed one frame input per frame boundary (live capture, or the v1
/// file feeder in verification) and restores any recorded position.
///
/// Phase 1, Steps 1-2 (this version): time, positions, regions, checkpoints
/// with their parent, optional streams, memory accounting, and the piece
/// store (each change stored once, chain limit per piece, encode once). The
/// reference table is still copied whole when a region changes; Step 3
/// replaces it with copy-on-write blocks without changing this interface.

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "debugger/ttd/engine/ttdframeinput.h"
#include "debugger/ttd/engine/ttdpiecestore.h"
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
    /// Per region: piece index -> stored version, or kAbsent for a piece the
    /// session has not seen (Step 1: a whole table, shared until it changes)
    std::vector<std::shared_ptr<const std::vector<uint32_t>>> regionTables;
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

class TimeTravelEngine
{
public:
    /// A piece the session has not seen yet
    static constexpr uint32_t kAbsent = 0xFFFFFFFFu;

    /// @p store: the piece store, shareable by several sessions (D22); a store
    /// of its own when null
    explicit TimeTravelEngine(std::shared_ptr<TTDPieceStore> store = nullptr);
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

    /// endregion </Restore>

    TTDEngineHeapBreakdown HeapBreakdown() const;

    const TTDPieceStore& PieceStore() const { return *_store; }

private:
    bool _open = false;
    std::shared_ptr<TTDPieceStore> _store;
    std::vector<TTDRegionDesc> _regions;
    /// Per region: its latest contents (allocated when the region's first
    /// piece arrives), the base each new difference is computed against
    std::vector<std::vector<uint8_t>> _deltaBase;
    TTDFrameTable _frames;
    TTDStreamRegistry _streams;
    std::vector<TTDEngineCheckpoint> _checkpoints;
};

}  // namespace ttd
