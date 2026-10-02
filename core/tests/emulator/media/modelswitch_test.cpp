// A model switch keeps the media (FR-21, ACC-5): live media go into the slot
// with the same id on the new machine, unsaved writes included; dirty media
// the new model has no slot for need save, discard or keep
//
// Every switch builds a second machine (config, ROMs): 50-100 ms per test by nature

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/media/mediaformatregistry.h"
#include "emulator/media/mediamanager.h"
#include "emulator/media/modelswitch.h"

namespace
{
    std::unique_ptr<Medium> MemoryCard(uint64_t sectors)
    {
        MediaSource source;
        source.type = MediaSourceType::Blank;
        return MediaFormatRegistry::WrapBlock(source, AccessMode::Session, "memory", std::make_unique<MemoryDisk>(sectors));
    }

    void WriteSectors(Medium& medium, uint64_t count)
    {
        std::vector<uint8_t> data(512, 0x5A);
        for (uint64_t lba = 0; lba < count; lba++)
            ASSERT_TRUE(medium.Block()->WriteSector(lba, data.data()));
    }

    /// Floppy A: an image with one sector rewritten in memory
    void DirtyFloppyA(Emulator& emulator)
    {
        ASSERT_TRUE(emulator.LoadDisk(TestPathHelper::GetTestDataPath("loaders/trd/EyeAche.trd"), 0));
        DiskImage* disk = emulator.GetContext()->pMediaManager->GetMedium("fdd.a")->Floppy();
        ASSERT_NE(disk, nullptr);
        std::vector<uint8_t> data(256, 0xE5);
        disk->getTrackForCylinderAndSide(1, 0)->writeSectorData(1, data.data(), data.size());
        ASSERT_TRUE(disk->isDirty());
    }

    class ModelSwitch_Test : public ::testing::Test
    {
    protected:
        std::string _newId;

        void TearDown() override
        {
            if (!_newId.empty())
                EmulatorManager::GetInstance()->RemoveEmulator(_newId);
            MessageCenter::DisposeDefaultMessageCenter();
        }

        ModelSwitchResult Switch(Emulator* old, const char* model, StrandedMedia stranded = StrandedMedia::Refuse)
        {
            ModelSwitchRequest request;
            request.emulatorId = old->GetId();
            request.model = model;
            request.stranded = stranded;
            ModelSwitchResult result = ModelSwitch::Run(request);
            if (result.emulator)
                _newId = result.emulator->GetId();
            return result;
        }
    };
}  // namespace

/// ACC-5: Pentagon -> ZX-Evo keeps floppy A (with its unsaved writes, the same
/// live medium) and the tape
TEST_F(ModelSwitch_Test, FloppyAndTapeFollowWithTheirWrites)
{
    Emulator* old = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(old, nullptr);
    DirtyFloppyA(*old);
    ASSERT_TRUE(old->LoadTape(TestPathHelper::GetTestDataPath("loaders/tap/AYtest_v0.2.tap")));
    const Medium* floppy = old->GetContext()->pMediaManager->GetMedium("fdd.a");
    const std::string oldId = old->GetId();

    const ModelSwitchResult switched = Switch(old, "ATM3");
    ASSERT_TRUE(switched.result.Ok()) << switched.result.message;
    ASSERT_NE(switched.emulator, nullptr);
    EXPECT_FALSE(EmulatorManager::GetInstance()->HasEmulator(oldId)) << "the old machine is gone";

    MediaManager& media = *switched.emulator->GetContext()->pMediaManager;
    EXPECT_EQ(media.GetMedium("fdd.a"), floppy) << "the live medium moved, nothing re-read";
    const auto a = media.Info("fdd.a");
    ASSERT_TRUE(a.has_value());
    EXPECT_TRUE(a->dirty) << "the unsaved writes came along";
    EXPECT_EQ(a->changes, "1 track: 1 sector");
    ASSERT_NE(media.GetMedium("tape"), nullptr);
    EXPECT_EQ(switched.emulator->GetContext()->coreState.tapeFilePath,
              TestPathHelper::GetTestDataPath("loaders/tap/AYtest_v0.2.tap"));
    EXPECT_NE(std::find(switched.media.attached.begin(), switched.media.attached.end(), "fdd.a"),
              switched.media.attached.end());
    EXPECT_NE(std::find(switched.media.attached.begin(), switched.media.attached.end(), "tape"),
              switched.media.attached.end());
}

