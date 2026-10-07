#pragma once

/// @file slotmanager.h
/// @brief The slot set of one emulator instance (docs/inprogress/2026-10-03-zx-bus-slots/architecture.md §5-§7):
/// planned once when the instance is created, from the INI's [SLOTS] section or, without one, from the legacy card
/// keys translated into slots. The same plan engine (SlotPlanner) as an automation request decides, without the
/// replace flag, entry by entry in slot order. Configured entries that conflict with each other under the
/// compatibility matrix (a shared function D1, a pointless socket pair D3 / D12, an accidental port clash D7, an
/// explicit `ay-socket = ay` whose chip a card needs out of its socket, Q7) refuse the creation of the machine, with
/// every conflicting pair and its rule in the reason (owner decision Q8, 2026-10-05; it replaces R-CFG-3's "first
/// wins"). An entry the machine itself cannot take (a fixed built-in, a missing bus signal, a card not emulated) is
/// still left out with the reason. A slot with the fit override (`<slot>.fit = unrealistic`; every translated legacy
/// key, so an old INI keeps the devices it always had) may fit a card the bus cannot take, never displace one.
///
/// The cards themselves are still built by their owners (SoundManager mixes the sound cards, NetworkManager keeps
/// the virtual network): SlotManager decides which cards exist and writes that into the card fields of CONFIG they
/// read (Apply), group by group as SL-4 moves them (kSlotDecidedGroups). Code that still sets a card field directly
/// after the INI was read (the test runner's sound policy, a snapshot transfer, the TTD bench) is honored: that
/// group's slots are translated from the field again at creation.
///
/// Cards written for the slots (a CardType in card.h; the ZX-MultiSound is the first) are built here, not by a legacy
/// owner: BuildCards creates one ICard per fitted slot of such a card once the machine's sound manager and port
/// decoder exist, puts its port claims into the decoder's claim table with the machine's bus arbitration and its
/// mixer rows into SoundManager; ReleaseCards takes them out again before the machine goes.
///
/// No slot change in a running machine (Q6): a change is planned here (PlanChange: the plan engine over the current
/// slot set, the TTD guard, the dirty media) and applied by a restart of the instance with the planned [SLOTS]
/// (SlotChange::Run in slotchange.h, through the model-switch path). A model switch carries the slot set to the new
/// machine (Carry, R-OP-9).

// Qt defines `slots` / `signals` as macros; this header names the slots namespace
#pragma push_macro("slots")
#pragma push_macro("signals")
#undef slots
#undef signals

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "emulator/slots/slotconfig.h"
#include "emulator/slots/slotplanner.h"

struct CONFIG;
enum class GSTypeKind : uint8_t;
class EmulatorContext;
class ICard;
namespace slots
{
class IClaimSignals;
}
namespace ttd
{
class TTDPeripheralRegistry;
class TTDDeviceTable;
struct TTDConfigFingerprint;
}

/// The card groups SL-4 moves onto slots, one per step
enum class SlotCardGroup : uint8_t
{
    Socket,         ///< ay / ts / tsfm / none in the AY socket ([SOUND] TurboSound)
    GeneralSound,   ///< gs / gs-lw / neogs ([SOUND] GSType)
    MoonSound,      ///< moonsound ([SOUND] MoonSound)
    Covox,          ///< covox-fb / soundrive cards and the board Covox ([SOUND] CovoxFB, SD)
    Network,        ///< zxnetusb / zx-wifi ([NETWORK] Card)
    Count
};

constexpr uint32_t SlotGroupBit(SlotCardGroup group)
{
    return 1u << static_cast<uint32_t>(group);
}

/// The groups whose devices the slot set decides (the others still come from their legacy fields)
constexpr uint32_t kSlotDecidedGroups =
    SlotGroupBit(SlotCardGroup::Socket) | SlotGroupBit(SlotCardGroup::GeneralSound) | SlotGroupBit(SlotCardGroup::MoonSound) |
    SlotGroupBit(SlotCardGroup::Covox) | SlotGroupBit(SlotCardGroup::Network);

class SlotManager
{
public:
    /// One configured slot after the create-time plan
    struct Slot
    {
        slots::SlotEntry entry;             ///< as planned: `disabled` + `disabledReason` when not fitted
        SlotCardGroup group = SlotCardGroup::Count;
        std::string source;                 ///< "[SLOTS] zxbus.1", "[SOUND] GSType=NGS"
        bool fitOverride = false;           ///< the slot carried the fit override
        slots::Fit fit = slots::Fit::Real;
        std::vector<std::string> notes;     ///< what the plan said besides "allowed" (fit, dead ports, a clamp)
    };

