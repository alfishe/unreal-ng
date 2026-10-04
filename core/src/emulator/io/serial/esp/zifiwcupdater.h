#pragma once

/// @file zifiwcupdater.h
/// @brief The ZiFi S3 firmware's Wild Commander updater (WCU_START / APPLY / STOP / SYNC, events 67 / 68): the ESP
/// compares the WC files on the SD card (VFS reads, git SHA-1) with a GitHub tree (HTTPS) and replaces the ones the
/// user marks, through a verified copy and FILEX MOVE_RENAME (or the RENAME fallback). Ported from ZiFi-ESP32-S3-Zero
/// 2e5ba83 (https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/wc_updater.cpp,
/// src/wc_update_service.cpp; docs/PROTOCOL.md "Проверка и обновление Wild Commander").
///
/// The firmware runs it as a task of its own that blocks on every VFS answer and HTTPS fetch. Here it is a C++20
/// coroutine with the firmware's control flow kept as it is; every contact with the outside (a VFS answer, a fetch,
/// an event, the time, the stop flag, the rings) is a "primitive" whose result is logged. TTD state = the session's
/// parameters + that log + the primitive in flight: a load re-runs the coroutine from the start against the log
/// (no side effects) and leaves it waiting where it was. Bodies are kept by journal reference.

#include <coroutine>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "emulator/io/serial/esp/zifigit.h"
#include "emulator/io/serial/esp/zifihttpfetch.h"
#include "emulator/io/serial/esp/zifivfsbridge.h"

class ZiFiBridgeHost;
class ZiFiStateWriter;
class ZiFiStateReader;

class ZiFiWcUpdater
{
public:
    static constexpr size_t kMaxFiles = 96;
    static constexpr size_t kMaxProtected = 8;
    static constexpr size_t kMaxDirectories = 16;
    static constexpr size_t kMaxJson = 64 * 1024;
    static constexpr uint32_t kMaxFileSize = 1024 * 1024;
    static constexpr uint8_t kEventState = 0x67, kEventEntry = 0x68;

    enum class Status : uint8_t
    {
        Unknown = 0,
        Same = 1,
        Different = 2,
        New = 3,
        LocalOnly = 4,
        KeptSame = 5,
        KeptDifferent = 6,
        ReadError = 7,
        Updated = 8,
        Failed = 9,
    };
    enum class Phase : uint8_t
    {
        Github = 1,
        Local = 2,
        Check = 3,
        Ready = 4,
        Apply = 5,
        Sync = 6,
        Error = 0xFF,
    };

    ZiFiWcUpdater(ZiFiBridgeHost& host, ZiFiHttpFetch& fetch);
    ~ZiFiWcUpdater();

    // --- WcUpdateService ----------------------------------------------------------------------------------------

    /// WCU_START: configure and run the check; false with `error` ("bad repository", "already running")
    bool Start(const std::vector<uint8_t>& payload, std::string& error);
    /// WCU_APPLY: queued while the session waits for a command and no APPLY runs
    bool Apply(const std::vector<uint8_t>& indices);
    /// WCU_SYNC
    bool Sync();
    /// WCU_STOP: the stop flag (the fetch ends at once) and a STOP command; Finished() says when it is over
    void RequestStop();
    bool Running() const { return _session.h != nullptr && !_finished; }
    bool Finished() const { return _finished; }
    /// Drop the session (it ended, or the module restarted): no socket is touched
    void Release();
    /// Resume the session when what it waits for is there (VFS answer, fetch, event room, a command, time)
    void Poll();

    // Status
    std::string Activity() const;
    size_t FileCount() const { return _files.size(); }
    const std::string& Commit() const { return _commit; }
    const std::string& LastState() const { return _stateText; }
    size_t LogLength() const { return _log.size(); }

    // TTD
    void Save(ZiFiStateWriter& w) const;
    /// Rebuilds the session by re-running it against the saved log; the fetch is loaded first (bodies by journal
    /// reference through `bytes`)
    bool Load(ZiFiStateReader& r, const EspStack::ByteSource& bytes);

    // --- The coroutine type (public: the member coroutines return it) ---------------------------------------------

