// The Sprinter's BIOS selection at runtime (sprinterbios.h; automation audit G11): names and aliases, the
// options every interface parses, the create override, the report (which image the flash holds, by CRC-32)
// and a selection that a reset loads (Emulator::RequestRomReload).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <string>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/platform.h"
#include "emulator/ports/models/sprinter/sprinterbios.h"
#include "emulator/state/devicestate.h"
#include "sprinterfixture.h"

namespace
{
const StateNode* Find(const StateNode& node, const char* key) { return node.find(key); }

std::string LoadedFile(const StateNode& report)
{
    const StateNode* loaded = report.find("loaded");
    return loaded ? loaded->s : std::string();
}
}  // namespace

TEST(SprinterBios_Test, NamesAliasesAndOptions)
{
    std::string path, error;
    ASSERT_TRUE(SprinterBios::Resolve("3.06", path, error)) << error;
    EXPECT_EQ(path, "rom/sprinter/sp2k-3.06-hf2.rom");
    ASSERT_TRUE(SprinterBios::Resolve("sp2k-3.07-beta1.rom", path, error)) << error;
    EXPECT_EQ(path, "rom/sprinter/sp2k-3.07-beta1.rom");
    EXPECT_FALSE(SprinterBios::Resolve("9.99", path, error));
    EXPECT_NE(error.find("3.04 (sp2k-3.04.rom)"), std::string::npos) << error;

    SprinterBios::Options options;
    ASSERT_TRUE(SprinterBios::OptionsFromStrings("3.04", "on", "0", "false", options, error)) << error;
    EXPECT_EQ(options.fastStart, 1);
    EXPECT_EQ(options.accelIntSuspend, 0);
    EXPECT_FALSE(options.reset);
    ASSERT_TRUE(SprinterBios::OptionsFromStrings("", "", "", "", options, error));
    EXPECT_EQ(options.fastStart, -1) << "empty = keep the configuration";
    EXPECT_TRUE(options.reset);
    EXPECT_FALSE(SprinterBios::OptionsFromStrings("", "maybe", "", "", options, error));

    CONFIG config{};
    ASSERT_TRUE(SprinterBios::OptionsFromStrings("3.07", "0", "1", "", options, error));
    SprinterBios::CreateOverride(options)(config);
    EXPECT_STREQ(config.sprinter_rom_path, "rom/sprinter/sp2k-3.07-beta1.rom");
    EXPECT_EQ(config.sprinter.fast_start, 0);
    EXPECT_EQ(config.sprinter.accel_int_suspend, 1);

    const std::string check = "123456789";
    EXPECT_EQ(SprinterBios::Crc32(reinterpret_cast<const uint8_t*>(check.data()), check.size()), 0xCBF43926u);
}

class SprinterBiosReport_Test : public SprinterFixture
{
};

// Known issues by image: only 3.07 BETA 1 has one, the floppy driver's IY change (bios-versions.md §5.2)
TEST(SprinterBios_Test, KnownIssuesByImage)
{
    const std::vector<std::string> beta = SprinterBios::KnownIssues(0xA06A1A02u);
    ASSERT_EQ(beta.size(), 1u);
    EXPECT_NE(beta[0].find("IY changed"), std::string::npos) << beta[0];
    EXPECT_NE(beta[0].find("DSS 1.71.57"), std::string::npos) << beta[0];
    EXPECT_NE(beta[0].find("3.06 Hotfix 2"), std::string::npos) << beta[0];
    EXPECT_TRUE(SprinterBios::KnownIssues(0x9AA7BB29u).empty()) << "3.06 Hotfix 2";
    EXPECT_TRUE(SprinterBios::KnownIssues(0x1729CB5Cu).empty()) << "3.04";
    EXPECT_TRUE(SprinterBios::KnownIssues(0x12345678u).empty()) << "not a shipped image";
}

