#include "slotplanner.h"

#include <algorithm>
#include <bit>
#include <cstdio>

namespace slots
{

namespace
{

constexpr std::string_view kAySocketBus = "ay-socket";

// region <Port arithmetic>

/// Two claims share at least one port
bool Overlaps(uint16_t maskA, uint16_t matchA, uint16_t maskB, uint16_t matchB)
{
    return ((matchA ^ matchB) & maskA & maskB) == 0;
}

bool Overlaps(const PortClaim& a, const PortClaim& b)
{
    return Overlaps(a.mask, a.match, b.mask, b.match);
}

bool Covers(const PortClaim& claim, uint16_t port)
{
    return (port & claim.mask) == claim.match;
}

/// The documented port of a claim: the explicit one, else the match with every undecoded line high (#FFFD style)
uint16_t DocumentedPort(const PortClaim& claim)
{
    return claim.port != 0 ? claim.port : static_cast<uint16_t>(claim.match | static_cast<uint16_t>(~claim.mask));
}

bool GatesMeet(Gate a, Gate b)
{
    return a == Gate::Always || b == Gate::Always || a == b;
}

uint8_t DirBits(Dir dir)
{
    return static_cast<uint8_t>(dir);
}

constexpr uint8_t kIn = static_cast<uint8_t>(Dir::In);

bool DrivesIorqge(const PortClaim& claim, uint8_t dirs)
{
    return claim.iorqge == Iorqge::Yes || (claim.iorqge == Iorqge::ReadsOnly && (dirs & kIn) != 0);
}

/// Every port of `claim` is covered by the union of `by`
bool FullyCovered(const PortClaim& claim, const std::vector<const PortClaim*>& by)
{
    uint16_t relevant = 0;
    std::vector<const PortClaim*> overlapping;
    for (const PortClaim* other : by)
    {
        if (Overlaps(claim, *other))
        {
            relevant |= other->mask;
            overlapping.push_back(other);
        }
    }
    if (overlapping.empty())
    {
        return false;
    }

    // Only the lines some covering claim decodes and `claim` leaves free can make a port escape
    const uint16_t freeBits = static_cast<uint16_t>(relevant & ~claim.mask);
    std::vector<uint16_t> bits;
    for (int i = 0; i < 16; i++)
    {
        if ((freeBits >> i) & 1)
        {
            bits.push_back(static_cast<uint16_t>(1u << i));
        }
    }

    const uint32_t combinations = 1u << bits.size();
    for (uint32_t combination = 0; combination < combinations; combination++)
    {
        uint16_t port = claim.match;
        for (size_t i = 0; i < bits.size(); i++)
        {
            if ((combination >> i) & 1)
            {
                port |= bits[i];
            }
        }
        const bool covered = std::any_of(overlapping.begin(), overlapping.end(),
                                         [port](const PortClaim* other) { return Covers(*other, port); });
        if (!covered)
        {
            return false;
        }
    }
    return true;
}

std::string Hex(uint32_t value, int digits)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%0*X", digits, value);
    return buffer;
}

// endregion

// region <Names>

std::string Quoted(std::string_view text)
{
    return "`" + std::string(text) + "`";
}

std::string FunctionList(const std::vector<Function>& functions)
{
    std::string result;
    for (Function function : functions)
    {
        if (!result.empty())
        {
            result += ", ";
        }
        result += Quoted(Describe(function).id);
    }
    return result;
}

std::string PortList(const std::vector<uint16_t>& ports)
{
    std::string result;
    for (uint16_t port : ports)
    {
        if (!result.empty())
        {
            result += ", ";
        }
        result += Quoted(FormatPort(port));
    }
    return result;
}

std::string SignalList(SignalSet signals)
{
    std::string result;
    for (int i = 0; i < kBusSignalCount; i++)
    {
        if (signals & (1u << i))
        {
            if (!result.empty())
            {
                result += ", ";
            }
            result += Describe(SignalAt(i)).id;
        }
    }
    return result;
}

/// "zxbus.2" -> "zxbus"; "ay-socket" -> "ay-socket"
std::string BusIdOf(std::string_view slot)
{
    const size_t dot = slot.rfind('.');
    return std::string(dot == std::string_view::npos ? slot : slot.substr(0, dot));
}

int SlotNumber(std::string_view slot)
{
    const size_t dot = slot.rfind('.');
    if (dot == std::string_view::npos)
    {
        return 0;
    }
    int number = 0;
    for (char c : slot.substr(dot + 1))
    {
        if (c < '0' || c > '9')
        {
            return 0;
        }
        number = number * 10 + (c - '0');
    }
    return number;
}

bool EndsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

// endregion

// region <Card configuration>

const OptionDef* FindOption(const CardDef& card, Opt key)
{
    for (const OptionDef& option : card.options)
    {
        if (option.key == key)
        {
            return &option;
        }
    }
    return nullptr;
}

const OptionDef* FindOptionByName(const CardDef& card, std::string_view name)
{
    for (const OptionDef& option : card.options)
    {
        if (name == Describe(option.key).id)
        {
            return &option;
        }
    }
    return nullptr;
}

bool Holds(const When& when, const CardDef& card, const CardOptions& options)
{
    return when.option == Opt::None || (OptionBits(card, options, when.option) & when.anyOf) != 0;
}

std::vector<PortClaim> ActiveClaims(const CardDef& card, const CardOptions& options)
{
    std::vector<PortClaim> result;
    for (const PortClaim& claim : card.claims)
    {
        if (Holds(claim.when, card, options))
        {
            result.push_back(claim);
        }
    }
    return result;
}

std::vector<FunctionUse> ActiveFunctions(const CardDef& card, const CardOptions& options)
{
    std::vector<FunctionUse> result;
    for (const FunctionUse& use : card.functions)
    {
        if (Holds(use.when, card, options))
        {
            result.push_back(use);
        }
    }
    return result;
}

bool ValidateOptions(const CardDef& card, const CardOptions& options, std::string* error)
{
    for (int key = 1; key < kOptCount; key++)
    {
        const Opt opt = static_cast<Opt>(key);
        if (!options.Has(opt))
        {
            continue;
        }
        const OptionDef* option = FindOption(card, opt);
        if (option == nullptr)
        {
            *error = Quoted(card.id) + " has no option " + Quoted(Describe(opt).id);
            return false;
        }
        const uint32_t bits = options.bits[static_cast<size_t>(key)];
        const uint32_t valid = option->values.size() >= 32 ? ~0u : (1u << option->values.size()) - 1;
        if ((bits & ~valid) != 0 || (option->kind == OptionKind::Enum && std::popcount(bits) != 1))
        {
            *error = Quoted(card.id) + " option " + Quoted(Describe(opt).id) + " has an invalid value";
            return false;
        }
    }
    return true;
}

// endregion

/// One device the new card's claims are compared with: a built-in, or the board in the AY socket (it answers the
/// socket's host decode, the claims of the socket's default built-in)
struct Device
{
    const BuiltInDef* builtIn = nullptr;
    const SlotEntry* socketEntry = nullptr;   ///< set: the board in the AY socket
};

/// Builds one plan; the state of one Plan() call
class PlanBuilder
{
public:
    PlanBuilder(const SlotPlanner& planner, const MachineDef& machine, const SlotRequest& request,
                const PlanContext& context, SlotPlan& plan)
        : _planner(planner), _machine(machine), _request(request), _context(context), _plan(plan)
    {
    }

