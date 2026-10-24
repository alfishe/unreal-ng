#include "stdafx.h"

#include "companionregistry.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include "3rdparty/digestpp/digestpp.hpp"
#include "3rdparty/message-center/messagecenter.h"
#include "common/filehelper.h"
#include "common/logger.h"
#include "emulator/execution/automationgate.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#ifndef _RYML_SINGLE_HEADER_AMALGAMATED_HPP_
#include "3rdparty/rapidyaml/ryml_all.hpp"
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace companion
{
namespace
{
constexpr int kStoreVersion = 1;
/// A finished pairing (denied, expired, approved and not collected) still answers its poll this long
constexpr int64_t kFinishedPairingKeptMs = 60 * 1000;

/// rapidyaml's default error handler aborts; ours throws, so a broken store is set aside instead
[[noreturn]] void ThrowOnError(const char* message, size_t length, ryml::Location, void*)
{
    throw std::runtime_error(std::string(message, length));
}

std::string Text(ryml::csubstr s)
{
    return std::string(s.str ? s.str : "", s.len);
}

std::string RandomBytes(size_t count)
{
    std::random_device device;
    std::string bytes(count, '\0');
    for (char& byte : bytes)
        byte = static_cast<char>(device() & 0xFF);
    return bytes;
}

std::string Hex(const std::string& bytes)
{
    static const char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (char c : bytes)
    {
        const auto b = static_cast<unsigned char>(c);
        text += digits[b >> 4];
        text += digits[b & 0x0F];
    }
    return text;
}

std::string Base64Url(const std::string& bytes)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string text;
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : bytes)
    {
        buffer = (buffer << 8) | static_cast<unsigned char>(c);
        bits += 8;
        while (bits >= 6)
        {
            bits -= 6;
            text += alphabet[(buffer >> bits) & 0x3F];
        }
    }
    if (bits > 0)
        text += alphabet[(buffer << (6 - bits)) & 0x3F];
    return text;
}

/// A JSON string literal
std::string Quote(const std::string& text)
{
    std::string out = "\"";
    for (char c : text)
    {
        const auto b = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += c;
        }
        else if (b < 0x20)
        {
            static const char digits[] = "0123456789abcdef";
            out += "\\u00";
            out += digits[b >> 4];
            out += digits[b & 0x0F];
        }
        else
            out += c;
    }
    return out + "\"";
}

std::vector<std::string> Split(const std::string& text, char separator)
{
    std::vector<std::string> parts;
    std::string part;
    std::istringstream in(text);
    while (std::getline(in, part, separator))
        parts.push_back(part);
    return parts;
}

std::string Trim(const std::string& text)
{
    const size_t start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return {};
    const size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}
}  // namespace

ActionCheck CheckAction(const CompanionInfo& companion, uint32_t required, bool claimed, const std::string& holder,
                        const std::string& takeBackHint, bool needsPause, bool paused)
{
    ActionCheck check;
    check.required = required;
    if (!(companion.scopes & required))
    {
        check.error = "companion_forbidden";
        check.message = "this companion may not use '" + automation_gate::CompanionScopeName(required) +
                        "': the user did not grant it at pairing";
        return check;
    }
    if (claimed)
    {
        check.error = "held_by_session";
        check.message = holder + " controls the emulator" +
                        ((companion.scopes & automation_gate::ScopeTakeBack)
                             ? "; " + takeBackHint + " takes control back"
                             : std::string("; this companion may not take control back"));
        return check;
    }
    if (needsPause && !paused)
    {
        check.error = "not_now";
        check.message = "the machine runs: stepping, direct runs and register writes need a pause - pause first";
        return check;
    }
    check.allowed = true;
    return check;
}

CompanionRegistry& CompanionRegistry::Instance()
{
    static CompanionRegistry registry;
    return registry;
}

/// region <Helpers>

std::string CompanionRegistry::GenerateToken()
{
    return "unc_" + Base64Url(RandomBytes(32));
}

