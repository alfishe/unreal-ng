#include "sdb/actions.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "sdb/format.h"

#ifndef _WIN32
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <windows.h>
#endif
#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#endif

namespace sdb::scn
{

json Substitute(const json& j, const json& vars)
{
    if (j.is_string())
    {
        const std::string s = j.get<std::string>();
        if (s.size() > 1 && s[0] == '$' && vars.contains(s.substr(1)))
            return vars[s.substr(1)];
        std::string out = s;
        for (const auto& [k, v] : vars.items())
        {
            const std::string key = "$" + k, val = v.is_string() ? v.get<std::string>() : v.dump();
            for (size_t p = out.find(key); p != std::string::npos; p = out.find(key, p + val.size()))
                out.replace(p, key.size(), val);
        }
        return out;
    }
    if (j.is_object())
    {
        json o = json::object();
        for (const auto& [k, v] : j.items())
            o[k] = Substitute(v, vars);
        return o;
    }
    if (j.is_array())
    {
        json a = json::array();
        for (const auto& v : j)
            a.push_back(Substitute(v, vars));
        return a;
    }
    return j;
}

namespace
{
std::string HttpMessage(const std::string& what, const HttpResponse& r)
{
    if (r.status == 0)
        return what + " -> " + (r.error.empty() ? "no answer" : r.error);
    std::string m = sdb::Format("{} -> {} ({:.0f} ms)", what, r.status, r.ms);
    if (!r.Ok())
    {
        std::string why;
        try
        {
            const json b = json::parse(r.body);
            why = b.value("message", b.value("error", ""));
        }
        catch (...)
        {
        }
        if (!why.empty())
            m += ": " + why;
    }
    return m;
}
}  // namespace

ActionResult Execute(const Action& a, ActionHost& host, const json& vars, bool* toggleState, int depth)
{
    if (depth > kMaxMultiDepth)
        return {false, "multi actions nested too deep"};
    if (host.Cancelled())
        return {false, "cancelled"};
    const json j = Substitute(a.spec, vars);
    const std::string& k = a.kind;
    if (k == "webapi")
    {
        const std::string method = j.value("method", "POST"), path = j.value("path", "");
        const json body = j.contains("body") ? j["body"] : json();
        const auto r = host.Webapi(method, path, body, j.value("control", method != "GET"));
        return {r.Ok(), HttpMessage("webapi " + method + " " + path, r)};
    }
    if (k == "http")
    {
        const std::string url = j.value("url", "");
        const std::string body = !j.contains("body")     ? std::string()
                                 : j["body"].is_string() ? j["body"].get<std::string>()
                                                         : j["body"].dump();
        const std::string method = j.value("method", body.empty() ? "GET" : "POST");
        const auto r = host.Http(url, method, j.value("headers", json::object()), body);
        return {r.Ok(), HttpMessage("http " + method + " " + url, r)};
    }
    if (k == "delay")
    {
        host.SleepMs(j.value("ms", 0));
        return {true, sdb::Format("delay {} ms", j.value("ms", 0))};
    }
    if (k == "multi")
    {
        const auto& steps = j.value("steps", json::array());
        int i = 0;
        for (const auto& st : steps)
        {
            ++i;
            if (host.Cancelled())
                return {false, sdb::Format("cancelled at step {}", i)};
            const auto r = Execute(Action{st.value("kind", ""), st}, host, vars, nullptr, depth + 1);
            if (!r.ok)
                return {false, sdb::Format("step {}/{}: {}", i, steps.size(), r.message)};
        }
        return {true, sdb::Format("multi: {} steps", steps.size())};
    }
    if (k == "ttd_seek")
    {
        // a dial scrubbing time travel, faster the faster it turns: min..max a detent, in frames or (unit "seconds")
        // in time; the strip shows where it stands (frame, or the time code mm:ss.ff from the start of the recording)
        const bool secs = j.value("unit", "frames") == "seconds";
        const double fps = j.value("fps", 50.0);  // Spectrum frames a second (Pentagon 48.8, 128K 49.4, 48K 50.1)
        const double lo = std::max(secs ? 1.0 / fps : 1.0, j.value("min", 1.0)), hi = std::max(lo, j.value("max", lo));
        const double rate = vars.value("rate", 0.0);                 // detents per second
        const double t = std::clamp((rate - 4.0) / 36.0, 0.0, 1.0);  // 4/s and slower = min, 40/s and faster = max
        const double per = lo * std::pow(hi / lo, t);
        const long long perFrames = std::max(1LL, std::llround(secs ? per * fps : per));
        const long long frames = static_cast<long long>(vars.value("ticks", 1)) * perFrames;
        const auto r = host.TtdSeekBy(frames);
        ActionResult out{r.Ok(), HttpMessage(sdb::Format("ttd seek {:+} frames ({} a detent)", frames, perFrames), r)};
        if (r.Ok())
        {
            try
            {
                const auto a = json::parse(r.body);
                const long long at =
                    a.contains("arrived_at") ? a["arrived_at"].value("frame", 0LL) : a.value("frame", 0LL);
                const long long from = a.value("session_start", 1LL);
                const std::string edge = a.value("at_edge", false) ? " |" : "  ";  // "|" = at an end of the history
                if (secs)
                {
                    const double s = std::max(0LL, at - from) / fps;
                    const int mm = static_cast<int>(s) / 60, ss = static_cast<int>(s) % 60,
                              ff = static_cast<int>(std::fmod(s, 1.0) * fps);
                    out.status =
                        sdb::Format("TTD {:02}:{:02}.{:02}{}", mm, ss, ff, edge);  // when no readout shows the strip
                }
                else
                {
                    out.status = sdb::Format("FRAME {:07}{}", at, edge);  // fixed width: the digits do not shift
                }
            }
            catch (...)
            {
            }
        }
        return out;
    }
    if (k == "run_toggle")
    {  // run <-> pause by the machine's real state (a dial press: no state of its own)
        const auto st = host.Webapi("GET", "", json(), false);
        if (!st.Ok())
            return {false, HttpMessage("run_toggle: state", st)};
        bool paused = false;
        try
        {
            const auto j = json::parse(st.body);
            paused = j.value("is_paused", false) || j.value("state", "") == "paused" || !j.value("is_running", true);
        }
        catch (...)
        {
        }
        const std::string path = paused ? "/resume" : "/pause";
        const auto r = host.Webapi("POST", path, json(), true);
        return {r.Ok(), HttpMessage("run_toggle: " + path, r)};
    }
    if (k == "first")
    {  // the first step that works: e.g. a step through the recorded history, else a live one
        std::string why;
        for (const auto& st : j.value("try", json::array()))
        {
            const auto r = Execute(Action{st.value("kind", ""), st}, host, vars, nullptr, depth + 1);
            if (r.ok)
                return r;
            why += (why.empty() ? "" : "; ") + r.message;
        }
        return {false, "first: none worked: " + why};
    }
    if (k == "dir")
    {  // a dial turn: "left" for each detent to the left, "right" to the right (at most 16 a turn)
        const int ticks = vars.value("ticks", 1);
        const auto it = j.find(ticks < 0 ? "left" : "right");
        if (it == j.end() || !it->is_object())
            return {true, "dir: nothing on this side"};
        const json& side = *it;
        const int n = std::min(std::abs(ticks), 16);
        for (int i = 0; i < std::max(n, 1); ++i)
        {
            if (host.Cancelled())
                return {false, "cancelled"};
            const auto r = Execute(Action{side.value("kind", ""), side}, host, vars, nullptr, depth + 1);
            if (!r.ok)
                return {false, sdb::Format("dir {} {}/{}: {}", ticks < 0 ? "left" : "right", i + 1, n, r.message)};
        }
        return {true, sdb::Format("dir {} x{}", ticks < 0 ? "left" : "right", std::max(n, 1))};
    }
    if (k == "toggle")
    {
        const bool on = toggleState ? !*toggleState : true;
        const auto it = j.find(on ? "on" : "off");
        const json next = it != j.end() && it->is_object() ? *it : json::object();
        auto r = Execute(Action{next.value("kind", ""), next}, host, vars, nullptr, depth + 1);
        if (r.ok && toggleState)
            *toggleState = on;
        r.message = std::string(on ? "on: " : "off: ") + r.message;
        return r;
    }
    if (k == "shell")
    {
        std::string out;
        const int code = host.Shell(j.value("command", ""), j.value("timeout_ms", 10000), out);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
            out.pop_back();
        if (out.size() > 200)
            out = out.substr(out.size() - 200);
        return {code == 0, sdb::Format("shell exit {}{}", code, out.empty() ? "" : ": " + out)};
    }
    if (k == "open")
    {
        const bool ok = host.Open(j.value("target", ""));
        return {ok, "open " + j.value("target", "") + (ok ? "" : " failed")};
    }
    if (k == "keys")
    {
        std::string why;
        const bool ok = host.Keys(j.value("combo", ""), j.value("text", ""), why);
        return {ok, "keys " + j.value("combo", j.value("text", "")) +
                        (ok ? "" : " failed" + (why.empty() ? std::string() : ": " + why))};
    }
    if (k == "nav")
    {
        host.Navigate(j);
        return {true, "nav " + j.value("to", "") + (j.contains("node") ? " " + j.value("node", "") : "")};
    }
    if (k == "brightness")
    {
        host.Brightness(j);
        return {true, "brightness " +
                          (j.contains("value") ? j["value"].dump() : "delta " + j.value("delta", json(0)).dump())};
    }
    return {false, "unknown action kind '" + k + "'"};
}

// ---------------------------------------------------------------- system host
std::string DefaultCompanionFile()
{
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config/unreal-streamdeck/companion.json";
}

namespace
{

std::string CompanionFile(const Settings& s)
{
    return s.companionFile.empty() ? DefaultCompanionFile() : s.companionFile;
}

std::string ReplaceAll(std::string s, const std::string& from, const std::string& to)
{
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
        s.replace(at, from.size(), to);
    return s;
}

void SplitUrl(const std::string& url, std::string& base, std::string& path)
{
    const auto scheme = url.find("://");
    const auto slash = url.find('/', scheme == std::string::npos ? 0 : scheme + 3);
    base = slash == std::string::npos ? url : url.substr(0, slash);
    path = slash == std::string::npos ? "/" : url.substr(slash);
}

#ifndef _WIN32
// Runs argv (no shell unless argv is sh -c), collects stdout+stderr, kills it after timeoutMs (<0: do not wait).
int RunProcess(const std::vector<std::string>& argv, int timeoutMs, std::string& out)
{
    int pipefd[2];
    if (pipe(pipefd) != 0)
        return -1;
    const pid_t pid = fork();
    if (pid < 0)
    {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0)
    {
        dup2(pipefd[1], 1);
        dup2(pipefd[1], 2);
        close(pipefd[0]);
        close(pipefd[1]);
        std::vector<char*> args;
        for (const auto& s : argv)
            args.push_back(const_cast<char*>(s.c_str()));
        args.push_back(nullptr);
        execvp(args[0], args.data());
        _exit(127);
    }
    close(pipefd[1]);
    if (timeoutMs < 0)
    {
        close(pipefd[0]);
        std::thread([pid] {
            int st;
            waitpid(pid, &st, 0);
        }).detach();
        return 0;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    char buf[4096];
    bool timedOut = false;
    for (;;)
    {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0)
        {
            timedOut = true;
            break;
        }
        pollfd p{pipefd[0], POLLIN, 0};
        if (poll(&p, 1, static_cast<int>(std::min<long long>(left, 100))) > 0)
        {
            const ssize_t n = read(pipefd[0], buf, sizeof buf);
            if (n <= 0)
                break;
            if (out.size() < 64 * 1024)
                out.append(buf, static_cast<size_t>(n));
        }
    }
    close(pipefd[0]);
    if (timedOut)
        kill(pid, SIGKILL);
    int st = 0;
    waitpid(pid, &st, 0);
    if (timedOut)
    {
        out += "\n(timeout)";
        return -1;
    }
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
#else
std::wstring Widen(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// Runs a command through cmd.exe (no console window), collects stdout+stderr, kills the whole process tree (a job
// object) after timeoutMs (<0: do not wait).
int RunCommand(const std::string& command, int timeoutMs, std::string& out)
{
    std::wstring line = L"cmd.exe /d /s /c \"" + Widen(command) + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (timeoutMs < 0)
    {
        if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                            &pi))
            return -1;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 0;
    }
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr;
    HANDLE wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0))
        return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in;
    si.hStdOutput = wr;
    si.hStdError = wr;
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    }
    const BOOL started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);  // the child holds its copy; ours would keep the pipe open
    if (in != INVALID_HANDLE_VALUE)
        CloseHandle(in);
    if (!started)
    {
        CloseHandle(rd);
        if (job)
            CloseHandle(job);
        return -1;
    }
    if (job)
        AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    char buf[4096];
    auto drain = [&] {
        DWORD avail = 0;
        while (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
        {
            DWORD n = 0;
            if (!ReadFile(rd, buf, static_cast<DWORD>(std::min<size_t>(avail, sizeof buf)), &n, nullptr) || n == 0)
                break;
            if (out.size() < 64 * 1024)
                out.append(buf, n);
        }
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    bool timedOut = false;
    for (;;)
    {
        drain();
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0)
        {
            timedOut = true;
            break;
        }
        if (WaitForSingleObject(pi.hProcess, static_cast<DWORD>(std::min<long long>(left, 10))) == WAIT_OBJECT_0)
        {
            drain();
            break;
        }
    }
    DWORD code = 0;
    if (timedOut)
    {
        if (job)
            TerminateJobObject(job, 1);
        else
            TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 1000);
    }
    else
        GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(rd);
    if (job)
        CloseHandle(job);  // KILL_ON_JOB_CLOSE: what the command left running goes too
    if (timedOut)
    {
        out += "\n(timeout)";
        return -1;
    }
    return static_cast<int>(code);
}
#endif