    struct Task
    {
        struct promise_type
        {
            bool value = false;
            std::coroutine_handle<> continuation;
            Task get_return_object() { return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
            std::suspend_always initial_suspend() noexcept { return {}; }
            struct Final
            {
                bool await_ready() noexcept { return false; }
                std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept
                {
                    auto c = h.promise().continuation;
                    return c ? c : std::noop_coroutine();
                }
                void await_resume() noexcept {}
            };
            Final final_suspend() noexcept { return {}; }
            void return_value(bool v) { value = v; }
            void unhandled_exception() {}
        };
        std::coroutine_handle<promise_type> h;
        Task() = default;
        explicit Task(std::coroutine_handle<promise_type> handle) : h(handle) {}
        Task(Task&& other) noexcept : h(other.h) { other.h = nullptr; }
        Task& operator=(Task&& other) noexcept
        {
            if (this != &other)
            {
                if (h)
                    h.destroy();
                h = other.h;
                other.h = nullptr;
            }
            return *this;
        }
        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;
        ~Task()
        {
            if (h)
                h.destroy();
        }
        bool await_ready() const noexcept { return false; }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> c) noexcept
        {
            h.promise().continuation = c;
            return h;
        }
        bool await_resume() noexcept { return h.promise().value; }
    };

    /// One logged contact with the outside
    enum class Prim : uint8_t
    {
        None,
        Vfs,          ///< a VFS request with the updater's own wait (15 / 70 / 190 s)
        Event,        ///< 67 / 68: waits for room up to 5 s
        Millis,
        Stopped,
        FetchRef,     ///< the branch's commit
        FetchTree,    ///< the tree
        Download,     ///< one file into the download buffer (3 attempts, SHA-checked)
        HashBegin,
        HashChunk,    ///< ring -> SHA, up to 1024 bytes
        HashFinish,
        RingToNet,    ///< bytes the VFS read into the ring (vfsToNetworkAvailable)
        RingWrite,    ///< download buffer -> ring (writeFromNetwork)
        RingQueued,   ///< networkToVfsAvailable
        RingFree,     ///< networkToVfsFree
        Command,      ///< the next WCU command
        SetApplying,
        DropDownload, ///< the file is done: its buffer goes (a TTD checkpoint then needs no journal bytes for it)
    };
    struct Entry
    {
        Prim prim = Prim::None;
        bool ok = false;
        uint32_t a = 0, b = 0;
        std::string text;
        std::vector<uint8_t> blob;
        ZiFiVfsBridge::Result vfs;
        std::vector<zifigit::TreeEntry> tree;
    };
    /// A primitive's request (what is issued when live)
    struct Request
    {
        Prim prim = Prim::None;
        uint8_t op = 0;         ///< Vfs: ZiFiVfsBridge::Op; Event: the command
        uint32_t a = 0, b = 0;  ///< value / offset, timeout ms, ...
        std::string path, path2;
        std::vector<uint8_t> data;
        bool submitted = false; ///< Vfs: the request went to the bridge
    };
    struct Await
    {
        ZiFiWcUpdater& u;
        Request request;
        bool await_ready() { return u.Ready(request); }
        void await_suspend(std::coroutine_handle<> h) { u._suspended = h; }
        Entry await_resume() { return u.Resume(); }
    };

private:
    struct File
    {
        std::string path;
        uint8_t sha[zifigit::Sha1::kDigestSize] = {};
        uint32_t remoteSize = 0, localSize = 0;
        bool remote = false, local = false, kept = false;
        Status status = Status::Unknown;
        std::string reason;
    };
    enum class Replace : uint8_t
    {
        Done,
        Untouched,
        Unknown
    };

    // Primitive machinery
    bool Ready(const Request& request);
    Entry Resume();
    void Issue(const Request& request);
    bool Complete(Entry& out);
    void Record(const Entry& e) { _log.push_back(e); }

    Await Vfs(ZiFiVfsBridge::Op op, const std::string& path, uint32_t value, uint32_t waitMs);
    Await VfsRename(const std::string& oldPath, const std::string& newName, uint32_t waitMs);
    Await VfsMove(const std::string& oldPath, const std::string& newPath, bool replace, uint32_t waitMs);
    Await Event(uint8_t cmd, const std::vector<uint8_t>& payload);
    Await Prim0(Prim prim, uint32_t a = 0, uint32_t b = 0);
    Await Fetch(Prim prim, const std::string& path, uint32_t size = 0, const uint8_t* sha = nullptr);

