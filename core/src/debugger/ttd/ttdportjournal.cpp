#include "ttdportjournal.h"

#include <algorithm>
#include <cstring>

#include "ttdcompression.h"

namespace ttd
{

namespace
{

template <typename Pod>
bool WritePod(std::ostream& out, const Pod& value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    return static_cast<bool>(out);
}

template <typename Pod>
bool ReadPod(std::istream& in, Pod& value)
{
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return static_cast<bool>(in);
}

template <typename T>
void PutLE(uint8_t* dst, T value)
{
    for (size_t i = 0; i < sizeof(T); ++i)
        dst[i] = static_cast<uint8_t>(static_cast<uint64_t>(value) >> (8 * i));
}

template <typename T>
T GetLE(const uint8_t* src)
{
    uint64_t v = 0;
    for (size_t i = 0; i < sizeof(T); ++i)
        v |= static_cast<uint64_t>(src[i]) << (8 * i);
    return static_cast<T>(v);
}

/// A claimed record count above this is corruption: 2^40 accesses is weeks of
/// a CPU doing nothing but I/O
constexpr uint64_t kMaxRecords = 1ull << 40;

/// zstd never expands a block beyond its bound; a larger claim is corruption
constexpr uint32_t kMaxCompressedBlock =
    TTDPortJournal::kBlockRecords * TTDPortJournal::kRawRecordBytes + 4096u;

}  // namespace

void TTDPortJournal::Clear()
{
    _mode = Mode::Off;
    _cursor = 0;
    _blocks.clear();
    _blocks.shrink_to_fit();
    _sealedRecords = 0;
    _open.clear();
    _cache = ReadCache{};
    ResetStatistics();
}

void TTDPortJournal::ResetStatistics()
{
    _valueMismatches = 0;
    _divergences = 0;
    _hasFirstMismatch = false;
    _firstMismatch = Mismatch{};
    _hasFirstDivergence = false;
    _firstDivergence = Mismatch{};
}

void TTDPortJournal::StartRecording()
{
    _mode = Mode::Record;
    _cursor = Size();
}

void TTDPortJournal::StartPlayback(uint64_t cursor)
{
    _cursor = cursor;
    _mode = cursor < Size() ? Mode::Play : Mode::Off;
}

void TTDPortJournal::Stop()
{
    _mode = Mode::Off;
}

void TTDPortJournal::RestorePosition(Mode mode, uint64_t cursor)
{
    _cursor = cursor;
    _mode = mode;
    if (_mode == Mode::Play && _cursor >= Size())
        _mode = Mode::Off;
}

uint8_t TTDPortJournal::PlayNext(const TTDPortRecord& live)
{
    TTDPortRecord recorded;
    if (!Get(_cursor, recorded))
    {
        // Past the end (or a block that no longer decodes): the recorded
        // history is over - the machine runs on live devices
        _mode = Mode::Off;
        return live.value;
    }
    const uint64_t index = _cursor++;
    const bool diverged = !recorded.SameAccess(live) || (_direction == Direction::Write && recorded.value != live.value);
    if (diverged)
    {
        ++_divergences;
        if (!_hasFirstDivergence)
        {
            _hasFirstDivergence = true;
            _firstDivergence = Mismatch{index, recorded, live};
        }
    }
    else if (_direction == Direction::Read && recorded.value != live.value)
    {
        ++_valueMismatches;
        if (!_hasFirstMismatch)
        {
            _hasFirstMismatch = true;
            _firstMismatch = Mismatch{index, recorded, live};
        }
    }
    if (_cursor >= Size())
        _mode = Mode::Off;
    return recorded.value;
}

bool TTDPortJournal::Get(uint64_t index, TTDPortRecord& out, ReadCache& cache) const
{
    if (index >= Size())
        return false;
    if (index >= _sealedRecords)
    {
        out = _open[static_cast<size_t>(index - _sealedRecords)];
        return true;
    }
    // Every sealed block holds exactly kBlockRecords records
    const size_t block = static_cast<size_t>(index / kBlockRecords);
    if (!DecodeBlock(block, cache))
        return false;
    out = cache.records[static_cast<size_t>(index % kBlockRecords)];
    return true;
}

uint64_t TTDPortJournal::LowerBound(const TTDTimePoint& time, ReadCache& cache) const
{
    // Skip whole blocks that end before `time`, then scan
    size_t block = 0;
    while (block < _blocks.size() && _blocks[block].lastFrame < time.frame)
        ++block;
    uint64_t index = static_cast<uint64_t>(block) * kBlockRecords;
    TTDPortRecord r;
    while (Get(index, r, cache) && r.Time() < time)
        ++index;
    return std::min(index, Size());
}

std::vector<uint8_t> TTDPortJournal::RawLayout(const std::vector<TTDPortRecord>& records)
{
    const size_t n = records.size();
    std::vector<uint8_t> raw(n * kRawRecordBytes);
    uint8_t* ports = raw.data();
    uint8_t* values = ports + 2 * n;
    uint8_t* pcs = values + n;
    uint8_t* frames = pcs + 2 * n;
    uint8_t* times = frames + 4 * n;
    uint64_t prevFrame = n ? records[0].frame : 0;
    uint32_t prevT = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const TTDPortRecord& r = records[i];
        PutLE<uint16_t>(ports + 2 * i, r.port);
        values[i] = r.value;
        PutLE<uint16_t>(pcs + 2 * i, r.pc);
        const uint32_t frameDelta = static_cast<uint32_t>(r.frame - prevFrame);
        PutLE<uint32_t>(frames + 4 * i, frameDelta);
        // T-state: absolute at the first record and whenever the frame
        // changed, else the step from the previous record (constant in loops)
        const bool absolute = i == 0 || frameDelta != 0;
        PutLE<uint32_t>(times + 4 * i, absolute ? r.tInFrame : r.tInFrame - prevT);
        prevFrame = r.frame;
        prevT = r.tInFrame;
    }
    return raw;
}