std::string CompanionRegistry::HashToken(const std::string& token)
{
    return digestpp::sha256().absorb(token).hexdigest();
}

std::optional<uint32_t> CompanionRegistry::ScopesFromNames(const std::vector<std::string>& names)
{
    uint32_t scopes = 0;
    for (const std::string& name : names)
    {
        const uint32_t scope = automation_gate::CompanionScopeFromName(name);
        if (!scope)
            return std::nullopt;
        scopes |= scope;
    }
    return scopes;
}

std::string CompanionRegistry::CleanName(const std::string& text)
{
    std::string clean;
    for (char c : text)
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F)
            clean += c;
    clean = Trim(clean);
    if (clean.size() > kMaxNameLength)
    {
        size_t cut = kMaxNameLength;
        while (cut > 0 && (static_cast<unsigned char>(clean[cut]) & 0xC0) == 0x80)  // not inside a UTF-8 character
            --cut;
        clean = Trim(clean.substr(0, cut));
    }
    return clean;
}

void CompanionRegistry::SetClockForTesting(std::function<int64_t()> nowMs)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _clock = std::move(nowMs);
}

void CompanionRegistry::ResetForTesting()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _companions.clear();
    _byHash.clear();
    _pairings.clear();
    _approvalAvailable = false;
    _pairingAllowed = true;
    _storePath.clear();
    _storeReadOnly = false;
    _clock = nullptr;
}

int64_t CompanionRegistry::NowMs() const
{
    if (_clock)
        return _clock();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string CompanionRegistry::NewIdLocked(const char* prefix) const
{
    for (;;)
    {
        std::string id = prefix + Hex(RandomBytes(6));
        const bool taken =
            std::any_of(_companions.begin(), _companions.end(), [&](const Entry& e) { return e.info.id == id; }) ||
            std::any_of(_pairings.begin(), _pairings.end(), [&](const Pairing& p) { return p.info.id == id; });
        if (!taken)
            return id;
    }
}

void CompanionRegistry::Announce(const std::vector<Event>& events)
{
    for (const Event& e : events)
        MessageCenter::DefaultMessageCenter().Post(
            NC_COMPANION_CHANGED, new CompanionChangedPayload(e.event, e.companionId, e.pairingId, e.name), true);
}

void CompanionRegistry::ExpireLocked(int64_t now, std::vector<Event>& events)
{
    const int64_t lifetime = int64_t(kPairingLifetimeSeconds) * 1000;
    for (Pairing& p : _pairings)
    {
        if (p.info.state == PairingState::Pending && now - p.info.createdMs >= lifetime)
        {
            p.info.state = PairingState::Expired;
            p.finishedMs = now;
            events.push_back({"pairing_expired", {}, p.info.id, p.info.name});
        }
    }
    _pairings.erase(std::remove_if(_pairings.begin(), _pairings.end(),
                                   [&](const Pairing& p) {
                                       const int64_t keep = p.info.state == PairingState::Approved
                                                                ? lifetime
                                                                : kFinishedPairingKeptMs;
                                       return p.info.state != PairingState::Pending && now - p.finishedMs >= keep;
                                   }),
                    _pairings.end());
}

/// endregion </Helpers>

/// region <Pairing>

PairingRequestResult CompanionRegistry::RequestPairing(const std::string& name, const std::string& device,
                                                       uint32_t scopes, const std::string& peer)
{
    PairingRequestResult result;
    std::vector<Event> events;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const int64_t now = NowMs();
        ExpireLocked(now, events);
        const std::string cleanName = CleanName(name);
        if (cleanName.empty())
        {
            result.error = "bad_request";
            result.message = "a pairing needs a name";
        }
        else if (!_approvalAvailable)
        {
            result.error = "pairing_unavailable";
            result.message = "no user interface to approve a pairing on this instance; its owner provisions companions "
                             "in UNREAL_COMPANION_TOKENS";
        }
        else if (!_pairingAllowed)
        {
            result.error = "pairing_disabled";
            result.message = "the user turned pairing requests off (Tools > Companions... at the emulator)";
        }
        else
        {
            size_t pending = 0;
            bool fromPeer = false;
            for (const Pairing& p : _pairings)
                if (p.info.state == PairingState::Pending)
                {
                    ++pending;
                    fromPeer = fromPeer || (!peer.empty() && p.info.peer == peer);
                }
            if (pending >= kMaxPendingPairings || fromPeer)
            {
                result.error = "too_many_pairings";
                result.message = fromPeer ? "a pairing from this address is already waiting for the user"
                                          : "too many pairings are waiting for the user";
            }
            else
            {
                Pairing p;
                p.info.id = NewIdLocked("p-");
                p.info.name = cleanName;
                p.info.device = CleanName(device);
                p.info.peer = peer;
                p.info.scopes = (scopes ? scopes : automation_gate::kCompanionDefaultScopes) &
                                automation_gate::kCompanionAllScopes;
                p.info.createdMs = now;
                p.info.expiresIn = kPairingLifetimeSeconds;
                std::random_device device;
                p.code = std::to_string(100000 + device() % 900000);
                result.pairing = p.info;
                result.code = p.code;
                _pairings.push_back(std::move(p));
                events.push_back({"pairing_requested", {}, result.pairing.id, result.pairing.name});
            }
        }
    }
    Announce(events);
    if (result.error.empty())
        LOGINFO("Companion pairing requested: '%s' (%s) from %s", result.pairing.name.c_str(),
                result.pairing.device.c_str(), peer.c_str());
    return result;
}

