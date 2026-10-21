// Executing scenario actions. Side effects go through ActionHost (system host in the runtime, a fake in tests).
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "sdb/actionexec.h"
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
    // The `action` kind: a call of the host's action registry (sdb/actionexec.h)
    virtual ActionResult RunAction(const ActionCall& call);
    // A readout a state-bound face shows (scenarios.md §4), polled while no surface topic delivers it; null =
    // unknown here. The system host answers run.state.paused.
    virtual json Readout(const std::string& path)
    {
        (void)path;
        return nullptr;
    }
    virtual int Shell(const std::string& command, int timeoutMs,
                      std::string& output) = 0;  // exit code, -1 = could not run / timeout
    virtual bool Open(const std::string& target) = 0;
    // a key combination (ParseKeyCombo) and / or text typed; false with `error` saying why
    virtual bool Keys(const std::string& combo, const std::string& text, std::string& error) = 0;
    virtual void Navigate(const json& request) = 0;    // {to: back|home|node|scenario, node, file}
    virtual void Brightness(const json& request) = 0;  // {value} or {delta}
    virtual void SleepMs(int ms) = 0;
    virtual void Note(const std::string& line) = 0;
    [[nodiscard]] virtual bool Cancelled() const
    {
        return false;
    }
};

// A "keys" combo: modifiers and one key joined by '+', any case ("cmd+shift+4", "ctrl+return"). Modifiers: cmd
// (command, meta: Command on macOS, the Windows key, Super on Linux), ctrl (control), alt (option, opt), shift, fn
// (macOS only; ignored elsewhere). Keys: KeyNames(), the same on every platform (US layout positions).
struct KeyCombo
{
    bool cmd = false, ctrl = false, alt = false, shift = false, fn = false;
    std::string key;  // one of KeyNames()
};
// nullopt with `error` for an unknown key name, two keys, or no key
[[nodiscard]] std::optional<KeyCombo> ParseKeyCombo(const std::string& combo, std::string* error = nullptr);
// a-z 0-9 = - [ ] ' ; \ , / . ` return enter tab space delete backspace escape esc f1-f12 home end pageup pagedown
// forwarddelete left right up down
[[nodiscard]] std::span<const std::string_view> KeyNames();

// `vars` fills "$name" placeholders in the action (e.g. "$ticks" for a dial turn; "rate" = its detents per second).
// Reserved: "phase" (+1 the press, -1 the release of an action step with "hold"), "__dir" (the scenario's folder:
// "$scenario/..." and the other path roots in an action's params are resolved against it before the call).
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
    // runs the `action` kind; null = the REST fallback (POST /api/v1/controllers/actions/{id} on settings.webapi)
    std::shared_ptr<ActionExecutor> executor{};
};
/// The companion token store when the scenario names none (mode 0600)
[[nodiscard]] std::string DefaultCompanionFile();

[[nodiscard]] std::unique_ptr<ActionHost> MakeSystemHost(const Settings& s, SystemHostCallbacks cb,
                                                         std::atomic<bool>* cancel = nullptr);

}  // namespace sdb::scn
