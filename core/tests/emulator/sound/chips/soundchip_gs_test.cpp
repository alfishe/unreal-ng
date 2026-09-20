// General Sound card unit + integration tests (GS design §12.1).
//
// The chip is a coprocessor subsystem, so protocol behaviour is driven two
// ways: directly through the ZX-side host ports / automation actions, and
// through real firmware execution - a tiny hand-assembled program is burned
// into a scratch ROM so the internal z80ex drives the GS-side ports
// (#00 MPAG, #06-#09 volume latches) exactly like production firmware.

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/soundchip_gs.h"
#include "emulator/sound/soundmanager.h"

namespace
{

// Little-endian decode of the timing counters the chip serialises into the
// TTD blob (soundchip_gs.h layout: [13..20] gsCyclesAbs, [21..22] intQuantum,
// [23] nmiPending)
int64_t decodeGsCyclesAbs(const std::vector<uint8_t>& blob)
{
    uint64_t raw = 0;
    for (int i = 7; i >= 0; i--)
        raw = (raw << 8) | blob[13 + i];
    return static_cast<int64_t>(raw);
}

int decodeIntQuantum(const std::vector<uint8_t>& blob)
{
    return blob[21] | (blob[22] << 8);
}

// Burn a program into a 32 KB ROM image in the per-process scratch dir
std::string writeRom(const char* leafName, const uint8_t* program, size_t programSize)
{
    std::vector<uint8_t> image(SoundChip_GeneralSound::ROM_SIZE, 0x00); // 0x00 = NOP padding
    memcpy(image.data(), program, programSize);
    std::string path = TestPathHelper::GetUniqueTestScratchPath(leafName);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(image.data()), image.size());
    return path;
}

} // namespace

class SoundChip_GeneralSound_Test : public ::testing::Test
{
protected:
    EmulatorContext* ctx = nullptr;
    std::unique_ptr<SoundChip_GeneralSound> chip;

    void SetUp() override
    {
        ctx = new EmulatorContext(LoggerLevel::LogError);
        ctx->config.sound.gs_vol = 8000;
        // Pentagon frame geometry: 3.5 MHz base clock, 12 MHz GS clock ->
        // llround(69888 * 12 / 3.5) = 239602 GS cycles per frame
        ctx->config.frame = 69888;
        ctx->config.frame_duration_us = 19968;
        // Real speed (the boot-time state the core maintains; a bare context
        // leaves the multiplier uninitialized and the GS frame length would
        // collapse to zero)
        ctx->emulatorState.current_z80_frequency_multiplier = 1;
        ctx->emulatorState.hw_turbo_shift_applied = 0;
        chip = std::make_unique<SoundChip_GeneralSound>(ctx, 512);
    }

    void TearDown() override
    {
        chip.reset();
        delete ctx;
    }

    void runOneFrame()
    {
        chip->handleFrameStart();
        chip->handleFrameEnd();
    }

    std::vector<uint8_t> saveState()
    {
        std::vector<uint8_t> blob(chip->TTDStateSize());
        chip->TTDSaveState(blob.data());
        return blob;
    }
};

/// region <Host port protocol>

TEST_F(SoundChip_GeneralSound_Test, PortDecode_B3_IsData)
{
    chip->portDeviceOutMethod(SoundChip_GeneralSound::PORT_DATA, 0x42);

    EXPECT_EQ(chip->getDataFromHost(), 0x42);
    EXPECT_NE(chip->getStatusRaw() & 0x80, 0) << "OUT #B3 must set the data-pending flag (bit7)";
}

TEST_F(SoundChip_GeneralSound_Test, PortDecode_BB_IsCommand)
{
    chip->portDeviceOutMethod(SoundChip_GeneralSound::PORT_COMMAND, 0x2A);

    EXPECT_EQ(chip->getCommandFromHost(), 0x2A);
    EXPECT_NE(chip->getStatusRaw() & 0x01, 0) << "OUT #BB must set the command-pending flag (bit0)";
}

