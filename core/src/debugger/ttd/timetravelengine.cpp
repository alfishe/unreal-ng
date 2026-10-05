#include "timetravelengine.h"

#include "debugger/ttd/engine/ttdregiontracker.h"
#include "debugger/ttd/ttdperipheralregistry.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iterator>
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
/// bytes for 4- and 8-byte fields, +-2047 for 2-byte ones, +-15 for 1-byte
/// ones. A larger one starts a new line. Wide enough for a clock quantized to
/// a period that does not divide the frame (NeoGS timers on a 48K frame step
/// N or N-1 periods: +-3,200 ticks); narrow enough that a 1-byte timer gets
/// its step (with no limit its first line, step 0, would never be replaced)
bool SmallResidual(uint64_t residual, uint8_t width)
{
    const uint64_t limit = width >= 4 ? 32767 : width == 2 ? 2047 : 15;
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
    _busReads.StartRecording();
    _busWrites.StartRecording();
    _busVectors.StartRecording();
    _mediaReads.StartRecording();
    _open = true;
    return true;
}

bool TimeTravelEngine::SetConfiguration(uint64_t frame, TTDConfigFingerprint fingerprint)
{
    if (!_open)
        return false;
    if (!_configs.empty() && _configs.back().fingerprint == fingerprint)
        return true;
    _configs.push_back({frame, std::move(fingerprint)});
    if (_configs.size() == 1)
        return true;
    TTDEvent cut;
    cut.kind = TTDEventKind::ConfigChange;
    const uint32_t entry = static_cast<uint32_t>(_configs.size() - 1);
    std::memcpy(cut.args, &entry, sizeof(entry));
    return AppendEvent(frame, 0, cut);
}

const TTDConfigFingerprint* TimeTravelEngine::ConfigurationAt(size_t index) const
{
    const TTDEngineCheckpoint* cp = Checkpoint(index);
    if (!cp)
        return nullptr;
    const TTDConfigFingerprint* found = nullptr;
    for (const TTDConfigEntry& e : _configs)
        if (e.frame <= cp->position.frame)
            found = &e.fingerprint;
    return found;
}

TTDRestoreResult TimeTravelEngine::CheckConfiguration(size_t index, const TTDConfigFingerprint& live,
                                                      bool forReplay) const
{
    TTDRestoreResult result;
    const TTDConfigFingerprint* recorded = ConfigurationAt(index);
    if (!recorded)
        return result;
    for (const TTDFingerprintDiff& d : Compare(*recorded, live))
    {
        if (!forReplay && !d.affectsRestore)
            continue;
        TTDRestoreIssue issue;
        issue.kind = TTDRestoreIssueKind::ConfigurationDiffers;
        issue.severity = d.affectsRestore ? TTDRestoreStatus::Degraded : TTDRestoreStatus::NotBitExact;
        issue.detail = d.field + ": recorded " + d.recorded + ", this machine " + d.live;
        result.Add(std::move(issue));
    }
    return result;
}

void TimeTravelEngine::NoteMediaVersion(const std::string& slot, const std::string& format, bool hasVersions,
                                        const TTDMediaVersion& version)
{
    if (!_open)
        return;
    size_t i = 0;
    while (i < _mediaSlots.size() && _mediaSlots[i].slot != slot)
        ++i;
    if (i == _mediaSlots.size())
        _mediaSlots.push_back({slot, format, hasVersions, {}});
    TTDMediaSlot& s = _mediaSlots[i];
    s.format = format;
    s.hasVersions = hasVersions;
    for (auto& pending : _pendingMedia)
        if (pending.first == i)
        {
            pending.second = version;
            return;
        }
    if (s.changes.empty() || s.changes.back().second != version)
        _pendingMedia.emplace_back(static_cast<uint32_t>(i), version);
}

bool TimeTravelEngine::MediaVersionAt(size_t index, size_t slot, TTDMediaVersion& out) const
{
    if (slot >= _mediaSlots.size())
        return false;
    const auto& changes = _mediaSlots[slot].changes;
    auto it = std::upper_bound(changes.begin(), changes.end(), static_cast<uint32_t>(index),
                               [](uint32_t i, const std::pair<uint32_t, TTDMediaVersion>& c) { return i < c.first; });
    if (it == changes.begin())
        return false;
    out = std::prev(it)->second;
    return true;
}

bool TimeTravelEngine::AppendEvent(uint64_t frame, uint64_t tInFrame, TTDEvent ev)
{
    TTDMachineTime start = 0;
    if (!_open || _frames.Empty())
    {
        _payloads.Release(ev.payload);
        return false;
    }
    if (!_frames.Start(frame, start))
    {
        // After the last recorded frame: placed by the last frame's length
        const uint64_t last = _frames.LastFrame();
        TTDMachineTime lastStart = 0, before = 0;
        _frames.Start(last, lastStart);
        if (frame < last || _frames.Count() < 2 || !_frames.Start(_frames.FirstFrame(), before))
        {
            _payloads.Release(ev.payload);
            return false;
        }
        const uint64_t frames = last - _frames.FirstFrame();
        const TTDMachineTime length = frames ? (lastStart - before) / frames : 0;
        start = lastStart + (frame - last) * length;
    }
    ev.machineTime = start + tInFrame;
    return AppendTimedEvent(ev);
}

