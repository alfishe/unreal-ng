// The CD drives under TTD (PLAN #83): the CdDrive blob (id 41) exists only on a
// board with a CD unit, so every other machine's checkpoints are unchanged; it
// carries the audio play state, the head and page 0Eh; a recording replays a
// play mid-track exactly from a frame checkpoint and from a mid-frame seek
// (sealed replay: the disc is media, read again at the same head position).

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/cdtestdisc.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/scratchfolder.h"
#include "base/featuremanager.h"
#include "debugger/ttd/ide/ttdcddrive.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/ide/ata/atapicdrom.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/io/storage/cd/cdimage.h"
#include "emulator/media/mediamanager.h"

namespace
{
    /// A ZX-Evo (ATM3) with the music disc in its CD drive (the IDE slave)
    class TTDCdDrive_Test : public ::testing::Test
    {
    protected:
        Emulator* _emulator = nullptr;
        EmulatorContext* _context = nullptr;
        ttd::TimeTravelManager* _ttd = nullptr;
        std::unique_ptr<ScratchFolder> _folder;

        void SetUp() override
        {
            _emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
            ASSERT_NE(_emulator, nullptr);
            _context = _emulator->GetContext();
            _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
            _ttd = _context->pTimeTravelManager;
            _folder = std::make_unique<ScratchFolder>("ttd-cd");
            MediaSource source;
            source.path = cdtest::WriteMusicDisc(_folder->Path(), 1, 3, 16, cdtest::MusicLayout::Mixed);
            InsertOptions options;
            options.immediate = true;
            ASSERT_TRUE(_context->pMediaManager->Insert("ide0.slave", source, options).Ok());
        }
        void TearDown() override
        {
            _folder.reset();
            if (_emulator)
                EmulatorTestHelper::CleanupEmulator(_emulator);
            MessageCenter::DisposeDefaultMessageCenter();
        }

        AtapiCdrom& Cd() { return *static_cast<AtapiCdrom*>(_context->pIdeController->Channel().Unit(1)); }

        std::vector<uint8_t> Blob()
        {
            ttd::TTDCdDrive blob(_context);
            std::vector<uint8_t> bytes(blob.TTDStateSize());
            blob.TTDSaveState(bytes.data());
            return bytes;
        }

        void RunTo(uint64_t frame, uint32_t t)
        {
            const EmulatorState* state = &_context->emulatorState;
            _emulator->RunUntilCondition([state, frame, t](const Z80State& z80) {
                return state->frame_counter > frame || (state->frame_counter == frame && z80.t >= t);
            });
        }
    };
}  // namespace

TEST_F(TTDCdDrive_Test, RegisteredOnlyWithACdDrive)
{
    ASSERT_TRUE(_ttd->StartRecording());
    EXPECT_TRUE(_ttd->GetPeripheralRegistry().IsRegistered(ttd::PeripheralId::CdDrive));
    EXPECT_TRUE(_ttd->GetPeripheralRegistry().IsRegistered(ttd::PeripheralId::AtaChannel));
    _ttd->StopRecording();
    EXPECT_EQ(ttd::TTDCdDrive(_context).TTDStateSize(), 8u + 2 * (sizeof(CdAudioState) + sizeof(AtapiStage) + 8));

    // A Pentagon's Nemo board has two hard-disk units: no CdDrive blob, its checkpoints as before
    Emulator* pentagon = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(pentagon, nullptr);
    pentagon->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    ttd::TimeTravelManager* other = pentagon->GetContext()->pTimeTravelManager;
    ASSERT_TRUE(other->StartRecording());
    EXPECT_TRUE(other->GetPeripheralRegistry().IsRegistered(ttd::PeripheralId::AtaChannel));
    EXPECT_FALSE(other->GetPeripheralRegistry().IsRegistered(ttd::PeripheralId::CdDrive));
    other->StopRecording();
    EmulatorTestHelper::CleanupEmulator(pentagon);
}

TEST_F(TTDCdDrive_Test, BlobCarriesPlayStateHeadAndVolume)
{
    CdAudioState& page = Cd().Audio().MutableState();
    page.portVolume[0] = 0x40;
    page.portSelect[1] = 3;
    Cd().Audio().Play(166, 166 + 225);
    _emulator->RunNFrames(10);
    const std::vector<uint8_t> saved = Blob();
    const int64_t head = Cd().Audio().State().head;  // frame-relative while playing: compare the stored value
    EXPECT_EQ(saved[1], 0x02) << "unit 1 is the CD drive";

    _emulator->RunNFrames(10);
    Cd().Audio().Pause();
    page.portVolume[0] = 0xFF;
    ttd::TTDCdDrive(_context).TTDLoadState(saved.data());
    EXPECT_EQ(Cd().Audio().Status(), CdAudioStatus::Playing);
    EXPECT_EQ(Cd().Audio().State().head, head);
    EXPECT_EQ(Cd().Audio().State().portVolume[0], 0x40);
    EXPECT_EQ(Cd().Audio().State().portSelect[1], 3);
    EXPECT_EQ(Blob(), saved);

    // Another version is not loaded
    std::vector<uint8_t> foreign = saved;
    foreign[0] = 99;
    _emulator->RunNFrames(2);
    const std::vector<uint8_t> now = Blob();
    ttd::TTDCdDrive(_context).TTDLoadState(foreign.data());
    EXPECT_EQ(Blob(), now);
}

