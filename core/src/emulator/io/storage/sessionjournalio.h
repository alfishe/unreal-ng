#pragma once

/// @file sessionjournalio.h
/// @brief The session journal's disk side, kept off the emulation thread (C10e, c10e-session-journal.md §8):
/// - `SessionJournalFile`: the file, read and written at offsets (`pread` / `pwrite`, `ReadFile` / `WriteFile` with
///   an offset), safe from several threads at once at different offsets;
/// - `JournalIoPool`: a few I/O threads shared by every session of the process (every emulator instance);
/// - `JournalStrand`: one queue per session; its batches run in the order they were posted, one at a time, on
///   whichever pool thread is free. The emulation thread only posts and later collects what is done.

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class SessionJournalFile
{
public:
    /// A new file at `path` (replacing one there). `temp`: gone with the object (unlinked at once on POSIX,
    /// delete-on-close on Windows)
    static std::unique_ptr<SessionJournalFile> Create(const std::filesystem::path& path, bool temp);
    /// An existing file, read and written in place
    static std::unique_ptr<SessionJournalFile> OpenExisting(const std::filesystem::path& path);
    /// A fresh name for a temp journal in `folder` (empty: the system temp folder); empty when there is none
    static std::filesystem::path TempPath(const std::string& folder);

    ~SessionJournalFile();
    SessionJournalFile(const SessionJournalFile&) = delete;
    SessionJournalFile& operator=(const SessionJournalFile&) = delete;

    bool WriteAt(uint64_t offset, const void* data, size_t size);
    bool ReadAt(uint64_t offset, void* data, size_t size) const;
    /// To the disk, past the OS's cache
    bool Sync();
    uint64_t Size() const;
    /// Closed, the file kept
    void Close();
    /// Closed and deleted
    void Remove();
    std::string Path() const;

private:
    SessionJournalFile() = default;

    std::filesystem::path _path;
#if defined(_WIN32)
    void* _handle = nullptr;
#else
    int _fd = -1;
#endif
    bool _temp = false;
    bool _gone = false;  ///< no name on disk any more
};

/// One batch of writes: the sector data first, then the slot headers that name it, then an optional sync
struct JournalBatch
{
    struct Piece
    {
        uint64_t offset = 0;
        const uint8_t* data = nullptr;  ///< into an arena that waits for this batch; null: `owned` at `ownedAt`
        size_t ownedAt = 0;
        uint32_t size = 0;
    };
    std::shared_ptr<SessionJournalFile> file;
    std::vector<Piece> data;
    std::vector<Piece> headers;
    std::vector<uint8_t> owned;  ///< copies of sectors and header images
    bool sync = false;
    uint64_t ticket = 0;

    bool Empty() const { return data.empty() && headers.empty(); }
};

class JournalStrand : public std::enable_shared_from_this<JournalStrand>
{
public:
    /// Queued behind the strand's earlier batches
    void Post(std::unique_ptr<JournalBatch> batch);
    /// (ticket, written) of the batches done since the last call, in order
    std::vector<std::pair<uint64_t, bool>> TakeDone();
    /// Until every posted batch is done
    void WaitIdle();

private:
    friend class JournalIoPool;
    /// Runs the queued batches (a pool thread); false when there was nothing to run
    bool RunOne();

    std::mutex _mutex;
    std::condition_variable _idle;
    std::deque<std::unique_ptr<JournalBatch>> _queue;
    bool _scheduled = false;  ///< in the pool's ready list or running
    std::vector<std::pair<uint64_t, bool>> _done;
};

class JournalIoPool
{
public:
    static JournalIoPool& Instance();
    /// The threads the pool starts with (0: a quarter of the cores, 1 to 4); takes effect only before the pool's
    /// first use (SessionWriteMap::SetDefaults passes the config's value)
    static void Configure(unsigned threads);
    /// A strand with work, for the next free thread
    void Schedule(std::shared_ptr<JournalStrand> strand);
    /// The pool's threads
    size_t Threads() const { return _threads.size(); }
    ~JournalIoPool();

private:
    JournalIoPool();
    void Worker();

    std::mutex _mutex;
    std::condition_variable _wake;
    std::deque<std::shared_ptr<JournalStrand>> _ready;
    std::vector<std::thread> _threads;
    bool _stop = false;
};
