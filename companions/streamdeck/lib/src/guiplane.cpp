#include "sdb/guiplane.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

namespace sdb::gui
{
namespace
{

std::string Env(const char* name)
{
#ifdef _MSC_VER  // MinGW has no _dupenv_s at link time; its getenv is not deprecated
    char* value = nullptr;
    size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr)
        return {};
    std::string out(value);
    free(value);
    return out;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

bool Alive(long long pid)
{
    if (pid <= 0)
        return false;
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr)
        return GetLastError() == ERROR_ACCESS_DENIED;
    DWORD code = 0;
    const bool running = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    return running;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

using Clock = std::chrono::steady_clock;

int MsLeft(Clock::time_point deadline)
{
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return static_cast<int>((std::max)(0LL, static_cast<long long>(left)));
}

// Lines out of a byte stream: `read` fills a buffer (0 = closed, -1 = nothing yet within the time it was given)
class StreamLine : public Line
{
public:
    bool Receive(std::string& line, int timeoutMs) override
    {
        const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        while (true)
        {
            if (const size_t nl = _buffer.find('\n'); nl != std::string::npos)
            {
                line = _buffer.substr(0, nl);
                _buffer.erase(0, nl + 1);
                return true;
            }
            char chunk[4096];
            const long n = Read(chunk, sizeof(chunk), MsLeft(deadline));
            if (n == 0)
                return false;  // closed
            if (n > 0)
                _buffer.append(chunk, static_cast<size_t>(n));
            else if (MsLeft(deadline) == 0)
                return false;
        }
    }

protected:
    // bytes read, 0 closed, -1 none within `timeoutMs`
    virtual long Read(char* to, size_t size, int timeoutMs) = 0;

private:
    std::string _buffer;
};

#ifdef _WIN32
class PipeLine final : public StreamLine
{
public:
    explicit PipeLine(HANDLE pipe) : _pipe(pipe) {}
    ~PipeLine() override
    {
        CloseHandle(_pipe);
    }
    bool Send(const std::string& line) override
    {
        size_t sent = 0;
        while (sent < line.size())
        {
            DWORD wrote = 0;
            if (!WriteFile(_pipe, line.data() + sent, static_cast<DWORD>(line.size() - sent), &wrote, nullptr))
                return false;
            sent += wrote;
        }
        return true;
    }

protected:
    long Read(char* to, size_t size, int timeoutMs) override
    {
        const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        while (true)
        {
            DWORD ready = 0;
            if (!PeekNamedPipe(_pipe, nullptr, 0, nullptr, &ready, nullptr))
                return 0;
            if (ready > 0)
            {
                DWORD got = 0;
                if (!ReadFile(_pipe, to, (std::min)(ready, static_cast<DWORD>(size)), &got, nullptr))
                    return 0;
                return static_cast<long>(got);
            }
            if (MsLeft(deadline) == 0)
                return -1;
            Sleep(2);
        }
    }

private:
    HANDLE _pipe;
};
#else
class SocketLine final : public StreamLine
{
public:
    explicit SocketLine(int fd) : _fd(fd) {}
    ~SocketLine() override
    {
        ::close(_fd);
    }
    bool Send(const std::string& line) override
    {
        size_t sent = 0;
        while (sent < line.size())
        {
#ifdef MSG_NOSIGNAL
            const ssize_t n = ::send(_fd, line.data() + sent, line.size() - sent, MSG_NOSIGNAL);
#else
            const ssize_t n = ::send(_fd, line.data() + sent, line.size() - sent, 0);
#endif
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }

protected:
    long Read(char* to, size_t size, int timeoutMs) override
    {
        pollfd p{_fd, POLLIN, 0};
        const int ready = ::poll(&p, 1, timeoutMs);
        if (ready < 0)
            return errno == EINTR ? -1 : 0;
        if (ready == 0)
            return -1;
        const ssize_t n = ::recv(_fd, to, size, 0);
        if (n < 0)
            return errno == EINTR || errno == EAGAIN ? -1 : 0;
        return static_cast<long>(n);
    }

private:
    int _fd;
};
#endif

// "http://localhost:8090/" -> "http://127.0.0.1:8090"
std::string NormalEndpoint(std::string url)
{
    std::transform(url.begin(), url.end(), url.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const size_t scheme = url.find("://");
    const size_t hostAt = scheme == std::string::npos ? 0 : scheme + 3;
    const size_t path = url.find('/', hostAt);
    if (path != std::string::npos)
        url.erase(path);
    std::string host = url.substr(hostAt);
    const std::string prefix = url.substr(0, hostAt);
    if (host.rfind("localhost", 0) == 0)
        host = "127.0.0.1" + host.substr(9);
    if (host.find(':') == std::string::npos)
        host += prefix == "https://" ? ":443" : ":80";
    return (prefix.empty() ? std::string("http://") : prefix) + host;
}

const char* kNoHost = "no unreal-ng program serves this WebAPI on the GUI control plane";

}  // namespace

std::string DiscoveryDirectory()
{
    if (std::string dir = Env("UNREAL_GUI_CONTROL_DIR"); !dir.empty())
        return dir;
    std::string base;
#if defined(_WIN32)
    base = Env("USERPROFILE");
#elif defined(__APPLE__)
    base = Env("HOME") + "/Library/Application Support";
#else
    base = Env("XDG_RUNTIME_DIR");
    if (base.empty())
        base = std::filesystem::temp_directory_path().string() + "/runtime-" + Env("USER");
#endif
    if (base.empty())
        base = std::filesystem::temp_directory_path().string();
    return base + "/unreal-ng/gui";
}

std::vector<HostEntry> Hosts(const std::string& dir)
{
    std::vector<HostEntry> hosts;
    std::error_code ec;
    for (const auto& file : std::filesystem::directory_iterator(dir, ec))
    {
        if (file.path().extension() != ".json")
            continue;
        std::ifstream in(file.path(), std::ios::binary);
        const json j = json::parse(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()),
                                   nullptr, false);
        if (!j.is_object())
            continue;
        HostEntry entry;
        entry.id = j.value("id", "");
        entry.pid = j.value("pid", 0LL);
        entry.program = j.value("program", "");
        entry.socket = j.value("socket", "");
        for (const auto& e : j.value("endpoints", json::array()))
            if (e.is_string())
                entry.endpoints.push_back(e.get<std::string>());
        if (!entry.id.empty() && !entry.socket.empty() && Alive(entry.pid))
            hosts.push_back(std::move(entry));
    }
    std::sort(hosts.begin(), hosts.end(), [](const HostEntry& a, const HostEntry& b) { return a.id < b.id; });
    return hosts;
}

bool SameEndpoint(const std::string& endpoint, const std::string& webapi)
{
    return NormalEndpoint(endpoint) == NormalEndpoint(webapi);
}

std::unique_ptr<Line> ConnectLocal(const std::string& path, std::string& error)
{
#ifdef _WIN32
    const std::string name = path.rfind("\\\\.\\pipe\\", 0) == 0 ? path : "\\\\.\\pipe\\" + path;
    HANDLE pipe = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        error = "cannot open the pipe " + name + " (error " + std::to_string(GetLastError()) + ")";
        return nullptr;
    }
    return std::make_unique<PipeLine>(pipe);
#else
    sockaddr_un address{};
    if (path.size() >= sizeof(address.sun_path))
    {
        error = "the socket path is too long: " + path;
        return nullptr;
    }
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        error = std::string("socket: ") + std::strerror(errno);
        return nullptr;
    }
#ifdef SO_NOSIGPIPE
    const int on = 1;
    (void)::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    {
        error = "cannot connect to " + path + ": " + std::strerror(errno);
        ::close(fd);
        return nullptr;
    }
    return std::make_unique<SocketLine>(fd);
#endif
}