bool TimeTravelEngine::AppendTimedEvent(const TTDEvent& ev)
{
    if (!_events.Append(ev))
        return false;
    if (ev.kind == TTDEventKind::InterruptFrame)
    {
        uint64_t rzxFrame = 0;
        std::memcpy(&rzxFrame, ev.args, sizeof(rzxFrame));
        _rzxFrames.emplace_back(rzxFrame, ev.machineTime);
    }
    return true;
}

bool TimeTravelEngine::RzxFrameTime(uint64_t rzxFrame, TTDMachineTime& at) const
{
    auto it = std::lower_bound(_rzxFrames.begin(), _rzxFrames.end(), rzxFrame,
                               [](const std::pair<uint64_t, TTDMachineTime>& f, uint64_t n) { return f.first < n; });
    // Before the first fact only an exact frame: frames done before the session also lie below it
    if (it == _rzxFrames.end() || (it == _rzxFrames.begin() && it->first != rzxFrame))
        return false;
    at = it->second;
    return true;
}

TTDReplaySource TimeTravelEngine::ReplaySourceAt(TTDMachineTime t) const
{
    TTDReplaySource source = TTDReplaySource::LiveInput;
    for (const TTDEvent& ev : _events.Events())
    {
        if (ev.machineTime > t)
            break;
        if (ev.kind == TTDEventKind::ReplaySourceChange)
            source = static_cast<TTDReplaySource>(ev.args[0]);
    }
    return source;
}

size_t TimeTravelEngine::BindLive(const std::vector<TTDRegionDesc>& liveRegions,
                                  const std::vector<TTDDeviceEntry>& liveDevices, std::string* unbound)
{
    size_t missing = 0;
    auto note = [&](const std::string& what) {
        ++missing;
        if (unbound)
            *unbound += (unbound->empty() ? "" : ", ") + what;
    };
    for (uint32_t r = 0; r < _regions.size(); ++r)
    {
        if (IsDeviceStateRegion(r))
            continue;
        TTDRegionDesc& desc = _regions[r];
        const TTDRegionDesc* live = nullptr;
        for (const TTDRegionDesc& l : liveRegions)
            if (l.id == desc.id && l.pieces == desc.pieces && l.bytes == desc.bytes)
                live = &l;
        if (!live)
        {
            note("region " + desc.name);
            continue;
        }
        desc.memory = live->memory;
        desc.restorePiece = live->restorePiece;
        desc.onRestored = live->onRestored;
    }
    for (size_t i = 0; i < _devices.Entries().size(); ++i)
    {
        const TTDDeviceEntry& e = _devices.Entries()[i];
        const TTDDeviceEntry* live = nullptr;
        for (const TTDDeviceEntry& l : liveDevices)
            if (l.descriptor.legacyId == e.descriptor.legacyId)
                live = &l;
        if (!live)
        {
            note("device " + e.descriptor.instance);
            continue;
        }
        _devices.Bind(i, live->device, live->withoutRegions);
    }
    ForgetMemory();   // live memory is not what the engine last wrote
    return missing;
}

bool TimeTravelEngine::PositionOf(TTDMachineTime t, TTDPosition& out) const
{
    uint64_t frame = 0;
    TTDMachineTime start = 0;
    if (!_frames.FrameAt(t, frame) || !_frames.Start(frame, start))
        return false;
    out = TTDPosition{0, frame, t - start};
    return true;
}

