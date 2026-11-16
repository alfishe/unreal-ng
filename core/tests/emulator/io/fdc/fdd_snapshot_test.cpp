// A drive's disk read from another thread while the emulation runs: FDD::SnapshotDisk / FDD::WithDisk.
//
// The WebAPI, the CLI, the scripting planes and the GUI read the inserted image on their own threads. The emulation
// thread meanwhile reshapes tracks (WRITE TRACK reallocates a stream and rebuilds its sector index, FORMAT on the
// uPD765, cylinders added past the end) and swaps disks (the media manager frees the previous image right after the
// insert). A reader holding a raw pointer then walks freed memory - a dump-less death on Windows. These tests pin the
// contract: a reader gets its own copy, the emulation thread waits only for a plain memory copy, a disk change waits
// for a running read and the drive never keeps an image alive (no reference counting: the medium alone owns it).

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/testtier.h"
#include "_helpers/testwaithelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"
#include "loaders/disk/loader_trd.h"
#include "unrealdisk/diskfs.h"
#include "unrealdisk/trdosfs.h"

namespace
{
/// A TR-DOS disk with one file
std::unique_ptr<DiskImage> TrdosDisk(EmulatorContext* context, uint8_t cylinders = 80, uint8_t fill = 0x11)
{
    auto image = std::make_unique<DiskImage>(cylinders, 2);
    LoaderTRD(context, "").format(image.get());
    unrealdisk::TrdosFileSystem fs(*image);
    std::string error;
    unrealdisk::TrdosFileSystem::WriteOptions options;
    fs.Write("data", 'C', std::vector<uint8_t>(4 * 256, fill), options, error);
    return image;
}

/// Every indexed field inside its own track's stream, the track count matching the geometry
bool Consistent(const DiskImage& image)
{
    DiskImage& disk = const_cast<DiskImage&>(image);
    for (uint8_t c = 0; c < disk.getCylinders(); c++)
        for (uint8_t s = 0; s < disk.getSides(); s++)
        {
            DiskImage::Track* track = disk.getTrackForCylinderAndSide(c, s);
            if (!track)
                return false;
            const uint8_t* begin = track->rawData();
            const uint8_t* end = begin + track->rawSize();
            for (size_t i = 0; i < track->sectorCount(); i++)
            {
                const DiskImage::Sector* sector = track->getRawSector(i);
                const auto* id = reinterpret_cast<const uint8_t*>(sector->id);
                if (id < begin || id >= end)
                    return false;
                if (sector->hasData && (sector->data < begin || sector->data + sector->dataSize > end))
                    return false;
            }
        }
    return true;
}
}  // namespace

