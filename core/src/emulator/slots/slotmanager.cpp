#include "slotmanager.h"

#include <algorithm>
#include <map>
#include <utility>

#include "common/modulelogger.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/network/networkspec.h"
#include "emulator/platform.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/card.h"
#include "emulator/slots/slotvocabulary.h"
#include "emulator/sound/soundmanager.h"

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

/// The bus signals the claim table reads for the slot-built cards' ROM-locked and DOS-gated claims: the IN / OUT
/// instruction's M1 address (the last opcode fetch before the I/O cycle) and the TR-DOS state
class SlotClaimSignals : public IClaimSignals
{
public:
    explicit SlotClaimSignals(EmulatorContext* context) : _context(context)
    {
    }
    uint16_t LastM1Address() const override
    {
        Z80* z80 = _context->pCore != nullptr ? _context->pCore->GetZ80() : nullptr;
        return z80 != nullptr ? z80->m1_pc : 0;
    }
    bool DosActive() const override
    {
        return (_context->emulatorState.flags & CF_TRDOS) != 0;
    }

private:
    EmulatorContext* _context;
};

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

/// What a rule says, for the refusal text (Q8)
const char* RuleMeaning(const std::string& rule)
{
    if (rule == "D1")
    {
        return "D1: one function, one card";
    }
    if (rule == "D3")
    {
        return "D3: a socket board under a card's IORQGE would be shadowed, a pointless pair";
    }
    if (rule == "D12")
    {
        return "D12: both would drive the same reads, a pointless pair";
    }
    if (rule == "D7")
    {
        return "D7: an accidental port clash";
    }
    return "Q7: the card needs the socketed chip out, `ay-socket = ay` keeps it in";
}

const char* RuleId(Rule rule)
{
    switch (rule)
    {
        case Rule::D3:
            return "D3";
        case Rule::D12:
            return "D12";
        default:
            break;
    }
    return "D1";
}

// endregion

} // namespace

/// The configured entries a plan for `slot` would displace or clash with (Q8); empty when none
std::vector<SlotManager::Conflict> SlotManager::ConflictsOf(const SlotPlan& plan, const Slot& slot,
                                                             bool socketConfigured, const std::vector<Slot>& planned)
{
    std::vector<Conflict> out;
    auto add = [&](const std::string& laterSlot, const std::string& otherSlot, const std::string& rule,
                   const std::string& why) {
        const std::string& other = laterSlot == slot.entry.slot ? otherSlot : laterSlot;
        if (std::any_of(out.begin(), out.end(), [&](const Conflict& c) { return c.withSlot == other; }))
        {
            return;
        }
        Conflict conflict;
        conflict.slot = slot.entry.slot;
        conflict.card = slot.entry.card;
        conflict.source = slot.source;
        conflict.withSlot = other;
        for (const Slot& entry : planned)
        {
            if (entry.entry.slot == other && !entry.entry.disabled)
            {
                conflict.withCard = entry.entry.card;
                conflict.withSource = entry.source;
            }
        }
        conflict.rule = rule;
        conflict.reason = why + " (" + RuleMeaning(rule) + ")";
        out.push_back(std::move(conflict));
    };
    for (const RemovedCard& removed : plan.removed)
    {
        add(slot.entry.slot, removed.slot, RuleId(removed.rule), removed.reason);
    }
    if (socketConfigured)
    {
        for (const RemovedFromSocket& chip : plan.removedFromSocket)
        {
            add(slot.entry.slot, chip.socket.empty() ? std::string(kAySocket) : chip.socket, "Q7",
                "the " + chip.chip + " would have to leave its socket");
        }
    }
    for (const DisabledCard& disabled : plan.disabled)
    {
        add(disabled.slot, disabled.clashSlot, "D7", disabled.reason);
    }
    // An entry nobody configured (an empty `withCard`) is no conflict between entries
    out.erase(std::remove_if(out.begin(), out.end(), [](const Conflict& c) { return c.withCard.empty(); }), out.end());
    return out;
}

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