// One table for every platform's "keys" action: the name a scenario writes, the macOS virtual key code, the Windows
// virtual-key code (+ whether it is an extended key for SendInput) and the X11 keysym name xdotool takes.
struct KeyCode
{
    std::string_view name;
    std::uint16_t mac, win;
    bool extended;
    std::string_view x11;
};
constexpr KeyCode kKeyCodes[] = {
    {"a", 0, 'A', false, "a"},
    {"b", 11, 'B', false, "b"},
    {"c", 8, 'C', false, "c"},
    {"d", 2, 'D', false, "d"},
    {"e", 14, 'E', false, "e"},
    {"f", 3, 'F', false, "f"},
    {"g", 5, 'G', false, "g"},
    {"h", 4, 'H', false, "h"},
    {"i", 34, 'I', false, "i"},
    {"j", 38, 'J', false, "j"},
    {"k", 40, 'K', false, "k"},
    {"l", 37, 'L', false, "l"},
    {"m", 46, 'M', false, "m"},
    {"n", 45, 'N', false, "n"},
    {"o", 31, 'O', false, "o"},
    {"p", 35, 'P', false, "p"},
    {"q", 12, 'Q', false, "q"},
    {"r", 15, 'R', false, "r"},
    {"s", 1, 'S', false, "s"},
    {"t", 17, 'T', false, "t"},
    {"u", 32, 'U', false, "u"},
    {"v", 9, 'V', false, "v"},
    {"w", 13, 'W', false, "w"},
    {"x", 7, 'X', false, "x"},
    {"y", 16, 'Y', false, "y"},
    {"z", 6, 'Z', false, "z"},
    {"0", 29, '0', false, "0"},
    {"1", 18, '1', false, "1"},
    {"2", 19, '2', false, "2"},
    {"3", 20, '3', false, "3"},
    {"4", 21, '4', false, "4"},
    {"5", 23, '5', false, "5"},
    {"6", 22, '6', false, "6"},
    {"7", 26, '7', false, "7"},
    {"8", 28, '8', false, "8"},
    {"9", 25, '9', false, "9"},
    {"=", 24, 0xBB, false, "equal"},         // VK_OEM_PLUS
    {"-", 27, 0xBD, false, "minus"},         // VK_OEM_MINUS
    {"[", 33, 0xDB, false, "bracketleft"},   // VK_OEM_4
    {"]", 30, 0xDD, false, "bracketright"},  // VK_OEM_6
    {"'", 39, 0xDE, false, "apostrophe"},    // VK_OEM_7
    {";", 41, 0xBA, false, "semicolon"},     // VK_OEM_1
    {"\\", 42, 0xDC, false, "backslash"},    // VK_OEM_5
    {",", 43, 0xBC, false, "comma"},         // VK_OEM_COMMA
    {"/", 44, 0xBF, false, "slash"},         // VK_OEM_2
    {".", 47, 0xBE, false, "period"},        // VK_OEM_PERIOD
    {"`", 50, 0xC0, false, "grave"},         // VK_OEM_3
    {"return", 36, 0x0D, false, "Return"},
    {"enter", 36, 0x0D, false, "Return"},
    {"tab", 48, 0x09, false, "Tab"},
    {"space", 49, 0x20, false, "space"},
    {"delete", 51, 0x08, false, "BackSpace"},  // the Mac's delete key: backspace
    {"backspace", 51, 0x08, false, "BackSpace"},
    {"escape", 53, 0x1B, false, "Escape"},
    {"esc", 53, 0x1B, false, "Escape"},
    {"f1", 122, 0x70, false, "F1"},
    {"f2", 120, 0x71, false, "F2"},
    {"f3", 99, 0x72, false, "F3"},
    {"f4", 118, 0x73, false, "F4"},
    {"f5", 96, 0x74, false, "F5"},
    {"f6", 97, 0x75, false, "F6"},
    {"f7", 98, 0x76, false, "F7"},
    {"f8", 100, 0x77, false, "F8"},
    {"f9", 101, 0x78, false, "F9"},
    {"f10", 109, 0x79, false, "F10"},
    {"f11", 103, 0x7A, false, "F11"},
    {"f12", 111, 0x7B, false, "F12"},
    {"home", 115, 0x24, true, "Home"},
    {"end", 119, 0x23, true, "End"},
    {"pageup", 116, 0x21, true, "Prior"},
    {"pagedown", 121, 0x22, true, "Next"},
    {"forwarddelete", 117, 0x2E, true, "Delete"},
    {"left", 123, 0x25, true, "Left"},
    {"up", 126, 0x26, true, "Up"},
    {"right", 124, 0x27, true, "Right"},
    {"down", 125, 0x28, true, "Down"},
};

