#include "debugger/ttd/engine/ttdsessionfile.h"

#include <algorithm>
#include <iterator>
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
/// as in v1's journal blocks. A time is the delta from the previous record of
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

bool TTDSessionFile::KnownStream(uint16_t id)
{
    if ((id >= kFrameStreamFirst && id <= kFrameStreamLast) || IsHolderStream(id))
        return true;
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
    _next = first == SIZE_MAX ? e._cpBase : first;
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
    const std::vector<TTDEvent>& events = e._events.Events();
    // The file's last part takes everything up to the next checkpoint after
    // it (the next segment's baseline), or everything left at the session's end
    const bool lastPart = final && last == _end;
    const bool atSessionEnd = last >= e.CheckpointCount();
    const TTDEngineCheckpoint& head = e.CpAt(first);
    const bool fileStart = _part == 0;
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
        checkpoints.U8(cp.baseline ? 1 : 0);
        checkpoints.U64(cp.start);
        checkpoints.Raw(&cp.cpu, sizeof(cp.cpu));
        checkpoints.Raw(&cp.chipset, sizeof(cp.chipset));
        checkpoints.Varint(cp.unclaimedDevices.size());
        checkpoints.Bytes(cp.unclaimedDevices.data(), cp.unclaimedDevices.size());
        checkpoints.Varint(cp.busReadCursor - _base[0]);
        checkpoints.Varint(cp.busWriteCursor - _base[1]);
        checkpoints.Varint(cp.mediaReadCursor - _base[2]);
        checkpoints.Varint(cp.busVectorCursor - _base[3]);
        uint64_t changedRegions = 0;
        for (const TTDEngineCheckpoint::RegionRefs& refs : cp.regions)
            changedRegions += refs.changeCount > 0;
        checkpoints.Varint(changedRegions);
        for (const TTDEngineCheckpoint::RegionRefs& refs : cp.regions)
        {
            if (refs.changeCount == 0)
                continue;
            checkpoints.Varint(refs.region);
            checkpoints.Varint(refs.changeCount);
            for (uint32_t k = refs.firstChange; k < refs.firstChange + refs.changeCount; ++k)
            {
                const auto& change = e.ChangeAt(k);
                checkpoints.Varint(change.piece);
                // The version itself, numbered as it appears
                const TTDPieceId id = change.id;
                const uint32_t item = static_cast<uint32_t>(_itemPart.size());
                _itemOf[id] = item;
                _itemPart.push_back(static_cast<uint32_t>(_part));
                const auto encoding = store.EncodingOf(id);
                uint32_t baseItem = kNoItem;
                if (TTDPieceStore::IsDifference(encoding))
                {
                    const auto base = _itemOf.find(store.BaseOf(id));
                    if (base == _itemOf.end())
                    {
                        _buildError = "a version's base is not in the session";
                        return false;
                    }
                    baseItem = base->second;
                    if (_itemPart[baseItem] != _part)
                        dependencies.insert(_itemPart[baseItem]);
                }
                pieces.U8(static_cast<uint8_t>(encoding));
                pieces.Varint(store.DepthOf(id));
                pieces.Varint(baseItem == kNoItem ? 0 : uint64_t(baseItem) + 1);
                pieces.U32(store.CrcOf(id));
                pieces.Varint(store.PayloadSize(id));
                pieces.Bytes(store.PayloadData(id), store.PayloadSize(id));
                _liveItem[refs.region][change.piece] = item;
            }
        }
    }
    // Every piece's current version at the part's end
    for (const std::vector<uint32_t>& region : _liveItem)
        for (uint32_t item : region)
            if (item != kNoItem && _itemPart[item] != _part)
                dependencies.insert(_itemPart[item]);

    job.records.emplace_back(kPieces, std::move(pieces.bytes));
    job.records.emplace_back(kCheckpoints, std::move(checkpoints.bytes));

    // Events up to the next part's start
    TTDMachineTime until = 0;
    if (!atSessionEnd)
        until = e.CpAt(last).start;
    TTDByteWriter ev;
    size_t count = 0;
    // By time, not by index: a ring drops old events, which shifts the indices
    const size_t eventsFrom = first == 0 ? 0 : e._events.CursorAt(head.start);
    const size_t eventsTo = atSessionEnd ? events.size() : e._events.CursorAt(until);
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
    while (_nextConfig < e._configs.size() && e._configs[_nextConfig].frame < frameUntil)
        ++_nextConfig;
    config.Varint(_nextConfig - configFrom);
    for (size_t k = configFrom; k < _nextConfig; ++k)
    {
        const TTDConfigEntry& c = e._configs[k];
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
    for (const TTDMediaSlot& slot : e._mediaSlots)
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
    const Range rr = range(&TTDEngineCheckpoint::busReadCursor, e._busReads.Size());
    const Range wr = range(&TTDEngineCheckpoint::busWriteCursor, e._busWrites.Size());
    const Range vr = range(&TTDEngineCheckpoint::busVectorCursor, e._busVectors.Size());
    const Range mr = range(&TTDEngineCheckpoint::mediaReadCursor, e._mediaReads.Size());
    WritePortRecords(reads, e._busReads, rr.from, rr.to);
    WritePortRecords(writes, e._busWrites, wr.from, wr.to);
    WritePortRecords(vectors, e._busVectors, vr.from, vr.to);
    mediaReads.Varint(mr.to - mr.from);
    for (uint64_t k = mr.from; k < mr.to; ++k)
    {
        const TTDMediaJournal::Record& m = e._mediaReads.At(k);
        mediaReads.U64(m.frame);
        mediaReads.U32(m.tInFrame);
        mediaReads.Str(m.slot < e._mediaReads.SlotNames().size() ? e._mediaReads.SlotNames()[m.slot] : "");
        mediaReads.U64(m.lba);
        mediaReads.Varint(m.size);
        mediaReads.Bytes(e._mediaReads.Bytes(m), m.size);
    }

    // A stream with nothing in this part writes no record (a missing record reads as empty)
    auto add = [&job](uint16_t stream, TTDByteWriter& w, bool empty) {
        if (!empty)
            job.records.emplace_back(stream, std::move(w.bytes));
        return true;
    };
    bool ok = add(kEvents, ev, count == 0) && add(kConfiguration, config, _nextConfig == configFrom && media.empty()) &&
              add(kBusReads, reads, rr.to == rr.from) && add(kBusWrites, writes, wr.to == wr.from) &&
              add(kBusVectors, vectors, vr.to == vr.from) && add(kMediaReads, mediaReads, mr.to == mr.from);

    // Frame-boundary stream copies of the part's frames (D19): each the XOR
    // with the stream's previous copy, zstd; a full copy at least every 50
    // frames, on a size change and at the file's start
    const uint64_t streamUntil = atSessionEnd ? UINT64_MAX : e.CpAt(last).position.frame;
    for (const auto& [stream, copies] : e._streamCopies)
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
        job.records.emplace_back(static_cast<uint16_t>(kFrameStreamFirst + stream), std::move(sw.bytes));
    }

    // The write journal (derived, D40) goes whole with the last part
    if (ok && lastPart && atSessionEnd && (e._writes.Size() > 0 || !e._writes.Segments().empty()))
    {
        // In v1's column blocks of 2,048 records (EncodeWriteBlock)
        TTDByteWriter wj;
        std::vector<TTDWriteRecord> all;
        all.reserve(static_cast<size_t>(e._writes.Size()));
        e._writes.ForEach([&all](const TTDWriteRecord& rec) { all.push_back(rec); });
        wj.Varint(all.size());
        for (size_t k = 0; k < all.size(); k += kWriteBlockRecords)
        {
            const uint32_t n = static_cast<uint32_t>(std::min<size_t>(kWriteBlockRecords, all.size() - k));
            const std::vector<uint8_t> block = EncodeWriteBlock(all.data() + k, n);
            wj.Varint(block.size());
            wj.Bytes(block.data(), block.size());
        }
        wj.Varint(e._writes.Segments().size());
        for (const TTDJournalSegment& s : e._writes.Segments())
        {
            wj.U64(s.from);
            wj.U64(s.to);
        }
        job.records.emplace_back(kWriteJournal, std::move(wj.bytes));
    }

    // The holder's own data, whole, with the session's last part
    if (ok && lastPart && atSessionEnd)
        for (const TTDHolderStream& h : _params.holderStreams)
            if (!h.bytes.empty())
                job.records.emplace_back(h.id, h.bytes);

    job.end.firstFrame = head.position.frame;
    job.end.frameCount = static_cast<uint32_t>(e.CpAt(last - 1).position.frame - head.position.frame + 1);
    job.end.dependencies.assign(dependencies.begin(), dependencies.end());
    ++_part;
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
    if (!writer.Begin(e, sink, params, error))
        return false;
    if (!writer.Finish(e))
    {
        error = writer.Error();
        return false;
    }
    return true;
}

