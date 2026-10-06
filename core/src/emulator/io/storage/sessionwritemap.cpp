#include "stdafx.h"

#include "sessionwritemap.h"

#include "common/filehelper.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <system_error>

/// The spill tier's file: 64 KiB slots written and read at offsets. Removed when closed; on POSIX unlinked as soon
/// as it is open, so a crash leaves nothing behind
class SessionSpillFile
{
public:
    static std::unique_ptr<SessionSpillFile> Create(const std::string& folder)
    {
        std::error_code ec;
        std::filesystem::path dir = folder.empty() ? std::filesystem::temp_directory_path(ec) : FileHelper::ToFsPath(folder);
        if (ec || dir.empty())
            return nullptr;
        std::filesystem::create_directories(dir, ec);
        RemoveLeftovers(dir);

        static std::atomic<uint64_t> counter{0};
        static const uint64_t process = std::random_device{}() * 0x9E3779B97F4A7C15ULL;
        char name[64];
        std::snprintf(name, sizeof name, "unreal-ng-session-%016llx-%llu.spill", static_cast<unsigned long long>(process),
                      static_cast<unsigned long long>(counter.fetch_add(1)));
        std::unique_ptr<SessionSpillFile> file(new SessionSpillFile());
        file->_path = dir / name;
        file->_stream.open(file->_path, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
        if (!file->_stream)
            return nullptr;
#ifndef _WIN32
        // The open stream keeps the data; the name goes now
        file->_unlinked = std::filesystem::remove(file->_path, ec);
#endif
        return file;
    }

    ~SessionSpillFile()
    {
        _stream.close();
        if (!_unlinked)
        {
            std::error_code ec;
            std::filesystem::remove(_path, ec);
        }
    }

    bool Write(uint64_t offset, const uint8_t* src, size_t length)
    {
        _stream.clear();
        _stream.seekp(static_cast<std::streamoff>(offset));
        _stream.write(reinterpret_cast<const char*>(src), static_cast<std::streamsize>(length));
        _stream.flush();
        return static_cast<bool>(_stream);
    }

    bool Read(uint64_t offset, uint8_t* dst, size_t length) const
    {
        _stream.clear();
        _stream.seekg(static_cast<std::streamoff>(offset));
        _stream.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(length));
        const bool ok = static_cast<size_t>(_stream.gcount()) == length;
        _stream.clear();
        return ok;
    }

    std::string Path() const { return _unlinked ? "(deleted) " + FileHelper::FromFsPath(_path) : FileHelper::FromFsPath(_path); }

private:
    SessionSpillFile() = default;

    /// Spill files of earlier runs that ended without removing theirs (Windows: a file still open by a running
    /// process cannot be removed, so only the stale ones go)
    static void RemoveLeftovers(const std::filesystem::path& dir)
    {
        static std::once_flag once;
        std::call_once(once, [&dir] {
            std::error_code ec;
            for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            {
                const std::string name = it->path().filename().string();
                if (name.rfind("unreal-ng-session-", 0) == 0 && it->path().extension() == ".spill")
                {
                    std::error_code ignored;
                    std::filesystem::remove(it->path(), ignored);
                }
            }
        });
    }

    std::filesystem::path _path;
    mutable std::fstream _stream;
    bool _unlinked = false;
};

namespace
{
    constexpr uint64_t kSlotBytes = SessionWriteMap::kChunkSectors * IBlockDevice::kSectorSize;
    constexpr uint64_t kRetryAfterFailure = 4096;  ///< writes between spill attempts once one failed

    std::atomic<uint64_t> g_defaultLimit{128ull * 1024 * 1024};
    std::atomic<uint64_t> g_spilledChunks{0};
    std::mutex g_folderLock;
    std::string g_spillFolder;

    uint64_t Mix(uint64_t x)
    {
        x ^= x >> 30;
        x *= 0xBF58476D1CE4E5B9ULL;
        x ^= x >> 27;
        x *= 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
    }

