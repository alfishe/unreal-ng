#pragma once

/// @file sessionwritemap.h
/// @brief Guest writes kept on top of any disk: the medium underneath is
/// never modified.
///
/// Used for "write for this session only" (SD card WriteMode::Session, host
/// folder volumes, composites, blank media). A write stores the sector; a
/// write that makes a sector equal to the medium again drops it, so software
/// that rewrites unchanged sectors costs nothing (DOSBox-X bios_disk.cpp does
/// the same). The result, medium plus changes, can be exported to a plain
/// image file.
///
/// The changed sectors live in two tiers (C10e). In memory: arenas of sector
/// slots (1 MiB each by default), up to a limit (16 MiB); a rewrite goes to
/// its slot in place. On disk: a journal of 64 KiB slots, one per group of
/// 128 sectors, each with a header naming the sectors it holds. The oldest
/// arena moves to the journal when the limit is crossed, and every write
/// reaches the journal at most `flushSeconds` (30) after it was made. A
/// journal with a name (next to the medium) survives a crash of the emulator
/// and is replayed by the next insert (`OpenJournal`); without one a session
/// spills into a temp file that is gone with it. A sector is changed when
/// either tier holds it; memory wins when both do.
///
/// Design: docs/inprogress/2026-09-21-profi/2026-09-25-ide-hdd-design.md §7.4.3,
/// shared with the SD card by tdd-storage-sd-ide-cd.md §1 (S3); the tiers:
/// docs/inprogress/2026-10-05-media-multisource/phases/c10e-session-journal.md.

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "emulator/io/storage/changeview.h"
#include "emulator/io/storage/iblockdevice.h"

class SessionJournalFile;

/// The tunables of new sessions ([MEDIA] keys, c10e-session-journal.md §5)
struct SessionSettings
{
    uint64_t memoryLimit = 16ull * 1024 * 1024;  ///< bytes of arenas per session; 0: no limit
    uint32_t arenaBytes = 1024 * 1024;           ///< a multiple of 512 (the config takes 64 KiB ... 16 MiB)
    uint32_t flushSeconds = 30;                  ///< the longest a write stays only in memory; 0: only at the limit
    uint32_t syncSeconds = 30;                   ///< how often a written journal is synced to the disk; 0: never
    bool journal = true;                         ///< media get a journal next to them (the insert's default)
    std::string spillFolder;                     ///< journals without a place of their own; empty: the system temp folder
};

class SessionWriteMap : public IBlockDevice, public IChangeView
{
public:
    static constexpr uint64_t kChunkSectors = 128;  ///< a journal slot: 64 KiB

    /// The settings sessions created from now on get (process-wide: the last loaded config)
    static void SetDefaults(const SessionSettings& settings);
    static SessionSettings Defaults();
    static void SetDefaultMemoryLimit(uint64_t bytes);
    static uint64_t DefaultMemoryLimit();
    static void SetSpillFolder(const std::string& folder);
    static std::string SpillFolder();
    /// A default for `journal` that the config cannot change (the test runner: no journal next to test data unless a
    /// test asks for one); nullopt: the config decides
    static void OverrideJournalDefault(std::optional<bool> on);
    /// Groups written to journals by every session of the process so far (tests, reports)
    static uint64_t TotalSpilledChunks();

    explicit SessionWriteMap(std::unique_ptr<IBlockDevice> base);
    ~SessionWriteMap() override;

    uint64_t SectorCount() const override { return _base->SectorCount(); }
    bool ReadSector(uint64_t lba, uint8_t* dst) override;
    bool WriteSector(uint64_t lba, const uint8_t* src) override;
    bool IsWritable() const override { return true; }
    /// The base's run, cut at the next changed sector (a changed sector itself: 0)
    uint64_t ZeroRun(uint64_t lba) override;
    std::optional<BlockGeometry> NativeGeometry() const override { return _base->NativeGeometry(); }
    std::string Describe() const override { return _base->Describe() + " (session writes)"; }
    uint64_t ContentId() const override;

    IBlockDevice& Base() { return *_base; }
    const IBlockDevice& Base() const { return *_base; }

