#pragma once

/// @file slotplanner.h
/// @brief The ZX-bus slot plan engine (docs/inprogress/2026-10-03-zx-bus-slots/architecture.md §5,
/// reference-data.md §5): pure functions over the reference data collection and a machine's current slot set.
/// Given a request (plug / remove / set options) it computes the whole plan - what is refused, removed, shadowed,
/// switched off, taken out of a socket, disabled, lost, which media are released and how the card fits the bus -
/// without changing anything. Applying a plan (a restart with the new configuration, Q6) is a later phase (SL-6).
///
/// Rules D1-D12 in the order of reference-data.md §5: D9 (exceptions), D4 (refusals), D8 (fit), then D1-D3, D5-D7,
/// D12 in slot order, finally D10-D11. Displacement is one step (removing a card never displaces another) and every
/// matching card is collected at once. The slot set is put into canonical order first, so the result does not
/// depend on the order in which cards were added.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "emulator/slots/refdata/refdata.h"
#include "emulator/slots/slottypes.h"

namespace slots
{

/// A card's option values (bit sets as in OptionDef); options never set take the card's defaults
struct CardOptions
{
    std::array<uint32_t, kOptCount> bits{};
    uint32_t present = 0;   ///< bit per Opt: the option was given

    void Set(Opt key, uint32_t valueBits)
    {
        bits[static_cast<size_t>(key)] = valueBits;
        present |= 1u << static_cast<int>(key);
    }
    bool Has(Opt key) const
    {
        return (present & (1u << static_cast<int>(key))) != 0;
    }
    bool operator==(const CardOptions& other) const = default;
};

/// The effective value bits of one option (the card's default when not given; 0 when the card has no such option)
uint32_t OptionBits(const CardDef& card, const CardOptions& options, Opt key);

/// The card's port claims its options switch on (a claim with a `when` condition only when it holds)
std::vector<PortClaim> CardClaims(const CardDef& card, const CardOptions& options);

/// Parses "dip=ym,saa,gs mode=2" (space-separated name=value; a set option takes a comma list, "" or "none" = empty)
bool ParseCardOptions(const CardDef& card, std::string_view text, CardOptions& out, std::string* error = nullptr);

/// Every option of the card with its effective value: "dip=ym,saa,gs,sd gsRam=1m ctrlMask=pro" (enough to put the
/// card back); "" for a card without options
std::string FormatCardOptions(const CardDef& card, const CardOptions& options);

/// A port range as the docs write it: `#FB` for a low-byte decode (mask #00FF), `#C00F/#C00D` otherwise
std::string FormatPortRange(uint16_t mask, uint16_t match);
std::string FormatPort(uint16_t port);

/// One occupied slot
struct SlotEntry
{
    std::string slot;               ///< "zxbus.1", "ay-socket"
    std::string card;               ///< card id; "none" = the AY socket with its chip taken out
    CardOptions options;
    std::string adapter;            ///< adapter id, empty for none
    bool unrealistic = false;       ///< fitted with the override (`fit: unrealistic`)
    bool disabled = false;          ///< plugged in but disabled for an accidental port clash (R-COMP-6)
    std::string disabledReason;
};
using SlotSet = std::vector<SlotEntry>;

/// The slot id "none" card: an empty AY socket
inline constexpr std::string_view kEmptySocket = "none";

enum class MediaDisposition : uint8_t
{
    None,
    Save,
    Discard,
};

struct SlotRequest
{
    enum class Op : uint8_t
    {
        Plug,
        Remove,
        SetOptions,
    };
    Op op = Op::Plug;
    std::string slot;                   ///< "zxbus.2", "zxbus.next", "ay-socket"; empty = suggested by the planner
    std::string card;                   ///< Plug
    CardOptions options;                ///< Plug; SetOptions: the options to change (merged over the current ones)
    std::string adapter;                ///< Plug: adapter in the slot
    bool replaceIfIncompatible = false; ///< automation flag (Q1, Q5, Q7); the Qt UI plans with it set
    bool dryRun = false;
    MediaDisposition mediaDisposition = MediaDisposition::None;
};

/// Machine state a plan reads besides the slot set
struct PlanContext
{
    std::vector<std::string> dirtyMedia;    ///< media slots with unsaved changes ("sd.ngs")
    bool applyExceptions = true;            ///< false: ignore rule D9 (the consistency test checks exceptions are not redundant)
};

enum class Fit : uint8_t
{
    Real,
    Adapter,
    Unrealistic,
};

/// Which rule produced a plan item (reference-data.md §5)
enum class Rule : uint8_t
{
    Request,    ///< malformed request (unknown card / slot / option)
    D1, D2, D3, D4, D5, D6, D7, D8, D9, D10, D11, D12,
};

struct PlanReason
{
    Rule rule = Rule::Request;
    bool hard = false;          ///< refused even with replaceIfIncompatible
    std::string text;           ///< one sentence for people
    std::string brief;          ///< matrix cell wording
};

struct RemovedCard
{
    std::string slot;
    std::string card;
    CardOptions options;
    std::string optionsText;            ///< FormatCardOptions: enough to put the card back
    std::vector<Function> clashing;     ///< D1: the shared functions
    bool replacedInSlot = false;        ///< the new card goes into this slot
    bool pointless = false;             ///< D3 / D12: the socket board would be shadowed or fight the new card
    std::string reason;
};

struct ShadowedDevice
{
    std::string device;                 ///< built-in id ("ay")
    std::string by;                     ///< slot of the new card
    std::vector<uint16_t> ports;        ///< documented ports covered
};

struct SwitchedOffBuiltIn
{
    std::string builtIn;
    std::vector<Function> functions;
};

struct RemovedFromSocket
{
    std::string builtIn;
    std::string socket;
    std::string chip;
    std::vector<uint16_t> ports;        ///< the reads that would be a bus fight
};

struct DisabledCard
{
    std::string slot;
    std::string card;
    std::string clashSlot;
    std::vector<std::string> ports;
    std::string reason;
};

struct MediaRelease
{
    std::string slot;
    std::string card;
    std::string mediaSlot;
    bool dirty = false;
    MediaDisposition disposition = MediaDisposition::None;
};

/// Ports of the new card that the board hides from the slots (BoardWins, Iorq card)
struct DeadPort
{
    uint16_t mask = 0;
    uint16_t match = 0;
    std::vector<std::string> servedBy;  ///< built-ins on that port
};

/// A read two devices drive with nobody winning (no IORQGE effect) and no socket to empty
struct BusFight
{
    std::string device;
    uint16_t port = 0;
};

struct SlotPlan
{
    bool allowed = false;
    bool hardRefusal = false;           ///< refused whatever the flag
    bool needsConfirmation = false;     ///< something to confirm: automation needs replaceIfIncompatible
    bool dryRun = false;
    SlotRequest::Op op = SlotRequest::Op::Plug;
    std::string slot;                   ///< the resolved target slot
    std::string card;
    CardOptions options;
    std::vector<PlanReason> reasons;
    Fit fit = Fit::Real;
    std::string adapter;
    SignalSet missingSignals = 0;
    Arbitration arbitration = Arbitration::None;   ///< in effect for the new card
    std::vector<std::string> exceptions;           ///< D9 reasons applied
    std::vector<RemovedCard> removed;
    std::vector<ShadowedDevice> shadowed;
    std::vector<SwitchedOffBuiltIn> builtInSwitchedOff;
    std::vector<RemovedFromSocket> removedFromSocket;
    std::vector<DisabledCard> disabled;
    std::vector<DeadPort> deadPorts;
    std::vector<BusFight> busFights;
    std::vector<Function> lostFunctions;
    std::vector<MediaRelease> media;
    SlotSet resultingSlots;             ///< the slot set once the plan is applied (canonical order)

    /// Allowed and not a dry run: the caller writes resultingSlots and restarts the machine (SL-6)
    bool WouldApply() const
    {
        return allowed && !dryRun;
    }
};

class SlotPlanner
{
public:
    explicit SlotPlanner(const Collection& collection = refdata::All());

    SlotPlan Plan(MEM_MODEL model, const SlotSet& slots, const SlotRequest& request,
                  const PlanContext& context = {}) const;

    const CardDef* FindCard(std::string_view id) const;
    const MachineDef* FindMachine(MEM_MODEL model) const;
    const AdapterDef* FindAdapter(std::string_view id) const;

    /// Where a card goes on a machine by default: "ay-socket" for a socket board, "<bus>.next" for the card's native
    /// bus, else a bus an adapter connects it to, else the first expansion bus; "" when the machine has none
    std::string SuggestSlot(const MachineDef& machine, const CardDef& card) const;

    /// The slot set in canonical order (bus order of the machine, then slot number)
    SlotSet Canonical(const MachineDef& machine, const SlotSet& slots) const;

    const Collection& Data() const
    {
        return _collection;
    }

private:
    Collection _collection;
};

/// The plan as readable text (test diagnostics, logs)
std::string ToText(const SlotPlan& plan);

} // namespace slots
