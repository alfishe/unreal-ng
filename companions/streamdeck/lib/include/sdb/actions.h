// Executing scenario actions. Side effects go through ActionHost (system host in the runtime, a fake in tests).
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "sdb/http.h"
#include "sdb/scenario.h"

namespace sdb::scn
{

struct ActionResult
{
    bool ok = true;
    std::string message;
    std::string status{};  // a live readout for the strip's status slot (the time-travel position): stays in place
};

class ActionHost
{
public:
    virtual ~ActionHost() = default;
    // unreal-ng: `path` relative to /api/v1/emulator/{id}; control = open a run-control session (no companion token)
    virtual HttpResponse Webapi(const std::string& method, const std::string& path, const json& body, bool control) = 0;
    virtual HttpResponse Http(const std::string& url, const std::string& method, const json& headers,
                              const std::string& body) = 0;
    // unreal-ng time travel: move the replay position by `frames` (- back, + forward) within the recorded history.
    virtual HttpResponse TtdSeekBy(long long frames)
    {
        (void)frames;
        HttpResponse r;
        r.error = "time travel is not available here";
        return r;
    }
    // unreal-ng time travel now: {state, first, last, pos} (frames), null when unknown / unavailable.
    virtual json TtdState()
    {
        return nullptr;
    }
    virtual int Shell(const std::string& command, int timeoutMs,
                      std::string& output) = 0;  // exit code, -1 = could not run / timeout
    virtual bool Open(const std::string& target) = 0;
    virtual bool Keys(const std::string& combo, const std::string& text) = 0;
    virtual void Navigate(const json& request) = 0;    // {to: back|home|node|scenario, node, file}
    virtual void Brightness(const json& request) = 0;  // {value} or {delta}
    virtual void SleepMs(int ms) = 0;
    virtual void Note(const std::string& line) = 0;
    [[nodiscard]] virtual bool Cancelled() const
    {
        return false;
    }
};

// `vars` fills "$name" placeholders in the action (e.g. "$ticks" for a dial turn; "rate" = its detents per second).
// `toggleState` is the state of a toggle node (flipped on success); nested toggles without state run "on".
ActionResult Execute(const Action& a, ActionHost& host, const json& vars = json::object(), bool* toggleState = nullptr,
                     int depth = 0);

// Replaces "$name" strings (whole value -> the variable's JSON value, inside text -> its text).
[[nodiscard]] json Substitute(const json& j, const json& vars);

// The host the runtime uses: unreal-ng WebAPI (emulator auto-detected, companion token when paired), HTTP,
// /bin/sh (POSIX) or cmd (Windows), the system "open" handler, keystrokes (macOS CGEvent / Linux xdotool).
// navigate / brightness / note are forwarded to the callbacks.
struct SystemHostCallbacks
{
    std::function<void(const json&)> navigate, brightness;
    std::function<void(const std::string&)> note;
};
/// The companion token store when the scenario names none (mode 0600)
[[nodiscard]] std::string DefaultCompanionFile();

[[nodiscard]] std::unique_ptr<ActionHost> MakeSystemHost(const Settings& s, SystemHostCallbacks cb,
                                                         std::atomic<bool>* cancel = nullptr);

}  // namespace sdb::scn
