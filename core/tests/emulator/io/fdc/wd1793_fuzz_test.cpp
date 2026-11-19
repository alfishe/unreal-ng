#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/wd1793.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "unrealdisk/trdosfs.h"
#include "_helpers/testtier.h"
#include "_helpers/wd1793testcut.h"
#include "common/modulelogger.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "loaders/disk/loader_trd.h"

/// WD1793 fuzzing (long tier: UNREAL_LONG_TESTS=1; seeds, replay and tracing: docs/emulator/environment-variables.md).
///
/// Drives the controller only through what a Z80 program can do - the Beta 128 ports #1F / #3F / #5F / #7F / #FF -
/// with random commands, register values, drive / side / density selections and DRQ service (WRITE TRACK sources
/// biased towards the control bytes F5 / F6 / F7 / FE / FB), the time between accesses random from one byte cell to
/// several revolutions. Mixed in: TTD state + transfer context round trips in the middle of a command, a disk swap in
/// any drive with the old image freed (as the media manager frees it), host catalog reads and writes. Every 64 steps
/// every image must be consistent: each indexed ID and data field inside its own track stream, at most 86 cylinders.
/// Run it in an ASan build (tools/sanitize/configure.sh asan): a stray access then stops the run with a report.
/// Born from the ZF6 B2 mastering crash (a Windows process that vanished without a dump during a boot-disk build);
/// its first runs found a running format still pointing into a freed image and stale sector pointers in TTD saves.

namespace
{
constexpr uint16_t PORT_CMD = 0x1F;
constexpr uint16_t PORT_TRACK = 0x3F;
constexpr uint16_t PORT_SECTOR = 0x5F;
constexpr uint16_t PORT_DATA = 0x7F;
constexpr uint16_t PORT_BETA = 0xFF;

/// A TR-DOS image with two files, cylinders as given (83 = step8_PL.fdi of the B2 report)
std::unique_ptr<DiskImage> MakeTrdosImage(EmulatorContext* context, uint8_t cylinders, uint8_t sides)
{
    auto image = std::make_unique<DiskImage>(cylinders, sides);
    LoaderTRD(context, "").format(image.get());
    unrealdisk::TrdosFileSystem fs(*image);
    std::string error;
    unrealdisk::TrdosFileSystem::WriteOptions options;
    fs.Write("MAKTAB", 'C', std::vector<uint8_t>(3 * 256, 0x11), options, error);
    fs.Write("boot", 'B', std::vector<uint8_t>(5 * 256, 0x22), options, error);
    return image;
}
/// The action of a fuzz step from a roll 0..99 (weights: the upper bounds below; the rest is the last action)
int Action(uint32_t roll)
{
    static const uint32_t upper[] = {9, 14, 19, 24, 27, 59, 64, 89, 93, 95, 97};
    int action = 0;
    while (action < static_cast<int>(sizeof(upper) / sizeof(upper[0])) && roll > upper[action]) action++;
    return action;
}
}  // namespace

