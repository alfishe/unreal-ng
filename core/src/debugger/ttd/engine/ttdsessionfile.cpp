#include "debugger/ttd/engine/ttdsessionfile.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

#include "debugger/ttd/engine/ttdbytes.h"
#include "debugger/ttd/ttdcompression.h"
#include "debugger/ttd/timetravelengine.h"

namespace ttd
{

namespace
{
constexpr uint16_t kTablesVersion = 1;
constexpr uint32_t kNoItem = 0xFFFFFFFFu;

using namespace sessionstream;

const std::vector<TTDStreamDesc>& Streams()
{
    static const std::vector<TTDStreamDesc> streams = {
        {kPieces, 1, TTDStreamKind::Required, "pieces"},
        {kCheckpoints, 1, TTDStreamKind::Required, "checkpoints"},
        {kEvents, 1, TTDStreamKind::Required, "events"},
        {kConfiguration, 1, TTDStreamKind::Required, "configuration"},
        {kWriteJournal, 1, TTDStreamKind::Ancillary, "write-journal"},
        {kBusReads, 1, TTDStreamKind::Required, "bus-reads"},
        {kBusWrites, 1, TTDStreamKind::Required, "bus-writes"},
        {kBusVectors, 1, TTDStreamKind::Required, "bus-vectors"},
        {kMediaReads, 1, TTDStreamKind::Required, "media-reads"},
    };
    return streams;
}

/// region <Tables>

void WriteDescriptor(TTDByteWriter& w, const TTDDeviceDescriptor& d)
{
    w.U16(static_cast<uint16_t>(d.type));
    w.Str(d.instance);
    w.U8(static_cast<uint8_t>(d.legacyId));
    w.U16(d.layoutVersion);
    w.U32(d.stateSize);
    w.U8(d.variableSize ? 1 : 0);
    w.U64(d.firmwareFingerprint);
    w.Varint(d.restoreAfter.size());
    for (const TTDDeviceKey& k : d.restoreAfter)
    {
        w.U16(static_cast<uint16_t>(k.type));
        w.Str(k.instance);
    }
    w.Varint(d.timeFields.size());
    for (const TTDTimeField& f : d.timeFields)
    {
        w.U16(f.offset);
        w.U8(f.width);
    }
    w.U8(d.runsBehindCpu ? 1 : 0);
}

bool ReadDescriptor(TTDByteReader& r, TTDDeviceDescriptor& d)
{
    uint16_t type = 0;
    uint8_t legacy = 0, variable = 0, behind = 0;
    uint64_t count = 0;
    if (!r.U16(type) || !r.Str(d.instance) || !r.U8(legacy) || !r.U16(d.layoutVersion) || !r.U32(d.stateSize) ||
        !r.U8(variable) || !r.U64(d.firmwareFingerprint) || !r.Varint(count) || count > r.Left())
        return false;
    d.type = static_cast<TTDDeviceType>(type);
    d.legacyId = static_cast<PeripheralId>(legacy);
    d.variableSize = variable != 0;
    for (uint64_t i = 0; i < count; ++i)
    {
        TTDDeviceKey k;
        uint16_t t = 0;
        if (!r.U16(t) || !r.Str(k.instance))
            return false;
        k.type = static_cast<TTDDeviceType>(t);
        d.restoreAfter.push_back(std::move(k));
    }
    if (!r.Varint(count) || count > r.Left())
        return false;
    for (uint64_t i = 0; i < count; ++i)
    {
        TTDTimeField f;
        if (!r.U16(f.offset) || !r.U8(f.width))
            return false;
        d.timeFields.push_back(f);
    }
    if (!r.U8(behind))
        return false;
    d.runsBehindCpu = behind != 0;
    return true;
}

/// endregion </Tables>

/// Columns (frame deltas, times, ports, PCs, values): each compresses on its own,
/// as in the bus journal's blocks. A time is the delta from the previous record of
/// its frame, or the time itself on a new frame
void WritePortRecords(TTDByteWriter& w, const TTDPortJournal& j, uint64_t from, uint64_t to)
{
    std::vector<TTDPortRecord> records(to - from);
    for (uint64_t i = from; i < to; ++i)
        j.Get(i, records[i - from]);
    w.Varint(records.size());
    uint64_t lastFrame = 0;
    uint32_t lastTime = 0;
    for (const TTDPortRecord& r : records)
    {
        w.Varint(r.frame - lastFrame);
        w.Varint(r.frame == lastFrame ? r.tInFrame - lastTime : r.tInFrame);
        lastFrame = r.frame;
        lastTime = r.tInFrame;
    }
    for (const TTDPortRecord& r : records)
        w.U16(r.port);
    for (const TTDPortRecord& r : records)
        w.U16(r.pc);
    for (const TTDPortRecord& r : records)
        w.U8(r.value);
}

bool ReadPortRecords(TTDByteReader& r, std::vector<TTDPortRecord>& out)
{
    uint64_t count = 0;
    if (!r.Varint(count) || count > r.Left())
        return false;
    out.assign(static_cast<size_t>(count), {});
    uint64_t frame = 0;
    uint32_t time = 0;
    for (TTDPortRecord& p : out)
    {
        uint64_t delta = 0;
        uint32_t t = 0;
        if (!r.Varint(delta) || !r.VarintAs(t))
            return false;
        time = delta == 0 ? time + t : t;
        frame += delta;
        p.frame = frame;
        p.tInFrame = time;
    }
    for (TTDPortRecord& p : out)
        if (!r.U16(p.port))
            return false;
    for (TTDPortRecord& p : out)
        if (!r.U16(p.pc))
            return false;
    for (TTDPortRecord& p : out)
        if (!r.U8(p.value))
            return false;
    return true;
}

struct Range
{
    uint64_t from = 0, to = 0;
};
}  // namespace

namespace
{
std::atomic<bool> g_branchReading{true};
}  // namespace

void TTDSessionFile::SetBranchReadingForTesting(bool on)
{
    g_branchReading = on;
}

bool TTDSessionFile::KnownStream(uint16_t id)
{
    if ((id >= kFrameStreamFirst && id <= kFrameStreamLast) || IsHolderStream(id))
        return true;
    if (id == kBranchTable)
        return true;
    if (const uint16_t trunk = TrunkOfTwin(id))
        return KnownStream(trunk);
    for (const TTDStreamDesc& s : Streams())
        if (s.id == id)
            return true;
    return false;
}

/// region <Save>

/// region <Writer>

TTDSessionWriter::~TTDSessionWriter()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stop = true;
    }
    _wake.notify_all();
    if (_thread.joinable())
        _thread.join();
}

std::string TTDSessionWriter::Error() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _error;
}

void TTDSessionWriter::Fail(const std::string& why)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_failed.exchange(true))
        _error = why;
}