    /// One changed sector's share of the content hash
    uint64_t SectorHash(uint64_t lba, const uint8_t* data)
    {
        uint64_t h = 0xcbf29ce484222325ULL ^ Mix(lba + 1);
        for (size_t i = 0; i < IBlockDevice::kSectorSize; i++)
        {
            h ^= data[i];
            h *= 0x100000001b3ULL;
        }
        return Mix(h);
    }
}  // namespace

void SessionWriteMap::SetDefaultMemoryLimit(uint64_t bytes)
{
    g_defaultLimit = bytes;
}

uint64_t SessionWriteMap::DefaultMemoryLimit()
{
    return g_defaultLimit;
}

void SessionWriteMap::SetSpillFolder(const std::string& folder)
{
    std::lock_guard<std::mutex> lock(g_folderLock);
    g_spillFolder = folder;
}

std::string SessionWriteMap::SpillFolder()
{
    std::lock_guard<std::mutex> lock(g_folderLock);
    return g_spillFolder;
}

uint64_t SessionWriteMap::TotalSpilledChunks()
{
    return g_spilledChunks;
}

uint64_t SessionWriteMap::IndexBytes() const
{
    // A map node: the value and about 32 bytes of links and colour; a hash node: the pair and about 24 bytes
    constexpr uint64_t kMapNode = 32;
    constexpr uint64_t kHashNode = 24;
    return _spilled.size() * (sizeof(uint64_t) + sizeof(Spilled) + kMapNode) +
           _lastWrite.size() * (2 * sizeof(uint64_t) + kHashNode) + _lastWrite.bucket_count() * sizeof(void*) +
           _order.size() * sizeof(std::pair<uint64_t, uint64_t>) + _freeSlots.capacity() * sizeof(uint64_t);
}

SessionWriteMap::SessionWriteMap(std::unique_ptr<IBlockDevice> base) : _base(std::move(base)), _limit(g_defaultLimit) {}

SessionWriteMap::~SessionWriteMap() = default;

bool SessionWriteMap::ReadSector(uint64_t lba, uint8_t* dst)
{
    if (lba >= SectorCount())
        return false;

    const auto it = _hot.find(lba);
    if (it != _hot.end())
    {
        std::memcpy(dst, it->second.data(), kSectorSize);
        return true;
    }
    if (_spilledSectors)
    {
        const auto chunk = _spilled.find(lba / kChunkSectors);
        if (chunk != _spilled.end() && chunk->second.Has(lba % kChunkSectors))
            return _file->Read(chunk->second.slot * kSlotBytes + (lba % kChunkSectors) * kSectorSize, dst, kSectorSize);
    }
    return _base->ReadSector(lba, dst);
}

bool SessionWriteMap::WriteSector(uint64_t lba, const uint8_t* src)
{
    if (lba >= SectorCount())
        return false;

    // Writing the medium's own contents back frees the entry
    uint8_t original[kSectorSize];
    const bool same = _base->ReadSector(lba, original) && std::memcmp(original, src, kSectorSize) == 0;
    const uint64_t chunk = lba / kChunkSectors;

    if (auto it = _hot.find(lba); it != _hot.end())
    {
        if (std::memcmp(it->second.data(), src, kSectorSize) == 0)
            return true;
        ToggleHash(lba, it->second.data());
        _generation++;
        if (same)
        {
            _hot.erase(it);
            return true;
        }
        std::memcpy(it->second.data(), src, kSectorSize);
        ToggleHash(lba, src);
        Touch(chunk);
        return true;
    }

    if (_spilledSectors)
    {
        const auto spilled = _spilled.find(chunk);
        if (spilled != _spilled.end() && spilled->second.Has(lba % kChunkSectors))
        {
            uint8_t old[kSectorSize];
            if (_file->Read(spilled->second.slot * kSlotBytes + (lba % kChunkSectors) * kSectorSize, old, kSectorSize))
            {
                if (std::memcmp(old, src, kSectorSize) == 0)
                    return true;
                ToggleHash(lba, old);
            }
            Unspill(lba);
            _generation++;
            if (same)
                return true;
            std::memcpy(_hot[lba].data(), src, kSectorSize);
            ToggleHash(lba, src);
            Touch(chunk);
            if (_limit && HotBytes() > _limit)
                Spill();
            return true;
        }
    }

    if (same)
        return true;
    std::memcpy(_hot[lba].data(), src, kSectorSize);
    ToggleHash(lba, src);
    _generation++;
    Touch(chunk);
    if (_limit && HotBytes() > _limit)
        Spill();
    return true;
}