void TimeTravelEngine::EndSession()
{
    _readOnly = false;
    _segments.clear();
    _streamCopies.clear();
    _configs.clear();
    _mediaSlots.clear();
    _pendingMedia.clear();
    _events.Clear();
    _rzxFrames.clear();
    _payloads.Clear();
    _busReads.Clear();
    _busWrites.Clear();
    _busVectors.Clear();
    _mediaReads.Clear();
    _writes.Clear();
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
    _cpBase = 0;
    _changeBase = 0;
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
    if (_readOnly)
    {
        error = "a session loaded from a file is not continued";
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
    cp.busReadCursor = _busReads.Size();
    cp.busWriteCursor = _busWrites.Size();
    cp.mediaReadCursor = _mediaReads.Size();
    cp.busVectorCursor = _busVectors.Size();
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
    const size_t index = _cpBase + _checkpoints.size();
    cp.parent = index == 0 ? TTDEngineCheckpoint::kNoParent : static_cast<uint32_t>(index - 1);
    // A segment starts here (D41): every piece is stored whole, so nothing in
    // it needs an earlier segment and a ring can drop that one whole
    cp.baseline = _segments.empty() ||
                  (_policy.segmentFrames > 0 && input.position.frame - _segments.back().firstFrame >= _policy.segmentFrames);
    if (cp.baseline)
        BeginSegment(input.position.frame);
    const bool snapshot = cp.baseline || index % _snapshotInterval == 0;
    std::vector<TTDChangedPiece> baselinePieces;

    for (uint32_t r = 0; r < _regions.size(); ++r)
    {
        TTDEngineCheckpoint::RegionRefs refs;
        refs.region = r;
        refs.firstChange = static_cast<uint32_t>(_changeBase + _changes.size());
        const std::vector<TTDChangedPiece>* pieces = IsDeviceStateRegion(r) ? &devicePieces : &input.changed;
        if (cp.baseline && index > 0)
        {
            // Every piece the session knows, offered now or as last captured
            std::vector<const uint8_t*> bytesOf(_regions[r].pieces, nullptr);
            for (uint32_t p = 0; p < _regions[r].pieces; ++p)
                if (_live[r][p] != TTDPieceStore::kNone)
                    bytesOf[p] = _deltaBase[r].data() + size_t(p) * kTTDPieceSize;
            for (const TTDChangedPiece& c : *pieces)
                if (c.region == r)
                    bytesOf[c.piece] = c.bytes;
            baselinePieces.clear();
            for (uint32_t p = 0; p < _regions[r].pieces; ++p)
                if (bytesOf[p])
                    baselinePieces.push_back({r, p, bytesOf[p]});
            pieces = &baselinePieces;
        }
        for (const TTDChangedPiece& c : *pieces)
        {
            if (c.region != r)
                continue;
            if (cp.baseline && index > 0)
            {
                // Whole, never a difference from the previous segment's version
                std::vector<uint8_t>& base = _deltaBase[r];
                if (base.empty())
                    base.assign(size_t(_regions[r].pieces) * kTTDPieceSize, 0);
                if (base.data() + size_t(c.piece) * kTTDPieceSize != c.bytes)
                    std::memcpy(base.data() + size_t(c.piece) * kTTDPieceSize, c.bytes, kTTDPieceSize);
                const TTDPieceId next = _store->InternFirst(c.bytes);
                _inMemory[r][c.piece] = next;
                NoteChange(r, c.piece, next);
                continue;
            }
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
            NoteChange(r, c.piece, next);   // the record takes the reference
        }
        CloseRegion(cp, refs, snapshot, cp.baseline);
    }

    for (uint32_t r = 0; r < _regions.size(); ++r)
        if (IsDeviceStateRegion(r))
            _lastWork.deviceBlobBytes += _regionPayload[r];
    _lastWork.deviceBlobBytes -= devicePayloadBefore;

    const uint32_t cpIndex = static_cast<uint32_t>(index);
    for (const auto& [slot, version] : _pendingMedia)
        _mediaSlots[slot].changes.emplace_back(cpIndex, version);
    _pendingMedia.clear();
    _checkpoints.push_back(std::move(cp));
    _streams.CaptureEnabled(input.position);
    ReleaseHistory();

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

void TimeTravelEngine::NoteChange(uint32_t r, uint32_t piece, TTDPieceId next)
{
    _changes.push_back({piece, next});
    _regionPayload[r] += _store->PayloadSize(next);
    ++_regionVersions[r];
    _live[r][piece] = next;
    if (!_sinceSnapshotFlag[r][piece])
    {
        _sinceSnapshotFlag[r][piece] = 1;
        _sinceSnapshot[r].push_back(piece);
    }
}

void TimeTravelEngine::CloseRegion(TTDEngineCheckpoint& cp, TTDEngineCheckpoint::RegionRefs& refs, bool snapshot,
                                   bool fresh)
{
    const uint32_t r = refs.region;
    refs.changeCount = static_cast<uint32_t>(_changeBase + _changes.size() - refs.firstChange);
    if (snapshot)
    {
        // A full table: the previous one, with the pieces changed since
        // then set; unchanged blocks stay shared. A segment's baseline starts
        // a table of its own, so nothing is shared with the segment before
        TTDRefTables::Table* table = _lastSnapshot[r] && !fresh
                                         ? _tables->Derive(_lastSnapshot[r])
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

bool TimeTravelEngine::ImportCheckpoint(TTDEngineCheckpoint cp, const std::vector<TTDImportedChange>& changes,
                                        std::string& error)
{
    if (!_open)
    {
        error = "no session";
        return false;
    }
    if (!_frames.Append(cp.position.frame, cp.start))
    {
        error = "frame " + std::to_string(cp.position.frame) + " does not follow the last recorded frame";
        return false;
    }
    _readOnly = true;   // capture state (the delta base) is not in a file: a loaded session is not continued
    const size_t index = _cpBase + _checkpoints.size();
    cp.parent = index == 0 ? TTDEngineCheckpoint::kNoParent : static_cast<uint32_t>(index - 1);
    cp.regions.clear();
    if (_segments.empty())
        cp.baseline = true;
    if (cp.baseline)
        BeginSegment(cp.position.frame);
    const bool snapshot = cp.baseline || index % _snapshotInterval == 0;
    size_t next = 0;
    for (uint32_t r = 0; r < _regions.size(); ++r)
    {
        TTDEngineCheckpoint::RegionRefs refs;
        refs.region = r;
        refs.firstChange = static_cast<uint32_t>(_changeBase + _changes.size());
        for (; next < changes.size() && changes[next].region == r; ++next)
        {
            const TTDImportedChange& c = changes[next];
            if (c.piece >= _regions[r].pieces || c.id == TTDPieceStore::kNone)
            {
                error = "change out of range (region " + std::to_string(r) + ", piece " + std::to_string(c.piece) + ")";
                return false;
            }
            _inMemory[r][c.piece] = kUnknown;
            NoteChange(r, c.piece, c.id);   // the record takes the import's reference
        }
        CloseRegion(cp, refs, snapshot, cp.baseline);
    }
    if (next != changes.size())
    {
        error = "changes are not sorted by region";
        return false;
    }
    _checkpoints.push_back(std::move(cp));
    return true;
}

const TTDEngineCheckpoint* TimeTravelEngine::Checkpoint(size_t index) const
{
    return HasCheckpoint(index) ? &CpAt(index) : nullptr;
}

void TimeTravelEngine::AddFrameStreamCopy(uint32_t stream, uint64_t frame, const uint8_t* data, size_t size)
{
    _streamCopies[stream].push_back({frame, std::vector<uint8_t>(data, data + size)});
}

bool TimeTravelEngine::FrameStreamCopy(uint32_t stream, uint64_t frame, std::vector<uint8_t>& out) const
{
    const auto it = _streamCopies.find(stream);
    if (it == _streamCopies.end())
        return false;
    for (const FrameStreamCopy_& c : it->second)
        if (c.frame == frame)
        {
            out = c.bytes;
            return true;
        }
    return false;
}

void TimeTravelEngine::DropFrameStreamCopiesBefore(uint64_t frame)
{
    for (auto& [stream, copies] : _streamCopies)
        while (!copies.empty() && copies.front().frame < frame)
            copies.pop_front();
}

void TimeTravelEngine::BeginSegment(uint64_t frame)
{
    TTDSegmentInfo s;
    s.firstCheckpoint = _cpBase + _checkpoints.size();
    s.firstChange = _changeBase + _changes.size();
    s.firstFrame = frame;
    _segments.push_back(s);
}

void TimeTravelEngine::ReleaseHistory()
{
    if (_policy.mode != TTDHistoryMode::Ring || _frames.Empty())
        return;
    const uint64_t last = _frames.LastFrame();
    while (_segments.size() >= 2 && last - _segments[1].firstFrame >= _policy.windowFrames)
        DropOldestSegment();
}

void TimeTravelEngine::DropOldestSegment()
{
    const TTDSegmentInfo next = _segments[1];
    const size_t checkpoints = next.firstCheckpoint - _cpBase;
    const size_t changes = next.firstChange - _changeBase;

    // The versions its change records hold, its full tables
    for (size_t k = 0; k < changes; ++k)
        _store->Release(_changes[k].id);
    _changes.erase(_changes.begin(), _changes.begin() + static_cast<std::ptrdiff_t>(changes));
    _changeBase += changes;
    for (size_t i = 0; i < checkpoints; ++i)
        for (const TTDEngineCheckpoint::RegionRefs& r : _checkpoints[i].regions)
            if (r.snapshot)
                _tables->Release(r.snapshot);
    _checkpoints.erase(_checkpoints.begin(), _checkpoints.begin() + static_cast<std::ptrdiff_t>(checkpoints));
    _cpBase += checkpoints;
    _frames.DropFront(checkpoints);

    // Events, the RZX frame index and the bus journals before the new start
    const TTDEngineCheckpoint& head = _checkpoints.front();
    _events.DropBefore(head.start);
    _rzxFrames.erase(_rzxFrames.begin(),
                     std::lower_bound(_rzxFrames.begin(), _rzxFrames.end(), head.start,
                                      [](const std::pair<uint64_t, TTDMachineTime>& f, TTDMachineTime t) {
                                          return f.second < t;
                                      }));
    _busReads.DropBefore(head.busReadCursor);
    _busWrites.DropBefore(head.busWriteCursor);
    _busVectors.DropBefore(head.busVectorCursor);
    DropFrameStreamCopiesBefore(head.position.frame);
    _segments.pop_front();
}

int64_t TimeTravelEngine::CheckpointIndexOf(const TTDPosition& position) const
{
    if (position.branch != 0 || position.tInFrame != 0)
        return -1;
    // Phase 1: one checkpoint per recorded frame, in frame-table order
    return _frames.IndexOf(position.frame);
}

int64_t TimeTravelEngine::CheckpointAtOrBefore(uint64_t frame) const
{
    // Phase 1: one checkpoint per recorded frame, in frame-table order
    return _frames.IndexAtOrBefore(frame);
}

bool TimeTravelEngine::DropOldestHeldSegment()
{
    if (_segments.size() < 2)
        return false;
    DropOldestSegment();
    return true;
}

bool TimeTravelEngine::TruncateAfter(size_t index, const TTDPosition& cut, std::string& error)
{
    if (!_open)
    {
        error = "no session";
        return false;
    }
    if (!HasCheckpoint(index))
    {
        error = "checkpoint " + std::to_string(index) + " is not held";
        return false;
    }
    const TTDEngineCheckpoint& keep = CpAt(index);
    if (cut.branch != 0 || cut.frame != keep.position.frame)
    {
        error = "the cut lies outside checkpoint " + std::to_string(index) + "'s frame";
        return false;
    }
    const TTDMachineTime cutTime = keep.start + cut.tInFrame;

    // The later checkpoints: their change records (the newest ones, in
    // checkpoint order), their full tables, their frames
    const size_t count = _cpBase + _checkpoints.size();
    size_t firstDropped = _changeBase + _changes.size();
    for (size_t i = index + 1; i < count; ++i)
        for (const TTDEngineCheckpoint::RegionRefs& r : CpAt(i).regions)
        {
            if (r.changeCount > 0)
                firstDropped = std::min<size_t>(firstDropped, r.firstChange);
            if (r.snapshot)
                _tables->Release(r.snapshot);
        }
    for (size_t k = firstDropped; k < _changeBase + _changes.size(); ++k)
        _store->Release(ChangeAt(k).id);
    _changes.resize(firstDropped - _changeBase);
    _checkpoints.resize(index + 1 - _cpBase);
    _frames.DropBack(count - index - 1);
    while (_segments.back().firstCheckpoint > index)
        _segments.pop_back();

    // What the later checkpoints carried
    for (TTDMediaSlot& slot : _mediaSlots)
        while (!slot.changes.empty() && slot.changes.back().first > index)
            slot.changes.pop_back();
    _pendingMedia.clear();
    while (_configs.size() > 1 && _configs.back().frame > keep.position.frame)
        _configs.pop_back();
    for (auto& [stream, copies] : _streamCopies)
        while (!copies.empty() && copies.back().frame > keep.position.frame)
            copies.pop_back();

    // Records after the cut
    _events.DropAfter(cutTime);
    while (!_rzxFrames.empty() && _rzxFrames.back().second > cutTime)
        _rzxFrames.pop_back();
    const TTDTimePoint after{cut.frame, static_cast<uint32_t>(cut.tInFrame + 1)};
    _busReads.TruncateTo(_busReads.LowerBound(after));
    _busWrites.TruncateTo(_busWrites.LowerBound(after));
    _busVectors.TruncateTo(_busVectors.LowerBound(after));
    _mediaReads.TruncateTo(_mediaReads.CountUpTo(cut.frame, static_cast<uint32_t>(cut.tInFrame)));

    // Capture continues from the kept checkpoint: every piece's version and
    // content as it was there, the pieces changed since its region's last
    // full table, and that table
    std::vector<TTDPieceId> map;
    for (uint32_t r = 0; r < _regions.size(); ++r)
    {
        BuildMap(index, r, map);
        std::copy(map.begin(), map.end(), _live[r].begin());
        std::vector<uint8_t>& base = _deltaBase[r];
        if (base.empty())
            base.assign(size_t(_regions[r].pieces) * kTTDPieceSize, 0);
        for (uint32_t p = 0; p < map.size(); ++p)
            if (map[p] != TTDPieceStore::kNone && !_store->Decode(map[p], base.data() + size_t(p) * kTTDPieceSize))
            {
                error = "region " + std::to_string(r) + " piece " + std::to_string(p) + " does not decode";
                return false;
            }
        size_t s = index;
        while (!HasFullTable(s, r))
            --s;
        _lastSnapshot[r] = RefsOf(s, r)->snapshot;
        for (const uint32_t piece : _sinceSnapshot[r])
            _sinceSnapshotFlag[r][piece] = 0;
        _sinceSnapshot[r].clear();
        for (size_t i = s + 1; i <= index; ++i)
            if (const TTDEngineCheckpoint::RegionRefs* refs = RefsOf(i, r))
                for (uint32_t k = 0; k < refs->changeCount; ++k)
                {
                    const uint32_t piece = ChangeAt(refs->firstChange + k).piece;
                    if (!_sinceSnapshotFlag[r][piece])
                    {
                        _sinceSnapshotFlag[r][piece] = 1;
                        _sinceSnapshot[r].push_back(piece);
                    }
                }
    }
    // Device time fields start new lines (each checkpoint stores its anchors)
    for (std::vector<TimeLine>& lines : _timeLines)
        for (TimeLine& line : lines)
            line.valid = false;
    ForgetMemory();
    // A loaded session continues from here too: the capture state is rebuilt
    _readOnly = false;
    return true;
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
            const PieceChange& c = ChangeAt(refs->firstChange + k);
            map[c.piece] = c.id;
        }
    }
}

TTDRestoreResult TimeTravelEngine::RestoreRegion(size_t index, uint32_t region, uint8_t* out,
                                                 std::vector<uint8_t>* present) const
{
    TTDRestoreResult result;
    if (!HasCheckpoint(index) || region >= _regions.size() || out == nullptr)
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
            result.Add(DamageIssue(index, region, p));
            continue;
        }
        if (present)
            (*present)[p] = 1;
    }
    return result;
}

