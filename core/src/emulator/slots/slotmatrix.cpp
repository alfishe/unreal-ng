#include "slotmatrix.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "emulator/platform.h"
#include "emulator/slots/slotplanner.h"

namespace slots
{

namespace
{

constexpr std::string_view kTableNames[] = { "functions", "cards", "card-x-card", "machines", "card-x-machine" };

/// Card × card outcomes are computed on this machine: a CardWins ZX-bus with an AY socket, the setting the first
/// catalog was designed for (any machine with such buses gives the same card-versus-card outcomes)
constexpr MEM_MODEL kPairMachine = MM_PENTAGON;

std::string Quoted(std::string_view text)
{
    return "`" + std::string(text) + "`";
}

std::string Join(const std::vector<std::string>& items, std::string_view separator)
{
    std::string result;
    for (const std::string& item : items)
    {
        if (!result.empty())
        {
            result += separator;
        }
        result += item;
    }
    return result;
}

std::string Row(const std::vector<std::string>& cells)
{
    return "| " + Join(cells, " | ") + " |\n";
}

std::string Header(const std::vector<std::string>& cells)
{
    std::string separator = "|";
    for (size_t i = 0; i < cells.size(); i++)
    {
        separator += "---|";
    }
    return Row(cells) + separator + "\n";
}

std::string Signals(SignalSet signals)
{
    std::vector<std::string> names;
    for (int i = 0; i < kBusSignalCount; i++)
    {
        if (signals & (1u << i))
        {
            names.emplace_back(Describe(SignalAt(i)).id);
        }
    }
    return names.empty() ? "-" : Join(names, ", ");
}

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

/// "`ym`", "mode 2", "`#EF` build" (several values joined by " or ")
std::string WhenLabel(const CardDef& card, const When& when)
{
    const OptionDef* option = FindOption(card, when.option);
    if (option == nullptr)
    {
        return {};
    }
    std::vector<std::string> labels;
    for (size_t i = 0; i < option->values.size(); i++)
    {
        if (when.anyOf & Bit(static_cast<int>(i)))
        {
            labels.emplace_back(option->values[i].label);
        }
    }
    return Join(labels, " or ");
}

/// A claim as the tables show it: `#FF/#B3` (mask / match)
std::string ClaimRange(const PortClaim& claim)
{
    const int digits = claim.mask <= 0xFF ? 2 : 4;
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "#%0*X/#%0*X", digits, claim.mask, digits, claim.match);
    return Quoted(buffer);
}

std::string ClaimName(const PortClaim& claim)
{
    return Quoted(claim.port != 0 ? FormatPort(claim.port) : FormatPortRange(claim.mask, claim.match));
}

// region <Variants: the option values that change a card's functions or claims>

struct Variant
{
    std::string label;
    CardOptions options;
};

std::vector<Variant> Variants(const CardDef& card)
{
    std::vector<Opt> relevant;
    auto note = [&relevant](const When& when) {
        if (when.option != Opt::None && std::find(relevant.begin(), relevant.end(), when.option) == relevant.end())
        {
            relevant.push_back(when.option);
        }
    };
    for (const FunctionUse& use : card.functions)
    {
        note(use.when);
    }
    for (const PortClaim& claim : card.claims)
    {
        note(claim.when);
    }

    std::vector<Variant> variants{ Variant{} };
    for (Opt key : relevant)
    {
        const OptionDef* option = FindOption(card, key);
        std::vector<Variant> next;
        for (const Variant& base : variants)
        {
            for (size_t i = 0; i < option->values.size(); i++)
            {
                // A set option: one member at a time (which member makes the outcome); an enum option: each value
                Variant variant = base;
                variant.options.Set(key, Bit(static_cast<int>(i)));
                variant.label = (base.label.empty() ? "" : base.label + ", ") + option->values[i].label;
                next.push_back(variant);
            }
        }
        variants = next;
    }
    return variants;
}

/// The card × machine table splits a card into one row per value of its splitMatrixRows options
std::vector<Variant> RowVariants(const CardDef& card)
{
    std::vector<Variant> variants{ Variant{} };
    for (const OptionDef& option : card.options)
    {
        if (!option.splitMatrixRows)
        {
            continue;
        }
        std::vector<Variant> next;
        for (const Variant& base : variants)
        {
            for (size_t i = 0; i < option.values.size(); i++)
            {
                Variant variant = base;
                variant.options.Set(option.key, Bit(static_cast<int>(i)));
                variant.label = (base.label.empty() ? "" : base.label + ", ") + option.values[i].label;
                next.push_back(variant);
            }
        }
        variants = next;
    }
    return variants;
}

// endregion

std::vector<const CardDef*> CatalogCards(const Collection& collection)
{
    std::vector<const CardDef*> cards;
    for (const CardDef& card : collection.cards)
    {
        if (!card.socketDefault)
        {
            cards.push_back(&card);
        }
    }
    return cards;
}

// region <§1, §2>

std::string RenderFunctions()
{
    std::string text = Header({ "Function", "Meaning", "Ports involved" });
    for (int i = 0; i < static_cast<int>(Function::Count); i++)
    {
        const FunctionInfo& info = Describe(static_cast<Function>(i));
        text += Row({ Quoted(info.id), info.meaning, info.ports });
    }
    return text;
}

std::string RenderCards(const Collection& collection)
{
    std::string text = Header({ "Card id", "Card", "Bus", "Needs", "Detection", "Functions", "Ports (mask / match)",
                                "IORQGE", "Options" });
    for (const CardDef& card : collection.cards)
    {
        const bool socket = card.bus == BusKind::AySocket;

        std::vector<std::string> functions;
        for (const FunctionUse& use : card.functions)
        {
            std::string item = Quoted(Describe(use.function).id);
            if (use.role == Role::Takeover)
            {
                item += " (takeover)";
            }
            if (use.when.option != Opt::None)
            {
                item += " if " + WhenLabel(card, use.when);
            }
            functions.push_back(item);
        }

        std::vector<std::string> ports;
        std::vector<std::string> iorqgePorts;
        bool allSame = true;
        for (const PortClaim& claim : card.claims)
        {
            std::vector<std::string> notes;
            if (claim.when.option != Opt::None)
            {
                notes.push_back(WhenLabel(card, claim.when));
            }
            if (claim.gate != Gate::Always)
            {
                notes.emplace_back(Describe(claim.gate).id);
            }
            if (claim.dir != Dir::InOut)
            {
                notes.emplace_back(claim.dir == Dir::In ? "read" : "write");
            }
            if (claim.romLock)
            {
                notes.emplace_back("ROM lock");
            }
            ports.push_back(ClaimRange(claim) + (notes.empty() ? "" : " (" + Join(notes, ", ") + ")"));
            if (claim.iorqge != card.claims.front().iorqge)
            {
                allSame = false;
            }
            if (claim.iorqge != Iorqge::No)
            {
                const std::string name = ClaimName(claim);
                if (std::find(iorqgePorts.begin(), iorqgePorts.end(), name) == iorqgePorts.end())
                {
                    iorqgePorts.push_back(name);
                }
            }
        }

        std::string iorqge;
        if (socket)
        {
            iorqge = "host";
        }
        else if (card.claims.empty())
        {
            iorqge = "-";
        }
        else if (allSame)
        {
            iorqge = Describe(card.claims.front().iorqge).id;
        }
        else
        {
            iorqge = Join(iorqgePorts, ", ");
        }

        std::vector<std::string> options;
        for (const OptionDef& option : card.options)
        {
            std::vector<std::string> values;
            std::vector<std::string> defaults;
            for (size_t i = 0; i < option.values.size(); i++)
            {
                values.push_back(Quoted(option.values[i].id));
                if (option.defaultBits & Bit(static_cast<int>(i)))
                {
                    defaults.push_back(Quoted(option.values[i].id));
                }
            }
            if (option.kind == OptionKind::Set)
            {
                const bool all = defaults.size() == option.values.size();
                options.push_back(Quoted(Describe(option.key).id) + " = any of " + Join(values, ", ") + " (default " +
                                  (all ? std::string("all") : defaults.empty() ? std::string("none") : Join(defaults, ", ")) + ")");
            }
            else
            {
                options.push_back(Quoted(Describe(option.key).id) + " = " + Join(values, " / ") + " (default " +
                                  Join(defaults, "") + ")");
            }
        }

        text += Row({ Quoted(card.id), card.name, Describe(card.bus).id, Signals(card.needs),
                      socket ? "-" : Describe(card.detection).id, Join(functions, ", "),
                      socket ? std::string(card.portsNote) : Join(ports, ", "), iorqge,
                      options.empty() ? "-" : Join(options, "; ") });
    }
    return text;
}

// endregion

// region <§3 card × card>

Outcome PairOutcome(const SlotPlanner& planner, const CardDef& newCard, const CardOptions& newOptions,
                    const CardDef& installed, const CardOptions& installedOptions)
{
    const bool newInSocket = newCard.bus == BusKind::AySocket;
    const bool installedInSocket = installed.bus == BusKind::AySocket;
    const std::string installedSlot = installedInSocket ? "ay-socket" : "zxbus.1";

    SlotEntry entry;
    entry.slot = installedSlot;
    entry.card = installed.id;
    entry.options = installedOptions;

    SlotRequest request;
    request.op = SlotRequest::Op::Plug;
    request.slot = newInSocket ? "ay-socket" : "zxbus.2";
    request.card = newCard.id;
    request.options = newOptions;
    request.replaceIfIncompatible = true;

    const SlotPlan plan = planner.Plan(kPairMachine, SlotSet{ entry }, request);
    if (plan.hardRefusal)
    {
        return Outcome::Refused;
    }
    for (const RemovedCard& removed : plan.removed)
    {
        if (removed.slot == installedSlot)
        {
            return removed.replacedInSlot ? Outcome::Replaces : removed.pointless ? Outcome::Pointless : Outcome::Displaced;
        }
    }
    for (const DisabledCard& disabled : plan.disabled)
    {
        if (disabled.slot == installedSlot || disabled.clashSlot == installedSlot)
        {
            return Outcome::PortClash;
        }
    }
    return Outcome::Coexist;
}

/// One cell from the grid of outcomes over both cards' variants: one code when every variant agrees, else each
/// non-✓ code with the variants that produce it ("**D** if `gs`", "**D** if mode 2")
std::string PairCell(const std::vector<std::vector<Outcome>>& grid, const std::vector<Variant>& rows,
                     const std::vector<Variant>& columns)
{
    const Outcome first = grid[0][0];
    bool uniform = true;
    for (const auto& line : grid)
    {
        for (Outcome outcome : line)
        {
            uniform = uniform && outcome == first;
        }
    }
    if (uniform)
    {
        return Describe(first).id;
    }

    std::vector<std::string> parts;
    for (int code = 0; code < static_cast<int>(Outcome::Count); code++)
    {
        const Outcome outcome = static_cast<Outcome>(code);
        if (outcome == Outcome::Coexist)
        {
            continue;
        }
        std::vector<size_t> rowHits;
        std::vector<size_t> columnHits;
        bool any = false;
        for (size_t r = 0; r < rows.size(); r++)
        {
            bool whole = true;
            for (size_t c = 0; c < columns.size(); c++)
            {
                any = any || grid[r][c] == outcome;
                whole = whole && grid[r][c] == outcome;
            }
            if (whole)
            {
                rowHits.push_back(r);
            }
        }
        if (!any)
        {
            continue;
        }
        for (size_t c = 0; c < columns.size(); c++)
        {
            bool whole = true;
            for (size_t r = 0; r < rows.size(); r++)
            {
                whole = whole && grid[r][c] == outcome;
            }
            if (whole)
            {
                columnHits.push_back(c);
            }
        }

        auto cellsMatch = [&](auto predicate) {
            for (size_t r = 0; r < rows.size(); r++)
            {
                for (size_t c = 0; c < columns.size(); c++)
                {
                    if ((grid[r][c] == outcome) != predicate(r, c))
                    {
                        return false;
                    }
                }
            }
            return true;
        };
        auto contains = [](const std::vector<size_t>& list, size_t value) {
            return std::find(list.begin(), list.end(), value) != list.end();
        };

        std::vector<std::string> conditions;
        if (cellsMatch([&](size_t r, size_t) { return contains(rowHits, r); }))
        {
            for (size_t r : rowHits)
            {
                conditions.push_back(rows[r].label);
            }
        }
        else if (cellsMatch([&](size_t, size_t c) { return contains(columnHits, c); }))
        {
            for (size_t c : columnHits)
            {
                conditions.push_back(columns[c].label);
            }
        }
        else
        {
            for (size_t r = 0; r < rows.size(); r++)
            {
                for (size_t c = 0; c < columns.size(); c++)
                {
                    if (grid[r][c] == outcome)
                    {
                        conditions.push_back(rows[r].label + " + " + columns[c].label);
                    }
                }
            }
        }
        parts.push_back(std::string(Describe(outcome).id) + " if " + Join(conditions, " or "));
    }
    return Join(parts, "; ");
}

std::string RenderCardByCard(const Collection& collection)
{
    const SlotPlanner planner(collection);
    const std::vector<const CardDef*> cards = CatalogCards(collection);

    std::vector<std::string> header{ "new \\ installed" };
    for (const CardDef* card : cards)
    {
        header.push_back(Quoted(card->id));
    }
    std::string text = Header(header);

    for (const CardDef* newCard : cards)
    {
        std::vector<std::string> row{ "**" + std::string(newCard->id) + "**" };
        for (const CardDef* installed : cards)
        {
            // A card against another of its own kind: both at their defaults
            // (one default-constructed Variant: no initializer_list temporary in a conditional, which
            // gcc 16 flags as -Wdangling-pointer)
            const bool sameCard = newCard == installed;
            const std::vector<Variant> rows = sameCard ? std::vector<Variant>(1) : Variants(*newCard);
            const std::vector<Variant> columns = sameCard ? std::vector<Variant>(1) : Variants(*installed);
            std::vector<std::vector<Outcome>> grid(rows.size(), std::vector<Outcome>(columns.size()));
            for (size_t r = 0; r < rows.size(); r++)
            {
                for (size_t c = 0; c < columns.size(); c++)
                {
                    grid[r][c] = PairOutcome(planner, *newCard, rows[r].options, *installed, columns[c].options);
                }
            }
            row.push_back(PairCell(grid, rows, columns));
        }
        text += Row(row);
    }
    return text;
}

// endregion

// region <§4 machines, card × machine>

std::string BusLabel(const BusDef& bus)
{
    if (bus.kind == BusKind::AySocket)
    {
        return Quoted(bus.id);
    }
    const std::string kind = Describe(bus.kind).id;
    if (bus.retrofit)
    {
        return Quoted(bus.id) + " (retrofit, no physical slots)";
    }
    const std::string count = std::to_string(bus.physicalSlots);
    return Quoted(bus.id) + (kind == bus.id ? " (" + count + ")" : " (" + kind + ", " + count + ")");
}

std::string RenderMachines(const Collection& collection)
{
    std::string text = Header({ "Model", "Board", "Buses (physical slots)", "Arbitration", "+12 V", "Built-ins",
                                "Board ports hidden from the slots", "Notes" });
    for (const MachineDef& machine : collection.machines)
    {
        std::vector<const BusDef*> slotBuses;
        std::vector<std::string> buses;
        std::vector<std::string> notes;
        for (const BusDef& bus : machine.buses)
        {
            buses.push_back(BusLabel(bus));
            if (bus.kind != BusKind::AySocket)
            {
                slotBuses.push_back(&bus);
            }
            if (bus.note[0] != '\0')
            {
                notes.push_back(Quoted(bus.id) + ": " + bus.note);
            }
        }
        const bool prefix = slotBuses.size() > 1;

        std::vector<std::string> arbitration;
        std::vector<std::string> power;
        std::vector<std::string> boardPorts;
        for (const BusDef* bus : slotBuses)
        {
            std::string item = (prefix ? Quoted(bus->id) + ": " : "") + Describe(bus->arbitration).id;
            for (const AdapterDef& adapter : collection.adapters)
            {
                if (adapter.machineSide == bus->kind && adapter.hasArbitration)
                {
                    item += " (adapter " + Quoted(adapter.id) + ": " + Describe(adapter.arbitration).id + ")";
                }
            }
            arbitration.push_back(item);
            power.push_back((prefix ? Quoted(bus->id) + ": " : "") +
                            ((bus->signals & Sig(BusSignal::Plus12V)) != 0 ? "yes" : "no"));
            for (const PortClaim& port : bus->boardPorts)
            {
                std::string item2 = Quoted(FormatPortRange(port.mask, port.match));
                if (port.gate != Gate::Always)
                {
                    item2 += std::string(" (") + Describe(port.gate).id + ")";
                }
                boardPorts.push_back(item2);
            }
        }

        std::vector<std::string> builtIns;
        for (const BuiltInDef& builtIn : machine.builtIns)
        {
            std::string item = Quoted(builtIn.id);
            if (builtIn.kind == BuiltInKind::Socketed)
            {
                item += std::string(" (socketed ") + builtIn.chip + ")";
            }
            else if (builtIn.kind == BuiltInKind::Switchable)
            {
                item += " (switchable)";
            }
            builtIns.push_back(item);
        }

        text += Row({ machine.name, machine.variant, Join(buses, ", "), Join(arbitration, "; "), Join(power, "; "),
                      builtIns.empty() ? "-" : Join(builtIns, ", "), boardPorts.empty() ? "-" : Join(boardPorts, " "),
                      notes.empty() ? "-" : Join(notes, "; ") });
    }
    return text;
}

std::string MachineCell(const SlotPlanner& planner, const MachineDef& machine, const CardDef& card,
                        const CardOptions& options)
{
    SlotRequest request;
    request.op = SlotRequest::Op::Plug;
    request.card = card.id;
    request.options = options;
    request.replaceIfIncompatible = true;
    const SlotPlan plan = planner.Plan(machine.model, SlotSet{}, request);

    for (const PlanReason& reason : plan.reasons)
    {
        if (reason.hard)
        {
            return std::string(Describe(Outcome::Refused).id) + " (" + reason.brief + ")";
        }
    }
    if (plan.fit != Fit::Real)
    {
        for (const PlanReason& reason : plan.reasons)
        {
            if (reason.rule == Rule::D8 && !reason.brief.empty())
            {
                return std::string(Describe(Outcome::NeedsAdapter).id) + " (" + reason.brief + ")";
            }
        }
        return Describe(Outcome::NeedsAdapter).id;
    }

    std::vector<std::string> parts;
    for (const RemovedFromSocket& removed : plan.removedFromSocket)
    {
        parts.push_back(std::string(Describe(Outcome::RemovedFromSocket).id) + " (" + Quoted(removed.builtIn) + " " +
                        removed.chip + " out of its socket)");
    }
    for (const ShadowedDevice& shadowed : plan.shadowed)
    {
        parts.push_back(std::string(Describe(Outcome::Shadowed).id) + " (" + Quoted(shadowed.device) + ")");
    }
    for (const SwitchedOffBuiltIn& off : plan.builtInSwitchedOff)
    {
        parts.push_back("switches off " + Quoted(off.builtIn));
    }
    for (const BusFight& fight : plan.busFights)
    {
        parts.push_back("bus fight " + Quoted(FormatPort(fight.port)) + " with " + Quoted(fight.device));
    }
    if (!plan.deadPorts.empty())
    {
        std::vector<std::string> ports;
        for (const DeadPort& dead : plan.deadPorts)
        {
            ports.push_back(Quoted(FormatPortRange(dead.mask, dead.match)));
        }
        parts.push_back(std::string(Describe(Outcome::PartlyDead).id) + " (" + Join(ports, ", ") +
                        (ports.size() == 1 ? " is a board port)" : " are board ports)"));
    }
    if (!plan.disabled.empty())
    {
        parts.emplace_back(Describe(Outcome::PortClash).id);
    }
    return parts.empty() ? std::string(Describe(Outcome::Coexist).id) : Join(parts, ", ");
}

std::string RenderCardByMachine(const Collection& collection)
{
    const SlotPlanner planner(collection);

    struct RowDef
    {
        std::string label;
        const CardDef* card;
        CardOptions options;
    };
    std::vector<RowDef> rows;
    for (const CardDef* card : CatalogCards(collection))
    {
        for (const Variant& variant : RowVariants(*card))
        {
            rows.push_back({ "**" + std::string(card->id) + "**" + (variant.label.empty() ? "" : " " + variant.label),
                             card, variant.options });
        }
    }

    // One column per machine, adjacent machines with identical columns merged
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> columns;
    for (const MachineDef& machine : collection.machines)
    {
        std::vector<std::string> column;
        for (const RowDef& row : rows)
        {
            column.push_back(MachineCell(planner, machine, *row.card, row.options));
        }
        if (!columns.empty() && columns.back() == column)
        {
            headers.back() += std::string(" / ") + machine.name;
            continue;
        }
        headers.emplace_back(machine.name);
        columns.push_back(column);
    }

    std::vector<std::string> header{ "Card \\ machine" };
    header.insert(header.end(), headers.begin(), headers.end());
    std::string text = Header(header);
    for (size_t r = 0; r < rows.size(); r++)
    {
        std::vector<std::string> line{ rows[r].label };
        for (const auto& column : columns)
        {
            line.push_back(column[r]);
        }
        text += Row(line);
    }
    return text;
}

// endregion

} // namespace

std::span<const std::string_view> MatrixTableNames()
{
    return kTableNames;
}

std::string RenderMatrixTable(std::string_view name, const Collection& collection)
{
    if (name == "functions")
    {
        return RenderFunctions();
    }
    if (name == "cards")
    {
        return RenderCards(collection);
    }
    if (name == "card-x-card")
    {
        return RenderCardByCard(collection);
    }
    if (name == "machines")
    {
        return RenderMachines(collection);
    }
    if (name == "card-x-machine")
    {
        return RenderCardByMachine(collection);
    }
    return {};
}

std::string MatrixBeginMarker(std::string_view name)
{
    return "<!-- slots:generated:" + std::string(name) + ":begin -->";
}

std::string MatrixEndMarker(std::string_view name)
{
    return "<!-- slots:generated:" + std::string(name) + ":end -->";
}

} // namespace slots