const KeyCode* FindKeyCode(std::string_view name)
{
    for (const auto& k : kKeyCodes)
        if (k.name == name)
            return &k;
    return nullptr;
}

class SystemHost final : public ActionHost
{
public:
    SystemHost(Settings s, SystemHostCallbacks cb, std::atomic<bool>* cancel)
        : _s(std::move(s)), _cb(std::move(cb)), _cancel(cancel)
    {
    }

    HttpResponse Webapi(const std::string& method, const std::string& path, const json& body, bool control) override
    {
        std::lock_guard l(_m);
        if (!_resolved || _emuId.empty())
            Resolve();  // nothing found last time (unreal-ng not up yet): look again
        if (path == "/api/v1/emulator/start" && body.value("replace", false))
            return ReplaceMachine(body);
        if (_emuId.empty())
        {
            HttpResponse r;
            r.error = "no unreal-ng emulator at " + _s.webapi;
            return r;
        }
        auto r = Attempt(method, path, body, control);
        if (r.status == 404 && path.rfind("/api/", 0) != 0)
        {
            // the instance may be gone (model switched from the GUI, recreated): find the current one, retry once
            const std::string was = _emuId;
            Resolve();
            if (!_emuId.empty() && _emuId != was)
                r = Attempt(method, path, body, control);
        }
        return r;
    }