    void Run(const SlotSet& slots)
    {
        _slots = _planner.Canonical(_machine, slots);
        switch (_request.op)
        {
            case SlotRequest::Op::Plug:
            {
                const CardDef* card = _planner.FindCard(_request.card);
                if (card == nullptr)
                {
                    Refuse(Rule::Request, "unknown card " + Quoted(_request.card), "unknown card");
                    return;
                }
                Plug(_request.slot, *card, _request.options, _request.adapter);
                break;
            }
            case SlotRequest::Op::SetOptions:
            {
                const SlotEntry* entry = Find(_request.slot);
                const CardDef* card = entry != nullptr ? _planner.FindCard(entry->card) : nullptr;
                if (card == nullptr)
                {
                    Refuse(Rule::Request, Quoted(_request.slot) + " holds no card", "empty slot");
                    return;
                }
                CardOptions options = entry->options;
                for (int key = 1; key < kOptCount; key++)
                {
                    if (_request.options.Has(static_cast<Opt>(key)))
                    {
                        options.Set(static_cast<Opt>(key), _request.options.bits[static_cast<size_t>(key)]);
                    }
                }
                const SlotEntry current = *entry;
                _slots.erase(_slots.begin() + (entry - _slots.data()));
                Plug(current.slot, *card, options, current.adapter);
                break;
            }
            case SlotRequest::Op::Remove:
                Remove();
                break;
        }
    }

    void Finish()
    {
        _plan.hardRefusal = std::any_of(_plan.reasons.begin(), _plan.reasons.end(),
                                        [](const PlanReason& reason) { return reason.hard; });
        if (_request.op != SlotRequest::Op::Remove)
        {
            _plan.needsConfirmation = !_plan.removed.empty() || !_plan.builtInSwitchedOff.empty() ||
                                      !_plan.removedFromSocket.empty() || _plan.fit == Fit::Unrealistic ||
                                      !_plan.busFights.empty();
        }
        _plan.allowed = !_plan.hardRefusal && (!_plan.needsConfirmation || _request.replaceIfIncompatible);
    }

private:
    // region <Helpers>

    void Refuse(Rule rule, std::string text, std::string brief)
    {
        _plan.reasons.push_back({ rule, true, std::move(text), std::move(brief) });
    }

    void Confirm(Rule rule, std::string text, std::string brief = {})
    {
        _plan.reasons.push_back({ rule, false, std::move(text), std::move(brief) });
    }