uint32_t TimeTravelEngine::VersionAt(size_t index, uint32_t region, uint32_t piece) const
{
    if (!HasCheckpoint(index) || region >= _regions.size() || piece >= _regions[region].pieces)
        return kAbsent;
    // The newest record of the piece up to the checkpoint, else the nearest full table's entry
    for (size_t i = index;; --i)
    {
        const TTDEngineCheckpoint::RegionRefs* refs = RefsOf(i, region);
        if (!refs)
            continue;
        for (uint32_t k = refs->changeCount; k-- > 0;)
            if (ChangeAt(refs->firstChange + k).piece == piece)
                return ChangeAt(refs->firstChange + k).id;
        if (refs->snapshot)
            return _tables->Get(refs->snapshot, piece);
    }
}

TTDRestoreResult TimeTravelEngine::RestoreToMemory(size_t index, const TTDWrittenFn& written, TTDRestoreStats* stats)
{
    TTDRestoreResult result;
    if (!HasCheckpoint(index))
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
                result.Add(DamageIssue(index, r, p));
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
    // A heap block is at least 16 bytes and a multiple of 16 on the common
    // allocators: the small per-checkpoint vectors are counted as allocated
    auto block = [](size_t bytes) { return bytes ? (bytes + 15) & ~size_t(15) : 0; };
#ifdef _LIBCPP_VERSION
    // libc++ keeps a deque of elements over 256 bytes in blocks of 16, one
    // block ahead of the last element
    h.checkpoints = (_checkpoints.size() / 16 + 1) * 16 * sizeof(TTDEngineCheckpoint);
#else
    h.checkpoints = _checkpoints.size() * sizeof(TTDEngineCheckpoint);
#endif
    for (const TTDEngineCheckpoint& cp : _checkpoints)
    {
        h.checkpoints += block(cp.regions.capacity() * sizeof(cp.regions[0]));
        h.deviceBlobs += block(cp.unclaimedDevices.capacity());
    }
    h.frameTable = _frames.HeapBytes();
    h.eventLog = _events.Events().capacity() * sizeof(TTDEvent) + _payloads.HeapBytes() +
                 _rzxFrames.capacity() * sizeof(_rzxFrames[0]);
    h.portReads = _busReads.HeapBytes() - _busReads.CompressedSlackBytes();
    h.portWrites = _busWrites.HeapBytes() - _busWrites.CompressedSlackBytes();
    h.portJournalSlack = _busReads.CompressedSlackBytes() + _busWrites.CompressedSlackBytes();
    h.mediaReads = _mediaReads.HeapBytes();
    h.busVectors = _busVectors.HeapBytes();
    h.writeJournal = _writes.HeapBytes();
    for (const auto& [stream, copies] : _streamCopies)
        for (const FrameStreamCopy_& c : copies)
            h.frameStreams += c.bytes.capacity() + sizeof(c);

    size_t& b = h.bookkeeping;
    b += _regions.capacity() * sizeof(TTDRegionDesc);
    for (const TTDRegionDesc& r : _regions)
        b += r.name.capacity() + r.ownerInstance.capacity();
    b += _inMemory.capacity() * sizeof(_inMemory[0]) + _live.capacity() * sizeof(_live[0]) +
         _sinceSnapshot.capacity() * sizeof(_sinceSnapshot[0]) + _sinceSnapshotFlag.capacity() * sizeof(_sinceSnapshotFlag[0]) +
         _deltaBase.capacity() * sizeof(_deltaBase[0]);
    for (const auto& v : _inMemory)
        b += v.capacity() * sizeof(TTDPieceId);
    for (const auto& v : _deviceScratch)
        b += v.capacity();
    b += _deviceScratch.capacity() * sizeof(_deviceScratch[0]);
    for (const auto& v : _timeLines)
        b += v.capacity() * sizeof(TimeLine);
    for (const auto& v : _timeFields)
        b += v.capacity() * sizeof(TTDTimeField);
    b += _timeLines.capacity() * sizeof(_timeLines[0]) + _timeFields.capacity() * sizeof(_timeFields[0]);
    b += (_regionPayload.capacity() + _regionVersions.capacity()) * sizeof(uint64_t) +
         _lastSnapshot.capacity() * sizeof(void*) + _offeredByRegion.capacity() * sizeof(uint32_t);
    for (const TTDConfigEntry& e : _configs)
        for (const TTDConfigField& f : e.fingerprint.fields)
            b += sizeof(f) + f.name.capacity();
    b += _configs.capacity() * sizeof(TTDConfigEntry);
    for (const TTDMediaSlot& m : _mediaSlots)
        b += sizeof(m) + m.slot.capacity() + m.format.capacity() + m.changes.capacity() * sizeof(m.changes[0]);
    b += _segments.size() * sizeof(TTDSegmentInfo) + _syncMisses.capacity() * sizeof(TTDSyncMiss);
    return h;
}