TEST_F(SoundChip_GeneralSound_Test, PortDecode_LowByteOnlyMirrors)
{
    // The hardware decodes the low byte only - any A15-A8 mirror of #B3/#BB
    // reaches the same latch
    chip->portDeviceOutMethod(0xF1B3, 0x11);
    chip->portDeviceOutMethod(0x02BB, 0x22);

    EXPECT_EQ(chip->getDataFromHost(), 0x11);
    EXPECT_EQ(chip->getCommandFromHost(), 0x22);
}

TEST_F(SoundChip_GeneralSound_Test, StatusBit0_SetOnCommand)
{
    EXPECT_EQ(chip->getStatusRaw() & 0x01, 0);
    chip->sendCommand(0x01);
    EXPECT_NE(chip->getStatusRaw() & 0x01, 0);
    EXPECT_EQ(chip->getCommandFromHost(), 0x01);
}

TEST_F(SoundChip_GeneralSound_Test, StatusBit7_SetOnDataWrite)
{
    EXPECT_EQ(chip->getStatusRaw() & 0x80, 0);
    chip->sendData(0x99);
    EXPECT_NE(chip->getStatusRaw() & 0x80, 0);
    EXPECT_EQ(chip->getDataFromHost(), 0x99);
}

TEST_F(SoundChip_GeneralSound_Test, StatusBit7_ClearOnDataRead)
{
    chip->sendData(0x99);
    ASSERT_NE(chip->getStatusRaw() & 0x80, 0);

    (void)chip->readData();
    EXPECT_EQ(chip->getStatusRaw() & 0x80, 0) << "IN #B3 must clear the data-pending flag";
}

TEST_F(SoundChip_GeneralSound_Test, StatusRead_PullUpBits)
{
    // Bits 1-6 of the #BB status read float high (pull-ups)
    chip->sendCommand(0x5A); // sets bit0 only
    const uint8_t status = chip->readStatus();

    EXPECT_EQ(status & 0x7E, 0x7E);
    EXPECT_NE(status & 0x01, 0);
}

TEST_F(SoundChip_GeneralSound_Test, DataToHost_ReturnedByPortB3Read)
{
    // Stage a GS->ZX byte through the TTD interface (the only public producer
    // is the GS CPU itself - the firmware writes port 0x03)
    std::vector<uint8_t> blob = saveState();
    blob[2] = 0x42;  // dataToHost
    blob[0] = 0x80;  // status: data pending
    chip->TTDLoadState(blob.data());

    EXPECT_EQ(chip->portDeviceInMethod(SoundChip_GeneralSound::PORT_DATA), 0x42);
    EXPECT_EQ(chip->getStatusRaw() & 0x80, 0) << "IN #B3 consumes the pending byte";
}

TEST_F(SoundChip_GeneralSound_Test, Port33_Bit7ResetsCardButKeepsLatches)
{
    // Firmware: LD A,0x3F; OUT (0x06),A; HALT - latches volume, then halts
    const uint8_t program[] = {0x3E, 0x3F, 0xD3, 0x06, 0x76};
    chip->loadROM(writeRom("gs-volume.rom", program, sizeof(program)));
    runOneFrame();
    ASSERT_TRUE(chip->isCPUHalted());
    ASSERT_EQ(chip->getChannelVolume(0), 0x3F);
    ASSERT_NE(chip->getCPUReg(regPC), 0);

    chip->portDeviceOutMethod(SoundChip_GeneralSound::PORT_CONTROL, 0x80);

    EXPECT_EQ(chip->getCPUReg(regPC), 0) << "#33 bit7 restarts the coprocessor";
    EXPECT_EQ(chip->getChannelVolume(0), 0x3F) << "volume latches are external flip-flops - they survive";
}

TEST_F(SoundChip_GeneralSound_Test, Port33_Bit6LatchesNmi)
{
    chip->portDeviceOutMethod(SoundChip_GeneralSound::PORT_CONTROL, 0x40);

    // NMI is latched until the coprocessor accepts it at an instruction
    // boundary - observable via the TTD blob (byte 23)
    const std::vector<uint8_t> blob = saveState();
    EXPECT_EQ(blob[23], 1);
}

/// endregion </Host port protocol>