    json TtdState() override
    {
        std::lock_guard l(_m);
        if (!_resolved || _emuId.empty())
            Resolve();
        if (_emuId.empty())
            return nullptr;
        return TtdNow();
    }

    HttpResponse TtdSeekBy(long long frames) override
    {
        std::lock_guard l(_m);
        if (!_resolved || _emuId.empty())
            Resolve();
        if (_emuId.empty())
        {
            HttpResponse r;
            r.error = "no unreal-ng emulator at " + _s.webapi;
            return r;
        }
        const json t = TtdNow();
        if (!t.is_object() || !t.value("history", false))
        {
            HttpResponse r;
            r.error = "no time-travel history: record first";
            return r;
        }
        const long long first = t.value("first", 0LL), last = t.value("last", 0LL);
        const long long pos =
            std::clamp(t.value("pos", last), first, last);  // stopped: the live machine runs past the end
        const long long target = std::clamp(pos + frames, first, last);
        if (target == pos)
        {
            HttpResponse r;
            r.status = 200;
            r.body = json{{"at_edge", true}, {"frame", pos}, {"session_start", first}}.dump();
            return r;
        }
        // tinframe 0: the frame's start (a frame alone means its end, i.e. the next frame's start)
        auto r = Attempt("POST", "/ttd/seek", json{{"frame", target}, {"tinframe", 0}}, true);
        if (r.Ok())
        {  // where the recording starts, for the time code
            try
            {
                auto j = json::parse(r.body);
                j["session_start"] = first;
                j["at_edge"] = target == first || target == last;
                r.body = j.dump();
            }
            catch (...)
            {
            }
        }
        return r;
    }

private:
    // Time travel now: the position from /ttd/position (fast: polled 50 times a second), where the recording starts
    // from /ttd/status (twice a second, or when the state changes). {state, history, first, last, pos, tin}
    json TtdNow()
    {
        auto p = Attempt("GET", "/ttd/position", json(), false);
        if (!p.Ok())
            return nullptr;
        try
        {
            const auto j = json::parse(p.body);
            std::string state = j.value("state", "");
            const auto now = std::chrono::steady_clock::now();
            if (state != _ttdLastState || now - _ttdStatusAt > std::chrono::milliseconds(500))
            {
                if (auto st = Attempt("GET", "/ttd/status", json(), false); st.Ok())
                {
                    const auto t = json::parse(st.body);
                    _ttdFirst = t.contains("earliest") && t["earliest"].is_object()
                                    ? t["earliest"].value("frame", 0LL)
                                    : t.value("session_start_frame", 0LL);
                    _ttdCheckpoints = t.value("checkpoint_count", 0LL);
                }
                _ttdStatusAt = now;
                _ttdLastState = state;
            }
            const long long last =
                j.contains("session_end") && j["session_end"].is_object() ? j["session_end"].value("frame", 0LL) : 0;
            const long long pos =
                j.contains("current") && j["current"].is_object() ? j["current"].value("frame", last) : last;
            const long long tin =
                j.contains("current") && j["current"].is_object() ? j["current"].value("tinframe", 0LL) : 0;
            const bool history = _ttdCheckpoints > 0 || state == "recording" || state == "detached";
            if (state == "idle" && history)
                state = "stopped";  // stopped, the history kept
            return json{{"state", state}, {"history", history}, {"first", _ttdFirst},
                        {"last", last},   {"pos", pos},         {"tin", tin}};
        }
        catch (...)
        {
            return nullptr;
        }
    }

private:
    // A new machine, no frills: remove the current instance, create and start the requested one in its place (same
    // symbolic id, selected, so the desktop front end shows it), follow its id. body = POST /emulator/start +
    // "replace".
    HttpResponse ReplaceMachine(json body)
    {
        body.erase("replace");
        std::vector<std::pair<std::string, std::string>> hdr;
        if (!_token.empty())
            hdr.emplace_back("Authorization", "Bearer " + _token);
        std::string symbolic;
        json current;
        if (!_emuId.empty())
        {
            if (auto cur = HttpRequest(_s.webapi, "GET", "/api/v1/emulator/" + _emuId, {}, hdr); cur.Ok())
            {
                try
                {
                    current = json::parse(cur.body);
                    symbolic = current.value("symbolic_id", "");
                }
                catch (...)
                {
                }
            }
            if (body.value("model", "") == "$model")
            {  // the same machine again (cold restart): model and RAM size
                if (!current.contains("model"))
                {
                    HttpResponse e;
                    e.error = "cannot read the current model";
                    return e;
                }
                body["model"] = current["model"];
                if (current.contains("ram_kb") && !body.contains("ram_size"))
                    body["ram_size"] = current["ram_kb"];
            }
            auto del = HttpRequest(_s.webapi, "DELETE", "/api/v1/emulator/" + _emuId, {}, hdr);
            if (!del.Ok() && del.status != 404)
                return del;
            _emuId.clear();
        }
        if (!symbolic.empty() && !body.contains("symbolic_id"))
            body["symbolic_id"] = symbolic;
        if (!body.contains("select"))
            body["select"] = true;
        auto r = HttpRequest(_s.webapi, "POST", "/api/v1/emulator/start", body.dump(), hdr);
        if (r.Ok())
        {
            try
            {
                _emuId = json::parse(r.body).value("id", "");
            }
            catch (...)
            {
            }
        }
        _resolved = !_emuId.empty();
        return r;
    }

