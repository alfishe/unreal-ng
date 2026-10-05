#include "slotmanager.h"

#include <algorithm>
#include <exception>
#include <map>
#include <stdexcept>
#include <utility>

#include "common/modulelogger.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/config.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/network/networkspec.h"
#include "emulator/media/mediamanager.h"
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

/// The options a card was given, by name: "ram=4m" (the unset ones keep the card's defaults, as in the INI)
std::string GivenOptions(const CardDef& card, const CardOptions& options)
{
    std::string text;
    for (const OptionDef& option : card.options)
    {
        if (!options.Has(option.key))
        {
            continue;
        }
        const uint32_t bits = options.bits[static_cast<size_t>(option.key)];
        std::string values;
        for (size_t i = 0; i < option.values.size(); i++)
        {
            if (bits & Bit(static_cast<int>(i)))
            {
                values += (values.empty() ? "" : ",") + std::string(option.values[i].id);
            }
        }
        text += (text.empty() ? "" : " ") + std::string(Describe(option.key).id) + "=" + (values.empty() ? "none" : values);
    }
    return text;
}

/// Why the plan engine refused a change: the hard reasons, or everything that needs the replace flag
std::string PlanRefusal(const SlotPlan& plan)
{
    std::string reasons;
    for (const PlanReason& reason : plan.reasons)
    {
        if (!plan.hardRefusal || reason.hard)
        {
            reasons += (reasons.empty() ? "" : "; ") + reason.text;
        }
    }
    if (plan.hardRefusal)
    {
        return "refused: " + (reasons.empty() ? std::string("the plan engine refused it") : reasons);
    }
    return "needs replaceIfIncompatible: " + reasons;
}

/// The card id of a General Sound personality; "" for none
const char* GsCardOf(GSTypeKind kind)
{
    switch (kind)
    {
        case GSTypeKind::Z80:
            return "gs";
        case GSTypeKind::LW:
        case GSTypeKind::BASS:
            return "gs-lw";
        case GSTypeKind::NGS:
            return "neogs";
        case GSTypeKind::NONE:
            break;
    }
    return "";
}

