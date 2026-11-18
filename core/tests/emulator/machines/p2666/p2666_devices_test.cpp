// Pentagon 2.666 LE peripherals through the board's port decode (hardware-reference.md §5): Z-Controller SD, the sound, CMOS, mouse, joystick.

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <gtest/gtest.h>

#include <memory>

#include "_helpers/emulatortesthelper.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/ports/models/portdecoder_p2666.h"
#include "pch.h"
#include "stdafx.h"

class P2666Devices_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoderP2666* _decoder = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("P2666", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoderP2666*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    EmulatorState& State() { return _context->emulatorState; }
    void Out(uint16_t port, uint8_t value) { _decoder->DecodePortOut(port, value, 0x8000); }
    uint8_t In(uint16_t port) { return _decoder->DecodePortIn(port, 0x8000); }
    void Dos(bool on)
    {
        if (on)
            State().flags |= CF_TRDOS;
        else
            State().flags &= ~CF_TRDOS;
    }

    /// One SD command on the data port: the R1 byte (#FF: none in 16 polls)
    uint8_t Command(uint16_t data, uint8_t index, uint32_t arg, uint8_t crc)
    {
        Out(data, static_cast<uint8_t>(0x40 | index));
        for (int shift = 24; shift >= 0; shift -= 8)
            Out(data, static_cast<uint8_t>(arg >> shift));
        Out(data, crc);
        for (int i = 0; i < 16; i++)
        {
            (void)In(0x0057);
            if (_decoder->GetZController().GetState().rxLatch != 0xFF)
                return In(0x0057);
        }
        return 0xFF;
    }

    /// CMD0, CMD8, ACMD41 until ready, CMD17 of LBA 3: the sector's first bytes match the disk's
    void InitAndRead(uint16_t data, MemoryDisk* disk)
    {
        ASSERT_EQ(Command(data, 0, 0, 0x95), 0x01) << "CMD0: idle";
        ASSERT_EQ(Command(data, 8, 0x1AA, 0x87), 0x01) << "CMD8: idle, SD v2";
        for (int i = 0; i < 4; i++)
            (void)In(0x0057);  // the R7 tail
        uint8_t r1 = 0x01;
        for (int attempt = 0; attempt < 16 && r1 == 0x01; attempt++)
        {
            ASSERT_EQ(Command(data, 55, 0, 0x01), 0x01) << "CMD55";
            r1 = Command(data, 41, 0x40000000, 0x01);
        }
        ASSERT_EQ(r1, 0x00) << "ACMD41: ready";

        ASSERT_EQ(Command(data, 17, 3 * 512, 0x01), 0x00) << "CMD17 (a 32 KB card is SDSC: the argument is the byte address)";
        uint8_t token = 0xFF;
        for (int i = 0; i < 64 && token == 0xFF; i++)
            token = In(0x0057);
        ASSERT_EQ(token, 0xFE) << "data token";
        for (int i = 0; i < 16; i++)
            EXPECT_EQ(In(0x0057), disk->Data()[3 * 512 + i]) << i;
    }

    MemoryDisk* InsertCard()
    {
        auto disk = std::make_unique<MemoryDisk>(64);
        for (int i = 0; i < 64 * 512; i++)
            disk->Data()[i] = static_cast<uint8_t>(i * 7 + (i >> 9));
        MemoryDisk* raw = disk.get();
        EXPECT_TRUE(_decoder->GetSdCard().insert(std::move(disk), SdCardSpi::WriteMode::Session));
        return raw;
    }
};

/// T-I1: out of DOS #xx77 is the config (D1 = /CS) and #xx57 the data at any A15 (PaE.vhd:977-985, 1288-1294)
TEST_F(P2666Devices_Test, SdOutOfDosConfig77Data57)
{
    MemoryDisk* disk = InsertCard();
    Dos(false);
    Out(0x0077, 0x02);
    EXPECT_FALSE(_decoder->GetZController().IsSelected());
    Out(0x0077, 0x00);
    EXPECT_TRUE(_decoder->GetZController().IsSelected());
    InitAndRead(0x8057, disk);  // A15 = 1 is data out of DOS
}

