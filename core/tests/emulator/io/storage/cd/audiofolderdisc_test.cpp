// A folder of MP3 / FLAC / WAV files as an audio CD (audiofolderdisc.h): natural order, one
// track per file with 2-second pregaps, the 4-second minimum, the 99-track and 80-minute caps
// with "the first N that fit, no packing", the skip report, the samples on the disc, the
// identity (content, not path), and the media manager: a folder into a CD slot (format
// audio-cd or detected), the track list in the media info, the insert targets.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/cdtestdisc.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/storage/cd/audiofolderdisc.h"
#include "emulator/media/mediacontrol.h"
#include "emulator/media/mediamanager.h"
#include "emulator/media/mediatargets.h"

using namespace AudioFolderDisc;

namespace
{
    /// A WAVE file of `frames` CD frames of a tone (44.1 kHz 16-bit stereo, cdtestdisc.h)
    void Tone(const std::filesystem::path& path, uint32_t frames, double hz)
    {
        cdtest::WriteFile(path, cdtest::Wave(cdtest::TonePcm(frames, hz)));
    }

    std::vector<std::string> Lines(const Result& result)
    {
        return result.Lines();
    }

    bool Contains(const std::vector<std::string>& lines, const std::string& needle)
    {
        return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(needle) != std::string::npos; });
    }
}  // namespace

TEST(AudioFolderDisc_Test, NaturalOrder)
{
    EXPECT_LT(NaturalCompare("2 b.mp3", "10 a.mp3"), 0);
    EXPECT_LT(NaturalCompare("track9.wav", "track10.wav"), 0);
    EXPECT_LT(NaturalCompare("Abc.wav", "abd.wav"), 0) << "ASCII case ignored";
    EXPECT_LT(NaturalCompare("01 x.wav", "1 y.wav"), 0) << "the same number: the rest decides";
    EXPECT_NE(NaturalCompare("01.wav", "1.wav"), 0) << "ties broken by the bytes: a strict order";
    EXPECT_EQ(NaturalCompare("a.wav", "a.wav"), 0);
    EXPECT_LT(NaturalCompare("a", "a1"), 0);
    std::vector<std::string> names = {"10 c.flac", "1 a.mp3", "Bonus.wav", "2 b.wav", "a.wav"};
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return NaturalCompare(a, b) < 0; });
    EXPECT_EQ(names, (std::vector<std::string>{"1 a.mp3", "2 b.wav", "10 c.flac", "a.wav", "Bonus.wav"}));
}

