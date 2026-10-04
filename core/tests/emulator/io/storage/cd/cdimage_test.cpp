// CdImage and time travel's media read journal: a drive's data reads
// (ReadUser, ReadFrame - the ATAPI drive reads them past the block stack) are
// recorded and, replaying, handed back whatever the image holds now. The
// audio samples (ReadAudio: the drive's output) and the block stack's sector
// reads (its MediaReadTap records those) are not recorded here.

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "_helpers/cdtestdisc.h"
#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/cd/cdimageformats.h"
#include "emulator/media/mediareadjournal.h"

using namespace cdtest;

namespace
{
struct FakeJournal : IMediaReadJournal
{
    bool playing = false;
    std::map<std::pair<uint64_t, size_t>, std::vector<uint8_t>> recorded;   // (lba, size) -> bytes
    bool Playing() const override { return playing; }
    bool Play(const std::string& slot, uint64_t lba, uint8_t* out, size_t size) override
    {
        auto it = recorded.find({lba, size});
        if (slot != "cd.test" || it == recorded.end())
            return false;
        std::memcpy(out, it->second.data(), size);
        return true;
    }
    void Record(const std::string& slot, uint64_t lba, const uint8_t* bytes, size_t size) override
    {
        EXPECT_EQ(slot, "cd.test");
        recorded[{lba, size}].assign(bytes, bytes + size);
    }
};
}  // namespace

TEST(CdImage_Test, DriveDataReadsAreJournaled_AudioAndBlockReadsAreNot)
{
    ScratchFolder folder("cd-journal");
    WriteFile(folder.Path() / "data.bin", DataFrames(0, 8, true));
    std::string error;
    auto disc = CdImageFormats::Open(Utf8(folder.Path() / "data.bin"), &error);
    ASSERT_NE(disc, nullptr) << error;

    FakeJournal journal;
    IMediaReadJournal* current = &journal;
    disc->BindReadJournal(&current, "cd.test");
    uint8_t user[2048];
    uint8_t frame[2352];
    int16_t samples[588 * 2];
    uint8_t sector[512];
    ASSERT_EQ(disc->ReadUser(4, user), CdImage::ReadResult::Ok);
    ASSERT_EQ(disc->ReadFrame(5, frame), CdImage::ReadResult::Ok);
    ASSERT_EQ(disc->ReadAudio(6, samples), CdImage::ReadResult::Ok);
    ASSERT_TRUE(disc->ReadSector(7 * 4, sector));
    ASSERT_EQ(journal.recorded.size(), 2u) << "the drive's two data reads only";
    EXPECT_EQ(journal.recorded.count({4, 2048}), 1u);
    EXPECT_EQ(journal.recorded.count({5, 2352}), 1u);
    const std::vector<uint8_t> recordedUser(user, user + 2048);

    // The image changes after the recording; a replay gets the recorded bytes
    WriteFile(folder.Path() / "data.bin", DataFrames(100, 8, true));
    disc = CdImageFormats::Open(Utf8(folder.Path() / "data.bin"), &error);
    ASSERT_NE(disc, nullptr) << error;
    disc->BindReadJournal(&current, "cd.test");
    journal.playing = true;
    ASSERT_EQ(disc->ReadUser(4, user), CdImage::ReadResult::Ok);
    EXPECT_EQ(std::vector<uint8_t>(user, user + 2048), recordedUser) << "replaying: the recorded data";
    ASSERT_EQ(disc->ReadUser(3, user), CdImage::ReadResult::Ok);
    EXPECT_EQ(std::vector<uint8_t>(user, user + 2048), UserData(103)) << "a read the journal lacks: the image";
    EXPECT_EQ(journal.recorded.size(), 2u) << "replaying records nothing";
}