/// T-I1 in DOS (Gluk's FATALL): #xx57 with A15 = 1 is the config, with A15 = 0 the data; #xx77 is the ATM latch
TEST_F(P2666Devices_Test, SdInDosConfigIsA15Of57)
{
    MemoryDisk* disk = InsertCard();
    Dos(true);
    const uint8_t x77 = State().p2666.pxx77;
    Out(0x8057, 0x02);
    EXPECT_FALSE(_decoder->GetZController().IsSelected());
    Out(0x8057, 0x00);
    EXPECT_TRUE(_decoder->GetZController().IsSelected());
    EXPECT_EQ(State().p2666.pxx77, x77) << "the SD config write is not an xx77 write";
    InitAndRead(0x0057, disk);
}

/// sd_config has no reset term: a CPU reset keeps the card deselected or selected; power-on selects it (config 0)
TEST_F(P2666Devices_Test, SdConfigSurvivesCpuReset)
{
    EXPECT_TRUE(_decoder->GetZController().IsSelected()) << "power-on: sd_config = 0";
    Dos(false);
    Out(0x0077, 0x02);
    _decoder->reset();
    EXPECT_FALSE(_decoder->GetZController().IsSelected());
    _decoder->PowerCycle();
    EXPECT_TRUE(_decoder->GetZController().IsSelected());
}

/// region <Sound (HR §5.1-5.3)>

/// T-S1: #FF / #FE select the chip after the address goes to the old one; reads are the VHDL's masked O_DA, an
/// address above 13 leaves the last value read; addr(3 downto 0) only, so #10 is R0 again
TEST_F(P2666Devices_Test, YmSelectAndRegisterReads)
{
    P2666Audio& audio = _decoder->GetAudio();
    Dos(false);  // in DOS every odd port's A7..5 = 111 read is the Beta Disk's system register
    Out(0xFFFD, 0x01);
    Out(0xBFFD, 0xAB);
    EXPECT_EQ(audio.Register(0, 1), 0xAB);
    EXPECT_EQ(In(0xFFFD), 0x0B) << "R1 reads its low 4 bits";
    Out(0xFFFD, 0x08);
    Out(0xBFFD, 0xFF);
    EXPECT_EQ(In(0xFFFD), 0x1F) << "R8 reads 5 bits";
    Out(0xFFFD, 0x0E);
    EXPECT_EQ(In(0xFFFD), 0x1F) << "R14 does not exist: O_DA keeps the last read";
    Out(0xBFFD, 0x55);
    EXPECT_EQ(In(0xFFFD), 0x1F) << "a write to R14 is lost";

    Out(0xFFFD, 0xFE);  // chip 1 from now on
    EXPECT_EQ(audio.SelectedChip(), 1u);
    Out(0xFFFD, 0x10);  // R0 of chip 1
    Out(0xBFFD, 0x42);
    EXPECT_EQ(audio.Register(1, 0), 0x42);
    EXPECT_EQ(audio.Register(0, 0), 0x00);
    EXPECT_EQ(In(0xFFFD), 0x42);
    EXPECT_EQ(In(0xDFFD), 0xFF) << "A13 = 0: not the YM read row";
    Out(0xFFFD, 0xFF);
    EXPECT_EQ(audio.SelectedChip(), 0u);
    Out(0xFFFD, 0x01);
    EXPECT_EQ(In(0xFFFD), 0x0B);
}