/// ATM Turbo 2 v4.50 (T4.4 of docs/inprogress/2026-10-01-atm450/tdd-plan.md):
/// a switch to it and back keeps floppy A with its unsaved writes - the same
/// live medium both ways
TEST_F(ModelSwitch_Test, Atm450AwayAndBackKeepsFloppy)
{
    Emulator* old = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(old, nullptr);
    DirtyFloppyA(*old);
    const Medium* floppy = old->GetContext()->pMediaManager->GetMedium("fdd.a");

    const ModelSwitchResult toAtm = Switch(old, "ATM450");
    ASSERT_TRUE(toAtm.result.Ok()) << toAtm.result.message;
    ASSERT_NE(toAtm.emulator, nullptr);
    EXPECT_EQ(toAtm.emulator->GetContext()->config.mem_model, MM_ATM450);
    EXPECT_EQ(toAtm.emulator->GetContext()->pMediaManager->GetMedium("fdd.a"), floppy);

    const ModelSwitchResult back = Switch(toAtm.emulator.get(), "PENTAGON");
    ASSERT_TRUE(back.result.Ok()) << back.result.message;
    ASSERT_NE(back.emulator, nullptr);
    MediaManager& media = *back.emulator->GetContext()->pMediaManager;
    EXPECT_EQ(media.GetMedium("fdd.a"), floppy) << "the live medium came back";
    const auto a = media.Info("fdd.a");
    ASSERT_TRUE(a.has_value());
    EXPECT_TRUE(a->dirty);
}

/// ZX-Evo -> Pentagon strands the SD card: with unsaved writes the switch is
/// refused and nothing changes; keep puts it among the detached media of the
/// new machine, discard closes it
TEST_F(ModelSwitch_Test, StrandedDirtyCardNeedsADecision)
{
    for (StrandedMedia decision : {StrandedMedia::Keep, StrandedMedia::Discard})
    {
        Emulator* old = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
        ASSERT_NE(old, nullptr);
        MediaManager& before = *old->GetContext()->pMediaManager;
        ASSERT_TRUE(before.HasSlot("sd.zc"));
        ASSERT_TRUE(before.Insert("sd.zc", MemoryCard(64)).Ok());
        WriteSectors(*before.GetMedium("sd.zc"), 3);
        const std::string oldId = old->GetId();
        const size_t machines = EmulatorManager::GetInstance()->GetEmulatorIds().size();

        const ModelSwitchResult refused = Switch(old, "PENTAGON");
        EXPECT_EQ(refused.result.error, MediaError::Dirty) << refused.result.message;
        ASSERT_EQ(refused.stranded.size(), 1u);
        EXPECT_EQ(refused.stranded[0].descriptor.id, "sd.zc");
        EXPECT_EQ(refused.stranded[0].changes, "3 sectors");
        EXPECT_NE(refused.result.message.find("sd.zc"), std::string::npos);
        EXPECT_TRUE(EmulatorManager::GetInstance()->HasEmulator(oldId)) << "refused: the old machine stays";
        EXPECT_EQ(EmulatorManager::GetInstance()->GetEmulatorIds().size(), machines) << "and the new one is gone";
        EXPECT_TRUE(before.Info("sd.zc")->dirty);
        _newId.clear();

        const ModelSwitchResult switched = Switch(old, "PENTAGON", decision);
        ASSERT_TRUE(switched.result.Ok()) << ModelSwitch::StrandedName(decision) << ": " << switched.result.message;
        MediaManager& after = *switched.emulator->GetContext()->pMediaManager;
        EXPECT_FALSE(after.HasSlot("sd.zc"));
        const std::vector<SlotInfo> detached = after.Detached();
        if (decision == StrandedMedia::Keep)
        {
            ASSERT_EQ(detached.size(), 1u);
            EXPECT_EQ(detached[0].descriptor.id, "sd.zc");
            EXPECT_EQ(detached[0].changes, "3 sectors");
            EXPECT_EQ(switched.media.detached, std::vector<std::string>{"sd.zc"});
        }
        else
        {
            EXPECT_TRUE(detached.empty());
            EXPECT_EQ(switched.media.closed, std::vector<std::string>{"sd.zc"});
        }
        EmulatorManager::GetInstance()->RemoveEmulator(_newId);
        _newId.clear();
    }
}