bool TTDPortJournal::DecodeRaw(const uint8_t* raw, uint32_t n, uint64_t baseFrame, std::vector<TTDPortRecord>& out)
{
    const uint8_t* ports = raw;
    const uint8_t* values = ports + 2 * n;
    const uint8_t* pcs = values + n;
    const uint8_t* frames = pcs + 2 * n;
    const uint8_t* times = frames + 4 * n;
    out.resize(n);
    uint64_t frame = baseFrame;
    uint32_t t = 0;
    for (uint32_t i = 0; i < n; ++i)
    {
        const uint32_t frameDelta = GetLE<uint32_t>(frames + 4 * i);
        if (i == 0 && frameDelta != 0)
            return false;  // the base frame is the first record's
        frame += frameDelta;
        const uint32_t tField = GetLE<uint32_t>(times + 4 * i);
        t = (i == 0 || frameDelta != 0) ? tField : t + tField;
        TTDPortRecord& r = out[i];
        r.frame = frame;
        r.tInFrame = t;
        r.port = GetLE<uint16_t>(ports + 2 * i);
        r.value = values[i];
        r.pc = GetLE<uint16_t>(pcs + 2 * i);
    }
    return true;
}

TTDPortJournal::Block TTDPortJournal::MakeBlock(const std::vector<TTDPortRecord>& records)
{
    const std::vector<uint8_t> raw = RawLayout(records);
    Block b;
    b.records = static_cast<uint32_t>(records.size());
    b.baseFrame = records.empty() ? 0 : records.front().frame;
    b.lastFrame = records.empty() ? 0 : records.back().frame;
    b.crc = codec::Crc32C(raw.data(), raw.size());
    b.compressed = codec::Compress(raw.data(), raw.size());
    return b;
}

bool TTDPortJournal::DecodeBlock(size_t block, ReadCache& cache) const
{
    if (cache.block == static_cast<int64_t>(block))
        return true;
    if (block >= _blocks.size())
        return false;
    const Block& b = _blocks[block];
    const size_t rawSize = static_cast<size_t>(b.records) * kRawRecordBytes;
    std::vector<uint8_t> raw(rawSize);
    if (!codec::Decompress(b.compressed, rawSize, raw.data()) || codec::Crc32C(raw.data(), rawSize) != b.crc ||
        !DecodeRaw(raw.data(), b.records, b.baseFrame, cache.records))
    {
        cache.block = -1;
        return false;
    }
    cache.block = static_cast<int64_t>(block);
    return true;
}