/// region <GS-side ports via real firmware execution>

TEST_F(SoundChip_GeneralSound_Test, VolumeRange_0to63)
{
    // Firmware: LD A,0x3F; OUT (0x06),A; LD A,0x55; OUT (0x07),A; HALT
    // Channel 2 latches 0x55 & 0x3F = 0x15 (6-bit latches, Unreal gsz80.cpp:353)
    const uint8_t program[] = {0x3E, 0x3F, 0xD3, 0x06, 0x3E, 0x55, 0xD3, 0x07, 0x76};
    chip->loadROM(writeRom("gs-volmask.rom", program, sizeof(program)));
    runOneFrame();

    EXPECT_EQ(chip->getChannelVolume(0), 0x3F);
    EXPECT_EQ(chip->getChannelVolume(1), 0x15);
}

TEST_F(SoundChip_GeneralSound_Test, PageSwitch_ROMtoRAM)
{
    // Firmware writes 0xA5 to 0x8000 while MPAG=0 (ROM window - discarded),
    // switches MPAG=1 (RAM pair 0), then writes 0x5A which must land in RAM
    const uint8_t program[] = {
        0x3E, 0xA5,             // LD A,0xA5
        0x32, 0x00, 0x80,       // LD (0x8000),A - discarded, window 2 is ROM
        0x3E, 0x01,             // LD A,1
        0xD3, 0x00,             // OUT (#00),A - MPAG = 1
        0x3E, 0x5A,             // LD A,0x5A
        0x32, 0x00, 0x80,       // LD (0x8000),A - lands in RAM pair 0
        0x76                    // HALT
    };
    chip->loadROM(writeRom("gs-mpag.rom", program, sizeof(program)));
    runOneFrame();

    EXPECT_EQ(chip->getMPAG(), 1);

    // RAM pair 0 maps at 0x8000 after the switch; it is the start of the TTD
    // RAM image (fixed part is 59 bytes)
    const std::vector<uint8_t> blob = saveState();
    ASSERT_EQ(blob.size(), SoundChip_GeneralSound::TTD_FIXED_STATE_SIZE + 512 * 1024);
    EXPECT_EQ(blob[SoundChip_GeneralSound::TTD_FIXED_STATE_SIZE], 0x5A)
        << "write behind MPAG=1 must land in RAM";
}

/// endregion </GS-side ports via real firmware execution>

/// region <Timing>

TEST_F(SoundChip_GeneralSound_Test, InterruptTiming_Every320Cycles)
{
    // Zeroed ROM: the coprocessor free-runs NOPs for one Pentagon frame
    // (239602 GS cycles at 12 MHz). The 37.5 kHz quantum bookkeeping must
    // keep _gsCyclesAbs on exact 320-cycle boundaries and consume the frame
    const double frameGsCycles = std::llround(69888.0 * 12000000.0 / 3500000.0);
    runOneFrame();

    const std::vector<uint8_t> blob = saveState();
    const int64_t gsCyclesAbs = decodeGsCyclesAbs(blob);
    const int quantum = decodeIntQuantum(blob);

    EXPECT_EQ(gsCyclesAbs % SoundChip_GeneralSound::GS_CYCLES_PER_INT, 0);
    EXPECT_LE(static_cast<double>(gsCyclesAbs), frameGsCycles);
    // The loop only stops once total = abs + quantum reaches the target;
    // quantum stays below one quantum + the longest instruction (23 cycles)
    EXPECT_GE(static_cast<double>(gsCyclesAbs), frameGsCycles - 320 - 23);
    EXPECT_GE(quantum, 0);
    EXPECT_LT(quantum, 320 + 23);
}

/// endregion </Timing>

/// region <Reset semantics>

TEST_F(SoundChip_GeneralSound_Test, HostReset_GSResetZeroKeepsMailbox)
{
    // GSReset=0 (default): the card is a separate subsystem - a ZX reset
    // leaves it running (Unreal z80.cpp "if (gsreset) reset_gs()")
    ctx->config.sound.gsreset = 0;
    chip->sendCommand(0x55);
    chip->sendData(0xAA);

    chip->hostReset();

    EXPECT_EQ(chip->getCommandFromHost(), 0x55);
    EXPECT_EQ(chip->getDataFromHost(), 0xAA);
}

