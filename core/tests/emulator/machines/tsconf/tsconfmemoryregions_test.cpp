// TS-Conf CRAM and SFILE as device memory regions (tsconfmemoryregions.h; debugger additions tdd §2): listed by
// name, written through the decoder's CommitTableWord like an FM-window write, kept by TTD.

#include "tsconffixture.h"

#include <memory>
#include <string>
#include <vector>

#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/devicememory.h"
#include "emulator/platforms/tsconf/tsconfstate.h"

class TsConfMemoryRegions_Test : public TsConfFixture
{
protected:
    std::vector<uint8_t> Read(const char* region, uint32_t offset, uint32_t length)
    {
        std::vector<uint8_t> bytes;
        std::string error;
        EXPECT_TRUE(DeviceMemory::Read(_context, region, offset, length, bytes, error)) << error;
        return bytes;
    }
    void Write(const char* region, uint32_t offset, const std::vector<uint8_t>& bytes)
    {
        std::string error;
        ASSERT_TRUE(DeviceMemory::Write(_context, region, offset, bytes, "test", error)) << error;
    }
};

TEST_F(TsConfMemoryRegions_Test, BothTablesAreListed)
{
    const std::vector<IDeviceMemoryRegion*> regions = DeviceMemory::Regions(_context);
    ASSERT_EQ(regions.size(), 4u) << "cram, sfile; the ZX-Evo clock's cmos and eeprom";
    EXPECT_STREQ(regions[0]->Name(), "cram");
    EXPECT_STREQ(regions[1]->Name(), "sfile");
    EXPECT_STREQ(regions[2]->Name(), "cmos");
    EXPECT_STREQ(regions[3]->Name(), "eeprom");
    EXPECT_EQ(regions[0]->Size(), 512u);
    EXPECT_EQ(regions[1]->Size(), 512u);
    EXPECT_EQ(DeviceMemory::Find(_context, "CRAM"), regions[0]);
    EXPECT_EQ(DeviceMemory::Regions(_context)[0], regions[0]) << "the same objects on every call";
}

TEST_F(TsConfMemoryRegions_Test, CramWriteChangesTheColorAndThePalette)
{
    TsConfState& ts = _decoder->GetState();
    ts.cram[1] = 0x7C00;  // red
    const uint32_t version = _decoder->CramVersion();
    EXPECT_EQ(Read("cram", 2, 2), (std::vector<uint8_t>{0x00, 0x7C})) << "word 1 at offset 2, low byte first";
    EXPECT_EQ(_decoder->CramVersion(), version) << "a read changes nothing";

    Write("cram", 2, {0x1F, 0x00});  // pure blue
    EXPECT_EQ(ts.cram[1], 0x001F);
    EXPECT_GT(_decoder->CramVersion(), version) << "the screen must rebuild its palette";

    Write("cram", 3, {0x80});  // one byte: the high half only (VDAC flag)
    EXPECT_EQ(ts.cram[1], 0x801F);
}

TEST_F(TsConfMemoryRegions_Test, SfileWriteChangesTheSpriteWord)
{
    TsConfState& ts = _decoder->GetState();
    Write("sfile", 3 * 2 * 4, {0x34, 0x12});  // sprite 4, word 0
    EXPECT_EQ(ts.sfile[12], 0x1234);
    EXPECT_EQ(Read("sfile", 24, 2), (std::vector<uint8_t>{0x34, 0x12}));
}

// The region's offsets are the FM window's, and a region write leaves a program's half-done FM write alone
TEST_F(TsConfMemoryRegions_Test, SameWordAsTheFmWindow)
{
    TsConfState& ts = _decoder->GetState();
    Reg(TsConfReg::FMaps, 0x14);  // FM window on at #4000
    Poke(0x4002, 0x1F);           // color 1, even byte: stashed
    Write("cram", 4, {0xE0, 0x03});  // color 2 by region: green
    Poke(0x4003, 0x00);           // the odd byte commits {#00, stash #1F}
    EXPECT_EQ(ts.cram[1], 0x001F) << "the region write disturbed the FM stash";
    EXPECT_EQ(ts.cram[2], 0x03E0);
    EXPECT_EQ(Read("cram", 2, 4), (std::vector<uint8_t>{0x1F, 0x00, 0xE0, 0x03}));
}

TEST(TsConfMemoryRegionsTtd_Test, RecordingKeepsARegionWrite)
{
    std::shared_ptr<Emulator> emulator =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel("tsconf-regions-test", "TSL", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* decoder = dynamic_cast<PortDecoder_TSConf*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;
    emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    ASSERT_TRUE(ttd->StartRecording());
    emulator->RunNFrames(1, true);
    const size_t before = ttd->GetCheckpointCount() - 1;
    const uint16_t original = decoder->GetState().cram[200];

    std::string error;
    ASSERT_TRUE(DeviceMemory::Write(context, "cram", 400, {0x55, 0x15}, "test", error)) << error;
    emulator->RunNFrames(1, true);
    const size_t after = ttd->GetCheckpointCount() - 1;
    ttd->StopRecording();

    const auto markers = ttd->GetExternalEvents().SnapshotEvents();
    ASSERT_EQ(markers.size(), 1u);
    EXPECT_EQ(markers.front().kind, ttd::TTDExternalEventKind::DebuggerEdit);
    ASSERT_TRUE(ttd->RestoreCheckpointForTesting(before));
    EXPECT_EQ(decoder->GetState().cram[200], original);
    ASSERT_TRUE(ttd->RestoreCheckpointForTesting(after));
    EXPECT_EQ(decoder->GetState().cram[200], 0x1555) << "the checkpoint after the edit lost the color";
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}