    HttpResponse Attempt(const std::string& method, const std::string& path, const json& body, bool control)
    {
        const std::string base = "/api/v1/emulator/" + _emuId;
        std::vector<std::pair<std::string, std::string>> hdr;
        if (!_token.empty())
        {
            hdr.emplace_back("Authorization", "Bearer " + _token);
            control = false;
        }
        std::string session;
        if (control)
        {
            auto sr = HttpRequest(_s.webapi, "POST", base + "/control/session",
                                  json{{"client", "streamdeck"}, {"surface", "webapi"}, {"lease_seconds", 5}}.dump());
            if (!sr.Ok())
                return sr;
            try
            {
                session = json::parse(sr.body).value("session", "");
            }
            catch (...)
            {
            }
            if (session.empty())
                session = sr.Header("X-Unreal-Session");
            if (!session.empty())
                hdr.emplace_back("X-Unreal-Session", session);
        }
        // "/api/..." = a route outside the instance (scripting, videowall); anything else is relative to the instance
        const std::string url = path.rfind("/api/", 0) == 0 ? path : base + path;
        // "$emulator" = this instance's id, "$model" = its model (restart the same machine, lock the videowall to it)
        std::string text = body.is_null() ? std::string() : body.dump();
        if (text.find("\"$emulator\"") != std::string::npos)
            text = ReplaceAll(text, "\"$emulator\"", json(_emuId).dump());
        if (text.find("\"$model\"") != std::string::npos)
        {
            std::string model;
            if (auto mr = HttpRequest(_s.webapi, "GET", base, {}, hdr); mr.Ok())
            {
                try
                {
                    model = json::parse(mr.body).value("model", "");
                }
                catch (...)
                {
                }
            }
            if (model.empty())
            {
                HttpResponse e;
                e.error = "cannot read the current model";
                return e;
            }
            text = ReplaceAll(text, "\"$model\"", json(model).dump());
        }
        auto r = HttpRequest(_s.webapi, method, url, text, hdr);
        if (control)
            HttpRequest(_s.webapi, "DELETE", base + "/control/session", {}, hdr);
        if (path == "/ttd/start" || path == "/ttd/resume" || path == "/ttd/invalidate" || path == "/ttd/stop")
            _ttdFrame = -1;
        if (r.Ok())
        {
            try
            {  // where time travel stands now (seek, step-back / -forward answer it)
                const auto j = json::parse(r.body);
                if (j.contains("arrived_at") && j["arrived_at"].is_object())
                    _ttdFrame = j["arrived_at"].value("frame", _ttdFrame);
            }
            catch (...)
            {
            }
        }
        if (r.Ok())
        {
            // a model switch replaces the instance: follow it. POST /model answers new_emulator_id; a snapshot or RZX
            // of another model answers emulator_id with model_switched = true
            try
            {
                const auto j = json::parse(r.body);
                std::string next = j.value("new_emulator_id", "");
                if (next.empty() && j.value("model_switched", false))
                    next = j.value("emulator_id", "");
                if (!next.empty())
                    _emuId = next;
            }
            catch (...)
            {
            }
        }
        return r;
    }

public:
    HttpResponse Http(const std::string& url, const std::string& method, const json& headers,
                      const std::string& body) override
    {
        std::string base, path;
        SplitUrl(url, base, path);
        std::vector<std::pair<std::string, std::string>> hdr;
        for (const auto& [k, v] : headers.items())
            hdr.emplace_back(k, v.is_string() ? v.get<std::string>() : v.dump());
        return HttpRequest(base, method, path, body, hdr);
    }