bool TTDSessionWriter::Begin(const TimeTravelEngine& e, ITTDByteSink& sink, const TTDSessionSaveParams& params,
                             std::string& error, const Limits& limits, size_t first)
{
    if (!e._open)
    {
        error = "no session to write";
        return false;
    }
    _sink = &sink;
    _params = params;
    _next = first == SIZE_MAX ? e._line->cpBase : first;
    _limits = limits;
    _liveItem.assign(e._regions.size(), {});
    for (size_t r = 0; r < e._regions.size(); ++r)
        _liveItem[r].assign(e._regions[r].pieces, kNoItem);

    // Header: the streams and the session's tables
    TTDContainerHeader header;
    header.flags = params.headerFlags;
    header.uuid = params.uuid;
    header.createdMicros = params.createdMicros;
    header.streams = Streams();
    for (uint32_t id = 0; id < TTDStreamRegistry::kMaxStreams; ++id)
        if (e._streams.IsRegistered(id))
            header.streams.push_back(
                {static_cast<uint16_t>(kFrameStreamFirst + id), 1, TTDStreamKind::Ancillary, e._streams.Name(id)});
    for (const TTDHolderStream& h : params.holderStreams)
    {
        if (!IsHolderStream(h.id))
        {
            error = "holder stream " + std::to_string(h.id) + " is not a holder stream id";
            return false;
        }
        header.streams.push_back({h.id, 1, TTDStreamKind::Ancillary, h.name});
    }
    // Branches: the table and the twins of every stream a branch writes (ancillary: an old reader loads the trunk)
    size_t liveLines = 0;
    for (uint32_t l = 0; l < e._lines.size(); ++l)
        liveLines += e._lines[l] != nullptr;
    const bool branchFile = params.fileLine != UINT32_MAX && params.fileLine != 0;
    if (branchFile)
    {
        _lineId = params.fileLine;
        _local = params.fileLine;
    }
    if ((params.allLines && liveLines > 1 && params.fileLine == UINT32_MAX) || branchFile)
    {
        const size_t trunkStreams = header.streams.size();
        header.streams.push_back({kBranchTable, 1, TTDStreamKind::Ancillary, "branch-table"});
        for (size_t k = 0; k < trunkStreams; ++k)
        {
            const TTDStreamDesc& d = header.streams[k];
            if (const uint16_t twin = BranchTwin(d.id))
                header.streams.push_back({twin, d.layoutVersion, TTDStreamKind::Ancillary, "branch-" + d.name});
        }
        std::set<uint16_t> declared;
        for (const TTDStreamDesc& d : header.streams)
            declared.insert(d.id);
        for (const auto& [line, streams] : params.lineHolderStreams)
            for (const TTDHolderStream& h : streams)
            {
                const uint16_t twin = BranchTwin(h.id);
                if (!IsHolderStream(h.id) || !twin)
                {
                    error = "branch holder stream " + std::to_string(h.id) + " has no branch twin";
                    return false;
                }
                if (declared.insert(twin).second)
                    header.streams.push_back({twin, 1, TTDStreamKind::Ancillary, "branch-" + h.name});
            }
    }
    {
        TTDByteWriter w;
        w.U16(kTablesVersion);
        w.U32(e._snapshotInterval);
        std::vector<const TTDRegionDesc*> memory;
        for (uint32_t r = 0; r < e._regions.size(); ++r)
            if (!e.IsDeviceStateRegion(r))
                memory.push_back(&e._regions[r]);
        w.Varint(memory.size());
        for (const TTDRegionDesc* m : memory)
        {
            w.U16(static_cast<uint16_t>(m->id));
            w.Str(m->name);
            w.U16(m->ownerType);
            w.Str(m->ownerInstance);
            w.U32(m->pieces);
            w.U32(m->bytes);
            w.U32(m->dirtyGranularity);
            w.U32(m->blockPieces);
        }
        w.Varint(e._devices.Entries().size());
        for (const TTDDeviceEntry& d : e._devices.Entries())
            WriteDescriptor(w, d.descriptor);
        header.sessionTables = std::move(w.bytes);
    }

    if (!_writer.Begin(sink, header, &error))
        return false;
    if (_background)
        _thread = std::thread([this]() { Run(); });
    return true;
}

bool TTDSessionWriter::Collect(TimeTravelEngine& e)
{
    const bool ok = CollectParts(e);
    // Written through: the stream copies of the queued parts are the writer's now
    if (_next < e.CheckpointCount() && e.HasCheckpoint(_next))
        e.DropFrameStreamCopiesBefore(e.CpAt(_next).position.frame);
    return ok;
}

bool TTDSessionWriter::CollectParts(const TimeTravelEngine& e)
{
    const size_t perPart = std::max<uint32_t>(_params.checkpointsPerPart, 1);
    while (!Failed() && _next + perPart < std::min(_end, e.CheckpointCount()))
    {
        if (_queuedBytes.load() > _limits.lagHardBytes)
        {
            Fail("the disk cannot keep up: " + std::to_string(_queuedBytes.load() >> 20) + " MB waiting");
            break;
        }
        PartJob job;
        if (!BuildPart(e, _next, _next + perPart, false, job))
        {
            Fail(_buildError.empty() ? "a part could not be laid out" : _buildError);
            break;
        }
        _next += perPart;
        Queue(std::move(job));
    }
    return !Failed();
}

bool TTDSessionWriter::Finish(const TimeTravelEngine& e, size_t end)
{
    if (_finished)
        return !Failed();
    _finished = true;
    _end = std::min(end, e.CheckpointCount());
    CollectParts(e);
    const size_t perPart = std::max<uint32_t>(_params.checkpointsPerPart, 1);
    while (!Failed() && _next < _end)
    {
        const size_t last = std::min(_next + perPart, _end);
        PartJob job;
        if (!BuildPart(e, _next, last, true, job))
        {
            Fail(_buildError.empty() ? "a part could not be laid out" : _buildError);
            break;
        }
        _next = last;
        Queue(std::move(job));
    }
    PartJob finalize;
    finalize.finalize = true;
    Queue(std::move(finalize));
    if (_background)
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _stop = true;
        }
        _wake.notify_all();
        if (_thread.joinable())
            _thread.join();
    }
    return !Failed();
}

void TTDSessionWriter::Queue(PartJob&& job)
{
    for (const auto& [id, bytes] : job.records)
        job.bytes += bytes.size();
    if (!_background)
    {
        Write(job);
        return;
    }
    _queuedBytes += job.bytes;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push_back(std::move(job));
    }
    _wake.notify_one();
}

void TTDSessionWriter::Run()
{
    for (;;)
    {
        PartJob job;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _wake.wait(lock, [this]() { return _stop || !_queue.empty(); });
            if (_queue.empty())
                return;   // stopped and drained
            job = std::move(_queue.front());
            _queue.pop_front();
        }
        Write(job);
        _queuedBytes -= job.bytes;
    }
}

void TTDSessionWriter::Write(PartJob& job)
{
    // Parts already queued are written even after the writer stopped taking
    // new ones (it fell behind): their data is whole. Only a write error stops it
    if (_ioFailed.load())
        return;
    auto ioFail = [this](const char* why) {
        _ioFailed = true;
        Fail(why);
    };
    if (job.finalize)
    {
        if (!_writer.Finalize())
            ioFail("the index could not be written");
        return;
    }
    for (const auto& [stream, bytes] : job.records)
        if (!_writer.AddRecord(stream, bytes))
        {
            ioFail("a write failed (disk full or device gone)");
            return;
        }
    if (!_writer.EndPart(job.end, true))
        ioFail("a write failed (disk full or device gone)");
}