class FDD_Snapshot_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;

    void SetUp() override { _context = new EmulatorContext(LoggerLevel::LogError); }
    void TearDown() override
    {
        delete _context;
        MessageCenter::DisposeDefaultMessageCenter();
    }

    /// The stress run: the "emulation thread" (this one) reshapes tracks, writes bytes and swaps disks, freeing
    /// each previous image at once; the reader snapshots, walks the copy's file system and reads geometry live.
    /// Runs until both sides did their share. Returns how long the writer waited for the drive's lock at most (the
    /// wait alone: a loaded machine preempting the writer in the middle of its own work does not count)
    struct Waits
    {
        std::chrono::microseconds longest{0};
        int overOneMs = 0;
        int ops = 0;
    };
    Waits Stress(int writerOps, int reads, bool swapDisks, bool withReader = true)
    {
        FDD fdd(_context);
        std::unique_ptr<DiskImage> disk = TrdosDisk(_context);
        fdd.insertDisk(disk.get());

        std::atomic<bool> stop{false};
        std::atomic<int> snapshots{0};
        std::atomic<int> readsDuringWrites{0};
        std::atomic<int> writes{0};
        std::atomic<bool> readerBroken{false};
        std::thread reader([&]() {
            while (!stop.load())
            {
                if (!withReader)
                {
                    snapshots++;
                    readsDuringWrites++;
                    std::this_thread::yield();
                    continue;
                }
                const int before = writes.load();
                std::unique_ptr<DiskImage> copy = fdd.SnapshotDisk();
                if (copy)
                {
                    if (!Consistent(*copy))
                        readerBroken = true;
                    unrealdisk::DiskFs fs(*copy);
                    std::vector<unrealdisk::DiskFsEntry> entries;
                    std::string error;
                    fs.List("", "", true, entries, error);
                }
                fdd.WithDisk([&](const DiskImage* live) {
                    if (live && (live->getCylinders() == 0 || live->getSides() == 0))
                        readerBroken = true;
                });
                snapshots++;
                if (writes.load() != before)
                    readsDuringWrites++;
            }
        });

        // Wait for the reader, then work until both sides did enough
        EXPECT_TRUE(TestWait::For([&]() { return snapshots.load() > 0; })) << "the reader thread never started";
        Waits waits;
        const auto deadline = std::chrono::steady_clock::now() + TestWait::Scaled(TestWait::kDefaultTimeout) * (writerOps > 1000 ? 60 : 5);
        uint32_t step = 0;
        while ((writes.load() < writerOps || readsDuringWrites.load() < reads) &&
               std::chrono::steady_clock::now() < deadline)
        {
            step++;
            DiskImage* image = fdd.getDiskImage();
            const uint8_t cylinder = static_cast<uint8_t>(step % image->getCylinders());
            DiskImage::Track* track = image->getTrackForCylinderAndSide(cylinder, static_cast<uint8_t>(step & 1));
            // Take the lock the layout change takes, timing the wait alone (the change re-enters it: recursive)
            unrealdisk::LayoutMutex* lock = image->layoutMutex();
            const auto start = std::chrono::steady_clock::now();
            lock->lock();
            const auto waited =
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
            waits.longest = std::max(waits.longest, waited);
            if (waited.count() > 1000)
                waits.overOneMs++;
            waits.ops++;
            switch (step % 6)
            {
                case 0:  // WRITE TRACK: a fresh stream, bytes written, the index rebuilt at the end
                    track->resizeRaw(DiskImage::RawTrack::NominalTrackSize(DiskImage::Encoding::MFM,
                                                                           unrealdisk::FdcDataRate::Rate250Kbps));
                    std::fill(track->rawData(), track->rawData() + 100, uint8_t{0x4E});
                    track->reindex();
                    break;
                case 1:  // uPD765 FORMAT
                    track->formatTrack(cylinder, static_cast<uint8_t>(step & 1));
                    break;
                case 2:  // a sector write: bytes only, no lock
                    if (DiskImage::Sector* sector = track->getRawSector(0); sector && sector->hasData)
                        std::fill(sector->data, sector->data + sector->dataSize, static_cast<uint8_t>(step));
                    break;
                case 3:  // WRITE TRACK past the last cylinder
                    image->growCylinders(static_cast<uint8_t>(std::min<int>(image->getCylinders() + 1, unrealdisk::MAX_CYLINDERS)));
                    break;
                case 4:  // a disk change, the previous image freed at once (as MediaManager::ApplySlot)
                    if (swapDisks)
                    {
                        std::unique_ptr<DiskImage> next = TrdosDisk(_context, 80, static_cast<uint8_t>(step));
                        fdd.insertDisk(next.get());
                        disk = std::move(next);
                    }
                    break;
                default:  // an eject and the same disk back
                    if (swapDisks)
                    {
                        fdd.ejectDisk();
                        fdd.insertDisk(disk.get());
                    }
                    break;
            }
            lock->unlock();  // still the old drive's lock after a swap: the FDD keeps one mutex for every disk
            writes++;
            // The emulation between two layout changes (a few microseconds even in the densest WRITE TRACK loop):
            // a writer with no gap at all would keep a reader out, as it goes ahead of readers by design
            for (const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(5);
                 std::chrono::steady_clock::now() < until;)
            {
            }
        }
        stop = true;
        reader.join();

        EXPECT_FALSE(readerBroken.load()) << "a reader saw an inconsistent image";
        EXPECT_GE(writes.load(), writerOps) << "the writer did not finish its share before the deadline";
        // The long tier asks for many overlapping reads and runs to its deadline for them; on a loaded machine the
        // readers, giving way to the writer by design, get fewer. A few are enough to prove the race happened
        if (withReader)
            EXPECT_GE(readsDuringWrites.load(), std::min(reads, 3)) << "no read overlapped the writer: the race did not happen";
        fdd.ejectDisk();
        return waits;
    }
};

/// The copy holds the same streams, the same sector index and the same unsaved-change flags, and is the caller's
TEST_F(FDD_Snapshot_Test, SnapshotIsAnIndependentCopyWithItsDirtyFlags)
{
    FDD fdd(_context);
    std::unique_ptr<DiskImage> disk = TrdosDisk(_context);
    disk->setFilePath("disk.trd");
    disk->markClean();
    DiskImage::Track* track = disk->getTrackForCylinderAndSide(3, 1);
    DiskImage::Sector* sector = track->getSector(4);
    sector->data[0] = 0xA5;
    sector->dirty = true;
    track->markDirty();
    fdd.insertDisk(disk.get());

    std::unique_ptr<DiskImage> copy = fdd.SnapshotDisk();
    ASSERT_NE(copy, nullptr);
    ASSERT_NE(copy.get(), disk.get());
    EXPECT_EQ(copy->getCylinders(), disk->getCylinders());
    EXPECT_EQ(copy->getSides(), disk->getSides());
    EXPECT_EQ(copy->getFilePath(), "disk.trd");
    EXPECT_TRUE(copy->isDirty());
    DiskImage::Track* copied = copy->getTrackForCylinderAndSide(3, 1);
    ASSERT_EQ(copied->rawSize(), track->rawSize());
    EXPECT_EQ(0, std::memcmp(copied->rawData(), track->rawData(), track->rawSize()));
    ASSERT_EQ(copied->sectorCount(), track->sectorCount());
    EXPECT_TRUE(copied->getSector(4)->dirty);
    EXPECT_FALSE(copied->getSector(5)->dirty);
    EXPECT_EQ(copy->layoutMutex(), nullptr) << "the copy is in no drive";

    copied->getSector(4)->data[0] = 0x5A;
    EXPECT_EQ(sector->data[0], 0xA5) << "the copy shares no memory with the drive's image";
    fdd.ejectDisk();
}

