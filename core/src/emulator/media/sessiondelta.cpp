#include "stdafx.h"

#include "sessiondelta.h"

#include <zstd.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <system_error>

#include "common/filehelper.h"
#include "emulator/io/storage/sessionwritemap.h"

namespace
{
    constexpr char kMagic[8] = {'U', 'N', 'G', 'D', 'E', 'L', 'T', 'A'};
    constexpr char kEnd[8] = {'U', 'N', 'G', 'D', 'E', 'N', 'D', '!'};
    constexpr size_t kSector = 512;
    constexpr size_t kChunk = 1024 * 1024;
    constexpr uint8_t kStored = 0;
    constexpr uint8_t kZstd = 1;

    /// Written straight to the file: a delta of a spilled session is larger than memory
    class Out
    {
    public:
        explicit Out(std::ostream& stream) : _stream(stream) {}
        void Bytes(const void* data, size_t size) { _stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size)); }
        void U8(uint8_t v) { Bytes(&v, 1); }
        void U16(uint16_t v) { Int(v); }
        void U32(uint32_t v) { Int(v); }
        void U64(uint64_t v) { Int(v); }
        std::streamoff Position() { return static_cast<std::streamoff>(_stream.tellp()); }
        /// Write `v` at `at`, then carry on at the end
        void PatchU32(std::streamoff at, uint32_t v)
        {
            const std::streampos end = _stream.tellp();
            _stream.seekp(at);
            U32(v);
            _stream.seekp(end);
        }

    private:
        template <typename T>
        void Int(T v)
        {
            uint8_t b[sizeof(T)];
            for (size_t i = 0; i < sizeof(T); i++)
                b[i] = static_cast<uint8_t>(v >> (8 * i));
            Bytes(b, sizeof b);
        }
        std::ostream& _stream;
    };

    /// Read from the file as it goes (the chunks twice: checked, then applied)
    class In
    {
    public:
        explicit In(std::istream& stream) : _stream(stream) {}
        bool Bytes(void* dst, size_t size)
        {
            _stream.read(static_cast<char*>(dst), static_cast<std::streamsize>(size));
            return static_cast<size_t>(_stream.gcount()) == size;
        }
        template <typename T>
        bool Int(T& v)
        {
            uint8_t b[sizeof(T)];
            if (!Bytes(b, sizeof b))
                return false;
            v = 0;
            for (size_t i = 0; i < sizeof(T); i++)
                v |= static_cast<T>(static_cast<T>(b[i]) << (8 * i));
            return true;
        }
        bool AtEnd() { return _stream.peek() == std::char_traits<char>::eof(); }
        std::streampos Position() { return _stream.tellg(); }
        void Seek(std::streampos at)
        {
            _stream.clear();
            _stream.seekg(at);
        }

    private:
        std::istream& _stream;
    };

    uint64_t Fnv(uint64_t h, const uint8_t* data, size_t size)
    {
        for (size_t i = 0; i < size; i++)
        {
            h ^= data[i];
            h *= 0x100000001b3ULL;
        }
        return h;
    }

    /// Which layers differ between the delta's and the composite's
    std::string Differences(const DeltaIdentity& file, const DeltaIdentity& now)
    {
        std::string text;
        auto add = [&text](const std::string& line) { text += (text.empty() ? "" : ", ") + line; };
        for (const DeltaIdentity::Layer& layer : now.layers)
        {
            const auto it = std::find_if(file.layers.begin(), file.layers.end(),
                                         [&layer](const DeltaIdentity::Layer& l) { return l.name == layer.name; });
            if (it == file.layers.end())
                add("layer '" + layer.name + "' is new");
            else if (it->identity != layer.identity)
                add("layer '" + layer.name + "' changed");
        }
        for (const DeltaIdentity::Layer& layer : file.layers)
        {
            if (std::none_of(now.layers.begin(), now.layers.end(), [&layer](const DeltaIdentity::Layer& l) { return l.name == layer.name; }))
                add("layer '" + layer.name + "' is gone");
        }
        return text.empty() ? std::string("the composite's options changed") : text;
    }
}  // namespace

