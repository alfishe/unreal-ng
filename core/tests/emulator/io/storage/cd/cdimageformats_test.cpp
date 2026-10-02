// The CD image formats (cdimageformats.h): CUE sheets (one or several BINs,
// INDEX 00 pregaps in the file, PREGAP / POSTGAP silence, MOTOROLA byte order,
// WAVE files, errors), ISO, raw BIN, and MAME CD CHDs (cdlz / cdzl / cdfl /
// cdzs, audio stored big-endian, pregaps in the CHD, track padding), all read
// back sample-exact against the ramps of cdtestdisc.h.

#include <gtest/gtest.h>

#include <cstring>

#include "_helpers/cdtestdisc.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/testpathhelper.h"
#include "emulator/io/storage/cd/cdimageformats.h"

using namespace cdtest;

namespace
{
    std::string Fixture(const std::string& name)
    {
        return TestPathHelper::GetTestDataPath("media/cd/" + name);
    }

    /// Every sample of `frames` frames from `lba` is ramp sample `first + i`
    void ExpectRamp(CdImage& disc, uint32_t lba, uint32_t frames, const Ramp& ramp, uint64_t first)
    {
        int16_t samples[kSamples * 2];
        for (uint32_t f = 0; f < frames; f++)
        {
            ASSERT_EQ(disc.ReadAudio(lba + f, samples), CdImage::ReadResult::Ok) << "LBA " << lba + f;
            for (uint32_t i = 0; i < kSamples; i++)
            {
                const uint64_t n = first + static_cast<uint64_t>(f) * kSamples + i;
                ASSERT_EQ(samples[2 * i], ramp.Left(n)) << "LBA " << lba + f << " sample " << i;
                ASSERT_EQ(samples[2 * i + 1], ramp.Right(n)) << "LBA " << lba + f << " sample " << i;
            }
        }
    }

    void ExpectSilence(CdImage& disc, uint32_t lba, uint32_t frames)
    {
        int16_t samples[kSamples * 2];
        for (uint32_t f = 0; f < frames; f++)
        {
            ASSERT_EQ(disc.ReadAudio(lba + f, samples), CdImage::ReadResult::Ok);
            for (int16_t s : samples)
                ASSERT_EQ(s, 0) << "LBA " << lba + f;
        }
    }

    /// The fixture disc's layout (cdtestdisc.h WriteFixtureDisc, tools/cd/make-test-fixtures.py)
    void ExpectFixtureLayout(CdImage& disc)
    {
        ASSERT_EQ(disc.TrackCount(), 3u);
        EXPECT_EQ(disc.LeadOutLba(), 22u);
        const cd::Track& t1 = disc.TrackAt(0);
        EXPECT_EQ(t1.number, 1);
        EXPECT_EQ(t1.mode, cd::TrackMode::Mode1);
        EXPECT_EQ(t1.startLba, 0u);
        EXPECT_EQ(t1.endLba, 4u);
        const cd::Track& t2 = disc.TrackAt(1);
        EXPECT_TRUE(t2.IsAudio());
        EXPECT_EQ(t2.pregapLba, 4u);
        EXPECT_EQ(t2.startLba, 8u);
        EXPECT_EQ(t2.endLba, 16u);
        const cd::Track& t3 = disc.TrackAt(2);
        EXPECT_TRUE(t3.IsAudio());
        EXPECT_EQ(t3.pregapLba, 16u);
        EXPECT_EQ(t3.startLba, 18u);
        EXPECT_EQ(t3.endLba, 22u);
        EXPECT_EQ(disc.IndexAt(5), 0);
        EXPECT_EQ(disc.IndexAt(8), 1);
        EXPECT_EQ(disc.TrackIndexAt(17), 2);
        EXPECT_EQ(disc.TrackIndexAt(22), -1);
        EXPECT_TRUE(disc.HasAudio());
    }

