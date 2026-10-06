// What the File > Save Snapshot menu and the save dialog offer (snapshot pipeline P6): the formats the running machine can
// be saved in right now, the reason for each it cannot, the preferred one first. Real emulators, no display needed.

#include <gtest/gtest.h>

#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/savesnapshotchoices.h"

class SaveSnapshotChoices_Test : public ::testing::Test
{
protected:
    void TearDown() override
    {
        for (const auto& id : EmulatorManager::GetInstance()->GetEmulatorIds())
            EmulatorManager::GetInstance()->RemoveEmulator(id);
    }

    std::vector<SaveSnapshotChoices::Choice> ChoicesOf(const char* model, uint32_t ramKb)
    {
        std::shared_ptr<Emulator> emulator =
            EmulatorManager::GetInstance()->CreateEmulatorWithModelAndRAM("save-choices", model, ramKb, LoggerLevel::LogError);
        EXPECT_NE(emulator, nullptr) << model;
        return SaveSnapshotChoices::Build(emulator->SnapshotSaveFormats());
    }
};

TEST_F(SaveSnapshotChoices_Test, APlain128kOffersEveryFormatAndTheChosenOneFirst)
{
    const auto all = ChoicesOf("128k", 128);
    ASSERT_EQ(all.size(), 3u);
    for (const auto& choice : all)
        EXPECT_TRUE(choice.enabled) << choice.extension.toStdString();
    EXPECT_EQ(all[0].extension, "szx") << "SZX first: it keeps the most state";

    const auto offered = SaveSnapshotChoices::Offered(all, "z80");
    ASSERT_EQ(offered.size(), 3u);
    EXPECT_EQ(offered[0].extension, "z80");
    EXPECT_EQ(SaveSnapshotChoices::Offered(all, "").front().extension, "szx");
}

TEST_F(SaveSnapshotChoices_Test, ABigPentagonOffersSzxOnlyAndSaysWhyNotTheRest)
{
    const auto all = ChoicesOf("PENTAGON", 512);
    const auto offered = SaveSnapshotChoices::Offered(all, "sna");   // the SNA item asked for, but it cannot be had
    ASSERT_EQ(offered.size(), 1u);
    EXPECT_EQ(offered[0].extension, "szx");
    EXPECT_TRUE(offered[0].filter.contains("*.szx"));
    EXPECT_FALSE(all[2].enabled) << "the .sna item is disabled";
    EXPECT_TRUE(all[2].tip.contains("szx")) << "its tip says what would work: " << all[2].tip.toStdString();
    const QString refusals = SaveSnapshotChoices::Refusals(all);
    EXPECT_TRUE(refusals.contains(".sna:"));
    EXPECT_TRUE(refusals.contains(".z80:"));
    EXPECT_FALSE(refusals.contains(".szx:"));
}

TEST_F(SaveSnapshotChoices_Test, AMachineWithNoViewOffersNothing)
{
    const auto all = ChoicesOf("SPRINTER", 4096);   // at the BIOS, in no Spectrum mode
    EXPECT_TRUE(SaveSnapshotChoices::Offered(all, "szx").empty());
    for (const auto& choice : all)
    {
        EXPECT_FALSE(choice.enabled);
        EXPECT_TRUE(choice.tip.contains("ESC")) << "the tip says how to get a view: " << choice.tip.toStdString();
    }
    EXPECT_TRUE(SaveSnapshotChoices::Refusals(all).contains(".szx:"));
}
