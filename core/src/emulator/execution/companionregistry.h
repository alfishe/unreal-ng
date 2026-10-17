#pragma once

/// @file companionregistry.h
/// @brief Companion clients (docs/inprogress/2026-10-16-companion-client): paired devices that act as the user's own
/// hands. A companion changes the machine without a run-control session - the GUI shows no remote control and does not
/// lock - within the scopes the user granted (automationgate.h). This registry holds the companions and the pending
/// pairings of the process (companions are the user's, not an instance's).
///
/// Trust: a pairing is approved at the GUI only (ApprovePairing has no automation surface); a headless instance gets
/// its companions from UNREAL_COMPANION_TOKENS. A token is 32 random bytes; only its SHA-256 is kept and stored.
///
/// Thread-safe: requests arrive on WebAPI, CLI and GUI threads at once. NC_COMPANION_CHANGED is posted after each
/// change, outside the lock.

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "emulator/state/statenode.h"

namespace companion
{
/// A paired (or provisioned) companion, as every surface lists it: never its token
struct CompanionInfo
{
    std::string id;           ///< "c-" + 12 hex digits
    std::string name;
    std::string device;
    std::string source;       ///< "paired" (kept in the store) | "environment" (UNREAL_COMPANION_TOKENS)
    uint32_t scopes = 0;      ///< automation_gate::CompanionScope bits
    int64_t pairedMs = 0;     ///< wall clock, ms since the epoch
    int64_t lastSeenMs = 0;   ///< the last authenticated request; 0 = never
};

enum class PairingState
{
    Pending,
    Approved,  ///< the token waits to be collected by one poll
    Denied,
    Expired
};

/// A pairing request, as listed (never its code: only the device and the GUI prompt show that)
struct PairingInfo
{
    std::string id;           ///< "p-" + 12 hex digits
    std::string name;
    std::string device;
    std::string peer;         ///< the requesting host
    uint32_t scopes = 0;      ///< asked for
    int64_t createdMs = 0;
    int expiresIn = 0;        ///< seconds left while pending
    PairingState state = PairingState::Pending;
};

/// RequestPairing's answer. `error`: "" (ok), "bad_request", "pairing_unavailable", "pairing_disabled",
/// "too_many_pairings"
struct PairingRequestResult
{
    std::string error;
    std::string message;
    PairingInfo pairing;
    std::string code;         ///< six digits, for the device to show
};

/// PollPairing's answer. `state`: "pending", "approved" (with the token, delivered once), "denied", "expired",
/// "unknown" (a wrong id, or the token was collected)
struct PairingPoll
{
    std::string state;
    int expiresIn = 0;
    std::string token;
    std::string companionId;
    uint32_t scopes = 0;
};

/// The gate's answer to a companion's changing action (every surface asks the same): allowed, or refused with
/// `error` "companion_forbidden" (`required`: the scope it lacks), "held_by_session" or "not_now" (it needs a pause)
struct ActionCheck
{
    bool allowed = false;
    std::string error;
    uint32_t required = 0;
    std::string message;
};

/// May `companion` do an action that needs `required` (automation_gate::CompanionScopeOf / CompanionCliScopeOf) while
/// the machine's run control is `claimed` by `holder`? `takeBackHint`: how this surface takes control back
/// `needsPause` (CompanionNeedsPause) with the machine not `paused`: refused not_now
ActionCheck CheckAction(const CompanionInfo& companion, uint32_t required, bool claimed, const std::string& holder,
                        const std::string& takeBackHint, bool needsPause = false, bool paused = true);

class CompanionRegistry
{
public:
    static constexpr int kPairingLifetimeSeconds = 120;
    static constexpr size_t kMaxPendingPairings = 4;
    static constexpr size_t kMaxNameLength = 64;
    static constexpr size_t kMinTokenLength = 16;

    /// The process's registry
    static CompanionRegistry& Instance();

    CompanionRegistry() = default;
    CompanionRegistry(const CompanionRegistry&) = delete;
    CompanionRegistry& operator=(const CompanionRegistry&) = delete;