uint64_t SessionWriteMap::ZeroRun(uint64_t lba)
{
    const std::optional<uint64_t> next = NextChanged(lba);
    if (next && *next == lba)
        return 0;
    const uint64_t run = _base->ZeroRun(lba);
    return next ? std::min<uint64_t>(run, *next - lba) : run;
}

std::optional<uint64_t> SessionWriteMap::NextChanged(uint64_t lba) const
{
    std::optional<uint64_t> best;
    if (const auto hot = _hot.lower_bound(lba); hot != _hot.end())
        best = hot->first;
    for (auto it = _spilled.lower_bound(lba / kChunkSectors); it != _spilled.end(); ++it)
    {
        const uint64_t first = it->first * kChunkSectors;
        if (best && first >= *best)
            break;
        for (uint64_t i = lba > first ? lba - first : 0; i < kChunkSectors; i++)
        {
            if (it->second.Has(i))
            {
                if (!best || first + i < *best)
                    best = first + i;
                return best;
            }
        }
    }
    return best;
}

bool SessionWriteMap::ReadChanged(uint64_t lba, uint8_t* dst) const
{
    if (const auto it = _hot.find(lba); it != _hot.end())
    {
        std::memcpy(dst, it->second.data(), kSectorSize);
        return true;
    }
    const auto chunk = _spilled.find(lba / kChunkSectors);
    if (chunk == _spilled.end() || !chunk->second.Has(lba % kChunkSectors))
        return false;
    return _file->Read(chunk->second.slot * kSlotBytes + (lba % kChunkSectors) * kSectorSize, dst, kSectorSize);
}

void SessionWriteMap::Discard()
{
    if (ChangedSectors())
        _generation++;
    _hot.clear();
    _spilled.clear();
    _spilledSectors = 0;
    _freeSlots.clear();
    _nextSlot = 0;
    _file.reset();
    _spillFailed = false;
    _order.clear();
    _lastWrite.clear();
    _contentHash = 0;
}

void SessionWriteMap::SetMemoryLimit(uint64_t bytes)
{
    if (bytes && !_limit)
    {
        // No order was kept without a limit: the chunks in memory, in sector order
        _order.clear();
        _lastWrite.clear();
        for (auto it = _hot.begin(); it != _hot.end(); it = _hot.lower_bound((it->first / kChunkSectors + 1) * kChunkSectors))
        {
            _lastWrite[it->first / kChunkSectors] = ++_stamp;
            _order.emplace_back(it->first / kChunkSectors, _stamp);
        }
    }
    _limit = bytes;
    if (_limit && HotBytes() > _limit)
        Spill();
}

std::string SessionWriteMap::SpillPath() const
{
    return _file ? _file->Path() : std::string();
}

uint64_t SessionWriteMap::ContentId() const
{
    // The medium's id, moved by the changes: the same changes give the same id, whichever tier holds them
    const uint64_t base = _base->ContentId();
    return ChangedSectors() ? Mix(base ^ Mix(_contentHash)) : base;
}

void SessionWriteMap::ToggleHash(uint64_t lba, const uint8_t* data)
{
    _contentHash ^= SectorHash(lba, data);
}

void SessionWriteMap::Touch(uint64_t chunk)
{
    if (!_limit)
        return;  // nothing will spill: no write order to keep (SetMemoryLimit rebuilds it)
    _lastWrite[chunk] = ++_stamp;
    if (!_order.empty() && _order.back().first == chunk)
        _order.back().second = _stamp;
    else
        _order.emplace_back(chunk, _stamp);

    // Rewrites of a few chunks in turn leave stale entries: rebuild from the live stamps now and then
    if (_order.size() > 2 * _lastWrite.size() + 1024)
    {
        std::vector<std::pair<uint64_t, uint64_t>> live;  // (stamp, chunk)
        live.reserve(_lastWrite.size());
        for (const auto& [c, stamp] : _lastWrite)
            live.emplace_back(stamp, c);
        std::sort(live.begin(), live.end());
        _order.clear();
        for (const auto& [stamp, c] : live)
            _order.emplace_back(c, stamp);
    }
}

