// CdAudioControl (cdaudiocontrol.h): the CD audio report and verbs every
// automation surface shares - status, play (track / LBA / MSF), pause, resume,
// stop, volume (page 0Eh), mixer; drive selection; the errors; refused while
// TTD records. Multisession: the play ends at its session; a range into a data track is refused.

#include <gtest/gtest.h>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/cdtestdisc.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/scratchfolder.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/ide/cdaudiocontrol.h"
#include "emulator/media/mediamanager.h"
#include "emulator/sound/soundmanager.h"

namespace
{
    class CdAudioControl_Test : public ::testing::Test
    {
    protected:
        Emulator* _emulator = nullptr;
        EmulatorContext* _context = nullptr;
        std::unique_ptr<ScratchFolder> _folder;

        void SetUp() override
        {
            _emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
            ASSERT_NE(_emulator, nullptr);
            _context = _emulator->GetContext();
            _folder = std::make_unique<ScratchFolder>("cd-control");
            MediaSource source;
            source.path = cdtest::WriteMusicDisc(_folder->Path(), 2, 2, 16, cdtest::MusicLayout::Mixed);
            InsertOptions options;
            options.immediate = true;
            ASSERT_TRUE(_context->pMediaManager->Insert("ide0.slave", source, options).Ok());
            _emulator->RunNFrames(1);
        }
        void TearDown() override
        {
            _folder.reset();
            if (_emulator)
                EmulatorTestHelper::CleanupEmulator(_emulator);
            MessageCenter::DisposeDefaultMessageCenter();
        }

        CdAudioReply Run(const std::string& verb, const std::string& drive = "", std::map<std::string, std::string> options = {})
        {
            return CdAudioControl(_context).Execute({verb, drive, std::move(options)});
        }
        static const StateNode& Audio(const CdAudioReply& reply) { return *reply.body.find("drive")->find("audio"); }
    };
}  // namespace

TEST_F(CdAudioControl_Test, StatusReportsTheDiscAndTheHead)
{
    const StateNode state = CdAudioControl::State(_context);
    EXPECT_TRUE(state.find("available")->b);
    const StateNode& drive = state.find("drives")->items.at(0);
    EXPECT_EQ(drive.find("slot")->s, "ide0.slave");
    EXPECT_EQ(drive.find("unit")->i, 1);
    const StateNode& disc = *drive.find("disc");
    EXPECT_EQ(disc.find("format")->s, "cue");
    EXPECT_EQ(disc.find("last_track")->i, 3);
    const StateNode& tracks = *disc.find("tracks");
    ASSERT_EQ(tracks.items.size(), 3u);
    EXPECT_EQ(tracks.items[1].find("type")->s, "audio");
    EXPECT_EQ(tracks.items[1].find("start_lba")->i, 166);
    EXPECT_EQ(tracks.items[1].find("start_msf")->s, "00:04:16");
    EXPECT_EQ(tracks.items[1].find("length_msf")->s, "00:02:00");
    EXPECT_EQ(drive.find("audio")->find("status")->s, "idle");
    EXPECT_EQ(drive.find("audio")->find("status_code")->i, 0x15);
    EXPECT_EQ(drive.find("drive_volume")->find("left")->i, 255);
    EXPECT_EQ(drive.find("mixer")->find("row")->s, "CD ide0.slave");

    // The same through every drive selector
    EXPECT_TRUE(Run("status", "ide0.slave").ok);
    EXPECT_TRUE(Run("status", "1").ok);
    EXPECT_TRUE(Run("status", "cd").ok);
    const CdAudioReply wrong = Run("status", "ide0.master");
    EXPECT_FALSE(wrong.ok);
    EXPECT_EQ(wrong.error, "no-cd-drive");
    EXPECT_EQ(wrong.HttpStatus(), 404);
    EXPECT_NE(wrong.message.find("ide0.slave"), std::string::npos) << "names the CD drives";
}

