// NeoGS media requests from automation (neogsmedia.h): SD card insert /
// eject go to the media manager's slot `sd.ngs` (applied at a frame
// boundary), a flash save is carried out on the machine's thread; insert and
// eject are refused while a TTD recording runs, because the machine's
// configuration is fixed for the recording.
//
// Runtime justification: the hand-off tests run the emulator loop on its own
// thread for a few frames; the stress test for about a second.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/neogstestsdcard.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/neogs/neogsmedia.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/soundmanager.h"

namespace
{
/// ATM710 fits a General Sound card by default (data/configs/atm710/unreal.ini)
constexpr const char* kGsCapableModel = "ATM710";

class NeoGSMedia_Test : public ::testing::Test
{
protected:
    SoundCardScope _gs{TestSound::GeneralSound}; // first: the GS slot must be fitted when the machine is created
    std::unique_ptr<ScratchFatImage> _image; // outlives the emulator
    std::unique_ptr<ScratchFatImage> _other;
    Emulator* _emulator = nullptr;
    EmulatorContext* _ctx = nullptr;

    void SetUp() override
    {
        _image = MakeNeoGSTestSd(NeoGSTestSd::Fat16Mbr);
        _other = MakeNeoGSTestSd(NeoGSTestSd::Fat32Mbr);
        ASSERT_TRUE(_image->ok()) << _image->error();
        ASSERT_TRUE(_other->ok()) << _other->error();
        _emulator = EmulatorTestHelper::CreateStandardEmulator(kGsCapableModel, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _ctx = _emulator->GetContext();
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _ctx->pMemory->UpdateFeatureCache();
        // The tests start from the classic card and fit NeoGS themselves
        ASSERT_TRUE(FitGeneralSoundCard(_ctx->pSoundManager, GSTypeKind::Z80));
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void fitNeoGS()
    {
        ASSERT_TRUE(FitNeoGSWithSd(_ctx, _image->path()));
    }

    SoundChip_NeoGS* card() const { return dynamic_cast<SoundChip_NeoGS*>(_ctx->pSoundManager->getGeneralSound()); }
};
} // namespace

TEST_F(NeoGSMedia_Test, RequestsNeedTheNeoGSCard)
{
    // The configured classic card
    EXPECT_EQ(NeoGSRequestSdInsert(_ctx, _image->path()), NeoGSMediaResult::NoNeoGS);
    EXPECT_EQ(NeoGSRequestSdEject(_ctx), NeoGSMediaResult::NoNeoGS);
    EXPECT_EQ(NeoGSRequestFlashSave(_ctx), NeoGSMediaResult::NoNeoGS);
}

TEST_F(NeoGSMedia_Test, CarriedOutAtOnceWhileTheLoopIsNotRunning)
{
    fitNeoGS();
    EXPECT_EQ(NeoGSRequestSdEject(_ctx), NeoGSMediaResult::Done);
    EXPECT_FALSE(card()->sdCardPresent());
    EXPECT_EQ(NeoGSRequestSdInsert(_ctx, _other->path()), NeoGSMediaResult::Done);
    EXPECT_TRUE(card()->sdCardPresent());
    EXPECT_EQ(card()->sdCardImage(), _other->path());

    EXPECT_EQ(NeoGSRequestSdInsert(_ctx, ""), NeoGSMediaResult::NoPath);
    EXPECT_EQ(NeoGSRequestSdInsert(_ctx, _other->path() + ".missing"), NeoGSMediaResult::NoFile);
    EXPECT_EQ(card()->sdCardImage(), _other->path()) << "a refused insert leaves the card alone";
}

/// The machine's configuration is fixed while a TTD recording runs: the SD
/// card's contents are fixed data for the recording's whole length
TEST_F(NeoGSMedia_Test, InsertAndEjectRefusedWhileTtdRecords)
{
    fitNeoGS();
    ASSERT_TRUE(_ctx->pTimeTravelManager->StartRecording());
    const std::string path = card()->sdCardImage();

    EXPECT_EQ(NeoGSRequestSdEject(_ctx), NeoGSMediaResult::TtdRecording);
    EXPECT_EQ(NeoGSRequestSdInsert(_ctx, _other->path()), NeoGSMediaResult::TtdRecording);
    // The card enforces it too, whoever calls it
    EXPECT_FALSE(card()->ejectSdCard());
    EXPECT_FALSE(card()->insertSdCard(_other->path()));
    EXPECT_TRUE(card()->sdCardPresent());
    EXPECT_EQ(card()->sdCardImage(), path);

    // A flash save changes nothing in the machine: it reaches the card (which
    // saves only in persist mode - off here - so it reports Failed)
    const NeoGSMediaResult save = NeoGSRequestFlashSave(_ctx);
    EXPECT_TRUE(save == NeoGSMediaResult::Done || save == NeoGSMediaResult::Failed) << NeoGSMediaResultText(save);

    _ctx->pTimeTravelManager->StopRecording();
    EXPECT_EQ(NeoGSRequestSdEject(_ctx), NeoGSMediaResult::Done);
    EXPECT_FALSE(card()->sdCardPresent());
}

/// The SD slot is the media manager's `sd.ngs`, there only while NeoGS is
/// fitted. Switching the card away parks the medium with its session writes;
/// a NeoGS fitted again finds it
TEST_F(NeoGSMedia_Test, SdSlotListedOnlyWhileNeoGSIsFitted)
{
    MediaManager* media = _ctx->pMediaManager;
    ASSERT_NE(media, nullptr);
    auto listed = [media]
    {
        for (const SlotInfo& slot : media->List())
        {
            if (slot.descriptor.id == SoundChip_NeoGS::SD_SLOT_ID)
                return true;
        }
        return false;
    };
    EXPECT_FALSE(listed()) << "the classic card has no SD slot";

    fitNeoGS();
    ASSERT_TRUE(listed());
    const std::optional<SlotInfo> info = media->Info(SoundChip_NeoGS::SD_SLOT_ID);
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->present);
    EXPECT_EQ(info->source, _image->path());
    EXPECT_TRUE(info->descriptor.acceptsFolder);
    EXPECT_NE(std::find(info->tags.begin(), info->tags.end(), "neogs"), info->tags.end());
    EXPECT_EQ(_ctx->pSoundManager->generalSoundSlot().sdCardImage, _image->path());

