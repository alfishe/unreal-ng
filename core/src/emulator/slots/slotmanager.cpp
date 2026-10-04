#include "slotmanager.h"

#include <algorithm>
#include <map>
#include <utility>

#include "common/modulelogger.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/network/networkspec.h"
#include "emulator/platform.h"
#include "emulator/slots/slotvocabulary.h"

using namespace slots;

namespace
{

constexpr const char* kAySocket = "ay-socket";
constexpr const char* kBoardCovox = "covox";

const SlotPlanner& Planner()
{
    static const SlotPlanner planner;
    return planner;
}

// region <Card fields>

/// The CONFIG fields the card groups stand for (what the device code reads)
struct CardFields
{
    TurboSoundKind turboSound = TurboSoundKind::AY;
    GSTypeKind gs = GSTypeKind::NONE;
    bool moonSound = false;
    bool covoxFb = false;
    bool sd = false;
    uint8_t sdMode = 0;         ///< the SounDrive card's port set: 0 both, 1, 2
    uint8_t zxBusNetwork = 0;   ///< networkspec::kCardZxNetUsb | kCardZxWifi

    static CardFields Read(const CONFIG& config)
    {
        CardFields fields;
        fields.turboSound = config.sound.turboSoundKind;
        fields.gs = config.sound.gsTypeKind;
        fields.moonSound = config.sound.moonsound != 0;
        fields.covoxFb = config.sound.covoxFB != 0;
        fields.sd = config.sound.sd != 0;
        fields.sdMode = config.sound.sdMode;
        fields.zxBusNetwork = static_cast<uint8_t>(config.network.card & ~networkspec::kCardAtm2IoEsp);
        return fields;
    }

    bool SameGroup(const CardFields& other, SlotCardGroup group) const
    {
        switch (group)
        {
            case SlotCardGroup::Socket:
                return turboSound == other.turboSound;
            case SlotCardGroup::GeneralSound:
                return gs == other.gs;
            case SlotCardGroup::MoonSound:
                return moonSound == other.moonSound;
            case SlotCardGroup::Covox:
                return covoxFb == other.covoxFb && sd == other.sd && (!sd || sdMode == other.sdMode);
            case SlotCardGroup::Network:
                return zxBusNetwork == other.zxBusNetwork;
            case SlotCardGroup::Count:
                break;
        }
        return true;
    }