bool TTDSessionWriter::BuildPart(const TimeTravelEngine& e, size_t first, size_t last, bool final, PartJob& job)
{
    const TTDPieceStore& store = *e._store;
    const std::vector<TTDEvent>& events = e._line->events.Events();
    // The file's last part takes everything up to the next checkpoint after
    // it (the next segment's baseline), or everything left at the session's end
    const bool lastPart = final && last == _end;
    const bool atSessionEnd = last >= e.CheckpointCount();
    const TTDEngineCheckpoint& head = e.CpAt(first);
    const bool fileStart = _linePart == 0;
    const bool forkCopy = _local != 0 && first == e._line->cpBase;   // a branch's first checkpoint (the fork's copy)
    const bool folderFile = _params.fileLine != UINT32_MAX;
    if (fileStart)
    {
        // Journal positions in the file count from its first record, so files
        // load one after another into a session that starts anywhere
        _base[0] = first == 0 ? 0 : head.busReadCursor;
        _base[1] = first == 0 ? 0 : head.busWriteCursor;
        _base[2] = first == 0 ? 0 : head.mediaReadCursor;
        _base[3] = first == 0 ? 0 : head.busVectorCursor;
        job.end.extra = {kPartFileStart};
    }
    TTDByteWriter pieces, checkpoints;
    std::set<uint32_t> dependencies;
    checkpoints.Varint(last - first);
    uint64_t previousFrame = 0;
    for (size_t i = first; i < last; ++i)
    {
        const TTDEngineCheckpoint& cp = e.CpAt(i);
        checkpoints.Varint(cp.position.frame - previousFrame);
        previousFrame = cp.position.frame;
        // A folder file's fork copy: every piece whole, a segment's start of its own
        const bool whole = forkCopy && folderFile && i == first;
        checkpoints.U8(cp.baseline || whole ? 1 : 0);
        checkpoints.U64(cp.start);
        checkpoints.Raw(&cp.cpu, sizeof(cp.cpu));
        checkpoints.Raw(&cp.chipset, sizeof(cp.chipset));
        checkpoints.Varint(cp.unclaimedDevices.size());
        checkpoints.Bytes(cp.unclaimedDevices.data(), cp.unclaimedDevices.size());
        checkpoints.Varint(cp.busReadCursor - _base[0]);
        checkpoints.Varint(cp.busWriteCursor - _base[1]);
        checkpoints.Varint(cp.mediaReadCursor - _base[2]);
        checkpoints.Varint(cp.busVectorCursor - _base[3]);
        // One version, numbered as it appears; @p asFull: a difference decoded and stored whole (a folder file's fork
        // copy: its delta bases are in another file)
        auto version = [&](uint32_t region, uint32_t piece, TTDPieceId id, bool asFull) {
            checkpoints.Varint(piece);
            const uint32_t item = static_cast<uint32_t>(_itemPart.size());
            _itemOf[id] = item;
            _itemPart.push_back(static_cast<uint32_t>(_part));
            auto encoding = store.EncodingOf(id);
            if (asFull && TTDPieceStore::IsDifference(encoding))
            {
                std::array<uint8_t, kTTDPieceSize> raw{};
                if (!store.Decode(id, raw.data()))
                {
                    _buildError = "a version of the fork could not be decoded";
                    return false;
                }
                const bool zero = codec::IsAllZero(raw.data(), raw.size());
                const std::vector<uint8_t> packed = zero ? std::vector<uint8_t>{} : codec::Compress(raw.data(), raw.size());
                pieces.U8(static_cast<uint8_t>(zero ? TTDPieceStore::Encoding::Zero : TTDPieceStore::Encoding::Full));
                pieces.Varint(0);   // depth
                pieces.Varint(0);   // no base
                pieces.U32(store.CrcOf(id));
                pieces.Varint(packed.size());
                pieces.Bytes(packed.data(), packed.size());
                _liveItem[region][piece] = item;
                return true;
            }
            uint32_t baseItem = kNoItem, baseLocal = _local;
            if (TTDPieceStore::IsDifference(encoding))
            {
                // A branch's version may build on an ancestor's (its delta base at the fork)
                uint32_t basePart = 0;
                if (!FindItem(store.BaseOf(id), baseLocal, baseItem, basePart))
                {
                    _buildError = "a version's base is not in the session";
                    return false;
                }
                if (basePart != _part)
                    dependencies.insert(basePart);
            }
            pieces.U8(static_cast<uint8_t>(encoding));
            pieces.Varint(store.DepthOf(id));
            if (_local != 0)
            {
                // Branch records: the base's line, then its number there (storage.md §3.3)
                pieces.Varint(baseItem == kNoItem ? 0 : uint64_t(baseLocal) + 1);
                if (baseItem != kNoItem)
                    pieces.Varint(baseItem);
            }
            else
                pieces.Varint(baseItem == kNoItem ? 0 : uint64_t(baseItem) + 1);
            pieces.U32(store.CrcOf(id));
            pieces.Varint(store.PayloadSize(id));
            pieces.Bytes(store.PayloadData(id), store.PayloadSize(id));
            _liveItem[region][piece] = item;
            return true;
        };
        if (whole)
        {
            // Every piece from the copy's full tables
            uint64_t regions = 0;
            for (const TTDEngineCheckpoint::RegionRefs& refs : cp.regions)
                for (uint32_t p = 0; refs.snapshot && p < e._regions[refs.region].pieces; ++p)
                    if (e._tables->Get(refs.snapshot, p) != TTDPieceStore::kNone)
                    {
                        ++regions;
                        break;
                    }
            checkpoints.Varint(regions);
            for (const TTDEngineCheckpoint::RegionRefs& refs : cp.regions)
            {
                uint32_t count = 0;
                for (uint32_t p = 0; refs.snapshot && p < e._regions[refs.region].pieces; ++p)
                    count += e._tables->Get(refs.snapshot, p) != TTDPieceStore::kNone;
                if (count == 0)
                    continue;
                checkpoints.Varint(refs.region);
                checkpoints.Varint(count);
                for (uint32_t p = 0; p < e._regions[refs.region].pieces; ++p)
                    if (const TTDPieceId id = e._tables->Get(refs.snapshot, p); id != TTDPieceStore::kNone)
                        if (!version(refs.region, p, id, true))
                            return false;
            }
            continue;
        }
        // A branch's fork copy in a saved file: its tables are its parent's at the fork, rebuilt on load
        // (ImportForkLine)
        const bool noChanges = forkCopy && i == first;
        uint64_t changedRegions = 0;
        for (const TTDEngineCheckpoint::RegionRefs& refs : cp.regions)
            changedRegions += refs.changeCount > 0 && !noChanges;
        checkpoints.Varint(changedRegions);
        for (const TTDEngineCheckpoint::RegionRefs& refs : cp.regions)
        {
            if (refs.changeCount == 0 || noChanges)
                continue;
            checkpoints.Varint(refs.region);
            checkpoints.Varint(refs.changeCount);
            for (uint32_t k = refs.firstChange; k < refs.firstChange + refs.changeCount; ++k)
                if (!version(refs.region, e.ChangeAt(k).piece, e.ChangeAt(k).id, false))
                    return false;
        }
    }
    // Every piece's current version at the part's end
    for (const std::vector<uint32_t>& region : _liveItem)
        for (uint32_t item : region)
            if (item != kNoItem && _itemPart[item] != _part)
                dependencies.insert(_itemPart[item]);

    if (_local != 0 && folderFile)
    {
        // A folder's branch file: its first file starts with the branch's creation; nothing outside the file
        if (fileStart && forkCopy)
            job.records.emplace_back(kBranchTable, CreateRecord(_local, e._line->parent, e._line->fork.frame,
                                                                e._line->fork.tInFrame, _lineId));
    }
    else if (_local != 0)
    {
        // A branch: its first part starts with its creation; every part needs the line's previous one, the first
        // the parent's parts from the fork's segment start to the fork
        if (fileStart)
        {
            const LineLayout& parent = _layouts.at(_layouts.at(_lineId).parent);
            const TimeTravelEngine::BranchLine& parentLine = *e._lines[_layouts.at(_lineId).parent];
            const size_t forkIndex = e._line->forkIndex;
            size_t segmentStart = forkIndex;
            while (segmentStart > parentLine.cpBase && !parentLine.checkpoints[segmentStart - parentLine.cpBase].baseline)
                --segmentStart;
            for (size_t k = 0; k < parent.parts.size(); ++k)
            {
                const size_t partEnd = k + 1 < parent.parts.size() ? parent.parts[k + 1].first : SIZE_MAX;
                if (partEnd > segmentStart && parent.parts[k].first <= forkIndex)
                    dependencies.insert(parent.parts[k].second);
            }
            job.records.emplace_back(kBranchTable, CreateRecord(_local, parent.local, e._line->fork.frame,
                                                                e._line->fork.tInFrame, _lineId));
        }
        else
            dependencies.insert(_layouts.at(_lineId).parts.back().second);
        dependencies.erase(_part);
    }
    job.records.emplace_back(StreamOf(kPieces), std::move(pieces.bytes));
    job.records.emplace_back(StreamOf(kCheckpoints), std::move(checkpoints.bytes));

    // Events up to the next part's start
    TTDMachineTime until = 0;
    if (!atSessionEnd)
        until = e.CpAt(last).start;
    TTDByteWriter ev;
    size_t count = 0;
    // By time, not by index: a ring drops old events, which shifts the indices
    const size_t eventsFrom = first == 0 ? 0 : e._line->events.CursorAt(head.start);
    const size_t eventsTo = atSessionEnd ? events.size() : e._line->events.CursorAt(until);
    count = eventsTo - eventsFrom;
    ev.Varint(count);
    TTDMachineTime previousTime = 0;
    for (size_t k = eventsFrom; k < eventsTo; ++k)
    {
        const TTDEvent& x = events[k];
        ev.Varint(x.machineTime - previousTime);
        previousTime = x.machineTime;
        ev.U16(static_cast<uint16_t>(x.kind));
        ev.U16(static_cast<uint16_t>(x.cpu));
        ev.Raw(x.args, sizeof(x.args));
        if (x.payload)
        {
            const std::vector<uint8_t>& bytes = e._payloads.Bytes(x.payload);
            ev.Varint(uint64_t(bytes.size()) + 1);
            ev.Bytes(bytes.data(), bytes.size());
        }
        else
            ev.Varint(0);
    }

    // Configuration entries starting in the part, media versions changing in it
    TTDByteWriter config;
    const uint64_t frameUntil = atSessionEnd ? UINT64_MAX : e.CpAt(last).position.frame;
    const size_t configFrom = _nextConfig;
    while (_nextConfig < e._line->configs.size() && e._line->configs[_nextConfig].frame < frameUntil)
        ++_nextConfig;
    config.Varint(_nextConfig - configFrom);
    for (size_t k = configFrom; k < _nextConfig; ++k)
    {
        const TTDConfigEntry& c = e._line->configs[k];
        config.U64(c.frame);
        config.Varint(c.fingerprint.fields.size());
        for (const TTDConfigField& f : c.fingerprint.fields)
        {
            config.Str(f.name);
            config.U64(f.value);
            config.U8(f.affectsRestore ? 1 : 0);
        }
    }
    std::vector<std::pair<const TTDMediaSlot*, std::pair<uint32_t, TTDMediaVersion>>> media;
    for (const TTDMediaSlot& slot : e._line->mediaSlots)
        for (const auto& change : slot.changes)
            if (change.first >= first && change.first < last)
                media.push_back({&slot, change});
    config.Varint(media.size());
    for (const auto& [slot, change] : media)
    {
        config.Str(slot->slot);
        config.Str(slot->format);
        config.U8(slot->hasVersions ? 1 : 0);
        config.Varint(change.first);
        config.U64(change.second.contentId);
        config.U64(change.second.version);
    }

    // Journals from this part's first checkpoint to the next part's
    auto range = [&](uint64_t TTDEngineCheckpoint::*cursor, uint64_t size) {
        return Range{first == 0 ? 0 : head.*cursor, atSessionEnd ? size : e.CpAt(last).*cursor};
    };
    TTDByteWriter reads, writes, vectors, mediaReads;
    const Range rr = range(&TTDEngineCheckpoint::busReadCursor, e._line->busReads.Size());
    const Range wr = range(&TTDEngineCheckpoint::busWriteCursor, e._line->busWrites.Size());
    const Range vr = range(&TTDEngineCheckpoint::busVectorCursor, e._line->busVectors.Size());
    const Range mr = range(&TTDEngineCheckpoint::mediaReadCursor, e._line->mediaReads.Size());
    WritePortRecords(reads, e._line->busReads, rr.from, rr.to);
    WritePortRecords(writes, e._line->busWrites, wr.from, wr.to);
    WritePortRecords(vectors, e._line->busVectors, vr.from, vr.to);
    mediaReads.Varint(mr.to - mr.from);
    for (uint64_t k = mr.from; k < mr.to; ++k)
    {
        const TTDMediaJournal::Record& m = e._line->mediaReads.At(k);
        mediaReads.U64(m.frame);
        mediaReads.U32(m.tInFrame);
        mediaReads.Str(m.slot < e._line->mediaReads.SlotNames().size() ? e._line->mediaReads.SlotNames()[m.slot] : "");
        mediaReads.U64(m.lba);
        mediaReads.Varint(m.size);
        mediaReads.Bytes(e._line->mediaReads.Bytes(m), m.size);
    }

    // A stream with nothing in this part writes no record (a missing record reads as empty)
    auto add = [&job, this](uint16_t stream, TTDByteWriter& w, bool empty) {
        if (!empty)
            job.records.emplace_back(StreamOf(stream), std::move(w.bytes));
        return true;
    };
    bool ok = add(kEvents, ev, count == 0) && add(kConfiguration, config, _nextConfig == configFrom && media.empty()) &&
              add(kBusReads, reads, rr.to == rr.from) && add(kBusWrites, writes, wr.to == wr.from) &&
              add(kBusVectors, vectors, vr.to == vr.from) && add(kMediaReads, mediaReads, mr.to == mr.from);

    // Frame-boundary stream copies of the part's frames (D19): each the XOR
    // with the stream's previous copy, zstd; a full copy at least every 50
    // frames, on a size change and at the file's start
    const uint64_t streamUntil = atSessionEnd ? UINT64_MAX : e.CpAt(last).position.frame;
    for (const auto& [stream, copies] : e._line->streamCopies)
    {
        TTDByteWriter sw;
        size_t n = 0;
        for (const TimeTravelEngine::FrameStreamCopy_& copy : copies)
            n += copy.frame >= head.position.frame && copy.frame < streamUntil;
        if (n == 0)
            continue;
        sw.Varint(n);
        std::vector<uint8_t>& previous = _streamPrevious[stream];
        uint32_t& sinceFull = _streamSinceFull[stream];
        std::vector<uint8_t> diff;
        for (const TimeTravelEngine::FrameStreamCopy_& copy : copies)
        {
            if (copy.frame < head.position.frame || copy.frame >= streamUntil)
                continue;
            const bool full = previous.size() != copy.bytes.size() || sinceFull + 1 >= kFrameStreamFullEvery;
            const uint8_t* source = copy.bytes.data();
            if (!full)
            {
                diff.resize(copy.bytes.size());
                for (size_t k = 0; k < diff.size(); ++k)
                    diff[k] = static_cast<uint8_t>(copy.bytes[k] ^ previous[k]);
                source = diff.data();
            }
            const std::vector<uint8_t> packed = codec::Compress(source, copy.bytes.size());
            sw.Varint(copy.frame - head.position.frame);
            sw.U8(full ? 0 : 1);
            sw.Varint(copy.bytes.size());
            sw.Varint(packed.size());
            sw.Bytes(packed.data(), packed.size());
            previous = copy.bytes;
            sinceFull = full ? 0 : sinceFull + 1;
        }
        job.records.emplace_back(StreamOf(static_cast<uint16_t>(kFrameStreamFirst + stream)), std::move(sw.bytes));
    }

    // The write journal (derived, D40) goes whole with the last part
    if (ok && !_lineage && lastPart && atSessionEnd &&
        (e._line->writes.Size() > 0 || !e._line->writes.Segments().empty()))
    {
        // In column blocks of 2,048 records (EncodeWriteBlock)
        TTDByteWriter wj;
        std::vector<TTDWriteRecord> all;
        all.reserve(static_cast<size_t>(e._line->writes.Size()));
        e._line->writes.ForEach([&all](const TTDWriteRecord& rec) { all.push_back(rec); });
        wj.Varint(all.size());
        for (size_t k = 0; k < all.size(); k += kWriteBlockRecords)
        {
            const uint32_t n = static_cast<uint32_t>(std::min<size_t>(kWriteBlockRecords, all.size() - k));
            const std::vector<uint8_t> block = EncodeWriteBlock(all.data() + k, n);
            wj.Varint(block.size());
            wj.Bytes(block.data(), block.size());
        }
        wj.Varint(e._line->writes.Segments().size());
        for (const TTDJournalSegment& s : e._line->writes.Segments())
        {
            wj.U64(s.from);
            wj.U64(s.to);
        }
        job.records.emplace_back(StreamOf(kWriteJournal), std::move(wj.bytes));
    }

    // The holder's own data, whole, with the session's last part
    if (ok && lastPart && atSessionEnd && (_local == 0 || folderFile))
        for (const TTDHolderStream& h : _params.holderStreams)
            if (!h.bytes.empty())
                job.records.emplace_back(h.id, h.bytes);
    if (ok && lastPart && atSessionEnd && _local != 0)
        if (const auto it = _params.lineHolderStreams.find(_lineId); it != _params.lineHolderStreams.end())
            for (const TTDHolderStream& h : it->second)
                if (!h.bytes.empty())
                    job.records.emplace_back(BranchTwin(h.id), h.bytes);

    job.end.firstFrame = head.position.frame;
    job.end.frameCount = static_cast<uint32_t>(e.CpAt(last - 1).position.frame - head.position.frame + 1);
    job.end.dependencies.assign(dependencies.begin(), dependencies.end());
    job.end.branch = static_cast<uint16_t>(_local);
    _layouts[_lineId].parts.emplace_back(first, _part);
    ++_part;
    ++_linePart;
    return ok;
}