    int Shell(const std::string& command, int timeoutMs, std::string& output) override
    {
#ifndef _WIN32
        return RunProcess({"/bin/sh", "-c", command}, timeoutMs, output);
#else
        return RunCommand(command, timeoutMs, output);
#endif
    }

    bool Open(const std::string& target) override
    {
#ifdef __APPLE__
        std::string out;
        return RunProcess({"open", target}, 5000, out) == 0;
#elif defined(_WIN32)
        std::string out;
        return RunCommand("start \"\" \"" + target + "\"", -1, out) == 0;
#else
        std::string out;
        return RunProcess({"xdg-open", target}, -1, out) == 0;
#endif
    }

    bool Keys(const std::string& combo, const std::string& text, std::string& error) override
    {
        std::optional<KeyCombo> c;
        const KeyCode* key = nullptr;
        if (!combo.empty())
        {
            c = ParseKeyCombo(combo, &error);
            if (!c)
                return false;
            key = FindKeyCode(c->key);
        }
#ifdef __APPLE__
        CGEventSourceRef src = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
        auto post = [&](CGKeyCode code, bool down, CGEventFlags flags) {
            CGEventRef e = CGEventCreateKeyboardEvent(src, code, down);
            CGEventSetFlags(e, flags);
            CGEventPost(kCGHIDEventTap, e);
            CFRelease(e);
        };
        if (key)
        {
            CGEventFlags flags = 0;
            if (c->cmd)
                flags |= kCGEventFlagMaskCommand;
            if (c->ctrl)
                flags |= kCGEventFlagMaskControl;
            if (c->alt)
                flags |= kCGEventFlagMaskAlternate;
            if (c->shift)
                flags |= kCGEventFlagMaskShift;
            if (c->fn)
                flags |= kCGEventFlagMaskSecondaryFn;
            post(key->mac, true, flags);
            post(key->mac, false, flags);
        }
        for (char32_t ch : text)
        {  // ASCII text; one unicode event per character
            UniChar u = static_cast<UniChar>(ch);
            CGEventRef e = CGEventCreateKeyboardEvent(src, 0, true);
            CGEventKeyboardSetUnicodeString(e, 1, &u);
            CGEventPost(kCGHIDEventTap, e);
            CFRelease(e);
            e = CGEventCreateKeyboardEvent(src, 0, false);
            CGEventKeyboardSetUnicodeString(e, 1, &u);
            CGEventPost(kCGHIDEventTap, e);
            CFRelease(e);
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        if (src)
            CFRelease(src);
        return true;
#elif defined(_WIN32)
        // SendInput: the modifiers down, the key down and up, the modifiers up (reverse order); text as unicode
        // key events (KEYEVENTF_UNICODE, UTF-16 units). Windows refuses input to a window of a higher integrity
        // level (an elevated program) without telling which: SendInput then inserts fewer events.
        std::vector<INPUT> in;
        auto vk = [&](WORD code, bool up, bool extended) {
            INPUT i{};
            i.type = INPUT_KEYBOARD;
            i.ki.wVk = code;
            i.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0u) | (extended ? KEYEVENTF_EXTENDEDKEY : 0u);
            in.push_back(i);
        };
        if (key)
        {
            std::vector<std::pair<WORD, bool>> mods;  // fn: no such key on Windows
            if (c->ctrl)
                mods.emplace_back(static_cast<WORD>(VK_CONTROL), false);
            if (c->alt)
                mods.emplace_back(static_cast<WORD>(VK_MENU), false);
            if (c->shift)
                mods.emplace_back(static_cast<WORD>(VK_SHIFT), false);
            if (c->cmd)
                mods.emplace_back(static_cast<WORD>(VK_LWIN), true);
            for (const auto& [m, ext] : mods)
                vk(m, false, ext);
            vk(key->win, false, key->extended);
            vk(key->win, true, key->extended);
            for (auto it = mods.rbegin(); it != mods.rend(); ++it)
                vk(it->first, true, it->second);
        }
        if (!text.empty())
        {
            const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring w(static_cast<size_t>(n > 0 ? n : 0), L'\0');
            if (n > 0)
                MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), w.data(), n);
            else
            {
                error = "text is not UTF-8";
                return false;
            }
            for (wchar_t ch : w)
                for (bool up : {false, true})
                {
                    INPUT i{};
                    i.type = INPUT_KEYBOARD;
                    i.ki.wScan = static_cast<WORD>(ch);
                    i.ki.dwFlags = KEYEVENTF_UNICODE | (up ? KEYEVENTF_KEYUP : 0u);
                    in.push_back(i);
                }
        }
        if (in.empty())
            return true;
        const UINT sent = SendInput(static_cast<UINT>(in.size()), in.data(), sizeof(INPUT));
        if (sent != in.size())
        {
            error = sdb::Format(
                "SendInput took {} of {} events (error {}; an elevated window does not take input from "
                "a normal one)",
                sent, in.size(), static_cast<unsigned long>(GetLastError()));
            return false;
        }
        return true;