PairingPoll CompanionRegistry::PollPairing(const std::string& pairingId)
{
    PairingPoll poll;
    poll.state = "unknown";
    std::vector<Event> events;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const int64_t now = NowMs();
        ExpireLocked(now, events);
        auto it = std::find_if(_pairings.begin(), _pairings.end(),
                               [&](const Pairing& p) { return p.info.id == pairingId; });
        if (it != _pairings.end())
        {
            switch (it->info.state)
            {
                case PairingState::Pending:
                    poll.state = "pending";
                    poll.expiresIn = static_cast<int>(
                        std::max<int64_t>(0, (it->info.createdMs + kPairingLifetimeSeconds * 1000 - now + 999) / 1000));
                    break;
                case PairingState::Approved:
                    poll.state = "approved";
                    poll.token = it->token;
                    poll.companionId = it->companionId;
                    poll.scopes = it->info.scopes;
                    _pairings.erase(it);  // the token is delivered once
                    break;
                case PairingState::Denied:
                    poll.state = "denied";
                    break;
                case PairingState::Expired:
                    poll.state = "expired";
                    break;
            }
        }
    }
    Announce(events);
    return poll;
}

bool CompanionRegistry::CancelPairing(const std::string& pairingId)
{
    std::vector<Event> events;
    bool cancelled = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ExpireLocked(NowMs(), events);
        auto it = std::find_if(_pairings.begin(), _pairings.end(), [&](const Pairing& p) {
            return p.info.id == pairingId && p.info.state == PairingState::Pending;
        });
        if (it != _pairings.end())
        {
            events.push_back({"pairing_cancelled", {}, it->info.id, it->info.name});
            _pairings.erase(it);
            cancelled = true;
        }
    }
    Announce(events);
    return cancelled;
}

