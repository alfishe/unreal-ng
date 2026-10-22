#include "sdb/daemonlife.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "sdb/http.h"

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/event.h>
#elif defined(__linux__)
#include <poll.h>
#include <sys/syscall.h>
#endif
#endif

namespace sdb::life
{

namespace fs = std::filesystem;

json Holder::ToJson() const
{
    return {{"pid", pid}, {"api", api}, {"viewers", viewers}, {"version", version}, {"host", host}};
}

std::optional<Holder> Holder::FromJson(const json& j)
{
    if (!j.is_object() || !j.contains("pid") || !j["pid"].is_number_integer())
        return std::nullopt;
    Holder h;
    h.pid = j.value("pid", 0);
    h.api = j.value("api", 0);
    h.viewers = j.value("viewers", 0);
    h.version = j.value("version", "");
    h.host = j.value("host", "");
    return h;
}

std::string DefaultLockPath()
{
    if (const char* env = std::getenv("UNREAL_STREAMDECK_LOCK"); env && *env)
        return env;
#ifdef _WIN32
    const char* local = std::getenv("LOCALAPPDATA");
    fs::path dir = fs::path(local && *local ? local : ".") / "unreal-streamdeck";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return (dir / "unreal-streamdeck.lock").string();
#else
    for (const char* name : {"XDG_RUNTIME_DIR", "TMPDIR"})
        if (const char* dir = std::getenv(name); dir && *dir)
            return (fs::path(dir) / "unreal-streamdeck.lock").string();
    return "/tmp/unreal-streamdeck-" + std::to_string(static_cast<long long>(getuid())) + ".lock";
#endif
}

std::optional<Holder> ReadHolder(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::stringstream text;
    text << in.rdbuf();
    const json j = json::parse(text.str(), nullptr, false);
    if (j.is_discarded())
        return std::nullopt;
    return Holder::FromJson(j);
}

// ---------------------------------------------------------------- InstanceLock
InstanceLock::~InstanceLock()
{
    Release();
}

bool InstanceLock::Acquire(const std::string& path, bool* busy, std::string* error)
{
    if (busy)
        *busy = false;
    Release();
    std::error_code ec;
    if (const fs::path parent = fs::path(path).parent_path(); !parent.empty())
        fs::create_directories(parent, ec);
#ifdef _WIN32
    // the path is UTF-8 (the daemon's arguments and environment as it reads them)
    std::wstring wide(static_cast<size_t>(MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0)), L'\0');
    if (!wide.empty())
    {
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), static_cast<int>(wide.size()));
        wide.pop_back();  // the terminator
    }
    HANDLE h = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        if (error)
            *error = "cannot open " + path + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    // one byte far past the content: the content stays readable to a second start
    OVERLAPPED at{};
    at.Offset = 0x7FFFFFF0u;
    if (!LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &at))
    {
        const DWORD why = GetLastError();
        CloseHandle(h);
        if (why == ERROR_LOCK_VIOLATION || why == ERROR_IO_PENDING)
        {
            if (busy)
                *busy = true;
            if (error)
                *error = "another unreal-streamdeck holds " + path;
        }
        else if (error)
            *error = "cannot lock " + path + " (error " + std::to_string(why) + ")";
        return false;
    }
    _handle = reinterpret_cast<std::intptr_t>(h);
#else
    const int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0)
    {
        if (error)
            *error = "cannot open " + path + ": " + std::strerror(errno);
        return false;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0)
    {
        const int why = errno;
        close(fd);
        if (why == EWOULDBLOCK)
        {
            if (busy)
                *busy = true;
            if (error)
                *error = "another unreal-streamdeck holds " + path;
        }
        else if (error)
            *error = "cannot lock " + path + ": " + std::strerror(why);
        return false;
    }
    _handle = fd;
#endif
    _path = path;
    Holder self;
    self.pid = CurrentPid();
    Publish(self);
    return true;
}

bool InstanceLock::Publish(const Holder& holder)
{
    if (_handle == kNone)
        return false;
    const std::string text = holder.ToJson().dump() + "\n";
#ifdef _WIN32
    HANDLE h = reinterpret_cast<HANDLE>(_handle);
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(h, zero, nullptr, FILE_BEGIN) || !SetEndOfFile(h))
        return false;
    DWORD written = 0;
    return WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
#else
    const int fd = static_cast<int>(_handle);
    if (ftruncate(fd, 0) != 0)
        return false;
    return pwrite(fd, text.data(), text.size(), 0) == static_cast<ssize_t>(text.size());
#endif
}

