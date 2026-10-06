#include "stdafx.h"

#include "sessionwritemap.h"

#include "common/filehelper.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <new>
#include <random>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace
{
    constexpr uint64_t kSector = IBlockDevice::kSectorSize;
    constexpr uint64_t kHeaderBytes = 4096;
    constexpr uint64_t kSlotHeaderBytes = 512;
    constexpr uint64_t kSlotBytes = kSlotHeaderBytes + SessionWriteMap::kChunkSectors * kSector;
    constexpr char kFileMagic[8] = {'U', 'N', 'G', 'S', 'E', 'S', 'S', 'N'};
    constexpr char kSlotMagic[8] = {'U', 'N', 'G', 'S', 'L', 'O', 'T', '!'};
    constexpr uint32_t kVersion = 1;
    constexpr uint64_t kRetryAfterFailure = 4096;  ///< writes between journal attempts once one failed

    std::mutex g_settingsLock;
    SessionSettings g_settings;
    std::optional<bool> g_journalOverride;
    std::mutex g_journalsLock;
    std::vector<std::string> g_openJournals;  ///< named journals of live sessions (one owner each)

    bool ClaimJournal(const std::string& key)
    {
        std::lock_guard<std::mutex> lock(g_journalsLock);
        if (std::find(g_openJournals.begin(), g_openJournals.end(), key) != g_openJournals.end())
            return false;
        g_openJournals.push_back(key);
        return true;
    }

    void ReleaseJournal(const std::string& key)
    {
        std::lock_guard<std::mutex> lock(g_journalsLock);
        g_openJournals.erase(std::remove(g_openJournals.begin(), g_openJournals.end(), key), g_openJournals.end());
    }

    std::string JournalKey(const std::string& path)
    {
        return FileHelper::FromFsPath(std::filesystem::absolute(FileHelper::ToFsPath(path)).lexically_normal());
    }
    std::atomic<uint64_t> g_spilledChunks{0};

    void Put32(uint8_t* p, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            p[i] = static_cast<uint8_t>(v >> (8 * i));
    }
    void Put64(uint8_t* p, uint64_t v)
    {
        for (int i = 0; i < 8; i++)
            p[i] = static_cast<uint8_t>(v >> (8 * i));
    }
    uint32_t Get32(const uint8_t* p)
    {
        uint32_t v = 0;
        for (int i = 0; i < 4; i++)
            v |= static_cast<uint32_t>(p[i]) << (8 * i);
        return v;
    }
    uint64_t Get64(const uint8_t* p)
    {
        uint64_t v = 0;
        for (int i = 0; i < 8; i++)
            v |= static_cast<uint64_t>(p[i]) << (8 * i);
        return v;
    }
    uint32_t Fnv32(const uint8_t* data, size_t size)
    {
        uint32_t h = 0x811C9DC5u;
        for (size_t i = 0; i < size; i++)
        {
            h ^= data[i];
            h *= 0x01000193u;
        }
        return h;
    }

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
        for (size_t i = 0; i < kSector; i++)
        {
            h ^= data[i];
            h *= 0x100000001b3ULL;
        }
        return Mix(h);
    }

    uint64_t SlotOffset(uint64_t slot) { return kHeaderBytes + slot * kSlotBytes; }
}  // namespace

/// The journal file: a 4 KiB header naming the base, then 64.5 KiB slots (a 512-byte header naming a group and the
/// sectors it holds, then the group's 128 sectors). A temp journal is unlinked as soon as it is open on POSIX
class SessionJournalFile
{
public:
    /// A new journal at `path` (replacing what is there); `temp`: in a folder, unlinked at once where the OS allows
    static std::unique_ptr<SessionJournalFile> Create(const std::filesystem::path& path, bool temp, uint64_t contentId,
                                                      uint64_t sectors)
    {
        std::unique_ptr<SessionJournalFile> file(new SessionJournalFile());
        file->_path = path;
        file->_temp = temp;
        file->_file = Open(path, "w+b");
        if (!file->_file)
            return nullptr;
#ifndef _WIN32
        if (temp)
        {
            std::error_code ec;
            file->_unlinked = std::filesystem::remove(path, ec);
        }
#endif
        uint8_t header[kHeaderBytes] = {};
        std::memcpy(header, kFileMagic, 8);
        Put32(header + 8, kVersion);
        Put32(header + 12, 0);
        Put64(header + 16, contentId);
        Put64(header + 24, sectors);
        Put32(header + 32, static_cast<uint32_t>(kSlotBytes));
        Put32(header + 36, Fnv32(header, 36));
        if (!file->Write(0, header, sizeof header) || std::fflush(file->_file) != 0)
            return nullptr;
        return file;
    }

