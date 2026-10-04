// ZX-bus slots reference data collection (core/src/emulator/slots/refdata/): every entry is complete, cites its
// sources and is consistent with the vocabulary and the emulator's model list
// (docs/inprogress/2026-10-03-zx-bus-slots/reference-data.md §6).

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <filesystem>
#include <set>
#include <string>

#include "_helpers/testpathhelper.h"
#include "emulator/config.h"
#include "emulator/platform.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/refdata/refdata.h"
#include "emulator/slots/slotplanner.h"

using namespace slots;

namespace
{

bool NonEmpty(const char* text)
{
    return text != nullptr && text[0] != '\0';
}

bool SameIgnoringCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
           });
}

/// A claim decodes inside 16 bits: a non-empty mask and no match bit outside it
void ExpectValidClaim(const PortClaim& claim, const std::string& where)
{
    EXPECT_NE(claim.mask, 0) << where << ": a claim must decode at least one address line";
    EXPECT_EQ(claim.match & ~claim.mask, 0) << where << ": match bits outside the mask";
    if (claim.port != 0)
    {
        EXPECT_EQ(claim.port & claim.mask, claim.match) << where << ": the documented port is not in the claim";
    }
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

void ExpectValidWhen(const CardDef& card, const When& when, const std::string& where)
{
    if (when.option == Opt::None)
    {
        EXPECT_EQ(when.anyOf, 0u) << where;
        return;
    }
    const OptionDef* option = FindOption(card, when.option);
    ASSERT_NE(option, nullptr) << where << ": the condition names option " << Describe(when.option).id
                               << " the card does not have";
    const uint32_t valid = (1u << option->values.size()) - 1;
    EXPECT_NE(when.anyOf, 0u) << where << ": empty condition";
    EXPECT_EQ(when.anyOf & ~valid, 0u) << where << ": condition names a value the option does not have";
}

void ExpectSources(std::span<const Src> sources, const std::string& where)
{
    EXPECT_FALSE(sources.empty()) << where << " cites no source";
    for (Src source : sources)
    {
        const bool known = std::any_of(refdata::Sources().begin(), refdata::Sources().end(),
                                       [source](const SourceRef& ref) { return ref.id == source; });
        EXPECT_TRUE(known) << where << " cites source " << static_cast<int>(source) << " missing from sources.cpp";
    }
}

} // namespace

