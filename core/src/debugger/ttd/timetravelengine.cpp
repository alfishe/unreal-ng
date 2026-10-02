#include "timetravelengine.h"

#include <cstring>
#include <unordered_set>

#include "debugger/ttd/ttdcompression.h"

namespace ttd
{

TimeTravelEngine::TimeTravelEngine(std::shared_ptr<TTDPieceStore> store)
    : _store(store ? std::move(store) : std::make_shared<TTDPieceStore>())
{
}

TimeTravelEngine::~TimeTravelEngine() = default;

/// region <Session>

bool TimeTravelEngine::BeginSession(const std::vector<TTDRegionDesc>& regions, std::string& error)
{
    if (regions.empty())
    {
        error = "a session needs at least one region";
        return false;
    }
    std::unordered_set<uint16_t> ids;
    for (const TTDRegionDesc& r : regions)
    {
        if (r.pieces == 0 || r.bytes == 0 || r.bytes > uint64_t(r.pieces) * kTTDPieceSize)
        {
            error = "region '" + r.name + "' has an inconsistent size";
            return false;
        }
        if (!ids.insert(static_cast<uint16_t>(r.id)).second)
        {
            error = "region id " + std::to_string(static_cast<uint16_t>(r.id)) + " is listed twice";
            return false;
        }
    }
    EndSession();
    _regions = regions;
    _deltaBase.assign(_regions.size(), {});
    _open = true;
    return true;
}

void TimeTravelEngine::EndSession()
{
    // Every distinct table holds one reference per version it lists. A store
    // of its own is simply cleared; a shared one keeps other sessions' versions
    if (_store.use_count() == 1)
        _store->Clear();
    else
    {
        std::unordered_set<const std::vector<uint32_t>*> seen;
        for (const TTDEngineCheckpoint& cp : _checkpoints)
            for (const auto& table : cp.regionTables)
                if (table && seen.insert(table.get()).second)
                    for (const uint32_t id : *table)
                        if (id != kAbsent)
                            _store->Release(id);
    }

    _open = false;
    _regions.clear();
    _deltaBase.clear();
    _frames.Clear();
    _checkpoints.clear();
    _checkpoints.shrink_to_fit();
}

/// endregion </Session>

/// region <Capture>

bool TimeTravelEngine::CaptureFrame(const TTDFrameInput& input, std::string& error)
{
    if (!_open)
    {
        error = "no session";
        return false;
    }
    if (input.position.branch != 0 || input.position.tInFrame != 0)
    {
        error = "Phase 1 records frame boundaries of the trunk only";
        return false;
    }
    for (const TTDChangedPiece& c : input.changed)
        if (c.region >= _regions.size() || c.piece >= _regions[c.region].pieces || c.bytes == nullptr)
        {
            error = "changed piece out of range (region " + std::to_string(c.region) + ", piece " +
                    std::to_string(c.piece) + ")";
            return false;
        }
    if (!_frames.Append(input.position.frame, input.start))
    {
        error = "frame " + std::to_string(input.position.frame) + " does not follow the last recorded frame";
        return false;
    }

    TTDEngineCheckpoint cp;
    cp.position = input.position;
    cp.start = input.start;
    cp.cpu = input.cpu;
    cp.chipset = input.chipset;
    if (input.deviceBlobs)
        cp.deviceBlobs = *input.deviceBlobs;

    // Reference tables: start from the parent's; a region with a changed piece
    // gets its own copy (Step 3 replaces this with copy-on-write blocks)
    std::vector<std::shared_ptr<std::vector<uint32_t>>> changedTables(_regions.size());
    if (_checkpoints.empty())
    {
        cp.parent = TTDEngineCheckpoint::kNoParent;
        cp.regionTables.resize(_regions.size());
        for (size_t r = 0; r < _regions.size(); ++r)
        {
            changedTables[r] = std::make_shared<std::vector<uint32_t>>(_regions[r].pieces, kAbsent);
            cp.regionTables[r] = changedTables[r];
        }
    }
    else
    {
        cp.parent = static_cast<uint32_t>(_checkpoints.size() - 1);
        cp.regionTables = _checkpoints.back().regionTables;
    }

    for (const TTDChangedPiece& c : input.changed)
    {
        if (!changedTables[c.region])
        {
            changedTables[c.region] = std::make_shared<std::vector<uint32_t>>(*cp.regionTables[c.region]);
            // The copy holds its own reference to every version it lists
            for (const uint32_t id : *changedTables[c.region])
                if (id != kAbsent)
                    _store->AddRef(id);
            cp.regionTables[c.region] = changedTables[c.region];
        }
        std::vector<uint8_t>& base = _deltaBase[c.region];
        if (base.empty())
            base.assign(size_t(_regions[c.region].pieces) * kTTDPieceSize, 0);
        uint8_t* previousBytes = base.data() + size_t(c.piece) * kTTDPieceSize;
        uint32_t& slot = (*changedTables[c.region])[c.piece];
        const uint32_t previous = slot;
        slot = previous == kAbsent ? _store->InternFirst(c.bytes) : _store->Intern(previous, previousBytes, c.bytes);
        if (previous != kAbsent)
            _store->Release(previous);   // the table's reference moves to the new version
        std::memcpy(previousBytes, c.bytes, kTTDPieceSize);
    }

    _checkpoints.push_back(std::move(cp));
    _streams.CaptureEnabled(input.position);
    return true;
}

/// endregion </Capture>

/// region <Restore>

const TTDEngineCheckpoint* TimeTravelEngine::Checkpoint(size_t index) const
{
    return index < _checkpoints.size() ? &_checkpoints[index] : nullptr;
}

int64_t TimeTravelEngine::CheckpointIndexOf(const TTDPosition& position) const
{
    if (position.branch != 0 || position.tInFrame != 0)
        return -1;
    // Phase 1: one checkpoint per recorded frame, in frame-table order
    return _frames.IndexOf(position.frame);
}

TTDRestoreResult TimeTravelEngine::RestoreRegion(size_t index, uint32_t region, uint8_t* out,
                                                 std::vector<uint8_t>* present) const
{
    TTDRestoreResult result;
    if (index >= _checkpoints.size() || region >= _regions.size() || out == nullptr)
    {
        result.status = TTDRestoreStatus::Damaged;
        result.message = "no such checkpoint or region";
        return result;
    }
    const std::vector<uint32_t>& table = *_checkpoints[index].regionTables[region];
    if (present)
        present->assign(table.size(), 0);
    for (size_t p = 0; p < table.size(); ++p)
    {
        if (table[p] == kAbsent)
            continue;
        if (!_store->Decode(table[p], out + p * kTTDPieceSize))
        {
            result.status = TTDRestoreStatus::Damaged;
            result.message = "piece " + std::to_string(p) + " of region '" + _regions[region].name +
                             "' failed its integrity check";
            continue;
        }
        if (present)
            (*present)[p] = 1;
    }
    return result;
}

/// endregion </Restore>

TTDEngineHeapBreakdown TimeTravelEngine::HeapBreakdown() const
{
    TTDEngineHeapBreakdown h;
    h.pieceVersions = _store->VersionTableBytes();
    h.piecePayload = _store->PayloadBytes();
    h.arenaSlack = _store->ArenaBytes() - _store->PayloadBytes();
    for (const auto& base : _deltaBase)
        h.deltaBase += base.capacity();

    std::unordered_set<const void*> tables;
    h.checkpoints = _checkpoints.capacity() * sizeof(TTDEngineCheckpoint);
    for (const TTDEngineCheckpoint& cp : _checkpoints)
    {
        h.checkpoints += cp.regionTables.capacity() * sizeof(cp.regionTables[0]);
        for (const auto& t : cp.regionTables)
            if (t && tables.insert(t.get()).second)
                h.referenceTables += sizeof(*t) + t->capacity() * sizeof(uint32_t);
        for (const auto& [id, blob] : cp.deviceBlobs)
            h.deviceBlobs += blob.capacity();
    }
    h.frameTable = _frames.HeapBytes();
    return h;
}

}  // namespace ttd
