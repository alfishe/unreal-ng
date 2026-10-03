#include "timetravelengine.h"

#include "debugger/ttd/engine/ttdregiontracker.h"
#include "debugger/ttd/ttdperipheralregistry.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <unordered_set>

#include "debugger/ttd/ttdcompression.h"

namespace ttd
{

namespace
{
/// Bytes a time field's anchor takes at the end of its device's region
constexpr size_t kTimeAnchorBytes = 24;

uint64_t WidthMask(uint8_t width)
{
    return width >= 8 ? ~uint64_t(0) : (uint64_t(1) << (8 * width)) - 1;
}

uint64_t ReadLE(const uint8_t* p, uint8_t width)
{
    uint64_t v = 0;
    for (uint8_t i = 0; i < width; ++i)
        v |= uint64_t(p[i]) << (8 * i);
    return v;
}

void WriteLE(uint8_t* p, uint8_t width, uint64_t v)
{
    for (uint8_t i = 0; i < width; ++i)
        p[i] = static_cast<uint8_t>(v >> (8 * i));
}

/// A value of `width` bytes read as a signed number
int64_t SignExtend(uint64_t v, uint8_t width)
{
    if (width >= 8)
        return static_cast<int64_t>(v);
    const uint64_t sign = uint64_t(1) << (8 * width - 1);
    return static_cast<int64_t>((v ^ sign) - sign);
}

/// A residual kept on the line (wrapping at the field's width): within two
/// bytes for 4- and 8-byte fields, +-2047 for 2-byte ones; a 1-byte field
/// stays on its line whatever it does. A larger one starts a new line. Wide
/// enough for a clock quantized to a period that does not divide the frame
/// (NeoGS timers on a 48K frame step N or N-1 periods: +-3,200 ticks)
bool SmallResidual(uint64_t residual, uint8_t width)
{
    const uint64_t limit = width >= 4 ? 32767 : width == 2 ? 2047 : ~uint64_t(0);
    const uint64_t mask = WidthMask(width);
    const uint64_t negative = (0 - residual) & mask;
    return residual <= limit || negative <= limit;
}
}  // namespace


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

bool TimeTravelEngine::BeginSession(const std::vector<TTDRegionDesc>& memoryRegions,
                                    std::vector<TTDDeviceEntry> devices, std::string& error)
{
    if (memoryRegions.empty())
    {
        error = "a session needs at least one region";
        return false;
    }
    TTDDeviceTable table;
    if (!table.Build(std::move(devices), error))
        return false;
    // Each device's state as a region of its own: 4 bytes of length, then the state
    std::vector<TTDRegionDesc> regions = memoryRegions;
    std::array<int32_t, 256> deviceRegionOf;
    deviceRegionOf.fill(-1);
    for (size_t k = 0; k < table.Entries().size(); ++k)
    {
        const TTDDeviceDescriptor& d = table.Entries()[k].descriptor;
        TTDRegionDesc r;
        r.id = static_cast<TTDRegionId>(static_cast<uint16_t>(TTDRegionId::DeviceStateFirst) + k);
        r.name = "device." + d.instance;
        r.ownerType = static_cast<uint16_t>(d.legacyId);
        // Length, the state, then each time field's anchor (value, frame, step)
        r.bytes = 4 + d.stateSize + static_cast<uint32_t>(d.timeFields.size()) * kTimeAnchorBytes;
        r.pieces = (r.bytes + kTTDPieceSize - 1) / kTTDPieceSize;
        deviceRegionOf[static_cast<uint8_t>(d.legacyId)] = static_cast<int32_t>(regions.size());
        regions.push_back(r);
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
    _regionVersions.assign(_regions.size(), 0);
    _devices = std::move(table);
    _deviceRegionOf = deviceRegionOf;
    _deviceScratch.assign(_regions.size(), {});
    _timeLines.assign(_regions.size(), {});
    _timeFields.assign(_regions.size(), {});
    for (size_t k = 0; k < _devices.Entries().size(); ++k)
    {
        const TTDDeviceDescriptor& d = _devices.Entries()[k].descriptor;
        const int32_t r = _deviceRegionOf[static_cast<uint8_t>(d.legacyId)];
        _timeFields[static_cast<size_t>(r)] = d.timeFields;
        _timeLines[static_cast<size_t>(r)].assign(d.timeFields.size(), {});
    }
    _syncMissCount = 0;
    _syncMisses.clear();
    _open = true;
    return true;
}

void TimeTravelEngine::EndSession()
{
    _timeLines.clear();
    _timeFields.clear();
    _deviceRegionOf.fill(-1);
    _deviceScratch.clear();
    _devices.Clear();
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
    _regionVersions.clear();
    _frames.Clear();
    _checkpoints.clear();
    _checkpoints.shrink_to_fit();   // a deque returns its blocks
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
    _lastWork.deviceStateBytes = input.deviceStateBytes;
    _offeredByRegion.assign(_regions.size(), 0);   // no allocation once sized
    for (const TTDChangedPiece& c : input.changed)
        if (c.region < _offeredByRegion.size())
            ++_offeredByRegion[c.region];

    // FR-19: every device that runs behind the CPU has caught up to this
    // boundary (one call per such device; a miss is reported, the frame is
    // still recorded as it is)
    for (const TTDDeviceEntry& e : _devices.Entries())
    {
        int64_t offset = 0;
        if (!e.descriptor.runsBehindCpu || !e.device || e.device->TTDSyncedTime(offset))
            continue;
        if (_syncMisses.size() < 16)
            _syncMisses.push_back({input.position.frame, e.descriptor.Key(), offset});
        ++_syncMissCount;
    }

    TTDEngineCheckpoint cp;
    cp.position = input.position;
    cp.start = input.start;
    cp.cpu = input.cpu;
    cp.chipset = input.chipset;

    // Device states: each laid out in its region (length, state, zero padding)
    // and offered whole; the comparison below keeps only the pieces that changed
    std::vector<TTDChangedPiece> devicePieces;
    std::array<bool, 256> seen{};
    auto layDevice = [&](uint8_t id, const uint8_t* bytes, size_t size) {
        seen[id] = true;
        const int32_t r = _deviceRegionOf[id];
        if (r < 0)
        {
            cp.unclaimedDevices.push_back(id);
            return;
        }
        const TTDRegionDesc& desc = _regions[static_cast<size_t>(r)];
        std::vector<uint8_t>& scratch = _deviceScratch[static_cast<size_t>(r)];
        scratch.assign(size_t(desc.pieces) * kTTDPieceSize, 0);
        const std::vector<TTDTimeField>& fields = _timeFields[static_cast<size_t>(r)];
        const size_t anchors = desc.bytes - fields.size() * kTimeAnchorBytes;   // where the anchors start
        const uint32_t length = size + 4 <= anchors ? static_cast<uint32_t>(size) : 0;   // does not fit: no state
        std::memcpy(scratch.data(), &length, 4);
        if (length)
            std::memcpy(scratch.data() + 4, bytes, length);
        // Time fields: stored as the residual from their line
        std::vector<TimeLine>& lines = _timeLines[static_cast<size_t>(r)];
        for (size_t f = 0; f < fields.size(); ++f)
        {
            const TTDTimeField& tf = fields[f];
            TimeLine& line = lines[f];
            if (length && tf.offset + tf.width <= length)
            {
                uint8_t* at = scratch.data() + 4 + tf.offset;
                const uint64_t mask = WidthMask(tf.width);
                const uint64_t value = ReadLE(at, tf.width);
                const uint64_t frame = input.position.frame;
                uint64_t residual = (value - (line.value + (frame - line.frame) * line.step)) & mask;
                if (!line.valid || !SmallResidual(residual, tf.width))
                {
                    // A new line through this value. Its step: the average
                    // over the line it replaces (a clock whose step alternates
                    // or has a fraction keeps a small, non-growing residual),
                    // or the last step when that line was one frame long
                    if (!line.valid)
                        line.step = 0;
                    else if (frame - line.frame > 1)
                    {
                        const int64_t span = static_cast<int64_t>(frame - line.frame);
                        line.step = static_cast<uint64_t>(SignExtend((value - line.value) & mask, tf.width) / span) & mask;
                    }
                    else
                        line.step = (value - line.last) & mask;
                    line.value = value;
                    line.frame = frame;
                    line.valid = true;
                    residual = 0;
                }
                line.last = value;
                WriteLE(at, tf.width, residual);
            }
            uint8_t* anchor = scratch.data() + anchors + f * kTimeAnchorBytes;
            std::memcpy(anchor, &line.value, 8);
            std::memcpy(anchor + 8, &line.frame, 8);
            std::memcpy(anchor + 16, &line.step, 8);
        }
    };
    if (!input.deviceStates.empty())
    {
        for (const TTDDeviceStateInput& d : input.deviceStates)
            layDevice(d.id, d.bytes, d.size);
    }
    else if (input.deviceBlobs)
    {
        for (const auto& [id, blob] : *input.deviceBlobs)
        {
            const std::vector<uint8_t> state = TTDPeripheralRegistry::DecodeBlob(id, blob);
            layDevice(id, state.data(), state.size());
        }
    }
    for (uint32_t r = 0; r < _regions.size(); ++r)
    {
        if (!IsDeviceStateRegion(r))
            continue;
        std::vector<uint8_t>& scratch = _deviceScratch[r];
        if (!seen[static_cast<uint8_t>(_regions[r].ownerType)])
            scratch.assign(size_t(_regions[r].pieces) * kTTDPieceSize, 0);   // no state this frame: length 0
        for (uint32_t p = 0; p < _regions[r].pieces; ++p)
            devicePieces.push_back({r, p, scratch.data() + size_t(p) * kTTDPieceSize});
    }
    const uint64_t devicePayloadBefore = [&] {
        uint64_t sum = 0;
        for (uint32_t r = 0; r < _regions.size(); ++r)
            if (IsDeviceStateRegion(r))
                sum += _regionPayload[r];
        return sum;
    }();
    const size_t index = _checkpoints.size();
    cp.parent = index == 0 ? TTDEngineCheckpoint::kNoParent : static_cast<uint32_t>(index - 1);
    const bool snapshot = index % _snapshotInterval == 0;

    for (uint32_t r = 0; r < _regions.size(); ++r)
    {
        TTDEngineCheckpoint::RegionRefs refs;
        refs.region = r;
        refs.firstChange = static_cast<uint32_t>(_changes.size());
        const std::vector<TTDChangedPiece>& pieces = IsDeviceStateRegion(r) ? devicePieces : input.changed;
        for (const TTDChangedPiece& c : pieces)
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
            ++_regionVersions[r];
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

    for (uint32_t r = 0; r < _regions.size(); ++r)
        if (IsDeviceStateRegion(r))
            _lastWork.deviceBlobBytes += _regionPayload[r];
    _lastWork.deviceBlobBytes -= devicePayloadBefore;

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
            // The last piece of a region whose size is no multiple of 4 KB (the
            // SMUC EEPROM, 2 KB) is decoded aside: only its real bytes reach memory
            const size_t offset = size_t(p) * kTTDPieceSize;
            const bool partial = offset + kTTDPieceSize > desc.bytes;
            uint8_t* dst = (desc.restorePiece || partial) ? piece : desc.memory + offset;
            if (!_store->Decode(target, dst))
            {
                result.status = TTDRestoreStatus::Damaged;
                result.message = "piece " + std::to_string(p) + " of region '" + desc.name + "' failed its integrity check";
                _inMemory[r][p] = kUnknown;
                continue;
            }
            if (desc.restorePiece)
                desc.restorePiece(p, piece);
            else if (partial)
                std::memcpy(desc.memory + offset, piece, desc.bytes - offset);
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
    h.checkpoints = _checkpoints.size() * sizeof(TTDEngineCheckpoint);
    for (const TTDEngineCheckpoint& cp : _checkpoints)
    {
        h.checkpoints += cp.regions.capacity() * sizeof(cp.regions[0]);
        h.deviceBlobs += cp.unclaimedDevices.capacity();
    }
    h.frameTable = _frames.HeapBytes();
    return h;
}

bool TimeTravelEngine::DeviceState(size_t index, uint8_t id, std::vector<uint8_t>& out) const
{
    out.clear();
    const int32_t r = _deviceRegionOf[id];
    if (r < 0 || index >= _checkpoints.size())
        return false;
    const TTDRegionDesc& desc = _regions[static_cast<size_t>(r)];
    std::vector<uint8_t> bytes(size_t(desc.pieces) * kTTDPieceSize, 0);
    if (!RestoreRegion(index, static_cast<uint32_t>(r), bytes.data()).Ok())
        return false;
    const std::vector<TTDTimeField>& fields = _timeFields[static_cast<size_t>(r)];
    const size_t anchors = desc.bytes - fields.size() * kTimeAnchorBytes;
    uint32_t length = 0;
    std::memcpy(&length, bytes.data(), 4);
    if (length == 0 || length + 4 > anchors)
        return false;
    // Time fields back from their line: anchor + (frame - anchor frame) x step + residual
    const uint64_t frame = _checkpoints[index].position.frame;
    for (size_t f = 0; f < fields.size(); ++f)
    {
        const TTDTimeField& tf = fields[f];
        if (tf.offset + tf.width > length)
            continue;
        uint64_t value = 0, from = 0, step = 0;
        const uint8_t* anchor = bytes.data() + anchors + f * kTimeAnchorBytes;
        std::memcpy(&value, anchor, 8);
        std::memcpy(&from, anchor + 8, 8);
        std::memcpy(&step, anchor + 16, 8);
        uint8_t* at = bytes.data() + 4 + tf.offset;
        WriteLE(at, tf.width, (value + (frame - from) * step + ReadLE(at, tf.width)) & WidthMask(tf.width));
    }
    out.assign(bytes.begin() + 4, bytes.begin() + 4 + length);
    return true;
}

TTDRestoreResult TimeTravelEngine::RestoreDevices(size_t index, const TTDRestoreContext& context)
{
    TTDRestoreResult result;
    const TTDEngineCheckpoint* cp = Checkpoint(index);
    if (!cp)
    {
        result.status = TTDRestoreStatus::Degraded;
        result.message = "no checkpoint " + std::to_string(index);
        return result;
    }
    const std::vector<TTDDeviceEntry>& entries = _devices.Entries();
    std::vector<bool> claimed(256, false);
    for (uint32_t i : _devices.RestoreOrder())
    {
        const TTDDeviceEntry& e = entries[i];
        const TTDDeviceDescriptor& d = e.descriptor;
        const auto id = static_cast<uint8_t>(d.legacyId);
        claimed[id] = true;
        if (!e.device)
            continue;
        TTDRestoreIssue issue;
        issue.device = d.Key();
        std::vector<uint8_t> state;
        if (!DeviceState(index, id, state) || state.empty())
        {
            issue.kind = TTDRestoreIssueKind::DeviceMissingState;
            issue.action = e.device->TTDResetToPowerOn() ? TTDLiveStateAction::ResetToPowerOn
                                                         : TTDLiveStateAction::KeptLive;
            issue.detail = issue.action == TTDLiveStateAction::ResetToPowerOn
                               ? "no state at this position, reset to power-on"
                               : "no state at this position, kept its live state";
            result.Add(issue);
            continue;
        }
        bool loaded = false;
        if (e.withoutRegions)
            loaded = !state.empty() && e.withoutRegions->TTDLoadStateWithoutRegions(state.data(), state.size());
        else if (d.variableSize ? (!state.empty() && state.size() <= d.stateSize) : state.size() == d.stateSize)
        {
            e.device->TTDLoadState(state.data());
            loaded = true;
        }
        if (!loaded)
        {
            issue.kind = TTDRestoreIssueKind::SizeMismatch;
            issue.action = e.device->TTDResetToPowerOn() ? TTDLiveStateAction::ResetToPowerOn
                                                         : TTDLiveStateAction::KeptLive;
            issue.detail = "stored state of " + std::to_string(state.size()) + " bytes does not fit (" +
                           std::to_string(d.stateSize) + ")";
            result.Add(issue);
            continue;
        }
        // Restored exactly; a replay from here may still differ when the firmware does
        const uint64_t live = e.device->TTDDescribe().firmwareFingerprint;
        if (live != d.firmwareFingerprint)
        {
            issue.kind = TTDRestoreIssueKind::FirmwareDiffers;
            issue.severity = TTDRestoreStatus::NotBitExact;
            issue.detail = "recorded with another firmware image";
            result.Add(issue);
        }
    }
    for (const uint8_t id : cp->unclaimedDevices)
        if (!claimed[id])
        {
            TTDRestoreIssue issue;
            issue.kind = TTDRestoreIssueKind::DeviceNotPresent;
            issue.device.type = static_cast<TTDDeviceType>(id);
            issue.device.instance = "device " + std::to_string(id);
            issue.detail = "state recorded for a device this machine does not have";
            result.Add(issue);
        }
    for (uint32_t i : _devices.RestoreOrder())
        if (entries[i].device)
            entries[i].device->TTDAfterRestore(context);
    // FR-19 after a restore: a device that runs behind the CPU stands at the
    // restored boundary once its after-restore work is done
    for (uint32_t i : _devices.RestoreOrder())
    {
        const TTDDeviceEntry& e = entries[i];
        int64_t offset = 0;
        if (!e.descriptor.runsBehindCpu || !e.device || e.device->TTDSyncedTime(offset))
            continue;
        TTDRestoreIssue issue;
        issue.kind = TTDRestoreIssueKind::AfterRestoreFailed;
        issue.device = e.descriptor.Key();
        issue.detail = "its clock is not at the restored frame boundary (" + std::to_string(offset) +
                       " from the frame start)";
        result.Add(issue);
    }
    return result;
}

}  // namespace ttd