Client::Client(std::string webapi, std::string dir) : _webapi(std::move(webapi)), _dir(std::move(dir)) {}

Client::Client(Connector connect) : _connect(std::move(connect)) {}

bool Client::ConnectLocked(std::string& error)
{
    std::unique_ptr<Line> line;
    if (_connect)
        line = _connect(error);
    else
    {
        const std::string dir = _dir.empty() ? DiscoveryDirectory() : _dir;
        std::vector<std::string> others;
        for (const HostEntry& host : Hosts(dir))
        {
            const bool serves = std::any_of(host.endpoints.begin(), host.endpoints.end(),
                                            [&](const std::string& e) { return SameEndpoint(e, _webapi); });
            if (!serves)
            {
                others.push_back(host.program + " " + host.id);
                continue;
            }
            line = ConnectLocal(host.socket, error);
            if (line)
                break;
        }
        if (!line && error.empty())
        {
            error = std::string(kNoHost) + " (" + _webapi + ")";
            if (!others.empty())
            {
                error += "; the hosts in " + dir + ":";
                for (const std::string& other : others)
                    error += " " + other;
            }
        }
    }
    if (!line)
        return false;
    std::string welcome;
    if (!line->Receive(welcome, 2000) || json::parse(welcome, nullptr, false).value("op", "") != "welcome")
    {
        error = "the GUI host sent no welcome";
        return false;
    }
    _line = std::move(line);
    return true;
}