bool TTDSessionFile::Save(const TimeTravelEngine& e, ITTDByteSink& sink, std::string& error,
                          const TTDSessionSaveParams& params)
{
    if (!e._open || e.CheckpointCount() == e.FirstCheckpoint())
    {
        error = "no session to save";
        return false;
    }
    TTDSessionWriter writer(false);
    size_t liveLines = 0;
    for (const auto& line : e._lines)
        liveLines += line != nullptr;
    if (params.allLines && liveLines > 1)
    {
        // Branches: every line, the trunk first
        if (!writer.Begin(e, sink, params, error, {}, e._lines.front()->cpBase))
            return false;
        if (!writer.WriteAllLines(const_cast<TimeTravelEngine&>(e)))
        {
            error = writer.Error();
            return false;
        }
        return true;
    }
    if (!params.allLines && liveLines > 1)
    {
        // One lineage, linear: the trunk from its start
        const uint32_t tip = params.lineageOf == UINT32_MAX ? e._lineId : params.lineageOf;
        if (!e.HasLine(tip))
        {
            error = "line " + std::to_string(tip) + " is not in the session";
            return false;
        }
        if (!writer.Begin(e, sink, params, error, {}, e._lines.front()->cpBase))
            return false;
        if (!writer.WriteLineage(const_cast<TimeTravelEngine&>(e), tip))
        {
            error = writer.Error();
            return false;
        }
        return true;
    }
    if (!writer.Begin(e, sink, params, error))
        return false;
    if (!writer.Finish(e))
    {
        error = writer.Error();
        return false;
    }
    return true;
}

