#include "stdafx.h"

#include "sessiondelta.h"

#include <zstd.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
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

    class Out
    {
    public:
        void Bytes(const void* data, size_t size)
        {
            const auto* p = static_cast<const uint8_t*>(data);
            buffer.insert(buffer.end(), p, p + size);
        }
        void U8(uint8_t v) { buffer.push_back(v); }
        void U16(uint16_t v)
        {
            for (int i = 0; i < 2; i++)
                buffer.push_back(static_cast<uint8_t>(v >> (8 * i)));
        }
        void U32(uint32_t v)
        {
            for (int i = 0; i < 4; i++)
                buffer.push_back(static_cast<uint8_t>(v >> (8 * i)));
        }
        void U64(uint64_t v)
        {
            for (int i = 0; i < 8; i++)
                buffer.push_back(static_cast<uint8_t>(v >> (8 * i)));
        }
        std::vector<uint8_t> buffer;
    };

    class In
    {
    public:
        explicit In(const std::vector<uint8_t>& data) : _data(data) {}
        bool Bytes(void* dst, size_t size)
        {
            if (size > _data.size() - _at)
                return false;
            std::memcpy(dst, _data.data() + _at, size);
            _at += size;
            return true;
        }
        const uint8_t* Take(size_t size)
        {
            if (size > _data.size() - _at)
                return nullptr;
            const uint8_t* p = _data.data() + _at;
            _at += size;
            return p;
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
        bool AtEnd() const { return _at == _data.size(); }

    private:
        const std::vector<uint8_t>& _data;
        size_t _at = 0;
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
    const auto& changes = map.Changes();
    // Runs of consecutive sectors
    std::vector<std::pair<uint64_t, uint32_t>> runs;
    for (const auto& entry : changes)
    {
        if (!runs.empty() && runs.back().first + runs.back().second == entry.first && runs.back().second < 0xFFFFFFFFu)
            runs.back().second++;
        else
            runs.push_back({entry.first, 1});
    }

    Out out;
    out.Bytes(kMagic, sizeof kMagic);
    out.U32(kVersion);
    out.U32(0);
    out.U64(identity.contentId);
    out.U64(identity.sectorCount);
    out.U64(changes.size());
    out.U32(static_cast<uint32_t>(runs.size()));
    out.U32(static_cast<uint32_t>(identity.layers.size()));
    for (const DeltaIdentity::Layer& layer : identity.layers)
    {
        out.U64(layer.identity);
        const std::string name = layer.name.substr(0, 0xFFFF);
        out.U16(static_cast<uint16_t>(name.size()));
        out.Bytes(name.data(), name.size());
    }
    for (const auto& [lba, count] : runs)
    {
        out.U64(lba);
        out.U32(count);
    }

    // The sectors in LBA order, compressed per 1 MiB
    const size_t raw = changes.size() * kSector;
    out.U32(static_cast<uint32_t>((raw + kChunk - 1) / kChunk));
    std::vector<uint8_t> chunk;
    chunk.reserve(std::min(raw, kChunk));
    uint64_t hash = 0xcbf29ce484222325ULL;
    auto flush = [&]() {
        hash = Fnv(hash, chunk.data(), chunk.size());
        std::vector<uint8_t> packed(ZSTD_compressBound(chunk.size()));
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
    for (const auto& entry : changes)
    {
        chunk.insert(chunk.end(), entry.second.begin(), entry.second.end());
        if (chunk.size() == kChunk)
            flush();
    }
    if (!chunk.empty())
        flush();
    out.Bytes(kEnd, sizeof kEnd);
    out.U64(hash);

    std::filesystem::path temp = path;
    temp += ".writing";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(out.buffer.data()), static_cast<std::streamsize>(out.buffer.size()));
        file.flush();
        if (!file)
        {
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            return MediaResult::Fail(MediaError::IoError, "cannot write " + FileHelper::FromFsPath(temp));
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec)
    {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        return MediaResult::Fail(MediaError::IoError, "cannot replace " + FileHelper::FromFsPath(path) + ": " + ec.message());
    }
    return MediaResult::Success();
}

DeltaLoad SessionDelta::Load(const std::filesystem::path& path, SessionWriteMap& map, const DeltaIdentity& identity, std::string& detail)
{
    detail.clear();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        return DeltaLoad::Missing;
    std::vector<uint8_t> data;
    {
        std::ifstream file(path, std::ios::binary);
        const auto size = std::filesystem::file_size(path, ec);
        if (ec || !file)
        {
            detail = "cannot be read";
            return DeltaLoad::Damaged;
        }
        data.resize(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!file)
        {
            detail = "cannot be read";
            return DeltaLoad::Damaged;
        }
    }

    In in(data);
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

    std::vector<uint8_t> sectors;
    sectors.reserve(static_cast<size_t>(std::min<uint64_t>(changed * kSector, data.size() * 64)));
    for (uint32_t c = 0; c < chunkCount; c++)
    {
        uint32_t rawBytes = 0, storedBytes = 0;
        uint8_t header[4];
        if (!in.Int(rawBytes) || !in.Int(storedBytes) || !in.Bytes(header, sizeof header) || rawBytes > kChunk)
        {
            detail = "cut short in chunk " + std::to_string(c);
            return DeltaLoad::Damaged;
        }
        const uint8_t* payload = in.Take(storedBytes);
        if (!payload)
        {
            detail = "cut short in chunk " + std::to_string(c);
            return DeltaLoad::Damaged;
        }
        const size_t at = sectors.size();
        sectors.resize(at + rawBytes);
        if (header[0] == kStored && storedBytes == rawBytes)
            std::memcpy(sectors.data() + at, payload, rawBytes);
        else if (header[0] != kZstd || ZSTD_decompress(sectors.data() + at, rawBytes, payload, storedBytes) != rawBytes)
        {
            detail = "chunk " + std::to_string(c) + " does not decompress";
            return DeltaLoad::Damaged;
        }
    }
    char end[8];
    uint64_t hash = 0;
    if (sectors.size() != changed * kSector || !in.Bytes(end, sizeof end) || std::memcmp(end, kEnd, sizeof end) != 0 ||
        !in.Int(hash) || !in.AtEnd())
    {
        detail = "cut short or with trailing bytes";
        return DeltaLoad::Damaged;
    }
    if (hash != Fnv(0xcbf29ce484222325ULL, sectors.data(), sectors.size()))
    {
        detail = "the sector data does not match its checksum";
        return DeltaLoad::Damaged;
    }

    map.Discard();
    size_t at = 0;
    for (const auto& [lba, count] : runs)
    {
        for (uint32_t s = 0; s < count; s++, at += kSector)
            map.WriteSector(lba + s, sectors.data() + at);
    }
    detail = std::to_string(changed) + " sector(s)";
    return DeltaLoad::Restored;
}
