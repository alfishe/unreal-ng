#include "timetravelengine.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <unordered_set>

#include "debugger/ttd/ttdcompression.h"

namespace ttd
{

namespace
{
uint32_t BlockPieces(const TTDRegionDesc& r)
{
    return r.blockPieces ? r.blockPieces : TTDRefTables::DefaultBlockPieces(r.pieces);
}
}  // namespace

TimeTravelEngine::TimeTravelEngine(std::shared_ptr<TTDPieceStore> store)
    : _store(store ? std::move(store) : std::make_shared<TTDPieceStore>()),
      _tables(std::make_unique<TTDRefTables>(*_store))
{
}

TimeTravelEngine::~TimeTravelEngine()
{
    EndSession();
}

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
    _live.resize(_regions.size());
    _inMemory.resize(_regions.size());
    _sinceSnapshot.assign(_regions.size(), {});
    _sinceSnapshotFlag.resize(_regions.size());
    for (size_t r = 0; r < _regions.size(); ++r)
    {
        _live[r].assign(_regions[r].pieces, TTDPieceStore::kNone);
        _inMemory[r].assign(_regions[r].pieces, kUnknown);
        _sinceSnapshotFlag[r].assign(_regions[r].pieces, 0);
    }
    _lastSnapshot.assign(_regions.size(), nullptr);
    _regionPayload.assign(_regions.size(), 0);
    _open = true;
    return true;
}