std::vector<uint8_t> TTDBranchCreateRecord(uint32_t local, uint32_t parentLocal, uint64_t forkFrame, uint32_t forkT,
                                           const std::vector<uint8_t>& meta)
{
    TTDByteWriter table;
    table.U8(1);   // record kind: create
    table.Varint(local);
    table.Varint(parentLocal);
    table.Varint(forkFrame);
    table.Varint(forkT);
    table.Varint(meta.size());
    table.Bytes(meta.data(), meta.size());
    return std::move(table.bytes);
}

std::vector<uint8_t> TTDSessionWriter::CreateRecord(uint32_t local, uint32_t parentLocal, uint64_t forkFrame,
                                                    uint32_t forkT, uint32_t line) const
{
    const auto meta = _params.lineMeta.find(line);
    return TTDBranchCreateRecord(local, parentLocal, forkFrame, forkT,
                                 meta == _params.lineMeta.end() ? std::vector<uint8_t>{} : meta->second);
}

bool TTDSessionWriter::FindItem(uint32_t storeId, uint32_t& local, uint32_t& item, uint32_t& part) const
{
    if (const auto it = _itemOf.find(storeId); it != _itemOf.end())
    {
        local = _local;
        item = it->second;
        part = _itemPart[item];
        return true;
    }
    // The ancestors, nearest first
    const auto self = _layouts.find(_lineId);
    for (uint32_t line = self == _layouts.end() ? 0xFFFFFFFFu : self->second.parent; line != 0xFFFFFFFFu;)
    {
        const auto layout = _layouts.find(line);
        if (layout == _layouts.end())
            return false;
        if (const auto it = layout->second.itemOf.find(storeId); it != layout->second.itemOf.end())
        {
            local = layout->second.local;
            item = it->second;
            part = layout->second.itemPart[item];
            return true;
        }
        line = layout->second.parent;
    }
    return false;
}

bool TTDSessionWriter::WriteAllLines(TimeTravelEngine& e)
{
    TimeTravelEngine::BranchLine* const active = e._line;
    const uint32_t activeId = e._lineId;
    // Lines are numbered in the file densely, in id order: a parent always comes before its children
    uint32_t local = 0;
    for (uint32_t id = 0; id < e._lines.size() && !Failed(); ++id)
    {
        if (!e._lines[id])
            continue;
        if (id != 0 && (_layouts.find(e._lines[id]->parent) == _layouts.end()))
        {
            Fail("branch line " + std::to_string(id) + " has no parent in the session");
            break;
        }
        // The previous line's layout stays for its children's references
        if (local > 0 || id != 0)
        {
            LineLayout& done = _layouts[_lineId];
            done.itemOf = std::move(_itemOf);
            done.itemPart = std::move(_itemPart);
            _itemOf.clear();
            _itemPart.clear();
        }
        e._line = e._lines[id].get();
        e._lineId = id;
        LineLayout& layout = _layouts[id];
        layout.local = local;
        layout.parent = id == 0 ? 0xFFFFFFFFu : e._line->parent;
        _lineId = id;
        _local = local++;
        _linePart = 0;
        _next = e._line->cpBase;
        _nextConfig = 0;
        _streamPrevious.clear();
        _streamSinceFull.clear();
        for (std::vector<uint32_t>& region : _liveItem)
            std::fill(region.begin(), region.end(), kNoItem);
        _end = e.CheckpointCount();
        const size_t perPart = std::max<uint32_t>(_params.checkpointsPerPart, 1);
        while (!Failed() && _next < _end)
        {
            const size_t last = std::min(_next + perPart, _end);
            PartJob job;
            if (!BuildPart(e, _next, last, true, job))
            {
                Fail(_buildError.empty() ? "a part could not be laid out" : _buildError);
                break;
            }
            _next = last;
            Queue(std::move(job));
        }
    }
    e._line = active;
    e._lineId = activeId;
    _finished = true;
    PartJob finalize;
    finalize.finalize = true;
    Queue(std::move(finalize));
    return !Failed();
}

bool TTDSessionWriter::WriteLineage(TimeTravelEngine& e, uint32_t tip)
{
    TimeTravelEngine::BranchLine* const active = e._line;
    const uint32_t activeId = e._lineId;
    std::vector<uint32_t> chain;
    for (uint32_t l = tip; e.HasLine(l); l = e._lines[l]->parent)
        chain.insert(chain.begin(), l);
    if (chain.empty() || chain.front() != 0)
    {
        Fail("line " + std::to_string(tip) + " does not reach the trunk");
        return false;
    }
    // Every line on trunk streams, one numbering of versions: a branch's first checkpoint (the fork's copy) carries
    // the parent's change records at the fork, so it follows the parent's checkpoint before the fork as the parent's
    // own fork checkpoint would
    _lineage = chain.size() > 1;
    _local = 0;
    const size_t perPart = std::max<uint32_t>(_params.checkpointsPerPart, 1);
    for (size_t k = 0; k < chain.size() && !Failed(); ++k)
    {
        TimeTravelEngine::BranchLine& line = *e._lines[chain[k]];
        if (k > 0)
        {
            // The journals count on: the branch's positions from its fork copy follow the parent's at the fork
            const TTDEngineCheckpoint& forkCopy = line.checkpoints.front();
            const TTDEngineCheckpoint& parentAt = e.CpAt(line.forkIndex);   // e._line is still the parent
            _base[0] = forkCopy.busReadCursor - (parentAt.busReadCursor - _base[0]);
            _base[1] = forkCopy.busWriteCursor - (parentAt.busWriteCursor - _base[1]);
            _base[2] = forkCopy.mediaReadCursor - (parentAt.mediaReadCursor - _base[2]);
            _base[3] = forkCopy.busVectorCursor - (parentAt.busVectorCursor - _base[3]);
        }
        e._line = &line;
        e._lineId = chain[k];
        _lineId = chain[k];
        _next = line.cpBase;
        // The settings in force at the fork went with the parent's parts; the branch's from the fork frame on
        _nextConfig = 0;
        if (k > 0)
            while (_nextConfig < line.configs.size() && line.configs[_nextConfig].frame < line.fork.frame)
                ++_nextConfig;
        _end = k + 1 < chain.size() ? e._lines[chain[k + 1]]->forkIndex : e.CheckpointCount();
        while (!Failed() && _next < _end)
        {
            const size_t last = std::min(_next + perPart, _end);
            PartJob job;
            if (!BuildPart(e, _next, last, true, job))
            {
                Fail(_buildError.empty() ? "a part could not be laid out" : _buildError);
                break;
            }
            _next = last;
            Queue(std::move(job));
        }
    }
    e._line = active;
    e._lineId = activeId;
    _finished = true;
    PartJob finalize;
    finalize.finalize = true;
    Queue(std::move(finalize));
    return !Failed();
}

/// endregion </Save>

/// region <Load>

