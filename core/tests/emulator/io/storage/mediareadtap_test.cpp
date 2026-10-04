// MediaReadTap: the top of every block medium's stack. Recording, each
// sector read is passed to the read journal; replaying, the journal's bytes
// are returned even when the image has changed since; a read the journal
// does not have goes to the image

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <vector>

#include "emulator/io/storage/mediareadtap.h"
#include "emulator/io/storage/memorydisk.h"

namespace
{
struct FakeJournal : IMediaReadJournal
{
    bool playing = false;
    std::map<uint64_t, std::vector<uint8_t>> recorded;
    std::vector<std::string> slots;
    bool Playing() const override { return playing; }
    bool Play(const std::string& slot, uint64_t lba, uint8_t* out, size_t size) override
    {
        auto it = recorded.find(lba);
        if (slot != "sd.test" || it == recorded.end())
            return false;
        std::copy(it->second.begin(), it->second.begin() + size, out);
        return true;
    }
    void Record(const std::string& slot, uint64_t lba, const uint8_t* bytes, size_t size) override
    {
        slots.push_back(slot);
        recorded[lba].assign(bytes, bytes + size);
    }
};
}  // namespace

TEST(MediaReadTap_Test, RecordsReadsAndPlaysThemBackWhateverTheImageHolds)
{
    auto disk = std::make_unique<MemoryDisk>(4);
    MemoryDisk* image = disk.get();
    image->Data()[512] = 0x11;
    MediaReadTap tap(std::move(disk));
    uint8_t sector[512];

    ASSERT_TRUE(tap.ReadSector(1, sector)) << "unbound: passes through";
    FakeJournal journal;
    IMediaReadJournal* current = nullptr;
    tap.Bind(&current, "sd.test");
    ASSERT_TRUE(tap.ReadSector(1, sector)) << "no journal: passes through";
    EXPECT_TRUE(journal.recorded.empty());

    current = &journal;
    ASSERT_TRUE(tap.ReadSector(1, sector));
    ASSERT_EQ(journal.recorded.count(1), 1u) << "recording: the read is journaled";
    EXPECT_EQ(journal.slots[0], "sd.test");
    EXPECT_EQ(journal.recorded[1][0], 0x11);

    image->Data()[512] = 0x99;   // the image changes after the recording
    journal.playing = true;
    ASSERT_TRUE(tap.ReadSector(1, sector));
    EXPECT_EQ(sector[0], 0x11) << "replaying: the recorded bytes, not the image's";
    image->Data()[1024] = 0x77;
    ASSERT_TRUE(tap.ReadSector(2, sector));
    EXPECT_EQ(sector[0], 0x77) << "a read the journal lacks goes to the image";
    EXPECT_EQ(journal.recorded.count(2), 0u) << "replaying records nothing";
}