TEST_F(FDD_Snapshot_Test, EmptyDriveGivesNoSnapshot)
{
    FDD fdd(_context);
    EXPECT_EQ(fdd.SnapshotDisk(), nullptr);
    EXPECT_TRUE(fdd.WithDisk([](const DiskImage* image) { return image == nullptr; }));
}

/// The drive lends its lock only while the image is in it, and keeps nothing of an image once it is out: the
/// owner frees it at once, while a snapshot taken before lives on
TEST_F(FDD_Snapshot_Test, ImageOutOfTheDriveIsFreeAndOwnedOnlyByItsMedium)
{
    FDD fdd(_context);
    std::unique_ptr<DiskImage> first = TrdosDisk(_context, 80, 0x11);
    std::unique_ptr<DiskImage> second = TrdosDisk(_context, 40, 0x22);

    fdd.insertDisk(first.get());
    EXPECT_NE(first->layoutMutex(), nullptr);
    std::unique_ptr<DiskImage> before = fdd.SnapshotDisk();

    fdd.insertDisk(second.get());
    EXPECT_EQ(first->layoutMutex(), nullptr) << "the replaced image still takes the drive's lock";
    EXPECT_NE(second->layoutMutex(), nullptr);
    first.reset();  // as MediaManager::ApplySlot retires it right after the insert

    std::unique_ptr<DiskImage> after = fdd.SnapshotDisk();
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->getCylinders(), 40);
    ASSERT_NE(before, nullptr);
    EXPECT_EQ(before->getCylinders(), 80) << "an earlier snapshot is its own copy";
    EXPECT_TRUE(Consistent(*before));

    fdd.ejectDisk();
    EXPECT_EQ(second->layoutMutex(), nullptr);
    EXPECT_EQ(fdd.getDiskImage(), nullptr);
    EXPECT_EQ(fdd.SnapshotDisk(), nullptr);
    second.reset();
}

/// A disk change waits for a read that holds the drive's image, then goes ahead: the image is never freed under it
TEST_F(FDD_Snapshot_Test, DiskChangeWaitsForARunningReadThenCompletes)
{
    FDD fdd(_context);
    std::unique_ptr<DiskImage> disk = TrdosDisk(_context);
    fdd.insertDisk(disk.get());

    std::atomic<bool> reading{false};
    std::atomic<bool> release{false};
    std::atomic<bool> ejected{false};
    std::atomic<bool> sawItWhole{false};
    std::thread reader([&]() {
        fdd.WithDisk([&](const DiskImage* image) {
            reading = true;
            TestWait::For([&]() { return release.load(); });
            sawItWhole = image && !ejected.load() && image->getCylinders() == 80;
        });
    });
    ASSERT_TRUE(TestWait::For([&]() { return reading.load(); }));

    std::thread changer([&]() {
        fdd.ejectDisk();
        ejected = true;
    });
    // The eject cannot finish while the read holds the image
    EXPECT_FALSE(TestWait::For([&]() { return ejected.load(); }, std::chrono::milliseconds(20)));
    release = true;
    reader.join();
    changer.join();

    EXPECT_TRUE(sawItWhole.load());
    EXPECT_TRUE(ejected.load());
    EXPECT_EQ(fdd.getDiskImage(), nullptr);
    disk.reset();  // freed right after the eject: no reader holds it
}

/// Reshaping tracks, writing bytes and swapping disks on the emulation thread while another thread reads
TEST_F(FDD_Snapshot_Test, ReadersBesideTrackRewritesAndDiskSwaps)
{
    Stress(40, 3, true);
}

/// Long tier: the same, much longer, with the writer's longest wait for the layout lock reported. The emulation
/// thread may wait for a running snapshot's one-track memory copy only
TEST_F(FDD_Snapshot_Test, LongReadersBesideTrackRewritesAndDiskSwaps)
{
    if (!TestTier::Long())
        GTEST_SKIP() << "long tier (UNREAL_LONG_TESTS=1)";

    const Waits withSwaps = Stress(20000, 300, true);
    const Waits layoutOnly = Stress(20000, 300, false);
    const Waits noReader = Stress(20000, 300, false, false);
    std::fprintf(stderr, "the same lock with no reader at all: longest %lld us, %d of %d over 1 ms\n",
                 static_cast<long long>(noReader.longest.count()), noReader.overOneMs, noReader.ops);
    std::fprintf(stderr,
                 "writer's wait for the reader's lock: longest %lld us, %d of %d over 1 ms (with disk swaps); "
                 "longest %lld us, %d of %d over 1 ms (layout only)\n",
                 static_cast<long long>(withSwaps.longest.count()), withSwaps.overOneMs, withSwaps.ops,
                 static_cast<long long>(layoutOnly.longest.count()), layoutOnly.overOneMs, layoutOnly.ops);
    // The waits are reported, not asserted: on a machine loaded with tens of runnable threads per core the OS
    // preempts the writer itself for tens of milliseconds, which no lock design can rule out
}