TEST(AudioFolderDisc_Test, TracksInNaturalOrderWithPregapsAndSkips)
{
    ScratchFolder folder("audio-cd-order");
    Tone(folder.Path() / "10 last.wav", 75, 880);        // 1 s: padded to 4 s
    Tone(folder.Path() / "2 second.wav", 600, 660);      // 8 s
    std::filesystem::copy_file(TestPathHelper::GetTestDataPath("media/audio/tone-440-44k-stereo-16.flac"), folder.Path() / "1 first.flac");
    cdtest::WriteFile(folder.Path() / "3 broken.mp3", "not an mp3 at all");
    cdtest::WriteFile(folder.Path() / "cover.jpg", "\xFF\xD8\xFF");
    Tone(folder.Path() / ".hidden.wav", 75, 100);
    std::filesystem::create_directories(folder.Path() / "sub");
    Tone(folder.Path() / "sub" / "0 nested.wav", 75, 100);

    Result result;
    std::string error;
    std::unique_ptr<CdImage> disc = Build(cdtest::Utf8(folder.Path()), result, &error);
    ASSERT_NE(disc, nullptr) << error;
    EXPECT_EQ(disc->Format(), "audio-cd");
    ASSERT_EQ(disc->TrackCount(), 3u);
    EXPECT_EQ(disc->SessionCount(), 1);
    EXPECT_EQ(disc->TrackTitle(0), "1 first.flac");
    EXPECT_EQ(disc->TrackTitle(1), "2 second.wav");
    EXPECT_EQ(disc->TrackTitle(2), "10 last.wav");
    // Track 1: 0.25 s FLAC padded to 300 frames at LBA 0; then 150-frame pregaps
    EXPECT_EQ(disc->TrackAt(0).startLba, 0u);
    EXPECT_EQ(disc->TrackAt(0).endLba, 300u);
    EXPECT_EQ(disc->TrackAt(1).pregapLba, 300u);
    EXPECT_EQ(disc->TrackAt(1).startLba, 450u);
    EXPECT_EQ(disc->TrackAt(1).endLba, 1050u);
    EXPECT_EQ(disc->TrackAt(2).startLba, 1200u);
    EXPECT_EQ(disc->TrackAt(2).Frames(), 300u) << "the Red Book's 4-second minimum";
    EXPECT_EQ(disc->LeadOutLba(), 1500u);
    for (size_t i = 0; i < 3; i++)
    {
        EXPECT_TRUE(disc->TrackAt(i).IsAudio());
        EXPECT_EQ(disc->TrackAt(i).number, i + 1);
    }

    // The report: every audio file with what happened to it, the rest counted
    ASSERT_EQ(result.files.size(), 4u);
    EXPECT_EQ(result.files[2].name, "3 broken.mp3");
    EXPECT_EQ(result.files[2].status, FileEntry::Status::Skipped);
    EXPECT_NE(result.files[2].reason.find("does not decode"), std::string::npos) << result.files[2].reason;
    EXPECT_EQ(result.ignored, 3u) << "cover.jpg, .hidden.wav, sub/";
    const std::vector<std::string> lines = Lines(result);
    EXPECT_TRUE(Contains(lines, "track 01: 1 first.flac (flac, 0:04)"));
    EXPECT_TRUE(Contains(lines, "track 03: 10 last.wav (wav, 0:04)"));
    EXPECT_TRUE(Contains(lines, "skipped: 3 broken.mp3"));
    EXPECT_TRUE(Contains(lines, "ignored: 3"));

    // The samples on the disc: the WAV's, sample for sample; the pregap and the padding silent
    int16_t frame[cdtest::kSamples * 2];
    const std::string pcm = cdtest::TonePcm(600, 660);
    for (uint32_t f = 0; f < 600; f += 97)
    {
        ASSERT_EQ(disc->ReadAudio(450 + f, frame), CdImage::ReadResult::Ok);
        ASSERT_EQ(std::memcmp(frame, pcm.data() + f * cdtest::kFrame, cdtest::kFrame), 0) << "frame " << f;
    }
    ASSERT_EQ(disc->ReadAudio(320, frame), CdImage::ReadResult::Ok);
    EXPECT_TRUE(std::all_of(std::begin(frame), std::end(frame), [](int16_t s) { return s == 0; })) << "the pregap";
    ASSERT_EQ(disc->ReadAudio(1300, frame), CdImage::ReadResult::Ok);
    EXPECT_TRUE(std::all_of(std::begin(frame), std::end(frame), [](int16_t s) { return s == 0; })) << "the padding";
    uint8_t user[2048];
    EXPECT_EQ(disc->ReadUser(10, user), CdImage::ReadResult::AudioTrack) << "no data on an audio CD";
}

TEST(AudioFolderDisc_Test, NinetyNineTracksAtMost)
{
    // 100 files of 0.1 s: 99 tracks (4 s each with their pregaps), the 100th not taken
    ScratchFolder folder("audio-cd-99");
    const std::string wave = cdtest::Wave(cdtest::TonePcm(8, 440));
    for (int i = 1; i <= 100; i++)
        cdtest::WriteFile(folder.Path() / (std::to_string(i) + ".wav"), wave);
    Result result;
    std::string error;
    std::unique_ptr<CdImage> disc = Build(cdtest::Utf8(folder.Path()), result, &error);
    ASSERT_NE(disc, nullptr) << error;
    EXPECT_EQ(disc->TrackCount(), 99u);
    EXPECT_EQ(disc->LastTrackNumber(), 99);
    EXPECT_EQ(disc->TrackTitle(98), "99.wav");
    ASSERT_EQ(result.files.size(), 100u);
    EXPECT_EQ(result.files.back().name, "100.wav");
    EXPECT_EQ(result.files.back().status, FileEntry::Status::NotTaken);
    EXPECT_NE(result.files.back().reason.find("99 tracks"), std::string::npos) << result.files.back().reason;
}