    /// One built-in device of the machine declaration and its state
    struct BuiltIn
    {
        std::string id;
        std::string name;
        slots::BuiltInKind kind = slots::BuiltInKind::Fixed;
        bool on = true;                     ///< false: switched off (builtin.<id> = off)
        bool removed = false;               ///< a socketed chip a card took out of its socket (Q7)
        std::string state;                  ///< "active", "switched off", "shadowed by zxbus.1", "replaced by tsfm"
        std::string source;                 ///< where a switch came from
    };

    /// Two configured entries that cannot both be fitted (Q8): `slot` is the later entry in slot order
    struct Conflict
    {
        std::string slot;
        std::string card;
        std::string source;
        std::string withSlot;
        std::string withCard;
        std::string withSource;
        std::string rule;       ///< "D1", "D3", "D7", "D12", "Q7"
        std::string reason;     ///< the planner's sentence and what the rule says
    };

    /// A slot of the machine's own design, filled by the machine's own configuration (SL-8: the Sprinter's ISA-8
    /// slots, `[ISA] SlotN`): reported with the slot set, not planned (the cards in it are the board's). A ZX-bus
    /// adapter in it hosts a ZX-bus of its own: the slot of the same id in [SLOTS] (`isa.1 = neogs`, behind the
    /// adapter `sprinter-isa-zxbus`) is that bus's card
    struct MachineSlot
    {
        std::string slot;       ///< "isa.1"
        std::string bus;        ///< the machine's bus id: "isa"
        std::string card;       ///< the board's card key ("zxbus", "ne2000", "none")
        std::string name;       ///< "ISA to ZX-bus adapter"
        std::string source;     ///< "[ISA] Slot1"
        std::string hosts;      ///< the bus kind a ZX-bus adapter hosts ("zxbus"); "" when none
        std::string details;    ///< the card's resources ("RTL8019AS, #300, IRQ 3"); "" when none
    };

    struct Result
    {
        MEM_MODEL model{};
        const slots::MachineDef* machine = nullptr;   ///< nullptr: the model has no slot declaration
        bool fromSlotsSection = false;                ///< the INI's [SLOTS] (else: translated legacy keys)
        std::vector<Slot> entries;                    ///< every configured slot, in slot order
        std::vector<BuiltIn> builtIns;
        std::vector<std::string> log;                 ///< deprecation and refusal lines (logged as warnings)
        std::vector<std::string> info;                ///< cards fitted with the fit override (logged as info)
        std::vector<Conflict> conflicts;              ///< configured entries in conflict: the machine is not created
        std::vector<MachineSlot> machineSlots;        ///< the machine's own slots (Sprinter ISA), SL-8

        /// Why the machine is not created: every conflicting pair with its rule; "" when the set is creatable
        std::string Refusal() const;

        /// The fitted (not disabled) cards as the plan engine's slot set
        slots::SlotSet Fitted() const;
        /// The fitted entry of a slot / of the first card of a group; nullptr when none
        const Slot* FindSlot(const std::string& slot) const;
        const Slot* FindGroup(SlotCardGroup group) const;
        const BuiltIn* FindBuiltIn(const std::string& id) const;
        const MachineSlot* FindMachineSlot(const std::string& slot) const;
    };

    /// The machine's own slots of a config (SL-8): the Sprinter's ISA slots from `[ISA]`; empty for a machine
    /// without them
    static std::vector<MachineSlot> MachineSlotsOf(const CONFIG& config, const slots::MachineDef* machine);

    /// The legacy card fields of a config as slots (every group, or only `groups`), for the machine of the config:
    /// TurboSound -> ay-socket, GSType -> a GS card, MoonSound, SD / CovoxFB -> soundrive / covox-fb or the board
    /// Covox, [NETWORK] Card -> zxnetusb / zx-wifi. Every entry carries the fit override (an old INI keeps its devices)
    static SlotConfig TranslateLegacy(const CONFIG& config, uint32_t groups = ~0u);

    /// Plans the cards of a config for its model (pure: tests call it without an emulator)
    static Result Plan(const CONFIG& config, uint32_t decidedGroups = kSlotDecidedGroups);

    /// Writes the fitted cards of the decided groups into the CONFIG card fields the device code reads
    static void Apply(const Result& result, CONFIG& config, uint32_t decidedGroups = kSlotDecidedGroups);

    /// The card fields a [SLOTS] section stands for, without a machine (parse time): the decided groups only
    static void Project(const SlotConfig& slotConfig, CONFIG& config, uint32_t decidedGroups = kSlotDecidedGroups);