bool TTDSessionFile::ReadTables(const std::vector<uint8_t>& tables, uint32_t& interval, std::vector<TTDRegionDesc>& regions,
                                std::vector<TTDDeviceEntry>& devices, std::string& error)
{
    TTDByteReader t(tables);
    uint16_t version = 0;
    uint64_t count = 0;
    if (!t.U16(version) || version != kTablesVersion || !t.U32(interval) || !t.Varint(count) || count > t.Left())
    {
        error = "damaged session tables";
        return false;
    }
    for (uint64_t i = 0; i < count; ++i)
    {
        TTDRegionDesc r;
        uint16_t id = 0;
        if (!t.U16(id) || !t.Str(r.name) || !t.U16(r.ownerType) || !t.Str(r.ownerInstance) || !t.U32(r.pieces) ||
            !t.U32(r.bytes) || !t.U32(r.dirtyGranularity) || !t.U32(r.blockPieces))
        {
            error = "damaged session tables (regions)";
            return false;
        }
        r.id = static_cast<TTDRegionId>(id);
        regions.push_back(std::move(r));
    }
    if (!t.Varint(count) || count > t.Left())
    {
        error = "damaged session tables (devices)";
        return false;
    }
    for (uint64_t i = 0; i < count; ++i)
    {
        TTDDeviceEntry d;
        if (!ReadDescriptor(t, d.descriptor))
        {
            error = "damaged session tables (devices)";
            return false;
        }
        devices.push_back(std::move(d));
    }
    return true;
}

bool TTDSessionFile::ReadDevices(const ITTDByteSource& source, std::vector<TTDDeviceDescriptor>& out, std::string& error)
{
    out.clear();
    TTDContainerReader reader;
    if (!reader.Open(source, error, [](uint16_t id) { return KnownStream(id); }))
        return false;
    uint32_t interval = 0;
    std::vector<TTDRegionDesc> regions;
    std::vector<TTDDeviceEntry> devices;
    if (!ReadTables(reader.Header().sessionTables, interval, regions, devices, error))
        return false;
    for (TTDDeviceEntry& d : devices)
        out.push_back(std::move(d.descriptor));
    return true;
}