    void Write(CONFIG& config, uint32_t groups) const
    {
        if (groups & SlotGroupBit(SlotCardGroup::Socket))
        {
            config.sound.turboSoundKind = turboSound;
        }
        if (groups & SlotGroupBit(SlotCardGroup::GeneralSound))
        {
            config.sound.gsTypeKind = gs;
        }
        if (groups & SlotGroupBit(SlotCardGroup::MoonSound))
        {
            config.sound.moonsound = moonSound ? 1 : 0;
        }
        if (groups & SlotGroupBit(SlotCardGroup::Covox))
        {
            config.sound.covoxFB = covoxFb ? 1 : 0;
            config.sound.sd = sd ? 1 : 0;
            config.sound.sdMode = sd ? sdMode : 0;
        }
        if (groups & SlotGroupBit(SlotCardGroup::Network))
        {
            config.network.card = static_cast<uint8_t>((config.network.card & networkspec::kCardAtm2IoEsp) | zxBusNetwork);
        }
    }
};

TurboSoundKind SocketKindOf(const std::string& card)
{
    if (card == "ts")
    {
        return TurboSoundKind::AY;   // the two-AY TurboSound (the legacy "AY" kind)
    }
    if (card == "tsfm")
    {
        return TurboSoundKind::FM;
    }
    if (card == "ay")
    {
        return TurboSoundKind::Single;
    }
    return TurboSoundKind::None;
}

const char* SocketCardOf(TurboSoundKind kind)
{
    switch (kind)
    {
        case TurboSoundKind::AY:
            return "ts";
        case TurboSoundKind::FM:
            return "tsfm";
        case TurboSoundKind::Single:
            return "ay";
        case TurboSoundKind::None:
            break;
    }
    return "none";
}

const char* SocketKeyValue(TurboSoundKind kind)
{
    switch (kind)
    {
        case TurboSoundKind::AY:
            return "AY";
        case TurboSoundKind::FM:
            return "FM";
        case TurboSoundKind::Single:
            return "Single";
        case TurboSoundKind::None:
            break;
    }
    return "None";
}

GSTypeKind GsKindOf(const std::string& card)
{
    if (card == "gs")
    {
        return GSTypeKind::Z80;
    }
    if (card == "gs-lw")
    {
        return GSTypeKind::LW;
    }
    if (card == "neogs")
    {
        return GSTypeKind::NGS;
    }
    return GSTypeKind::NONE;
}

/// The SounDrive mode of an options text ("mode=2"): 0 both, 1, 2; not given = the card's default, mode 1
uint8_t SdModeOf(const std::string& options)
{
    const size_t at = options.find("mode=");
    if (at == std::string::npos)
    {
        return 1;   // the card's default (refdata: mode 1)
    }
    const std::string value = options.substr(at + 5, options.find(' ', at) == std::string::npos
                                                         ? std::string::npos
                                                         : options.find(' ', at) - at - 5);
    return value == "2" ? 2 : value == "both" ? 0 : 1;
}

/// The fields a slot configuration stands for without a machine (a missing AY socket = the machine's own chip)
CardFields ProjectFields(const SlotConfig& slotConfig, const CONFIG& config)
{
    CardFields fields = CardFields::Read(config);
    fields.turboSound = TurboSoundKind::Single;
    fields.gs = GSTypeKind::NONE;
    fields.moonSound = false;
    fields.covoxFb = false;
    fields.sd = false;
    fields.zxBusNetwork = 0;
    for (const SlotConfigEntry& entry : slotConfig.entries)
    {
        if (entry.slot == kAySocket)
        {
            fields.turboSound = SocketKindOf(entry.card);
        }
        else if (GsKindOf(entry.card) != GSTypeKind::NONE && fields.gs == GSTypeKind::NONE)
        {
            fields.gs = GsKindOf(entry.card);
        }
        else if (entry.card == "moonsound")
        {
            fields.moonSound = true;
        }
        else if (entry.card == "covox-fb")
        {
            fields.covoxFb = true;
        }
        else if (entry.card == "soundrive")
        {
            fields.sd = true;
            fields.sdMode = SdModeOf(entry.options);
        }
        else if (entry.card == "zxnetusb")
        {
            fields.zxBusNetwork |= networkspec::kCardZxNetUsb;
        }
        else if (entry.card == "zx-wifi")
        {
            fields.zxBusNetwork |= networkspec::kCardZxWifi;
        }
    }
    for (const SlotBuiltInSwitch& builtIn : slotConfig.builtIns)
    {
        if (builtIn.id == kBoardCovox && builtIn.on)
        {
            fields.covoxFb = true;
        }
    }
    return fields;
}

// endregion

// region <Helpers>

bool StartsWith(const std::string& text, const std::string& prefix)
{
    return text.compare(0, prefix.size(), prefix) == 0;
}

const BuiltInDef* FindBuiltIn(const MachineDef* machine, std::string_view id)
{
    if (machine == nullptr)
    {
        return nullptr;
    }
    for (const BuiltInDef& builtIn : machine->builtIns)
    {
        if (id == builtIn.id)
        {
            return &builtIn;
        }
    }
    return nullptr;
}

/// The built-in the AY socket holds by default (`ay`); nullptr when the machine has none
const BuiltInDef* SocketDefault(const MachineDef* machine)
{
    if (machine == nullptr)
    {
        return nullptr;
    }
    for (const BuiltInDef& builtIn : machine->builtIns)
    {
        if (builtIn.socket != nullptr && std::string_view(builtIn.socket) == kAySocket)
        {
            return &builtIn;
        }
    }
    return nullptr;
}

const BusDef* FindBus(const MachineDef* machine, std::string_view id)
{
    if (machine == nullptr)
    {
        return nullptr;
    }
    for (const BusDef& bus : machine->buses)
    {
        if (id == bus.id)
        {
            return &bus;
        }
    }
    return nullptr;
}

std::string BusOf(const std::string& slot)
{
    const size_t dot = slot.rfind('.');
    return dot == std::string::npos ? slot : slot.substr(0, dot);
}

int NumberOf(const std::string& slot)
{
    const size_t dot = slot.rfind('.');
    return dot == std::string::npos ? 0 : std::atoi(slot.c_str() + dot + 1);
}

/// Places translated cards: the slot the planner suggests ("<bus>.next" numbered here, in translation order) and,
/// when the card's bus is not the slot's, the adapter that connects them
class Placer
{
public:
    explicit Placer(const MachineDef* machine) : _machine(machine)
    {
    }