    // IChangeView: the changed sectors of both tiers
    size_t ChangedSectors() const override { return _changed; }
    std::optional<uint64_t> NextChanged(uint64_t lba) const override;
    bool ReadChanged(uint64_t lba, uint8_t* dst) const override;

    /// Forget every change: reads show the medium again; the journal file goes
    void Discard();

    /// Moves with every write that changes the layer (a saved session delta records it)
    uint64_t Generation() const { return _generation; }

    // --- Tunables of this session ---
    void SetMemoryLimit(uint64_t bytes);  ///< a lower one moves arenas to the journal at once
    uint64_t MemoryLimit() const { return _settings.memoryLimit; }
    void SetArenaBytes(uint32_t bytes);   ///< only while nothing is held in memory
    uint32_t ArenaBytes() const { return _settings.arenaBytes; }
    void SetFlushSeconds(uint32_t seconds) { _settings.flushSeconds = seconds; }
    void SetSyncSeconds(uint32_t seconds) { _settings.syncSeconds = seconds; }
    /// Milliseconds of a monotonic clock (tests inject their own)
    void SetClock(std::function<uint64_t()> nowMs) { _now = std::move(nowMs); }

    /// Once a frame: writes older than flushSeconds go to the journal, a written journal is synced every syncSeconds
    void Tick();
    /// Every write in memory to the journal now (and synced)
    bool FlushJournal();

    // --- The journal ---
    enum class JournalMode : uint8_t
    {
        Replay,   ///< a journal found at the path is replayed (it was written over this base)
        Discard,  ///< a journal found at the path is deleted unread
        Off,      ///< no journal at the path: a temp file, gone with the session
    };
    struct JournalOpen
    {
        enum class Outcome : uint8_t
        {
            Created,   ///< none was there: one is made at the first flush
            Replayed,  ///< `sectors` sectors came back
            Discarded, ///< the old one was deleted unread
            Stale,     ///< written over another base, or damaged: renamed to `kept`, not replayed
            Off,       ///< no journal at the path (a temp file instead)
            InUse,     ///< another live session has this journal: a temp file instead, not replayed
        };
        Outcome outcome = Outcome::Created;
        size_t sectors = 0;
        size_t badSlots = 0;  ///< damaged slot headers skipped while replaying
        std::string kept;     ///< Stale: the name the old journal now has
        std::string detail;   ///< Stale: why
    };
    /// Name this session's journal `path` (no other session may use it). Call before the first write
    JournalOpen OpenJournal(const std::string& path, JournalMode mode);
    /// The session ends: `keep` leaves a named journal on disk (flushed and synced) for the next insert; otherwise
    /// it is deleted. A temp journal always goes
    void CloseJournal(bool keep);
    /// The journal file ("" before it exists; "(deleted) ..." for a temp one already unlinked)
    std::string SpillPath() const;
    /// A named journal: it survives a crash and is replayed by the next insert
    bool JournalRecoverable() const { return !_journalPath.empty(); }
    /// A journal write failed (disk full, no folder): the sectors stay in memory, over the limit
    bool SpillFailed() const { return _spillFailed; }

    // --- Accounting ---
    /// The arenas held in memory
    uint64_t HotBytes() const { return static_cast<uint64_t>(_arenaOrder.size()) * _settings.arenaBytes; }
    /// Sectors whose data the journal holds (some may have a newer copy in memory)
    size_t SpilledSectors() const { return _journalSectors; }
    /// The index of both tiers and the arena bookkeeping (an estimate by node sizes)
    uint64_t IndexBytes() const;

    /// A save replaced the medium's file: close the old medium first (a file
    /// open on Windows cannot be replaced), then put the new one under the
    /// changes. The layer has no medium in between: no reads then
    std::unique_ptr<IBlockDevice> ReleaseBase() { return std::move(_base); }
    void SetBase(std::unique_ptr<IBlockDevice> base) { _base = std::move(base); }

    /// Write the whole disk as the guest sees it (medium plus changes) to a
    /// plain image file. False on a host I/O error or an unreadable sector
    bool ExportTo(const std::string& path, std::string* error = nullptr);

private:
    static constexpr uint64_t kNone = ~0ull;
    static constexpr uint64_t kGroupsPerLeaf = 16384;  ///< 1 GiB of the disk per leaf of the index

