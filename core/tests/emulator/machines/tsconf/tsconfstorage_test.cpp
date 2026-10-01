// TS-Conf storage (TSConf implementation-plan phase 6): the SD card's SPI
// ports and slot, the SPI DMA, the Beta-128 gating and the virtual TR-DOS,
// the Nemo IDE stall (hardware-spec §8).

#include "tsconffixture.h"

#include <vector>

#include "_helpers/zcsdtesthelper.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/scratchfolder.h"
#include "debugger/ttd/ide/ttdatachannel.h"
#include "emulator/emulator.h"
#include "emulator/io/ide/ata/atadisk.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/io/spi/spidevice.h"
#include "emulator/media/mediamanager.h"

using zcsdtest::PatternDisk;
using zcsdtest::SdCommand;
using zcsdtest::SdInit;

namespace
{
    /// Answers every byte with the count of bytes it received; remembers them
    struct EchoDevice : SpiDevice
    {
        std::vector<uint8_t> received;
        bool selected = false;
        void select(bool on) override { selected = on; }
        uint8_t exchange(uint8_t mosi) override
        {
            received.push_back(mosi);
            return static_cast<uint8_t>(received.size());
        }
    };
}  // namespace

class TsConfStorage_Test : public TsConfFixture
{
protected:
    uint8_t& Byte(uint32_t physical) { return _memory->RAMBase()[physical]; }
};

/// SPI-1 (hs §8.1): #57 write sends, #57 read returns the previous exchange
/// and sends #FF; #77 reads 0; #77 bit 1 is the card's /CS
TEST_F(TsConfStorage_Test, SPI1_PortSemantics)
{
    EchoDevice device;
    _decoder->GetZController().SetDevice(&device);
    EXPECT_EQ(In(0x0077), 0x00);
    Out(0x0077, 0x00);
    EXPECT_TRUE(device.selected);
    Out(0x0057, 0x12);
    EXPECT_EQ(In(0x0057), 0x01) << "the response of the OUT's exchange";
    EXPECT_EQ(In(0x0057), 0x02) << "the response of the previous IN's #FF";
    ASSERT_EQ(device.received.size(), 3u);
    EXPECT_EQ(device.received[0], 0x12);
    EXPECT_EQ(device.received[1], 0xFF);
    Out(0x0077, 0x02);
    EXPECT_FALSE(device.selected);
    _decoder->GetZController().SetDevice(&_decoder->GetSdCard());
}

/// SD-0 on TS-Conf: the shared card answers the protocol through #77 / #57;
/// AVR register C reports it
TEST_F(TsConfStorage_Test, SD0_ReadsASectorThroughThePorts)
{
    ASSERT_TRUE(_decoder->InsertSdCard(PatternDisk(64), SdCardSpi::WriteMode::Session));
    EvoAvr& avr = _decoder->GetEvoAvr();
    avr.WriteAddress(0x0C);
    EXPECT_EQ(avr.ReadData() & 0x08, 0x08) << "card present";

    Out(0x0077, 0x00);
    ASSERT_TRUE(SdInit(_decoder, 0x0057));
    ASSERT_EQ(SdCommand(_decoder, 0x0057, 17, 5 * 512), 0x00);
    int token = -1;
    for (int i = 0; i < 64 && token < 0; i++)
        if (In(0x0057) == 0xFE)
            token = i;
    ASSERT_GE(token, 0) << "data token";
    for (uint32_t i = 0; i < 512; i++)
    {
        const uint8_t b = In(0x0057);
        if (i == 0 || i == 300 || i == 511)
            EXPECT_EQ(b, static_cast<uint8_t>(5 + (5 * 512 + i) % 7)) << i;
    }
}