    // The firmware's procedures (wc_updater.cpp), as coroutines
    Task Session();
    Task Check();
    Task FetchRemote();
    Task ScanLocal();
    Task ScanDirectory(size_t index, bool reportExtra);
    Task ProbeLeftovers(size_t index, std::string directoryPath);
    Task HashFiles();
    Task HashLocal(std::string path, uint32_t size, uint8_t* digest);
    Task SendState(Phase phase, uint16_t current, uint16_t total, std::string text);
    Task SendEntry(size_t index);
    Task SendReady();
    Task AddProgress(uint32_t bytes);
    Task Fail(std::string text);
    Task Resend();
    Task ApplyFiles(std::vector<uint8_t> indices);
    Task UpdateFile(size_t index);
    Task WriteLocal(std::string path, uint32_t size, bool* left);
    Task EnsureDirectory(std::string relativeFile);
    Task VfsReplace(std::string tempPath, std::string finalPath, std::string finalName, std::string asidePath,
                    std::string relativeFile, bool keepExisting, Replace* out);
    Task RenameReplace(std::string tempPath, std::string finalPath, std::string finalName, std::string asidePath,
                       std::string relativeFile, bool keepExisting, Replace* out);
    Task RenameEntry(std::string oldPath, std::string newName);
    Task QuietVfs(ZiFiVfsBridge::Op op, std::string path, uint32_t waitMs);
    Task StopNow();

    bool Configure(const std::vector<uint8_t>& payload, std::string& error);
    bool IsProtected(const std::string& path) const;
    File* FindFile(const std::string& path);
    int DirectoryIndex(const std::string& dir) const;
    int DirectoryOf(const std::string& relativeFile) const;
    bool LeftoverIn(const std::string& relativeFile) const;
    void MarkLeftover(const std::string& relativeFile);
    void SortFiles();
    static bool CanUpdate(const File& f);
    static bool Updatable(Status s);
    static bool AutoSelect(const File& f);
    static bool UrlEncodePath(const std::string& path, std::string& out);

    // Primitive executors (live)
    void StartDownload();
    void PollDownload();

    ZiFiBridgeHost& _host;
    ZiFiHttpFetch& _fetch;

    // Session parameters (WCU_START's payload; configure() rebuilds the rest)
    std::vector<uint8_t> _payload;
    // The service
    Task _session;
    std::coroutine_handle<> _suspended;
    bool _begun = false;   ///< the task ran (Start leaves the first step to Poll)
    bool _finished = true;
    bool _applying = false;
    bool _stop = false;   ///< requestStop: read by the session only through the Stopped primitive
    struct QueuedCommand
    {
        uint8_t kind = 0;   ///< 1 apply, 2 stop, 3 sync
        std::vector<uint8_t> indices;
    };
    std::deque<QueuedCommand> _commands;   ///< xQueueCreate(2)

    // The primitive log and the one in flight
    std::vector<Entry> _log;
    size_t _logPos = 0;
    bool _replaying = false;
    Request _pending;          ///< issued, not complete
    bool _pendingIssued = false;
    uint64_t _pendingDeadline = 0;
    bool _reclaiming = false;  ///< Vfs: the updater's wait ran out, the bridge's result is still owed
    Entry _ready;              ///< the completed result handed to the resumed coroutine

    // Executor state (live side effects live here, saved as they are)
    zifigit::Sha1 _sha;
    std::vector<uint8_t> _download;
    std::vector<netstate::Reference> _downloadRefs;
    uint8_t _downloadAttempt = 0;
    std::string _downloadError;

    // The firmware's members (rebuilt by the coroutine; replay rebuilds them too)
    std::vector<File> _files;
    std::string _repo, _branch, _directory;
    std::vector<std::string> _protected;
    std::string _commit;
    std::string _error;
    std::vector<std::string> _directories;
    std::vector<bool> _leftover, _present;
    bool _diskSuspect = false, _listTruncated = false, _leftoverSeen = false;
    Phase _statePhase = Phase::Github;
    uint16_t _stateCurrent = 0, _stateTotal = 0;
    std::string _stateText;
    uint32_t _progressTotal = 0, _progressDone = 0;
    uint8_t _progressPercent = 0;
    uint32_t _progressSentMs = 0;
};