    /// The group a card id belongs to (Count for an unknown card)
    static SlotCardGroup GroupOf(const std::string& card);

    // region <Changes (SL-6): plan here, apply by a restart (SlotChange::Run)>

    /// The fitted slot set of a plan as a [SLOTS] section: every fitted entry with the options it was given, its
    /// adapter and fit override (also when the plan accepted an unrealistic fit), every switchable built-in's
    /// switch. Creating the same model from it fits the same cards (what a restart carries)
    static SlotConfig ConfigOf(const Result& result);

    /// Makes `slotConfig` the configuration's slot set: [SLOTS] (section set) and the card fields it stands for, as
    /// the parser leaves an INI with that section (the restart's config override)
    static void UseSlots(const SlotConfig& slotConfig, CONFIG& config);

    /// A slot change planned against a slot set
    struct ChangePlan
    {
        slots::SlotPlan plan;       ///< the plan engine's plan: reasons, removed, shadowed, lost, media, fit
        /// Why it is not applied ("" when it may be): the TTD session (R-OP-7), the plan's reasons (refused, or it
        /// needs replaceIfIncompatible), a resulting set the machine would refuse to create (Q8)
        std::string refusal;
        bool recording = false;     ///< refused because a TTD session records (R-OP-7)
        SlotConfig config;          ///< the [SLOTS] the restarted machine gets (an allowed plan)

        bool Allowed() const
        {
            return refusal.empty();
        }
    };

    /// Plans a change of `current` (pure). `config` is the machine's configuration (the new set is checked for the
    /// conflicts that would refuse its creation, Q8)
    static ChangePlan PlanChange(const Result& current, const CONFIG& config, const slots::SlotRequest& request,
                                 const slots::PlanContext& context = {});
    /// Plans a change of this instance: its slot set, the media with unsaved changes (R-OP-6) and the TTD guard
    /// (R-OP-7: refused while a user recording runs, naming the session)
    ChangePlan PlanChange(const slots::SlotRequest& request) const;
    /// A whole slot set for this instance (an Undo puts the previous one back, ConfigOf of the plan before the
    /// change): refused while a user recording runs (R-OP-7) or when the machine would not be created from it (Q8)
    ChangePlan PlanSet(const SlotConfig& slotConfig) const;
    /// Why a machine with this configuration and slot set would not be created (Q8 conflicts, a card that cannot be
    /// fitted); "" when it would
    static std::string CreationRefusal(const CONFIG& config, const SlotConfig& slotConfig);

    /// What a model switch did with the cards of the old machine (R-OP-9)
    struct CarryReport
    {
        struct Dropped
        {
            std::string slot;       ///< on the old machine
            std::string card;
            std::string reason;
        };
        bool carried = false;                 ///< the slot set was carried (false: the old machine had none)
        std::vector<std::string> kept;        ///< "zxbus.1 = neogs" (old slot -> new slot when it moved)
        std::vector<Dropped> dropped;         ///< the cards the new machine cannot take
        std::vector<std::string> lines;       ///< for people, one per card

        /// "zxbus.2 = multisound not carried: ..." lines joined; "" when nothing was dropped
        std::string DroppedText() const;
    };

    /// A model switch (R-OP-9): the cards of `from` (the old machine's plan) planned against the new machine of
    /// `config` (its INI loaded). The carried cards come first and keep their slot where the new machine has that bus
    /// (else they go where the planner suggests, behind the adapter that connects them); the new machine's own
    /// configured cards fill what the carried ones leave free and give way where they conflict; a carried card the
    /// new machine cannot take is dropped and reported. Writes the merged set into `config` (UseSlots)
    static CarryReport Carry(const Result& from, CONFIG& config);

    /// The General Sound personality as a slot change (R-OP-8): a plug of `gs` / `gs-lw` / `neogs` into the slot the
    /// fitted GS card is in, replacing it (the RAM option from [SOUND] GSRamSize / [NGS] RamSize, as a legacy key
    /// would give it). False with the reason when no General Sound card is fitted or the kind is not a personality
    static bool GeneralSoundRequest(const Result& current, const CONFIG& config, GSTypeKind kind,
                                    slots::SlotRequest& request, std::string* error = nullptr);
    /// The frame-boundary personality switch of a running machine (SoundManager::switchGeneralSoundCard) asks first:
    /// why the plan refuses it ("" when it may); it may only replace the card in the GS slot
    std::string GeneralSoundSwitchRefusal(GSTypeKind kind) const;
    /// ... and reports it done: the plan's GS slot names the new personality, and so does the TTD fingerprint
    void FollowGeneralSoundSwitch(GSTypeKind kind);

