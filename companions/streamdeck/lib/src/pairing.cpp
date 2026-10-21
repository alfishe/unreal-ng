#include "sdb/pairing.h"

#include <chrono>
#include <filesystem>
#include <fstream>

#include "sdb/http.h"
#include "sdb/surfaceclient.h"

namespace sdb::pairing
{

namespace fs = std::filesystem;

bool WriteCompanion(const std::string& file, const std::string& webapi, const json& approved, std::string* error)
{
    std::error_code ec;
    if (const fs::path parent = fs::path(file).parent_path(); !parent.empty())
        fs::create_directories(parent, ec);
    const json store = {{"webapi", webapi},
                        {"token", approved.value("token", "")},
                        {"companion_id", approved.value("companion_id", "")},
                        {"scopes", approved.value("scopes", json::array())}};
    // written beside it, then moved over it: the surface client never reads half a file
    const std::string temp = file + ".new";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            if (error)
                *error = "cannot write " + temp;
            return false;
        }
        out << store.dump(2);
    }
    fs::permissions(temp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    fs::rename(temp, file, ec);
    if (ec)
    {
        if (error)
            *error = "cannot write " + file + ": " + ec.message();
        return false;
    }
    return true;
}

Pairer::Pairer(Options options) : _options(std::move(options))
{
}

Pairer::~Pairer()
{
    {
        std::lock_guard l(_mutex);
        _quit = true;
        _cancel = true;
    }
    _wake.notify_all();
    if (_thread.joinable())
        _thread.join();
}

json Pairer::StatusLocked() const
{
    json s = _state;
    const auto c = surface::LoadCompanion(_options.file);
    s["paired"] = c && !c->token.empty();
    s["companion_id"] = c ? c->id : std::string();
    s["file"] = _options.file;
    s["host"] = _options.webapi;
    return s;
}

json Pairer::Status() const
{
    std::lock_guard l(_mutex);
    return StatusLocked();
}

bool Pairer::Busy() const
{
    std::lock_guard l(_mutex);
    return _busy;
}

json Pairer::Start()
{
    std::lock_guard l(_mutex);
    // the last pairing's thread may still be in its onChange: then this is a pairing that has not ended yet (a
    // caller that holds what onChange takes must never wait for it here)
    if (_busy || _quit || !_threadDone)
        return StatusLocked();
    if (_thread.joinable())
        _thread.join();  // it has returned from Run: at once
    _busy = true;
    _threadDone = false;
    _cancel = false;
    _state = json{{"state", "requesting"}};
    _thread = std::thread([this] {
        Run();
        std::lock_guard done(_mutex);
        _threadDone = true;
    });  // its first Set says "requesting" to onChange, in order
    return StatusLocked();
}

void Pairer::Cancel()
{
    {
        std::lock_guard l(_mutex);
        _cancel = true;
    }
    _wake.notify_all();
}

void Pairer::Set(const std::string& state, const json& extra)
{
    json now;
    {
        std::lock_guard l(_mutex);
        _state = extra;
        _state["state"] = state;
        if (state != "requesting" && state != "pending")
            _busy = false;
        now = StatusLocked();
    }
    if (onChange)
        onChange(now);
}