bool TimeTravelEngine::DeviceState(size_t index, uint8_t id, std::vector<uint8_t>& out) const
{
    return ReadDeviceState(index, id, out) == DeviceStateRead::Ok;
}

TimeTravelEngine::DeviceStateRead TimeTravelEngine::ReadDeviceState(size_t index, uint8_t id,
                                                                    std::vector<uint8_t>& out,
                                                                    uint32_t* damagedPiece) const
{
    out.clear();
    const int32_t r = _deviceRegionOf[id];
    if (r < 0 || !HasCheckpoint(index))
        return DeviceStateRead::Missing;
    const TTDRegionDesc& desc = _regions[static_cast<size_t>(r)];
    std::vector<uint8_t> bytes(size_t(desc.pieces) * kTTDPieceSize, 0);
    std::vector<TTDPieceId> map;
    BuildMap(index, static_cast<uint32_t>(r), map);
    for (uint32_t p = 0; p < map.size(); ++p)
        if (map[p] != TTDPieceStore::kNone && !_store->Decode(map[p], bytes.data() + size_t(p) * kTTDPieceSize))
        {
            if (damagedPiece)
                *damagedPiece = p;
            return DeviceStateRead::Damaged;
        }
    const std::vector<TTDTimeField>& fields = _timeFields[static_cast<size_t>(r)];
    const size_t anchors = desc.bytes - fields.size() * kTimeAnchorBytes;
    uint32_t length = 0;
    std::memcpy(&length, bytes.data(), 4);
    if (length == 0 || length + 4 > anchors)
        return DeviceStateRead::Missing;
    // Time fields back from their line: anchor + (frame - anchor frame) x step + residual
    const uint64_t frame = CpAt(index).position.frame;
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
    return DeviceStateRead::Ok;
}