/// endregion </Save>

/// region <Load>

bool TTDSessionFile::Load(TimeTravelEngine& e, const ITTDByteSource& source, std::string& error,
                          TTDSessionLoadReport* reportOut, bool append)
{
    TTDSessionLoadReport report;
    TTDContainerReader reader;
    if (!reader.Open(source, error, [](uint16_t id) { return KnownStream(id); }))
        return false;
    report.notes = reader.Notes();
    report.partsInFile = reader.Parts().size();
    report.convertedFromV1 = (reader.Header().flags & kSessionConvertedFromV1) != 0;

    // The tables: regions and devices, then the session as at its start
    TTDByteReader t(reader.Header().sessionTables);
    uint16_t version = 0;
    uint32_t interval = 0;
    uint64_t count = 0;
    if (!t.U16(version) || version != kTablesVersion || !t.U32(interval) || !t.Varint(count) || count > t.Left())
    {
        error = "damaged session tables";
        return false;
    }
    std::vector<TTDRegionDesc> regions;
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
    std::vector<TTDDeviceEntry> devices;
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
    std::vector<TTDPieceId> items;   // number in the file -> store id
    uint64_t base[4] = {};           // the journals' positions where the current file starts
    std::vector<TTDPortRecord> ports;
    auto stop = [&](size_t part, const std::string& why) {
        report.stoppedAt = "part " + std::to_string(part) + ": " + why;
    };

    for (const TTDPartRef& part : reader.Parts())
    {
        if (!reader.IsReachable(part.index))
        {
            stop(part.index, part.damaged ? part.damage : "it depends on a damaged part");
            break;
        }
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> records;
        std::string why;
        bool readOk = true;
        for (const TTDRecordRef& record : part.records)
        {
            if (!KnownStream(record.streamId))
                continue;
            std::vector<uint8_t> bytes;
            if (IsHolderStream(record.streamId))
            {
                // The holder's data: ancillary, a damaged one is left out
                std::string holderWhy;
                if (reader.ReadRecord(record, bytes, &holderWhy))
                    report.holderStreams[record.streamId] = std::move(bytes);
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
            records.emplace_back(record.streamId, std::move(bytes));
        }
        if (!readOk)
        {
            stop(part.index, why);
            break;
        }
        auto find = [&records](uint16_t stream) -> const std::vector<uint8_t>* {
            for (const auto& [id, bytes] : records)
                if (id == stream)
                    return &bytes;
            return nullptr;
        };
        if (!part.extra.empty() && (part.extra[0] & kPartFileStart))
        {
            // A file (or a joined segment) starts: its own numbering and positions
            items.clear();
            base[0] = e._busReads.Size();
            base[1] = e._busWrites.Size();
            base[2] = e._mediaReads.Size();
            base[3] = e._busVectors.Size();
        }
        const std::vector<uint8_t>* pieceBytes = find(kPieces);
        const std::vector<uint8_t>* cpBytes = find(kCheckpoints);
        if (!pieceBytes || !cpBytes)
        {
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
                    ok = c.VarintAs(piece) && p.U8(encoding) && p.VarintAs(depth) && p.Varint(base) && p.U32(crc) &&
                         p.VarintAs(size) && p.View(payload, size) && depth <= 0xFFFF &&
                         (base == 0 || base - 1 < items.size());
                    if (!ok)
                        break;
                    const TTDPieceId baseId = base == 0 ? TTDPieceStore::kNone : items[static_cast<size_t>(base - 1)];
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
            if (ok && (!e.ImportCheckpoint(std::move(cp), changes, why)))
                ok = false;
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
                    e._configs.push_back(std::move(entry));
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
                    auto it = std::find_if(e._mediaSlots.begin(), e._mediaSlots.end(),
                                           [&slot](const TTDMediaSlot& s) { return s.slot == slot; });
                    if (it == e._mediaSlots.end())
                    {
                        e._mediaSlots.push_back({slot, format, hasVersions != 0, {}});
                        it = std::prev(e._mediaSlots.end());
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
                  [&](const TTDPortRecord& rec) { e._busVectors.OnRead(rec.port, rec.value, rec.frame, rec.tInFrame, rec.pc); });
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
                        e._mediaReads.Append(frameAt, tInFrame, slot, lba, data, size);
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
                            e._writes.Append(rec);
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
                    e._writes.SetSegments(std::move(segments));
                else
                {
                    e._writes.Clear();   // ancillary: dropped, the session stands
                    report.notes.push_back("the write journal is damaged: left out");
                }
            }
        if (!ok)
        {
            stop(part.index, "damaged events, configuration or journals");
            break;
        }
        report.partsLoaded++;
    }

    report.checkpoints = e.CheckpointCount();
    report.complete = report.partsLoaded == report.partsInFile;
    const bool any = !e._checkpoints.empty();
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