    /// An existing journal, to replay
    static std::unique_ptr<SessionJournalFile> OpenExisting(const std::filesystem::path& path)
    {
        std::unique_ptr<SessionJournalFile> file(new SessionJournalFile());
        file->_path = path;
        file->_file = Open(path, "r+b");
        if (!file->_file)
            return nullptr;
        return file;
    }

    ~SessionJournalFile() { Close(); }

    bool Write(uint64_t offset, const void* data, size_t size)
    {
        return Seek(offset) && std::fwrite(data, 1, size, _file) == size;
    }

    bool Read(uint64_t offset, void* data, size_t size) const
    {
        return Seek(offset) && std::fread(data, 1, size, _file) == size;
    }

    bool Flush() { return std::fflush(_file) == 0; }

    /// To the disk, past the OS's cache
    bool Sync()
    {
        if (!Flush())
            return false;
#if defined(_WIN32)
        return _commit(_fileno(_file)) == 0;
#else
        return fsync(fileno(_file)) == 0;
#endif
    }

    uint64_t Size() const
    {
        std::error_code ec;
        if (!_unlinked)
        {
            const uint64_t size = std::filesystem::file_size(_path, ec);
            if (!ec)
                return size;
        }
#if defined(_WIN32)
        _fseeki64(_file, 0, SEEK_END);
        return static_cast<uint64_t>(_ftelli64(_file));
#else
        fseeko(_file, 0, SEEK_END);
        return static_cast<uint64_t>(ftello(_file));
#endif
    }

    /// Closed and deleted
    void Remove()
    {
        Close();
        if (!_unlinked)
        {
            std::error_code ec;
            std::filesystem::remove(_path, ec);
        }
        _unlinked = true;
    }

    void Close()
    {
        if (_file)
        {
            std::fclose(_file);
            _file = nullptr;
            if (_temp && !_unlinked)
            {
                std::error_code ec;
                std::filesystem::remove(_path, ec);
                _unlinked = true;
            }
        }
    }

    std::string Path() const { return _unlinked ? "(deleted) " + FileHelper::FromFsPath(_path) : FileHelper::FromFsPath(_path); }

    /// A temp journal of a new session: in `folder` (empty: the system temp folder)
    static std::filesystem::path TempPath(const std::string& folder)
    {
        std::error_code ec;
        std::filesystem::path dir = folder.empty() ? std::filesystem::temp_directory_path(ec) : FileHelper::ToFsPath(folder);
        if (ec || dir.empty())
            return {};
        std::filesystem::create_directories(dir, ec);
        RemoveLeftovers(dir);
        static std::atomic<uint64_t> counter{0};
        static const uint64_t process = std::random_device{}() * 0x9E3779B97F4A7C15ULL;
        char name[80];
        std::snprintf(name, sizeof name, "unreal-ng-session-%016llx-%llu.spill", static_cast<unsigned long long>(process),
                      static_cast<unsigned long long>(counter.fetch_add(1)));
        return dir / name;
    }

private:
    SessionJournalFile() = default;

    static FILE* Open(const std::filesystem::path& path, const char* mode)
    {
#if defined(_WIN32)
        const std::wstring wmode(mode, mode + std::strlen(mode));
        return _wfopen(path.c_str(), wmode.c_str());
#else
        return std::fopen(path.c_str(), mode);
#endif
    }