    void Place(const CardDef& card, SlotConfigEntry& entry)
    {
        std::string slot = _machine != nullptr ? Planner().SuggestSlot(*_machine, card) : std::string();
        if (slot.empty())
        {
            // The machine has no place for it: the plan refuses it with the reason
            slot = card.bus == BusKind::AySocket ? std::string(kAySocket) : std::string("zxbus.1");
        }
        const std::string next = ".next";
        if (slot.size() > next.size() && slot.compare(slot.size() - next.size(), next.size(), next) == 0)
        {
            const std::string bus = slot.substr(0, slot.size() - next.size());
            slot = bus + "." + std::to_string(++_used[bus]);
        }
        entry.slot = slot;
        const BusDef* bus = FindBus(_machine, BusOf(slot));
        if (bus != nullptr && bus->kind != card.bus)
        {
            for (const AdapterDef& adapter : Planner().Data().adapters)
            {
                if (adapter.cardSide == card.bus && adapter.machineSide == bus->kind)
                {
                    entry.adapter = adapter.id;
                    break;
                }
            }
        }
    }

private:
    const MachineDef* _machine = nullptr;
    std::map<std::string, int> _used;
};

/// A card the emulator cannot build (yet) with these options: the reason; empty when it can
std::string NotEmulated(const CardDef& card, const CardOptions& options)
{
    const std::string id = card.id;
    if (id == "multisound")
    {
        return "the ZX-MultiSound is not emulated yet (docs/inprogress/2026-10-03-zx-multisound)";
    }
#ifndef UNREALNG_HAVE_OPL4
    if (id == "moonsound")
    {
        return "this build has no OPL4 (MoonSound) support";
    }
#endif
    if (id == "covox-fb" && OptionBits(card, options, Opt::Decode) != Bit(0))
    {
        return "the A2-only Covox decode is not emulated (decode=full)";
    }
    if (id == "zx-wifi" && OptionBits(card, options, Opt::Port) != Bit(0))
    {
        return "the ZX-WiFi #EE build is not emulated (port=ef)";
    }
    return {};
}

std::string OptionValueId(const CardDef& card, const CardOptions& options, Opt key)
{
    const uint32_t bits = OptionBits(card, options, key);
    for (const OptionDef& option : card.options)
    {
        if (option.key != key)
        {
            continue;
        }
        for (size_t i = 0; i < option.values.size(); i++)
        {
            if (bits & Bit(static_cast<int>(i)))
            {
                return option.values[i].id;
            }
        }
    }
    return {};
}

// endregion

} // namespace

// region <Result>

SlotSet SlotManager::Result::Fitted() const
{
    SlotSet set;
    for (const Slot& slot : entries)
    {
        if (!slot.entry.disabled)
        {
            set.push_back(slot.entry);
        }
    }
    return set;
}

const SlotManager::Slot* SlotManager::Result::FindSlot(const std::string& slot) const
{
    for (const Slot& entry : entries)
    {
        if (entry.entry.slot == slot && !entry.entry.disabled)
        {
            return &entry;
        }
    }
    return nullptr;
}