    /// Its sectors live in pages taken from the OS and given back whole when it goes (no heap: RSS drops)
    struct Arena
    {
        explicit Arena(uint32_t bytes);
        ~Arena();
        Arena(const Arena&) = delete;
        Arena& operator=(const Arena&) = delete;

        uint8_t* data = nullptr;
        size_t bytes = 0;
        std::vector<uint64_t> lba;     ///< per slot; kNone: free
        std::vector<uint8_t> dirty;    ///< per slot: not in the journal yet
        std::vector<uint32_t> free;    ///< freed slots, reused before `next`
        uint32_t next = 0;
        uint32_t live = 0;
        uint32_t dirtyCount = 0;
    };

    struct Group
    {
        uint64_t hot[2] = {0, 0};      ///< sectors in memory
        uint64_t journal[2] = {0, 0};  ///< sectors in the journal slot
        std::vector<uint64_t> refs;    ///< (arena id << 32 | slot) of the hot sectors, by rank in `hot`
        uint64_t slot = kNone;         ///< its journal slot
        uint64_t sequence = 0;
        bool headerDirty = false;      ///< the slot header on disk does not match `journal`
        bool queued = false;           ///< in _headerQueue
    };

    struct Leaf
    {
        std::array<std::unique_ptr<Group>, kGroupsPerLeaf> groups;
    };

    static bool Has(const uint64_t* mask, uint64_t i) { return (mask[i / 64] >> (i % 64)) & 1; }
    static void Set(uint64_t* mask, uint64_t i) { mask[i / 64] |= 1ull << (i % 64); }
    static void Clear(uint64_t* mask, uint64_t i) { mask[i / 64] &= ~(1ull << (i % 64)); }
    static size_t Rank(const uint64_t* mask, uint64_t i);

    Group* FindGroup(uint64_t group) const;
    Group& MakeGroup(uint64_t group);
    void DropGroupIfEmpty(uint64_t group);

    uint8_t* SlotData(uint64_t ref) const;
    uint64_t NewSlot(uint64_t lba);  ///< a slot in the newest arena (a new arena past the newest's end)
    void FreeSlot(uint64_t ref);
    void MarkDirty(uint64_t ref);
    void AddHot(Group& g, uint64_t index, uint64_t ref);
    void RemoveHot(Group& g, uint64_t index);

    bool EnsureJournal();
    bool WriteDirty(Arena& arena);  ///< its dirty slots into their groups' journal slots
    bool WriteHeaders();                              ///< the queued slot headers
    void QueueHeader(uint64_t group, Group& g);
    bool EvictOldest();
    void Rebalance();  ///< arenas over the limit go to the journal
    void ToggleHash(uint64_t lba, const uint8_t* data);
    uint64_t Now() const;
    void ClearAll();

    std::unique_ptr<IBlockDevice> _base;
    SessionSettings _settings;
    std::function<uint64_t()> _now;

    std::vector<std::unique_ptr<Leaf>> _leaves;
    size_t _groupCount = 0;
    size_t _changed = 0;
    size_t _journalSectors = 0;

    std::unordered_map<uint32_t, std::unique_ptr<Arena>> _arenas;
    std::deque<uint32_t> _arenaOrder;  ///< oldest first
    uint32_t _nextArenaId = 0;
    uint64_t _dirtyTotal = 0;
    uint64_t _oldestDirtyMs = 0;

    std::unique_ptr<SessionJournalFile> _journal;
    std::string _journalPath;  ///< empty: a temp journal
    bool _journalOff = false;
    std::vector<uint64_t> _freeJournalSlots;
    uint64_t _nextJournalSlot = 0;
    std::vector<uint64_t> _headerQueue;  ///< groups whose slot header is to be written
    bool _unsynced = false;
    uint64_t _lastSyncMs = 0;
    bool _spillFailed = false;
    uint64_t _writesSinceFailure = 0;

    uint64_t _contentHash = 0;  ///< XOR over the changed sectors of a hash of (lba, data)
    uint64_t _generation = 0;
};