const TTDDeviceEntry* TimeTravelEngine::DeviceOfRegion(uint32_t region) const
{
    for (const TTDDeviceEntry& e : _devices.Entries())
        if (_deviceRegionOf[static_cast<uint8_t>(e.descriptor.legacyId)] == static_cast<int32_t>(region))
            return &e;
    return nullptr;
}

TTDRestoreIssue TimeTravelEngine::DamageIssue(size_t index, uint32_t region, uint32_t piece) const
{
    // The neighbouring checkpoints whose version of the piece fails too: the
    // same version, or a difference that depends on it (decoded once per version)
    std::vector<uint8_t> scratch(kTTDPieceSize);
    auto fails = [&](size_t i, uint32_t& lastId, bool& lastFails) {
        const uint32_t id = VersionAt(i, region, piece);
        if (id != lastId)
        {
            lastId = id;
            lastFails = id != kAbsent && !_store->Decode(id, scratch.data());
        }
        return lastFails;
    };
    size_t first = index, last = index;
    uint32_t id = kAbsent;
    bool bad = false;
    while (first > _cpBase && fails(first - 1, id, bad))
        --first;
    id = kAbsent;
    bad = false;
    while (HasCheckpoint(last + 1) && fails(last + 1, id, bad))
        ++last;

    TTDRestoreIssue issue;
    issue.kind = TTDRestoreIssueKind::DataDamaged;
    issue.severity = TTDRestoreStatus::Damaged;
    issue.firstFrame = CpAt(first).position.frame;
    issue.lastFrame = CpAt(last).position.frame;
    const std::string frames = " (frames " + std::to_string(issue.firstFrame) + "-" + std::to_string(issue.lastFrame) + ")";
    if (const TTDDeviceEntry* e = DeviceOfRegion(region))
    {
        issue.device = e->descriptor.Key();
        issue.detail = "stored state failed its integrity check" + frames;
    }
    else
        issue.detail = "piece " + std::to_string(piece) + " of region '" + _regions[region].name +
                       "' failed its integrity check" + frames;
    return issue;
}