const SlotManager::Slot* SlotManager::Result::FindGroup(SlotCardGroup group) const
{
    for (const Slot& entry : entries)
    {
        if (entry.group == group && !entry.entry.disabled && entry.entry.slot != kAySocket)
        {
            return &entry;
        }
    }
    return nullptr;
}

const SlotManager::BuiltIn* SlotManager::Result::FindBuiltIn(const std::string& id) const
{
    for (const BuiltIn& builtIn : builtIns)
    {
        if (builtIn.id == id)
        {
            return &builtIn;
        }
    }
    return nullptr;
}

// endregion

// region <Translation, projection>

SlotCardGroup SlotManager::GroupOf(const std::string& card)
{
    if (card == "ay" || card == "ts" || card == "tsfm" || card == "none")
    {
        return SlotCardGroup::Socket;
    }
    if (card == "gs" || card == "gs-lw" || card == "neogs")
    {
        return SlotCardGroup::GeneralSound;
    }
    if (card == "moonsound")
    {
        return SlotCardGroup::MoonSound;
    }
    if (card == "covox-fb" || card == "soundrive")
    {
        return SlotCardGroup::Covox;
    }
    if (card == "zxnetusb" || card == "zx-wifi")
    {
        return SlotCardGroup::Network;
    }
    return SlotCardGroup::Count;
}

SlotConfig SlotManager::TranslateLegacy(const CONFIG& config, uint32_t groups)
{
    SlotConfig out;
    const MachineDef* machine = Planner().FindMachine(config.mem_model);
    Placer placer(machine);

    auto add = [&](const char* cardId, std::string options, std::string source) {
        const CardDef* card = Planner().FindCard(cardId);
        SlotConfigEntry entry;
        entry.card = cardId;
        entry.options = std::move(options);
        entry.fitOverride = true;
        entry.source = std::move(source);
        if (card != nullptr)
        {
            placer.Place(*card, entry);
        }
        out.entries.push_back(std::move(entry));
    };

    if (groups & SlotGroupBit(SlotCardGroup::Socket))
    {
        const TurboSoundKind kind = config.sound.turboSoundKind;
        SlotConfigEntry entry;
        entry.slot = kAySocket;
        entry.card = SocketCardOf(kind);
        entry.fitOverride = true;
        entry.source = std::string("[SOUND] TurboSound=") + SocketKeyValue(kind);
        out.entries.push_back(std::move(entry));
    }

    if (groups & SlotGroupBit(SlotCardGroup::GeneralSound))
    {
        switch (config.sound.gsTypeKind)
        {
            case GSTypeKind::Z80:
            {
                const unsigned kb = config.sound.gsRamKB;
                add("gs", kb >= 512 ? "ram=512k" : kb >= 256 ? "ram=256k" : "ram=128k", "[SOUND] GSType=Z80");
                break;
            }
            case GSTypeKind::LW:
            case GSTypeKind::BASS:
                add("gs-lw", {}, "[SOUND] GSType=LW");
                break;
            case GSTypeKind::NGS:
                add("neogs", config.ngs.ramKB >= 4096 ? "ram=4m" : "ram=2m", "[SOUND] GSType=NGS");
                break;
            case GSTypeKind::NONE:
                break;
        }
    }

    if ((groups & SlotGroupBit(SlotCardGroup::MoonSound)) && config.sound.moonsound)
    {
        add("moonsound", {}, "[SOUND] MoonSound=1");
    }

    if (groups & SlotGroupBit(SlotCardGroup::Covox))
    {
        // A machine with a Covox of its own (ATM, ZX-Evo, TS-Conf, Profi): CovoxFB switches that one. SD fits the
        // SounDrive card in the emulator's decode of both port sets (mode=both), which also answers #FB
        const bool board = FindBuiltIn(machine, kBoardCovox) != nullptr;
        if (board)
        {
            out.builtIns.push_back({ kBoardCovox, config.sound.covoxFB != 0,
                                     std::string("[SOUND] CovoxFB=") + (config.sound.covoxFB ? "1" : "0") });
        }
        if (config.sound.sd)
        {
            const uint8_t mode = config.sound.sdMode;
            add("soundrive", mode == 1 ? "mode=1" : mode == 2 ? "mode=2" : "mode=both", "[SOUND] SD=1");
        }
        else if (config.sound.covoxFB && !board)
        {
            add("covox-fb", {}, "[SOUND] CovoxFB=1");
        }
    }

    if (groups & SlotGroupBit(SlotCardGroup::Network))
    {
        if (config.network.card & networkspec::kCardZxNetUsb)
        {
            add("zxnetusb", {}, "[NETWORK] Card=ZXNETUSB");
        }
        if (config.network.card & networkspec::kCardZxWifi)
        {
            add("zx-wifi", "port=ef", "[NETWORK] Card=ZXWIFI");
        }
    }
    return out;
}

