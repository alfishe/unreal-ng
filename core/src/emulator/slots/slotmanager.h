#pragma once

/// @file slotmanager.h
/// @brief The slot set of one emulator instance (docs/inprogress/2026-10-03-zx-bus-slots/architecture.md §5-§7):
/// planned once when the instance is created, from the INI's [SLOTS] section or, without one, from the legacy card
/// keys translated into slots. The same plan engine (SlotPlanner) as an automation request decides, without the
/// replace flag: entries are taken in slot order, the first card wins and a later one that would displace it is
/// disabled with the reason (R-CFG-3). A slot with the fit override (`<slot>.fit = unrealistic`; every translated
/// legacy key, so an old INI keeps the devices it always had) may fit a card the bus cannot take, never displace one.
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
/// No slot change in a running machine (Q6): SL-6 applies a change by restarting the instance.

// Qt defines `slots` / `signals` as macros; this header names the slots namespace
#pragma push_macro("slots")
#pragma push_macro("signals")
#undef slots
#undef signals

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "emulator/slots/slotconfig.h"
#include "emulator/slots/slotplanner.h"

struct CONFIG;
class EmulatorContext;
class ICard;

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

    struct Result
    {
        MEM_MODEL model{};
        const slots::MachineDef* machine = nullptr;   ///< nullptr: the model has no slot declaration
        bool fromSlotsSection = false;                ///< the INI's [SLOTS] (else: translated legacy keys)
        std::vector<Slot> entries;                    ///< every configured slot, in slot order
        std::vector<BuiltIn> builtIns;
        std::vector<std::string> log;                 ///< deprecation and refusal lines (logged as warnings)
        std::vector<std::string> info;                ///< cards fitted with the fit override (logged as info)

        /// The fitted (not disabled) cards as the plan engine's slot set
        slots::SlotSet Fitted() const;
        /// The fitted entry of a slot / of the first card of a group; nullptr when none
        const Slot* FindSlot(const std::string& slot) const;
        const Slot* FindGroup(SlotCardGroup group) const;
        const BuiltIn* FindBuiltIn(const std::string& id) const;
    };

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

    explicit SlotManager(EmulatorContext* context);
    ~SlotManager();
    SlotManager(const SlotManager&) = delete;
    SlotManager& operator=(const SlotManager&) = delete;

    /// Create time (Core::Init, before any card exists): plans, logs and applies
    void PlanAtCreate();

    /// Create time, once the sound manager and the port decoder exist: builds the cards the slots own (card.h) for
    /// the fitted slots, in slot order
    void BuildCards();
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

private:
    EmulatorContext* _context = nullptr;
    Result _result;
    std::vector<std::unique_ptr<ICard>> _cards;
};

#pragma pop_macro("signals")
#pragma pop_macro("slots")