TEST_F(CdAudioControl_Test, PlayPauseResumeStop)
{
    CdAudioReply reply = Run("play", "", {{"track", "2"}});
    ASSERT_TRUE(reply.ok) << reply.message;
    EXPECT_EQ(Audio(reply).find("status")->s, "playing");
    EXPECT_EQ(Audio(reply).find("track")->i, 2);
    EXPECT_EQ(Audio(reply).find("play_end_lba")->i, 166 + 150 + 150 + 150) << "through the last track";
    _emulator->RunNFrames(10);
    reply = Run("pause");
    ASSERT_TRUE(reply.ok);
    EXPECT_EQ(Audio(reply).find("status")->s, "paused");
    EXPECT_GT(Audio(reply).find("lba")->i, 166);
    reply = Run("resume");
    EXPECT_EQ(Audio(reply).find("status")->s, "playing");
    reply = Run("stop");
    EXPECT_EQ(Audio(reply).find("status")->s, "idle");
    reply = Run("pause");
    EXPECT_FALSE(reply.ok);
    EXPECT_EQ(reply.error, "not-playing");
    EXPECT_EQ(reply.HttpStatus(), 409);

    reply = Run("play", "", {{"track", "2"}, {"to", "2"}});
    EXPECT_EQ(Audio(reply).find("play_end_lba")->i, 316);
    reply = Run("play", "", {{"lba", "400"}, {"frames", "75"}});
    ASSERT_TRUE(reply.ok) << reply.message;
    EXPECT_EQ(Audio(reply).find("track")->i, 3);
    EXPECT_EQ(Audio(reply).find("index")->i, 0) << "LBA 400 is track 3's pregap";
    reply = Run("play", "", {{"msf", "00:04:16"}, {"end", "00:05:16"}});
    ASSERT_TRUE(reply.ok) << reply.message;
    EXPECT_EQ(Audio(reply).find("play_start_lba")->i, 166);
    EXPECT_EQ(Audio(reply).find("play_end_lba")->i, 241);

    for (const auto& bad : std::vector<std::map<std::string, std::string>>{
             {{"track", "1"}}, {{"track", "7"}}, {{"lba", "10"}, {"frames", "5"}}, {{"lba", "400"}}, {{"msf", "1:2"}, {"end", "00:05:00"}}, {}})
    {
        reply = Run("play", "", bad);
        EXPECT_FALSE(reply.ok);
        EXPECT_EQ(reply.error, "bad-request") << reply.message;
    }
    EXPECT_EQ(Run("eject").error, "bad-request");
}

TEST_F(CdAudioControl_Test, VolumeAndMixer)
{
    CdAudioReply reply = Run("volume", "", {{"left", "128"}, {"right", "64"}, {"route", "swap"}, {"sotc", "on"}});
    ASSERT_TRUE(reply.ok) << reply.message;
    const StateNode& volume = *reply.body.find("drive")->find("drive_volume");
    EXPECT_EQ(volume.find("left")->i, 128);
    EXPECT_EQ(volume.find("right")->i, 64);
    EXPECT_EQ(volume.find("left_channel")->s, "right");
    EXPECT_EQ(volume.find("right_channel")->s, "left");
    EXPECT_TRUE(Audio(reply).find("sotc")->b);
    EXPECT_FALSE(Run("volume", "", {{"left", "300"}}).ok);
    EXPECT_FALSE(Run("volume", "", {{"route", "quad"}}).ok);

    reply = Run("mixer", "", {{"volume", "0.5"}, {"mute", "true"}});
    ASSERT_TRUE(reply.ok) << reply.message;
    const AudioDeviceInfo* row = _context->pSoundManager->device(AudioSourceType::CdAudio1);
    ASSERT_NE(row, nullptr);
    EXPECT_FLOAT_EQ(row->volume, 0.5f);
    EXPECT_TRUE(row->mute);
    EXPECT_TRUE(reply.body.find("drive")->find("mixer")->find("mute")->b);
    EXPECT_FALSE(Run("mixer", "", {{"volume", "9"}}).ok);
}

