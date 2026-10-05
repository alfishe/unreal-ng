// SlotManager and time travel (ZX-bus slots SL-5, docs/inprogress/2026-10-03-zx-bus-slots/architecture.md §8): the slot
// set in the session's configuration fingerprint, slot-named device instances, the registry checked against the plan,
// the slot-set guard on a session load and the refusal of slot changes while a session records (R-OP-7, R-NF-2).

#include "emulator/slots/slotmanager.h"

#include "debugger/ttd/engine/ttdconfigfingerprint.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/card.h"

#include <algorithm>

using namespace slots;
using ttd::PeripheralId;

namespace
{

constexpr const char* kAySocket = "ay-socket";

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
/// card the slots build themselves (card.h: the ZX-MultiSound), known by its own device's id. A v1 checkpoint holds
/// one blob per id, so each position is known by the ids its cards save under
struct TtdPosition
{
    SlotCardGroup group;
    std::string label;   ///< when the plan names no slot for it
    struct Card
    {
        PeripheralId id;
        const char* name;
    };
    std::vector<Card> cards;
    const CardType* slotBuilt = nullptr;   ///< a slot-built card: its slot is the fitted entry of this card id
};

const std::vector<TtdPosition>& TtdPositions()
{
    static const std::vector<TtdPosition> positions = [] {
        std::vector<TtdPosition> list = {
            {SlotCardGroup::Socket, kAySocket, {{PeripheralId::TurboSound, "ay / ts"}, {PeripheralId::TSFM, "tsfm"}}},
            {SlotCardGroup::GeneralSound,
             "General Sound card",
             {{PeripheralId::GeneralSound, "gs"},
              {PeripheralId::GeneralSoundLightweight, "gs-lw"},
              {PeripheralId::NeoGS, "neogs"}}},
            {SlotCardGroup::MoonSound, "MoonSound card", {{PeripheralId::MoonSound, "moonsound"}}},
        };
        for (const CardType& type : CardTypes())
        {
            if (!type.ttdIds.empty())
            {
                list.push_back({SlotCardGroup::Count, std::string(type.id) + " card", {{type.ttdIds[0], type.id}}, &type});
            }
        }
        return list;
    }();
    return positions;
}

bool Holds(const SlotManager::TtdDeviceSet& set, PeripheralId id)
{
    const auto raw = static_cast<uint8_t>(id);
    return std::find(set.ids.begin(), set.ids.end(), raw) != set.ids.end() || ((set.notRecorded >> raw) & 1u) != 0;
}

/// The cards of a position a device set holds, as their names ("none" when none, "a + b" when a broken writer
/// stored two)
std::string CardsAt(const TtdPosition& position, const SlotManager::TtdDeviceSet& set)
{
    std::string names;
    for (const TtdPosition::Card& card : position.cards)
    {
        if (Holds(set, card.id))
        {
            names += (names.empty() ? "" : " + ") + std::string(card.name);
        }
    }
    return names.empty() ? "none" : names;
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

SlotManager::TtdDeviceSet SlotManager::TtdDeviceSet::Of(const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs,
                                                        uint64_t notRecorded)
{
    TtdDeviceSet set;
    for (const auto& [id, blob] : blobs)
    {
        if (!blob.empty())
        {
            set.ids.push_back(id);
        }
    }
    set.notRecorded = notRecorded;
    return set;
}

SlotManager::TtdDeviceSet SlotManager::TtdDeviceSet::Of(const ttd::TTDPeripheralRegistry& registry)
{
    TtdDeviceSet set;
    for (const auto& [id, device] : registry.Devices())
    {
        if (device != nullptr)
        {
            set.ids.push_back(id);
        }
    }
    set.notRecorded = registry.NotRecordedMask();
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
    return slot == nullptr ? std::string() : slot->entry.slot + "." + Lower(module);
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
        if (type->ttdIds.empty())
        {
            differences += (differences.empty() ? "" : "; ") + slot.entry.slot + ": " + slot.entry.card +
                           " has no time-travel state, a recording would lose it";
            continue;
        }
        for (const PeripheralId id : type->ttdIds)
        {
            if (!Holds(live, id))
            {
                differences += (differences.empty() ? "" : "; ") + slot.entry.slot + ": " + slot.entry.card +
                               " did not register its device " + std::to_string(static_cast<unsigned>(id));
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
        // The socket's board decides the id (ay / ts: TurboSound, tsfm: TSFM); the General Sound personality is the
        // plan's card (the runtime switch moves the plan with it, SL-6)
        if (matches && expectDevice && position.group == SlotCardGroup::Socket)
        {
            matches = (planned->entry.card == "tsfm") == (fitted == "tsfm");
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
        if (was == is && was.find(" + ") == std::string::npos)
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

bool SlotManager::TtdSessionMatches(const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs,
                                    uint64_t notRecordedMask, const ttd::TTDPeripheralRegistry& live,
                                    std::string& why) const
{
    if (!TtdSlotSetMatches(_result, TtdDeviceSet::Of(blobs, notRecordedMask), TtdDeviceSet::Of(live), why))
    {
        return false;
    }
    // The slot-built cards' own checks (configuration the session must share: the MultiSound's MIDI bank)
    for (const std::unique_ptr<ICard>& card : _cards)
    {
        if (!card->TtdSessionMatches(blobs, why))
        {
            return false;
        }
    }
    // The machine's own slots (the Sprinter's ISA slots): their population is compared by the board until SL-8
    // reports them in the slot set
    if (_context != nullptr && _context->pPortDecoder != nullptr)
    {
        return _context->pPortDecoder->TtdSessionMatches(blobs, why);
    }
    return true;
}

std::string SlotManager::ChangeRefusal() const
{
    if (_context == nullptr || _context->pTimeTravelManager == nullptr)
    {
        return {};
    }
    return _context->pTimeTravelManager->RecordingGuard(ttd::TTDGuardedAction::ChangeSlots);
}
