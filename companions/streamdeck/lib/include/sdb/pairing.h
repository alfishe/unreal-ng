// Pairing the daemon with unreal-ng as a companion (docs/inprogress/2026-10-19-streamdeck-integration/daemon.md §6;
// the companion protocol: docs/inprogress/2026-10-16-companion-client/protocol.md §3). The daemon pairs once for every
// deck (`device: "Stream Deck daemon"`); the user approves at unreal-qt's GUI (no automation plane can approve); the
// token goes to the companion file (mode 0600), which the surface client watches.
//
// Started from the deck (a tap on the strip while it says NOT PAIRED / REVOKED), from Stream Deck Studio and from
// unreal-qt's settings page (both through POST /api/v1/pair), or by `unreal-streamdeck pair`.
//
//   GET    /api/v1/pair    the state: idle | requesting | pending (code, expires_in) | paired | denied | expired |
//                          refused (error: pairing_unavailable, pairing_disabled, too_many_pairings, ...) |
//                          unreachable; paired, companion_id (the companion file's)
//   POST   /api/v1/pair    ask the host for a pairing (a pending one: its state again)
//   DELETE /api/v1/pair    cancel a pending one
#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#include "sdb/automation.h"

namespace sdb::pairing
{

using json = nlohmann::json;

struct Options
{
    std::string webapi = "http://127.0.0.1:8090";  // the host
    std::string file;                              // the companion file the token goes to
    std::string name = "Stream Deck daemon";
    std::string device = "Stream Deck daemon (unreal-streamdeck)";
    int pollMs = 400;      // how often a pending pairing is asked about
    int timeoutMs = 2000;  // each HTTP call
};

// Writes the companion file (mode 0600): {webapi, token, companion_id, scopes}
bool WriteCompanion(const std::string& file, const std::string& webapi, const json& approved,
                    std::string* error = nullptr);

class Pairer
{
public:
    explicit Pairer(Options options);
    ~Pairer();  // cancels a pending pairing, joins its thread
    Pairer(const Pairer&) = delete;
    Pairer& operator=(const Pairer&) = delete;

    // Asks the host for a pairing on a thread of its own (it lasts as long as the pairing). One at a time: while one
    // is requested or pending, its state again. Returns the state
    json Start();
    // A pending pairing is cancelled at the host
    void Cancel();
    // {state, code, pairing_id, expires_in, error, message, paired, companion_id, file, host}; thread-safe
    [[nodiscard]] json Status() const;
    [[nodiscard]] bool Busy() const;

    // Every change of the state, on the pairer's thread (no lock held)
    std::function<void(const json& status)> onChange;

private:
    void Run();
    void Set(const std::string& state, const json& extra = json::object());
    [[nodiscard]] json StatusLocked() const;

    Options _options;
    mutable std::mutex _mutex;
    std::condition_variable _wake;
    json _state = json{{"state", "idle"}};
    bool _busy = false, _cancel = false, _quit = false;
    bool _threadDone = true;  // the thread has returned from Run and its last onChange
    std::thread _thread;
};

// The strip's line for the pairing state and the surface link ("" = nothing to say):
//   requesting / pending -> "PAIR CODE 418 205 - APPROVE IN UNREALNG"
//   denied / expired / refused / unreachable -> why, and "TOUCH TO TRY AGAIN"
//   not paired -> "NOT PAIRED - TOUCH TO PAIR"; revoked -> "COMPANION REVOKED - TOUCH TO PAIR"
[[nodiscard]] std::string NoticeOf(const json& status, bool paired, bool revoked);

void AddPairRoutes(automation::Api& api, Pairer& pairer);

}  // namespace sdb::pairing