class WD1793_Fuzz_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    CoreCUT* _core = nullptr;
    Z80* _z80 = nullptr;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _context->pModuleLogger->TurnOffLoggingForAll();
        _core = new CoreCUT(_context);
        _z80 = new Z80(_context);
        _core->_z80 = _z80;
        _context->pCore = _core;
    }

    void TearDown() override
    {
        _core->_z80 = nullptr;
        delete _z80;
        _context->pCore = nullptr;
        delete _core;
        delete _context;
    }

    /// Every sector index entry points inside its own track stream; the image stays within the drive's 86 cylinders
    static ::testing::AssertionResult ImageConsistent(DiskImage& image)
    {
        if (image.getCylinders() > MAX_CYLINDERS)
            return ::testing::AssertionFailure() << "image grew to " << int(image.getCylinders()) << " cylinders";
        for (uint8_t c = 0; c < image.getCylinders(); c++)
            for (uint8_t s = 0; s < image.getSides(); s++)
            {
                DiskImage::Track* track = image.getTrackForCylinderAndSide(c, s);
                if (!track)
                    continue;
                const uint8_t* begin = track->rawData();
                const uint8_t* end = begin + track->rawSize();
                for (const DiskImage::Sector& sector : track->sectors())
                {
                    const uint8_t* id = reinterpret_cast<const uint8_t*>(sector.id);
                    if (id < begin || id + 7 > end)
                        return ::testing::AssertionFailure() << "ID field outside track " << int(c) << "/" << int(s);
                    if (sector.hasData && (sector.data < begin || sector.data + sector.dataSize + 2 > end))
                        return ::testing::AssertionFailure() << "data field outside track " << int(c) << "/" << int(s);
                }
            }
        return ::testing::AssertionSuccess();
    }

    /// One fuzz run: steps of random port traffic from a fixed seed (reproducible: the seed is in the failure)
    void RunSeed(uint32_t seed, size_t steps)
    {
        std::mt19937 rng(seed);
        // UNREAL_FDC_FUZZ_TRACE=1: every step on stderr (a sanitizer report then follows the step that caused it)
        static const bool trace = std::getenv("UNREAL_FDC_FUZZ_TRACE") != nullptr;
        if (trace)
            fprintf(stderr, "seed %u\n", seed);
        auto pick = [&rng](uint32_t n) { return static_cast<uint32_t>(rng() % n); };

        std::vector<std::unique_ptr<DiskImage>> images;
        images.push_back(MakeTrdosImage(_context, 83, 2));
        images.push_back(MakeTrdosImage(_context, 80, 2));
        images.push_back(MakeTrdosImage(_context, 40, 1));

        WD1793TestCUT fdc(_context);
        fdc._time = 1000;
        for (size_t d = 0; d < 3; d++)
            _context->coreState.diskDrives[d]->insertDisk(images[d].get());

        // Beta 128: reset released, drive A, head loaded
        fdc.portDeviceOutMethod(PORT_BETA, 0x3C);

        std::vector<uint8_t> ttdBlob(fdc.TTDStateSize());
        std::vector<uint8_t> transfer(WD1793::kTransferContextSize);

        // The byte a program feeds on DRQ: mostly the format language of a WRITE TRACK, sometimes any byte
        auto feedByte = [&]() -> uint8_t {
            static const uint8_t language[] = {0x4E, 0x00, 0xF5, 0xF5, 0xF5, 0xFE, 0xF7, 0xF7, 0xFB, 0xF8, 0xF6, 0xFC, 0xE5};
            return pick(4) ? language[pick(sizeof(language))] : static_cast<uint8_t>(rng());
        };

        for (size_t step = 0; step < steps; step++)
        {
            SCOPED_TRACE("seed " + std::to_string(seed) + " step " + std::to_string(step));

            const int action = Action(pick(100));
            if (trace)
                fprintf(stderr, "step %zu action %d state %d cmd %02X sector %p\n", step, action, int(fdc._state),
                        fdc._commandRegister, static_cast<const void*>(fdc._currentSector));
            switch (action)
            {
                case 0:  // A command: any byte (Type I..IV, every flag)
                    fdc.portDeviceOutMethod(PORT_CMD, static_cast<uint8_t>(rng()));
                    break;
                case 1:  // The commands that matter for a format / read-back cycle, flags random
                {
                    static const uint8_t base[] = {0x00, 0x10, 0x40, 0x60, 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xE0, 0xF0, 0xD0};
                    fdc.portDeviceOutMethod(PORT_CMD, static_cast<uint8_t>(base[pick(sizeof(base))] | (rng() & 0x0F)));
                    break;
                }
                case 2:
                    fdc.portDeviceOutMethod(PORT_TRACK, pick(3) ? static_cast<uint8_t>(pick(90)) : static_cast<uint8_t>(rng()));
                    break;
                case 3:
                    fdc.portDeviceOutMethod(PORT_SECTOR, pick(3) ? static_cast<uint8_t>(1 + pick(16)) : static_cast<uint8_t>(rng()));
                    break;
                case 4:  // Drive, side, density; rarely a reset pulse (bit 2 low)
                {
                    uint8_t beta = static_cast<uint8_t>((rng() & 0x53) | 0x08 | 0x04 | 0x20);
                    if (pick(20) == 0)
                        beta &= ~0x04;
                    fdc.portDeviceOutMethod(PORT_BETA, beta);
                    break;
                }
                case 5:  // DRQ service as INI / OUTI loops do it: read or write the data port
                    if (fdc.portDeviceInMethod(PORT_BETA) & 0x40)
                    {
                        if (fdc._lastDecodedCmd == WD1793::WD_CMD_WRITE_TRACK || fdc._lastDecodedCmd == WD1793::WD_CMD_WRITE_SECTOR)
                            fdc.portDeviceOutMethod(PORT_DATA, feedByte());
                        else
                            fdc.portDeviceInMethod(PORT_DATA);
                    }
                    fdc._time += 1 + pick(60);
                    break;
                case 6:
                    fdc.portDeviceInMethod(static_cast<uint16_t>(0x1F + 0x20 * pick(4)));
                    break;
                case 7:  // Time passes: one byte cell .. 3 revolutions
                {
                    const size_t span = pick(4) ? 1 + pick(400) : 1 + pick(3 * 70000);
                    for (size_t t = 0; t < span; t += 50)
                    {
                        fdc._time += 50;
                        fdc.process();
                    }
                    break;
                }
                case 8:  // TTD: save, run on, restore in the middle of whatever runs
                {
                    fdc.TTDSaveState(ttdBlob.data());
                    fdc.SaveTransferContext(transfer.data());
                    const size_t span = pick(3000);
                    for (size_t t = 0; t < span; t += 50)
                    {
                        fdc._time += 50;
                        fdc.process();
                    }
                    fdc.TTDLoadState(ttdBlob.data());
                    fdc.LoadTransferContext(transfer.data());
                    break;
                }
                case 9:  // A disk swap in any drive, selected or not: the old image is freed as the emulator frees it
                {
                    const size_t d = pick(3);
                    auto fresh = MakeTrdosImage(_context, static_cast<uint8_t>(pick(2) ? 83 : 40 + pick(47)), static_cast<uint8_t>(1 + pick(2)));
                    _context->coreState.diskDrives[d]->insertDisk(fresh.get());
                    images[d] = std::move(fresh);
                    break;
                }
                case 10:  // Host side: list and read every file (inspect / disk catalog)
                {
                    DiskImage* image = _context->coreState.diskDrives[pick(3)]->getDiskImage();
                    if (image)
                    {
                        unrealdisk::TrdosFileSystem fs(*image);
                        std::vector<unrealdisk::TrdosFile> files;
                        std::string error;
                        if (fs.List(files, error))
                            for (const unrealdisk::TrdosFile& f : files)
                            {
                                std::vector<uint8_t> data;
                                fs.Read(f, data, nullptr, error);
                            }
                    }
                    break;
                }
                default:  // Host side: a file written into the image under a running command
                {
                    DiskImage* image = _context->coreState.diskDrives[pick(3)]->getDiskImage();
                    if (image)
                    {
                        unrealdisk::TrdosFileSystem fs(*image);
                        std::string error;
                        unrealdisk::TrdosFileSystem::WriteOptions options;
                        fs.Write("F" + std::to_string(pick(100)), 'C', std::vector<uint8_t>(1 + pick(2000), 0x33), options, error);
                    }
                    break;
                }
            }

            if (step % 64 == 0 || step + 1 == steps)
            {
                for (size_t d = 0; d < 3; d++)
                {
                    DiskImage* image = _context->coreState.diskDrives[d]->getDiskImage();
                    if (image)
                        ASSERT_TRUE(ImageConsistent(*image)) << "drive " << d;
                }
            }
        }
    }
};

/// Random port traffic over many seeds; a crash or an inconsistent image names its seed and step
TEST_F(WD1793_Fuzz_Test, PortTrafficKeepsImagesConsistent)
{
    if (!TestTier::Long())
        GTEST_SKIP() << "long tier: set UNREAL_LONG_TESTS=1";

    const char* seedsEnv = std::getenv("UNREAL_FDC_FUZZ_SEEDS");
    const uint32_t seeds = seedsEnv ? static_cast<uint32_t>(std::strtoul(seedsEnv, nullptr, 10)) : 200;
    // UNREAL_FDC_FUZZ_FIRST: the first seed (replay one: FIRST=n, SEEDS=n)
    const char* firstEnv = std::getenv("UNREAL_FDC_FUZZ_FIRST");
    const uint32_t first = firstEnv ? static_cast<uint32_t>(std::strtoul(firstEnv, nullptr, 10)) : 1;
    for (uint32_t seed = first; seed <= seeds; seed++)
    {
        RunSeed(seed, 20000);
        if (HasFatalFailure())
            return;
    }
}