/// A clean medium without a slot on the new model is closed and reported; a
/// save that cannot be done (a card has no image file) leaves the old machine
TEST_F(ModelSwitch_Test, CleanStrandedMediaCloseAndFailedSavesChangeNothing)
{
    Emulator* old = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
    ASSERT_NE(old, nullptr);
    MediaManager& before = *old->GetContext()->pMediaManager;
    ASSERT_TRUE(before.Insert("sd.zc", MemoryCard(64)).Ok());
    WriteSectors(*before.GetMedium("sd.zc"), 1);
    const std::string oldId = old->GetId();

    const ModelSwitchResult failed = Switch(old, "PENTAGON", StrandedMedia::Save);
    EXPECT_EQ(failed.result.error, MediaError::NotSupported) << failed.result.message;
    EXPECT_TRUE(EmulatorManager::GetInstance()->HasEmulator(oldId));
    EXPECT_TRUE(before.Info("sd.zc")->dirty);
    _newId.clear();

    ASSERT_TRUE(before.Discard("sd.zc").Ok());
    ASSERT_FALSE(before.Info("sd.zc")->dirty);
    const ModelSwitchResult switched = Switch(old, "PENTAGON");
    ASSERT_TRUE(switched.result.Ok()) << switched.result.message;
    EXPECT_EQ(switched.media.closed, std::vector<std::string>{"sd.zc"});
    ASSERT_FALSE(switched.media.lines.empty());
    EXPECT_NE(switched.result.report.back().find("not on this model, closed"), std::string::npos);
}

/// The new machine keeps the old one's power-on RAM mode unless the request
/// names one: a machine created with zeroed RAM stays reproducible
TEST_F(ModelSwitch_Test, NewMachineKeepsThePowerOnRamModeUnlessTold)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto old = manager->CreateEmulatorWithModel("", "PENTAGON", LoggerLevel::LogError, nullptr,
                                                Config::RamPowerOnOverride(RamPowerOn::Zero));
    ASSERT_NE(old, nullptr);

    ModelSwitchResult kept = Switch(old.get(), "48K");
    ASSERT_TRUE(kept.result.Ok()) << kept.result.message;
    ASSERT_NE(kept.emulator, nullptr);
    EXPECT_EQ(kept.emulator->GetContext()->config.ramPowerOn, RamPowerOn::Zero);

    ModelSwitchRequest request;
    request.emulatorId = kept.emulator->GetId();
    request.model = "PENTAGON";
    request.ramPowerOn = RamPowerOn::Random;
    kept.emulator.reset();
    ModelSwitchResult told = ModelSwitch::Run(request);
    ASSERT_TRUE(told.result.Ok()) << told.result.message;
    ASSERT_NE(told.emulator, nullptr);
    _newId = told.emulator->GetId();
    EXPECT_EQ(told.emulator->GetContext()->config.ramPowerOn, RamPowerOn::Random);
}

TEST(ModelSwitch_Names_Test, StrandedPolicyNames)
{
    StrandedMedia value = StrandedMedia::Save;
    for (const char* name : {"refuse", "save", "discard", "keep"})
    {
        ASSERT_TRUE(ModelSwitch::ParseStranded(name, value)) << name;
        EXPECT_STREQ(ModelSwitch::StrandedName(value), name);
    }
    EXPECT_TRUE(ModelSwitch::ParseStranded("", value));
    EXPECT_EQ(value, StrandedMedia::Refuse);
    EXPECT_FALSE(ModelSwitch::ParseStranded("maybe", value));
}