TEST_F(TTDCdDrive_Test, PlayReplaysExactlyMidTrack)
{
    _emulator->RunNFrames(1);
    _emulator->RunNCPUCycles(5000);
    // A play in progress when the recording starts (as a guest's PLAY would leave it), its
    // end inside the recording: 20 CD frames (0.27 s) end the play after ~13 machine frames
    Cd().Audio().Play(166, 166 + 20);
    _emulator->RunNFrames(3);
    ASSERT_TRUE(_ttd->StartRecording());
    const uint64_t startFrame = _ttd->GetCheckpoint(0)->time.frame;
    _emulator->RunNFrames(6);
    const std::vector<uint8_t> midway = Blob();
    _emulator->RunNFrames(14);
    _emulator->RunNCPUCycles(1234);
    const uint64_t endFrame = _context->emulatorState.frame_counter;
    const uint32_t endT = _context->pCore->GetZ80()->t;
    const std::vector<uint8_t> recorded = Blob();
    _ttd->StopRecording();
    ASSERT_EQ(Cd().Audio().Status(), CdAudioStatus::Completed) << "the play ended inside the recording";
    ASSERT_NE(midway, recorded);

    struct Start
    {
        const char* name;
        ttd::TTDTimePoint at;
    };
    const Start starts[] = {
        {"session start", {startFrame, 0}},
        {"a frame checkpoint mid-track", {startFrame + 5, 0}},
        {"mid-frame, mid-track", {startFrame + 9, 30000}},
    };
    for (const Start& start : starts)
    {
        SCOPED_TRACE(start.name);
        ASSERT_TRUE(_ttd->SeekTo(start.at));
        EXPECT_EQ(Cd().Audio().Status(), CdAudioStatus::Playing) << "restored: playing again";
        RunTo(endFrame, endT);
        EXPECT_EQ(Blob(), recorded) << "head, status and volume as recorded";
    }
}

TEST_F(TTDCdDrive_Test, FolderDiscReplaysIdenticallyAndAnotherDiscIsReported)
{
    // An audio CD built from a folder (AudioFolderDisc): the disc is decoded at mount, so a replay
    // reads the very samples the recording played. The blob carries the disc's identity: restoring
    // onto the same content (even from another folder) is quiet, onto other content it is reported
    ScratchFolder music("ttd-cd-folder");
    cdtest::WriteFile(music.Path() / "1 a.wav", cdtest::Wave(cdtest::TonePcm(300, 440.0)));
    cdtest::WriteFile(music.Path() / "2 b.wav", cdtest::Wave(cdtest::TonePcm(300, 660.0)));
    MediaSource source;
    source.path = cdtest::Utf8(music.Path());
    InsertOptions options;
    options.immediate = true;
    ASSERT_TRUE(_context->pMediaManager->Insert("ide0.slave", source, options).Ok());
    _emulator->RunNFrames(1);
    ASSERT_EQ(Cd().Disc()->Format(), "audio-cd");
    const uint32_t track2 = Cd().Disc()->TrackAt(1).startLba;
    EXPECT_EQ(track2, 450u);

    _emulator->RunNCPUCycles(3000);
    Cd().Audio().Play(track2, track2 + 20);
    _emulator->RunNFrames(2);
    ASSERT_TRUE(_ttd->StartRecording());
    const uint64_t startFrame = _ttd->GetCheckpoint(0)->time.frame;
    _emulator->RunNFrames(20);
    _emulator->RunNCPUCycles(777);
    const uint64_t endFrame = _context->emulatorState.frame_counter;
    const uint32_t endT = _context->pCore->GetZ80()->t;
    const std::vector<uint8_t> recorded = Blob();
    _ttd->StopRecording();
    ASSERT_EQ(Cd().Audio().Status(), CdAudioStatus::Completed);

    ASSERT_TRUE(_ttd->SeekTo({startFrame + 3, 20000}));
    EXPECT_EQ(Cd().Audio().Status(), CdAudioStatus::Playing);
    int16_t first[588 * 2];
    int16_t again[588 * 2];
    const uint32_t lba = Cd().Audio().HeadLba();
    ASSERT_EQ(Cd().Disc()->ReadAudio(lba, first), CdImage::ReadResult::Ok);
    RunTo(endFrame, endT);
    EXPECT_EQ(Blob(), recorded) << "head, status and the disc identity as recorded";

    // The same files in another folder: the same disc
    ScratchFolder copy("ttd-cd-folder-copy");
    for (const char* name : {"1 a.wav", "2 b.wav"})
        std::filesystem::copy_file(music.Path() / name, copy.Path() / name);
    source.path = cdtest::Utf8(copy.Path());
    ASSERT_TRUE(_context->pMediaManager->Insert("ide0.slave", source, options).Ok());
    _emulator->RunNFrames(1);
    ttd::TTDCdDrive same(_context);
    same.TTDLoadState(recorded.data());
    EXPECT_EQ(same.DiscMismatches(), 0u);
    ASSERT_EQ(Cd().Disc()->ReadAudio(lba, again), CdImage::ReadResult::Ok);
    EXPECT_EQ(std::memcmp(first, again, sizeof(first)), 0) << "sample for sample";

    // Other content: restored, and reported
    cdtest::WriteFile(copy.Path() / "2 b.wav", cdtest::Wave(cdtest::TonePcm(300, 661.0)));
    ASSERT_TRUE(_context->pMediaManager->Insert("ide0.slave", source, options).Ok());
    _emulator->RunNFrames(1);
    ttd::TTDCdDrive other(_context);
    other.TTDLoadState(recorded.data());
    EXPECT_EQ(other.DiscMismatches(), 1u);
}