MediaResult SessionDelta::Save(const std::filesystem::path& path, const SessionWriteMap& map, const DeltaIdentity& identity)
{
    std::filesystem::path temp = path;
    temp += ".writing";
    auto fail = [&temp](const std::string& why) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        return MediaResult::Fail(MediaError::IoError, why);
    };

    const uint64_t changed = map.ChangedSectors();
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file)
            return fail("cannot write " + FileHelper::FromFsPath(temp));
        Out out(file);
        out.Bytes(kMagic, sizeof kMagic);
        out.U32(kVersion);
        out.U32(0);
        out.U64(identity.contentId);
        out.U64(identity.sectorCount);
        out.U64(changed);
        const std::streamoff runCountAt = out.Position();
        out.U32(0);  // the run count, written once the runs are known
        out.U32(static_cast<uint32_t>(identity.layers.size()));
        for (const DeltaIdentity::Layer& layer : identity.layers)
        {
            out.U64(layer.identity);
            const std::string name = layer.name.substr(0, 0xFFFF);
            out.U16(static_cast<uint16_t>(name.size()));
            out.Bytes(name.data(), name.size());
        }

        // Runs of consecutive sectors, written as they close
        uint32_t runCount = 0;
        uint64_t runFirst = 0;
        uint32_t runLength = 0;
        for (std::optional<uint64_t> lba = map.NextChanged(0); lba; lba = map.NextChanged(*lba + 1))
        {
            if (runLength && runFirst + runLength == *lba && runLength < 0xFFFFFFFFu)
            {
                runLength++;
                continue;
            }
            if (runLength)
            {
                out.U64(runFirst);
                out.U32(runLength);
                runCount++;
            }
            runFirst = *lba;
            runLength = 1;
        }
        if (runLength)
        {
            out.U64(runFirst);
            out.U32(runLength);
            runCount++;
        }
        out.PatchU32(runCountAt, runCount);

        // The sectors in LBA order, compressed per 1 MiB
        const uint64_t raw = changed * kSector;
        out.U32(static_cast<uint32_t>((raw + kChunk - 1) / kChunk));
        std::vector<uint8_t> chunk;
        chunk.reserve(static_cast<size_t>(std::min<uint64_t>(raw, kChunk)));
        std::vector<uint8_t> packed(ZSTD_compressBound(kChunk));
        uint64_t hash = 0xcbf29ce484222325ULL;
        auto flush = [&]() {
            hash = Fnv(hash, chunk.data(), chunk.size());
            const size_t n = ZSTD_compress(packed.data(), packed.size(), chunk.data(), chunk.size(), 3);
            const bool zstd = !ZSTD_isError(n) && n < chunk.size();
            out.U32(static_cast<uint32_t>(chunk.size()));
            out.U32(static_cast<uint32_t>(zstd ? n : chunk.size()));
            out.U8(zstd ? kZstd : kStored);
            out.U8(0);
            out.U8(0);
            out.U8(0);
            if (zstd)
                out.Bytes(packed.data(), n);
            else
                out.Bytes(chunk.data(), chunk.size());
            chunk.clear();
        };
        uint64_t written = 0;
        const bool read = map.ForEachChange([&](uint64_t, const uint8_t* data) {
            chunk.insert(chunk.end(), data, data + kSector);
            written++;
            if (chunk.size() == kChunk)
                flush();
            return true;
        });
        if (!read || written != changed)
            return fail("a changed sector cannot be read back from the session's spill file");
        if (!chunk.empty())
            flush();
        out.Bytes(kEnd, sizeof kEnd);
        out.U64(hash);
        file.flush();
        if (!file)
            return fail("cannot write " + FileHelper::FromFsPath(temp));
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec)
        return fail("cannot replace " + FileHelper::FromFsPath(path) + ": " + ec.message());
    return MediaResult::Success();
}