/// T-S2: right = A + B, left = B + C of both chips through the linmix table, + Covox, + 50 per beeper / tape-out bit
TEST_F(P2666Devices_Test, DacSumIsTheBoardsStereo)
{
    P2666Audio& audio = _decoder->GetAudio();
    Dos(false);
    const auto reg = [&](uint8_t chip, uint8_t r, uint8_t v) {
        Out(0xFFFD, chip ? 0xFE : 0xFF);
        Out(0xFFFD, r);
        Out(0xBFFD, v);
    };
    uint8_t right = 0, left = 0;
    reg(0, 7, 0x3F);  // tone and noise off: the gate stays open, the volume is the level
    reg(1, 7, 0x3F);
    reg(0, 8, 0x0F);  // chip 0 A: amplitude 31 -> #3F
    audio.MixNow(right, left);
    EXPECT_EQ(right, 0x3F);
    EXPECT_EQ(left, 0x00);
    reg(1, 10, 0x08);  // chip 1 C: amplitude 17 -> #05
    reg(1, 9, 0x01);   // chip 1 B: amplitude 3 -> 0
    audio.MixNow(right, left);
    EXPECT_EQ(right, 0x3F);
    EXPECT_EQ(left, 0x05);
    reg(0, 9, 0x0C);  // chip 0 B: amplitude 25 -> #16, both sides
    audio.MixNow(right, left);
    EXPECT_EQ(right, 0x3F + 0x16);
    EXPECT_EQ(left, 0x05 + 0x16);

    Out(0x00FB, 0x20);
    audio.MixNow(right, left);
    EXPECT_EQ(right, 0x3F + 0x16 + 0x20);
    EXPECT_EQ(left, 0x05 + 0x16 + 0x20);
    Out(0x00FE, 0x10);  // beeper
    audio.MixNow(right, left);
    EXPECT_EQ(right, 0x3F + 0x16 + 0x20 + 50);
    Out(0x00FE, 0x18);  // beeper and tape out
    audio.MixNow(right, left);
    EXPECT_EQ(left, 0x05 + 0x16 + 0x20 + 100);
}

/// T-S3 (D-11): the 8-bit sum wraps
TEST_F(P2666Devices_Test, DacSumWraps)
{
    P2666Audio& audio = _decoder->GetAudio();
    Dos(false);
    for (uint8_t chip : {0xFF, 0xFE})
    {
        Out(0xFFFD, chip);
        Out(0xFFFD, 7);
        Out(0xBFFD, 0x3F);
        for (uint8_t r : {8, 9})
        {
            Out(0xFFFD, r);
            Out(0xBFFD, 0x0F);
        }
    }
    uint8_t right = 0, left = 0;
    audio.MixNow(right, left);
    EXPECT_EQ(right, 0xFC);  // 4 x #3F
    Out(0x00FB, 0x10);
    audio.MixNow(right, left);
    EXPECT_EQ(right, 0x0C) << "#10C keeps its low byte";
    EXPECT_EQ(left, 0x3F * 2 + 0x10);
}

/// T-S4: a CPU reset zeroes the YMs (R7 = 0 too), their address latches, the chip select and Covox
TEST_F(P2666Devices_Test, CpuResetClearsTheSound)
{
    P2666Audio& audio = _decoder->GetAudio();
    Dos(false);
    Out(0xFFFD, 0xFE);
    Out(0xFFFD, 0x07);
    Out(0xBFFD, 0x3F);
    Out(0x00FB, 0x80);
    _decoder->reset();
    EXPECT_EQ(audio.SelectedChip(), 0u);
    EXPECT_EQ(audio.Register(1, 7), 0x00);
    EXPECT_EQ(audio.Covox(), 0x00);
    EXPECT_EQ(In(0xFFFD), 0x00) << "chip 0, address 0, R0 = 0";
}

/// The mixer shows the board's sound on the standard rows: AY 1 / AY 2 / COVOX / Beeper are its parts
TEST_F(P2666Devices_Test, SoundPartsAreTheStandardRows)
{
    P2666Audio& audio = _decoder->GetAudio();
    ASSERT_EQ(audio.AudioPartCount(), 4u);
    EXPECT_EQ(audio.AudioPartType(0), AudioSourceType::AY1_All);
    EXPECT_EQ(audio.AudioPartType(1), AudioSourceType::AY2_All);
    EXPECT_EQ(audio.AudioPartType(2), AudioSourceType::COVOX);
    EXPECT_EQ(audio.AudioPartType(3), AudioSourceType::Beeper);
    EXPECT_EQ(_context->pSoundManager->getModelAudioSource(), &audio);
}

/// endregion </Sound>

/// region <CMOS (HR §5.4)>