std::optional<CompanionInfo> CompanionRegistry::ApprovePairing(const std::string& pairingId,
                                                               std::optional<uint32_t> scopes)
{
    std::vector<Event> events;
    std::optional<CompanionInfo> paired;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const int64_t now = NowMs();
        ExpireLocked(now, events);
        auto it = std::find_if(_pairings.begin(), _pairings.end(), [&](const Pairing& p) {
            return p.info.id == pairingId && p.info.state == PairingState::Pending;
        });
        if (it != _pairings.end())
        {
            const std::string token = GenerateToken();
            CompanionInfo info;
            info.id = NewIdLocked("c-");
            info.name = it->info.name;
            info.device = it->info.device;
            info.source = "paired";
            info.scopes = (scopes ? *scopes : it->info.scopes) & automation_gate::kCompanionAllScopes;
            info.pairedMs = now;
            if (AddLocked(info, HashToken(token)))
            {
                it->info.state = PairingState::Approved;
                it->info.scopes = info.scopes;
                it->finishedMs = now;
                it->token = token;
                it->companionId = info.id;
                paired = info;
                events.push_back({"paired", info.id, pairingId, info.name});
            }
        }
    }
    if (paired)
    {
        WriteStore();
        LOGINFO("Companion paired: '%s' (%s)", paired->name.c_str(), paired->id.c_str());
    }
    Announce(events);
    return paired;
}

bool CompanionRegistry::DenyPairing(const std::string& pairingId)
{
    std::vector<Event> events;
    bool denied = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const int64_t now = NowMs();
        ExpireLocked(now, events);
        auto it = std::find_if(_pairings.begin(), _pairings.end(), [&](const Pairing& p) {
            return p.info.id == pairingId && p.info.state == PairingState::Pending;
        });
        if (it != _pairings.end())
        {
            it->info.state = PairingState::Denied;
            it->finishedMs = now;
            events.push_back({"denied", {}, it->info.id, it->info.name});
            denied = true;
        }
    }
    Announce(events);
    return denied;
}

std::vector<PairingInfo> CompanionRegistry::Pending() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const int64_t now = NowMs();
    std::vector<PairingInfo> pending;
    for (const Pairing& p : _pairings)
    {
        const int64_t left = p.info.createdMs + kPairingLifetimeSeconds * 1000 - now;
        if (p.info.state != PairingState::Pending || left <= 0)
            continue;
        PairingInfo info = p.info;
        info.expiresIn = static_cast<int>((left + 999) / 1000);
        pending.push_back(info);
    }
    return pending;
}

std::string CompanionRegistry::CodeOf(const std::string& pairingId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (const Pairing& p : _pairings)
        if (p.info.id == pairingId && p.info.state == PairingState::Pending)
            return p.code;
    return {};
}

/// endregion </Pairing>

/// region <Companions>

bool CompanionRegistry::AddLocked(CompanionInfo info, const std::string& tokenHash)
{
    if (_byHash.count(tokenHash))
        return false;
    _byHash.emplace(tokenHash, _companions.size());
    _companions.push_back(Entry{std::move(info), tokenHash});
    return true;
}

std::optional<CompanionInfo> CompanionRegistry::Authenticate(const std::string& token)
{
    if (token.empty())
        return std::nullopt;
    // Looked up by its digest: the time a look-up takes says nothing about the token
    const std::string hash = HashToken(token);
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _byHash.find(hash);
    if (it == _byHash.end())
        return std::nullopt;
    Entry& entry = _companions[it->second];
    entry.info.lastSeenMs = NowMs();
    return entry.info;
}

std::optional<CompanionInfo> CompanionRegistry::Find(const std::string& idOrName) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (const Entry& e : _companions)
        if (e.info.id == idOrName)
            return e.info;
    for (const Entry& e : _companions)
        if (e.info.name == idOrName)
            return e.info;
    return std::nullopt;
}