    const SlotEntry* Find(std::string_view slot) const
    {
        for (const SlotEntry& entry : _slots)
        {
            if (entry.slot == slot)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    const BusDef* FindBus(std::string_view id) const
    {
        for (const BusDef& bus : _machine.buses)
        {
            if (id == bus.id)
            {
                return &bus;
            }
        }
        return nullptr;
    }

    const BuiltInDef* SocketDefault() const
    {
        for (const BuiltInDef& builtIn : _machine.builtIns)
        {
            if (builtIn.socket != nullptr && kAySocketBus == builtIn.socket)
            {
                return &builtIn;
            }
        }
        return nullptr;
    }

    bool IsRemoved(std::string_view slot) const
    {
        return std::any_of(_plan.removed.begin(), _plan.removed.end(),
                           [slot](const RemovedCard& removed) { return removed.slot == slot; });
    }

    /// An installed card that takes part in the analysis: a known card, not disabled, not removed, not the socket's
    /// own chip
    const CardDef* LiveCard(const SlotEntry& entry) const
    {
        if (entry.disabled || IsRemoved(entry.slot) || entry.card == kEmptySocket)
        {
            return nullptr;
        }
        const CardDef* card = _planner.FindCard(entry.card);
        return card != nullptr && !card->socketDefault ? card : nullptr;
    }

    void RemoveCard(const SlotEntry& entry, Rule rule, std::string reason, std::vector<Function> clashing,
                    bool replacedInSlot, bool pointless)
    {
        if (IsRemoved(entry.slot))
        {
            return;
        }
        const CardDef* card = _planner.FindCard(entry.card);
        RemovedCard removed;
        removed.slot = entry.slot;
        removed.card = entry.card;
        removed.options = entry.options;
        removed.optionsText = card != nullptr ? FormatCardOptions(*card, entry.options) : std::string();
        removed.clashing = std::move(clashing);
        removed.replacedInSlot = replacedInSlot;
        removed.pointless = pointless;
        removed.rule = rule;
        removed.reason = reason;
        _plan.removed.push_back(std::move(removed));
        Confirm(rule, "removes " + Quoted(entry.slot) + " " + Quoted(entry.card) + ": " + reason);
    }

    std::string NextFree(const BusDef& bus) const
    {
        for (int n = 1;; n++)
        {
            const std::string slot = std::string(bus.id) + "." + std::to_string(n);
            if (Find(slot) == nullptr)
            {
                return slot;
            }
        }
    }

    std::vector<const PortClaim*> AlwaysBoardPorts(const BusDef& bus, Gate gate) const
    {
        std::vector<const PortClaim*> result;
        for (const PortClaim& port : bus.boardPorts)
        {
            if (port.gate == Gate::Always && GatesMeet(port.gate, gate))
            {
                result.push_back(&port);
            }
        }
        return result;
    }

    std::vector<std::string> BuiltInsOn(uint16_t mask, uint16_t match) const
    {
        std::vector<std::string> result;
        for (const BuiltInDef& builtIn : _machine.builtIns)
        {
            for (const PortClaim& claim : builtIn.claims)
            {
                if (Overlaps(mask, match, claim.mask, claim.match))
                {
                    result.emplace_back(builtIn.id);
                    break;
                }
            }
        }
        return result;
    }

    // endregion

    // region <Plug>

    void Plug(std::string slot, const CardDef& card, const CardOptions& options, const std::string& adapterId)
    {
        _card = &card;
        _options = options;
        _plan.card = card.id;
        _plan.options = options;
        const bool socketCard = card.bus == BusKind::AySocket;

        std::string error;
        if (!ValidateOptions(card, options, &error))
        {
            Refuse(Rule::Request, error, "invalid options");
            return;
        }

        // Target slot
        if (slot.empty())
        {
            slot = _planner.SuggestSlot(_machine, card);
        }
        if (slot.empty())
        {
            if (socketCard)
            {
                Refuse(Rule::D4, Quoted(_machine.name) + " has no " + Quoted(kAySocketBus), "no `ay-socket`");
            }
            else
            {
                Refuse(Rule::D4, Quoted(_machine.name) + " has no expansion bus", "no bus");
            }
            return;
        }
        const BusDef* bus = FindBus(BusIdOf(slot));
        if (bus == nullptr)
        {
            Refuse(Rule::Request, Quoted(_machine.name) + " has no bus " + Quoted(BusIdOf(slot)), "no such bus");
            return;
        }
        if (EndsWith(slot, ".next"))
        {
            slot = NextFree(*bus);
        }
        _plan.slot = slot;
        _bus = bus;
        if (socketCard != (bus->kind == BusKind::AySocket))
        {
            Refuse(Rule::Request,
                   socketCard ? Quoted(card.id) + " is an AY-socket board; it goes into " + Quoted(kAySocketBus)
                              : Quoted(card.id) + " is a bus card; it does not go into " + Quoted(kAySocketBus),
                   "wrong slot");
            return;
        }

        _functions = ActiveFunctions(card, options);
        _claims = ActiveClaims(card, options);

        ApplyExceptions();                  // D9
        CheckFit(adapterId);                // D8 (also fixes the arbitration in effect)
        CheckDeadPorts();                   // D4: claims hidden by the board
        CheckBuiltInFunctions();            // D4 fixed / D5 switchable
        ReplaceOccupant(slot, socketCard);
        CheckFunctionClashes();             // D1
        if (!socketCard)
        {
            CheckDevices();                 // D2, D3, D6, D12
            CheckPortClashes();             // D7
        }
        CollectLostFunctions();             // D10
        CollectMedia();                     // D11
        BuildResultingSlots(adapterId);
    }

    void ApplyExceptions()
    {
        if (!_context.applyExceptions)
        {
            return;
        }
        for (const ExceptionDef& exception : _planner.Data().exceptions)
        {
            if (_card->id != std::string_view(exception.card))
            {
                continue;
            }
            if (!exception.anyMachine && exception.model != _machine.model)
            {
                continue;
            }
            if (exception.installedCard != nullptr &&
                std::none_of(_slots.begin(), _slots.end(),
                             [&](const SlotEntry& entry) { return entry.card == exception.installedCard; }))
            {
                continue;
            }
            _plan.exceptions.emplace_back(exception.reason);
            if (exception.outcome == Outcome::Refused)
            {
                _plan.reasons.push_back({ Rule::D9, exception.hard, exception.reason, exception.brief });
            }
        }
    }

    void CheckFit(const std::string& adapterId)
    {
        _plan.adapter = adapterId;
        if (_card->bus == BusKind::AySocket)
        {
            _plan.fit = Fit::Real;
            _plan.arbitration = Arbitration::None;
            return;
        }

        const AdapterDef* adapter = nullptr;
        if (!adapterId.empty())
        {
            adapter = _planner.FindAdapter(adapterId);
            if (adapter == nullptr || adapter->cardSide != _card->bus || adapter->machineSide != _bus->kind)
            {
                Refuse(Rule::D8, "adapter " + Quoted(adapterId) + " does not connect a " +
                                     Quoted(Describe(_card->bus).id) + " card to " + Quoted(Describe(_bus->kind).id),
                       "wrong adapter");
                adapter = nullptr;
            }
        }

        const bool native = _bus->kind == _card->bus;
        SignalSet available = 0;
        if (native)
        {
            available = _bus->signals;
        }
        else if (adapter != nullptr)
        {
            available = (_bus->signals & adapter->passes) | adapter->adds;
        }
        const SignalSet missing = _card->needs & ~available;

        if (native && missing == 0)
        {
            _plan.fit = Fit::Real;
            _plan.arbitration = _bus->arbitration;
            return;
        }
        if (adapter != nullptr && missing == 0)
        {
            _plan.fit = Fit::Adapter;
            _plan.arbitration = adapter->hasArbitration ? adapter->arbitration : _bus->arbitration;
            return;
        }

        // Unrealistic: works logically as if the bus carried every signal it needs (Q5)
        _plan.fit = Fit::Unrealistic;
        _plan.missingSignals = missing;
        if (adapter != nullptr && adapter->hasArbitration)
        {
            _plan.arbitration = adapter->arbitration;
        }
        else if (!native && (_card->needs & Sig(BusSignal::Iorqge)) != 0 &&
                 (_bus->arbitration == Arbitration::None || _bus->arbitration == Arbitration::UlaOnly))
        {
            _plan.arbitration = Arbitration::CardWins;
        }
        else
        {
            _plan.arbitration = _bus->arbitration;
        }

        std::string text;
        std::string brief;
        if (native)
        {
            text = Quoted(_bus->id) + " lacks " + SignalList(missing) + " that " + Quoted(_card->id) + " needs";
            brief = "needs " + SignalList(missing);
        }
        else
        {
            text = Quoted(_card->id) + " is a " + Quoted(Describe(_card->bus).id) + " card; " + Quoted(_bus->id) +
                   " is " + Quoted(Describe(_bus->kind).id);
            if (adapter != nullptr)
            {
                text += "; behind " + Quoted(adapter->id) + " it still lacks " + SignalList(missing);
            }
            else
            {
                for (const AdapterDef& candidate : _planner.Data().adapters)
                {
                    if (candidate.cardSide == _card->bus && candidate.machineSide == _bus->kind)
                    {
                        text += "; adapter: " + Quoted(candidate.id);
                        break;
                    }
                }
            }
        }
        Confirm(Rule::D8, text + " (fit `unrealistic` with the override)", brief);
    }

    /// D4: on a BoardWins bus an Iorq card never sees the board's own ports
    void CheckDeadPorts()
    {
        if (_plan.arbitration != Arbitration::BoardWins || _card->detection != CycleDetection::Iorq ||
            _bus->boardPorts.empty() || _claims.empty())
        {
            return;
        }

        bool allDead = true;
        for (const PortClaim& claim : _claims)
        {
            const std::vector<const PortClaim*> board = AlwaysBoardPorts(*_bus, claim.gate);
            if (!FullyCovered(claim, board))
            {
                allDead = false;
            }
            for (const PortClaim* port : board)
            {
                if (!Overlaps(claim, *port))
                {
                    continue;
                }
                const uint16_t mask = claim.mask | port->mask;
                const uint16_t match = claim.match | port->match;
                const bool known = std::any_of(_plan.deadPorts.begin(), _plan.deadPorts.end(),
                                               [&](const DeadPort& dead) { return dead.mask == mask && dead.match == match; });
                if (!known)
                {
                    _plan.deadPorts.push_back({ mask, match, BuiltInsOn(mask, match) });
                }
            }
        }
        if (!allDead)
        {
            return;
        }

        std::string ports;
        std::string servedBy;
        for (const DeadPort& dead : _plan.deadPorts)
        {
            ports += (ports.empty() ? "" : ", ") + Quoted(FormatPortRange(dead.mask, dead.match));
            for (const std::string& id : dead.servedBy)
            {
                if (servedBy.find(Quoted(id)) == std::string::npos)
                {
                    servedBy += (servedBy.empty() ? "" : ", ") + Quoted(id);
                }
            }
        }
        std::string text = ports + " is a board port" + (servedBy.empty() ? "" : " (" + servedBy + ")") +
                           " hidden from the slots";
        const std::string brief = ports + (servedBy.empty() ? " is a board port" : " is the board's " + servedBy);
        const std::string alternative = AlternativeOption();
        if (!alternative.empty())
        {
            text += "; use " + alternative;
        }
        Refuse(Rule::D4, text, brief);
    }

    /// An enum option value of the card whose claims the board does not hide ("`port=ee`"), or ""
    std::string AlternativeOption() const
    {
        for (const OptionDef& option : _card->options)
        {
            if (option.kind != OptionKind::Enum)
            {
                continue;
            }
            for (size_t index = 0; index < option.values.size(); index++)
            {
                if (OptionBits(*_card, _options, option.key) == Bit(static_cast<int>(index)))
                {
                    continue;
                }
                CardOptions candidate = _options;
                candidate.Set(option.key, Bit(static_cast<int>(index)));
                const std::vector<PortClaim> claims = ActiveClaims(*_card, candidate);
                const bool anyLive = std::any_of(claims.begin(), claims.end(), [&](const PortClaim& claim) {
                    return !FullyCovered(claim, AlwaysBoardPorts(*_bus, claim.gate));
                });
                if (anyLive)
                {
                    return Quoted(std::string(Describe(option.key).id) + "=" + option.values[index].id);
                }
            }
        }
        return {};
    }

    /// D4 / D5: a function the card needs that a built-in holds
    void CheckBuiltInFunctions()
    {
        for (const BuiltInDef& builtIn : _machine.builtIns)
        {
            if (builtIn.socket != nullptr && _card->bus == BusKind::AySocket)
            {
                continue;   // the socket board replaces the machine's chip
            }
            std::vector<Function> shared;
            for (Function held : builtIn.functions)
            {
                for (const FunctionUse& use : _functions)
                {
                    if (use.function == held && use.role == Role::Own)
                    {
                        shared.push_back(held);
                    }
                }
            }
            if (shared.empty())
            {
                continue;
            }
            switch (builtIn.kind)
            {
                case BuiltInKind::Fixed:
                    Refuse(Rule::D4, FunctionList(shared) + " is held by the built-in " + Quoted(builtIn.id) +
                                         ", which cannot be switched off",
                           FunctionList(shared) + " built in");
                    break;
                case BuiltInKind::Switchable:
                    _plan.builtInSwitchedOff.push_back({ builtIn.id, shared });
                    Confirm(Rule::D5, "switches off the built-in " + Quoted(builtIn.id) + " (" + FunctionList(shared) + ")");
                    break;
                case BuiltInKind::Socketed:
                    _plan.removedFromSocket.push_back({ builtIn.id, builtIn.socket ? builtIn.socket : "", builtIn.chip, {} });
                    Confirm(Rule::D12, "takes the built-in " + Quoted(builtIn.id) + " out of its socket (" +
                                           FunctionList(shared) + ")");
                    break;
                case BuiltInKind::Count:
                    break;
            }
        }
    }

    void ReplaceOccupant(const std::string& slot, bool socketCard)
    {
        const SlotEntry* occupant = Find(slot);
        if (occupant == nullptr)
        {
            return;
        }
        if (socketCard)
        {
            const CardDef* held = _planner.FindCard(occupant->card);
            if (occupant->card == kEmptySocket || (held != nullptr && held->socketDefault))
            {
                return;     // the machine's own chip (or an empty socket) simply makes way
            }
        }
        RemoveCard(*occupant, Rule::D1, "replaced in its slot", {}, true, false);
    }

    /// D1: every installed card sharing a function with the new card, all at once
    void CheckFunctionClashes()
    {
        for (const SlotEntry& entry : _slots)
        {
            const CardDef* card = LiveCard(entry);
            if (card == nullptr)
            {
                continue;
            }
            std::vector<Function> shared;
            for (const FunctionUse& mine : _functions)
            {
                for (const FunctionUse& theirs : ActiveFunctions(*card, entry.options))
                {
                    if (mine.function != theirs.function)
                    {
                        continue;
                    }
                    // A bus card taking the AY role over leaves the socket board to the claim rules (D3 / D12)
                    if (mine.role == Role::Takeover && theirs.role == Role::Own && card->bus == BusKind::AySocket)
                    {
                        continue;
                    }
                    if (std::find(shared.begin(), shared.end(), mine.function) == shared.end())
                    {
                        shared.push_back(mine.function);
                    }
                }
            }
            if (!shared.empty())
            {
                RemoveCard(entry, Rule::D1, "shares " + FunctionList(shared), shared, false, false);
            }
        }
    }

    bool IsAlwaysBoardPort(uint16_t port, Gate gate) const
    {
        for (const PortClaim* board : AlwaysBoardPorts(*_bus, gate))
        {
            if (Covers(*board, port))
            {
                return true;
            }
        }
        return false;
    }

    /// D2, D3, D6, D12: the new card's claims against the built-ins and the board in the AY socket
    void CheckDevices()
    {
        const BuiltInDef* socketDefault = SocketDefault();
        const SlotEntry* socketEntry = Find(kAySocketBus);
        const CardDef* socketCard = socketEntry != nullptr ? LiveCard(*socketEntry) : nullptr;
        const bool socketEmpty = socketEntry != nullptr && socketEntry->card == kEmptySocket;

        std::vector<Device> devices;
        for (const BuiltInDef& builtIn : _machine.builtIns)
        {
            if (&builtIn == socketDefault)
            {
                if (socketEmpty)
                {
                    continue;
                }
                devices.push_back({ &builtIn, socketCard != nullptr ? socketEntry : nullptr });
                continue;
            }
            devices.push_back({ &builtIn, nullptr });
        }

        for (const Device& device : devices)
        {
            std::vector<uint16_t> shadowPorts;
            std::vector<uint16_t> fightPorts;
            for (const PortClaim& theirs : device.builtIn->claims)
            {
                const uint16_t port = DocumentedPort(theirs);
                for (const PortClaim& mine : _claims)
                {
                    if (!GatesMeet(mine.gate, theirs.gate) || !Covers(mine, port))
                    {
                        continue;
                    }
                    const uint8_t dirs = DirBits(mine.dir) & DirBits(theirs.dir);
                    if (dirs == 0)
                    {
                        continue;
                    }
                    const bool iorqge = DrivesIorqge(mine, dirs);
                    if ((_plan.arbitration == Arbitration::CardWins && iorqge) ||
                        (_plan.arbitration == Arbitration::UlaOnly && iorqge && (port & 0x00FF) == 0x00FE))
                    {
                        AddUnique(shadowPorts, port);
                    }
                    else if ((dirs & kIn) != 0)
                    {
                        if (_plan.arbitration == Arbitration::BoardWins && _card->detection == CycleDetection::Iorq &&
                            IsAlwaysBoardPort(port, mine.gate))
                        {
                            continue;   // dead for this card (D4 reports it), no fight
                        }
                        AddUnique(fightPorts, port);
                    }
                    // a write both receive is co-reception, as on the real bus
                }
            }
            for (uint16_t port : shadowPorts)
            {
                fightPorts.erase(std::remove(fightPorts.begin(), fightPorts.end(), port), fightPorts.end());
            }

            if (!shadowPorts.empty())
            {
                if (device.socketEntry != nullptr)
                {
                    RemoveCard(*device.socketEntry, Rule::D3,
                               "pointless pair: " + Quoted(_plan.slot) + " would shadow it (" + PortList(shadowPorts) +
                                   "); the socket returns to the machine's chip",
                               {}, false, true);
                }
                _plan.shadowed.push_back({ device.builtIn->id, _plan.slot, shadowPorts });
            }
            if (!fightPorts.empty())
            {
                if (device.socketEntry != nullptr)
                {
                    RemoveCard(*device.socketEntry, Rule::D12,
                               "pointless pair: both would drive " + PortList(fightPorts) + " reads", {}, false, true);
                }
                if (device.builtIn->kind == BuiltInKind::Socketed)
                {
                    _plan.removedFromSocket.push_back(
                        { device.builtIn->id, device.builtIn->socket ? device.builtIn->socket : "", device.builtIn->chip,
                          fightPorts });
                    Confirm(Rule::D12, "takes the " + std::string(device.builtIn->chip) + " (" + Quoted(device.builtIn->id) +
                                           ") out of its socket: otherwise " + PortList(fightPorts) +
                                           " reads are a bus fight");
                }
                else
                {
                    for (uint16_t port : fightPorts)
                    {
                        _plan.busFights.push_back({ device.builtIn->id, port });
                    }
                    Confirm(Rule::D12, PortList(fightPorts) + " reads would be a bus fight with the built-in " +
                                           Quoted(device.builtIn->id) + " (fit `unrealistic` with the override)");
                }
            }
        }
    }

    static void AddUnique(std::vector<uint16_t>& ports, uint16_t port)
    {
        if (std::find(ports.begin(), ports.end(), port) == ports.end())
        {
            ports.push_back(port);
        }
    }

    /// D7: an accidental read clash with a remaining card that no function and no IORQGE explains; the later card
    /// by slot order is disabled
    void CheckPortClashes()
    {
        const SlotSet ordered = _planner.Canonical(_machine, WithNewCard());
        for (const SlotEntry& entry : _slots)
        {
            const CardDef* card = LiveCard(entry);
            if (card == nullptr || card->bus == BusKind::AySocket)
            {
                continue;
            }
            std::vector<std::string> ports;
            for (const PortClaim& mine : _claims)
            {
                for (const PortClaim& theirs : ActiveClaims(*card, entry.options))
                {
                    if ((DirBits(mine.dir) & kIn) == 0 || (DirBits(theirs.dir) & kIn) == 0 ||
                        !GatesMeet(mine.gate, theirs.gate) || !Overlaps(mine, theirs) || DrivesIorqge(mine, kIn) ||
                        DrivesIorqge(theirs, kIn))
                    {
                        continue;
                    }
                    const std::string range = FormatPortRange(mine.mask | theirs.mask, mine.match | theirs.match);
                    if (std::find(ports.begin(), ports.end(), range) == ports.end())
                    {
                        ports.push_back(range);
                    }
                }
            }
            if (ports.empty())
            {
                continue;
            }

            size_t mineIndex = 0;
            size_t theirIndex = 0;
            for (size_t i = 0; i < ordered.size(); i++)
            {
                if (ordered[i].slot == _plan.slot)
                {
                    mineIndex = i;
                }
                if (ordered[i].slot == entry.slot)
                {
                    theirIndex = i;
                }
            }
            const bool newIsLater = mineIndex > theirIndex;
            DisabledCard disabled;
            disabled.slot = newIsLater ? _plan.slot : entry.slot;
            disabled.card = newIsLater ? _card->id : entry.card;
            disabled.clashSlot = newIsLater ? entry.slot : _plan.slot;
            disabled.ports = ports;
            std::string list;
            for (const std::string& port : ports)
            {
                list += (list.empty() ? "" : ", ") + Quoted(port);
            }
            disabled.reason = "port " + list + " clashes with " + Quoted(disabled.clashSlot);
            _plan.disabled.push_back(std::move(disabled));
        }
    }

    SlotSet WithNewCard() const
    {
        SlotSet slots = _slots;
        SlotEntry entry;
        entry.slot = _plan.slot;
        entry.card = _card->id;
        slots.push_back(entry);
        return slots;
    }

    /// D10: functions the removed cards offered that nothing offers once the plan is applied: not the new card, not a
    /// card that stays, not a built-in that is active afterwards (removing a MultiSound that took the AY role over
    /// un-shadows the board AY: `ay-socket` is not lost; a socket the plan or an earlier card emptied stays empty)
    void CollectLostFunctions()
    {
        std::array<bool, static_cast<size_t>(Function::Count)> lost{};
        for (const RemovedCard& removed : _plan.removed)
        {
            const CardDef* card = _planner.FindCard(removed.card);
            if (card == nullptr)
            {
                continue;
            }
            for (const FunctionUse& use : ActiveFunctions(*card, removed.options))
            {
                lost[static_cast<size_t>(use.function)] = true;
            }
        }
        if (_card != nullptr)
        {
            for (const FunctionUse& use : _functions)
            {
                lost[static_cast<size_t>(use.function)] = false;
            }
        }
        bool socketTaken = _card != nullptr && _card->bus == BusKind::AySocket && !_card->socketDefault;
        for (const SlotEntry& entry : _slots)
        {
            if (entry.disabled || IsRemoved(entry.slot) || (_request.op == SlotRequest::Op::Remove && entry.slot == _request.slot))
            {
                continue;
            }
            const CardDef* card = _planner.FindCard(entry.card);
            if (entry.slot == kAySocketBus && (card == nullptr || !card->socketDefault))
            {
                socketTaken = true;   // a socket board, or the chip taken out (`none`)
            }
            if (card == nullptr)
            {
                continue;
            }
            for (const FunctionUse& use : ActiveFunctions(*card, entry.options))
            {
                lost[static_cast<size_t>(use.function)] = false;
            }
        }
        for (const BuiltInDef& builtIn : _machine.builtIns)
        {
            const std::string_view id = builtIn.id;
            const bool switchedOff = std::any_of(_plan.builtInSwitchedOff.begin(), _plan.builtInSwitchedOff.end(),
                                                 [id](const SwitchedOffBuiltIn& off) { return off.builtIn == id; });
            const bool outOfSocket = std::any_of(_plan.removedFromSocket.begin(), _plan.removedFromSocket.end(),
                                                 [id](const RemovedFromSocket& out) { return out.builtIn == id; });
            if (switchedOff || outOfSocket || (builtIn.socket != nullptr && socketTaken))
            {
                continue;
            }
            for (Function function : builtIn.functions)
            {
                lost[static_cast<size_t>(function)] = false;
            }
        }
        for (size_t i = 0; i < lost.size(); i++)
        {
            if (lost[i])
            {
                _plan.lostFunctions.push_back(static_cast<Function>(i));
            }
        }
    }

    /// D11: the removed cards' media follow the stranded-media rules; a dirty medium needs a disposition
    void CollectMedia()
    {
        for (const RemovedCard& removed : _plan.removed)
        {
            const CardDef* card = _planner.FindCard(removed.card);
            if (card == nullptr)
            {
                continue;
            }
            for (const char* mediaSlot : card->media)
            {
                MediaRelease release;
                release.slot = removed.slot;
                release.card = removed.card;
                release.mediaSlot = mediaSlot;
                release.dirty = std::find(_context.dirtyMedia.begin(), _context.dirtyMedia.end(), mediaSlot) !=
                                _context.dirtyMedia.end();
                release.disposition = _request.mediaDisposition;
                if (release.dirty && release.disposition == MediaDisposition::None)
                {
                    Refuse(Rule::D11, "medium " + Quoted(mediaSlot) + " of " + Quoted(removed.slot) + " " +
                                          Quoted(removed.card) + " has unsaved changes: give a disposition (save / discard)",
                           "dirty medium");
                }
                _plan.media.push_back(std::move(release));
            }
        }
    }

    void BuildResultingSlots(const std::string& adapterId)
    {
        SlotSet result;
        for (const SlotEntry& entry : _slots)
        {
            if (IsRemoved(entry.slot))
            {
                continue;
            }
            result.push_back(entry);
        }

        const bool socketCard = _card->bus == BusKind::AySocket;
        const bool socketEmptied = std::any_of(_plan.removedFromSocket.begin(), _plan.removedFromSocket.end(),
                                               [](const RemovedFromSocket& removed) { return removed.socket == kAySocketBus; });
        auto dropSocket = [&result]() {
            result.erase(std::remove_if(result.begin(), result.end(),
                                        [](const SlotEntry& entry) { return entry.slot == kAySocketBus; }),
                         result.end());
        };

        if (socketCard)
        {
            dropSocket();
            if (!_card->socketDefault)
            {
                SlotEntry entry;
                entry.slot = _plan.slot;
                entry.card = _card->id;
                entry.options = _options;
                result.push_back(entry);
            }
        }
        else
        {
            SlotEntry entry;
            entry.slot = _plan.slot;
            entry.card = _card->id;
            entry.options = _options;
            entry.adapter = adapterId;
            entry.unrealistic = _plan.fit == Fit::Unrealistic;
            result.push_back(entry);
            if (socketEmptied)
            {
                dropSocket();
                SlotEntry empty;
                empty.slot = std::string(kAySocketBus);
                empty.card = std::string(kEmptySocket);
                result.push_back(empty);
            }
        }

        for (const DisabledCard& disabled : _plan.disabled)
        {
            for (SlotEntry& entry : result)
            {
                if (entry.slot == disabled.slot)
                {
                    entry.disabled = true;
                    entry.disabledReason = disabled.reason;
                }
            }
        }
        _plan.resultingSlots = _planner.Canonical(_machine, result);
    }

    // endregion

    void Remove()
    {
        const SlotEntry* entry = Find(_request.slot);
        const CardDef* card = entry != nullptr ? _planner.FindCard(entry->card) : nullptr;
        _plan.slot = _request.slot;
        if (entry == nullptr || card == nullptr || card->socketDefault)
        {
            Refuse(Rule::Request, Quoted(_request.slot) + " holds no card to remove", "empty slot");
            return;
        }
        _plan.card = entry->card;
        RemoveCard(*entry, Rule::Request, "removed on request", {}, false, false);
        CollectLostFunctions();
        CollectMedia();

        SlotSet result;
        for (const SlotEntry& other : _slots)
        {
            if (other.slot != entry->slot)
            {
                result.push_back(other);
            }
        }
        _plan.resultingSlots = _planner.Canonical(_machine, result);
    }

    const SlotPlanner& _planner;
    const MachineDef& _machine;
    const SlotRequest& _request;
    const PlanContext& _context;
    SlotPlan& _plan;
    SlotSet _slots;
    const CardDef* _card = nullptr;
    CardOptions _options;
    const BusDef* _bus = nullptr;
    std::vector<FunctionUse> _functions;
    std::vector<PortClaim> _claims;
};

} // namespace

// region <Options and formatting>

uint32_t OptionBits(const CardDef& card, const CardOptions& options, Opt key)
{
    const OptionDef* option = FindOption(card, key);
    if (option == nullptr)
    {
        return 0;
    }
    return options.Has(key) ? options.bits[static_cast<size_t>(key)] : option->defaultBits;
}

std::vector<PortClaim> CardClaims(const CardDef& card, const CardOptions& options)
{
    return ActiveClaims(card, options);
}

bool ParseCardOptions(const CardDef& card, std::string_view text, CardOptions& out, std::string* error)
{
    auto fail = [error](std::string message) {
        if (error != nullptr)
        {
            *error = std::move(message);
        }
        return false;
    };

    size_t position = 0;
    while (position < text.size())
    {
        while (position < text.size() && text[position] == ' ')
        {
            position++;
        }
        if (position >= text.size())
        {
            break;
        }
        size_t end = text.find(' ', position);
        if (end == std::string_view::npos)
        {
            end = text.size();
        }
        const std::string_view item = text.substr(position, end - position);
        position = end;

        const size_t equals = item.find('=');
        if (equals == std::string_view::npos)
        {
            return fail("expected name=value, got " + Quoted(item));
        }
        const std::string_view name = item.substr(0, equals);
        const std::string_view value = item.substr(equals + 1);
        const OptionDef* option = FindOptionByName(card, name);
        if (option == nullptr)
        {
            return fail(Quoted(card.id) + " has no option " + Quoted(name));
        }

        uint32_t bits = 0;
        std::string_view rest = value;
        while (!rest.empty())
        {
            const size_t comma = rest.find(',');
            const std::string_view token = rest.substr(0, comma);
            rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
            if (token.empty() || (option->kind == OptionKind::Set && token == "none"))
            {
                continue;
            }
            bool found = false;
            for (size_t index = 0; index < option->values.size(); index++)
            {
                if (token == option->values[index].id)
                {
                    bits |= Bit(static_cast<int>(index));
                    found = true;
                }
            }
            if (!found)
            {
                return fail(Quoted(name) + " has no value " + Quoted(token));
            }
        }
        if (option->kind == OptionKind::Enum && std::popcount(bits) != 1)
        {
            return fail(Quoted(name) + " takes exactly one value");
        }
        out.Set(option->key, bits);
    }
    return true;
}

std::string FormatCardOptions(const CardDef& card, const CardOptions& options)
{
    std::string result;
    for (const OptionDef& option : card.options)
    {
        const uint32_t bits = OptionBits(card, options, option.key);
        std::string values;
        for (size_t index = 0; index < option.values.size(); index++)
        {
            if (bits & Bit(static_cast<int>(index)))
            {
                values += (values.empty() ? "" : ",") + std::string(option.values[index].id);
            }
        }
        if (values.empty())
        {
            values = "none";
        }
        result += (result.empty() ? "" : " ") + std::string(Describe(option.key).id) + "=" + values;
    }
    return result;
}

std::string FormatPort(uint16_t port)
{
    return Hex(port, port <= 0xFF ? 2 : 4);
}

std::string FormatPortRange(uint16_t mask, uint16_t match)
{
    if (mask == 0x00FF)
    {
        return Hex(match, 2);
    }
    if (mask == 0xFFFF)
    {
        return Hex(match, 4);
    }
    const int digits = mask <= 0xFF ? 2 : 4;
    return Hex(mask, digits) + "/" + Hex(match, digits);
}

// endregion

// region <SlotPlanner>

SlotPlanner::SlotPlanner(const Collection& collection) : _collection(collection)
{
}

SlotPlan SlotPlanner::Plan(MEM_MODEL model, const SlotSet& slots, const SlotRequest& request,
                           const PlanContext& context) const
{
    SlotPlan plan;
    plan.dryRun = request.dryRun;
    plan.op = request.op;
    plan.slot = request.slot;
    plan.card = request.card;

    const MachineDef* machine = FindMachine(model);
    if (machine == nullptr)
    {
        plan.reasons.push_back({ Rule::Request, true, "the model has no slot declaration", "no slots" });
        plan.hardRefusal = true;
        return plan;
    }

    PlanBuilder builder(*this, *machine, request, context, plan);
    builder.Run(slots);
    builder.Finish();
    return plan;
}

const CardDef* SlotPlanner::FindCard(std::string_view id) const
{
    for (const CardDef& card : _collection.cards)
    {
        if (id == card.id)
        {
            return &card;
        }
    }
    return nullptr;
}

const MachineDef* SlotPlanner::FindMachine(MEM_MODEL model) const
{
    for (const MachineDef& machine : _collection.machines)
    {
        if (machine.model == model)
        {
            return &machine;
        }
    }
    return nullptr;
}

const AdapterDef* SlotPlanner::FindAdapter(std::string_view id) const
{
    for (const AdapterDef& adapter : _collection.adapters)
    {
        if (id == adapter.id)
        {
            return &adapter;
        }
    }
    return nullptr;
}

std::string SlotPlanner::SuggestSlot(const MachineDef& machine, const CardDef& card) const
{
    if (card.bus == BusKind::AySocket)
    {
        for (const BusDef& bus : machine.buses)
        {
            if (bus.kind == BusKind::AySocket)
            {
                return bus.id;
            }
        }
        return {};
    }
    for (const BusDef& bus : machine.buses)
    {
        if (bus.kind == card.bus)
        {
            return std::string(bus.id) + ".next";
        }
    }
    for (const BusDef& bus : machine.buses)
    {
        for (const AdapterDef& adapter : _collection.adapters)
        {
            if (adapter.cardSide == card.bus && adapter.machineSide == bus.kind)
            {
                return std::string(bus.id) + ".next";
            }
        }
    }
    for (const BusDef& bus : machine.buses)
    {
        if (bus.kind != BusKind::AySocket)
        {
            return std::string(bus.id) + ".next";
        }
    }
    return {};
}

SlotSet SlotPlanner::Canonical(const MachineDef& machine, const SlotSet& slots) const
{
    auto busIndex = [&machine](const SlotEntry& entry) {
        const std::string busId = BusIdOf(entry.slot);
        for (size_t i = 0; i < machine.buses.size(); i++)
        {
            if (busId == machine.buses[i].id)
            {
                return i;
            }
        }
        return machine.buses.size();
    };
    SlotSet result = slots;
    std::stable_sort(result.begin(), result.end(), [&](const SlotEntry& a, const SlotEntry& b) {
        const size_t busA = busIndex(a);
        const size_t busB = busIndex(b);
        if (busA != busB)
        {
            return busA < busB;
        }
        const int numberA = SlotNumber(a.slot);
        const int numberB = SlotNumber(b.slot);
        if (numberA != numberB)
        {
            return numberA < numberB;
        }
        return a.slot < b.slot;
    });
    return result;
}

// endregion

std::string ToText(const SlotPlan& plan)
{
    static const char* const kFits[] = { "real", "adapter", "unrealistic" };
    std::string text = std::string(plan.allowed ? "allowed" : "refused") + (plan.hardRefusal ? " (hard)" : "") +
                       (plan.needsConfirmation ? " (needs confirmation)" : "") + ": " + plan.card + " -> " +
                       plan.slot + ", fit " + kFits[static_cast<int>(plan.fit)] + ", arbitration " +
                       Describe(plan.arbitration).id + "\n";
    for (const PlanReason& reason : plan.reasons)
    {
        text += std::string("  reason") + (reason.hard ? " (hard)" : "") + ": " + reason.text + "\n";
    }
    for (const RemovedCard& removed : plan.removed)
    {
        text += "  removed: " + removed.slot + " " + removed.card + " [" + removed.optionsText + "] " + removed.reason + "\n";
    }
    for (const ShadowedDevice& shadowed : plan.shadowed)
    {
        text += "  shadowed: " + shadowed.device + " by " + shadowed.by + " " + PortList(shadowed.ports) + "\n";
    }
    for (const RemovedFromSocket& removed : plan.removedFromSocket)
    {
        text += "  removed from socket: " + removed.builtIn + " (" + removed.chip + ")\n";
    }
    for (const SwitchedOffBuiltIn& off : plan.builtInSwitchedOff)
    {
        text += "  switched off: " + off.builtIn + "\n";
    }
    for (const DisabledCard& disabled : plan.disabled)
    {
        text += "  disabled: " + disabled.slot + " " + disabled.card + ": " + disabled.reason + "\n";
    }
    for (const DeadPort& dead : plan.deadPorts)
    {
        text += "  dead port: " + FormatPortRange(dead.mask, dead.match) + "\n";
    }
    for (const BusFight& fight : plan.busFights)
    {
        text += "  bus fight: " + fight.device + " " + FormatPort(fight.port) + "\n";
    }
    if (!plan.lostFunctions.empty())
    {
        text += "  lost: " + FunctionList(plan.lostFunctions) + "\n";
    }
    for (const MediaRelease& media : plan.media)
    {
        text += "  media: " + media.slot + " " + media.mediaSlot + (media.dirty ? " (dirty)" : "") + "\n";
    }
    for (const SlotEntry& entry : plan.resultingSlots)
    {
        text += "  -> " + entry.slot + " = " + entry.card + (entry.disabled ? " (disabled)" : "") +
                (entry.unrealistic ? " (unrealistic)" : "") + "\n";
    }
    return text;
}

} // namespace slots