    /// region <Pairing>
    /// A device asks to pair. `scopes` 0 = the default set. Refused without an approver (no GUI), with 4 pending, or
    /// with one pending from `peer` already
    PairingRequestResult RequestPairing(const std::string& name, const std::string& device, uint32_t scopes,
                                        const std::string& peer);
    /// The device polls its pairing; an approved one hands out the token once and is gone
    PairingPoll PollPairing(const std::string& pairingId);
    /// The device gives up; false for an unknown or finished pairing
    bool CancelPairing(const std::string& pairingId);
    /// The user allows it at the GUI, optionally with fewer scopes. Only the GUI calls this
    std::optional<CompanionInfo> ApprovePairing(const std::string& pairingId, std::optional<uint32_t> scopes = {});
    /// The user refuses it at the GUI
    bool DenyPairing(const std::string& pairingId);
    /// The pending pairings
    std::vector<PairingInfo> Pending() const;
    /// The code of a pending pairing, for the GUI prompt; "" when it is not pending
    std::string CodeOf(const std::string& pairingId) const;
    /// endregion </Pairing>

    /// region <Companions>
    /// The companion a token belongs to; nothing for an unknown or revoked token. Notes the time it was seen
    std::optional<CompanionInfo> Authenticate(const std::string& token);
    /// The companion with this id or name
    std::optional<CompanionInfo> Find(const std::string& idOrName) const;
    /// Revokes the companion with this id or name ("all": every one); the number revoked
    size_t Revoke(const std::string& idOrName);
    size_t RevokeAll();
    std::vector<CompanionInfo> List() const;
    /// The listing every surface returns: {companions: [...], pairings: [...], approval_available} - never a token or
    /// a code (WebAPI GET /api/v1/companions, Lua / Python companions())
    StateNode Report() const;
    /// endregion </Companions>

    /// region <Host wiring>
    /// A GUI that can approve pairings is present (unreal-qt); without one, pairing is refused
    void SetApprovalAvailable(bool available);
    bool IsApprovalAvailable() const;
    /// The user's setting (Tools > Companions...): false refuses new requests (pairing_disabled) and denies the ones
    /// waiting; paired companions keep working
    void SetPairingAllowed(bool allowed);
    bool IsPairingAllowed() const;
    /// Keeps paired companions in this file (loaded now); "" = for the process only
    void SetStorePath(const std::string& path);
    std::string StorePath() const;
    /// Writes the store (the last-seen times), at exit
    void Flush();
    /// Adds the companions of UNREAL_COMPANION_TOKENS; the number added
    size_t LoadFromEnvironment();
    /// Adds companions from a spec in the UNREAL_COMPANION_TOKENS format: `name:token[:scope,scope]`, entries
    /// separated by ';'. The number added; bad entries are logged and skipped
    size_t AddFromSpec(const std::string& spec);
    /// endregion </Host wiring>

    /// region <Helpers and tests>
    /// "unc_" + 43 base64url characters (32 random bytes)
    static std::string GenerateToken();
    /// The lower-case hex SHA-256 of a token
    static std::string HashToken(const std::string& token);
    /// Scopes from names ("run", "all", ...); nullopt when one is unknown
    static std::optional<uint32_t> ScopesFromNames(const std::vector<std::string>& names);
    /// A name or device text: control characters dropped, trimmed, at most kMaxNameLength bytes of whole characters
    static std::string CleanName(const std::string& text);
    /// Wall clock in ms (the store's times) and the steady clock for expiry; tests inject both as one
    void SetClockForTesting(std::function<int64_t()> nowMs);
    /// Forgets everything, the store path and the approver included (no file is touched)
    void ResetForTesting();
    /// endregion </Helpers and tests>

private:
    struct Entry
    {
        CompanionInfo info;
        std::string tokenHash;
    };
    struct Pairing
    {
        PairingInfo info;
        std::string code;
        std::string token;          ///< approved, not collected yet
        std::string companionId;
        int64_t finishedMs = 0;     ///< when it left the pending state
    };
    struct Event
    {
        std::string event, companionId, pairingId, name;
    };

    int64_t NowMs() const;
    void ExpireLocked(int64_t now, std::vector<Event>& events);
    std::string NewIdLocked(const char* prefix) const;
    bool AddLocked(CompanionInfo info, const std::string& tokenHash);
    void LoadStoreLocked();
    std::string StoreTextLocked() const;
    void WriteStore();
    static void Announce(const std::vector<Event>& events);

    mutable std::mutex _mutex;
    std::mutex _storeMutex;  ///< taken before _mutex: one store write at a time, in order
    std::vector<Entry> _companions;
    std::unordered_map<std::string, size_t> _byHash;  ///< token digest -> index in _companions
    std::vector<Pairing> _pairings;
    bool _approvalAvailable = false;
    bool _pairingAllowed = true;
    std::string _storePath;
    bool _storeReadOnly = false;  ///< a newer version: never overwritten
    std::function<int64_t()> _clock;
};
}  // namespace companion