size_t CompanionRegistry::Revoke(const std::string& idOrName)
{
    if (idOrName == "all")
        return RevokeAll();
    std::vector<Event> events;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = std::find_if(_companions.begin(), _companions.end(),
                               [&](const Entry& e) { return e.info.id == idOrName; });
        if (it == _companions.end())
            it = std::find_if(_companions.begin(), _companions.end(),
                              [&](const Entry& e) { return e.info.name == idOrName; });
        if (it == _companions.end())
            return 0;
        events.push_back({"revoked", it->info.id, {}, it->info.name});
        _companions.erase(it);
        _byHash.clear();
        for (size_t i = 0; i < _companions.size(); ++i)
            _byHash.emplace(_companions[i].tokenHash, i);
    }
    LOGINFO("Companion revoked: '%s' (%s)", events.front().name.c_str(), events.front().companionId.c_str());
    WriteStore();
    Announce(events);
    return 1;
}

size_t CompanionRegistry::RevokeAll()
{
    std::vector<Event> events;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const Entry& e : _companions)
            events.push_back({"revoked", e.info.id, {}, e.info.name});
        _companions.clear();
        _byHash.clear();
    }
    if (!events.empty())
    {
        LOGINFO("Every companion revoked (%zu)", events.size());
        WriteStore();
    }
    Announce(events);
    return events.size();
}

std::vector<CompanionInfo> CompanionRegistry::List() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<CompanionInfo> list;
    list.reserve(_companions.size());
    for (const Entry& e : _companions)
        list.push_back(e.info);
    return list;
}

StateNode CompanionRegistry::Report() const
{
    auto scopes = [](uint32_t bits) {
        StateNode list = StateNode::Array();
        for (const std::string& name : automation_gate::CompanionScopeNames(bits))
            list.push(name);
        return list;
    };
    StateNode report = StateNode::Object();
    StateNode companions = StateNode::Array();
    for (const CompanionInfo& c : List())
    {
        StateNode node = StateNode::Object();
        node["id"] = c.id;
        node["name"] = c.name;
        node["device"] = c.device;
        node["scopes"] = scopes(c.scopes);
        node["source"] = c.source;
        node["paired_ms"] = c.pairedMs;
        node["last_seen_ms"] = c.lastSeenMs;
        companions.push(std::move(node));
    }
    StateNode pairings = StateNode::Array();
    for (const PairingInfo& p : Pending())
    {
        StateNode node = StateNode::Object();
        node["pairing_id"] = p.id;
        node["name"] = p.name;
        node["device"] = p.device;
        node["peer"] = p.peer;
        node["expires_in"] = p.expiresIn;
        node["scopes"] = scopes(p.scopes);
        pairings.push(std::move(node));
    }
    report["companions"] = std::move(companions);
    report["pairings"] = std::move(pairings);
    report["approval_available"] = IsApprovalAvailable();
    report["pairing_allowed"] = IsPairingAllowed();
    return report;
}

/// endregion </Companions>

/// region <Host wiring>

void CompanionRegistry::SetApprovalAvailable(bool available)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _approvalAvailable = available;
}

bool CompanionRegistry::IsApprovalAvailable() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _approvalAvailable;
}

void CompanionRegistry::SetPairingAllowed(bool allowed)
{
    std::vector<Event> events;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _pairingAllowed = allowed;
        if (!allowed)
        {
            // The requests waiting now are refused too: their prompts close, the devices see "denied"
            const int64_t now = NowMs();
            ExpireLocked(now, events);
            for (Pairing& p : _pairings)
            {
                if (p.info.state != PairingState::Pending)
                    continue;
                p.info.state = PairingState::Denied;
                p.finishedMs = now;
                events.push_back({"denied", {}, p.info.id, p.info.name});
            }
        }
    }
    Announce(events);
}

bool CompanionRegistry::IsPairingAllowed() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _pairingAllowed;
}

std::string CompanionRegistry::StorePath() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _storePath;
}

void CompanionRegistry::SetStorePath(const std::string& path)
{
    std::lock_guard<std::mutex> storeLock(_storeMutex);
    std::lock_guard<std::mutex> lock(_mutex);
    // Paired companions of a previous store go; those of the environment stay
    _companions.erase(std::remove_if(_companions.begin(), _companions.end(),
                                     [](const Entry& e) { return e.info.source == "paired"; }),
                      _companions.end());
    _byHash.clear();
    for (size_t i = 0; i < _companions.size(); ++i)
        _byHash.emplace(_companions[i].tokenHash, i);
    _storePath = path;
    _storeReadOnly = false;
    if (!path.empty())
        LoadStoreLocked();
}

