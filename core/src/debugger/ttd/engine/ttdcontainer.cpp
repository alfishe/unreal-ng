#include "debugger/ttd/engine/ttdcontainer.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <iterator>

#include "debugger/ttd/engine/ttdbytes.h"
#include "debugger/ttd/ttdcompression.h"
#include "platform/fileio.h"

namespace ttd
{

namespace
{
constexpr uint8_t kMagic[4] = {'T', 'T', 'D', 'D'};
constexpr uint8_t kSync[4] = {'T', 'R', 'E', 'C'};
constexpr uint8_t kTrailerMagic[4] = {'T', 'T', 'D', 'X'};
constexpr uint16_t kFlagCompressed = 1;
constexpr uint16_t kFlagPartEnd = 2;
constexpr uint16_t kIndexVersion = 1;
constexpr uint32_t kMaxRawSize = 1u << 30;        ///< a record never decompresses to more (allocation bound)
constexpr uint32_t kMaxHeaderSize = 64u << 20;

using ByteWriter = TTDByteWriter;
using ByteReader = TTDByteReader;

void PutU32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
uint32_t GetU32(const uint8_t* p)
{
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

std::vector<uint8_t> EncodeRecordHeader(uint16_t streamId, uint16_t flags, uint32_t part, uint32_t sequence,
                                        uint32_t stored, uint32_t raw, uint32_t payloadCrc)
{
    ByteWriter w;
    w.Bytes(kSync, 4);
    w.U16(streamId);
    w.U16(flags);
    w.U32(part);
    w.U32(sequence);
    w.U32(stored);
    w.U32(raw);
    w.U32(payloadCrc);
    w.U32(codec::Crc32C(w.bytes.data(), w.bytes.size()));
    return std::move(w.bytes);
}
}  // namespace

const TTDStreamDesc* TTDContainerHeader::Stream(uint16_t id) const
{
    for (const TTDStreamDesc& s : streams)
        if (s.id == id)
            return &s;
    return nullptr;
}

/// region <Sinks and sources>

bool TTDMemorySink::Write(const uint8_t* data, size_t size)
{
    bytes.insert(bytes.end(), data, data + size);
    return true;
}

bool TTDMemorySource::ReadAt(uint64_t offset, uint8_t* out, size_t size) const
{
    if (offset > bytes.size() || size > bytes.size() - offset)
        return false;
    std::memcpy(out, bytes.data() + offset, size);
    return true;
}

TTDFileSink::TTDFileSink(const std::string& utf8Path) : _file(platform::AppendFile::Create(utf8Path, &_error)) {}
TTDFileSink::~TTDFileSink() = default;
bool TTDFileSink::Write(const uint8_t* data, size_t size) { return _file && _file->Write(data, size); }
bool TTDFileSink::Sync() { return _file && _file->Sync(); }
uint64_t TTDFileSink::Size() const { return _file ? _file->Size() : 0; }
void TTDFileSink::Close()
{
    if (_file)
        _file->Close();
    _file.reset();
}

TTDFileSource::TTDFileSource(const std::string& utf8Path) : _file(platform::RandomAccessFile::Open(utf8Path, &_error)) {}
TTDFileSource::~TTDFileSource() = default;
uint64_t TTDFileSource::Size() const { return _file ? _file->Size() : 0; }
bool TTDFileSource::ReadAt(uint64_t offset, uint8_t* out, size_t size) const
{
    return _file && _file->ReadAt(offset, out, size);
}

/// endregion </Sinks and sources>

/// region <Writer>

bool TTDContainerWriter::Begin(ITTDByteSink& sink, const TTDContainerHeader& header, std::string* error)
{
    _sink = &sink;
    _failed = false;
    _partIndex = 0;
    _sequence = 0;
    _partRecords.clear();
    _parts.clear();
    _totals.clear();

    ByteWriter w;
    w.Bytes(kMagic, 4);
    w.U16(kContainerSchema);
    w.U16(header.flags);
    w.U32(0);   // header size, patched below
    w.U32(0);   // header CRC, patched below
    w.Bytes(header.uuid.data(), header.uuid.size());
    w.U64(header.createdMicros);
    w.U32(static_cast<uint32_t>(ZSTD_versionNumber()));
    w.bytes.resize(kContainerFixedHeader, 0);
    w.U16(static_cast<uint16_t>(header.streams.size()));
    for (const TTDStreamDesc& s : header.streams)
    {
        w.U16(s.id);
        w.U16(s.layoutVersion);
        w.U8(static_cast<uint8_t>(s.kind));
        const size_t length = std::min<size_t>(s.name.size(), 255);
        w.U8(static_cast<uint8_t>(length));
        w.Bytes(reinterpret_cast<const uint8_t*>(s.name.data()), length);
    }
    w.U32(static_cast<uint32_t>(header.sessionTables.size()));
    w.Bytes(header.sessionTables.data(), header.sessionTables.size());

    PutU32(w.bytes.data() + 8, static_cast<uint32_t>(w.bytes.size()));
    PutU32(w.bytes.data() + 12, codec::Crc32C(w.bytes.data(), w.bytes.size()));
    if (!_sink->Write(w.bytes.data(), w.bytes.size()))
    {
        _failed = true;
        if (error)
            *error = "the header could not be written";
        return false;
    }
    return true;
}

bool TTDContainerWriter::WriteRecord(uint16_t streamId, uint16_t flags, const uint8_t* stored, size_t storedSize,
                                     size_t rawSize)
{
    if (!_sink || _failed || storedSize > UINT32_MAX || rawSize > kMaxRawSize)
    {
        _failed = true;
        return false;
    }
    const uint32_t crc = codec::Crc32C(stored, storedSize);
    const std::vector<uint8_t> header =
        EncodeRecordHeader(streamId, flags, _partIndex, _sequence++, static_cast<uint32_t>(storedSize),
                           static_cast<uint32_t>(rawSize), crc);
    if (!_sink->Write(header.data(), header.size()) || (storedSize && !_sink->Write(stored, storedSize)))
    {
        _failed = true;
        return false;
    }
    return true;
}

bool TTDContainerWriter::AddRecord(uint16_t streamId, const uint8_t* data, size_t size, bool compress)
{
    if (streamId == kContainerStream || !_sink || _failed)
        return false;
    const uint64_t offset = _sink->Size();
    std::vector<uint8_t> packed;
    if (compress && size > 0)
        packed = codec::Compress(data, size);
    const bool usePacked = !packed.empty() && packed.size() < size;
    const uint8_t* stored = usePacked ? packed.data() : data;
    const size_t storedSize = usePacked ? packed.size() : size;
    if (!WriteRecord(streamId, usePacked ? kFlagCompressed : 0, stored, storedSize, size))
        return false;
    _partRecords.push_back(offset);

    auto total = std::find_if(_totals.begin(), _totals.end(), [streamId](const auto& t) { return t.first == streamId; });
    if (total == _totals.end())
    {
        _totals.push_back({streamId, {}});
        total = std::prev(_totals.end());
    }
    total->second.records++;
    total->second.stored += storedSize;
    total->second.raw += size;
    return true;
}

bool TTDContainerWriter::EndPart(const TTDPartEnd& part, bool sync)
{
    if (!_sink || _failed)
        return false;
    ByteWriter w;
    w.U64(part.firstFrame);
    w.U32(part.frameCount);
    w.U16(part.branch);
    w.U32(static_cast<uint32_t>(_partRecords.size()));
    for (uint64_t offset : _partRecords)
        w.U64(offset);
    std::vector<uint32_t> deps = part.dependencies;
    std::sort(deps.begin(), deps.end());
    deps.erase(std::unique(deps.begin(), deps.end()), deps.end());
    w.Varint(deps.size());
    uint32_t previous = 0;
    for (uint32_t d : deps)
    {
        w.Varint(d - previous);
        previous = d;
    }
    w.Varint(part.extra.size());
    w.Bytes(part.extra.data(), part.extra.size());

    const uint64_t offset = _sink->Size();
    if (!WriteRecord(kContainerStream, kFlagPartEnd, w.bytes.data(), w.bytes.size(), w.bytes.size()))
        return false;
    if (sync && !_sink->Sync())
    {
        _failed = true;
        return false;
    }
    _parts.push_back({offset, part.firstFrame, part.frameCount, part.branch});
    _partRecords.clear();
    ++_partIndex;
    return true;
}

bool TTDContainerWriter::Finalize(const std::vector<uint8_t>& indexExtra)
{
    if (!_sink || _failed)
        return false;
    ByteWriter w;
    w.U16(kIndexVersion);
    w.U32(static_cast<uint32_t>(_parts.size()));
    for (const PartEntry& p : _parts)
    {
        w.U64(p.partEndOffset);
        w.U64(p.firstFrame);
        w.U32(p.frameCount);
        w.U16(p.branch);
    }
    w.U16(static_cast<uint16_t>(_totals.size()));
    for (const auto& [id, t] : _totals)
    {
        w.U16(id);
        w.U64(t.records);
        w.U64(t.stored);
        w.U64(t.raw);
    }
    w.U32(static_cast<uint32_t>(indexExtra.size()));
    w.Bytes(indexExtra.data(), indexExtra.size());

    const uint64_t indexOffset = _sink->Size();
    ByteWriter t;
    t.U64(indexOffset);
    t.U32(static_cast<uint32_t>(w.bytes.size()));
    t.U32(codec::Crc32C(w.bytes.data(), w.bytes.size()));
    t.Bytes(kTrailerMagic, 4);
    t.U32(codec::Crc32C(t.bytes.data(), t.bytes.size()));
    if (!_sink->Write(w.bytes.data(), w.bytes.size()) || !_sink->Write(t.bytes.data(), t.bytes.size()) ||
        !_sink->Sync())
    {
        _failed = true;
        return false;
    }
    return true;
}

/// endregion </Writer>

/// region <Reader>

bool TTDContainerReader::Open(const ITTDByteSource& source, std::string& error,
                              const std::function<bool(uint16_t)>& knownStream)
{
    _source = &source;
    _parts.clear();
    _notes.clear();
    _indexExtra.clear();
    _finalized = false;
    if (!ReadHeader(error))
        return false;

    for (const TTDStreamDesc& s : _header.streams)
    {
        if (!knownStream || knownStream(s.id))
            continue;
        if (s.kind == TTDStreamKind::Required)
        {
            error = "the file needs stream '" + s.name + "' (id " + std::to_string(s.id) +
                    ") that this version does not know";
            return false;
        }
        _notes.push_back("ancillary stream '" + s.name + "' (id " + std::to_string(s.id) + ") skipped");
    }

    std::string indexError;
    if (ReadIndex(indexError))
        _finalized = true;
    else
    {
        _notes.push_back("no valid index (" + indexError + "): opened by scanning");
        Scan();
    }
    return true;
}

bool TTDContainerReader::ReadHeader(std::string& error)
{
    uint8_t fixed[kContainerFixedHeader];
    if (_source->Size() < kContainerFixedHeader || !_source->ReadAt(0, fixed, sizeof(fixed)))
    {
        error = "too short for a session file";
        return false;
    }
    if (std::memcmp(fixed, kMagic, 4) != 0)
    {
        error = "not a TTD session file";
        return false;
    }
    uint16_t schema = 0;
    std::memcpy(&schema, fixed + 4, 2);
    if (schema != kContainerSchema)
    {
        error = "unsupported schema v" + std::to_string(schema);
        return false;
    }
    const uint32_t size = GetU32(fixed + 8);
    if (size < kContainerFixedHeader || size > kMaxHeaderSize || size > _source->Size())
    {
        error = "damaged header (size)";
        return false;
    }
    std::vector<uint8_t> bytes(size);
    if (!_source->ReadAt(0, bytes.data(), size))
    {
        error = "cannot read the header";
        return false;
    }
    const uint32_t crc = GetU32(bytes.data() + 12);
    PutU32(bytes.data() + 12, 0);
    if (codec::Crc32C(bytes.data(), bytes.size()) != crc)
    {
        error = "damaged header (CRC)";
        return false;
    }

    ByteReader r(bytes.data() + 6, bytes.size() - 6);
    TTDContainerHeader h;
    uint32_t ignored = 0;
    std::vector<uint8_t> uuid;
    bool ok = r.U16(h.flags) && r.U32(ignored) && r.U32(ignored) && r.Bytes(uuid, 16) && r.U64(h.createdMicros) &&
              r.U32(h.zstdVersion) && r.Skip(kContainerFixedHeader - 44);
    if (ok)
        std::copy(uuid.begin(), uuid.end(), h.uuid.begin());
    uint16_t count = 0;
    ok = ok && r.U16(count);
    for (uint16_t i = 0; ok && i < count; ++i)
    {
        TTDStreamDesc s;
        uint8_t kind = 0, length = 0;
        std::vector<uint8_t> name;
        ok = r.U16(s.id) && r.U16(s.layoutVersion) && r.U8(kind) && r.U8(length) && r.Bytes(name, length);
        s.kind = kind == 0 ? TTDStreamKind::Required : TTDStreamKind::Ancillary;
        s.name.assign(name.begin(), name.end());
        h.streams.push_back(std::move(s));
    }
    uint32_t tables = 0;
    ok = ok && r.U32(tables) && r.Bytes(h.sessionTables, tables);
    if (!ok)
    {
        error = "damaged header (tables)";
        return false;
    }
    _header = std::move(h);
    _dataStart = size;
    return true;
}

bool TTDContainerReader::ReadRecordHeader(uint64_t offset, TTDRecordRef& out) const
{
    uint8_t h[kRecordHeaderSize];
    const uint64_t fileSize = _source->Size();
    if (offset + kRecordHeaderSize > fileSize || !_source->ReadAt(offset, h, sizeof(h)))
        return false;
    if (std::memcmp(h, kSync, 4) != 0 || codec::Crc32C(h, 28) != GetU32(h + 28))
        return false;
    out.offset = offset;
    std::memcpy(&out.streamId, h + 4, 2);
    std::memcpy(&out.flags, h + 6, 2);
    out.partIndex = GetU32(h + 8);
    out.sequence = GetU32(h + 12);
    out.storedSize = GetU32(h + 16);
    out.rawSize = GetU32(h + 20);
    out.payloadCrc = GetU32(h + 24);
    return out.rawSize <= kMaxRawSize && offset + kRecordHeaderSize + out.storedSize <= fileSize &&
           ((out.flags & kFlagCompressed) != 0 || out.rawSize == out.storedSize);
}

bool TTDContainerReader::ParsePartEnd(const TTDRecordRef& partEnd, TTDPartRef& out, std::string& why) const
{
    if (partEnd.streamId != kContainerStream || (partEnd.flags & kFlagPartEnd) == 0)
    {
        why = "not a part-end record";
        return false;
    }
    std::vector<uint8_t> payload(partEnd.storedSize);
    if (!_source->ReadAt(partEnd.offset + kRecordHeaderSize, payload.data(), payload.size()) ||
        codec::Crc32C(payload.data(), payload.size()) != partEnd.payloadCrc)
    {
        why = "damaged part-end record";
        return false;
    }
    ByteReader r(payload.data(), payload.size());
    uint32_t records = 0;
    if (!r.U64(out.firstFrame) || !r.U32(out.frameCount) || !r.U16(out.branch) || !r.U32(records) ||
        r.Left() / 8 < records)
    {
        why = "damaged part-end record (fields)";
        return false;
    }
    out.index = partEnd.partIndex;
    out.records.clear();
    bool recordsOk = true;
    for (uint32_t i = 0; i < records; ++i)
    {
        uint64_t offset = 0;
        r.U64(offset);
        TTDRecordRef record;
        if (recordsOk && (!ReadRecordHeader(offset, record) || record.partIndex != out.index ||
                          record.streamId == kContainerStream))
        {
            recordsOk = false;
            why = "a record header of the part is damaged (at " + std::to_string(offset) + ")";
        }
        out.records.push_back(record);
    }
    uint64_t depCount = 0, length = 0;
    if (!r.Varint(depCount) || depCount > r.Left())
    {
        why = "damaged part-end record (dependencies)";
        return false;
    }
    uint64_t previous = 0;
    for (uint64_t i = 0; i < depCount; ++i)
    {
        uint64_t delta = 0;
        if (!r.Varint(delta))
        {
            why = "damaged part-end record (dependencies)";
            return false;
        }
        previous += delta;
        out.dependencies.push_back(static_cast<uint32_t>(previous));
    }
    if (!r.Varint(length) || !r.Bytes(out.extra, static_cast<size_t>(length)))
    {
        why = "damaged part-end record (extra)";
        return false;
    }
    if (!recordsOk)
    {
        out.damaged = true;
        out.damage = why;
    }
    return true;
}

bool TTDContainerReader::ReadIndex(std::string& error)
{
    const uint64_t size = _source->Size();
    uint8_t t[kTrailerSize];
    if (size < _dataStart + kTrailerSize || !_source->ReadAt(size - kTrailerSize, t, sizeof(t)))
    {
        error = "no trailer";
        return false;
    }
    if (std::memcmp(t + 16, kTrailerMagic, 4) != 0 || codec::Crc32C(t, 20) != GetU32(t + 20))
    {
        error = "no trailer";
        return false;
    }
    uint64_t indexOffset = 0;
    std::memcpy(&indexOffset, t, 8);
    const uint32_t indexSize = GetU32(t + 8);
    if (indexOffset < _dataStart || indexOffset + indexSize != size - kTrailerSize)
    {
        error = "damaged trailer";
        return false;
    }
    std::vector<uint8_t> index(indexSize);
    if (!_source->ReadAt(indexOffset, index.data(), index.size()) ||
        codec::Crc32C(index.data(), index.size()) != GetU32(t + 12))
    {
        error = "damaged index";
        return false;
    }

    ByteReader r(index.data(), index.size());
    uint16_t version = 0;
    uint32_t parts = 0;
    if (!r.U16(version) || version != kIndexVersion || !r.U32(parts) || r.Left() / 22 < parts)
    {
        error = "damaged index (parts)";
        return false;
    }
    std::vector<TTDPartRef> found;
    for (uint32_t i = 0; i < parts; ++i)
    {
        uint64_t partEnd = 0;
        TTDPartRef part;
        r.U64(partEnd);
        r.U64(part.firstFrame);
        r.U32(part.frameCount);
        r.U16(part.branch);
        part.index = i;
        TTDRecordRef record;
        std::string why;
        TTDPartRef parsed;
        if (!ReadRecordHeader(partEnd, record))
        {
            part.damaged = true;
            part.damage = "damaged part-end record header";
        }
        else if (!ParsePartEnd(record, parsed, why))
        {
            part.damaged = true;
            part.damage = why;
        }
        else
            part = std::move(parsed);
        if (part.damaged)
            _notes.push_back("part " + std::to_string(i) + ": " + part.damage);
        found.push_back(std::move(part));
    }
    uint16_t streams = 0;
    if (!r.U16(streams) || !r.Skip(static_cast<size_t>(streams) * 26))
    {
        error = "damaged index (streams)";
        return false;
    }
    uint32_t extra = 0;
    if (!r.U32(extra) || !r.Bytes(_indexExtra, extra))
    {
        error = "damaged index (extra)";
        return false;
    }
    _parts = std::move(found);
    return true;
}

void TTDContainerReader::Scan()
{
    const uint64_t size = _source->Size();
    uint64_t offset = _dataStart;
    size_t pending = 0;   // records seen since the last part end
    std::vector<TTDPartRef> found;
    while (offset + kRecordHeaderSize <= size)
    {
        TTDRecordRef record;
        if (ReadRecordHeader(offset, record))
        {
            if (record.streamId == kContainerStream)
            {
                TTDPartRef part;
                std::string why;
                if (ParsePartEnd(record, part, why))
                {
                    if (part.damaged)
                        _notes.push_back("part " + std::to_string(part.index) + ": " + part.damage);
                    found.push_back(std::move(part));
                }
                else
                    _notes.push_back("part " + std::to_string(record.partIndex) + ": " + why);
                pending = 0;
            }
            else
                ++pending;
            offset += kRecordHeaderSize + record.storedSize;
            continue;
        }

        // Damaged bytes: find the next record header that checks out
        const uint64_t damagedAt = offset;
        uint64_t next = size;
        std::vector<uint8_t> chunk(64 * 1024);
        for (uint64_t at = offset + 1; at + kRecordHeaderSize <= size && next == size;)
        {
            const size_t n = static_cast<size_t>(std::min<uint64_t>(chunk.size(), size - at));
            if (!_source->ReadAt(at, chunk.data(), n))
                break;
            for (size_t i = 0; i + 4 <= n; ++i)
            {
                TTDRecordRef probe;
                if (std::memcmp(chunk.data() + i, kSync, 4) == 0 && ReadRecordHeader(at + i, probe))
                {
                    next = at + i;
                    break;
                }
            }
            at += n > 3 ? n - 3 : n;
        }
        if (next < size)
            _notes.push_back("damaged bytes " + std::to_string(damagedAt) + "-" + std::to_string(next) + " skipped");
        offset = next;
    }
    if (pending > 0)
        _notes.push_back("incomplete last part dropped (" + std::to_string(pending) + " record(s) without a part end)");

    // Parts in index order; a part whose part-end was lost is a damaged placeholder
    std::sort(found.begin(), found.end(), [](const TTDPartRef& a, const TTDPartRef& b) { return a.index < b.index; });
    _parts.clear();
    for (TTDPartRef& part : found)
    {
        if (!_parts.empty() && part.index == _parts.back().index)
            continue;
        while (_parts.size() < part.index)
        {
            TTDPartRef lost;
            lost.index = static_cast<uint32_t>(_parts.size());
            lost.damaged = true;
            lost.damage = "part-end record lost";
            _parts.push_back(std::move(lost));
        }
        _parts.push_back(std::move(part));
    }
}

bool TTDContainerReader::ReadRecord(const TTDRecordRef& record, std::vector<uint8_t>& out, std::string* error)
{
    auto fail = [&](const std::string& why) {
        MarkDamaged(record.partIndex, why);
        if (error)
            *error = why;
        return false;
    };
    std::vector<uint8_t> stored(record.storedSize);
    if (!_source || !_source->ReadAt(record.offset + kRecordHeaderSize, stored.data(), stored.size()))
        return fail("cannot read the record at " + std::to_string(record.offset));
    if (codec::Crc32C(stored.data(), stored.size()) != record.payloadCrc)
        return fail("damaged record at " + std::to_string(record.offset) + " (CRC)");
    if ((record.flags & kFlagCompressed) == 0)
    {
        out = std::move(stored);
        return true;
    }
    out.resize(record.rawSize);
    if (record.rawSize > 0 && !codec::Decompress(stored.data(), stored.size(), record.rawSize, out.data()))
        return fail("damaged record at " + std::to_string(record.offset) + " (decompression)");
    return true;
}

void TTDContainerReader::MarkDamaged(uint32_t partIndex, const std::string& reason)
{
    if (partIndex >= _parts.size() || _parts[partIndex].damaged)
        return;
    _parts[partIndex].damaged = true;
    _parts[partIndex].damage = reason;
}

bool TTDContainerReader::IsReachable(uint32_t partIndex) const
{
    if (partIndex >= _parts.size())
        return false;
    // Dependencies point to earlier parts only: walk them once each
    std::vector<uint8_t> state(_parts.size(), 0);   // 0 unknown, 1 reachable, 2 not
    std::function<bool(uint32_t)> reachable = [&](uint32_t i) -> bool {
        if (state[i] != 0)
            return state[i] == 1;
        bool ok = !_parts[i].damaged;
        for (uint32_t d : _parts[i].dependencies)
            if (ok && (d >= i || !reachable(d)))
                ok = false;
        state[i] = ok ? 1 : 2;
        return ok;
    };
    return reachable(partIndex);
}

/// endregion </Reader>

}  // namespace ttd