// The fixture's tagged flash is no shipped image; a selection without an emulator changes the configuration
TEST_F(SprinterBiosReport_Test, ReportAndSelectionWithoutReset)
{
    const StateNode report = DeviceState::SprinterBios(_context);
    ASSERT_TRUE(Find(report, "available")->b);
    EXPECT_EQ(Find(report, "images")->items.size(), 3u);
    EXPECT_NE(LoadedFile(report).find("not a shipped image"), std::string::npos) << LoadedFile(report);
    EXPECT_TRUE(Find(*Find(report, "options"), "fast_start")->b);

    SprinterBios::Options options;
    std::string error;
    ASSERT_TRUE(SprinterBios::OptionsFromStrings("", "", "1", "0", options, error));
    const StateNode selected = DeviceState::SprinterBiosSelect(_context, options);
    ASSERT_TRUE(Find(selected, "available")->b) << DeviceState::ToText(selected);
    EXPECT_EQ(_context->config.sprinter.accel_int_suspend, 1) << "takes effect at once";

    ASSERT_TRUE(SprinterBios::OptionsFromStrings("nope", "", "", "0", options, error));
    const StateNode bad = DeviceState::SprinterBiosSelect(_context, options);
    EXPECT_FALSE(Find(bad, "available")->b);
}

// A running machine: the selection loads at the reset; the report names the image by the flash's CRC.
// Two machine builds and a 256 KB ROM read: ~100-300 ms
TEST(SprinterBiosReload_Test, ResetLoadsTheSelectedImage)
{
    if (!SprinterFixture::Rom304Available())
        GTEST_SKIP() << "data/rom/sprinter not found";
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("SPRINTER", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    // The shipped config's default (owner decision 2026-10-02, bios-versions.md §6)
    EXPECT_EQ(LoadedFile(DeviceState::SprinterBios(context)), "sp2k-3.07-beta1.rom");
    // 3.07 BETA 1 carries its floppy-driver warning (bios-versions.md §5.2) in the report and the brief
    const StateNode beta = DeviceState::SprinterBios(context);
    ASSERT_EQ(Find(beta, "known_issues")->items.size(), 1u) << DeviceState::ToText(beta);
    EXPECT_NE(Find(beta, "known_issues")->items[0].s.find("IY"), std::string::npos);
    EXPECT_EQ(DeviceState::SprinterBiosKnownIssues(context).size(), 1u);

    SprinterBios::Options options;
    std::string error;
    ASSERT_TRUE(SprinterBios::OptionsFromStrings("3.06", "1", "", "0", options, error));
    StateNode report = DeviceState::SprinterBiosSelect(context, options);
    ASSERT_TRUE(Find(report, "available")->b) << DeviceState::ToText(report);
    EXPECT_TRUE(Find(report, "reload_pending")->b) << "loads at the next reset";
    EXPECT_EQ(LoadedFile(report), "sp2k-3.07-beta1.rom");

    emulator->Reset();
    report = DeviceState::SprinterBios(context);
    EXPECT_EQ(LoadedFile(report), "sp2k-3.06-hf2.rom");
    EXPECT_FALSE(Find(report, "reload_pending")->b);
    EXPECT_TRUE(Find(report, "known_issues")->items.empty()) << "3.06 Hotfix 2 has none";
    EXPECT_TRUE(DeviceState::SprinterBiosKnownIssues(context).empty());

    EmulatorTestHelper::CleanupEmulator(emulator);
}

// The ISA slot population at create (Sprinter ISA tdd §5): isa_slot1 / isa_slot2 into [ISA] SlotN of the new config
TEST(SprinterBiosOptions_Test, IsaSlotsAtCreate)
{
    SprinterBios::Options options;
    std::string error;
    ASSERT_TRUE(SprinterBios::IsaSlotFromString("NONE", 1, options, error)) << error;
    ASSERT_TRUE(SprinterBios::IsaSlotFromString("", 0, options, error)) << "empty keeps the configured kind";
    EXPECT_EQ(options.isaSlot[0], -1);
    EXPECT_EQ(options.isaSlot[1], 0);
    EXPECT_FALSE(SprinterBios::IsaSlotFromString("ne3000", 0, options, error));
    EXPECT_NE(error.find("isa_slot1"), std::string::npos) << error;

    CONFIG config{};
    config.sprinter.isa = sprinterisa::DefaultConfig();
    ASSERT_TRUE(SprinterBios::ApplyToConfig(config, options, error)) << error;
    EXPECT_EQ(config.sprinter.isa.slot[1].kind, static_cast<uint8_t>(sprinterisa::CardKind::None));
    EXPECT_EQ(config.sprinter.isa.slot[0].kind, static_cast<uint8_t>(sprinterisa::CardKind::None));
}