void SlotManager::Project(const SlotConfig& slotConfig, CONFIG& config, uint32_t decidedGroups)
{
    ProjectFields(slotConfig, config).Write(config, decidedGroups);
}

// endregion

// region <Plan, apply>

SlotManager::Result SlotManager::Plan(const CONFIG& config, uint32_t decidedGroups)
{
    Result result;
    result.model = config.mem_model;
    result.machine = Planner().FindMachine(config.mem_model);
    const MachineDef* machine = result.machine;

    // What is asked for: [SLOTS], or the legacy fields translated
    SlotConfig requested;
    if (config.slotConfig.section)
    {
        requested = config.slotConfig;
        result.fromSlotsSection = true;
        for (const std::string& key : config.slotConfig.legacyKeys)
        {
            result.log.push_back("Config: " + key + " ignored: [SLOTS] names the cards");
        }
        // A card field code changed after the INI was read (the test runner's sound policy, a snapshot transfer):
        // that group's slots follow the field
        const CardFields actual = CardFields::Read(config);
        const CardFields projected = ProjectFields(config.slotConfig, config);
        for (uint32_t g = 0; g < static_cast<uint32_t>(SlotCardGroup::Count); g++)
        {
            const auto group = static_cast<SlotCardGroup>(g);
            if (!(decidedGroups & SlotGroupBit(group)) || actual.SameGroup(projected, group))
            {
                continue;
            }
            requested.entries.erase(std::remove_if(requested.entries.begin(), requested.entries.end(),
                                                   [group](const SlotConfigEntry& entry) {
                                                       return GroupOf(entry.card) == group;
                                                   }),
                                    requested.entries.end());
            if (group == SlotCardGroup::Covox)
            {
                requested.builtIns.erase(std::remove_if(requested.builtIns.begin(), requested.builtIns.end(),
                                                        [](const SlotBuiltInSwitch& s) { return s.id == kBoardCovox; }),
                                         requested.builtIns.end());
            }
            const SlotConfig translated = TranslateLegacy(config, SlotGroupBit(group));
            for (SlotConfigEntry entry : translated.entries)
            {
                // Numbered after the configured slots of the same bus
                if (entry.slot != kAySocket)
                {
                    const std::string bus = BusOf(entry.slot);
                    int highest = 0;
                    for (const SlotConfigEntry& other : requested.entries)
                    {
                        if (BusOf(other.slot) == bus)
                        {
                            highest = std::max(highest, NumberOf(other.slot));
                        }
                    }
                    entry.slot = bus + "." + std::to_string(highest + 1);
                }
                requested.entries.push_back(std::move(entry));
            }
            requested.builtIns.insert(requested.builtIns.end(), translated.builtIns.begin(), translated.builtIns.end());
        }
    }
    else
    {
        requested = TranslateLegacy(config);
        for (const std::string& key : config.slotConfig.legacyKeys)
        {
            std::string line = "Config: deprecated key " + key + " ->";
            bool any = false;
            for (const SlotConfigEntry& entry : requested.entries)
            {
                if (StartsWith(entry.source, key + "="))
                {
                    line += std::string(any ? "," : "") + " [SLOTS] " + entry.slot + " = " + entry.card +
                            (entry.options.empty() ? "" : " (" + entry.options + ")");
                    any = true;
                }
            }
            for (const SlotBuiltInSwitch& builtIn : requested.builtIns)
            {
                if (StartsWith(builtIn.source, key + "="))
                {
                    line += std::string(any ? "," : "") + " [SLOTS] builtin." + builtIn.id + " = " +
                            (builtIn.on ? "on" : "off");
                    any = true;
                }
            }
            result.log.push_back(line + (any ? "" : " no card"));
        }
    }
    for (const std::string& error : config.slotConfig.errors)
    {
        result.log.push_back("Config: [SLOTS] " + error);
    }

    // Built-in devices and their switches
    if (machine != nullptr)
    {
        for (const BuiltInDef& def : machine->builtIns)
        {
            BuiltIn builtIn;
            builtIn.id = def.id;
            builtIn.name = def.name;
            builtIn.kind = def.kind;
            builtIn.state = "active";
            result.builtIns.push_back(std::move(builtIn));
        }
    }
    for (const SlotBuiltInSwitch& sw : requested.builtIns)
    {
        BuiltIn* builtIn = nullptr;
        for (BuiltIn& candidate : result.builtIns)
        {
            builtIn = candidate.id == sw.id ? &candidate : builtIn;
        }
        if (builtIn == nullptr)
        {
            result.log.push_back("Config: " + sw.source + ": the machine has no built-in " + sw.id);
            continue;
        }
        if (builtIn->kind != BuiltInKind::Switchable)
        {
            result.log.push_back("Config: " + sw.source + ": " + builtIn->name + " cannot be switched off");
            continue;
        }
        builtIn->on = sw.on;
        builtIn->state = sw.on ? "active" : "switched off";
        builtIn->source = sw.source;
    }

    // Slot order: the AY socket first, then the machine's buses in declaration order, then the slot number
    auto busRank = [machine](const std::string& slot) {
        if (machine == nullptr)
        {
            return 0;
        }
        const std::string bus = BusOf(slot);
        for (size_t i = 0; i < machine->buses.size(); i++)
        {
            if (bus == machine->buses[i].id)
            {
                return static_cast<int>(i);
            }
        }
        return static_cast<int>(machine->buses.size());
    };
    std::stable_sort(requested.entries.begin(), requested.entries.end(),
                     [&](const SlotConfigEntry& a, const SlotConfigEntry& b) {
                         const bool aSocket = a.slot == kAySocket;
                         const bool bSocket = b.slot == kAySocket;
                         if (aSocket != bSocket)
                         {
                             return aSocket;
                         }
                         const int ra = busRank(a.slot);
                         const int rb = busRank(b.slot);
                         if (ra != rb)
                         {
                             return ra < rb;
                         }
                         return NumberOf(a.slot) < NumberOf(b.slot);
                     });

    // Plug the entries one by one, as an automation request without the replace flag would
    SlotSet set;
    for (const SlotConfigEntry& request : requested.entries)
    {
        Slot slot;
        slot.entry.slot = request.slot;
        slot.entry.card = request.card;
        slot.entry.adapter = request.adapter;
        slot.group = GroupOf(request.card);
        slot.source = request.source;
        slot.fitOverride = request.fitOverride;

        auto disable = [&](std::string reason) {
            slot.entry.disabled = true;
            slot.entry.disabledReason = std::move(reason);
            result.log.push_back("Slots: " + slot.entry.slot + " = " + slot.entry.card + " (" + slot.source +
                                 ") not fitted: " + slot.entry.disabledReason);
            result.entries.push_back(slot);
        };

        if (machine == nullptr)
        {
            disable("the model has no slot declaration");
            continue;
        }
        if (std::any_of(result.entries.begin(), result.entries.end(),
                        [&](const Slot& other) { return other.entry.slot == request.slot && !other.entry.disabled; }))
        {
            disable("the slot is taken");
            continue;
        }

        // The AY socket with its chip taken out
        if (request.card == kEmptySocket)
        {
            if (request.slot != kAySocket || FindBus(machine, kAySocket) == nullptr)
            {
                disable(request.slot != kAySocket ? "`none` empties the AY socket only"
                                                  : std::string(machine->name) + " has no AY socket");
                continue;
            }
            set.push_back(slot.entry);
            result.entries.push_back(slot);
            continue;
        }

        const CardDef* card = Planner().FindCard(request.card);
        if (card == nullptr)
        {
            disable("unknown card `" + request.card + "`");
            continue;
        }
        std::string error;
        if (!ParseCardOptions(*card, request.options, slot.entry.options, &error))
        {
            disable(error);
            continue;
        }
        const std::string missing = NotEmulated(*card, slot.entry.options);
        if (!missing.empty())
        {
            disable(missing);
            continue;
        }

        SlotRequest plug;
        plug.op = SlotRequest::Op::Plug;
        plug.slot = request.slot;
        plug.card = request.card;
        plug.options = slot.entry.options;
        plug.adapter = request.adapter;
        SlotPlan plan = Planner().Plan(result.model, set, plug);
        if (!plan.allowed && !plan.hardRefusal && request.fitOverride)
        {
            // The fit override: accepted when it only overrides the bus fit, never when it would displace a card,
            // switch a built-in off or take a chip out of its socket
            plug.replaceIfIncompatible = true;
            SlotPlan forced = Planner().Plan(result.model, set, plug);
            if (forced.allowed && forced.removed.empty() && forced.builtInSwitchedOff.empty() &&
                forced.removedFromSocket.empty())
            {
                plan = std::move(forced);
            }
        }
        if (!plan.allowed)
        {
            std::string reasons;
            for (const PlanReason& reason : plan.reasons)
            {
                reasons += (reasons.empty() ? "" : "; ") + reason.text;
            }
            disable(reasons.empty() ? std::string("refused") : reasons);
            continue;
        }

        set = plan.resultingSlots;
        slot.fit = plan.fit;
        for (const SlotEntry& planned : plan.resultingSlots)
        {
            if (planned.slot == plan.slot)
            {
                slot.entry = planned;
            }
        }
        for (const PlanReason& reason : plan.reasons)
        {
            slot.notes.push_back(reason.text);
        }
        for (const ShadowedDevice& shadowed : plan.shadowed)
        {
            for (BuiltIn& builtIn : result.builtIns)
            {
                if (builtIn.id == shadowed.device)
                {
                    builtIn.state = "shadowed by " + shadowed.by;
                }
            }
        }
        if (slot.entry.disabled)
        {
            result.log.push_back("Slots: " + slot.entry.slot + " = " + slot.entry.card + " (" + slot.source +
                                 ") not fitted: " + slot.entry.disabledReason);
        }
        if (slot.fit == Fit::Unrealistic)
        {
            result.info.push_back("Slots: " + slot.entry.slot + " = " + slot.entry.card + " (" + slot.source +
                                  ") fitted with the override (fit unrealistic): " +
                                 (slot.notes.empty() ? std::string() : slot.notes.front()));
        }
        result.entries.push_back(std::move(slot));
    }

    // The AY socket's own chip, when a board took its place or the socket is empty
    if (const BuiltInDef* socketDefault = SocketDefault(machine))
    {
        const Slot* socket = result.FindSlot(kAySocket);
        if (socket != nullptr && socket->entry.card != "ay")
        {
            for (BuiltIn& builtIn : result.builtIns)
            {
                if (builtIn.id == socketDefault->id)
                {
                    builtIn.state = socket->entry.card == kEmptySocket ? "socket empty"
                                                                        : "replaced by " + socket->entry.card;
                }
            }
        }
    }
    return result;
}