TEST_F(CdAudioControl_Test, RefusedWhileRecordingAndWithoutADrive)
{
    _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    ASSERT_TRUE(_context->pTimeTravelManager->StartRecording());
    CdAudioReply reply = Run("play", "", {{"track", "2"}});
    EXPECT_FALSE(reply.ok);
    EXPECT_EQ(reply.error, "recording");
    EXPECT_EQ(reply.HttpStatus(), 409);
    EXPECT_TRUE(Run("mixer", "", {{"mute", "false"}}).ok) << "the mixer row is host state";
    EXPECT_TRUE(Run("status").ok);
    _context->pTimeTravelManager->StopRecording();

    Emulator* spectrum = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(spectrum, nullptr);
    const StateNode state = CdAudioControl::State(spectrum->GetContext());
    EXPECT_FALSE(state.find("available")->b);
    EXPECT_EQ(CdAudioControl(spectrum->GetContext()).Execute({"play", "", {{"track", "1"}}}).error, "no-cd-drive");
    EmulatorTestHelper::CleanupEmulator(spectrum);
}

TEST_F(CdAudioControl_Test, EnhancedCdPlaysItsAudioSessionAndRefusesTheData)
{
    // The default test disc: session 1 audio tracks 1-2, session 2 data track 3
    ScratchFolder folder("cd-control-enhanced");
    MediaSource source;
    source.path = cdtest::WriteMusicDisc(folder.Path(), 2, 4, 300);
    InsertOptions options;
    options.immediate = true;
    ASSERT_TRUE(_context->pMediaManager->Insert("ide0.slave", source, options).Ok());
    _emulator->RunNFrames(1);
    const cdtest::MusicDiscLayout l = cdtest::MusicLayoutOf(2, 4, 300, cdtest::MusicLayout::Enhanced);

    const StateNode state = CdAudioControl::State(_context);
    const StateNode& disc = *state.find("drives")->items.at(0).find("disc");
    EXPECT_EQ(disc.find("sessions")->i, 2);
    const StateNode& tracks = *disc.find("tracks");
    ASSERT_EQ(tracks.items.size(), 3u);
    EXPECT_EQ(tracks.items[0].find("session")->i, 1);
    EXPECT_EQ(tracks.items[2].find("session")->i, 2);
    EXPECT_EQ(tracks.items[2].find("type")->s, "mode2");
    EXPECT_EQ(tracks.items[2].find("start_lba")->i, static_cast<int64_t>(l.dataStart));

    // track=1 without to=: through the last audio track of the session
    CdAudioReply reply = Run("play", "", {{"track", "1"}});
    ASSERT_TRUE(reply.ok) << reply.message;
    EXPECT_EQ(Audio(reply).find("play_end_lba")->i, static_cast<int64_t>(l.audioLeadOut));
    // to=3 (the data track): the session ends first, the play stops at its lead-out
    reply = Run("play", "", {{"track", "2"}, {"to", "3"}});
    ASSERT_TRUE(reply.ok) << reply.message;
    EXPECT_EQ(Audio(reply).find("play_end_lba")->i, static_cast<int64_t>(l.audioLeadOut));
    // The data track itself: no audio frame
    reply = Run("play", "", {{"track", "3"}});
    EXPECT_FALSE(reply.ok);
    EXPECT_EQ(reply.error, "bad-request");

    // A mixed-mode disc with data after audio in one session: a range into it is refused
    ScratchFolder mixed("cd-control-a-then-d");
    cdtest::WriteFile(mixed.Path() / "d.bin", cdtest::TonePcm(300, 440) + cdtest::DataFrames(300, 300, true));
    cdtest::WriteFile(mixed.Path() / "d.cue", "FILE \"d.bin\" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n"
                                              "  TRACK 02 MODE1/2352\n    INDEX 01 00:04:00\n");
    source.path = cdtest::Utf8(mixed.Path() / "d.cue");
    ASSERT_TRUE(_context->pMediaManager->Insert("ide0.slave", source, options).Ok());
    _emulator->RunNFrames(1);
    reply = Run("play", "", {{"lba", "100"}, {"frames", "300"}});
    EXPECT_FALSE(reply.ok);
    EXPECT_NE(reply.message.find("data track 2"), std::string::npos) << reply.message;
    reply = Run("play", "", {{"track", "1"}});
    ASSERT_TRUE(reply.ok) << reply.message;
    EXPECT_EQ(Audio(reply).find("play_end_lba")->i, 300) << "to the data track";
}