void SessionWriteMap::Spill()
{
    if (_spillFailed && _stamp % kRetryAfterFailure != 0)
        return;
    // Down to 7/8 of the limit, so the next spill is many writes away
    const uint64_t target = _limit / 8 * 7;
    while (HotBytes() > target && !_order.empty())
    {
        const auto [chunk, stamp] = _order.front();
        _order.pop_front();
        const auto last = _lastWrite.find(chunk);
        if (last == _lastWrite.end() || last->second != stamp)
            continue;  // written again later: a newer entry stands for it
        _lastWrite.erase(last);
        if (!SpillChunk(chunk))
        {
            _spillFailed = true;
            Touch(chunk);
            return;
        }
    }
    _spillFailed = false;
}

bool SessionWriteMap::SpillChunk(uint64_t chunk)
{
    const uint64_t first = chunk * kChunkSectors;
    auto begin = _hot.lower_bound(first);
    auto end = _hot.lower_bound(first + kChunkSectors);
    if (begin == end)
        return true;
    if (!_file)
    {
        _file = SessionSpillFile::Create(SpillFolder());
        if (!_file)
            return false;
    }

    const bool fresh = _spilled.find(chunk) == _spilled.end();
    Spilled& spilled = _spilled[chunk];
    if (fresh)
    {
        if (!_freeSlots.empty())
        {
            spilled.slot = _freeSlots.back();
            _freeSlots.pop_back();
        }
        else
        {
            spilled.slot = _nextSlot++;
        }
    }

    // Contiguous sectors in one write
    std::vector<uint8_t> run;
    uint64_t runFirst = 0;
    auto flush = [&]() {
        if (run.empty())
            return true;
        const bool ok = _file->Write(spilled.slot * kSlotBytes + (runFirst - first) * kSectorSize, run.data(), run.size());
        run.clear();
        return ok;
    };
    bool ok = true;
    for (auto it = begin; it != end && ok; ++it)
    {
        if (!run.empty() && it->first != runFirst + run.size() / kSectorSize)
            ok = flush();
        if (run.empty())
            runFirst = it->first;
        run.insert(run.end(), it->second.begin(), it->second.end());
    }
    if (ok)
        ok = flush();
    if (!ok)
    {
        // The sectors stay in memory; a slot that holds nothing goes back
        if (spilled.Empty())
        {
            _freeSlots.push_back(spilled.slot);
            _spilled.erase(chunk);
        }
        return false;
    }

    for (auto it = begin; it != end; ++it)
    {
        spilled.Set(it->first - first);
        _spilledSectors++;
    }
    _hot.erase(begin, end);
    g_spilledChunks++;
    return true;
}

void SessionWriteMap::Unspill(uint64_t lba)
{
    const auto it = _spilled.find(lba / kChunkSectors);
    if (it == _spilled.end() || !it->second.Has(lba % kChunkSectors))
        return;
    it->second.Clear(lba % kChunkSectors);
    _spilledSectors--;
    if (it->second.Empty())
    {
        _freeSlots.push_back(it->second.slot);
        _spilled.erase(it);
    }
}

bool SessionWriteMap::ExportTo(const std::string& path, std::string* error)
{
    std::ofstream out(FileHelper::ToFsPath(path), std::ios::binary | std::ios::trunc);
    if (!out)
    {
        if (error)
            *error = "cannot create " + path;
        return false;
    }

    uint8_t sector[kSectorSize];
    for (uint64_t lba = 0; lba < SectorCount(); lba++)
    {
        if (!ReadSector(lba, sector))
        {
            if (error)
                *error = "cannot read sector " + std::to_string(lba);
            return false;
        }
        out.write(reinterpret_cast<const char*>(sector), static_cast<std::streamsize>(kSectorSize));
    }

    out.flush();
    if (!out && error)
        *error = "write error on " + path;
    return static_cast<bool>(out);
}
