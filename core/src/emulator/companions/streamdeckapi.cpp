#include "stdafx.h"

#include "streamdeckapi.h"

#include <cctype>
#include <cstdlib>

#include "emulator/companions/streamdecksupervisor.h"
#include "emulator/execution/companionregistry.h"
#include "emulator/state/statenodejson.h"

namespace companion
{
namespace streamdeck
{

namespace
{
Reply Error(int status, const std::string& error, const std::string& message)
{
    Reply r;
    r.status = status;
    r.body["ok"] = false;
    r.body["error"] = error;
    r.body["message"] = message;
    return r;
}

Reply Ok(StateNode body = StateNode::Object())
{
    Reply r;
    r.body = StateNode::Object();
    r.body["ok"] = true;
    for (auto& [key, value] : body.members)
        r.body[key] = std::move(value);
    return r;
}

std::string Str(const StateNode* node)
{
    if (!node)
        return {};
    if (node->kind == StateNode::Kind::String)
        return node->s;
    if (node->kind == StateNode::Kind::Int)
        return std::to_string(node->i);
    return {};
}

StateNode SettingsNode(const Settings& s)
{
    StateNode n = StateNode::Object();
    n["mode"] = ModeName(s.mode);
    n["activity"] = s.activity;
    n["api_port"] = s.apiPort;
    n["viewers_port"] = s.viewersPort;
    n["binary"] = s.binary;
    StateNode brightness = StateNode::Object();
    for (const auto& [serial, value] : s.brightness)
        brightness[serial] = value;
    n["brightness"] = brightness;
    StateNode pack = StateNode::Object();
    for (const auto& [serial, value] : s.pack)
        pack[serial] = value;
    n["pack"] = pack;
    return n;
}

/// The daemon's answer as a reply: its body under `key`, its error as 502 (or 409 when nothing runs)
Reply Forwarded(const HttpResult& r, const char* key)
{
    if (r.status == 0)
        return Error(409, "not_running", r.error.empty() ? "the Stream Deck daemon does not answer" : r.error);
    StateNode body;
    if (!StateNodeFromJsonText(r.body, body))
        body = StateNode(r.body);
    if (!r.Ok())
    {
        Reply e = Error(r.status >= 500 ? 502 : r.status, "daemon_refused",
                        body.find("error") ? Str(body.find("error")) : "HTTP " + std::to_string(r.status));
        e.body["daemon"] = body;
        return e;
    }
    StateNode out = StateNode::Object();
    out[key] = body;
    return Ok(out);
}

/// %-encoding of a path segment (a deck serial)
std::string Segment(const std::string& text)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out.push_back(static_cast<char>(c));
        else
        {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}
} // namespace

const std::vector<std::string>& Verbs()
{
    static const std::vector<std::string> verbs = {"status",  "start",       "stop",   "restart",    "settings",
                                                   "pairing", "pair",        "pair_cancel", "revoke", "brightness",
                                                   "packs",   "pack",        "udev"};
    return verbs;
}

Reply Call(std::string_view verbView, const StateNode& args)
{
    const std::string verb(verbView);
    Supervisor& sup = Supervisor::Instance();
    if (verb == "udev")
    {
        StateNode body = StateNode::Object();
        body["file"] = "70-unreal-streamdeck.rules";
        body["rule"] = UdevRuleText();
#ifdef __linux__
        body["needed"] = true;
#else
        body["needed"] = false;
#endif
        return Ok(body);
    }
    if (verb == "status")
    {
        StateNode body = StateNode::Object();
        body["status"] = sup.Status();
        return Ok(body);
    }
    bool known = false;
    for (const std::string& v : Verbs())
        known = known || v == verb;
    if (!known)
        return Error(400, "Bad Request", "unknown verb '" + verb + "' (status, start, stop, restart, settings, "
                                         "pairing, pair, pair_cancel, revoke, brightness, packs, pack, udev)");
    if (!sup.Attached())
        return Error(503, "supervisor_unavailable",
                     "the Stream Deck daemon is supervised by unreal-qt; this host has no supervisor (headless, the "
                     "video wall): start unreal-streamdeck by hand");

    if (verb == "start" || verb == "stop" || verb == "restart")
    {
        if (verb == "start")
            sup.Start();
        else if (verb == "stop")
            sup.Stop();
        else
            sup.Restart();
        StateNode body = StateNode::Object();
        body["requested"] = verb;
        body["status"] = sup.Status();
        return Ok(body);
    }
    if (verb == "settings")
    {
        StateNode changes = StateNode::Object();
        for (const auto& [key, value] : args.members)
            if (key == "mode" || key == "activity" || key == "api_port" || key == "viewers_port" || key == "binary")
                changes[key] = value;
            else
                return Error(400, "Bad Request",
                             "settings: mode (off, deck, always), activity (off, flash, line), api_port, viewers_port, "
                             "binary; not '" + key + "'");
        std::string error;
        if (!changes.members.empty() && !sup.Update(changes, error))
            return Error(400, "Bad Request", error);
        StateNode body = StateNode::Object();
        body["settings"] = SettingsNode(sup.GetSettings());
        return Ok(body);
    }
    if (verb == "pairing")
        return Forwarded(sup.Daemon("GET", "/api/v1/pair"), "pairing");
    if (verb == "pair")
        return Forwarded(sup.Daemon("POST", "/api/v1/pair", "{}"), "pairing");
    if (verb == "pair_cancel")
        return Forwarded(sup.Daemon("DELETE", "/api/v1/pair"), "pairing");
    if (verb == "revoke")
    {
        std::string id = Str(args.find("id"));
        if (id.empty())
        {
            const StateNode status = sup.Status();
            if (const StateNode* daemon = status.find("daemon"))
                if (const StateNode* pairing = daemon->find("pairing"))
                    id = Str(pairing->find("companion_id"));
        }
        if (id.empty())
            return Error(404, "Not Found", "the Stream Deck daemon has no companion (not paired)");
        const size_t revoked = CompanionRegistry::Instance().Revoke(id);
        if (revoked == 0)
            return Error(404, "Not Found", "no companion '" + id + "'");
        StateNode body = StateNode::Object();
        body["revoked"] = static_cast<int64_t>(revoked);
        body["id"] = id;
        return Ok(body);
    }
    if (verb == "brightness")
    {
        std::string deck = Str(args.find("deck"));
        if (deck.empty())
            deck = "first";
        const StateNode* value = args.find("value");
        if (!value)
            return Forwarded(sup.Daemon("GET", "/api/v1/decks/" + Segment(deck) + "/brightness"), "deck");
        const std::string text = Str(value);
        char* end = nullptr;
        const long percent = std::strtol(text.c_str(), &end, 10);
        if (text.empty() || *end != '\0' || percent < 0 || percent > 100)
            return Error(400, "Bad Request", "value: 0-100");
        Reply r = Forwarded(sup.Daemon("PUT", "/api/v1/decks/" + Segment(deck) + "/brightness",
                                       "{\"value\":" + std::to_string(percent) + "}"),
                            "deck");
        if (r.Ok())
        {
            // kept for that deck: set again when the daemon starts again
            const StateNode* answer = r.body.find("deck");
            const std::string serial = answer ? Str(answer->find("serial")) : std::string();
            if (!serial.empty())
            {
                StateNode changes = StateNode::Object();
                changes["brightness"] = StateNode::Object();
                changes["brightness"][serial] = static_cast<int64_t>(percent);
                std::string error;
                (void)sup.Update(changes, error);
            }
        }
        return r;
    }
    if (verb == "packs")
    {
        // the daemon's {packs: [...], default} as the reply's own fields
        Reply r = Forwarded(sup.Daemon("GET", "/api/v1/packs"), "daemon");
        if (!r.Ok())
            return r;
        StateNode answer = std::move(r.body["daemon"]);
        StateNode body = StateNode::Object();
        const StateNode* packs = answer.find("packs");
        body["packs"] = packs && packs->isArray() ? *packs : StateNode::Array();
        body["default"] = Str(answer.find("default"));
        return Ok(body);
    }
    if (verb == "pack")
    {
        std::string deck = Str(args.find("deck"));
        if (deck.empty())
            deck = "first";
        const StateNode* value = args.find("pack");
        if (!value)
            return Forwarded(sup.Daemon("GET", "/api/v1/decks/" + Segment(deck) + "/pack"), "deck");
        const std::string name = Str(value);
        if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)
            return Error(400, "Bad Request", "pack: an icon pack name (packs lists them), or scenario");
        Reply r = Forwarded(sup.Daemon("PUT", "/api/v1/decks/" + Segment(deck) + "/pack",
                                       "{\"pack\":\"" + name + "\"}", 3000),
                            "deck");
        if (r.Ok())
        {
            // kept for that deck (scenario: none kept): set again when the daemon starts again
            const StateNode* answer = r.body.find("deck");
            const std::string serial = answer ? Str(answer->find("serial")) : std::string();
            if (!serial.empty())
                sup.KeepAppliedPack(serial, name);
        }
        return r;
    }
    return Error(400, "Bad Request", "unknown verb '" + verb + "'");
}

} // namespace streamdeck
} // namespace companion