#else
        // xdotool (X11; Wayland sessions do not take synthetic input from it): the combo in X keysym names
        std::string out;
        auto run = [&](const std::vector<std::string>& argv, int timeoutMs) {
            const int code = RunProcess(argv, timeoutMs, out);
            if (code == 0)
                return true;
            error = code == 127 ? "xdotool not found (install xdotool; X11 only)"
                                : sdb::Format("xdotool exit {}{}", code, out.empty() ? "" : ": " + out);
            return false;
        };
        if (key)
        {
            std::string x;
            if (c->ctrl)
                x += "ctrl+";
            if (c->alt)
                x += "alt+";
            if (c->shift)
                x += "shift+";
            if (c->cmd)
                x += "super+";
            x += key->x11;  // fn: no such key on X11
            if (!run({"xdotool", "key", "--", x}, 3000))
                return false;
        }
        if (!text.empty() && !run({"xdotool", "type", "--", text}, 10000))
            return false;
        return true;
#endif
    }

    void Navigate(const json& r) override
    {
        if (_cb.navigate)
            _cb.navigate(r);
    }
    void Brightness(const json& r) override
    {
        if (_cb.brightness)
            _cb.brightness(r);
    }
    void SleepMs(int ms) override
    {
        for (int left = ms; left > 0 && !Cancelled(); left -= 20)
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(left, 20)));
    }
    void Note(const std::string& line) override
    {
        if (_cb.note)
            _cb.note(line);
    }
    [[nodiscard]] bool Cancelled() const override
    {
        return _cancel && _cancel->load();
    }