TEST_F(SoundChip_GeneralSound_Test, HostReset_GSResetOneResets)
{
    ctx->config.sound.gsreset = 1;
    chip->sendCommand(0x55);
    chip->sendData(0xAA);

    chip->hostReset();

    EXPECT_EQ(chip->getStatusRaw(), 0);
    EXPECT_EQ(chip->getCommandFromHost(), 0);
    EXPECT_EQ(chip->getDataFromHost(), 0);
}

TEST_F(SoundChip_GeneralSound_Test, FullReset_ClearsVolumeLatches)
{
    const uint8_t program[] = {0x3E, 0x3F, 0xD3, 0x06, 0x76};
    chip->loadROM(writeRom("gs-volreset.rom", program, sizeof(program)));
    runOneFrame();
    ASSERT_EQ(chip->getChannelVolume(0), 0x3F);

    chip->reset();

    EXPECT_EQ(chip->getChannelVolume(0), 0);
    EXPECT_EQ(chip->getChannelSample(0), 0x80) << "DAC midpoints return to silence";
    EXPECT_EQ(chip->getCPUReg(regPC), 0);
}

/// endregion </Reset semantics>

/// region <TTD serialization>

TEST_F(SoundChip_GeneralSound_Test, TTDSerialize_RoundTrip)
{
    // Mutate the chip through public interfaces, then verify a second chip
    // reproduces the identical blob (registers + mailbox + RAM image)
    const uint8_t program[] = {
        0x3E, 0x01,             // LD A,1
        0xD3, 0x00,             // OUT (#00),A - MPAG = 1
        0x3E, 0xC3,             // LD A,0xC3
        0x32, 0x34, 0x82,       // LD (0x8234),A - RAM pair 0 + 0x0234
        0x76                    // HALT
    };
    chip->loadROM(writeRom("gs-ttd.rom", program, sizeof(program)));
    runOneFrame();
    chip->sendCommand(0x77);
    chip->sendData(0x33);

    const std::vector<uint8_t> blobA = saveState();

    SoundChip_GeneralSound chipB(ctx, 512);
    chipB.TTDLoadState(blobA.data());
    const std::vector<uint8_t> blobB = [&chipB]() {
        std::vector<uint8_t> blob(chipB.TTDStateSize());
        chipB.TTDSaveState(blob.data());
        return blob;
    }();

    ASSERT_EQ(blobA.size(), blobB.size());
    EXPECT_EQ(0, memcmp(blobA.data(), blobB.data(), blobA.size()));

    // Restored machine state matches the getters
    EXPECT_EQ(chipB.getCommandFromHost(), 0x77);
    EXPECT_EQ(chipB.getDataFromHost(), 0x33);
    EXPECT_EQ(chipB.getMPAG(), 1);
    EXPECT_EQ(chipB.getCPUReg(regPC), chip->getCPUReg(regPC));
    EXPECT_EQ(chipB.isCPUHalted(), chip->isCPUHalted());
}

TEST_F(SoundChip_GeneralSound_Test, TTDStateChanged_DetectsVolume)
{
    const uint64_t hashBefore = chip->TTDHashState();

    // Change one volume latch through a crafted blob (the producers are the
    // GS-side ports and TTD load; both are valid restore paths)
    std::vector<uint8_t> blob = saveState();
    blob[5] = 0x3F; // channelVol[0]
    chip->TTDLoadState(blob.data());

    const uint64_t hashAfter = chip->TTDHashState();
    EXPECT_NE(hashBefore, hashAfter) << "a volume change must alter the TTD hash";

    // Loading the same state again is stable
    chip->TTDLoadState(blob.data());
    EXPECT_EQ(chip->TTDHashState(), hashAfter);
}