bool TTDSessionFile::Load(TimeTravelEngine& e, const ITTDByteSource& source, std::string& error,
                          TTDSessionLoadReport* reportOut, bool append)
{
    TTDSessionLoadReport report;
    TTDContainerReader reader;
    const bool branchReading = g_branchReading.load();
    auto known = [branchReading](uint16_t id) {
        return KnownStream(id) && (branchReading || (id != kBranchTable && TrunkOfTwin(id) == 0));
    };
    if (!reader.Open(source, error, known))
        return false;
    report.notes = reader.Notes();
    report.partsInFile = reader.Parts().size();
    report.convertedFromV1 = (reader.Header().flags & kSessionConvertedFromV1) != 0;

    // The tables: regions and devices, then the session as at its start
    uint32_t interval = 0;
    std::vector<TTDRegionDesc> regions;
    std::vector<TTDDeviceEntry> devices;
    if (!ReadTables(reader.Header().sessionTables, interval, regions, devices, error))
        return false;
    if (append && e._open)
    {
        // The next file of the same recording: the same regions and devices
        size_t memoryRegions = 0;
        for (uint32_t r = 0; r < e._regions.size(); ++r)
            memoryRegions += !e.IsDeviceStateRegion(r);
        if (memoryRegions != regions.size() || e._devices.Entries().size() != devices.size())
        {
            error = "the file is not of the same recording (regions or devices differ)";
            return false;
        }
    }
    else
    {
        if (!e.BeginSession(regions, std::move(devices), error))
            return false;
        e.SetSnapshotInterval(interval);
    }

    TTDPieceStore& store = *e._store;
    // Per line in the file (the trunk 0, branches 1..): number in the file -> store id, and the journals'
    // positions where the line's current file starts
    std::map<uint32_t, std::vector<TTDPieceId>> itemsOf;
    std::map<uint32_t, std::array<uint64_t, 4>> basesOf;
    std::set<uint32_t> lostLines;   // branches that could not be loaded (their parts are skipped)
    std::vector<TTDPortRecord> ports;
    auto stop = [&](size_t part, const std::string& why) {
        report.stoppedAt = "part " + std::to_string(part) + ": " + why;
    };
    TimeTravelEngine::BranchLine* const trunk = e._lines.front().get();
    auto toTrunk = [&]() {
        e._line = trunk;
        e._lineId = 0;
    };

    for (const TTDPartRef& part : reader.Parts())
    {
        const uint32_t lineOf = branchReading ? part.branch : 0;
        // A branch's part: its line must be loaded, or be created by this part (the branch table's create)
        if (lineOf != 0 && lostLines.count(lineOf))
            continue;
        if (!reader.IsReachable(part.index))
        {
            if (lineOf != 0)
            {
                // A branch is lost from its first unreadable part on; the trunk and the other branches stand
                lostLines.insert(lineOf);
                report.notes.push_back("branch " + std::to_string(lineOf) + " stops at part " +
                                       std::to_string(part.index) + ": " +
                                       (part.damaged ? part.damage : "it depends on a damaged part"));
                continue;
            }
            stop(part.index, part.damaged ? part.damage : "it depends on a damaged part");
            break;
        }
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> records;
        std::vector<uint8_t> branchTable;
        bool hasBranchTable = false;
        std::string why;
        bool readOk = true;
        for (const TTDRecordRef& record : part.records)
        {
            if (!known(record.streamId))
                continue;
            // A branch's records are its streams' twins: read as the trunk's, into the branch's line. A part
            // holds one line's records; a twin in a trunk part (or a trunk stream in a branch's) is not read
            uint16_t stream = record.streamId;
            if (stream == kBranchTable)
            {
                if (lineOf == 0)
                    continue;
                std::string tableWhy;
                hasBranchTable = reader.ReadRecord(record, branchTable, &tableWhy);
                continue;
            }
            if (const uint16_t mirrored = TrunkOfTwin(stream))
            {
                if (lineOf == 0)
                    continue;
                stream = mirrored;
            }
            else if (lineOf != 0 && !IsHolderStream(stream))
                continue;
            std::vector<uint8_t> bytes;
            if (IsHolderStream(stream))
            {
                // A recording folder's last file may be a branch's: the session's holder data in the trunk's ids
                const bool trunkData = record.streamId == stream;
                // The holder's data: ancillary, a damaged one is left out
                std::string holderWhy;
                if (reader.ReadRecord(record, bytes, &holderWhy))
                {
                    if (lineOf == 0 || trunkData)
                        report.holderStreams[stream] = std::move(bytes);
                    else
                        report.lineHolderStreams[lineOf][stream] = std::move(bytes);
                }
                else
                    report.notes.push_back("holder stream " + std::to_string(record.streamId) +
                                           " is damaged: left out (" + holderWhy + ")");
                continue;
            }
            if (!reader.ReadRecord(record, bytes, &why))
            {
                readOk = false;
                break;
            }
            records.emplace_back(stream, std::move(bytes));
        }
        if (!readOk && lineOf != 0)
        {
            lostLines.insert(lineOf);
            report.notes.push_back("branch " + std::to_string(lineOf) + " stops at part " + std::to_string(part.index) +
                                   ": " + why);
            continue;
        }
        if (!readOk)
        {
            stop(part.index, why);
            break;
        }
        // The line this part fills: the trunk, a branch loaded so far, or one the part creates
        struct Create
        {
            uint32_t parent = 0;
            uint64_t frame = 0;
            uint32_t tInFrame = 0;
        };
        std::optional<Create> create;
        if (lineOf != 0)
        {
            const bool known = lineOf < e._lines.size() && e._lines[lineOf] != nullptr;
            if (!known)
            {
                // The branch table's create: the branch's id in the file, its parent, the fork; the holder's record
                TTDByteReader t(branchTable);
                uint8_t kind = 0;
                uint32_t id = 0;
                Create c;
                uint64_t metaSize = 0;
                std::vector<uint8_t> meta;
                const bool parsed = hasBranchTable && t.U8(kind) && kind == 1 && t.VarintAs(id) && t.VarintAs(c.parent) &&
                                    t.Varint(c.frame) && t.VarintAs(c.tInFrame) && t.Varint(metaSize) &&
                                    metaSize <= t.Left() && t.Bytes(meta, static_cast<size_t>(metaSize));
                std::string problem;
                if (!parsed)
                    problem = "its creation record is missing or damaged";
                else if (id != lineOf || id < e._lines.size())
                    problem = "it is numbered out of order";
                else if (c.parent >= e._lines.size() || !e._lines[c.parent] || lostLines.count(c.parent))
                    problem = "its parent is not loaded";
                if (!problem.empty())
                {
                    lostLines.insert(lineOf);
                    report.notes.push_back("branch " + std::to_string(lineOf) + " is left out: " + problem);
                    continue;
                }
                report.lineMeta[lineOf] = std::move(meta);
                create = c;
            }
            else
            {
                e._line = e._lines[lineOf].get();
                e._lineId = lineOf;
            }
        }
        else
            toTrunk();
        std::vector<TTDPieceId>& items = itemsOf[lineOf];
        std::array<uint64_t, 4>& base = basesOf[lineOf];
        auto find = [&records](uint16_t stream) -> const std::vector<uint8_t>* {
            for (const auto& [id, bytes] : records)
                if (id == stream)
                    return &bytes;
            return nullptr;
        };
        if (!part.extra.empty() && (part.extra[0] & kPartFileStart))
        {
            // A file (or a joined segment) starts: its own numbering and positions (a branch's new line: from 0)
            items.clear();
            base = {};
            if (!create)
                base = {e._line->busReads.Size(), e._line->busWrites.Size(), e._line->mediaReads.Size(),
                        e._line->busVectors.Size()};
        }
        const std::vector<uint8_t>* pieceBytes = find(kPieces);
        const std::vector<uint8_t>* cpBytes = find(kCheckpoints);
        if (!pieceBytes || !cpBytes)
        {
            if (lineOf != 0)
            {
                lostLines.insert(lineOf);
                report.notes.push_back("branch " + std::to_string(lineOf) + " stops at part " +
                                       std::to_string(part.index) + ": its pieces or checkpoints are missing");
                continue;
            }
            stop(part.index, "its pieces or checkpoints are missing");
            break;
        }

        // Checkpoints, their versions imported as each change names them
        TTDByteReader p(*pieceBytes);
        TTDByteReader c(*cpBytes);
        uint64_t cps = 0;
        bool ok = c.Varint(cps);
        uint64_t frame = 0;
        for (uint64_t i = 0; ok && i < cps; ++i)
        {
            TTDEngineCheckpoint cp;
            uint64_t delta = 0, unclaimed = 0, changedRegions = 0;
            uint8_t flags = 0;
            ok = c.Varint(delta) && c.U8(flags) && c.U64(cp.start) && c.Raw(&cp.cpu, sizeof(cp.cpu)) &&
                 c.Raw(&cp.chipset, sizeof(cp.chipset)) && c.Varint(unclaimed) && c.Bytes(cp.unclaimedDevices, unclaimed) &&
                 c.Varint(cp.busReadCursor) && c.Varint(cp.busWriteCursor) && c.Varint(cp.mediaReadCursor) &&
                 c.Varint(cp.busVectorCursor) && c.Varint(changedRegions);
            frame += delta;
            cp.position.frame = frame;
            cp.baseline = (flags & 1) != 0;
            cp.busReadCursor += base[0];
            cp.busWriteCursor += base[1];
            cp.mediaReadCursor += base[2];
            cp.busVectorCursor += base[3];
            std::vector<TTDImportedChange> changes;
            for (uint64_t g = 0; ok && g < changedRegions; ++g)
            {
                uint32_t region = 0, changeCount = 0;
                ok = c.VarintAs(region) && c.VarintAs(changeCount) && changeCount <= c.Left();
                for (uint32_t k = 0; ok && k < changeCount; ++k)
                {
                    uint32_t piece = 0, depth = 0, crc = 0, size = 0;
                    uint64_t base = 0;
                    uint8_t encoding = 0;
                    const uint8_t* payload = nullptr;
                    TTDPieceId baseId = TTDPieceStore::kNone;
                    ok = c.VarintAs(piece) && p.U8(encoding) && p.VarintAs(depth) && p.Varint(base);
                    if (ok && lineOf != 0 && base != 0)
                    {
                        // A branch's base: (its line in the file + 1, its number there); the line is this one or an
                        // ancestor loaded before
                        uint64_t number = 0;
                        const uint32_t baseLine = static_cast<uint32_t>(base - 1);
                        const auto it = itemsOf.find(baseLine);
                        ok = p.Varint(number) && it != itemsOf.end() && number < it->second.size() &&
                             (baseLine == lineOf || e.IsAncestorLine(baseLine, create ? create->parent : lineOf));
                        if (ok)
                            baseId = it->second[static_cast<size_t>(number)];
                    }
                    else if (ok && base != 0)
                    {
                        ok = base - 1 < items.size();
                        if (ok)
                            baseId = items[static_cast<size_t>(base - 1)];
                    }
                    ok = ok && p.U32(crc) && p.VarintAs(size) && p.View(payload, size) && depth <= 0xFFFF;
                    if (!ok)
                        break;
                    const TTDPieceId id = store.Import(static_cast<TTDPieceStore::Encoding>(encoding), baseId,
                                                       static_cast<uint16_t>(depth), crc, payload, size);
                    ok = id != TTDPieceStore::kNone;
                    if (ok)
                    {
                        items.push_back(id);
                        changes.push_back({region, piece, id});
                    }
                }
            }
            if (ok && create && i == 0)
            {
                // The branch's first checkpoint: the fork's copy, its tables the parent's (no changes recorded), or
                // whole (a recording folder's branch file: every piece, nothing read from another file)
                // The create record must name the fork the copy was taken at: the copy's frame, and the parent
                // the copy matches (its tables the parent's: the part depends on the parent's part of the fork;
                // whole: every piece's CRC the parent's)
                uint32_t made = lineOf;
                const TTDPosition cut{0, create->frame, create->tInFrame};
                if (cp.position.frame != create->frame)
                {
                    ok = false;
                    why = "its creation record names another fork than its first checkpoint's";
                }
                else if (changes.empty())
                {
                    const auto& parts = reader.Parts();
                    const bool fromParent = std::any_of(
                        part.dependencies.begin(), part.dependencies.end(), [&](uint32_t d) {
                            return d < parts.size() && parts[d].branch == create->parent &&
                                   parts[d].firstFrame <= create->frame &&
                                   create->frame <= parts[d].firstFrame + parts[d].frameCount;
                        });
                    ok = fromParent;
                    if (!ok)
                        why = "its first part does not depend on its parent's part of the fork";
                    ok = ok && e.ImportForkLine(create->parent, create->frame, cut, cp, made, why);
                }
                else
                    ok = e.ImportBranchLine(create->parent, create->frame, cut, made, why) &&
                         e.ImportCheckpoint(std::move(cp), changes, why) && e.ForkCopyMatchesParent(made, why);
            }
            else if (ok && (!e.ImportCheckpoint(std::move(cp), changes, why)))
                ok = false;
        }
        if (!ok && lineOf != 0)
        {
            lostLines.insert(lineOf);
            toTrunk();
            if (create)
            {
                // Its first part: the branch is left out whole (a line made before the failure goes again)
                std::string ignored;
                if (e.HasLine(lineOf))
                    e.ReleaseLine(lineOf, ignored);
                report.notes.push_back("branch " + std::to_string(lineOf) + " is left out: " +
                                       (why.empty() ? "damaged checkpoints" : why));
                continue;
            }
            report.notes.push_back("branch " + std::to_string(lineOf) + " stops at part " + std::to_string(part.index) +
                                   ": " + (why.empty() ? "damaged checkpoints" : why));
            continue;
        }
        if (!ok)
        {
            stop(part.index, why.empty() ? "damaged checkpoints" : why);
            break;
        }

        // Events
        if (const std::vector<uint8_t>* evBytes = find(kEvents))
        {
            TTDByteReader r(*evBytes);
            uint64_t n = 0;
            ok = r.Varint(n);
            TTDMachineTime time = 0;
            for (uint64_t i = 0; ok && i < n; ++i)
            {
                TTDEvent x;
                uint64_t delta = 0, payload = 0;
                uint16_t kind = 0, cpu = 0;
                ok = r.Varint(delta) && r.U16(kind) && r.U16(cpu) && r.Raw(x.args, sizeof(x.args)) && r.Varint(payload) &&
                     (payload == 0 || payload - 1 <= r.Left());
                if (!ok)
                    break;
                time += delta;
                x.machineTime = time;
                x.kind = static_cast<TTDEventKind>(kind);
                x.cpu = static_cast<TTDCpuId>(cpu);
                if (payload)
                {
                    const uint8_t* bytes = nullptr;
                    r.View(bytes, static_cast<size_t>(payload - 1));
                    x.payload = e._payloads.Store(bytes, static_cast<size_t>(payload - 1));
                }
                ok = e.AppendTimedEvent(x);
            }
        }
        // Configuration and media versions
        if (ok)
            if (const std::vector<uint8_t>* cfg = find(kConfiguration))
            {
                TTDByteReader r(*cfg);
                uint64_t n = 0;
                ok = r.Varint(n);
                for (uint64_t i = 0; ok && i < n; ++i)
                {
                    TTDConfigEntry entry;
                    uint64_t fields = 0;
                    ok = r.U64(entry.frame) && r.Varint(fields) && fields <= r.Left();
                    for (uint64_t f = 0; ok && f < fields; ++f)
                    {
                        TTDConfigField field;
                        uint8_t affects = 0;
                        ok = r.Str(field.name) && r.U64(field.value) && r.U8(affects);
                        field.affectsRestore = affects != 0;
                        entry.fingerprint.fields.push_back(std::move(field));
                    }
                    e._line->configs.push_back(std::move(entry));
                }
                ok = ok && r.Varint(n);
                for (uint64_t i = 0; ok && i < n; ++i)
                {
                    std::string slot, format;
                    uint8_t hasVersions = 0;
                    uint32_t cp = 0;
                    TTDMediaVersion v;
                    ok = r.Str(slot) && r.Str(format) && r.U8(hasVersions) && r.VarintAs(cp) && r.U64(v.contentId) &&
                         r.U64(v.version);
                    if (!ok)
                        break;
                    auto it = std::find_if(e._line->mediaSlots.begin(), e._line->mediaSlots.end(),
                                           [&slot](const TTDMediaSlot& s) { return s.slot == slot; });
                    if (it == e._line->mediaSlots.end())
                    {
                        e._line->mediaSlots.push_back({slot, format, hasVersions != 0, {}});
                        it = std::prev(e._line->mediaSlots.end());
                    }
                    it->changes.emplace_back(cp, v);
                }
            }
        // Journals
        auto loadPorts = [&](uint16_t stream, auto&& append) {
            if (!ok)
                return;
            if (const std::vector<uint8_t>* bytes = find(stream))
            {
                TTDByteReader r(*bytes);
                ok = ReadPortRecords(r, ports);
                for (const TTDPortRecord& rec : ports)
                    append(rec);
            }
        };
        loadPorts(kBusReads, [&](const TTDPortRecord& rec) { e.AppendBusRead(rec); });
        loadPorts(kBusWrites, [&](const TTDPortRecord& rec) { e.AppendBusWrite(rec); });
        loadPorts(kBusVectors,
                  [&](const TTDPortRecord& rec) { e._line->busVectors.OnRead(rec.port, rec.value, rec.frame, rec.tInFrame, rec.pc); });
        if (ok)
            if (const std::vector<uint8_t>* bytes = find(kMediaReads))
            {
                TTDByteReader r(*bytes);
                uint64_t n = 0;
                ok = r.Varint(n);
                for (uint64_t i = 0; ok && i < n; ++i)
                {
                    uint64_t frameAt = 0, lba = 0;
                    uint32_t tInFrame = 0, size = 0;
                    std::string slot;
                    const uint8_t* data = nullptr;
                    ok = r.U64(frameAt) && r.U32(tInFrame) && r.Str(slot) && r.U64(lba) && r.VarintAs(size) &&
                         r.View(data, size);
                    if (ok)
                        e._line->mediaReads.Append(frameAt, tInFrame, slot, lba, data, size);
                }
            }
        if (ok)
            if (const std::vector<uint8_t>* bytes = find(kWriteJournal))
            {
                TTDByteReader r(*bytes);
                uint64_t n = 0;
                bool journalOk = r.Varint(n);
                std::vector<TTDWriteRecord> block;
                for (uint64_t k = 0; journalOk && k < n; k += kWriteBlockRecords)
                {
                    const uint32_t count = static_cast<uint32_t>(std::min<uint64_t>(kWriteBlockRecords, n - k));
                    uint64_t size = 0;
                    std::vector<uint8_t> raw;
                    journalOk = r.Varint(size) && size <= r.Left() && r.Bytes(raw, static_cast<size_t>(size)) &&
                                DecodeWriteBlock(raw, count, block);
                    if (journalOk)
                        for (const TTDWriteRecord& rec : block)
                            e._line->writes.Append(rec);
                }
                std::vector<TTDJournalSegment> segments;
                journalOk = journalOk && r.Varint(n) && n <= r.Left() / 16;
                for (uint64_t i = 0; journalOk && i < n; ++i)
                {
                    TTDJournalSegment s;
                    journalOk = r.U64(s.from) && r.U64(s.to);
                    segments.push_back(s);
                }
                if (journalOk)
                    e._line->writes.SetSegments(std::move(segments));
                else
                {
                    e._line->writes.Clear();   // ancillary: dropped, the session stands
                    report.notes.push_back("the write journal is damaged: left out");
                }
            }
        if (!ok && lineOf != 0)
        {
            lostLines.insert(lineOf);
            report.notes.push_back("branch " + std::to_string(lineOf) + " stops at part " + std::to_string(part.index) +
                                   ": damaged events, configuration or journals");
            toTrunk();
            continue;
        }
        if (!ok)
        {
            stop(part.index, "damaged events, configuration or journals");
            break;
        }
        report.partsLoaded++;
    }
    toTrunk();
    report.lines = 0;
    for (const auto& line : e._lines)
        report.lines += line != nullptr;

    report.checkpoints = e.CheckpointCount();
    report.complete = report.partsLoaded == report.partsInFile;
    const bool any = !e._line->checkpoints.empty();
    if (!any)
        error = report.stoppedAt.empty() ? "the file holds no checkpoint" : report.stoppedAt;
    if (reportOut)
        *reportOut = std::move(report);
    return any;
}