void SlotManager::Apply(const Result& result, CONFIG& config, uint32_t decidedGroups)
{
    if (result.machine == nullptr || decidedGroups == 0)
    {
        return;
    }
    CardFields fields = CardFields::Read(config);

    // AY socket: the fitted board, else the machine's own chip ([SLOTS] without the key), else nothing
    if (const Slot* socket = result.FindSlot(kAySocket))
    {
        fields.turboSound = SocketKindOf(socket->entry.card);
    }
    else
    {
        const bool configured = std::any_of(result.entries.begin(), result.entries.end(),
                                            [](const Slot& s) { return s.entry.slot == kAySocket; });
        fields.turboSound =
            !configured && SocketDefault(result.machine) != nullptr ? TurboSoundKind::Single : TurboSoundKind::None;
    }

    // General Sound: the personality from the card; its RAM from the card option when the slot names it
    const Slot* gs = result.FindGroup(SlotCardGroup::GeneralSound);
    fields.gs = gs != nullptr ? GsKindOf(gs->entry.card) : GSTypeKind::NONE;
    if (gs != nullptr && (decidedGroups & SlotGroupBit(SlotCardGroup::GeneralSound)) &&
        gs->entry.options.Has(Opt::Ram))
    {
        const CardDef* card = Planner().FindCard(gs->entry.card);
        const std::string ram = card != nullptr ? OptionValueId(*card, gs->entry.options, Opt::Ram) : std::string();
        if (gs->entry.card == "gs")
        {
            // The classic card is built with 128-512 K (larger boards clamp, as [SOUND] GSRamSize does)
            config.sound.gsRamKB = ram == "128k" ? 128u : ram == "256k" ? 256u : 512u;
        }
        else if (gs->entry.card == "neogs")
        {
            config.ngs.ramKB = ram == "4m" ? 4096u : 2048u;
        }
    }

    fields.moonSound = std::any_of(result.entries.begin(), result.entries.end(), [](const Slot& s) {
        return s.entry.card == "moonsound" && !s.entry.disabled;
    });

    const BuiltIn* boardCovox = result.FindBuiltIn(kBoardCovox);
    const bool covoxCard = std::any_of(result.entries.begin(), result.entries.end(), [](const Slot& s) {
        return s.entry.card == "covox-fb" && !s.entry.disabled;
    });
    fields.covoxFb = covoxCard || (boardCovox != nullptr && boardCovox->kind == BuiltInKind::Switchable && boardCovox->on);
    fields.sd = false;
    fields.sdMode = 0;
    for (const Slot& s : result.entries)
    {
        if (s.entry.card == "soundrive" && !s.entry.disabled)
        {
            const CardDef* card = Planner().FindCard(s.entry.card);
            const std::string mode = card != nullptr ? OptionValueId(*card, s.entry.options, Opt::Mode) : "both";
            fields.sd = true;
            fields.sdMode = mode == "1" ? 1 : mode == "2" ? 2 : 0;
        }
    }

    fields.zxBusNetwork = 0;
    for (const Slot& s : result.entries)
    {
        if (s.entry.disabled)
        {
            continue;
        }
        if (s.entry.card == "zxnetusb")
        {
            fields.zxBusNetwork |= networkspec::kCardZxNetUsb;
        }
        else if (s.entry.card == "zx-wifi")
        {
            fields.zxBusNetwork |= networkspec::kCardZxWifi;
        }
    }

    fields.Write(config, decidedGroups);
}

// endregion

// region <Instance>

SlotManager::SlotManager(EmulatorContext* context) : _context(context)
{
}

void SlotManager::PlanAtCreate()
{
    if (_context == nullptr)
    {
        return;
    }
    _result = Plan(_context->config);
    if (ModuleLogger* logger = _context->pModuleLogger)
    {
        for (const std::string& line : _result.log)
        {
            logger->Warning(PlatformModulesEnum::MODULE_CORE, PlatformCoreSubmodulesEnum::SUBMODULE_CORE_CONFIG, "%s",
                            line.c_str());
        }
        for (const std::string& line : _result.info)
        {
            logger->Info(PlatformModulesEnum::MODULE_CORE, PlatformCoreSubmodulesEnum::SUBMODULE_CORE_CONFIG, "%s",
                         line.c_str());
        }
    }
    Apply(_result, _context->config);
}

// endregion