/// DMA-14 with the card: CMD17, the token by the CPU, then the SPI DMA
/// (0x02, LEN 0xFF, NUM 0) moves the sector into RAM, low byte first
TEST_F(TsConfStorage_Test, SpiDmaReadsTheSector)
{
    ASSERT_TRUE(_decoder->InsertSdCard(PatternDisk(64), SdCardSpi::WriteMode::Session));
    Out(0x0077, 0x00);
    ASSERT_TRUE(SdInit(_decoder, 0x0057));
    ASSERT_EQ(SdCommand(_decoder, 0x0057, 17, 3 * 512), 0x00);
    bool token = false;
    for (int i = 0; i < 64 && !token; i++)
        token = In(0x0057) == 0xFE;
    ASSERT_TRUE(token);

    // The token came from the last IN's latch; the next exchange is data byte 0
    Reg(TsConfReg::DmaDAl, 0x00);
    Reg(TsConfReg::DmaDAh, 0x00);
    Reg(TsConfReg::DmaDAx, 0x14);  // 0x50000
    Reg(TsConfReg::DmaLen, 0xFF);
    Reg(TsConfReg::DmaNum, 0x00);
    TsConfEngine& engine = _decoder->GetEngine();
    engine.OnMachineFrameRollover(TsConfEngine::kFrameTacts);
    Reg(TsConfReg::DmaCtrl, 0x02);
    for (uint32_t line = 1; line < 320 && _decoder->GetDma().Busy(); line++)
        engine.CatchUp(line * TsConfEngine::kLineTacts);
    ASSERT_FALSE(_decoder->GetDma().Busy());
    for (uint32_t i : {0u, 1u, 300u, 511u})
        EXPECT_EQ(Byte(0x50000 + i), static_cast<uint8_t>(3 + (3 * 512 + i) % 7)) << i;
}

/// VDOS-1 (hs §8.2): a trapped access to a virtual drive swaps RAM page #FF
/// into window 0 at the next M1; a VG93 register access inside vdos ends it;
/// #FF inside vdos only changes the drive bits
TEST_F(TsConfStorage_Test, VDOS1_VirtualDriveSwap)
{
    TsConfState& ts = _decoder->GetState();
    Reg(TsConfReg::FddVirt, 0x02);  // drive B is virtual
    ts.dos = 1;
    _decoder->ApplyState();

    Out(0x00FF, 0x01);              // select B (A latched before: no trap)
    EXPECT_EQ(ts.vgDrive, 1);
    EXPECT_EQ(ts.preVdos, 0);

    In(0x001F);                     // B is virtual: trapped
    EXPECT_EQ(ts.preVdos, 1);
    EXPECT_EQ(ts.vdos, 0) << "only at the next M1";
    _decoder->BeforeMachineM1(0x3D30);
    EXPECT_EQ(ts.vdos, 1);
    EXPECT_TRUE(IsRam(0x0000));
    EXPECT_EQ(Tag(0x0000), 0xFF);
    Poke(0x0100, 0x77);
    EXPECT_EQ(Ram(0xFF, 0x0100), 0x77) << "writable regardless of W0_WE";
    _decoder->BeforeMachineM1(0x5000);
    EXPECT_EQ(ts.dos, 1) << "no DOS exit while vdos";

    Out(0x00FF, 0x00);              // inside vdos: only the drive bits
    EXPECT_EQ(ts.vgDrive, 0);
    EXPECT_EQ(ts.vdos, 1);
    In(0x003F);                     // a VG93 register: vdos ends at once
    EXPECT_EQ(ts.vdos, 0);
    EXPECT_FALSE(IsRam(0x0000));
}

/// VDOS-1: the VG93 is not selected for a virtual drive
TEST_F(TsConfStorage_Test, VDOS1_VirtualDriveHidesTheController)
{
    TsConfState& ts = _decoder->GetState();
    Reg(TsConfReg::FddVirt, 0x01);  // drive A virtual
    Reg(TsConfReg::FddVirt, 0x81);  // + VG_OPEN: ports reachable outside DOS
    EXPECT_EQ(In(0x001F), 0xFF) << "the unselected chip floats";
    EXPECT_EQ(ts.preVdos, 0) << "no trap outside DOS";
}

/// VDOS-2 (hs §9): the CMOS answers inside vdos, not from the TR-DOS ROM
TEST_F(TsConfStorage_Test, VDOS2_CmosInsideVdos)
{
    TsConfState& ts = _decoder->GetState();
    Out(0xEFF7, 0x80);
    Out(0xDFF7, 0x0E);
    Out(0xBFF7, 0x5A);
    ts.dos = 1;
    EXPECT_EQ(In(0xBFF7), 0xFF);
    ts.vdos = 1;
    Out(0xDFF7, 0x0E);
    EXPECT_EQ(In(0xBFF7), 0x5A);
}