Result Client::CallLocked(const std::string& method, const std::string& target, const json& params, int timeoutMs)
{
    Result result;
    // A program that restarted closed the old socket: one more try on a fresh connection
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        std::string error;
        if (!_line && !ConnectLocked(error))
        {
            result.error = "not_found";
            result.message = error;
            return result;
        }
        const uint64_t rid = ++_rid;
        json call{{"op", "call"}, {"rid", rid}, {"method", method}};
        if (!target.empty())
            call["target"] = target;
        if (params.is_object() && !params.empty())
            call["params"] = params;
        if (!_line->Send(call.dump() + "\n"))
        {
            _line.reset();
            continue;
        }
        const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        std::string text;
        while (_line->Receive(text, MsLeft(deadline)))
        {
            const json answer = json::parse(text, nullptr, false);
            if (!answer.is_object() || answer.value("op", "") == "event")
                continue;
            if (answer.value("op", "") == "error" && !answer.contains("rid"))
                continue;
            if (!answer.contains("rid") || answer["rid"] != rid)
                continue;
            result.ok = answer.value("ok", false);
            result.error = answer.value("error", "");
            result.message = answer.value("message", "");
            if (answer.contains("body") && !answer["body"].is_null())
                result.body = answer["body"];
            return result;
        }
        _line.reset();
        if (MsLeft(deadline) == 0)
        {
            result.error = "timeout";
            result.message = method + ": no answer from the GUI host within " + std::to_string(timeoutMs) + " ms";
            return result;
        }
    }
    result.error = "not_found";
    result.message = "the GUI host closed the connection";
    return result;
}

Result Client::Call(const std::string& method, const std::string& target, const json& params, int timeoutMs)
{
    std::lock_guard l(_m);
    return CallLocked(method, target, params, timeoutMs);
}

json Client::List(int timeoutMs)
{
    std::lock_guard l(_m);
    Result r = CallLocked("list", "", json::object(), timeoutMs);
    if (!r.ok)
        return nullptr;
    _tree = r.body;
    return r.body;
}

bool Client::IsViewInterface(const std::string& iface)
{
    std::lock_guard l(_m);
    if (!_tree.is_object())
        if (Result r = CallLocked("list", "", json::object(), 2000); r.ok)
            _tree = r.body;
    for (const auto& window : _tree.value("windows", json::array()))
        for (const auto& view : window.value("views", json::array()))
            for (const auto& name : view.value("interfaces", json::array()))
                if (name == iface)
                    return true;
    return false;
}

json ReadoutOf(const json& list, const std::string& path)
{
    if (!list.is_object() || path.rfind("gui.", 0) != 0)
        return nullptr;
    const json* at = list.contains("state") ? &list["state"] : nullptr;
    std::string rest = path.substr(4);
    while (at != nullptr && !rest.empty())
    {
        const size_t dot = rest.find('.');
        const std::string part = rest.substr(0, dot);
        rest = dot == std::string::npos ? std::string() : rest.substr(dot + 1);
        if (!at->is_object() || !at->contains(part))
            return nullptr;
        at = &(*at)[part];
    }
    return at != nullptr ? *at : json();
}

}  // namespace sdb::gui