TTDRestoreResult TimeTravelEngine::CheckSession() const
{
    TTDRestoreResult result;
    size_t unlisted = 0;
    auto add = [&](TTDRestoreIssue issue) {
        if (result.issues.size() < 64)
            result.Add(std::move(issue));
        else
        {
            if (static_cast<uint8_t>(issue.severity) > static_cast<uint8_t>(result.status))
                result.status = issue.severity;
            ++unlisted;
        }
    };
    if (_checkpoints.empty())
        return result;
    const size_t count = _cpBase + _checkpoints.size();
    std::vector<uint8_t> scratch(kTTDPieceSize);
    std::unordered_map<TTDPieceId, bool> decodes;   // version -> decodes (each checked once)
    auto ok = [&](TTDPieceId id) {
        if (id == TTDPieceStore::kNone)
            return true;
        auto it = decodes.find(id);
        if (it != decodes.end())
            return it->second;
        return decodes[id] = _store->Decode(id, scratch.data());
    };
    auto frameOf = [&](size_t i) { return CpAt(i).position.frame; };

    for (uint32_t r = 0; r < _regions.size(); ++r)
    {
        const TTDDeviceEntry* device = DeviceOfRegion(r);
        std::vector<TTDPieceId> map;
        BuildMap(_cpBase, r, map);
        std::vector<int64_t> damagedFrom(map.size(), -1);
        int64_t missingFrom = -1;   // a device region: checkpoints whose state length is 0
        auto close = [&](uint32_t p, size_t lastIndex) {
            TTDRestoreIssue issue = DamageIssue(static_cast<size_t>(damagedFrom[p]), r, p);
            issue.firstFrame = frameOf(static_cast<size_t>(damagedFrom[p]));
            issue.lastFrame = frameOf(lastIndex);
            add(std::move(issue));
            damagedFrom[p] = -1;
        };
        auto lengthIsZero = [&]() {
            if (map.empty() || map[0] == TTDPieceStore::kNone || !ok(map[0]))
                return false;
            _store->Decode(map[0], scratch.data());
            uint32_t length = 0;
            std::memcpy(&length, scratch.data(), 4);
            return length == 0;
        };
        auto closeMissing = [&](size_t lastIndex) {
            TTDRestoreIssue issue;
            issue.kind = TTDRestoreIssueKind::DeviceMissingState;
            issue.device = device->descriptor.Key();
            issue.firstFrame = frameOf(static_cast<size_t>(missingFrom));
            issue.lastFrame = frameOf(lastIndex);
            issue.detail = "no state in frames " + std::to_string(issue.firstFrame) + "-" + std::to_string(issue.lastFrame);
            add(std::move(issue));
            missingFrom = -1;
        };
        for (uint32_t p = 0; p < map.size(); ++p)
            if (!ok(map[p]))
                damagedFrom[p] = static_cast<int64_t>(_cpBase);
        if (device && lengthIsZero())
            missingFrom = static_cast<int64_t>(_cpBase);
        for (size_t i = _cpBase + 1; i < count; ++i)
        {
            const TTDEngineCheckpoint::RegionRefs* refs = RefsOf(i, r);
            if (!refs || refs->changeCount == 0)
                continue;
            for (uint32_t k = 0; k < refs->changeCount; ++k)
            {
                const PieceChange& c = ChangeAt(refs->firstChange + k);
                map[c.piece] = c.id;
                const bool bad = !ok(c.id);
                if (bad && damagedFrom[c.piece] < 0)
                    damagedFrom[c.piece] = static_cast<int64_t>(i);
                else if (!bad && damagedFrom[c.piece] >= 0)
                    close(c.piece, i - 1);
            }
            if (device)
            {
                const bool zero = lengthIsZero();
                if (zero && missingFrom < 0)
                    missingFrom = static_cast<int64_t>(i);
                else if (!zero && missingFrom >= 0)
                    closeMissing(i - 1);
            }
        }
        for (uint32_t p = 0; p < map.size(); ++p)
            if (damagedFrom[p] >= 0)
                close(p, count - 1);
        if (device && missingFrom >= 0)
            closeMissing(count - 1);
    }

    // State recorded for devices this machine lacks
    std::array<bool, 256> unclaimed{};
    for (const TTDEngineCheckpoint& cp : _checkpoints)
        for (uint8_t id : cp.unclaimedDevices)
            unclaimed[id] = true;
    for (uint32_t id = 0; id < 256; ++id)
        if (unclaimed[id])
        {
            TTDRestoreIssue issue;
            issue.kind = TTDRestoreIssueKind::DeviceNotPresent;
            issue.device.type = static_cast<TTDDeviceType>(id);
            issue.device.instance = "device " + std::to_string(id);
            issue.detail = "state recorded for a device this machine does not have";
            add(std::move(issue));
        }
    // Firmware the live devices run against the recorded one
    for (const TTDDeviceEntry& e : _devices.Entries())
        if (e.device && e.device->TTDDescribe().firmwareFingerprint != e.descriptor.firmwareFingerprint)
        {
            TTDRestoreIssue issue;
            issue.kind = TTDRestoreIssueKind::FirmwareDiffers;
            issue.severity = TTDRestoreStatus::NotBitExact;
            issue.device = e.descriptor.Key();
            issue.detail = "recorded with another firmware image";
            add(std::move(issue));
        }
    if (unlisted > 0)
        result.message += "; " + std::to_string(unlisted) + " more issue(s) not listed";
    return result;
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
        uint32_t damagedPiece = 0;
        const DeviceStateRead read = ReadDeviceState(index, id, state, &damagedPiece);
        if (read == DeviceStateRead::Damaged)
        {
            TTDRestoreIssue damage = DamageIssue(index, static_cast<uint32_t>(_deviceRegionOf[id]), damagedPiece);
            damage.action = TTDLiveStateAction::KeptLive;
            damage.detail += ", kept its live state";
            result.Add(damage);
            continue;
        }
        if (read != DeviceStateRead::Ok || state.empty())
        {
            issue.kind = TTDRestoreIssueKind::DeviceMissingState;
            issue.action = TTDLiveStateAction::KeptLive;
            issue.detail = "no state at this position, kept its live state";
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
            issue.action = TTDLiveStateAction::KeptLive;
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