/// IDE-4 (hs §8.3): with [HDD] IdeStall=1 a bus cycle to the drive costs
/// +1 / +2 / +3 T at 3.5 / 7 / 14 MHz; latch accesses and IdeStall=0 cost nothing
TEST_F(TsConfStorage_Test, IDE4_Stall)
{
    _ideScheme = IDE_NEMO_DIVIDE;
    ASSERT_TRUE(RebuildWithRomPages(32));
    ASSERT_TRUE(_decoder->GetIdeAdapter().Active());

    auto cost = [&](uint16_t port) {
        const uint32_t before = _z80->t;
        In(port);
        return _z80->t - before;
    };
    EXPECT_EQ(cost(0x00F0), 0u) << "IdeStall=0 (default): bypass";

    _context->config.ide_stall = 1;
    EXPECT_EQ(cost(0x00F0), 1u) << "status register at 3.5 MHz";
    EXPECT_EQ(cost(0x0011), 0u) << "the #11 latch is not a bus cycle";
    Reg(TsConfReg::SysConfig, 0x01);
    EXPECT_EQ(cost(0x00F0), 2u) << "7 MHz";
    Reg(TsConfReg::SysConfig, 0x02);
    EXPECT_EQ(cost(0x00F0), 3u) << "14 MHz";
}

/// DMA-15 / IDE-1 (hs §8.3, §6.2): on the Nemo IDE (NEMO-DIVIDE), READ SECTORS
/// through the ports, then DMA 0x3 moves the sector's 256 words into RAM;
/// WRITE SECTORS and DMA 0xB write RAM back to the unit
TEST_F(TsConfStorage_Test, DMA15_IdeSectorsByDma)
{
    _ideScheme = IDE_NEMO_DIVIDE;
    ASSERT_TRUE(RebuildWithRomPages(32));
    MemoryDisk disk(64);
    for (uint64_t lba = 0; lba < 64; lba++)
        std::memset(disk.Data() + lba * 512, static_cast<int>(lba), 512);
    _context->pIdeController->Channel().Unit(0)->AttachMedium(disk, {});

    auto reg = [](uint8_t r) { return static_cast<uint16_t>((r << 5) | 0x10); };  // rrr10000
    auto command = [&](uint8_t lba, uint8_t code) {
        Out(reg(6), 0xE0);  // LBA, master
        Out(reg(2), 1);     // one sector
        Out(reg(3), lba);
        Out(reg(4), 0);
        Out(reg(5), 0);
        Out(reg(7), code);
    };
    auto runDma = [&](uint8_t ctrl) {
        Reg(TsConfReg::DmaLen, 0xFF);
        Reg(TsConfReg::DmaNum, 0);
        TsConfEngine& engine = _decoder->GetEngine();
        engine.OnMachineFrameRollover(TsConfEngine::kFrameTacts);
        Reg(TsConfReg::DmaCtrl, ctrl);
        for (uint32_t line = 1; line < 320 && _decoder->GetDma().Busy(); line++)
            engine.CatchUp(line * TsConfEngine::kLineTacts);
        return !_decoder->GetDma().Busy();
    };

    command(9, 0x20);  // READ SECTORS
    Reg(TsConfReg::DmaDAl, 0);
    Reg(TsConfReg::DmaDAh, 0);
    Reg(TsConfReg::DmaDAx, 0x14);  // 0x50000
    ASSERT_TRUE(runDma(0x03));
    EXPECT_EQ(Byte(0x50000), 9);
    EXPECT_EQ(Byte(0x501FF), 9);

    for (uint32_t i = 0; i < 512; i++)
        Byte(0x60000 + i) = static_cast<uint8_t>(i * 3);
    command(5, 0x30);  // WRITE SECTORS
    Reg(TsConfReg::DmaSAl, 0);
    Reg(TsConfReg::DmaSAh, 0);
    Reg(TsConfReg::DmaSAx, 0x18);  // 0x60000
    ASSERT_TRUE(runDma(0x83));
    EXPECT_EQ(disk.Data()[5 * 512 + 0], 0x00);
    EXPECT_EQ(disk.Data()[5 * 512 + 1], 0x03) << "low byte at the even address";
    EXPECT_EQ(disk.Data()[5 * 512 + 511], static_cast<uint8_t>(511 * 3));
    _context->pIdeController->Channel().Unit(0)->DetachMedium();
}