    // A guest-side write (the card's own SD protocol is not needed for this)
    uint8_t block[SdCardSpi::BLOCK];
    memset(block, 0xA5, sizeof block);
    ASSERT_TRUE(card()->sdCard()->writeBlock(100, block));

    ASSERT_TRUE(FitGeneralSoundCard(_ctx->pSoundManager, GSTypeKind::Z80));
    EXPECT_FALSE(listed()) << "the card went, its slot with it";
    bool parked = false;
    for (const SlotInfo& slot : media->Detached())
        parked |= slot.descriptor.id == SoundChip_NeoGS::SD_SLOT_ID && slot.source == _image->path();
    EXPECT_TRUE(parked) << "the medium waits for the card";

    ASSERT_TRUE(FitGeneralSoundCard(_ctx->pSoundManager, GSTypeKind::NGS));
    ASSERT_TRUE(card()->sdCardPresent()) << "a NeoGS fitted again finds its card";
    EXPECT_EQ(card()->sdCardImage(), _image->path());
    uint8_t back[SdCardSpi::BLOCK] = {};
    ASSERT_TRUE(card()->sdCard()->readBlock(100, back));
    EXPECT_EQ(back[0], 0xA5) << "session writes survive the parking";
}

TEST_F(NeoGSMedia_Test, RefusedWhileTtdReplayOwnsTheMachine)
{
    fitNeoGS();
    _ctx->ttdReplayActive = true;
    EXPECT_EQ(NeoGSRequestSdEject(_ctx), NeoGSMediaResult::ReplayOwnsInput);
    EXPECT_EQ(NeoGSRequestFlashSave(_ctx), NeoGSMediaResult::ReplayOwnsInput);
    _ctx->ttdReplayActive = false;
    EXPECT_TRUE(card()->sdCardPresent());
}

/// The SD slot belongs to the media manager: while the loop runs a request
/// is queued and applied at the next frame boundary; paused, at once
TEST_F(NeoGSMedia_Test, FromAnotherThreadCarriedOutByTheLoop)
{
    fitNeoGS();
    _emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([this] { return _emulator->GetState() == StateRun; }));

    EXPECT_EQ(NeoGSRequestSdEject(_ctx), NeoGSMediaResult::Queued);
    ASSERT_TRUE(_ctx->pMediaManager->WaitApplied(SoundChip_NeoGS::SD_SLOT_ID, 2000));
    _emulator->Pause();
    ASSERT_TRUE(_emulator->WaitForPauseConfirmation(2000));
    EXPECT_FALSE(card()->sdCardPresent());

    EXPECT_EQ(NeoGSRequestSdInsert(_ctx, _image->path()), NeoGSMediaResult::Done) << "paused: applied at once";
    EXPECT_TRUE(card()->sdCardPresent());
    _emulator->Stop();
}