TEST(AudioFolderDisc_Test, CapacityTakesTheFirstFilesThatFitNoPacking)
{
    // A disc of 150 + 750 frames: "1" (6 s = 450) fits; "2" (6 s + the 2 s pregap) does not: the
    // disc ends there, and "3" (4 s, it would fit) is not tried
    ScratchFolder folder("audio-cd-full");
    Tone(folder.Path() / "1.wav", 450, 440);
    Tone(folder.Path() / "2.wav", 450, 550);
    Tone(folder.Path() / "3.wav", 300, 660);
    Options options;
    options.capacityFrames = 150 + 750;
    Result result;
    std::string error;
    std::unique_ptr<CdImage> disc = Build(cdtest::Utf8(folder.Path()), result, &error, options);
    ASSERT_NE(disc, nullptr) << error;
    EXPECT_EQ(disc->TrackCount(), 1u);
    ASSERT_EQ(result.files.size(), 3u);
    EXPECT_EQ(result.files[1].status, FileEntry::Status::NotTaken);
    EXPECT_NE(result.files[1].reason.find("does not fit: needs 0:08"), std::string::npos) << result.files[1].reason;
    EXPECT_NE(result.files[1].reason.find("0:04 left"), std::string::npos) << result.files[1].reason;
    EXPECT_EQ(result.files[2].status, FileEntry::Status::NotTaken);
    EXPECT_NE(result.files[2].reason.find("first file that did not fit (2.wav)"), std::string::npos) << result.files[2].reason;

    // The real capacity: 80 minutes, the lead-out at 80:00:00 at the latest
    EXPECT_EQ(kCapacityFrames, 360000u);
    EXPECT_EQ(Options{}.capacityFrames, kCapacityFrames);

    // Nothing that fits, nothing to take: an error naming the reason
    Tone(folder.Path() / "0.wav", 1100, 440);
    options.capacityFrames = 1000;
    EXPECT_EQ(Build(cdtest::Utf8(folder.Path()), result, &error, options), nullptr);
    EXPECT_NE(error.find("does not fit"), std::string::npos) << error;
    ScratchFolder empty("audio-cd-empty");
    cdtest::WriteFile(empty.Path() / "readme.txt", "x");
    EXPECT_EQ(Build(cdtest::Utf8(empty.Path()), result, &error), nullptr);
    EXPECT_NE(error.find("no MP3, FLAC or WAV"), std::string::npos) << error;
    EXPECT_FALSE(HasAudioFiles(cdtest::Utf8(empty.Path())));
}

TEST(AudioFolderDisc_Test, IdentityFollowsTheContentNotThePath)
{
    ScratchFolder a("audio-cd-id-a");
    ScratchFolder b("audio-cd-id-b");
    for (const ScratchFolder* f : {&a, &b})
    {
        Tone(f->Path() / "1.wav", 300, 440);
        Tone(f->Path() / "2.wav", 300, 660);
    }
    Result result;
    auto discA = Build(cdtest::Utf8(a.Path()), result);
    auto discB = Build(cdtest::Utf8(b.Path()), result);
    ASSERT_NE(discA, nullptr);
    ASSERT_NE(discB, nullptr);
    EXPECT_EQ(discA->ContentId(), discB->ContentId()) << "the same files elsewhere: the same disc";
    Tone(b.Path() / "2.wav", 300, 661);
    auto changed = Build(cdtest::Utf8(b.Path()), result);
    EXPECT_NE(changed->ContentId(), discA->ContentId());
    std::filesystem::rename(b.Path() / "2.wav", b.Path() / "3.wav");
    Tone(b.Path() / "3.wav", 300, 660);
    auto renamed = Build(cdtest::Utf8(b.Path()), result);
    EXPECT_NE(renamed->ContentId(), discA->ContentId()) << "the names are part of it";
}

/// region <The media manager>

namespace
{
    class AudioFolderDiscMedia_Test : public ::testing::Test
    {
    protected:
        Emulator* _emulator = nullptr;
        EmulatorContext* _context = nullptr;

        void SetUp() override
        {
            _emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);  // ZX-Evo: CD on ide0.slave
            ASSERT_NE(_emulator, nullptr);
            _context = _emulator->GetContext();
        }
        void TearDown() override
        {
            if (_emulator)
                EmulatorTestHelper::CleanupEmulator(_emulator);
            MessageCenter::DisposeDefaultMessageCenter();
        }
    };
}  // namespace