DeltaLoad SessionDelta::Load(const std::filesystem::path& path, SessionWriteMap& map, const DeltaIdentity& identity, std::string& detail)
{
    detail.clear();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        return DeltaLoad::Missing;
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        detail = "cannot be read";
        return DeltaLoad::Damaged;
    }

    In in(file);
    char magic[8];
    uint32_t version = 0, flags = 0, runCount = 0, layerCount = 0, chunkCount = 0;
    DeltaIdentity written;
    uint64_t changed = 0;
    if (!in.Bytes(magic, sizeof magic) || std::memcmp(magic, kMagic, sizeof magic) != 0)
    {
        detail = "not a session delta";
        return DeltaLoad::Damaged;
    }
    if (!in.Int(version) || version != kVersion || !in.Int(flags))
    {
        detail = "version " + std::to_string(version) + " is not known";
        return DeltaLoad::Damaged;
    }
    if (!in.Int(written.contentId) || !in.Int(written.sectorCount) || !in.Int(changed) || !in.Int(runCount) || !in.Int(layerCount))
    {
        detail = "the header is cut short";
        return DeltaLoad::Damaged;
    }
    for (uint32_t i = 0; i < layerCount; i++)
    {
        DeltaIdentity::Layer layer;
        uint16_t length = 0;
        if (!in.Int(layer.identity) || !in.Int(length))
        {
            detail = "the header is cut short";
            return DeltaLoad::Damaged;
        }
        layer.name.resize(length);
        if (!in.Bytes(layer.name.data(), length))
        {
            detail = "the header is cut short";
            return DeltaLoad::Damaged;
        }
        written.layers.push_back(std::move(layer));
    }
    if (written.contentId != identity.contentId)
    {
        detail = Differences(written, identity);
        return DeltaLoad::Mismatch;
    }
    if (written.sectorCount != identity.sectorCount)
    {
        detail = "written over " + std::to_string(written.sectorCount) + " sectors, the composite has " +
                 std::to_string(identity.sectorCount);
        return DeltaLoad::Damaged;
    }

    const uint64_t fileSize = std::filesystem::file_size(path, ec);
    if (ec || static_cast<uint64_t>(runCount) * 12 > fileSize)
    {
        detail = "the run table is damaged";
        return DeltaLoad::Damaged;
    }
    std::vector<std::pair<uint64_t, uint32_t>> runs(runCount);
    uint64_t total = 0;
    for (auto& [lba, count] : runs)
    {
        if (!in.Int(lba) || !in.Int(count) || lba + count > identity.sectorCount || count == 0)
        {
            detail = "the run table is damaged";
            return DeltaLoad::Damaged;
        }
        total += count;
    }
    if (total != changed || !in.Int(chunkCount))
    {
        detail = "the run table is damaged";
        return DeltaLoad::Damaged;
    }

    // Two passes over the chunks: the first checks the whole file (a damaged delta changes nothing), the second
    // applies it. One chunk in memory at a time
    const std::streampos chunksAt = in.Position();
    std::vector<uint8_t> payload;
    std::vector<uint8_t> chunk(kChunk);
    auto readChunk = [&](uint32_t c, size_t& rawBytes) {
        uint32_t raw = 0, stored = 0;
        uint8_t header[4];
        if (!in.Int(raw) || !in.Int(stored) || !in.Bytes(header, sizeof header) || raw > kChunk ||
            stored > ZSTD_compressBound(kChunk))
        {
            detail = "cut short in chunk " + std::to_string(c);
            return false;
        }
        payload.resize(stored);
        if (!in.Bytes(payload.data(), stored))
        {
            detail = "cut short in chunk " + std::to_string(c);
            return false;
        }
        if (header[0] == kStored && stored == raw)
            std::memcpy(chunk.data(), payload.data(), raw);
        else if (header[0] != kZstd || ZSTD_decompress(chunk.data(), raw, payload.data(), stored) != raw)
        {
            detail = "chunk " + std::to_string(c) + " does not decompress";
            return false;
        }
        rawBytes = raw;
        return true;
    };

    uint64_t rawTotal = 0;
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (uint32_t c = 0; c < chunkCount; c++)
    {
        size_t raw = 0;
        if (!readChunk(c, raw))
            return DeltaLoad::Damaged;
        hash = Fnv(hash, chunk.data(), raw);
        rawTotal += raw;
    }
    char end[8];
    uint64_t stored = 0;
    if (rawTotal != changed * kSector || !in.Bytes(end, sizeof end) || std::memcmp(end, kEnd, sizeof end) != 0 ||
        !in.Int(stored) || !in.AtEnd())
    {
        detail = "cut short or with trailing bytes";
        return DeltaLoad::Damaged;
    }
    if (hash != stored)
    {
        detail = "the sector data does not match its checksum";
        return DeltaLoad::Damaged;
    }

    map.Discard();
    in.Seek(chunksAt);
    size_t run = 0;
    uint32_t inRun = 0;
    for (uint32_t c = 0; c < chunkCount; c++)
    {
        size_t raw = 0;
        if (!readChunk(c, raw))
        {
            map.Discard();
            detail = "changed while it was read";
            return DeltaLoad::Damaged;
        }
        for (size_t at = 0; at + kSector <= raw; at += kSector)
        {
            map.WriteSector(runs[run].first + inRun, chunk.data() + at);
            if (++inRun == runs[run].second)
            {
                run++;
                inRun = 0;
            }
        }
    }
    detail = std::to_string(changed) + " sector(s)";
    return DeltaLoad::Restored;
}