/// The race this path exists for: the card boots from its SD card (the
/// loader reads the FAT and 32 KB of NEOGS.ROM) while another thread ejects
/// and inserts the card as fast as it can. Every change lands at a frame
/// boundary (the media manager): no crash, no torn state (run under ASan in CI)
TEST_F(NeoGSMedia_Test, EjectAndInsertStormWhileTheCardReadsTheSdCard)
{
    fitNeoGS();
    _emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([this] { return _emulator->GetState() == StateRun; }));
    int accepted = 0;
    int queued = 0;
    for (int i = 0; i < 200; i++)
    {
        const NeoGSMediaResult r = (i & 1) ? NeoGSRequestSdInsert(_ctx, (i & 2) ? _image->path() : _other->path())
                                           : NeoGSRequestSdEject(_ctx);
        accepted += NeoGSMediaAccepted(r) ? 1 : 0;
        queued += r == NeoGSMediaResult::Queued ? 1 : 0;
        // Restart the loader so it reads the card again (also on the machine thread)
        _ctx->pTimeTravelManager->SubmitMachineTask([this] { card()->resetCard(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    ASSERT_TRUE(TestWait::For([this] { return !_ctx->HasStepWork(EmulatorContext::kStepWorkTtdInput); }));
    ASSERT_TRUE(_ctx->pMediaManager->WaitApplied(SoundChip_NeoGS::SD_SLOT_ID, 2000));
    _emulator->Pause();
    ASSERT_TRUE(_emulator->WaitForPauseConfirmation(2000));
    EXPECT_EQ(accepted, 200);
    EXPECT_EQ(queued, 200) << "the loop was running: every request goes through the queue";
    EXPECT_TRUE(card()->sdCardPresent()) << "the last request was an insert";
    _emulator->Stop();
}

/// Other threads (the GUI's audio settings, automation) read the GS slot from
/// SoundManager's copy, refreshed on the machine thread when the card or its
/// SD card changes
TEST_F(NeoGSMedia_Test, SlotCopyFollowsTheCardAndItsSdCard)
{
    EXPECT_EQ(_ctx->pSoundManager->generalSoundSlot().kind, GSTypeKind::Z80);
    EXPECT_TRUE(_ctx->pSoundManager->generalSoundSlot().sdCardImage.empty());

    fitNeoGS();
    GeneralSoundSlot slot = _ctx->pSoundManager->generalSoundSlot();
    EXPECT_EQ(slot.kind, GSTypeKind::NGS);
    EXPECT_EQ(slot.sdCardImage, card()->sdCardImage());

    ASSERT_EQ(NeoGSRequestSdEject(_ctx), NeoGSMediaResult::Done);
    EXPECT_TRUE(_ctx->pSoundManager->generalSoundSlot().sdCardImage.empty());
    ASSERT_EQ(NeoGSRequestSdInsert(_ctx, _other->path()), NeoGSMediaResult::Done);
    EXPECT_EQ(_ctx->pSoundManager->generalSoundSlot().sdCardImage, card()->sdCardImage());

    ASSERT_TRUE(_ctx->pSoundManager->switchGeneralSoundCard(GSTypeKind::LW));
    slot = _ctx->pSoundManager->generalSoundSlot();
    EXPECT_EQ(slot.kind, GSTypeKind::LW);
    EXPECT_TRUE(slot.sdCardImage.empty());
}

/// The GUI's path: a switch requested from another thread while the loop
/// runs. The mixer source is renamed "NeoGS", "NeoGS MP3" appears, and the
/// slot copy follows
TEST_F(NeoGSMedia_Test, SwitchRequestedWhileRunningRenamesTheMixerSource)
{
    SoundManager* sm = _ctx->pSoundManager;
    auto gsName = [sm]
    {
        for (const AudioDeviceInfo& d : sm->devices())
        {
            if (d.type == AudioSourceType::GeneralSound)
                return d.name;
        }
        return std::string();
    };
    EXPECT_EQ(gsName(), "GS");

    _emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([this] { return _emulator->GetState() == StateRun; }));
    ASSERT_TRUE(sm->requestGeneralSoundCardSwitch(GSTypeKind::NGS));
    ASSERT_TRUE(TestWait::For([sm] { return sm->generalSoundSlot().kind == GSTypeKind::NGS; }));
    _emulator->Pause();
    ASSERT_TRUE(_emulator->WaitForPauseConfirmation(2000));

    EXPECT_EQ(gsName(), "NeoGS");
    bool mp3 = false;
    for (const AudioDeviceInfo& d : sm->devices())
        mp3 |= d.type == AudioSourceType::GeneralSoundMp3 && d.name == "NeoGS MP3";
    EXPECT_TRUE(mp3);
    _emulator->Stop();
}

/// The NeoGS stereo mode is kept by the SoundManager (set from any thread):
/// the fitted card takes it at the next frame, and a card fitted later starts
/// with it
TEST_F(NeoGSMedia_Test, StereoModeReachesTheCardAndSurvivesASwitch)
{
    fitNeoGS();
    EXPECT_EQ(card()->stereoMode(), NeoGSConfig::StereoMode::Separated);
    _ctx->pSoundManager->setNeoGSStereoMode(NeoGSConfig::StereoMode::GS);
    _emulator->RunNFrames(1);
    EXPECT_EQ(card()->stereoMode(), NeoGSConfig::StereoMode::GS);

    ASSERT_TRUE(_ctx->pSoundManager->switchGeneralSoundCard(GSTypeKind::LW));
    ASSERT_TRUE(_ctx->pSoundManager->switchGeneralSoundCard(GSTypeKind::NGS));
    _emulator->RunNFrames(1);
    EXPECT_EQ(card()->stereoMode(), NeoGSConfig::StereoMode::GS);
}