TEST_F(AudioFolderDiscMedia_Test, FolderIntoACdSlot)
{
    ScratchFolder folder("audio-cd-media");
    Tone(folder.Path() / "2 b.wav", 300, 660);
    Tone(folder.Path() / "1 a.wav", 300, 440);
    MediaControl control(_context);

    // format: audio-cd, through the shared media verbs every surface uses
    MediaRequest insert;
    insert.verb = "insert";
    insert.selector = "ide0.slave";
    insert.path = cdtest::Utf8(folder.Path());
    insert.options = {{"format", "audio-cd"}, {"immediate", "true"}};
    MediaReply reply = control.Execute(insert);
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_TRUE(Contains(reply.result.report, "track 01: 1 a.wav (wav, 0:04)"));
    EXPECT_TRUE(Contains(reply.result.report, "track 02: 2 b.wav"));
    _emulator->RunNFrames(1);

    MediaRequest info;
    info.verb = "info";
    info.selector = "ide0.slave";
    reply = control.Execute(info);
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    const StateNode* medium = reply.body["info"].find("medium");
    ASSERT_NE(medium, nullptr);
    EXPECT_EQ(medium->find("format")->s, "audio-cd");
    EXPECT_EQ(medium->find("access")->s, "readonly");
    const StateNode* disc = medium->find("disc");
    ASSERT_NE(disc, nullptr) << "the track list";
    ASSERT_EQ(disc->find("tracks")->items.size(), 2u);
    EXPECT_EQ(disc->find("tracks")->items[1].find("title")->s, "2 b.wav");
    EXPECT_EQ(disc->find("tracks")->items[1].find("start_msf")->s, "00:08:00");
    EXPECT_TRUE(Contains(reply.result.report, "track 02: 2 b.wav"));

    // Without a format: a folder in a CD slot is an audio CD
    insert.options = {{"immediate", "true"}};
    reply = control.Execute(insert);
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;

    // Refusals: another format for a folder; audio-cd for a file; a folder without audio files
    insert.options = {{"format", "iso"}, {"immediate", "true"}};
    EXPECT_FALSE(control.Execute(insert).result.Ok());
    ScratchFolder empty("audio-cd-media-empty");
    cdtest::WriteFile(empty.Path() / "x.txt", "x");
    insert.path = cdtest::Utf8(empty.Path());
    insert.options = {{"immediate", "true"}};
    reply = control.Execute(insert);
    EXPECT_FALSE(reply.result.Ok());
    EXPECT_NE(reply.result.message.find("no MP3, FLAC or WAV"), std::string::npos) << reply.result.message;
    insert.path = cdtest::WriteMusicDisc(empty.Path(), 1, 4, 300);
    insert.options = {{"format", "audio-cd"}, {"immediate", "true"}};
    reply = control.Execute(insert);
    EXPECT_FALSE(reply.result.Ok());
    EXPECT_NE(reply.result.message.find("takes a folder"), std::string::npos) << reply.result.message;
}

TEST_F(AudioFolderDiscMedia_Test, TargetsOfferAnAudioFolderToTheCdDriveFirst)
{
    ScratchFolder music("audio-cd-targets");
    Tone(music.Path() / "1.wav", 300, 440);
    const FileClass file = MediaTargets::Classify(cdtest::Utf8(music.Path()));
    EXPECT_TRUE(file.folder);
    EXPECT_EQ(file.format, "audio-folder");
    ASSERT_FALSE(file.kinds.empty());
    EXPECT_EQ(file.kinds.front(), FileKind::Optical);
    const MediaPlan plan = MediaTargets::Plan(_context, file);
    ASSERT_FALSE(plan.targets.empty()) << plan.refusal;
    EXPECT_EQ(plan.targets.front().slotId, "ide0.slave");
    EXPECT_EQ(plan.targets.front().as, FileKind::Optical);

    // A folder without audio files: no CD drive offered
    ScratchFolder plain("audio-cd-targets-plain");
    cdtest::WriteFile(plain.Path() / "a.txt", "x");
    const MediaPlan other = MediaTargets::Plan(_context, MediaTargets::Classify(cdtest::Utf8(plain.Path())));
    for (const MediaTarget& target : other.targets)
        EXPECT_NE(target.as, FileKind::Optical) << target.slotId;
}

/// endregion </The media manager>