void Pairer::Run()
{
    Set("requesting");
    const json request = {{"name", _options.name}, {"device", _options.device}};
    const auto r = HttpRequest(_options.webapi, "POST", "/api/v1/companions/pairings", request.dump(), {},
                               _options.timeoutMs);
    json j = json::parse(r.body, nullptr, false);
    if (!j.is_object())
        j = json::object();
    if (r.status == 0)
    {
        Set("unreachable", {{"error", "unreachable"}, {"message", _options.webapi + ": " + r.error}});
        return;
    }
    if (r.status != 202)
    {
        // 503 pairing_unavailable (no GUI: the video wall, headless), 403 pairing_disabled, 429 too_many_pairings
        Set("refused", {{"error", j.value("error", "HTTP " + std::to_string(r.status))},
                        {"message", j.value("message", "")},
                        {"status", r.status}});
        return;
    }
    const std::string id = j.value("pairing_id", "");
    const std::string code = j.value("code", "");
    Set("pending", {{"pairing_id", id}, {"code", code}, {"expires_in", j.value("expires_in", 120)}});
    for (;;)
    {
        {
            std::unique_lock l(_mutex);
            _wake.wait_for(l, std::chrono::milliseconds(_options.pollMs), [this] { return _cancel; });
            if (_cancel)
            {
                l.unlock();
                (void)HttpRequest(_options.webapi, "DELETE", "/api/v1/companions/pairings/" + id, {}, {},
                                  _options.timeoutMs);
                Set("idle", {{"message", "cancelled"}});
                return;
            }
        }
        const auto p = HttpRequest(_options.webapi, "GET", "/api/v1/companions/pairings/" + id, {}, {},
                                   _options.timeoutMs);
        json pj = json::parse(p.body, nullptr, false);
        if (!pj.is_object())
            pj = json::object();
        const std::string state = pj.value("state", p.status == 404 ? "expired" : "");
        if (state == "approved")
        {
            std::string error;
            if (!WriteCompanion(_options.file, _options.webapi, pj, &error))
            {
                Set("refused", {{"error", "store"}, {"message", error}});
                return;
            }
            Set("paired", {{"scopes", pj.value("scopes", json::array())}});
            return;
        }
        if (state == "denied" || state == "expired")
        {
            Set(state);
            return;
        }
        if (state == "pending")
        {
            std::lock_guard l(_mutex);
            _state["expires_in"] = pj.value("expires_in", 0);
        }
        // anything else (the host restarting): ask again
    }
}

std::string NoticeOf(const json& status, bool paired, bool revoked)
{
    const std::string state = status.value("state", "idle");
    if (state == "requesting")
        return "PAIRING - ASKING UNREALNG";
    if (state == "pending")
    {
        const std::string code = status.value("code", "");
        return "PAIR CODE " + (code.size() == 6 ? code.substr(0, 3) + " " + code.substr(3) : code) +
               " - APPROVE IN UNREALNG";
    }
    if (state == "denied")
        return "PAIRING DENIED - TOUCH TO TRY AGAIN";
    if (state == "expired")
        return "PAIRING EXPIRED - TOUCH TO TRY AGAIN";
    if (state == "unreachable")
        return "UNREALNG NOT RUNNING - TOUCH TO TRY AGAIN";
    if (state == "refused")
    {
        const std::string error = status.value("error", "");
        if (error == "pairing_unavailable")
            return "PAIRING NEEDS THE UNREALNG WINDOW";
        if (error == "pairing_disabled")
            return "PAIRING IS OFF IN UNREALNG";
        return "PAIRING REFUSED - TOUCH TO TRY AGAIN";
    }
    if (revoked)
        return surface::kRevokedNotice;
    if (!paired)
        return surface::kNotPairedNotice;
    return {};
}

void AddPairRoutes(automation::Api& api, Pairer& pairer)
{
    using automation::Reply;
    using automation::Request;
    api.Add({"GET",
             "/api/v1/pair",
             "The pairing with unreal-ng",
             "state: idle | requesting | pending (code: the 6 digits unreal-qt's prompt shows too, expires_in) | "
             "paired | denied | expired | refused (error) | unreachable; paired: a token in the companion file; "
             "companion_id",
             {"daemon", "pairing", "companion"},
             {},
             nullptr,
             [&pairer](const Request&) { return Reply::Ok(pairer.Status()); }});
    api.Add({"POST",
             "/api/v1/pair",
             "Pair with unreal-ng",
             "Asks the host for a pairing (one for every deck); the user approves it at unreal-qt (the prompt shows the "
             "same code); the token goes to the companion file and the surface client connects. A pending pairing: "
             "its state again",
             {"daemon", "pairing", "companion"},
             {},
             nullptr,
             [&pairer](const Request&) { return Reply::Ok(pairer.Start()); }});
    api.Add({"DELETE",
             "/api/v1/pair",
             "Cancel a pending pairing",
             "",
             {"daemon", "pairing", "companion"},
             {},
             nullptr,
             [&pairer](const Request&) {
                 pairer.Cancel();
                 return Reply::Ok(pairer.Status());
             }});
}

}  // namespace sdb::pairing
