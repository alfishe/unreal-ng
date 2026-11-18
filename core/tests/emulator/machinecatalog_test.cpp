// The machine catalog (machinecatalog.h): every machine a create request can name, grouped family -> machine -> RAM
// size, the one tree every menu and client reads (docs/inprogress/2026-11-16-machine-families).

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/machinecatalog.h"
#include "emulator/machinevariants.h"
#include "emulator/platform.h"
#include "emulator/zxpoly/zxpolygroup.h"

namespace
{
/// The family id and machine of a name, "" when not found
std::string FamilyOfIn(const std::vector<MachineFamily>& families, const std::string& name)
{
    const MachinePlace place = MachineCatalog::Locate(families, name);
    return place.family < 0 ? std::string() : families[static_cast<size_t>(place.family)].id;
}
}  // namespace

TEST(MachineCatalog_Test, EveryModelVariantAndZxPolyConfigurationIsInExactlyOneFamily)
{
    std::multiset<std::string> names;
    for (const MachineFamily& family : MachineCatalog::All())
        for (const MachineEntry& machine : family.machines)
            names.insert(machine.name);
    size_t expected = 0;
    for (const TMemModel& model : Config::GetAvailableModels())
        if (model.ShortName != nullptr && model.ShortName[0] != '\0')
        {
            ++expected;
            EXPECT_EQ(names.count(model.ShortName), 1u) << model.ShortName;
        }
    for (const MachineVariant& variant : MachineVariants::All())
    {
        ++expected;
        EXPECT_EQ(names.count(variant.name), 1u) << variant.name;
    }
    for (const ZXPolyGroup::Configuration& configuration : ZXPolyGroup::Configurations())
    {
        ++expected;
        EXPECT_EQ(names.count(configuration.name), 1u) << configuration.name;
    }
    EXPECT_EQ(names.size(), expected);
}

TEST(MachineCatalog_Test, AtmTurboHoldsBothBoardsWithTheirRamSizes)
{
    const std::vector<MachineFamily>& all = MachineCatalog::All();
    const MachinePlace place = MachineCatalog::Locate(all, "ATM710");
    ASSERT_GE(place.family, 0);
    const MachineFamily& atm = all[static_cast<size_t>(place.family)];
    EXPECT_EQ(atm.id, "atm");
    ASSERT_EQ(atm.machines.size(), 2u);
    EXPECT_EQ(atm.machines[0].name, "ATM450");
    EXPECT_EQ(atm.machines[0].ramSizesKb, (std::vector<uint32_t>{512, 1024}));
    EXPECT_EQ(atm.machines[1].name, "ATM710");
    EXPECT_EQ(atm.machines[1].ramSizesKb, (std::vector<uint32_t>{128, 256, 512, 1024}));
    EXPECT_EQ(MachineCatalog::Title(atm.machines[1], 512), "ATM-Turbo 2+ v7.10 512K");
}

TEST(MachineCatalog_Test, VariantsAndAliasesFindTheirFamily)
{
    EXPECT_EQ(MachineCatalog::FamilyOf("TSL-VDAC2"), "zx-evo");
    EXPECT_EQ(MachineCatalog::FamilyOf("TSL-VDAC4"), "zx-evo");
    EXPECT_EQ(MachineCatalog::FamilyOf("TSCONF-VDAC4"), "zx-evo");
    EXPECT_EQ(MachineCatalog::FamilyOf("tsconf-vdac2"), "zx-evo") << "a variant alias";
    EXPECT_EQ(MachineCatalog::FamilyOf("TSCONF"), "zx-evo") << "a model alias";
    EXPECT_EQ(MachineCatalog::FamilyOf("PROFI3"), "profi");
    EXPECT_EQ(MachineCatalog::FamilyOf("PROFI-PLUS"), "profi");
    EXPECT_EQ(MachineCatalog::FamilyOf("48K-BETA128"), "sinclair");
    EXPECT_EQ(MachineCatalog::FamilyOf("ZXPOLY-128K"), "zx-poly");
    EXPECT_EQ(MachineCatalog::FamilyOf("NO-SUCH-MACHINE"), "");
}

TEST(MachineCatalog_Test, TheMenuTreeHoldsOnlyCreatableMachines)
{
    const std::vector<MachineFamily> menu = MachineCatalog::Creatable();
    for (const MachineFamily& family : menu)
    {
        EXPECT_FALSE(family.machines.empty()) << family.id;
        for (const MachineEntry& machine : family.machines)
            EXPECT_TRUE(machine.creatable) << machine.name;
    }
    EXPECT_EQ(FamilyOfIn(menu, "PENTAGON"), "pentagon");
    EXPECT_EQ(FamilyOfIn(menu, "P2666"), "pentagon") << "the Pentagon 2.666 LE sits with the Pentagon, after it";
    EXPECT_EQ(FamilyOfIn(menu, "KAY"), "") << "no port decoder or config folder yet";
}

TEST(MachineCatalog_Test, TitleOfNamesTheRunningConfiguration)
{
    // What the Machine menu's read-only entry, the window title and the handheld Model row show
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::string error;
    auto atm = manager->CreateEmulatorWithModelAndRAM("catalog-title", "ATM710", 512, LoggerLevel::LogError, &error);
    ASSERT_NE(atm, nullptr) << error;
    EXPECT_EQ(MachineCatalog::TitleOf(*atm), "ATM-Turbo 2+ v7.10 512K") << "a machine of several RAM sizes names its size";
    manager->RemoveEmulator(atm->GetUUID());

    auto spectrum = manager->CreateEmulatorWithModel("catalog-title", "48K", LoggerLevel::LogError, &error);
    ASSERT_NE(spectrum, nullptr) << error;
    const MachinePlace place = MachineCatalog::Locate(MachineCatalog::All(), "48K");
    ASSERT_GE(place.family, 0);
    EXPECT_EQ(MachineCatalog::TitleOf(*spectrum),
              MachineCatalog::All()[static_cast<size_t>(place.family)].machines[static_cast<size_t>(place.machine)].title)
        << "one RAM size: the title alone";
    manager->RemoveEmulator(spectrum->GetUUID());
}