/// T-I3: cells #00..#3F come back after a power cycle (the MCU uploads its copy), the rest of the FPGA RAM is blank
TEST_F(P2666Devices_Test, CmosKeepsTheMcuCells)
{
    Dos(false);
    Out(0xEFF7, 0x80);
    const auto write = [&](uint8_t cell, uint8_t value) {
        Out(0xDFF7, cell);
        Out(0xBFF7, value);
    };
    const auto read = [&](uint8_t cell) {
        Out(0xDFF7, cell);
        return In(0xBFF7);
    };
    write(0x20, 0x5A);
    write(0x3F, 0xC3);
    write(0x40, 0x77);
    EXPECT_EQ(read(0x20), 0x5A);
    EXPECT_EQ(read(0x40), 0x77);
    _decoder->PowerCycle();
    _decoder->reset();
    Dos(false);
    Out(0xEFF7, 0x80);
    EXPECT_EQ(read(0x20), 0x5A);
    EXPECT_EQ(read(0x3F), 0xC3);
    EXPECT_EQ(read(0x40), 0x00) << "cell #40 is the FPGA's only";
}

/// T-I4: the fixed answers of the read mux (PaE.vhd:1497-1501) and the clock cells
TEST_F(P2666Devices_Test, CmosFixedCellsAndClock)
{
    Dos(false);
    Out(0xEFF7, 0x80);
    const auto read = [&](uint8_t cell) {
        Out(0xDFF7, cell);
        return In(0xBFF7);
    };
    EXPECT_EQ(read(0x0A), 0x20);
    EXPECT_EQ(read(0x0B), 0x02) << "BCD, 24 hours";
    EXPECT_EQ(read(0x0C), 0x00);
    EXPECT_EQ(read(0x0D), 0x80);
    EXPECT_EQ(read(0x11), 0xAA);
    EXPECT_EQ(read(0x51), 0xAA) << "the fixed cells match the address' low 6 bits";
    const uint8_t seconds = read(0x00);
    EXPECT_LE(seconds & 0x0F, 9) << "BCD";
    EXPECT_LE(seconds >> 4, 5);
    Out(0xEFF7, 0x00);
    Out(0xDFF7, 0x0B);
    EXPECT_EQ(In(0xBFF7), 0xFF) << "#EFF7 bit 7 off: no CMOS";
}

/// endregion </CMOS>

/// region <Mouse and joystick (HR §5.5)>

/// T-I6: #1F out of DOS is the MCU's Kempston joystick, five bits (PaE.vhd:1528); in DOS it is the Beta Disk status
TEST_F(P2666Devices_Test, KempstonJoystickOutOfDosOnly)
{
    Joystick* joystick = _context->pJoystick;
    ASSERT_NE(joystick, nullptr);
    joystick->SetPresent(true);
    joystick->SetExtended(true);
    joystick->SetState(0xE5);
    Dos(false);
    EXPECT_EQ(In(0x001F), 0x05) << "000 & kempston(4 downto 0)";
    EXPECT_EQ(In(0xFF1F), 0x05) << "the low byte decodes";
}

/// T-I5: the mouse rows decode the full 16-bit address in and out of DOS (PaE.vhd:1536-1538)
TEST_F(P2666Devices_Test, KempstonMouseFullAddress)
{
    Mouse* mouse = _context->pMouse;
    ASSERT_NE(mouse, nullptr);
    mouse->SetPresent(true);
    mouse->SetCounters(0x12, 0x34);
    Dos(false);
    EXPECT_EQ(In(0xFBDF), 0x12);
    EXPECT_EQ(In(0xFFDF), 0x34);
    EXPECT_EQ(In(0x7BDF), 0xFF) << "A15 = 0: not the mouse";
}

/// endregion </Mouse and joystick>

/// T-I2: the read mux answers every even port with the FE value first (D-5): the Nemo IDE's even registers (#10,
/// #30..#F0) never reach the bus, in DOS or out of it
TEST_F(P2666Devices_Test, NemoIdeEvenRegistersReadFe)
{
    for (const bool dos : {false, true})
    {
        Dos(dos);
        const uint8_t fe = In(0x00FE);
        for (uint16_t reg : {0x0010, 0x0030, 0x0050, 0x0070, 0x0090, 0x00B0, 0x00D0, 0x00F0})
            EXPECT_EQ(In(reg), fe) << std::hex << reg << " dos " << dos;
    }
}