void InstanceLock::Release()
{
    if (_handle == kNone)
        return;
#ifdef _WIN32
    HANDLE h = reinterpret_cast<HANDLE>(_handle);
    LARGE_INTEGER zero{};
    SetFilePointerEx(h, zero, nullptr, FILE_BEGIN);
    SetEndOfFile(h);  // a stale holder is never read: the next start takes the lock
    CloseHandle(h);
#else
    const int fd = static_cast<int>(_handle);
    if (ftruncate(fd, 0) != 0)
    {
        // nothing to do: the lock goes with the descriptor, a second start then takes it
    }
    close(fd);  // releases the flock; the file stays (removing it would race a start that opened it)
#endif
    _handle = kNone;
}

// ---------------------------------------------------------------- processes
int CurrentPid()
{
#ifdef _WIN32
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(getpid());
#endif
}

int ParentPid()
{
#ifdef _WIN32
    const DWORD self = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    int parent = 0;
    for (BOOL more = Process32First(snap, &entry); more; more = Process32Next(snap, &entry))
        if (entry.th32ProcessID == self)
        {
            parent = static_cast<int>(entry.th32ParentProcessID);
            break;
        }
    CloseHandle(snap);
    return parent;
#else
    return static_cast<int>(getppid());
#endif
}

bool ProcessAlive(int pid)
{
    if (pid <= 0)
        return false;
#ifdef _WIN32
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h)
        return false;
    const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
#else
    return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

// ---------------------------------------------------------------- Parents
struct Parents::Watch
{
    int pid = 0;
    bool gone = false;
#ifdef _WIN32
    HANDLE process = nullptr;
#elif defined(__linux__)
    int pidfd = -1;
#endif
    ~Watch()
    {
#ifdef _WIN32
        if (process)
            CloseHandle(process);
#elif defined(__linux__)
        if (pidfd >= 0)
            close(pidfd);
#endif
    }
};

Parents::Parents(bool alone) : _alone(alone)
{
#if defined(__APPLE__)
    _queue = kqueue();
#endif
}

Parents::~Parents()
{
#if defined(__APPLE__)
    if (_queue >= 0)
        close(static_cast<int>(_queue));
#endif
}