void CompanionRegistry::LoadStoreLocked()
{
    std::error_code ec;
    const std::filesystem::path path = FileHelper::ToFsPath(_storePath);
    if (!std::filesystem::is_regular_file(path, ec))
        return;
    std::string text;
    {
        // Closed before a broken store is renamed below: Windows renames no open file
        std::ifstream in(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        text = buffer.str();
    }

    static std::once_flag once;
    std::call_once(once, [] { ryml::set_callbacks(ryml::Callbacks(nullptr, nullptr, nullptr, &ThrowOnError)); });
    try
    {
        const ryml::Tree tree = ryml::parse_json_in_arena(ryml::to_csubstr(text));
        ryml::ConstNodeRef root = tree.crootref();
        if (!root.is_map())
            throw std::runtime_error("the store is not an object");
        int version = 0;
        if (root.has_child("version") && root["version"].has_val())
            version = std::stoi(Text(root["version"].val()));
        if (version > kStoreVersion)
        {
            _storeReadOnly = true;
            LOGWARNING("Companion store %s has version %d, newer than %d: read only", _storePath.c_str(), version,
                       kStoreVersion);
        }
        if (!root.has_child("companions"))
            return;
        for (ryml::ConstNodeRef node : root["companions"].children())
        {
            auto field = [&](const char* key) -> std::string {
                const ryml::csubstr k = ryml::to_csubstr(key);
                return node.has_child(k) && node[k].has_val() ? Text(node[k].val()) : std::string();
            };
            CompanionInfo info;
            info.id = field("id");
            info.name = CleanName(field("name"));
            info.device = CleanName(field("device"));
            info.source = "paired";
            const std::string hash = field("token_sha256");
            if (info.id.empty() || info.name.empty() || hash.size() != 64)
                continue;
            if (node.has_child("scopes"))
                for (ryml::ConstNodeRef scope : node["scopes"].children())
                    if (scope.has_val())
                        info.scopes |= automation_gate::CompanionScopeFromName(Text(scope.val()));
            const std::string paired = field("paired_ms");
            const std::string seen = field("last_seen_ms");
            info.pairedMs = paired.empty() ? 0 : std::stoll(paired);
            info.lastSeenMs = seen.empty() ? 0 : std::stoll(seen);
            AddLocked(info, hash);
        }
    }
    catch (const std::exception& e)
    {
        _companions.erase(std::remove_if(_companions.begin(), _companions.end(),
                                         [](const Entry& entry) { return entry.info.source == "paired"; }),
                          _companions.end());
        _byHash.clear();
        for (size_t i = 0; i < _companions.size(); ++i)
            _byHash.emplace(_companions[i].tokenHash, i);
        std::filesystem::path bad = path;
        bad += ".bad";
        std::filesystem::rename(path, bad, ec);
        LOGWARNING("Companion store %s cannot be read (%s): set aside as %s, starting with no paired companions",
                   _storePath.c_str(), e.what(), FileHelper::FromFsPath(bad).c_str());
    }
}

std::string CompanionRegistry::StoreTextLocked() const
{
    std::ostringstream out;
    out << "{\n  \"version\": " << kStoreVersion << ",\n  \"companions\": [";
    bool first = true;
    for (const Entry& e : _companions)
    {
        if (e.info.source != "paired")
            continue;
        out << (first ? "\n" : ",\n") << "    { \"id\": " << Quote(e.info.id) << ", \"name\": " << Quote(e.info.name)
            << ", \"device\": " << Quote(e.info.device) << ", \"scopes\": [";
        const std::vector<std::string> names = automation_gate::CompanionScopeNames(e.info.scopes);
        for (size_t i = 0; i < names.size(); ++i)
            out << (i ? ", " : "") << Quote(names[i]);
        out << "], \"token_sha256\": " << Quote(e.tokenHash) << ", \"paired_ms\": " << e.info.pairedMs
            << ", \"last_seen_ms\": " << e.info.lastSeenMs << " }";
        first = false;
    }
    out << (first ? "]\n}\n" : "\n  ]\n}\n");
    return out.str();
}

void CompanionRegistry::WriteStore()
{
    std::lock_guard<std::mutex> storeLock(_storeMutex);
    std::string path, text;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_storePath.empty() || _storeReadOnly)
            return;
        path = _storePath;
        text = StoreTextLocked();
    }
    std::error_code ec;
    const std::filesystem::path target = FileHelper::ToFsPath(path);
    if (target.has_parent_path())
        std::filesystem::create_directories(target.parent_path(), ec);
    std::filesystem::path temporary = target;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out)
        {
            static std::once_flag logged;
            std::call_once(logged, [&] {
                LOGWARNING("Companion store %s cannot be written: pairings last for this run only", path.c_str());
            });
            return;
        }
    }
    std::filesystem::permissions(temporary, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, ec);
    std::filesystem::rename(temporary, target, ec);
    if (ec)
    {
        // Windows: rename onto an existing file fails on some file systems
        std::filesystem::remove(target, ec);
        std::filesystem::rename(temporary, target, ec);
    }
}