    bool Seek(uint64_t offset) const
    {
#if defined(_WIN32)
        return _fseeki64(_file, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
        return fseeko(_file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
    }

    /// Temp journals of earlier runs that ended without removing theirs (Windows: a file still open by a running
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
    FILE* _file = nullptr;
    bool _temp = false;
    bool _unlinked = false;
};

/// region <Settings>

void SessionWriteMap::SetDefaults(const SessionSettings& settings)
{
    std::lock_guard<std::mutex> lock(g_settingsLock);
    g_settings = settings;
}

SessionSettings SessionWriteMap::Defaults()
{
    std::lock_guard<std::mutex> lock(g_settingsLock);
    SessionSettings settings = g_settings;
    if (g_journalOverride)
        settings.journal = *g_journalOverride;
    return settings;
}

void SessionWriteMap::OverrideJournalDefault(std::optional<bool> on)
{
    std::lock_guard<std::mutex> lock(g_settingsLock);
    g_journalOverride = on;
}

void SessionWriteMap::SetDefaultMemoryLimit(uint64_t bytes)
{
    std::lock_guard<std::mutex> lock(g_settingsLock);
    g_settings.memoryLimit = bytes;
}

uint64_t SessionWriteMap::DefaultMemoryLimit()
{
    std::lock_guard<std::mutex> lock(g_settingsLock);
    return g_settings.memoryLimit;
}

void SessionWriteMap::SetSpillFolder(const std::string& folder)
{
    std::lock_guard<std::mutex> lock(g_settingsLock);
    g_settings.spillFolder = folder;
}

std::string SessionWriteMap::SpillFolder()
{
    std::lock_guard<std::mutex> lock(g_settingsLock);
    return g_settings.spillFolder;
}

uint64_t SessionWriteMap::TotalSpilledChunks()
{
    return g_spilledChunks;
}

/// endregion </Settings>

SessionWriteMap::SessionWriteMap(std::unique_ptr<IBlockDevice> base) : _base(std::move(base)), _settings(Defaults())
{
    _settings.arenaBytes = std::max<uint32_t>(static_cast<uint32_t>(kSector), _settings.arenaBytes - _settings.arenaBytes % kSector);
}

SessionWriteMap::~SessionWriteMap()
{
    // Without a word from the owner (a crash leaves no word at all): unsaved writes stay in a named journal
    CloseJournal(_changed > 0);
}

uint64_t SessionWriteMap::Now() const
{
    if (_now)
        return _now();
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

/// region <Index>

size_t SessionWriteMap::Rank(const uint64_t* mask, uint64_t i)
{
    if (i < 64)
        return static_cast<size_t>(std::popcount(mask[0] & ((1ull << i) - 1)));
    return static_cast<size_t>(std::popcount(mask[0]) + std::popcount(mask[1] & ((1ull << (i - 64)) - 1)));
}

SessionWriteMap::Group* SessionWriteMap::FindGroup(uint64_t group) const
{
    const uint64_t leaf = group / kGroupsPerLeaf;
    if (leaf >= _leaves.size() || !_leaves[leaf])
        return nullptr;
    return _leaves[leaf]->groups[group % kGroupsPerLeaf].get();
}

SessionWriteMap::Group& SessionWriteMap::MakeGroup(uint64_t group)
{
    const uint64_t leaf = group / kGroupsPerLeaf;
    if (leaf >= _leaves.size())
        _leaves.resize(leaf + 1);
    if (!_leaves[leaf])
        _leaves[leaf] = std::make_unique<Leaf>();
    std::unique_ptr<Group>& g = _leaves[leaf]->groups[group % kGroupsPerLeaf];
    if (!g)
    {
        g = std::make_unique<Group>();
        _groupCount++;
    }
    return *g;
}

void SessionWriteMap::DropGroupIfEmpty(uint64_t group)
{
    const uint64_t leaf = group / kGroupsPerLeaf;
    if (leaf >= _leaves.size() || !_leaves[leaf])
        return;
    std::unique_ptr<Group>& g = _leaves[leaf]->groups[group % kGroupsPerLeaf];
    if (g && !g->hot[0] && !g->hot[1] && !g->journal[0] && !g->journal[1] && g->slot == kNone && !g->queued)
    {
        g.reset();
        _groupCount--;
    }
}

uint8_t* SessionWriteMap::SlotData(uint64_t ref) const
{
    const Arena& a = *_arenas.at(static_cast<uint32_t>(ref >> 32));
    return a.data + static_cast<uint64_t>(static_cast<uint32_t>(ref)) * kSector;
}

void SessionWriteMap::AddHot(Group& g, uint64_t index, uint64_t ref)
{
    g.refs.insert(g.refs.begin() + static_cast<std::ptrdiff_t>(Rank(g.hot, index)), ref);
    Set(g.hot, index);
}

void SessionWriteMap::RemoveHot(Group& g, uint64_t index)
{
    const size_t rank = Rank(g.hot, index);
    const uint64_t ref = g.refs[rank];
    g.refs.erase(g.refs.begin() + static_cast<std::ptrdiff_t>(rank));
    Clear(g.hot, index);
    FreeSlot(ref);
}

/// endregion </Index>

/// region <Arenas>

SessionWriteMap::Arena::Arena(uint32_t size) : bytes(size)
{
#if defined(_WIN32)
    data = static_cast<uint8_t*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
    void* pages = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    data = pages == MAP_FAILED ? nullptr : static_cast<uint8_t*>(pages);
#endif
    if (!data)
        throw std::bad_alloc();
    const uint32_t slots = size / static_cast<uint32_t>(kSector);
    lba.assign(slots, kNone);
    dirty.assign(slots, 0);
}

SessionWriteMap::Arena::~Arena()
{
#if defined(_WIN32)
    VirtualFree(data, 0, MEM_RELEASE);
#else
    munmap(data, bytes);
#endif
}

uint64_t SessionWriteMap::NewSlot(uint64_t lba)
{
    const uint32_t slots = _settings.arenaBytes / static_cast<uint32_t>(kSector);
    Arena* newest = _arenaOrder.empty() ? nullptr : _arenas.at(_arenaOrder.back()).get();
    if (!newest || (newest->next == slots && newest->free.empty()))
    {
        // A new arena; past the limit the oldest moves to the journal first
        const uint64_t limit = _settings.memoryLimit;
        const size_t maxArenas = limit ? static_cast<size_t>(std::max<uint64_t>(1, limit / _settings.arenaBytes)) : SIZE_MAX;
        if (_arenaOrder.size() >= maxArenas && (!_spillFailed || _writesSinceFailure >= kRetryAfterFailure))
        {
            _writesSinceFailure = 0;
            EvictOldest();
        }
        auto arena = std::make_unique<Arena>(_settings.arenaBytes);
        const uint32_t id = _nextArenaId++;
        newest = arena.get();
        _arenas.emplace(id, std::move(arena));
        _arenaOrder.push_back(id);
    }
    uint32_t slot = 0;
    if (!newest->free.empty())
    {
        slot = newest->free.back();
        newest->free.pop_back();
    }
    else
    {
        slot = newest->next++;
    }
    newest->lba[slot] = lba;
    newest->live++;
    return (static_cast<uint64_t>(_arenaOrder.back()) << 32) | slot;
}

void SessionWriteMap::FreeSlot(uint64_t ref)
{
    const uint32_t id = static_cast<uint32_t>(ref >> 32);
    const uint32_t slot = static_cast<uint32_t>(ref);
    Arena& a = *_arenas.at(id);
    a.lba[slot] = kNone;
    if (a.dirty[slot])
    {
        a.dirty[slot] = 0;
        a.dirtyCount--;
        _dirtyTotal--;
    }
    a.live--;
    a.free.push_back(slot);
    if (a.live == 0 && id != _arenaOrder.back())
    {
        _arenaOrder.erase(std::find(_arenaOrder.begin(), _arenaOrder.end(), id));
        _arenas.erase(id);
    }
}

void SessionWriteMap::MarkDirty(uint64_t ref)
{
    Arena& a = *_arenas.at(static_cast<uint32_t>(ref >> 32));
    const uint32_t slot = static_cast<uint32_t>(ref);
    if (a.dirty[slot])
        return;
    if (_dirtyTotal == 0 && _headerQueue.empty())
        _oldestDirtyMs = Now();
    a.dirty[slot] = 1;
    a.dirtyCount++;
    _dirtyTotal++;
}

/// endregion </Arenas>

/// region <Journal>

bool SessionWriteMap::EnsureJournal()
{
    if (_journal)
        return true;
    if (!_journalPath.empty())
    {
        _journal = SessionJournalFile::Create(FileHelper::ToFsPath(_journalPath), /*temp*/ false, _base->ContentId(), SectorCount());
        if (_journal)
            return true;
        ReleaseJournal(JournalKey(_journalPath));
        _journalPath.clear();  // no place next to the medium: a temp journal, not recoverable
    }
    const std::filesystem::path temp = SessionJournalFile::TempPath(_settings.spillFolder);
    if (!temp.empty())
        _journal = SessionJournalFile::Create(temp, /*temp*/ true, _base->ContentId(), SectorCount());
    return _journal != nullptr;
}

void SessionWriteMap::QueueHeader(uint64_t group, Group& g)
{
    g.headerDirty = true;
    if (!g.queued)
    {
        if (_dirtyTotal == 0 && _headerQueue.empty())
            _oldestDirtyMs = Now();
        g.queued = true;
        _headerQueue.push_back(group);
    }
}

bool SessionWriteMap::WriteDirty(Arena& arena)
{
    if (!arena.dirtyCount)
        return true;
    if (!EnsureJournal())
        return false;
    std::vector<std::pair<uint64_t, uint32_t>> dirty;  // (lba, slot)
    dirty.reserve(arena.dirtyCount);
    for (uint32_t s = 0; s < arena.lba.size(); s++)
        if (arena.dirty[s])
            dirty.emplace_back(arena.lba[s], s);
    std::sort(dirty.begin(), dirty.end());
    uint64_t lastGroup = kNone;
    for (const auto& [lba, s] : dirty)
    {
        const uint64_t group = lba / kChunkSectors;
        const uint64_t index = lba % kChunkSectors;
        Group& g = MakeGroup(group);
        if (g.slot == kNone)
        {
            if (!_freeJournalSlots.empty())
            {
                g.slot = _freeJournalSlots.back();
                _freeJournalSlots.pop_back();
            }
            else
            {
                g.slot = _nextJournalSlot++;
            }
        }
        if (!_journal->Write(SlotOffset(g.slot) + kSlotHeaderBytes + index * kSector, arena.data + s * kSector, kSector))
            return false;
        if (!Has(g.journal, index))
        {
            Set(g.journal, index);
            _journalSectors++;
        }
        arena.dirty[s] = 0;
        arena.dirtyCount--;
        _dirtyTotal--;
        QueueHeader(group, g);
        if (group != lastGroup)
        {
            g_spilledChunks++;
            lastGroup = group;
        }
    }
    return true;
}

bool SessionWriteMap::WriteHeaders()
{
    if (_headerQueue.empty())
        return true;
    if (!EnsureJournal())
        return false;
    // Sector data first (written by WriteDirty), then the headers that name it
    if (!_journal->Flush())
        return false;
    std::vector<uint64_t> queue;
    queue.swap(_headerQueue);
    bool ok = true;
    for (size_t i = 0; i < queue.size(); i++)
    {
        const uint64_t group = queue[i];
        Group* g = FindGroup(group);
        if (!g)
            continue;
        if (!ok)
        {
            _headerQueue.push_back(group);  // stays queued for the next attempt
            continue;
        }
        if (g->slot != kNone)
        {
            uint8_t header[kSlotHeaderBytes] = {};
            const bool empty = !g->journal[0] && !g->journal[1];
            if (!empty)
            {
                std::memcpy(header, kSlotMagic, 8);
                Put64(header + 8, group);
                Put64(header + 16, ++g->sequence);
                Put64(header + 24, g->journal[0]);
                Put64(header + 32, g->journal[1]);
                Put32(header + 40, Fnv32(header, 40));
            }
            ok = _journal->Write(SlotOffset(g->slot), header, sizeof header);
            if (!ok)
            {
                _headerQueue.push_back(group);
                continue;
            }
            if (empty)
            {
                _freeJournalSlots.push_back(g->slot);
                g->slot = kNone;
            }
        }
        g->headerDirty = false;
        g->queued = false;
        DropGroupIfEmpty(group);
    }
    if (ok)
        ok = _journal->Flush();
    _unsynced = _unsynced || ok;
    return ok;
}

bool SessionWriteMap::EvictOldest()
{
    if (_arenaOrder.empty())
        return true;
    const uint32_t id = _arenaOrder.front();
    Arena& a = *_arenas.at(id);
    if (!WriteDirty(a) || !WriteHeaders())
    {
        _spillFailed = true;
        return false;
    }
    _spillFailed = false;
    // Every live sector of the arena is in the journal now: it leaves memory
    for (uint32_t s = 0; s < a.lba.size(); s++)
    {
        if (a.lba[s] == kNone)
            continue;
        Group& g = MakeGroup(a.lba[s] / kChunkSectors);
        const uint64_t index = a.lba[s] % kChunkSectors;
        const size_t rank = Rank(g.hot, index);
        g.refs.erase(g.refs.begin() + static_cast<std::ptrdiff_t>(rank));
        Clear(g.hot, index);
    }
    _arenaOrder.pop_front();
    _arenas.erase(id);
    return true;
}

void SessionWriteMap::Rebalance()
{
    const uint64_t limit = _settings.memoryLimit;
    if (!limit)
        return;
    const size_t maxArenas = static_cast<size_t>(std::max<uint64_t>(1, limit / _settings.arenaBytes));
    while (_arenaOrder.size() > maxArenas)
        if (!EvictOldest())
            break;
}

bool SessionWriteMap::FlushJournal()
{
    if (!_dirtyTotal && _headerQueue.empty() && !_unsynced)
        return true;
    bool ok = true;
    for (uint32_t id : _arenaOrder)
        ok = ok && WriteDirty(*_arenas.at(id));
    ok = ok && WriteHeaders();
    _spillFailed = !ok;
    if (!ok || (_journal && !_journal->Sync()))
        return false;
    _unsynced = false;
    _lastSyncMs = Now();
    return true;
}

void SessionWriteMap::Tick()
{
    // A temp journal holds nothing worth a timeout: it dies with the session anyway
    if (_journalPath.empty())
        return;
    if ((!_dirtyTotal && _headerQueue.empty() && !_unsynced))
        return;
    const uint64_t now = Now();
    if (_settings.flushSeconds && (_dirtyTotal || !_headerQueue.empty()) && now - _oldestDirtyMs >= _settings.flushSeconds * 1000ull)
    {
        bool ok = true;
        for (uint32_t id : _arenaOrder)
            ok = ok && WriteDirty(*_arenas.at(id));
        ok = ok && WriteHeaders();
        _spillFailed = !ok;
    }
    if (_settings.syncSeconds && _unsynced && _journal && now - _lastSyncMs >= _settings.syncSeconds * 1000ull)
    {
        if (_journal->Sync())
            _unsynced = false;
        _lastSyncMs = now;
    }
}

SessionWriteMap::JournalOpen SessionWriteMap::OpenJournal(const std::string& path, JournalMode mode)
{
    JournalOpen result;
    const std::filesystem::path fsPath = FileHelper::ToFsPath(path);
    std::error_code ec;
    const bool exists = std::filesystem::is_regular_file(fsPath, ec);
    if (mode == JournalMode::Off)
    {
        result.outcome = JournalOpen::Outcome::Off;
        return result;
    }
    if (!ClaimJournal(JournalKey(path)))
    {
        result.outcome = JournalOpen::Outcome::InUse;
        return result;
    }
    _journalPath = path;
    if (!exists)
        return result;
    if (mode == JournalMode::Discard)
    {
        std::filesystem::remove(fsPath, ec);
        result.outcome = JournalOpen::Outcome::Discarded;
        return result;
    }

    auto stale = [&](const std::string& why) {
        std::filesystem::path aside;
        for (int n = 1; n < 1000; n++)
        {
            aside = fsPath;
            aside += "." + std::to_string(n) + ".stale";
            if (!std::filesystem::exists(aside, ec))
                break;
        }
        std::filesystem::rename(fsPath, aside, ec);
        result.outcome = JournalOpen::Outcome::Stale;
        result.kept = FileHelper::FromFsPath(aside.filename());
        result.detail = why;
        return result;
    };

    auto file = SessionJournalFile::OpenExisting(fsPath);
    if (!file)
        return stale("it cannot be opened");
    uint8_t header[kHeaderBytes];
    if (!file->Read(0, header, sizeof header) || std::memcmp(header, kFileMagic, 8) != 0 || Get32(header + 36) != Fnv32(header, 36))
    {
        file.reset();
        return stale("its header is damaged");
    }
    if (Get32(header + 8) != kVersion || Get32(header + 32) != kSlotBytes)
    {
        file.reset();
        return stale("version " + std::to_string(Get32(header + 8)) + " is not known");
    }
    if (Get64(header + 16) != _base->ContentId() || Get64(header + 24) != SectorCount())
    {
        file.reset();
        return stale("it was written over another disk");
    }

    // A replay replaces what the session holds (a composite's delta: the journal has it all)
    if (_changed)
        Discard();
    // The last slot may end early: only the sectors written into it are in the file
    const uint64_t slots = (file->Size() - kHeaderBytes + kSlotBytes - 1) / kSlotBytes;
    const uint64_t groups = (SectorCount() + kChunkSectors - 1) / kChunkSectors;
    std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> best;  // group -> (sequence, slot)
    std::vector<uint64_t> losers;
    for (uint64_t s = 0; s < slots; s++)
    {
        uint8_t slotHeader[kSlotHeaderBytes];
        if (!file->Read(SlotOffset(s), slotHeader, sizeof slotHeader))
            break;
        if (std::memcmp(slotHeader, kSlotMagic, 8) != 0)
        {
            _freeJournalSlots.push_back(s);
            continue;
        }
        const uint64_t group = Get64(slotHeader + 8);
        if (Get32(slotHeader + 40) != Fnv32(slotHeader, 40) || group >= groups)
        {
            result.badSlots++;
            losers.push_back(s);
            continue;
        }
        const uint64_t sequence = Get64(slotHeader + 16);
        auto [it, fresh] = best.emplace(group, std::make_pair(sequence, s));
        if (!fresh)
        {
            losers.push_back(sequence > it->second.first ? it->second.second : s);
            if (sequence > it->second.first)
                it->second = {sequence, s};
        }
    }
    _nextJournalSlot = slots;
    // Slots that lost to a newer one, or are damaged, are wiped so they cannot come back
    const uint8_t zero[kSlotHeaderBytes] = {};
    for (uint64_t s : losers)
    {
        file->Write(SlotOffset(s), zero, sizeof zero);
        _freeJournalSlots.push_back(s);
    }
    file->Flush();

    std::vector<uint8_t> sector(kSector);
    for (const auto& [group, entry] : best)
    {
        uint8_t slotHeader[kSlotHeaderBytes];
        if (!file->Read(SlotOffset(entry.second), slotHeader, sizeof slotHeader))
            continue;
        Group& g = MakeGroup(group);
        g.slot = entry.second;
        g.sequence = entry.first;
        g.journal[0] = Get64(slotHeader + 24);
        g.journal[1] = Get64(slotHeader + 32);
        for (uint64_t i = 0; i < kChunkSectors; i++)
        {
            if (!Has(g.journal, i))
                continue;
            const uint64_t lba = group * kChunkSectors + i;
            if (lba >= SectorCount() || !file->Read(SlotOffset(g.slot) + kSlotHeaderBytes + i * kSector, sector.data(), kSector))
            {
                Clear(g.journal, i);
                continue;
            }
            ToggleHash(lba, sector.data());
            _journalSectors++;
            _changed++;
            result.sectors++;
        }
        if (!g.journal[0] && !g.journal[1])
            QueueHeader(group, g);  // nothing readable: the slot is wiped at the next flush
    }
    _journal = std::move(file);
    if (result.sectors)
        _generation++;
    result.outcome = JournalOpen::Outcome::Replayed;
    return result;
}

void SessionWriteMap::CloseJournal(bool keep)
{
    if (!_journalPath.empty())
        ReleaseJournal(JournalKey(_journalPath));
    if (keep && !_journalPath.empty() && _changed)
    {
        FlushJournal();
        if (_journal)
            _journal->Close();
    }
    else if (_journal)
    {
        _journal->Remove();
    }
    _journal.reset();
    _journalPath.clear();
}

std::string SessionWriteMap::SpillPath() const
{
    return _journal ? _journal->Path() : std::string();
}

/// endregion </Journal>

/// region <Reads and writes>

bool SessionWriteMap::ReadSector(uint64_t lba, uint8_t* dst)
{
    if (lba >= SectorCount())
        return false;
    if (const Group* g = FindGroup(lba / kChunkSectors))
    {
        const uint64_t index = lba % kChunkSectors;
        if (Has(g->hot, index))
        {
            std::memcpy(dst, SlotData(g->refs[Rank(g->hot, index)]), kSector);
            return true;
        }
        if (Has(g->journal, index))
            return _journal && _journal->Read(SlotOffset(g->slot) + kSlotHeaderBytes + index * kSector, dst, kSector);
    }
    return _base->ReadSector(lba, dst);
}

bool SessionWriteMap::ReadChanged(uint64_t lba, uint8_t* dst) const
{
    const Group* g = FindGroup(lba / kChunkSectors);
    if (!g)
        return false;
    const uint64_t index = lba % kChunkSectors;
    if (Has(g->hot, index))
    {
        std::memcpy(dst, SlotData(g->refs[Rank(g->hot, index)]), kSector);
        return true;
    }
    if (Has(g->journal, index))
        return _journal && _journal->Read(SlotOffset(g->slot) + kSlotHeaderBytes + index * kSector, dst, kSector);
    return false;
}

bool SessionWriteMap::WriteSector(uint64_t lba, const uint8_t* src)
{
    if (lba >= SectorCount())
        return false;
    _writesSinceFailure++;

    // Writing the medium's own contents back frees the entry
    uint8_t original[kSector];
    const bool same = _base->ReadSector(lba, original) && std::memcmp(original, src, kSector) == 0;
    const uint64_t group = lba / kChunkSectors;
    const uint64_t index = lba % kChunkSectors;
    Group* g = FindGroup(group);
    const bool hot = g && Has(g->hot, index);
    const bool journal = g && Has(g->journal, index);

    if (hot)
    {
        uint8_t* data = SlotData(g->refs[Rank(g->hot, index)]);
        if (std::memcmp(data, src, kSector) == 0)
            return true;
        ToggleHash(lba, data);
        _generation++;
        if (same)
        {
            RemoveHot(*g, index);
            if (journal)
            {
                Clear(g->journal, index);
                _journalSectors--;
                QueueHeader(group, *g);
            }
            _changed--;
            DropGroupIfEmpty(group);
            return true;
        }
        std::memcpy(data, src, kSector);
        ToggleHash(lba, src);
        MarkDirty(g->refs[Rank(g->hot, index)]);
        return true;
    }

    if (journal)
    {
        uint8_t old[kSector];
        if (_journal && _journal->Read(SlotOffset(g->slot) + kSlotHeaderBytes + index * kSector, old, kSector))
        {
            if (std::memcmp(old, src, kSector) == 0)
                return true;
            ToggleHash(lba, old);
        }
        _generation++;
        if (same)
        {
            Clear(g->journal, index);
            _journalSectors--;
            _changed--;
            QueueHeader(group, *g);
            return true;
        }
        // The newer copy lives in memory; the journal's old one is overwritten at the next flush
        const uint64_t ref = NewSlot(lba);
        g = &MakeGroup(group);  // NewSlot may have moved an arena out: the group stays
        std::memcpy(SlotData(ref), src, kSector);
        AddHot(*g, index, ref);
        MarkDirty(ref);
        ToggleHash(lba, src);
        return true;
    }

    if (same)
        return true;
    const uint64_t ref = NewSlot(lba);
    Group& fresh = MakeGroup(group);
    std::memcpy(SlotData(ref), src, kSector);
    AddHot(fresh, index, ref);
    MarkDirty(ref);
    ToggleHash(lba, src);
    _changed++;
    _generation++;
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
    uint64_t group = lba / kChunkSectors;
    uint64_t index = lba % kChunkSectors;
    const uint64_t groups = (SectorCount() + kChunkSectors - 1) / kChunkSectors;
    while (group < groups)
    {
        const uint64_t leaf = group / kGroupsPerLeaf;
        if (leaf >= _leaves.size())
            break;
        if (!_leaves[leaf])
        {
            group = (leaf + 1) * kGroupsPerLeaf;
            index = 0;
            continue;
        }
        if (const Group* g = _leaves[leaf]->groups[group % kGroupsPerLeaf].get())
        {
            const uint64_t m0 = g->hot[0] | g->journal[0];
            const uint64_t m1 = g->hot[1] | g->journal[1];
            if (index < 64)
            {
                if (const uint64_t b = m0 & (~0ull << index))
                    return group * kChunkSectors + static_cast<uint64_t>(std::countr_zero(b));
                if (m1)
                    return group * kChunkSectors + 64 + static_cast<uint64_t>(std::countr_zero(m1));
            }
            else if (const uint64_t b = m1 & (~0ull << (index - 64)))
            {
                return group * kChunkSectors + 64 + static_cast<uint64_t>(std::countr_zero(b));
            }
        }
        group++;
        index = 0;
    }
    return std::nullopt;
}

/// endregion </Reads and writes>

void SessionWriteMap::ClearAll()
{
    _leaves.clear();
    _groupCount = 0;
    _changed = 0;
    _journalSectors = 0;
    _arenas.clear();
    _arenaOrder.clear();
    _dirtyTotal = 0;
    _freeJournalSlots.clear();
    _nextJournalSlot = 0;
    _headerQueue.clear();
    _unsynced = false;
    _spillFailed = false;
    _contentHash = 0;
}

void SessionWriteMap::Discard()
{
    if (_changed)
        _generation++;
    ClearAll();
    // Nothing left to recover: the journal goes (a new one is made at the next flush)
    if (_journal)
        _journal->Remove();
    _journal.reset();
}

void SessionWriteMap::SetMemoryLimit(uint64_t bytes)
{
    _settings.memoryLimit = bytes;
    Rebalance();
}

void SessionWriteMap::SetArenaBytes(uint32_t bytes)
{
    if (!_arenaOrder.empty())
        return;
    _settings.arenaBytes = std::max<uint32_t>(static_cast<uint32_t>(kSector), bytes - bytes % kSector);
}

uint64_t SessionWriteMap::IndexBytes() const
{
    // A group: its node and its refs; a leaf: 16384 pointers; an arena's bookkeeping: 13 bytes a slot
    uint64_t refs = 0;
    for (const auto& [id, arena] : _arenas)
        refs += arena->live;
    const uint64_t slots = static_cast<uint64_t>(_arenas.size()) * (_settings.arenaBytes / kSector);
    return static_cast<uint64_t>(_leaves.capacity()) * sizeof(void*) +
           static_cast<uint64_t>(std::count_if(_leaves.begin(), _leaves.end(), [](const auto& l) { return l != nullptr; })) * sizeof(Leaf) +
           _groupCount * (sizeof(Group) + 16) + refs * sizeof(uint64_t) + slots * (sizeof(uint64_t) + 1 + sizeof(uint32_t)) +
           _freeJournalSlots.capacity() * sizeof(uint64_t) + _headerQueue.capacity() * sizeof(uint64_t);
}

uint64_t SessionWriteMap::ContentId() const
{
    // The medium's id, moved by the changes: the same changes give the same id, whichever tier holds them
    const uint64_t base = _base->ContentId();
    return _changed ? Mix(base ^ Mix(_contentHash)) : base;
}

void SessionWriteMap::ToggleHash(uint64_t lba, const uint8_t* data)
{
    _contentHash ^= SectorHash(lba, data);
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

    uint8_t sector[kSector];
    for (uint64_t lba = 0; lba < SectorCount(); lba++)
    {
        if (!ReadSector(lba, sector))
        {
            if (error)
                *error = "cannot read sector " + std::to_string(lba);
            return false;
        }
        out.write(reinterpret_cast<const char*>(sector), static_cast<std::streamsize>(kSector));
    }

    out.flush();
    if (!out && error)
        *error = "write error on " + path;
    return static_cast<bool>(out);
}