    /// The ZX-bus network cards fitted (networkspec::kCardZxNetUsb | kCardZxWifi)
    static uint8_t NetworkCardsOf(const Result& current);
    /// The network settings' `card` as slot changes (owner decision Q11): a remove for every fitted ZX-bus network
    /// card not in `zxBusCards`, then a plug (into the slot the planner suggests) for every wanted one not fitted.
    /// Empty when nothing changes. False with the reason when the machine has no slot declaration
    static bool NetworkRequests(const Result& current, uint8_t zxBusCards, std::vector<slots::SlotRequest>& out,
                                std::string* error = nullptr);
    /// Several requests planned one after the other into one change (one restart): each is planned against the set the
    /// previous one leaves; the first refusal stops. The merged plan lists every step's removals, shadowed devices,
    /// lost functions and media; its op / slot / card are the last plug's (else the last step's). Pure
    static ChangePlan PlanChanges(const Result& current, const CONFIG& config,
                                  const std::vector<slots::SlotRequest>& requests, const slots::PlanContext& context = {});
    /// The same for this instance: the media with unsaved writes and the TTD guard (R-OP-7), as PlanChange
    ChangePlan PlanChanges(const std::vector<slots::SlotRequest>& requests) const;


    // endregion

    explicit SlotManager(EmulatorContext* context);
    ~SlotManager();
    SlotManager(const SlotManager&) = delete;
    SlotManager& operator=(const SlotManager&) = delete;

    /// Create time (Core::Init, before any card exists): plans, logs and applies. False when the configured entries
    /// conflict (Q8): nothing is applied, Refusal() says why
    [[nodiscard]] bool PlanAtCreate();
    /// Why the create-time plan refused the machine; "" when it did not
    std::string Refusal() const
    {
        return _result.Refusal();
    }

    /// Create time, once the sound manager and the port decoder exist: builds the cards the slots own (card.h) for
    /// the fitted slots, in slot order. False when a card cannot be built (its factory fails or throws): none is
    /// left built, BuildError() names the slot, and the machine is not created
    [[nodiscard]] bool BuildCards();
    /// Why BuildCards failed; "" when it did not
    const std::string& BuildError() const
    {
        return _buildError;
    }
    /// Tests: BuildCards fails for every fitted card with this id, as a factory that throws would ("" = off)
    static void SetBuildFaultForTests(const std::string& card);
    /// Detaches and destroys the built cards (Core::Release, while the sound manager and the decoder still exist)
    void ReleaseCards();

    /// The cards the slots built, in slot order
    const std::vector<std::unique_ptr<ICard>>& Cards() const
    {
        return _cards;
    }
    /// The built card in a slot; nullptr when the slot holds none (or a card a legacy owner builds)
    ICard* FindCard(const std::string& slot) const;

    const Result& Current() const
    {
        return _result;
    }
    /// A copy of the slot set for another thread (a restart or a model switch plans from it while the machine runs)
    Result Snapshot() const
    {
        std::lock_guard<std::mutex> lock(_resultMutex);
        return _result;
    }

    // region <TTD (SL-5, slotttd.cpp; architecture.md §8)>

    /// The devices of a TTD checkpoint: the blob ids it holds and the devices fitted but deliberately not recorded
    /// (TTDPeripheralRegistry::MarkNotRecorded, the lightweight General Sound). Ids are ttd::PeripheralId values.
    /// The AY socket's `ay` and `ts` boards share one id (TurboSound); `socketCard` tells them apart where the
    /// device's engine instance names the socket's card (`ay-socket.ts`): the live registry and the engine's device
    /// table do, a v1 file does not ("" = not known, either board)
    struct TtdDeviceSet
    {
        std::vector<uint8_t> ids;
        uint64_t notRecorded = 0;   ///< bit per PeripheralId
        std::string socketCard;     ///< "ay" / "ts" / "tsfm" from the socket device's instance; "" when not known

        static TtdDeviceSet Of(const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs, uint64_t notRecorded);
        static TtdDeviceSet Of(const ttd::TTDPeripheralRegistry& registry);
        /// A session file of the engine: every device of its device table (each a key {type, instance} with its v1
        /// id), the not-recorded mask from the controller's facts
        static TtdDeviceSet Of(const ttd::TTDDeviceTable& devices, uint64_t notRecorded);
    };