TEST(RefData_Test, CollectionIsConsistent)
{
    // region <Vocabulary: every enum value has an id and a description>
    for (int i = 0; i < static_cast<int>(Function::Count); i++)
    {
        const FunctionInfo& info = Describe(static_cast<Function>(i));
        EXPECT_TRUE(NonEmpty(info.id) && NonEmpty(info.meaning) && NonEmpty(info.ports)) << "function " << i;
    }
    for (int i = 0; i < kBusSignalCount; i++)
    {
        const EnumInfo info = Describe(SignalAt(i));
        EXPECT_TRUE(NonEmpty(info.id) && NonEmpty(info.description)) << "signal " << i;
    }
    auto expectEnum = [](auto count, auto describe, const char* what) {
        for (int i = 0; i < static_cast<int>(count); i++)
        {
            const EnumInfo info = describe(i);
            EXPECT_TRUE(NonEmpty(info.description)) << what << " " << i << " has no description";
            EXPECT_TRUE(NonEmpty(info.id) || (std::string_view(what) == "Opt" && i == 0)) << what << " " << i << " has no id";
        }
    };
    expectEnum(BusKind::Count, [](int i) { return Describe(static_cast<BusKind>(i)); }, "BusKind");
    expectEnum(Arbitration::Count, [](int i) { return Describe(static_cast<Arbitration>(i)); }, "Arbitration");
    expectEnum(CycleDetection::Count, [](int i) { return Describe(static_cast<CycleDetection>(i)); }, "CycleDetection");
    expectEnum(ReadRule::Count, [](int i) { return Describe(static_cast<ReadRule>(i)); }, "ReadRule");
    expectEnum(Iorqge::Count, [](int i) { return Describe(static_cast<Iorqge>(i)); }, "Iorqge");
    expectEnum(Gate::Count, [](int i) { return Describe(static_cast<Gate>(i)); }, "Gate");
    expectEnum(Role::Count, [](int i) { return Describe(static_cast<Role>(i)); }, "Role");
    expectEnum(BuiltInKind::Count, [](int i) { return Describe(static_cast<BuiltInKind>(i)); }, "BuiltInKind");
    expectEnum(Opt::Count, [](int i) { return Describe(static_cast<Opt>(i)); }, "Opt");
    expectEnum(OptionKind::Count, [](int i) { return Describe(static_cast<OptionKind>(i)); }, "OptionKind");
    expectEnum(Outcome::Count, [](int i) { return Describe(static_cast<Outcome>(i)); }, "Outcome");
    // endregion

    // region <Sources: one entry per id, a title and a direct link (or a repository document that exists)>
    const auto root = TestPathHelper::FindProjectRoot();
    std::set<int> sourceIds;
    for (const SourceRef& ref : refdata::Sources())
    {
        EXPECT_TRUE(sourceIds.insert(static_cast<int>(ref.id)).second) << "source " << ref.title << " listed twice";
        EXPECT_TRUE(NonEmpty(ref.title)) << "source " << static_cast<int>(ref.id);
        const std::string url = ref.url;
        const bool web = url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0;
        if (!web)
        {
            EXPECT_TRUE(url.rfind("docs/", 0) == 0) << ref.title << ": neither a link nor a repository document";
            EXPECT_TRUE(std::filesystem::exists(root / url)) << ref.title << ": " << url << " does not exist";
        }
    }
    EXPECT_EQ(sourceIds.size(), static_cast<size_t>(Src::Count)) << "every Src id needs a SourceRef";
    // endregion

    // region <Cards>
    std::set<std::string> cardIds;
    int socketDefaults = 0;
    for (const CardDef& card : refdata::Cards())
    {
        const std::string where = std::string("card ") + card.id;
        EXPECT_TRUE(cardIds.insert(card.id).second) << where << " listed twice";
        EXPECT_TRUE(NonEmpty(card.id) && NonEmpty(card.name)) << where;
        EXPECT_FALSE(card.functions.empty()) << where << " holds no function";
        ExpectSources(card.sources, where);
        socketDefaults += card.socketDefault ? 1 : 0;

        const bool socket = card.bus == BusKind::AySocket;
        EXPECT_EQ(socket, card.claims.empty()) << where << ": socket boards inherit the host decode, bus cards claim ports";
        EXPECT_EQ(socket, NonEmpty(card.portsNote)) << where << ": a socket board describes its decode in portsNote";

        std::set<int> keys;
        for (const OptionDef& option : card.options)
        {
            EXPECT_NE(option.key, Opt::None) << where;
            EXPECT_TRUE(keys.insert(static_cast<int>(option.key)).second) << where << " option listed twice";
            EXPECT_FALSE(option.values.empty()) << where << " option without values";
            EXPECT_TRUE(NonEmpty(option.description)) << where;
            const uint32_t valid = (1u << option.values.size()) - 1;
            EXPECT_EQ(option.defaultBits & ~valid, 0u) << where << " default outside the values";
            if (option.kind == OptionKind::Enum)
            {
                EXPECT_EQ(std::popcount(option.defaultBits), 1) << where << " enum default must be one value";
            }
            for (const OptionValue& value : option.values)
            {
                EXPECT_TRUE(NonEmpty(value.id) && NonEmpty(value.label)) << where;
            }
        }
        for (const FunctionUse& use : card.functions)
        {
            EXPECT_LT(static_cast<int>(use.function), static_cast<int>(Function::Count)) << where;
            ExpectValidWhen(card, use.when, where + " function");
        }
        for (const PortClaim& claim : card.claims)
        {
            ExpectValidClaim(claim, where);
            ExpectValidWhen(card, claim.when, where + " claim");
        }
    }
    EXPECT_EQ(socketDefaults, 1) << "exactly one card is the socket's own chip (`ay`)";
    // endregion

    // region <Machines: exactly one MachineDef per creatable model>
    for (int model = 0; model < N_MM_MODELS; model++)
    {
        const auto count = std::count_if(refdata::Machines().begin(), refdata::Machines().end(),
                                         [model](const MachineDef& machine) { return machine.model == model; });
        const bool creatable = PortDecoder::IsModelSupported(static_cast<MEM_MODEL>(model));
        EXPECT_EQ(count, creatable ? 1 : 0) << "model " << model << (creatable ? " is creatable" : " is not creatable");
    }
    for (const MachineDef& machine : refdata::Machines())
    {
        const std::string where = std::string("machine ") + machine.name;
        const TMemModel* model = Config::FindModelByEnum(machine.model);
        ASSERT_NE(model, nullptr) << where;
        EXPECT_TRUE(SameIgnoringCase(machine.name, model->ShortName)) << where << " is " << model->ShortName;
        EXPECT_TRUE(NonEmpty(machine.variant)) << where;
        ExpectSources(machine.sources, where);

        std::set<std::string> busIds;
        bool hasSocketBus = false;
        for (const BusDef& bus : machine.buses)
        {
            EXPECT_TRUE(busIds.insert(bus.id).second) << where << " bus " << bus.id << " twice";
            if (bus.kind == BusKind::AySocket)
            {
                EXPECT_STREQ(bus.id, "ay-socket") << where;
                hasSocketBus = true;
                continue;
            }
            // A retrofitted bus (the board has no connector) has no physical slots; every other bus has some
            if (bus.retrofit)
            {
                EXPECT_EQ(bus.physicalSlots, 0) << where << " bus " << bus.id << ": retrofit means no physical slots";
                EXPECT_NE(bus.note[0], '\0') << where << " bus " << bus.id << ": a retrofit says so in its note";
            }
            else
            {
                EXPECT_GT(bus.physicalSlots, 0) << where << " bus " << bus.id;
            }
            EXPECT_EQ(std::string(bus.id).find('.'), std::string::npos) << where << ": a bus id names slots <id>.<n>";
            for (const PortClaim& port : bus.boardPorts)
            {
                ExpectValidClaim(port, where + " board port");
            }
            if (!bus.boardPorts.empty())
            {
                EXPECT_EQ(bus.arbitration, Arbitration::BoardWins) << where << ": board ports belong to a BoardWins bus";
            }
        }

        std::set<std::string> builtInIds;
        int socketBuiltIns = 0;
        for (const BuiltInDef& builtIn : machine.builtIns)
        {
            EXPECT_TRUE(builtInIds.insert(builtIn.id).second) << where << " built-in " << builtIn.id << " twice";
            EXPECT_TRUE(NonEmpty(builtIn.name)) << where;
            if (builtIn.socket != nullptr)
            {
                socketBuiltIns++;
                EXPECT_STREQ(builtIn.socket, "ay-socket") << where;
            }
            if (builtIn.kind == BuiltInKind::Socketed)
            {
                EXPECT_TRUE(NonEmpty(builtIn.chip)) << where << " socketed built-in " << builtIn.id << " names no chip";
            }
            for (const PortClaim& claim : builtIn.claims)
            {
                ExpectValidClaim(claim, where + " built-in " + builtIn.id);
            }
        }
        EXPECT_EQ(socketBuiltIns, hasSocketBus ? 1 : 0) << where << ": the AY socket's own chip is one built-in";
    }
    // endregion

    // region <Adapters>
    std::set<std::string> adapterIds;
    for (const AdapterDef& adapter : refdata::Adapters())
    {
        const std::string where = std::string("adapter ") + adapter.id;
        EXPECT_TRUE(adapterIds.insert(adapter.id).second) << where << " twice";
        EXPECT_NE(adapter.cardSide, adapter.machineSide) << where;
        ExpectSources(adapter.sources, where);
    }
    // endregion

    // region <Exceptions: complete and not explained by the claims already>
    const SlotPlanner planner;
    for (const ExceptionDef& exception : refdata::Exceptions())
    {
        const std::string where = std::string("exception for ") + exception.card;
        EXPECT_NE(planner.FindCard(exception.card), nullptr) << where;
        EXPECT_TRUE(NonEmpty(exception.reason) && NonEmpty(exception.brief)) << where;
        ExpectSources(exception.sources, where);
        if (exception.anyMachine || exception.installedCard != nullptr)
        {
            continue;   // no such entry yet; the redundancy check below covers one machine, an empty slot set
        }

        SlotRequest request;
        request.card = exception.card;
        request.replaceIfIncompatible = true;
        const SlotPlan with = planner.Plan(exception.model, {}, request);
        PlanContext without;
        without.applyExceptions = false;
        const SlotPlan plain = planner.Plan(exception.model, {}, request, without);
        EXPECT_FALSE(with.allowed) << where;
        EXPECT_TRUE(plain.allowed) << where << " is redundant: the claims already refuse it\n" << ToText(plain);
    }
    // endregion
}