bool Parents::Add(int pid, std::string* error)
{
    std::lock_guard l(_mutex);
    if (_parents.count(pid))
        return true;
    auto watch = std::make_unique<Watch>();
    watch->pid = pid;
    bool ok = pid > 0;
#ifdef _WIN32
    if (ok)
    {
        watch->process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
        ok = watch->process && WaitForSingleObject(watch->process, 0) == WAIT_TIMEOUT;
    }
#elif defined(__APPLE__)
    if (ok && _queue >= 0)
    {
        struct kevent change;
        EV_SET(&change, static_cast<uintptr_t>(pid), EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
        ok = kevent(static_cast<int>(_queue), &change, 1, nullptr, 0, nullptr) == 0;
    }
    else
        ok = ok && ProcessAlive(pid);
#elif defined(__linux__)
#ifdef SYS_pidfd_open
    if (ok)
        watch->pidfd = static_cast<int>(syscall(SYS_pidfd_open, static_cast<pid_t>(pid), 0));
#endif
    ok = ok && (watch->pidfd >= 0 || ProcessAlive(pid));
#else
    ok = ok && ProcessAlive(pid);
#endif
    if (!ok)
    {
        if (error)
            *error = "no process " + std::to_string(pid);
        return false;
    }
    _parents[pid] = std::move(watch);
    _had = true;
    return true;
}

bool Parents::Remove(int pid)
{
    std::lock_guard l(_mutex);
    auto it = _parents.find(pid);
    if (it == _parents.end())
        return false;
#if defined(__APPLE__)
    if (_queue >= 0)
    {
        struct kevent change;
        EV_SET(&change, static_cast<uintptr_t>(pid), EVFILT_PROC, EV_DELETE, NOTE_EXIT, 0, nullptr);
        (void)kevent(static_cast<int>(_queue), &change, 1, nullptr, 0, nullptr);  // gone already: nothing to drop
    }
#endif
    _parents.erase(it);
    return true;
}

bool Parents::Poll(std::vector<int>* gone)
{
    std::lock_guard l(_mutex);
#if defined(__APPLE__)
    if (_queue >= 0 && !_parents.empty())
    {
        struct kevent events[16];
        const timespec now{0, 0};
        const int n = kevent(static_cast<int>(_queue), nullptr, 0, events, 16, &now);
        for (int i = 0; i < n; ++i)
            if (auto it = _parents.find(static_cast<int>(events[i].ident)); it != _parents.end())
                it->second->gone = true;
    }
#endif
    for (auto it = _parents.begin(); it != _parents.end();)
    {
        Watch& w = *it->second;
#ifdef _WIN32
        if (w.process && WaitForSingleObject(w.process, 0) == WAIT_OBJECT_0)
            w.gone = true;
#elif defined(__linux__)
        if (w.pidfd >= 0)
        {
            pollfd p{w.pidfd, POLLIN, 0};
            if (poll(&p, 1, 0) > 0 && (p.revents & (POLLIN | POLLHUP | POLLERR)))
                w.gone = true;
        }
        else if (!ProcessAlive(w.pid))
            w.gone = true;
#elif !defined(__APPLE__)
        if (!ProcessAlive(w.pid))
            w.gone = true;
#endif
        if (w.gone)
        {
            if (gone)
                gone->push_back(w.pid);
            it = _parents.erase(it);
        }
        else
            ++it;
    }
    return EndLocked();
}

bool Parents::EndLocked() const
{
    return _had && _parents.empty() && !_alone;
}

std::vector<int> Parents::List() const
{
    std::lock_guard l(_mutex);
    std::vector<int> out;
    for (const auto& [pid, w] : _parents)
        out.push_back(pid);
    return out;
}

// ---------------------------------------------------------------- routes
namespace
{
int PidOf(const json& v)
{
    if (v.is_number_integer())
        return v.get<int>();
    if (v.is_string())
        return std::atoi(v.get<std::string>().c_str());
    return 0;
}
}  // namespace

void AddLifeRoutes(automation::Api& api, Parents& parents, std::function<void()> quit)
{
    using automation::Reply;
    using automation::Request;
    auto list = [&parents] {
        return json{{"parents", parents.List()}, {"alone", parents.Alone()}};
    };
    api.Add({"GET",
             "/api/v1/parents",
             "The processes the daemon lives for",
             "parents: the pids that leased it (unreal-qt, Stream Deck Studio); alone: started by hand, it stays when "
             "every lease is gone",
             {"daemon", "lifetime"},
             {},
             nullptr,
             [list](const Request&) { return Reply::Ok(list()); }});
    api.Add({"POST",
             "/api/v1/parents",
             "Lease the daemon for a process",
             "{pid}: the daemon lives at least as long as that process (or until it releases the lease). A second "
             "`unreal-streamdeck run --parent PID` does this and ends",
             {"daemon", "lifetime"},
             {{"pid", "body", "integer", "the process id", true}},
             json{{"pid", 4242}},
             [&parents, list](const Request& r) {
                 const int pid = PidOf(r.body.is_object() ? r.body.value("pid", json()) : json());
                 std::string error;
                 if (pid <= 0)
                     return Reply::Error(400, "pid: a process id");
                 if (!parents.Add(pid, &error))
                     return Reply::Error(404, error);
                 return Reply::Ok(list());
             }});
    api.Add({"DELETE",
             "/api/v1/parents/{pid}",
             "Release a lease",
             "The last lease gone ends a daemon its parents started (not one started by hand)",
             {"daemon", "lifetime"},
             {{"pid", "path", "integer", "the process id", true}},
             nullptr,
             [&parents, list](const Request& r) {
                 const int pid = std::atoi(r.Str("pid").c_str());
                 if (!parents.Remove(pid))
                     return Reply::Error(404, "no lease for process " + r.Str("pid"));
                 json body = list();
                 body["ending"] = parents.Poll();
                 return Reply::Ok(body);
             }});
    api.Add({"POST",
             "/api/v1/quit",
             "Stop the daemon",
             "A clean stop: every deck is told, the surface client says bye, the lock is released",
             {"daemon", "lifetime"},
             {},
             nullptr,
             [quit](const Request&) {
                 if (quit)
                     quit();
                 return Reply::Ok({{"quitting", true}});
             }});
}

std::optional<Holder> Adopt(const std::string& lockPath, int parent, int timeoutMs, std::string* error)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::optional<Holder> holder;
    for (;;)
    {
        holder = ReadHolder(lockPath);
        // a holder still starting has written its pid only: its automation port comes once it listens
        if (holder && holder->api > 0 && ProcessAlive(holder->pid))
            break;
        if (std::chrono::steady_clock::now() >= deadline)
        {
            if (error)
                *error = holder ? "the running unreal-streamdeck (pid " + std::to_string(holder->pid) +
                                      ") has no automation port"
                                : "no holder in " + lockPath;
            return holder && holder->pid > 0 ? holder : std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (parent > 0)
    {
        const std::string url = "http://127.0.0.1:" + std::to_string(holder->api);
        const auto r = HttpRequest(url, "POST", "/api/v1/parents", json{{"pid", parent}}.dump(), {}, 2000);
        if (!r.Ok())
        {
            if (error)
                *error = "the running unreal-streamdeck refused the lease: " +
                         (r.status ? "HTTP " + std::to_string(r.status) : r.error);
            return std::nullopt;
        }
    }
    return holder;
}

}  // namespace sdb::life