    /// The slot set as configuration fingerprint fields (R-NF-2), `affectsRestore` (a checkpoint holds the devices of
    /// its slot set): `slots.<slot>` = FNV-1a 64 over the card id, every option's effective value and the adapter, per
    /// fitted slot; `slots.builtin.<id>` = 1 / 0 per switchable built-in. Disabled cards are not fitted, not listed
    static std::vector<std::pair<std::string, uint64_t>> TtdFingerprintFields(const Result& result);
    /// Adds this instance's fields (computed once, at creation; the built cards' own, such as the MultiSound's MIDI
    /// bank, once they are built) to a fingerprint
    void AddTtdFingerprint(ttd::TTDConfigFingerprint& fingerprint) const;

    /// The TTD device instance of a slot card's device: "<slot>.<module>" ("zxbus.1.neogs"), so two cards carrying
    /// one module type get two device keys; the AY socket's device is named by the socket's card ("ay-socket.ay",
    /// "ay-socket.ts", "ay-socket.tsfm": the `ay` and `ts` boards are one module under one id). "" when the plan fits
    /// no card of the group there (the device keeps its own name). Socket, General Sound and MoonSound: the groups
    /// with a device of their own
    static std::string TtdInstance(const Result& result, SlotCardGroup group, const std::string& module);
    std::string TtdInstance(SlotCardGroup group, const std::string& module) const
    {
        return TtdInstance(_result, group, module);
    }

    /// The registered slot-card devices against the plan: a card the plan fits has its device, a device has its
    /// card; a card the slots build themselves (card.h) has every device its CardType declares (none declared: it has
    /// no time-travel state, refused). The General Sound personality is the plan's card (the runtime switch moves the
    /// plan with it, FollowGeneralSoundSwitch). False with every difference in `why`
    static bool TtdDevicesMatchPlan(const Result& result, const TtdDeviceSet& live, std::string& why);

    /// The slot-set guard on a session load: the cards the recording held (its baseline checkpoint) against the
    /// cards of this machine, per slot (AY socket, General Sound card, MoonSound card). False with every difference
    /// listed in `why` ("ay-socket: recorded tsfm, this machine ay / ts; zxbus.1: ..."). The AY socket's `ay` and
    /// `ts` share one blob id: a v1 file cannot tell them apart (the configuration fingerprint can)
    static bool TtdSlotSetMatches(const Result& result, const TtdDeviceSet& recorded, const TtdDeviceSet& live,
                                  std::string& why);
    /// The same for this machine, then each slot-built card's own check (the MultiSound's MIDI bank), then its
    /// machine slots (the Sprinter's ISA population, the port decoder's TtdSessionMatches). `blobs` as stored (the
    /// baseline checkpoint's)
    bool TtdSessionMatches(const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs, uint64_t notRecordedMask,
                           const ttd::TTDPeripheralRegistry& live, std::string& why) const;
    /// The same with the recorded device set given (a session file of the engine names its devices in its device
    /// table, the socket's board included); `blobs` = the baseline's device states as blobs for the card checks
    bool TtdSessionMatches(const TtdDeviceSet& recorded, const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs,
                           const ttd::TTDPeripheralRegistry& live, std::string& why) const;
    /// The guard for a machine without a slot manager (no plan: the positions keep their names), then its port
    /// decoder's own check
    static bool TtdSessionMatchesWithoutSlots(EmulatorContext* context, const TtdDeviceSet& recorded,
                                              const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs,
                                              const ttd::TTDPeripheralRegistry& live, std::string& why);

    /// R-OP-7: why a slot change is refused now, naming the recording session; "" when allowed. The device set is
    /// fixed for a TTD session (D38): no slot change while a user recording runs (a debugger's live history is
    /// dropped by the change instead, as for the General Sound personality switch)
    std::string ChangeRefusal() const;

    // endregion </TTD>

private:
    /// The live context of a change plan: the media with unsaved writes
    slots::PlanContext LiveContext() const;
    /// The TTD guard on a plan (R-OP-7)
    void GuardRecording(ChangePlan& plan) const;

    static std::vector<Conflict> ConflictsOf(const slots::SlotPlan& plan, const Slot& slot, bool socketConfigured,
                                             const std::vector<Slot>& planned);

    EmulatorContext* _context = nullptr;
    Result _result;
    mutable std::mutex _resultMutex;   ///< Snapshot() against a change of _result while the machine runs
    std::string _buildError;
    std::vector<std::unique_ptr<ICard>> _cards;
    std::unique_ptr<slots::IClaimSignals> _signals;   ///< the claim table's view of M1 and DOS, while cards exist
    std::vector<std::pair<std::string, uint64_t>> _ttdFingerprint;   ///< TtdFingerprintFields(_result)
    std::vector<std::pair<std::string, uint64_t>> _ttdCardFingerprint;   ///< the built cards' own (ICard::TtdFingerprint)
};

#pragma pop_macro("signals")
#pragma pop_macro("slots")