void CompanionRegistry::Flush()
{
    WriteStore();
}

size_t CompanionRegistry::LoadFromEnvironment()
{
    const char* value = std::getenv("UNREAL_COMPANION_TOKENS");
    return value && *value ? AddFromSpec(value) : 0;
}

size_t CompanionRegistry::AddFromSpec(const std::string& spec)
{
    size_t added = 0;
    for (const std::string& rawEntry : Split(spec, ';'))
    {
        const std::string entry = Trim(rawEntry);
        if (entry.empty())
            continue;
        const std::vector<std::string> fields = Split(entry, ':');
        const std::string name = fields.empty() ? std::string() : CleanName(fields[0]);
        const std::string token = fields.size() > 1 ? Trim(fields[1]) : std::string();
        if (name.empty() || token.size() < kMinTokenLength || fields.size() > 3)
        {
            LOGWARNING("UNREAL_COMPANION_TOKENS: entry '%s' skipped: expected name:token[:scopes] with a token of at "
                       "least %zu characters",
                       name.c_str(), kMinTokenLength);
            continue;
        }
        uint32_t scopes = automation_gate::kCompanionDefaultScopes;
        if (fields.size() == 3 && !Trim(fields[2]).empty())
        {
            std::vector<std::string> names;
            for (const std::string& scope : Split(fields[2], ','))
                if (!Trim(scope).empty())
                    names.push_back(Trim(scope));
            const std::optional<uint32_t> parsed = ScopesFromNames(names);
            if (!parsed)
            {
                LOGWARNING("UNREAL_COMPANION_TOKENS: entry '%s' skipped: unknown scope in '%s'", name.c_str(),
                           fields[2].c_str());
                continue;
            }
            scopes = *parsed;
        }
        std::lock_guard<std::mutex> lock(_mutex);
        const bool nameTaken = std::any_of(_companions.begin(), _companions.end(),
                                           [&](const Entry& e) { return e.info.name == name; });
        CompanionInfo info;
        info.id = NewIdLocked("c-");
        info.name = name;
        info.source = "environment";
        info.scopes = scopes;
        info.pairedMs = NowMs();
        if (nameTaken || !AddLocked(info, HashToken(token)))
        {
            LOGWARNING("UNREAL_COMPANION_TOKENS: entry '%s' skipped: its name or token is already in use", name.c_str());
            continue;
        }
        ++added;
    }
    return added;
}

/// endregion </Host wiring>
}  // namespace companion