std::string SlotManager::Result::Refusal() const
{
    if (conflicts.empty())
    {
        return {};
    }
    std::string text = "the [SLOTS] cards conflict, the machine is not created (Q8):";
    for (size_t i = 0; i < conflicts.size(); i++)
    {
        const Conflict& c = conflicts[i];
        text += std::string(i == 0 ? " " : "; ") + c.slot + " = " + c.card + " and " + c.withSlot + " = " + c.withCard +
                ": " + c.reason;
    }
    return text;
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
    const bool socketConfigured = std::any_of(requested.entries.begin(), requested.entries.end(),
                                              [](const SlotConfigEntry& entry) { return entry.slot == kAySocket; });
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

        // Q8: configured entries in conflict refuse the machine. A plan refused without the replace flag shows with
        // it what it would displace; an allowed plan may still disable a card for an accidental port clash
        SlotPlan forcedView;
        const SlotPlan* view = &plan;
        if (!plan.allowed && !plan.hardRefusal)
        {
            SlotRequest withFlag = plug;
            withFlag.replaceIfIncompatible = true;
            forcedView = Planner().Plan(result.model, set, withFlag);
            view = &forcedView;
        }
        const std::vector<Conflict> conflicts =
            view->hardRefusal ? std::vector<Conflict>{} : ConflictsOf(*view, slot, socketConfigured, result.entries);
        if (!conflicts.empty())
        {
            std::string with;
            for (const Conflict& conflict : conflicts)
            {
                with += (with.empty() ? "" : ", ") + conflict.withSlot + " = " + conflict.withCard;
            }
            result.conflicts.insert(result.conflicts.end(), conflicts.begin(), conflicts.end());
            disable("conflicts with " + with);
            continue;
        }

        if (!plan.allowed && !plan.hardRefusal && (request.fitOverride || !socketConfigured))
        {
            // Two confirmations a config may give without the replace flag, never a displaced card or a built-in
            // switched off:
            // - the fit override (`<slot>.fit = unrealistic`): the bus fit only;
            // - a socketed chip the config left in its socket by default (no `ay-socket` line) is taken out of it,
            //   the physical step a card that fights it needs (Q7: the ZX-Evo YM2149 under a ZX-MultiSound). An
            //   explicit `ay-socket = ay` keeps the chip, and the card that would need it out is not fitted
            plug.replaceIfIncompatible = true;
            SlotPlan forced = Planner().Plan(result.model, set, plug);
            const bool fitAccepted = request.fitOverride || (forced.fit != Fit::Unrealistic && forced.busFights.empty());
            const bool socketAccepted = forced.removedFromSocket.empty() || !socketConfigured;
            if (forced.allowed && forced.removed.empty() && forced.builtInSwitchedOff.empty() && fitAccepted &&
                socketAccepted)
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
        for (const RemovedFromSocket& removed : plan.removedFromSocket)
        {
            for (BuiltIn& builtIn : result.builtIns)
            {
                if (builtIn.id == removed.builtIn)
                {
                    builtIn.removed = true;
                    builtIn.state = "taken out of its socket for " + plan.slot;
                    builtIn.source = slot.source;
                }
            }
            result.info.push_back("Slots: " + slot.entry.slot + " = " + slot.entry.card + " (" + slot.source +
                                  ") takes the " + removed.chip + " (" + removed.builtIn + ") out of its socket");
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
        // The machine's own chip, unless a card took it out of its socket
        const BuiltInDef* chip = SocketDefault(result.machine);
        const BuiltIn* chipState = chip != nullptr ? result.FindBuiltIn(chip->id) : nullptr;
        const bool removed = chipState != nullptr && chipState->removed;
        fields.turboSound = !configured && chip != nullptr && !removed ? TurboSoundKind::Single : TurboSoundKind::None;
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

SlotManager::~SlotManager()
{
    ReleaseCards();
}

bool SlotManager::PlanAtCreate()
{
    if (_context == nullptr)
    {
        return true;
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
    if (!_result.conflicts.empty())
    {
        if (ModuleLogger* logger = _context->pModuleLogger)
        {
            logger->Error(PlatformModulesEnum::MODULE_CORE, PlatformCoreSubmodulesEnum::SUBMODULE_CORE_CONFIG, "Slots: %s",
                          _result.Refusal().c_str());
        }
        return false;
    }
    Apply(_result, _context->config);
    _ttdFingerprint = TtdFingerprintFields(_result);
    return true;
}

void SlotManager::BuildCards()
{
    ReleaseCards();
    if (_context == nullptr || _result.machine == nullptr)
    {
        return;
    }
    for (const Slot& slot : _result.entries)
    {
        const CardType* type = slot.entry.disabled ? nullptr : FindCardType(slot.entry.card);
        const CardDef* def = type != nullptr ? Planner().FindCard(slot.entry.card) : nullptr;
        if (def == nullptr)
        {
            continue;
        }
        CardContext cardContext;
        cardContext.emulator = _context;
        cardContext.def = def;
        cardContext.slot = slot.entry.slot;
        cardContext.options = slot.entry.options;
        std::unique_ptr<ICard> card = type->create(cardContext);
        if (card == nullptr)
        {
            continue;
        }

        // On the bus: the card's claims (with its options) in its slot order, resolved with the arbitration of the
        // bus its slot is on - or of the adapter between them, when the adapter brings its own IORQGE chain
        if (PortDecoder* decoder = _context->pPortDecoder)
        {
            const BusDef* bus = FindBus(_result.machine, BusOf(slot.entry.slot));
            Arbitration arbitration = bus != nullptr ? bus->arbitration : Arbitration::None;
            const ReadRule readRule = bus != nullptr ? bus->readRule : ReadRule::WiredAnd;
            const AdapterDef* adapter = slot.entry.adapter.empty() ? nullptr : Planner().FindAdapter(slot.entry.adapter);
            if (adapter != nullptr && adapter->hasArbitration)
            {
                arbitration = adapter->arbitration;
            }
            if (_cards.empty())
            {
                // One claim table per decoder: the first card's bus configures it (every bus of a machine that
                // takes slot-built cards today is the one ZX-bus)
                decoder->ConfigureSlotBus(arbitration, readRule,
                                          bus != nullptr ? bus->boardPorts : std::span<const PortClaim>{},
                                          _result.machine->builtIns);
                for (const BuiltIn& builtIn : _result.builtIns)
                {
                    const BuiltInDef* builtInDef = builtIn.removed ? FindBuiltIn(_result.machine, builtIn.id) : nullptr;
                    if (builtInDef != nullptr)
                    {
                        decoder->SetBuiltInRemoved(builtInDef->id);
                    }
                }
                if (_signals == nullptr)
                {
                    _signals = std::make_unique<SlotClaimSignals>(_context);
                }
                decoder->BindSlotSignals(_signals.get());
            }
            const std::vector<PortClaim> claims = CardClaims(*def, slot.entry.options);
            const auto order = static_cast<uint8_t>(PortDecoder::kSlotCardSlotBase + _cards.size());
            decoder->AttachSlotCard(card.get(), claims, order, def->detection);
        }
        if (SoundManager* sound = _context->pSoundManager)
        {
            sound->attachSlotCard(card.get());
        }
        _cards.push_back(std::move(card));
    }

    // The socket's chip shadowed by a card's IORQGE: its rows are silent by hardware, and say so
    const BuiltInDef* socketChip = SocketDefault(_result.machine);
    const BuiltIn* socketState = socketChip != nullptr ? _result.FindBuiltIn(socketChip->id) : nullptr;
    SoundManager* sound = _context->pSoundManager;
    if (!_cards.empty() && sound != nullptr && socketState != nullptr && StartsWith(socketState->state, "shadowed by "))
    {
        for (AudioSourceType row : { AudioSourceType::AY1_All, AudioSourceType::AY2_All, AudioSourceType::FM1,
                                     AudioSourceType::FM2 })
        {
            if (sound->device(row) != nullptr)
            {
                sound->setDeviceState(row, socketState->state);
            }
        }
    }
}

void SlotManager::ReleaseCards()
{
    PortDecoder* decoder = _context != nullptr ? _context->pPortDecoder : nullptr;
    SoundManager* sound = _context != nullptr ? _context->pSoundManager : nullptr;
    for (const std::unique_ptr<ICard>& card : _cards)
    {
        if (decoder != nullptr)
        {
            decoder->DetachSlotCard(card.get());
        }
        if (sound != nullptr)
        {
            sound->detachSlotCard(card.get());
        }
    }
    if (decoder != nullptr && !_cards.empty())
    {
        decoder->BindSlotSignals(nullptr);
    }
    _cards.clear();
}

ICard* SlotManager::FindCard(const std::string& slot) const
{
    for (const std::unique_ptr<ICard>& card : _cards)
    {
        if (card->SlotId() == slot)
        {
            return card.get();
        }
    }
    return nullptr;
}

// endregion