void TTDPortJournal::SealOpenBlock()
{
    _blocks.push_back(MakeBlock(_open));
    _sealedRecords += _open.size();
    _open.clear();
}

void TTDPortJournal::TruncateTo(uint64_t count)
{
    if (count >= Size())
        return;
    if (count < _sealedRecords)
    {
        // The block holding the cut becomes the open block again, cut to size
        const size_t block = static_cast<size_t>(count / kBlockRecords);
        const uint64_t keep = count % kBlockRecords;
        std::vector<TTDPortRecord> kept;
        if (keep > 0 && DecodeBlock(block, _cache))
            kept.assign(_cache.records.begin(), _cache.records.begin() + static_cast<std::ptrdiff_t>(keep));
        _blocks.resize(block);
        _sealedRecords = static_cast<uint64_t>(block) * kBlockRecords;
        _open = std::move(kept);
        _open.reserve(kBlockRecords);
        _cache = ReadCache{};
    }
    else
    {
        _open.resize(static_cast<size_t>(count - _sealedRecords));
    }
    if (_cursor > Size())
        _cursor = Size();
}

size_t TTDPortJournal::HeapBytes() const
{
    size_t bytes = (_open.capacity() + _cache.records.capacity()) * sizeof(TTDPortRecord) +
                   _blocks.capacity() * sizeof(Block);
    for (const Block& b : _blocks)
        bytes += b.compressed.capacity();
    return bytes;
}

size_t TTDPortJournal::CompressedSlackBytes() const
{
    size_t bytes = 0;
    for (const Block& b : _blocks)
        bytes += b.compressed.capacity() - b.compressed.size();
    return bytes;
}

size_t TTDPortJournal::SerializedBytes() const
{
    constexpr size_t kBlockHeader = 4 + 8 + 4 + 4;
    size_t bytes = 16;  // count, block_records, block_count
    for (const Block& b : _blocks)
        bytes += b.compressed.size() + kBlockHeader;
    if (!_open.empty())
        bytes += MakeBlock(_open).compressed.size() + kBlockHeader;
    return bytes;
}

bool TTDPortJournal::Serialize(std::ostream& out, const std::vector<uint64_t>& cursors, std::string& err) const
{
    // The open block is written sealed; the live journal is not changed
    std::vector<const Block*> blocks;
    blocks.reserve(_blocks.size() + 1);
    for (const Block& b : _blocks)
        blocks.push_back(&b);
    Block open;
    if (!_open.empty())
    {
        open = MakeBlock(_open);
        blocks.push_back(&open);
    }

    const uint64_t count = Size();
    const uint32_t blockRecords = kBlockRecords;
    const uint32_t blockCount = static_cast<uint32_t>(blocks.size());
    if (!WritePod(out, count) || !WritePod(out, blockRecords) || !WritePod(out, blockCount))
    {
        err = "stream write failed (port journal header)";
        return false;
    }
    for (const Block* b : blocks)
    {
        if (b->compressed.empty())
        {
            err = "port journal: block compression failed";
            return false;
        }
        const uint32_t compressedSize = static_cast<uint32_t>(b->compressed.size());
        if (!WritePod(out, b->records) || !WritePod(out, b->baseFrame) || !WritePod(out, b->crc) ||
            !WritePod(out, compressedSize))
        {
            err = "stream write failed (port journal block header)";
            return false;
        }
        out.write(reinterpret_cast<const char*>(b->compressed.data()), compressedSize);
        if (!out)
        {
            err = "stream write failed (port journal block)";
            return false;
        }
    }
    const uint32_t cursorCount = static_cast<uint32_t>(cursors.size());
    if (!WritePod(out, cursorCount))
    {
        err = "stream write failed (port journal cursors)";
        return false;
    }
    for (const uint64_t cursor : cursors)
    {
        if (!WritePod(out, cursor))
        {
            err = "stream write failed (port journal cursor)";
            return false;
        }
    }
    return true;
}