void TimeTravelEngine::EndSession()
{
    // Change records and full tables each hold their references; releasing
    // them frees what no other session sharing the store needs
    for (const PieceChange& c : _changes)
        _store->Release(c.id);
    for (TTDEngineCheckpoint& cp : _checkpoints)
        for (const TTDEngineCheckpoint::RegionRefs& r : cp.regions)
            if (r.snapshot)
                _tables->Release(r.snapshot);

    _open = false;
    _regions.clear();
    _deltaBase.clear();
    _changes.clear();
    _changes.shrink_to_fit();
    _live.clear();
    _inMemory.clear();
    _sinceSnapshot.clear();
    _sinceSnapshotFlag.clear();
    _lastSnapshot.clear();
    _regionPayload.clear();
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
    const auto started = std::chrono::steady_clock::now();
    _store->ResetWork();
    _lastWork = TTDEngineCaptureWork{};
    _lastWork.piecesOffered = input.changed.size();

    TTDEngineCheckpoint cp;
    cp.position = input.position;
    cp.start = input.start;
    cp.cpu = input.cpu;
    cp.chipset = input.chipset;
    if (input.deviceBlobs)
    {
        cp.deviceBlobs = *input.deviceBlobs;
        for (const auto& blob : cp.deviceBlobs)
            _lastWork.deviceBlobBytes += blob.second.size();
    }
    const size_t index = _checkpoints.size();
    cp.parent = index == 0 ? TTDEngineCheckpoint::kNoParent : static_cast<uint32_t>(index - 1);
    const bool snapshot = index % _snapshotInterval == 0;

    for (uint32_t r = 0; r < _regions.size(); ++r)
    {
        TTDEngineCheckpoint::RegionRefs refs;
        refs.region = r;
        refs.firstChange = static_cast<uint32_t>(_changes.size());
        for (const TTDChangedPiece& c : input.changed)
        {
            if (c.region != r)
                continue;
            std::vector<uint8_t>& base = _deltaBase[r];
            if (base.empty())
                base.assign(size_t(_regions[r].pieces) * kTTDPieceSize, 0);
            uint8_t* previousBytes = base.data() + size_t(c.piece) * kTTDPieceSize;
            const TTDPieceId previous = _live[r][c.piece];
            // Offered but unchanged (a dirty page rewritten with the same bytes,
            // a compare-mode region): nothing to store, nothing to copy
            if (previous != TTDPieceStore::kNone && std::memcmp(previousBytes, c.bytes, kTTDPieceSize) == 0)
            {
                _inMemory[r][c.piece] = previous;
                continue;
            }
            const TTDPieceId next = previous == TTDPieceStore::kNone ? _store->InternFirst(c.bytes)
                                                                     : _store->Intern(previous, previousBytes, c.bytes);
            std::memcpy(previousBytes, c.bytes, kTTDPieceSize);
            _lastWork.deltaBaseBytes += kTTDPieceSize;
            _inMemory[r][c.piece] = next;   // live memory holds this version's content now
            if (next == previous)
            {
                _store->Release(next);   // unchanged content: no record
                continue;
            }
            _changes.push_back({c.piece, next});   // the record takes the reference
            _regionPayload[r] += _store->PayloadSize(next);
            _live[r][c.piece] = next;
            if (!_sinceSnapshotFlag[r][c.piece])
            {
                _sinceSnapshotFlag[r][c.piece] = 1;
                _sinceSnapshot[r].push_back(c.piece);
            }
        }
        refs.changeCount = static_cast<uint32_t>(_changes.size()) - refs.firstChange;

        if (snapshot)
        {
            // A full table: the previous one, with the pieces changed since
            // then set; unchanged blocks stay shared
            TTDRefTables::Table* table = _lastSnapshot[r] ? _tables->Derive(_lastSnapshot[r])
                                                          : _tables->Create(_regions[r].pieces, BlockPieces(_regions[r]));
            for (const uint32_t piece : _sinceSnapshot[r])
            {
                const TTDPieceId id = _live[r][piece];
                _store->AddRef(id);
                _tables->Set(table, piece, id);
                _sinceSnapshotFlag[r][piece] = 0;
            }
            _sinceSnapshot[r].clear();
            refs.snapshot = table;
            _lastSnapshot[r] = table;
        }
        if (refs.changeCount > 0 || refs.snapshot)
            cp.regions.push_back(refs);
    }

    _checkpoints.push_back(std::move(cp));
    _streams.CaptureEnabled(input.position);

    const TTDPieceStore::Work& w = _store->GetWork();
    _lastWork.versionsStored = w.versionsStored;
    _lastWork.compressCalls = w.compressCalls;
    _lastWork.compressInputBytes = w.compressInputBytes;
    _lastCaptureNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
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

void TimeTravelEngine::BuildMap(size_t index, uint32_t region, std::vector<TTDPieceId>& map) const
{
    // The nearest full table at or before the checkpoint, then the changes after it
    size_t s = index;
    while (!HasFullTable(s, region))
        --s;
    const TTDRefTables::Table* table = RefsOf(s, region)->snapshot;
    map.resize(table->pieces);
    for (uint32_t p = 0; p < table->pieces; ++p)
        map[p] = _tables->Get(table, p);
    for (size_t i = s + 1; i <= index; ++i)
    {
        const TTDEngineCheckpoint::RegionRefs* refs = RefsOf(i, region);
        if (!refs)
            continue;
        for (uint32_t k = 0; k < refs->changeCount; ++k)
        {
            const PieceChange& c = _changes[refs->firstChange + k];
            map[c.piece] = c.id;
        }
    }
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
    std::vector<TTDPieceId> map;
    BuildMap(index, region, map);
    if (present)
        present->assign(map.size(), 0);
    for (uint32_t p = 0; p < map.size(); ++p)
    {
        if (map[p] == TTDPieceStore::kNone)
            continue;
        if (!_store->Decode(map[p], out + size_t(p) * kTTDPieceSize))
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

uint32_t TimeTravelEngine::VersionAt(size_t index, uint32_t region, uint32_t piece) const
{
    if (index >= _checkpoints.size() || region >= _regions.size() || piece >= _regions[region].pieces)
        return kAbsent;
    // The newest record of the piece up to the checkpoint, else the nearest full table's entry
    for (size_t i = index;; --i)
    {
        const TTDEngineCheckpoint::RegionRefs* refs = RefsOf(i, region);
        if (!refs)
            continue;
        for (uint32_t k = refs->changeCount; k-- > 0;)
            if (_changes[refs->firstChange + k].piece == piece)
                return _changes[refs->firstChange + k].id;
        if (refs->snapshot)
            return _tables->Get(refs->snapshot, piece);
    }
}

TTDRestoreResult TimeTravelEngine::RestoreToMemory(size_t index, const TTDWrittenFn& written, TTDRestoreStats* stats)
{
    TTDRestoreResult result;
    if (index >= _checkpoints.size())
    {
        result.status = TTDRestoreStatus::Damaged;
        result.message = "no such checkpoint";
        return result;
    }
    TTDRestoreStats local;
    const uint64_t linksBefore = _store->GetWork().linksDecoded;
    std::vector<TTDPieceId> map;
    for (uint32_t r = 0; r < _regions.size(); ++r)
    {
        const TTDRegionDesc& desc = _regions[r];
        if (!desc.memory && !desc.restorePiece)
            continue;
        BuildMap(index, r, map);
        uint8_t piece[kTTDPieceSize];
        for (uint32_t p = 0; p < map.size(); ++p)
        {
            const TTDPieceId target = map[p];
            if (target == TTDPieceStore::kNone)
                continue;
            if (_inMemory[r][p] == target && !(written && written(r, p)))
            {
                local.piecesSkipped++;
                continue;
            }
            uint8_t* dst = desc.restorePiece ? piece : desc.memory + size_t(p) * kTTDPieceSize;
            if (!_store->Decode(target, dst))
            {
                result.status = TTDRestoreStatus::Damaged;
                result.message = "piece " + std::to_string(p) + " of region '" + desc.name + "' failed its integrity check";
                _inMemory[r][p] = kUnknown;
                continue;
            }
            if (desc.restorePiece)
                desc.restorePiece(p, piece);
            _inMemory[r][p] = target;
            local.piecesDecoded++;
        }
        if (desc.onRestored)
            desc.onRestored();
    }
    local.linksDecoded = _store->GetWork().linksDecoded - linksBefore;
    if (stats)
        *stats = local;
    return result;
}

void TimeTravelEngine::ForgetMemory()
{
    for (auto& m : _inMemory)
        std::fill(m.begin(), m.end(), kUnknown);
}

/// endregion </Restore>

size_t TimeTravelEngine::ReferenceBytes() const
{
    size_t perCheckpoint = 0;
    for (const TTDEngineCheckpoint& cp : _checkpoints)
        perCheckpoint += cp.regions.size() * sizeof(TTDEngineCheckpoint::RegionRefs);
    return _changes.size() * sizeof(PieceChange) + _tables->HeapBytes() + perCheckpoint;
}

TTDEngineHeapBreakdown TimeTravelEngine::HeapBreakdown() const
{
    TTDEngineHeapBreakdown h;
    h.pieceVersions = _store->VersionTableBytes();
    h.piecePayload = _store->PayloadBytes();
    h.arenaSlack = _store->ArenaBytes() - _store->PayloadBytes();
    for (const auto& base : _deltaBase)
        h.deltaBase += base.capacity();
    h.referenceTables = _tables->HeapBytes() + _changes.capacity() * sizeof(PieceChange);
    for (size_t r = 0; r < _live.size(); ++r)
        h.referenceTables += _live[r].capacity() * sizeof(TTDPieceId) + _sinceSnapshot[r].capacity() * sizeof(uint32_t) +
                             _sinceSnapshotFlag[r].capacity();
    h.checkpoints = _checkpoints.capacity() * sizeof(TTDEngineCheckpoint);
    for (const TTDEngineCheckpoint& cp : _checkpoints)
    {
        h.checkpoints += cp.regions.capacity() * sizeof(cp.regions[0]);
        for (const auto& [id, blob] : cp.deviceBlobs)
            h.deviceBlobs += blob.capacity();
    }
    h.frameTable = _frames.HeapBytes();
    return h;
}

}  // namespace ttd