    /// The fixture disc's samples and data, whatever the format
    void ExpectFixtureContents(CdImage& disc)
    {
        ExpectSilence(disc, 4, 4);       // track 2's pregap, stored as silence
        ExpectRamp(disc, 8, 8, kRampA, 0);
        ExpectSilence(disc, 16, 2);      // track 3's PREGAP: not stored
        ExpectRamp(disc, 18, 4, kRampB, 0);

        uint8_t user[cd::kUserBytes];
        uint8_t frame[cd::kFrameBytes];
        for (uint32_t lba = 0; lba < 4; lba++)
        {
            ASSERT_EQ(disc.ReadUser(lba, user), CdImage::ReadResult::Ok);
            EXPECT_EQ(std::vector<uint8_t>(user, user + cd::kUserBytes), UserData(lba)) << "LBA " << lba;
            ASSERT_EQ(disc.ReadFrame(lba, frame), CdImage::ReadResult::Ok);
            const std::string expected = DataFrames(lba, 1, true);
            EXPECT_EQ(0, std::memcmp(frame, expected.data(), cd::kFrameBytes)) << "LBA " << lba << ": sync, header, EDC, ECC";
        }
        EXPECT_EQ(disc.ReadUser(8, user), CdImage::ReadResult::AudioTrack);
        EXPECT_EQ(disc.ReadFrame(22, frame), CdImage::ReadResult::OutOfRange);

        // As 512-byte sectors: the data blocks only
        uint8_t sector[512];
        ASSERT_TRUE(disc.ReadSector(3 * 4 + 2, sector));
        EXPECT_EQ(0, std::memcmp(sector, UserData(3).data() + 1024, 512));
        EXPECT_FALSE(disc.ReadSector(8 * 4, sector)) << "an audio frame has no user data";
        EXPECT_EQ(disc.SectorCount(), 22u * 4);
    }
}  // namespace

TEST(CdImageFormats_Test, CueWithOneBinPerTrack)
{
    ScratchFolder folder("cd-cue");
    const std::string cue = WriteFixtureDisc(folder.Path());
    EXPECT_EQ(CdImageFormats::Probe(cue), "cue");
    std::string error;
    auto disc = CdImageFormats::Open(cue, &error);
    ASSERT_NE(disc, nullptr) << error;
    EXPECT_EQ(disc->Format(), "cue");
    ExpectFixtureLayout(*disc);
    ExpectFixtureContents(*disc);
}

TEST(CdImageFormats_Test, CueWithSeveralTracksInOneBin)
{
    // One BIN with a MODE1/2352 track and two audio tracks: the first audio track's
    // INDEX 00 pregap inside the file, then a POSTGAP of silence that is not
    ScratchFolder folder("cd-cue-one-bin");
    const std::string bin = DataFrames(0, 4, true) + std::string(3 * kFrame, '\0') + RampPcm(kRampA, 5) + RampPcm(kRampB, 6);
    WriteFile(folder.Path() / "all.bin", bin);
    WriteFile(folder.Path() / "all.cue",
              "REM a comment\nCATALOG 0000000000000\nFILE all.bin BINARY\n"
              "  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
              "  TRACK 02 AUDIO\n    TITLE \"Ramp A\"\n    INDEX 00 00:00:04\n    INDEX 01 00:00:07\n    POSTGAP 00:00:02\n"
              "  TRACK 03 AUDIO\n    FLAGS DCP\n    INDEX 01 00:00:12\n");
    std::string error;
    auto disc = CdImageFormats::Open(Utf8(folder.Path() / "all.cue"), &error);
    ASSERT_NE(disc, nullptr) << error;
    ASSERT_EQ(disc->TrackCount(), 3u);
    EXPECT_EQ(disc->TrackAt(1).pregapLba, 4u);
    EXPECT_EQ(disc->TrackAt(1).startLba, 7u);
    EXPECT_EQ(disc->TrackAt(1).endLba, 14u) << "5 frames + a 2-frame postgap";
    EXPECT_EQ(disc->TrackAt(2).startLba, 14u);
    EXPECT_EQ(disc->LeadOutLba(), 20u);
    ExpectRamp(*disc, 7, 5, kRampA, 0);
    ExpectSilence(*disc, 12, 2);
    ExpectRamp(*disc, 14, 6, kRampB, 0);
}

