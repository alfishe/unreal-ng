// SlotManager and time travel (ZX-bus slots SL-5, docs/inprogress/2026-10-03-zx-bus-slots/architecture.md §8): the slot
// set in the session's configuration fingerprint, slot-named device instances, the registry checked against the plan,
// the slot-set guard on a session load and the refusal of slot changes while a session records (R-OP-7, R-NF-2).

#include "emulator/slots/slotmanager.h"

#include "debugger/ttd/engine/ttdconfigfingerprint.h"
#include "debugger/ttd/engine/ttddevicetable.h"
#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/coordinator/ports.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/card.h"

#include <algorithm>

using namespace slots;
using ttd::TTDDeviceType;

namespace
{

constexpr const char* kAySocket = "ay-socket";
/// The name a v1 file gives the socket's `ay` / `ts` board: the two share one id
constexpr const char* kAyOrTs = "ay / ts";

const SlotPlanner& TtdPlanner()
{
    static const SlotPlanner planner;
    return planner;
}

uint64_t Fnv1a(const std::string& text)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char c : text)
    {
        hash ^= static_cast<uint8_t>(c);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

/// A card position with a device of its own: the AY socket, the General Sound card, the MoonSound card, and one per
/// card the slots build themselves (card.h: the ZX-MultiSound), known by its own device's type. Each position is
/// known by the types of the cards it takes
struct TtdPosition
{
    SlotCardGroup group;
    std::string label;   ///< when the plan names no slot for it
    struct Card
    {
        TTDDeviceType type;
        const char* name;
    };
    std::vector<Card> cards;
    const CardType* slotBuilt = nullptr;   ///< a slot-built card: its slot is the fitted entry of this card id
};

const std::vector<TtdPosition>& TtdPositions()
{
    static const std::vector<TtdPosition> positions = [] {
        std::vector<TtdPosition> list = {
            {SlotCardGroup::Socket, kAySocket, {{TTDDeviceType::TurboSound, kAyOrTs}, {TTDDeviceType::TSFM, "tsfm"}}},
            {SlotCardGroup::GeneralSound,
             "General Sound card",
             {{TTDDeviceType::GeneralSound, "gs"},
              {TTDDeviceType::GeneralSoundLightweight, "gs-lw"},
              {TTDDeviceType::NeoGS, "neogs"}}},
            {SlotCardGroup::MoonSound, "MoonSound card", {{TTDDeviceType::MoonSound, "moonsound"}}},
        };
        for (const CardType& type : CardTypes())
        {
            if (!type.ttdTypes.empty())
            {
                list.push_back({SlotCardGroup::Count, std::string(type.id) + " card", {{type.ttdTypes[0], type.id}}, &type});
            }
        }
        return list;
    }();
    return positions;
}

/// The socket's card an engine instance names ("ay-socket.ts" -> "ts"); "" for any other instance (a device
/// without a socket entry keeps its own name, "turbosound": either board)
std::string SocketCardOf(const std::string& instance)
{
    const std::string prefix = std::string(kAySocket) + ".";
    if (instance.compare(0, prefix.size(), prefix) != 0)
    {
        return {};
    }
    const std::string card = instance.substr(prefix.size());
    return card == "ay" || card == "ts" || card == "tsfm" ? card : std::string();
}

/// The cards of a position a device set holds, as their names ("none" when none, "a + b" when a broken writer
/// stored two)
std::string CardsAt(const TtdPosition& position, const SlotManager::TtdDeviceSet& set)
{
    std::string names;
    for (const TtdPosition::Card& card : position.cards)
    {
        if (set.Holds(card.type))
        {
            // The socket's TurboSound type is the `ay` or the `ts` board: named where the instance says which
            const bool board = card.type == TTDDeviceType::TurboSound && (set.socketCard == "ay" || set.socketCard == "ts");
            names += (names.empty() ? "" : " + ") + (board ? set.socketCard : std::string(card.name));
        }
    }
    return names.empty() ? "none" : names;
}

/// The same cards on both sides; a side that cannot tell the socket's `ay` from its `ts` (a v1 file) matches either
bool SameCards(const std::string& was, const std::string& is)
{
    if (was == is)
    {
        return true;
    }
    auto board = [](const std::string& name) { return name == "ay" || name == "ts"; };
    return (was == kAyOrTs && board(is)) || (is == kAyOrTs && board(was));
}

/// The plan's fitted entry of a group (the AY socket's own entry for the socket); nullptr when none
const SlotManager::Slot* PlannedSlot(const SlotManager::Result& result, SlotCardGroup group)
{
    if (group == SlotCardGroup::Socket)
    {
        return result.FindSlot(kAySocket);
    }
    return result.FindGroup(group);
}

/// The plan's fitted entry of a position (a slot-built card: the first fitted slot with its card id)
const SlotManager::Slot* PlannedSlot(const SlotManager::Result& result, const TtdPosition& position)
{
    if (position.slotBuilt == nullptr)
    {
        return PlannedSlot(result, position.group);
    }
    for (const SlotManager::Slot& slot : result.entries)
    {
        if (!slot.entry.disabled && slot.entry.card == position.slotBuilt->id)
        {
            return &slot;
        }
    }
    return nullptr;
}

std::string Lower(std::string text)
{
    for (char& c : text)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

}  // namespace

bool SlotManager::TtdDeviceSet::Holds(TTDDeviceType type) const
{
    auto ofType = [type](const ttd::TTDDeviceKey& key) { return key.type == type; };
    return std::any_of(devices.begin(), devices.end(), ofType) || std::any_of(notRecorded.begin(), notRecorded.end(), ofType);
}

SlotManager::TtdDeviceSet SlotManager::TtdDeviceSet::Of(const ttd::TTDDeviceStates& states,
                                                        const std::vector<ttd::TTDDeviceKey>& notRecorded)
{
    TtdDeviceSet set;
    for (const auto& [key, state] : states)
    {
        if (!state.empty())
        {
            set.devices.push_back(key);
        }
    }
    set.notRecorded = notRecorded;
    return set;
}

SlotManager::TtdDeviceSet SlotManager::TtdDeviceSet::Of(const ttd::TTDPeripheralRegistry& registry)
{
    TtdDeviceSet set;
    for (const ttd::TTDRegistryEntry& entry : registry.Entries())
    {
        if (entry.device != nullptr)
        {
            set.devices.push_back(entry.key);
        }
    }
    set.notRecorded = registry.NotRecorded();
    set.socketCard = SocketCardOf(registry.InstanceOf(TTDDeviceType::TurboSound));
    if (set.socketCard.empty())
    {
        set.socketCard = SocketCardOf(registry.InstanceOf(TTDDeviceType::TSFM));
    }
    return set;
}

SlotManager::TtdDeviceSet SlotManager::TtdDeviceSet::Of(const ttd::TTDDeviceTable& devices,
                                                        const std::vector<ttd::TTDDeviceKey>& notRecorded)
{
    TtdDeviceSet set;
    for (const ttd::TTDDeviceEntry& entry : devices.Entries())
    {
        set.devices.push_back(entry.descriptor.Key());
        if (entry.descriptor.type == TTDDeviceType::TurboSound || entry.descriptor.type == TTDDeviceType::TSFM)
        {
            set.socketCard = SocketCardOf(entry.descriptor.instance);
        }
    }
    set.notRecorded = notRecorded;
    return set;
}

std::vector<std::pair<std::string, uint64_t>> SlotManager::TtdFingerprintFields(const Result& result)
{
    std::vector<std::pair<std::string, uint64_t>> fields;
    for (const Slot& slot : result.entries)
    {
        if (slot.entry.disabled)
        {
            continue;
        }
        const CardDef* card = TtdPlanner().FindCard(slot.entry.card);
        const std::string options = card != nullptr ? FormatCardOptions(*card, slot.entry.options) : std::string();
        fields.emplace_back("slots." + slot.entry.slot, Fnv1a(slot.entry.card + "|" + options + "|" + slot.entry.adapter));
    }
    for (const BuiltIn& builtIn : result.builtIns)
    {
        if (builtIn.kind == BuiltInKind::Switchable)
        {
            fields.emplace_back("slots.builtin." + builtIn.id, builtIn.on ? 1u : 0u);
        }
    }
    return fields;
}

void SlotManager::AddTtdFingerprint(ttd::TTDConfigFingerprint& fingerprint) const
{
    for (const auto& [name, value] : _ttdFingerprint)
    {
        fingerprint.Add(name, value, true);
    }
    for (const auto& [name, value] : _ttdCardFingerprint)
    {
        fingerprint.Add(name, value, true);
    }
}

std::string SlotManager::TtdInstance(const Result& result, SlotCardGroup group, const std::string& module)
{
    const Slot* slot = result.machine != nullptr ? PlannedSlot(result, group) : nullptr;
    if (slot == nullptr)
    {
        return {};
    }
    // The socket's boards: `ay` and `ts` are one module (TurboSound) under one id, the card tells them apart
    return slot->entry.slot + "." + (group == SlotCardGroup::Socket ? slot->entry.card : Lower(module));
}

bool SlotManager::TtdDevicesMatchPlan(const Result& result, const TtdDeviceSet& live, std::string& why)
{
    std::string differences;
    // Every slot-built card records its state: all of its devices registered, or refused naming the slot (a card
    // without time-travel state would be silently missing from every checkpoint)
    for (const Slot& slot : result.entries)
    {
        const CardType* type = slot.entry.disabled ? nullptr : FindCardType(slot.entry.card);
        if (type == nullptr)
        {
            continue;
        }
        if (type->ttdTypes.empty())
        {
            differences += (differences.empty() ? "" : "; ") + slot.entry.slot + ": " + slot.entry.card +
                           " has no time-travel state, a recording would lose it";
            continue;
        }
        for (const TTDDeviceType deviceType : type->ttdTypes)
        {
            if (!live.Holds(deviceType))
            {
                differences += (differences.empty() ? "" : "; ") + slot.entry.slot + ": " + slot.entry.card +
                               " did not register its device of type " + std::to_string(static_cast<unsigned>(deviceType));
            }
        }
    }
    for (const TtdPosition& position : TtdPositions())
    {
        const Slot* planned = PlannedSlot(result, position);
        const std::string fitted = CardsAt(position, live);
        // The AY socket without an entry holds the machine's own chip (or none): nothing to compare
        if (position.group == SlotCardGroup::Socket && planned == nullptr)
        {
            continue;
        }
        bool expectDevice = planned != nullptr;
        if (position.group == SlotCardGroup::Socket && planned != nullptr && planned->entry.card == "none")
        {
            expectDevice = false;
        }
        bool matches = expectDevice == (fitted != "none");
        // The socket's board decides the type (ay / ts: TurboSound, tsfm: TSFM) and the instance names the board;
        // the General Sound personality is the plan's card (the runtime switch moves the plan with it, SL-6)
        if (matches && expectDevice && position.group == SlotCardGroup::Socket)
        {
            matches = (planned->entry.card == "tsfm") == (fitted == "tsfm") && SameCards(planned->entry.card, fitted);
        }
        if (matches && expectDevice && position.group == SlotCardGroup::GeneralSound)
        {
            matches = fitted == planned->entry.card;
        }
        if (!matches)
        {
            const std::string where = planned != nullptr ? planned->entry.slot : position.label;
            differences += (differences.empty() ? "" : "; ") + where + ": the plan fits " +
                           (planned != nullptr ? planned->entry.card : std::string("none")) + ", the device is " + fitted;
        }
    }
    if (differences.empty())
    {
        return true;
    }
    why = "the slot-card devices differ from the slot set: " + differences;
    return false;
}

bool SlotManager::TtdSlotSetMatches(const Result& result, const TtdDeviceSet& recorded, const TtdDeviceSet& live,
                                    std::string& why)
{
    std::string differences;
    for (const TtdPosition& position : TtdPositions())
    {
        const std::string was = CardsAt(position, recorded);
        const std::string is = CardsAt(position, live);
        // Two cards in one position cannot come from a healthy writer: refused rather than guessing which to trust
        if (SameCards(was, is) && was.find(" + ") == std::string::npos)
        {
            continue;
        }
        const Slot* planned = PlannedSlot(result, position);
        const std::string where = planned != nullptr ? planned->entry.slot : position.label;
        differences += (differences.empty() ? "" : "; ") + where + ": recorded " + was + ", this machine " + is;
    }
    if (differences.empty())
    {
        return true;
    }
    why = "slot set differs from the recording: " + differences +
          " - fit the recorded cards ([SLOTS]) and restart, then load the session again";
    return false;
}

bool SlotManager::TtdSessionMatchesWithoutSlots(EmulatorContext* context, const TtdDeviceSet& recorded,
                                                const ttd::TTDDeviceStates& states,
                                                const ttd::TTDPeripheralRegistry& live, std::string& why)
{
    return TtdSlotSetMatches({}, recorded, TtdDeviceSet::Of(live), why) &&
           (context == nullptr || context->pPortDecoder == nullptr || context->pPortDecoder->TtdSessionMatches(states, why));
}

bool SlotManager::TtdSessionMatches(const TtdDeviceSet& recorded, const ttd::TTDDeviceStates& states,
                                    const ttd::TTDPeripheralRegistry& live, std::string& why) const
{
    if (!TtdSlotSetMatches(_result, recorded, TtdDeviceSet::Of(live), why))
    {
        return false;
    }
    // The slot-built cards' own checks (configuration the session must share: the MultiSound's MIDI bank)
    for (const std::unique_ptr<ICard>& card : _cards)
    {
        if (!card->TtdSessionMatches(states, why))
        {
            return false;
        }
    }
    // The machine's own slots (the Sprinter's ISA slots): the board compares their population (the ISA state)
    if (_context != nullptr && _context->pPortDecoder != nullptr)
    {
        return _context->pPortDecoder->TtdSessionMatches(states, why);
    }
    return true;
}

std::string SlotManager::ChangeRefusal() const
{
    // The coordinator's rules (OpKind::SlotChange: TTD v1 refuses while a user recording runs)
    if (_context == nullptr || _context->pMachineOps == nullptr)
    {
        return {};
    }
    coordinator::MachineOp op;
    op.kind = coordinator::OpKind::SlotChange;
    return _context->pMachineOps->WouldRefuse(op).reason;
}