bool TTDSessionFile::ReadFrameStream(const ITTDByteSource& source, uint32_t stream, uint64_t frame,
                                     std::vector<uint8_t>& out, std::string& error)
{
    TTDContainerReader reader;
    if (!reader.Open(source, error, [](uint16_t id) { return KnownStream(id); }))
        return false;
    const uint16_t streamId = static_cast<uint16_t>(kFrameStreamFirst + stream);
    std::vector<uint8_t> current, bytes, raw;
    bool found = false;
    for (const TTDPartRef& part : reader.Parts())
    {
        if (part.firstFrame > frame)
            break;
        if (!part.extra.empty() && (part.extra[0] & kPartFileStart))
            current.clear();
        for (const TTDRecordRef& record : part.records)
        {
            if (record.streamId != streamId)
                continue;
            if (!reader.ReadRecord(record, bytes, &error))
                return false;
            TTDByteReader r(bytes);
            uint64_t n = 0;
            if (!r.Varint(n))
            {
                error = "damaged frame stream record";
                return false;
            }
            for (uint64_t k = 0; k < n; ++k)
            {
                uint64_t delta = 0, rawSize = 0, packedSize = 0;
                uint8_t kind = 0;
                const uint8_t* packed = nullptr;
                if (!r.Varint(delta) || !r.U8(kind) || !r.Varint(rawSize) || !r.Varint(packedSize) ||
                    !r.View(packed, static_cast<size_t>(packedSize)) || rawSize > (64u << 20) ||
                    (kind == 1 && current.size() != rawSize))
                {
                    error = "damaged frame stream record";
                    return false;
                }
                raw.resize(static_cast<size_t>(rawSize));
                if (rawSize && !codec::Decompress(packed, static_cast<size_t>(packedSize), raw.size(), raw.data()))
                {
                    error = "damaged frame stream copy";
                    return false;
                }
                if (kind == 1)
                    for (size_t b = 0; b < raw.size(); ++b)
                        raw[b] ^= current[b];
                current = raw;
                if (part.firstFrame + delta == frame)
                {
                    out = current;
                    found = true;
                }
            }
        }
        if (found)
            return true;
    }
    error = "frame " + std::to_string(frame) + " has no copy of this stream (not recorded)";
    return false;
}

/// endregion </Load>

}  // namespace ttd