TEST(CdImageFormats_Test, CueByteOrderCookedTracksAndWave)
{
    ScratchFolder folder("cd-cue-kinds");
    WriteFile(folder.Path() / "data.iso", DataFrames(0, 6, false));
    WriteFile(folder.Path() / "be.raw", RampPcm(kRampA, 3, /*bigEndian*/ true));
    WriteFile(folder.Path() / "Music.WAV", Wave(RampPcm(kRampB, 2)));
    // A sheet from another system: backslashes, other case, CRLF, a byte order mark
    WriteFile(folder.Path() / "mixed.cue",
              "\xEF\xBB\xBF" "FILE \"subdir\\DATA.ISO\" BINARY\r\n  TRACK 01 MODE1/2048\r\n    INDEX 01 00:00:00\r\n"
              "FILE \"be.raw\" MOTOROLA\r\n  TRACK 02 AUDIO\r\n    INDEX 01 00:00:00\r\n"
              "FILE \"music.wav\" WAVE\r\n  TRACK 03 AUDIO\r\n    PREGAP 00:00:01\r\n    INDEX 01 00:00:00\r\n");
    std::string error;
    auto disc = CdImageFormats::Open(Utf8(folder.Path() / "mixed.cue"), &error);
    ASSERT_NE(disc, nullptr) << error;
    ASSERT_EQ(disc->TrackCount(), 3u);
    EXPECT_EQ(disc->TrackAt(1).startLba, 6u);
    EXPECT_EQ(disc->TrackAt(2).startLba, 10u);
    EXPECT_EQ(disc->LeadOutLba(), 12u);
    ExpectRamp(*disc, 6, 3, kRampA, 0);
    ExpectRamp(*disc, 10, 2, kRampB, 0);

    // A cooked track still reads as whole frames: sync, header, EDC and ECC built
    uint8_t frame[cd::kFrameBytes];
    ASSERT_EQ(disc->ReadFrame(5, frame), CdImage::ReadResult::Ok);
    const std::string expected = DataFrames(5, 1, true);
    EXPECT_EQ(0, std::memcmp(frame, expected.data(), cd::kFrameBytes));
}

TEST(CdImageFormats_Test, CueErrorsNameTheLine)
{
    ScratchFolder folder("cd-cue-errors");
    WriteFile(folder.Path() / "a.bin", std::string(4 * kFrame, '\0'));
    struct Case
    {
        const char* sheet;
        const char* reason;
    };
    const Case cases[] = {
        {"TRACK 01 AUDIO\n", "before any FILE"},
        {"FILE \"missing.bin\" BINARY\n", "is not there"},
        {"FILE \"a.bin\" MP3\n", "not supported"},
        {"FILE \"a.bin\" BINARY\n TRACK 01 MODE3/2352\n INDEX 01 00:00:00\n", "track type"},
        {"FILE \"a.bin\" BINARY\n TRACK 01 AUDIO\n", "no INDEX 01"},
        {"FILE \"a.bin\" BINARY\n TRACK 01 AUDIO\n INDEX 01 00:61:00\n", "bad time"},
        {"FILE \"a.bin\" BINARY\n TRACK 02 AUDIO\n INDEX 01 00:00:00\n TRACK 01 AUDIO\n INDEX 01 00:00:02\n", "ascend"},
        {"REM nothing\n", "no TRACK"},
    };
    for (const Case& c : cases)
    {
        std::string error;
        EXPECT_EQ(CdImageFormats::ParseCue(c.sheet, cdtest::Utf8(folder.Path()), "t.cue", &error), nullptr) << c.sheet;
        EXPECT_NE(error.find(c.reason), std::string::npos) << c.sheet << " -> " << error;
    }
}

TEST(CdImageFormats_Test, IsoAndRawBin)
{
    ScratchFolder folder("cd-iso");
    std::string iso = DataFrames(0, 20, false);
    std::memcpy(iso.data() + 16 * 2048, "\x01" "CD001", 6);
    WriteFile(folder.Path() / "disc.img", iso);
    const std::string isoPath = Utf8(folder.Path() / "disc.img");
    EXPECT_EQ(CdImageFormats::Probe(isoPath), "iso") << "the content decides, not the name";
    std::string error;
    auto disc = CdImageFormats::Open(isoPath, &error);
    ASSERT_NE(disc, nullptr) << error;
    ASSERT_EQ(disc->TrackCount(), 1u);
    EXPECT_EQ(disc->LeadOutLba(), 20u);
    EXPECT_FALSE(disc->HasAudio());

    WriteFile(folder.Path() / "raw.bin", DataFrames(0, 5, true));
    const std::string binPath = Utf8(folder.Path() / "raw.bin");
    EXPECT_EQ(CdImageFormats::Probe(binPath), "bin");
    disc = CdImageFormats::Open(binPath, &error);
    ASSERT_NE(disc, nullptr) << error;
    EXPECT_EQ(disc->LeadOutLba(), 5u);
    uint8_t user[2048];
    ASSERT_EQ(disc->ReadUser(4, user), CdImage::ReadResult::Ok);
    EXPECT_EQ(std::vector<uint8_t>(user, user + 2048), UserData(4));

    WriteFile(folder.Path() / "music.bin", RampPcm(kRampA, 2));
    EXPECT_EQ(CdImageFormats::Probe(Utf8(folder.Path() / "music.bin"), &error), "") << "audio without a sheet";
}