/// IDE-5: TTD in the middle of a Nemo write - between the two halves of a word
/// (the high byte waits in the #11 latch) and mid-sector. The shared AtaChannel
/// blob (id 17) taken there and restored continues the transfer exactly: the
/// sector written after the restore is byte-identical to the first run
TEST(TsConfIde_Test, IDE5_TtdMidWriteContinuesExactly)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("TSL", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(context->pIdeController && context->pIdeController->Enabled()) << "[HDD] Scheme=NEMO-DIVIDE";
    MemoryDisk disk(64);
    context->pIdeController->Channel().Unit(0)->AttachMedium(disk, {});
    PortDecoder* decoder = context->pPortDecoder;
    auto out = [&](uint16_t port, uint8_t value) { decoder->DecodePortOut(port, value, 0); };
    auto in = [&](uint16_t port) { return decoder->DecodePortIn(port, 0); };
    auto command = [&](uint8_t code, uint8_t lba) {
        out(0xFFD0, 0xE0);  // master, LBA
        out(0x0050, 1);
        out(0x0070, lba);
        out(0x0090, 0);
        out(0x00B0, 0);
        out(0x00F0, code);
    };
    auto word = [](int i) { return static_cast<uint16_t>(i * 0x0101 ^ 0x5A3C); };

    command(0x30, 7);  // WRITE SECTORS
    for (int i = 0; i < 100; i++)
    {
        out(0x0011, static_cast<uint8_t>(word(i) >> 8));
        out(0x0010, static_cast<uint8_t>(word(i)));
    }
    out(0x0011, static_cast<uint8_t>(word(100) >> 8));  // word 100: the high byte in the latch

    ttd::TTDAtaChannel blob(context);
    std::vector<uint8_t> saved(blob.TTDStateSize());
    blob.TTDSaveState(saved.data());
    const uint64_t hash = blob.TTDHashState();

    auto finish = [&]() {
        out(0x0010, static_cast<uint8_t>(word(100)));
        for (int i = 101; i < 256; i++)
        {
            out(0x0011, static_cast<uint8_t>(word(i) >> 8));
            out(0x0010, static_cast<uint8_t>(word(i)));
        }
        std::vector<uint8_t> sector(disk.Data() + 7 * 512, disk.Data() + 8 * 512);
        return sector;
    };
    const std::vector<uint8_t> first = finish();
    EXPECT_EQ(first[0], 0x3C) << "low byte first";
    EXPECT_EQ(first[201], static_cast<uint8_t>(word(100) >> 8)) << "the latched high byte of word 100";

    std::memset(disk.Data() + 7 * 512, 0, 512);  // the unit's copy goes: only the blob can bring the words back
    blob.TTDLoadState(saved.data());
    EXPECT_EQ(blob.TTDHashState(), hash);
    EXPECT_EQ(finish(), first) << "the restored transfer writes the same sector";
    EXPECT_EQ(in(0x00F0) & 0x88, 0x00) << "not busy, no data request: the command completed";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// BUGS.md #1: the Z-controller reads FAT32 only - a folder inserted into
/// sd.zc becomes a FAT32 volume, and an explicit fat16 request is refused
TEST(TsConfMedia_Test, SdSlotIsFat32Only)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("TSL", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_NE(context->pMediaManager, nullptr);

    ScratchFolder files("tsconf-sd-fat32");
    files.File("boot.$C", "boot");
    MediaSource source;
    const auto u8 = files.Path().u8string();
    source.path = std::string(u8.begin(), u8.end());

    const MediaResult inserted = context->pMediaManager->Insert("sd.zc", source);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    const auto info = context->pMediaManager->Info("sd.zc");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->format, "folder-fat32") << "TS-BIOS and Wild Commander read FAT32";

    InsertOptions fat16;
    fat16.fs = FatType::Fat16;
    const MediaResult refused = context->pMediaManager->Insert("sd.zc", source, fat16);
    EXPECT_EQ(refused.error, MediaError::BadRequest);
    EXPECT_NE(refused.message.find("fat32"), std::string::npos) << refused.message;

    EmulatorTestHelper::CleanupEmulator(emulator);
}