/// SetBuildFaultForTests
std::string& BuildFault()
{
    static std::string card;
    return card;
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

// region <Changes (SL-6)>

SlotConfig SlotManager::ConfigOf(const Result& result)
{
    SlotConfig out;
    out.section = true;
    for (const Slot& slot : result.entries)
    {
        if (slot.entry.disabled)
        {
            continue;
        }
        SlotConfigEntry entry;
        entry.slot = slot.entry.slot;
        entry.card = slot.entry.card;
        const CardDef* card = Planner().FindCard(slot.entry.card);
        entry.options = card != nullptr ? GivenOptions(*card, slot.entry.options) : std::string();
        entry.adapter = slot.entry.adapter;
        entry.fitOverride = slot.fitOverride || slot.entry.unrealistic;
        entry.source = slot.source;
        out.entries.push_back(std::move(entry));
    }
    for (const BuiltIn& builtIn : result.builtIns)
    {
        if (builtIn.kind == BuiltInKind::Switchable)
        {
            out.builtIns.push_back(
                { builtIn.id, builtIn.on, builtIn.source.empty() ? "[SLOTS] builtin." + builtIn.id : builtIn.source });
        }
    }
    return out;
}

void SlotManager::UseSlots(const SlotConfig& slotConfig, CONFIG& config)
{
    config.slotConfig = slotConfig;
    config.slotConfig.section = true;
    Project(config.slotConfig, config);
}

SlotManager::ChangePlan SlotManager::PlanChange(const Result& current, const CONFIG& config, const SlotRequest& request,
                                                const PlanContext& context)
{
    ChangePlan out;
    if (current.machine == nullptr)
    {
        out.refusal = "refused: the model has no slot declaration";
        return out;
    }
    out.plan = Planner().Plan(current.model, current.Fitted(), request, context);
    if (!out.plan.allowed)
    {
        out.refusal = PlanRefusal(out.plan);
        return out;
    }
    // A card the plan plugs in but disables (an accidental port clash, R-COMP-6) would give a configuration whose
    // entries conflict, which is not created (Q8)
    for (const DisabledCard& disabled : out.plan.disabled)
    {
        out.refusal = "refused: " + disabled.slot + " = " + disabled.card + " would be disabled (" + disabled.reason +
                      "), and a slot set in conflict is not created (Q8)";
        return out;
    }

    // The new [SLOTS]: the resulting set, each kept card with what it was configured with
    out.config.section = true;
    for (const SlotEntry& entry : out.plan.resultingSlots)
    {
        if (entry.disabled)
        {
            continue;
        }
        SlotConfigEntry line;
        line.slot = entry.slot;
        line.card = entry.card;
        line.adapter = entry.adapter;
        const CardDef* card = Planner().FindCard(entry.card);
        line.options = card != nullptr ? GivenOptions(*card, entry.options) : std::string();
        const Slot* was = nullptr;
        for (const Slot& slot : current.entries)
        {
            if (!slot.entry.disabled && slot.entry.slot == entry.slot && slot.entry.card == entry.card)
            {
                was = &slot;
            }
        }
        line.fitOverride = entry.unrealistic || (was != nullptr && was->fitOverride);
        line.source = was != nullptr ? was->source : "slot change " + entry.slot;
        out.config.entries.push_back(std::move(line));
    }
    for (const BuiltIn& builtIn : current.builtIns)
    {
        if (builtIn.kind != BuiltInKind::Switchable)
        {
            continue;
        }
        SlotBuiltInSwitch sw{ builtIn.id, builtIn.on,
                              builtIn.source.empty() ? "[SLOTS] builtin." + builtIn.id : builtIn.source };
        for (const SwitchedOffBuiltIn& off : out.plan.builtInSwitchedOff)
        {
            if (off.builtIn == builtIn.id)
            {
                sw.on = false;
                sw.source = "switched off for " + out.plan.slot;
            }
        }
        out.config.builtIns.push_back(std::move(sw));
    }

    // The machine is created from it: the same checks as at creation (Q8 conflicts, a card the emulator cannot build)
    out.refusal = CreationRefusal(config, out.config);
    if (!out.refusal.empty())
    {
        out.config = {};
    }
    return out;
}

std::string SlotManager::CreationRefusal(const CONFIG& config, const SlotConfig& slotConfig)
{
    auto trial = std::make_unique<CONFIG>(config);
    UseSlots(slotConfig, *trial);
    const Result created = Plan(*trial);
    if (!created.conflicts.empty())
    {
        return "refused: " + created.Refusal();
    }
    for (const Slot& slot : created.entries)
    {
        if (slot.entry.disabled)
        {
            return "refused: " + slot.entry.slot + " = " + slot.entry.card + " cannot be fitted: " +
                   slot.entry.disabledReason;
        }
    }
    return {};
}

SlotManager::ChangePlan SlotManager::PlanSet(const SlotConfig& slotConfig) const
{
    ChangePlan out;
    out.plan.allowed = true;
    out.config = slotConfig;
    out.config.section = true;
    static const CONFIG kNoConfig{};
    out.refusal = CreationRefusal(_context != nullptr ? _context->config : kNoConfig, out.config);
    const std::string recording = ChangeRefusal();
    if (!recording.empty())
    {
        out.refusal = recording;
        out.recording = true;
    }
    if (!out.refusal.empty())
    {
        out.plan.allowed = false;
        out.config = {};
    }
    return out;
}

PlanContext SlotManager::LiveContext() const
{
    PlanContext context;
    if (_context != nullptr && _context->pMediaManager != nullptr)
    {
        for (const SlotInfo& info : _context->pMediaManager->List())
        {
            if (info.present && info.dirty)
            {
                context.dirtyMedia.push_back(info.descriptor.id);
            }
        }
    }
    return context;
}

void SlotManager::GuardRecording(ChangePlan& plan) const
{
    const std::string recording = ChangeRefusal();
    if (!recording.empty())
    {
        plan.refusal = recording;
        plan.recording = true;
        plan.config = {};
    }
}

SlotManager::ChangePlan SlotManager::PlanChange(const SlotRequest& request) const
{
    static const CONFIG kNoConfig{};
    ChangePlan out = PlanChange(Snapshot(), _context != nullptr ? _context->config : kNoConfig, request, LiveContext());
    GuardRecording(out);
    return out;
}

SlotManager::ChangePlan SlotManager::PlanChanges(const std::vector<SlotRequest>& requests) const
{
    static const CONFIG kNoConfig{};
    ChangePlan out = PlanChanges(Snapshot(), _context != nullptr ? _context->config : kNoConfig, requests, LiveContext());
    GuardRecording(out);
    return out;
}

SlotManager::ChangePlan SlotManager::PlanChanges(const Result& current, const CONFIG& config,
                                                 const std::vector<SlotRequest>& requests, const PlanContext& context)
{
    if (requests.size() == 1)
    {
        return PlanChange(current, config, requests.front(), context);
    }
    ChangePlan merged;
    if (current.machine == nullptr)
    {
        merged.refusal = "refused: the model has no slot declaration";
        return merged;
    }
    if (requests.empty())
    {
        merged.refusal = "refused: nothing to change";
        return merged;
    }

    auto append = [](auto& into, const auto& from) { into.insert(into.end(), from.begin(), from.end()); };
    SlotPlan& plan = merged.plan;
    plan.allowed = true;
    Result step = current;
    bool plugged = false;
    for (size_t i = 0; i < requests.size(); i++)
    {
        ChangePlan one = PlanChange(step, config, requests[i], context);
        const SlotPlan& part = one.plan;
        plan.allowed = plan.allowed && part.allowed;
        plan.hardRefusal = plan.hardRefusal || part.hardRefusal;
        plan.needsConfirmation = plan.needsConfirmation || part.needsConfirmation;
        plan.dryRun = part.dryRun;
        if (part.op == SlotRequest::Op::Plug || !plugged)
        {
            // What the change is about: the last plug (else the last step)
            plugged = plugged || part.op == SlotRequest::Op::Plug;
            plan.op = part.op;
            plan.slot = part.slot;
            plan.card = part.card;
            plan.options = part.options;
            plan.fit = part.fit;
            plan.adapter = part.adapter;
            plan.missingSignals = part.missingSignals;
            plan.arbitration = part.arbitration;
        }
        append(plan.reasons, part.reasons);
        append(plan.exceptions, part.exceptions);
        append(plan.removed, part.removed);
        append(plan.shadowed, part.shadowed);
        append(plan.builtInSwitchedOff, part.builtInSwitchedOff);
        append(plan.removedFromSocket, part.removedFromSocket);
        append(plan.disabled, part.disabled);
        append(plan.deadPorts, part.deadPorts);
        append(plan.busFights, part.busFights);
        append(plan.media, part.media);
        for (Function function : part.lostFunctions)
        {
            if (std::find(plan.lostFunctions.begin(), plan.lostFunctions.end(), function) == plan.lostFunctions.end())
            {
                plan.lostFunctions.push_back(function);
            }
        }
        plan.resultingSlots = part.resultingSlots;
        merged.config = one.config;
        if (!one.Allowed())
        {
            merged.refusal = one.refusal;
            merged.config = {};
            plan.allowed = false;
            return merged;
        }
        if (i + 1 < requests.size())
        {
            // The next step plans against the set this one leaves, as the machine would be created from it
            auto trial = std::make_unique<CONFIG>(config);
            UseSlots(one.config, *trial);
            step = Plan(*trial);
        }
    }
    return merged;
}

uint8_t SlotManager::NetworkCardsOf(const Result& current)
{
    uint8_t cards = 0;
    for (const Slot& slot : current.entries)
    {
        if (slot.entry.disabled)
        {
            continue;
        }
        if (slot.entry.card == "zxnetusb")
        {
            cards |= networkspec::kCardZxNetUsb;
        }
        else if (slot.entry.card == "zx-wifi")
        {
            cards |= networkspec::kCardZxWifi;
        }
    }
    return cards;
}

bool SlotManager::NetworkRequests(const Result& current, uint8_t zxBusCards, std::vector<SlotRequest>& out,
                                  std::string* error)
{
    out.clear();
    if (current.machine == nullptr)
    {
        if (error != nullptr)
        {
            *error = "the model has no slot declaration";
        }
        return false;
    }
    struct NetworkCard
    {
        uint8_t bit;
        const char* id;
    };
    static constexpr NetworkCard kCards[] = { { networkspec::kCardZxNetUsb, "zxnetusb" },
                                              { networkspec::kCardZxWifi, "zx-wifi" } };
    // Removals first: a card that goes frees its slot for the one that comes
    for (const Slot& slot : current.entries)
    {
        for (const NetworkCard& card : kCards)
        {
            if (!slot.entry.disabled && slot.entry.card == card.id && (zxBusCards & card.bit) == 0)
            {
                SlotRequest request;
                request.op = SlotRequest::Op::Remove;
                request.slot = slot.entry.slot;
                out.push_back(std::move(request));
            }
        }
    }
    const uint8_t fitted = NetworkCardsOf(current);
    for (const NetworkCard& card : kCards)
    {
        if ((zxBusCards & card.bit) != 0 && (fitted & card.bit) == 0)
        {
            SlotRequest request;
            request.op = SlotRequest::Op::Plug;
            request.card = card.id;   // the slot the planner suggests (zxbus.next)
            out.push_back(std::move(request));
        }
    }
    return true;
}

void SlotManager::SetBuildFaultForTests(const std::string& card)
{
    BuildFault() = card;
}

bool SlotManager::GeneralSoundRequest(const Result& current, const CONFIG& config, GSTypeKind kind, SlotRequest& request,
                                      std::string* error)
{
    auto fail = [error](std::string why) {
        if (error != nullptr)
        {
            *error = std::move(why);
        }
        return false;
    };
    const std::string card = GsCardOf(kind);
    if (card.empty())
    {
        return fail("not a General Sound personality (gs, gs-lw, neogs)");
    }
    const Slot* slot = current.FindGroup(SlotCardGroup::GeneralSound);
    if (slot == nullptr)
    {
        return fail("no General Sound card is fitted: plug one into a slot");
    }
    request = {};
    request.op = SlotRequest::Op::Plug;
    request.slot = slot->entry.slot;
    request.card = card;
    request.adapter = slot->entry.adapter;
    request.replaceIfIncompatible = true;   // the request is the replacement of the card in this slot
    // The RAM as a legacy key gives it (TranslateLegacy), so the fingerprint matches a machine created with the card
    std::string options;
    if (card == "gs")
    {
        options = config.sound.gsRamKB >= 512 ? "ram=512k" : config.sound.gsRamKB >= 256 ? "ram=256k" : "ram=128k";
    }
    else if (card == "neogs")
    {
        options = config.ngs.ramKB >= 4096 ? "ram=4m" : "ram=2m";
    }
    const CardDef* def = Planner().FindCard(card);
    if (def != nullptr && !options.empty())
    {
        ParseCardOptions(*def, options, request.options);
    }
    return true;
}

std::string SlotManager::GeneralSoundSwitchRefusal(GSTypeKind kind) const
{
    if (_context == nullptr)
    {
        return {};
    }
    const Result current = Snapshot();
    if (current.machine == nullptr)
    {
        return {};   // a model without a slot declaration: the sound manager decides alone, as before
    }
    SlotRequest request;
    std::string why;
    if (!GeneralSoundRequest(current, _context->config, kind, request, &why))
    {
        return why;
    }
    // The NeoGS's SD card follows the sound manager's own media rules in the running machine: no dirty-media check
    const SlotPlan plan = Planner().Plan(current.model, current.Fitted(), request);
    if (!plan.allowed)
    {
        return PlanRefusal(plan);
    }
    for (const RemovedCard& removed : plan.removed)
    {
        if (removed.slot != request.slot)
        {
            return "refused: the switch would also remove " + removed.slot + " = " + removed.card + " (" +
                   removed.reason + "): a slot change with replaceIfIncompatible does that";
        }
    }
    return {};
}

void SlotManager::FollowGeneralSoundSwitch(GSTypeKind kind)
{
    if (_context == nullptr)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(_resultMutex);
    SlotRequest request;
    if (_result.machine == nullptr || !GeneralSoundRequest(_result, _context->config, kind, request))
    {
        return;
    }
    // The slot's entry as the plan engine would leave it (options, fit); the card id and options in any case: the
    // switch is done, the plan names what the machine has
    const SlotPlan plan = Planner().Plan(_result.model, _result.Fitted(), request);
    for (Slot& slot : _result.entries)
    {
        if (slot.entry.disabled || slot.entry.slot != request.slot)
        {
            continue;
        }
        slot.entry.card = request.card;
        slot.entry.options = request.options;
        for (const SlotEntry& planned : plan.resultingSlots)
        {
            if (plan.allowed && planned.slot == request.slot)
            {
                slot.entry = planned;
                slot.fit = plan.fit;
            }
        }
        slot.notes.push_back("personality switched to " + request.card + " in the running machine");
    }
    _ttdFingerprint = TtdFingerprintFields(_result);
}

std::string SlotManager::CarryReport::DroppedText() const
{
    std::string text;
    for (const Dropped& card : dropped)
    {
        text += (text.empty() ? "" : "; ") + card.slot + " = " + card.card + " not carried: " + card.reason;
    }
    return text;
}

SlotManager::CarryReport SlotManager::Carry(const Result& from, CONFIG& config)
{
    CarryReport report;
    if (from.machine == nullptr)
    {
        return report;
    }
    report.carried = true;
    const std::string model = Config::GetModelFullName(config.mem_model);
    const SlotConfig carried = ConfigOf(from);
    const MachineDef* machine = Planner().FindMachine(config.mem_model);
    if (machine == nullptr)
    {
        for (const SlotConfigEntry& entry : carried.entries)
        {
            report.dropped.push_back({ entry.slot, entry.card, model + " has no slot declaration" });
            report.lines.push_back("Slots: " + entry.slot + " = " + entry.card + " not carried: " +
                                   report.dropped.back().reason);
        }
        return report;
    }

    // Where each entry of the merged set came from
    struct Origin
    {
        bool carried = false;
        std::string oldSlot;
    };
    std::map<std::string, Origin> origin;
    SlotConfig merged;
    merged.section = true;
    auto drop = [&](const std::string& oldSlot, const std::string& card, const std::string& reason) {
        report.dropped.push_back({ oldSlot, card, reason });
        report.lines.push_back("Slots: " + oldSlot + " = " + card + " not carried to " + model + ": " + reason);
    };

    // The carried cards first: the slot they had where the new machine has that bus, else where the planner puts
    // the card, numbered after the slots kept on that bus, behind the adapter that connects it
    std::map<std::string, int> highest;
    std::vector<const SlotConfigEntry*> moved;
    for (const SlotConfigEntry& entry : carried.entries)
    {
        if (FindBus(machine, BusOf(entry.slot)) != nullptr)
        {
            merged.entries.push_back(entry);
            origin[entry.slot] = { true, entry.slot };
            highest[BusOf(entry.slot)] = std::max(highest[BusOf(entry.slot)], NumberOf(entry.slot));
        }
        else
        {
            moved.push_back(&entry);
        }
    }
    for (const SlotConfigEntry* entry : moved)
    {
        const CardDef* card = Planner().FindCard(entry->card);
        std::string slot = card != nullptr ? Planner().SuggestSlot(*machine, *card) : std::string();
        if (slot.empty() || entry->slot == kAySocket)
        {
            drop(entry->slot, entry->card, model + " has no " + (entry->slot == kAySocket ? "AY socket" : "slot for it"));
            continue;
        }
        const std::string next = ".next";
        if (slot.size() > next.size() && slot.compare(slot.size() - next.size(), next.size(), next) == 0)
        {
            const std::string bus = slot.substr(0, slot.size() - next.size());
            slot = bus + "." + std::to_string(++highest[bus]);
        }
        SlotConfigEntry placed = *entry;
        placed.slot = slot;
        placed.adapter.clear();
        const BusDef* bus = FindBus(machine, BusOf(slot));
        if (bus != nullptr && bus->kind != card->bus)
        {
            for (const AdapterDef& adapter : Planner().Data().adapters)
            {
                if (adapter.cardSide == card->bus && adapter.machineSide == bus->kind)
                {
                    placed.adapter = adapter.id;
                    break;
                }
            }
        }
        merged.entries.push_back(placed);
        origin[slot] = { true, entry->slot };
    }

    auto erase = [&merged](const std::string& slot) {
        merged.entries.erase(std::remove_if(merged.entries.begin(), merged.entries.end(),
                                            [&slot](const SlotConfigEntry& e) { return e.slot == slot; }),
                             merged.entries.end());
    };
    auto trial = std::make_unique<CONFIG>(config);
    auto plan = [&]() {
        *trial = config;
        UseSlots(merged, *trial);
        return Plan(*trial);
    };
    // A carried card the new machine does not take (a bus signal, a fixed built-in, not emulated there)
    auto dropDisabled = [&](const Result& planned) {
        for (const Slot& slot : planned.entries)
        {
            const auto it = origin.find(slot.entry.slot);
            if (!slot.entry.disabled || it == origin.end() || !it->second.carried ||
                std::none_of(merged.entries.begin(), merged.entries.end(), [&slot](const SlotConfigEntry& e) {
                    return e.slot == slot.entry.slot && e.card == slot.entry.card;
                }))
            {
                continue;
            }
            drop(it->second.oldSlot, slot.entry.card, slot.entry.disabledReason);
            erase(slot.entry.slot);
        }
    };
    // Conflicts (Q8): the new machine's own card gives way to a carried one; between two carried cards the later in
    // slot order goes. Planned as the machine's creation will plan it
    auto resolve = [&]() {
        Result planned = plan();
        for (size_t round = 0; !planned.conflicts.empty() && round <= merged.entries.size(); round++)
        {
            const Conflict& conflict = planned.conflicts.front();
            const bool laterIsOwn = !origin[conflict.slot].carried;
            const bool otherIsOwn = !origin[conflict.withSlot].carried;
            const bool laterGoes = !(otherIsOwn && !laterIsOwn);
            const std::string victim = laterGoes ? conflict.slot : conflict.withSlot;
            const std::string victimCard = laterGoes ? conflict.card : conflict.withCard;
            const std::string winner = laterGoes ? conflict.withSlot : conflict.slot;
            const std::string winnerCard = laterGoes ? conflict.withCard : conflict.card;
            if (origin[victim].carried)
            {
                drop(origin[victim].oldSlot, victimCard,
                     "conflicts with " + winner + " = " + winnerCard + ": " + conflict.reason);
            }
            else
            {
                report.lines.push_back("Slots: the " + model + " config's " + victim + " = " + victimCard +
                                       " gives way to the carried " + winner + " = " + winnerCard);
            }
            erase(victim);
            planned = plan();
        }
        return planned;
    };

    // First the carried cards alone: the ones the new machine cannot take at all go before they could push out one
    // of its own cards
    dropDisabled(resolve());

    // The new machine's own cards fill the slots the carried ones leave free
    const Result own = Plan(config);
    const SlotConfig target = ConfigOf(own);
    for (const SlotConfigEntry& entry : target.entries)
    {
        if (std::none_of(merged.entries.begin(), merged.entries.end(),
                         [&entry](const SlotConfigEntry& e) { return e.slot == entry.slot; }))
        {
            merged.entries.push_back(entry);
            origin[entry.slot] = { false, entry.slot };
        }
    }
    merged.builtIns = target.builtIns;
    for (const SlotBuiltInSwitch& sw : carried.builtIns)
    {
        for (SlotBuiltInSwitch& mine : merged.builtIns)
        {
            if (mine.id == sw.id)
            {
                mine = sw;
            }
        }
    }
    dropDisabled(resolve());

    for (const SlotConfigEntry& entry : merged.entries)
    {
        const auto it = origin.find(entry.slot);
        if (it != origin.end() && it->second.carried)
        {
            const std::string where = it->second.oldSlot == entry.slot ? entry.slot : it->second.oldSlot + " -> " + entry.slot;
            report.kept.push_back(where + " = " + entry.card);
            report.lines.push_back("Slots: " + where + " = " + entry.card + " carried to " + model +
                                   (entry.adapter.empty() ? "" : " behind " + entry.adapter));
        }
    }
    UseSlots(merged, config);
    return report;
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

bool SlotManager::BuildCards()
{
    ReleaseCards();
    _buildError.clear();
    if (_context == nullptr || _result.machine == nullptr)
    {
        return true;
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
        std::unique_ptr<ICard> card;
        std::string why;
        try
        {
            if (!BuildFault().empty() && BuildFault() == slot.entry.card)
            {
                throw std::runtime_error("the test's build fault");
            }
            card = type->create(cardContext);
        }
        catch (const std::exception& e)
        {
            why = e.what();
        }
        if (card == nullptr)
        {
            // A fitted card that cannot be built: the machine is not created without it (a restart with a new slot
            // set then leaves the previous machine running, SlotChange)
            _buildError = slot.entry.slot + " = " + slot.entry.card + ": the card could not be built" +
                          (why.empty() ? std::string() : " (" + why + ")");
            if (ModuleLogger* logger = _context->pModuleLogger)
            {
                logger->Error(PlatformModulesEnum::MODULE_CORE, PlatformCoreSubmodulesEnum::SUBMODULE_CORE_CONFIG,
                              "Slots: %s", _buildError.c_str());
            }
            ReleaseCards();
            return false;
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
    _ttdCardFingerprint.clear();
    for (const std::unique_ptr<ICard>& card : _cards)
    {
        card->TtdFingerprint(_ttdCardFingerprint);
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
    return true;
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
