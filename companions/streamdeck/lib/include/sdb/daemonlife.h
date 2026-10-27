// The daemon's life (docs/inprogress/2026-10-19-streamdeck-integration/daemon.md §7, §8): one daemon per user, and
// the processes it lives for.
//
//   - InstanceLock: `<runtime dir>/unreal-streamdeck.lock` (UNREAL_STREAMDECK_LOCK names another file), held with an
//     OS file lock (flock on macOS / Linux, LockFileEx on Windows) for the daemon's whole run; the file names the
//     holder: pid, automation port, viewers port, version, host. A second start reads it and ends (exit 0), so any
//     starter (unreal-qt, the studio, a script) can call "start" blindly.
//   - Parents: the starters' leases. A starter passes `--parent <pid>` (or a second start hands its pid to the running
//     daemon: Adopt, `POST /api/v1/parents`); the daemon ends when the last of them is gone (a process handle per
//     parent: kqueue EVFILT_PROC on macOS, a pidfd on Linux, a process handle on Windows; polled, no thread of its
//     own) or releases it (`DELETE /api/v1/parents/{pid}`). A daemon started with no parent (by hand) stays when its
//     leases go: it is nobody's.
//
// Routes (AddLifeRoutes, REST + MCP through automation::Api):
//   GET    /api/v1/parents          {parents: [pid...], alone}
//   POST   /api/v1/parents          {pid}: lease the daemon for that process (404: no such process)
//   DELETE /api/v1/parents/{pid}    release it; the last one gone ends a daemon that was started by its parents
//   POST   /api/v1/quit             a clean stop (the decks go to the idle screen first)
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "sdb/automation.h"

namespace sdb::life
{

using json = nlohmann::json;

// What the lock file says about the daemon holding it
struct Holder
{
    int pid = 0;
    int api = 0;      // automation port (0: none)
    int viewers = 0;  // device wire port (0: none)
    std::string version, host;

    [[nodiscard]] json ToJson() const;
    [[nodiscard]] static std::optional<Holder> FromJson(const json& j);
};

// UNREAL_STREAMDECK_LOCK, else the per-user runtime directory: $XDG_RUNTIME_DIR, else $TMPDIR (per user on macOS),
// else /tmp with the user id in the name; on Windows %LOCALAPPDATA%\unreal-streamdeck
[[nodiscard]] std::string DefaultLockPath();
// The holder the lock file names (nullopt: no file, or not written yet)
[[nodiscard]] std::optional<Holder> ReadHolder(const std::string& path);

class InstanceLock
{
public:
    InstanceLock() = default;
    ~InstanceLock();
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;

    // true: this process holds the lock now (the file names it with Publish). false: another process holds it
    // (`busy` true), or the file cannot be opened (`error`)
    bool Acquire(const std::string& path, bool* busy = nullptr, std::string* error = nullptr);
    // Rewrites the file with what the daemon is (the ports once they are known)
    bool Publish(const Holder& holder);
    void Release();
    [[nodiscard]] bool Held() const
    {
        return _handle != kNone;
    }
    [[nodiscard]] const std::string& Path() const
    {
        return _path;
    }

private:
    static constexpr std::intptr_t kNone = -1;
    std::intptr_t _handle = kNone;
    std::string _path;
};

[[nodiscard]] int CurrentPid();
// The process that started this one (0 when unknown)
[[nodiscard]] int ParentPid();
[[nodiscard]] bool ProcessAlive(int pid);

class Parents
{
public:
    // alone: the daemon was started with no parent (by hand); it stays when every lease is gone
    explicit Parents(bool alone);
    ~Parents();
    Parents(const Parents&) = delete;
    Parents& operator=(const Parents&) = delete;

    // Lease for `pid`; false (error) when there is no such process. A pid already a parent: true
    bool Add(int pid, std::string* error = nullptr);
    // Release; false when the pid is not a parent
    bool Remove(int pid);
    // Drops the parents that are gone (into `gone`); true when the daemon should end now: it had parents, has none
    // left, and is not alone. Thread-safe; called from the daemon's watcher every 250 ms
    bool Poll(std::vector<int>* gone = nullptr);
    [[nodiscard]] std::vector<int> List() const;
    [[nodiscard]] bool Alone() const
    {
        return _alone;
    }

private:
    struct Watch;
    bool EndLocked() const;
    mutable std::mutex _mutex;
    std::map<int, std::unique_ptr<Watch>> _parents;
    bool _alone = false;
    bool _had = false;
    std::intptr_t _queue = -1;  // macOS: the kqueue
};

// The parent leases and quit on the automation planes. `quit` asks the run to stop (any thread)
void AddLifeRoutes(automation::Api& api, Parents& parents, std::function<void()> quit);

// A second start: the running daemon the lock names, given `parent` as one more lease when it is not 0 (its
// automation port must answer; waits up to `timeoutMs` for a holder still starting). nullopt with `error` when no
// daemon answers there
[[nodiscard]] std::optional<Holder> Adopt(const std::string& lockPath, int parent, int timeoutMs,
                                          std::string* error = nullptr);

}  // namespace sdb::life