private:
    void Resolve()
    {
        _resolved = true;
        _emuId.clear();
        if (std::ifstream in(CompanionFile(_s)); in)
        {
            try
            {
                _token = json::parse(in).value("token", "");
            }
            catch (...)
            {
            }
        }
        auto r = HttpRequest(_s.webapi, "GET", "/api/v1/emulator");
        if (!r.Ok())
            return;
        try
        {
            std::string first;
            for (auto& e : json::parse(r.body).value("emulators", json::array()))
            {
                const std::string id = e.value("id", ""), sym = e.value("symbolic_id", "");
                if (first.empty())
                    first = id;
                if (_s.emulator != "auto" && (id == _s.emulator || sym == _s.emulator))
                {
                    _emuId = id;
                    return;
                }
                if (_s.emulator == "auto" && sym == "unreal-qt")
                {
                    _emuId = id;
                    return;
                }
            }
            if (_s.emulator == "auto")
                _emuId = first;
        }
        catch (...)
        {
        }
    }

    Settings _s;
    SystemHostCallbacks _cb;
    std::atomic<bool>* _cancel;
    std::mutex _m;
    bool _resolved = false;
    std::string _emuId, _token;
    long long _ttdFrame = -1;  // time travel: the replay position (from the last answer), -1 = unknown / live
    long long _ttdFirst = 0, _ttdCheckpoints = 0;  // where the recording starts, how much it holds (from /ttd/status)
    std::string _ttdLastState;
    std::chrono::steady_clock::time_point _ttdStatusAt{};
};

}  // namespace

std::optional<KeyCombo> ParseKeyCombo(const std::string& combo, std::string* error)
{
    auto fail = [&](std::string why) -> std::optional<KeyCombo> {
        if (error)
            *error = std::move(why);
        return std::nullopt;
    };
    KeyCombo c;
    size_t start = 0;
    while (start <= combo.size())
    {
        size_t plus = combo.find('+', start);
        if (plus == start)
            plus = combo.find('+', start + 1);  // "cmd++": a part "+" (no such key: an error below)
        std::string part = combo.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
        for (auto& ch : part)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (part == "cmd" || part == "command" || part == "meta")
            c.cmd = true;
        else if (part == "ctrl" || part == "control")
            c.ctrl = true;
        else if (part == "alt" || part == "option" || part == "opt")
            c.alt = true;
        else if (part == "shift")
            c.shift = true;
        else if (part == "fn")
            c.fn = true;
        else if (!FindKeyCode(part))
            return fail("unknown key '" + part + "' in '" + combo + "'");
        else if (!c.key.empty())
            return fail("two keys in '" + combo + "' ('" + c.key + "', '" + part + "')");
        else
            c.key = part;
        if (plus == std::string::npos)
            break;
        start = plus + 1;
    }
    if (c.key.empty())
        return fail("no key in '" + combo + "'");
    return c;
}

std::span<const std::string_view> KeyNames()
{
    static const auto names = [] {
        std::vector<std::string_view> v;
        for (const auto& k : kKeyCodes)
            v.push_back(k.name);
        return v;
    }();
    return names;
}

std::unique_ptr<ActionHost> MakeSystemHost(const Settings& s, SystemHostCallbacks cb, std::atomic<bool>* cancel)
{
    return std::make_unique<SystemHost>(s, std::move(cb), cancel);
}

}  // namespace sdb::scn