bool TTDPortJournal::Deserialize(std::istream& in, uint32_t checkpointCount, std::vector<uint64_t>& cursors,
                                 std::string& err)
{
    Clear();
    cursors.clear();
    const char* name = _direction == Direction::Read ? "port-read journal: " : "port-write journal: ";
    auto fail = [&](const std::string& message) {
        err = name + message;
        Clear();
        cursors.clear();
        return false;
    };

    uint64_t count = 0;
    uint32_t blockRecords = 0;
    uint32_t blockCount = 0;
    if (!ReadPod(in, count) || !ReadPod(in, blockRecords) || !ReadPod(in, blockCount))
        return fail("truncated header");
    if (count > kMaxRecords)
        return fail("implausible record count " + std::to_string(count));
    if (blockRecords != kBlockRecords)
        return fail("unsupported block size " + std::to_string(blockRecords));
    if (static_cast<uint64_t>(blockCount) != (count + kBlockRecords - 1) / kBlockRecords)
        return fail(std::to_string(blockCount) + " blocks cannot hold " + std::to_string(count) + " records");

    uint64_t total = 0;
    TTDTimePoint previous{0, 0};
    std::vector<TTDPortRecord> decoded;
    for (uint32_t i = 0; i < blockCount; ++i)
    {
        Block b;
        uint32_t compressedSize = 0;
        if (!ReadPod(in, b.records) || !ReadPod(in, b.baseFrame) || !ReadPod(in, b.crc) ||
            !ReadPod(in, compressedSize))
            return fail("truncated block " + std::to_string(i));
        const bool last = i + 1 == blockCount;
        if (b.records == 0 || b.records > kBlockRecords || (!last && b.records != kBlockRecords))
            return fail("block " + std::to_string(i) + " claims " + std::to_string(b.records) + " records");
        if (compressedSize > kMaxCompressedBlock)
            return fail("block " + std::to_string(i) + " is implausibly large");
        if (!codec::ReadExact(in, compressedSize, b.compressed))
            return fail("truncated block " + std::to_string(i) + " payload");

        const size_t rawSize = static_cast<size_t>(b.records) * kRawRecordBytes;
        std::vector<uint8_t> raw(rawSize);
        if (!codec::Decompress(b.compressed, rawSize, raw.data()))
            return fail("block " + std::to_string(i) + " does not decompress");
        if (codec::Crc32C(raw.data(), rawSize) != b.crc)
            return fail("block " + std::to_string(i) + " fails its CRC");
        if (!DecodeRaw(raw.data(), b.records, b.baseFrame, decoded))
            return fail("block " + std::to_string(i) + " does not start at its base frame");
        for (const TTDPortRecord& r : decoded)
        {
            if (r.Time() < previous)
                return fail("record " + std::to_string(total) + " is earlier than the one before it");
            previous = r.Time();
            ++total;
        }
        b.lastFrame = decoded.back().frame;

        if (last && b.records < kBlockRecords)
        {
            _open = std::move(decoded);  // the last partial block is the open block again
            decoded.clear();
        }
        else
        {
            _sealedRecords += b.records;
            _blocks.push_back(std::move(b));
        }
    }
    if (total != count)
        return fail("blocks hold " + std::to_string(total) + " records, the header claims " + std::to_string(count));

    uint32_t cursorCount = 0;
    if (!ReadPod(in, cursorCount))
        return fail("truncated cursor list");
    if (cursorCount != checkpointCount)
        return fail(std::to_string(cursorCount) + " cursors for " + std::to_string(checkpointCount) + " checkpoints");
    cursors.resize(cursorCount);
    for (uint32_t i = 0; i < cursorCount; ++i)
    {
        if (!ReadPod(in, cursors[i]))
            return fail("truncated cursor " + std::to_string(i));
        if (cursors[i] > count || (i > 0 && cursors[i] < cursors[i - 1]))
            return fail("cursor " + std::to_string(i) + " is out of order or past the end");
    }
    return true;
}

}  // namespace ttd