TEST(CdImageFormats_Test, EccMatchesTheReferenceFrames)
{
    // track1.bin: frames the fixture script built, checked there against MAME's ECC tables
    const std::string path = Fixture("track1.bin");
    std::ifstream in(FileHelper::ToFsPath(path), std::ios::binary);
    std::string reference((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_EQ(reference.size(), 4u * kFrame);
    for (uint32_t lba = 0; lba < 4; lba++)
    {
        uint8_t frame[cd::kFrameBytes];
        cd::BuildMode1Frame(frame, lba, UserData(lba).data());
        EXPECT_EQ(0, std::memcmp(frame, reference.data() + lba * kFrame, kFrame)) << "LBA " << lba;
        EXPECT_TRUE(cd::VerifyEcc(frame));
        frame[100] ^= 1;
        EXPECT_FALSE(cd::VerifyEcc(frame));
    }
}

TEST(CdImageFormats_Test, MsfAndLba)
{
    EXPECT_EQ(cd::MsfToLba(0, 2, 0), 0);
    EXPECT_EQ(cd::MsfToLba(0, 0, 0), -150);
    EXPECT_EQ(cd::MsfToLba(3, 12, 40), 14290);
    const cd::Msf msf = cd::LbaToMsf(14290);
    EXPECT_EQ(msf.m, 3);
    EXPECT_EQ(msf.s, 12);
    EXPECT_EQ(msf.f, 40);
    for (uint32_t lba : {0u, 1u, 74u, 75u, 4349u, 4350u, 359849u})
    {
        const cd::Msf m = cd::LbaToMsf(lba);
        EXPECT_EQ(cd::MsfToLba(m.m, m.s, m.f), static_cast<int32_t>(lba));
    }
    const cd::Msf length = cd::FramesToMsf(75 * 61 + 3);
    EXPECT_EQ(length.m, 1);
    EXPECT_EQ(length.s, 1);
    EXPECT_EQ(length.f, 3);
    EXPECT_EQ(cd::ToBcd(59), 0x59);
}

class CdImageFormats_Chd_Test : public ::testing::TestWithParam<const char*>
{
};

TEST_P(CdImageFormats_Chd_Test, SameDiscAsTheCue)
{
    const std::string path = Fixture(GetParam());
    EXPECT_EQ(CdImageFormats::Probe(path), "chd");
    EXPECT_TRUE(CdImageFormats::IsCdChd(path));
    std::string error;
    auto disc = CdImageFormats::Open(path, &error);
    ASSERT_NE(disc, nullptr) << error;
    EXPECT_EQ(disc->Format(), "chd");
    ExpectFixtureLayout(*disc);
    ExpectFixtureContents(*disc);  // audio big-endian in the CHD, swapped back; ECC rebuilt where chdman stripped it
}

// disc-default: hunks in cdlz and cdfl; disc-cdzs-cdzl: hunks in cdzs and cdzl (tools/cd/make-test-fixtures.py)
INSTANTIATE_TEST_SUITE_P(Codecs, CdImageFormats_Chd_Test, ::testing::Values("disc-default.chd", "disc-cdzs-cdzl.chd"));

TEST(CdImageFormats_Test, HardDiskChdIsNoCd)
{
    const std::string hdd = TestPathHelper::GetTestDataPath("media/chd/mixed-default.chd");
    EXPECT_FALSE(CdImageFormats::IsCdChd(hdd));
    std::string error;
    EXPECT_EQ(CdImageFormats::OpenChd(hdd, &error), nullptr);
    EXPECT_NE(error.find("hard-disk CHD"), std::string::npos) << error;
}