TEST_F(SoundChip_GeneralSound_Test, TTDStateSize_MatchesRamConfiguration)
{
    EXPECT_EQ(chip->TTDStateSize(), SoundChip_GeneralSound::TTD_FIXED_STATE_SIZE + 512 * 1024);

    SoundChip_GeneralSound smallChip(ctx, 128);
    EXPECT_EQ(smallChip.TTDStateSize(), SoundChip_GeneralSound::TTD_FIXED_STATE_SIZE + 128 * 1024);
}

/// endregion </TTD serialization>

/// region <Construction>

TEST_F(SoundChip_GeneralSound_Test, RamSize_ClampedToOriginalCardRange)
{
    EXPECT_EQ(chip->getRamSizeKB(), 512u);

    SoundChip_GeneralSound tiny(ctx, 64);    // below 128 KB
    EXPECT_EQ(tiny.getRamSizeKB(), 128u);

    SoundChip_GeneralSound huge(ctx, 4096);  // NeoGS-size config on a GS card
    EXPECT_EQ(huge.getRamSizeKB(), 512u);
}

TEST_F(SoundChip_GeneralSound_Test, RomLoad_ReportsSuccessAndFailure)
{
    EXPECT_FALSE(chip->isROMLoaded()) << "no ROM configured -> zero-filled, not loaded";

    chip->loadROM("/nonexistent/gs.rom");
    EXPECT_FALSE(chip->isROMLoaded());

    const uint8_t program[] = {0x76};
    chip->loadROM(writeRom("gs-halt.rom", program, sizeof(program)));
    EXPECT_TRUE(chip->isROMLoaded());
}

/// endregion </Construction>

/// region <SoundManager integration>

class GeneralSound_SoundManager_Test : public ::testing::Test
{
protected:
    EmulatorContext* ctx = nullptr;
    SoundManager* sm = nullptr;

    void SetUp() override
    {
        ctx = new EmulatorContext(LoggerLevel::LogError);
        ctx->config.sound.gs_vol = 8000;
        ctx->config.sound.gsTypeKind = GSTypeKind::Z80;
        ctx->config.gs_ramsize = 512;
    }

    void TearDown() override
    {
        delete sm;
        delete ctx;
    }
};

TEST_F(GeneralSound_SoundManager_Test, CreatedWhenConfigEnabled)
{
    sm = new SoundManager(ctx);
    sm->reset();

    EXPECT_TRUE(sm->hasGeneralSound());
    ASSERT_NE(sm->getGeneralSound(), nullptr);

    const auto* info = sm->device(AudioSourceType::GeneralSound);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->name, "GS");
    EXPECT_NE(sm->deviceBuffer(AudioSourceType::GeneralSound), nullptr);
}

TEST_F(GeneralSound_SoundManager_Test, NotCreatedWhenConfigDisabled)
{
    ctx->config.sound.gsTypeKind = GSTypeKind::NONE;
    sm = new SoundManager(ctx);
    sm->reset();

    EXPECT_FALSE(sm->hasGeneralSound());
    EXPECT_EQ(sm->getGeneralSound(), nullptr);
    EXPECT_EQ(sm->device(AudioSourceType::GeneralSound), nullptr);
}

TEST_F(GeneralSound_SoundManager_Test, NeoGSTypePlaceholderCreatesNoCard)
{
    // GSType=NGS parses (neogs-tdd.md P2 placeholder: the NeoGS card will
    // extend SoundChip_GeneralSound) but must not create any device yet
    ctx->config.sound.gsTypeKind = GSTypeKind::NGS;
    sm = new SoundManager(ctx);
    sm->reset();

    EXPECT_FALSE(sm->hasGeneralSound());
    EXPECT_EQ(sm->getGeneralSound(), nullptr);
    EXPECT_EQ(sm->device(AudioSourceType::GeneralSound), nullptr);
}

TEST_F(GeneralSound_SoundManager_Test, RamSizeHonouredFromConfig)
{
    ctx->config.gs_ramsize = 256;
    sm = new SoundManager(ctx);
    sm->reset();

    ASSERT_NE(sm->getGeneralSound(), nullptr);
    EXPECT_EQ(sm->getGeneralSound()->getRamSizeKB(), 256u);
}

/// endregion </SoundManager integration>
