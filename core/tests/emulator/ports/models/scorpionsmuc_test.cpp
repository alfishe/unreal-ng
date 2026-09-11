#include "pch.h"

#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"

#include <cstdio>
#include <string>

/// SMUC stub verification (profrom-smuc-not-found-and-driver-disassembly.md,
/// section 8). The ProfROM service ROM probes the SMUC board through the
/// page-7 driver primitives; the tests below replay those exact bit sequences
/// at the port level, so any state-machine regression in the SMUCNvram model
/// (Xpeccy LC16 port) or the decoder wiring fails here before it can wedge
/// the real boot.

namespace
{

// Raw #FFBA bit roles: SDA out = bit 4, WP = bit 5, SCL out = bit 6
constexpr uint8_t SdaOut = 0x10;
constexpr uint8_t WriteProtect = 0x20;
constexpr uint8_t SclOut = 0x40;

EmulatorContext* CreateProfScorpContext(std::shared_ptr<Emulator>& emulator)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    emulator = manager->CreateEmulatorWithModel("", "PROFSCORP", LoggerLevel::LogError);
    if (!emulator)
        return nullptr;

    // The SMUC board is absent by default (faster ProfROM boot - the presence
    // polls fail and the NVRAM/RTC/IDE init chains are skipped). These tests
    // verify the present-board stubs, so they opt in explicitly
    static_cast<PortDecoder_Scorpion256*>(emulator->GetContext()->pPortDecoder)->SetSmucEnabled(true);
    return emulator->GetContext();
}

PortDecoder_Scorpion256* GetScorpionDecoder(EmulatorContext* context)
{
    return static_cast<PortDecoder_Scorpion256*>(context->pPortDecoder);
}

void OutFFBA(PortDecoder_Scorpion256* decoder, uint8_t value)
{
    decoder->DecodePortOut(0xFFBA, value, 0x0000);
}

uint8_t InFFBA(PortDecoder_Scorpion256* decoder)
{
    return decoder->DecodePortIn(0xFFBA, 0x0000);
}

/// The #0F2C START condition: SDA 1->0 while SCL stays high
void StartCondition(PortDecoder_Scorpion256* decoder)
{
    OutFFBA(decoder, SdaOut | SclOut);
    OutFFBA(decoder, SclOut);
}

/// The #0F1C STOP condition: SDA 0->1 while SCL stays high
void StopCondition(PortDecoder_Scorpion256* decoder)
{
    OutFFBA(decoder, SclOut);
    OutFFBA(decoder, SdaOut | SclOut);
}

/// One bit of the #0EF7 shift-out: SDA settles while SCL low, SCL rises
/// (the device samples), SCL falls
void ShiftBitOut(PortDecoder_Scorpion256* decoder, int bit)
{
    const uint8_t level = bit ? SdaOut : 0x00;
    OutFFBA(decoder, level);
    OutFFBA(decoder, level | SclOut);
    OutFFBA(decoder, level);
}

void ShiftByteOut(PortDecoder_Scorpion256* decoder, uint8_t byte)
{
    for (int i = 7; i >= 0; i--)
        ShiftBitOut(decoder, (byte >> i) & 1);
}

/// The #0EDE ACK check: release SDA, raise SCL, sample bit 6, drop SCL.
/// A device that acknowledges pulls the line low
bool AckSample(PortDecoder_Scorpion256* decoder)
{
    OutFFBA(decoder, SdaOut);
    OutFFBA(decoder, SdaOut | SclOut);
    const bool ack = (InFFBA(decoder) & 0x40) == 0;
    OutFFBA(decoder, SdaOut);

    return ack;
}

/// The #0EB8 shift-in: SCL rises (the device presents the next MSB), sample
/// bit 6, SCL falls (the device shifts)
uint8_t ShiftByteIn(PortDecoder_Scorpion256* decoder)
{
    uint8_t result = 0;
    for (int i = 0; i < 8; i++)
    {
        OutFFBA(decoder, SdaOut | SclOut);
        result = static_cast<uint8_t>((result << 1) | ((InFFBA(decoder) & 0x40) ? 1 : 0));
        OutFFBA(decoder, SdaOut);
    }

    return result;
}

} // namespace

/// Full #0E4B-style write transaction and #0E01-style read transaction
/// against the serial EEPROM: the ACK after every byte is what the ProfROM
/// presence polls (#0E91) actually test
TEST(ScorpionSMUC_Test, SerialEEPROMWriteAndReadback)
{
    std::shared_ptr<Emulator> emulator;
    EmulatorContext* context = CreateProfScorpContext(emulator);
    ASSERT_TRUE(context);
    PortDecoder_Scorpion256* decoder = GetScorpionDecoder(context);
    ASSERT_TRUE(decoder);

    // START, select 0xA0 (device 0, write), ACK, address 0x00, ACK,
    // data 0x5A, ACK, STOP - commits the page at EEPROM address 0
    StartCondition(decoder);
    ShiftByteOut(decoder, 0xA0);
    EXPECT_TRUE(AckSample(decoder));
    ShiftByteOut(decoder, 0x00);
    EXPECT_TRUE(AckSample(decoder));
    ShiftByteOut(decoder, 0x5A);
    EXPECT_TRUE(AckSample(decoder));
    StopCondition(decoder);

    EXPECT_EQ(decoder->GetSMUCNvram().GetEEPROMByte(0), 0x5A);

    // READ transaction: dummy-write the address, RESTART with select 0xA1,
    // ACK, then clock the byte out MSB-first
    StartCondition(decoder);
    ShiftByteOut(decoder, 0xA0);
    EXPECT_TRUE(AckSample(decoder));
    ShiftByteOut(decoder, 0x00);
    EXPECT_TRUE(AckSample(decoder));
    StartCondition(decoder);
    ShiftByteOut(decoder, 0xA1);
    EXPECT_TRUE(AckSample(decoder));
    EXPECT_EQ(ShiftByteIn(decoder), 0x5A);
    StopCondition(decoder);
}

/// Register-level stubs: version / revision / PIC probes plus the IDE window
/// task-file readback that a controller-presence signature test relies on
TEST(ScorpionSMUC_Test, SmucRegisterStubsAnswer)
{
    std::shared_ptr<Emulator> emulator;
    EmulatorContext* context = CreateProfScorpContext(emulator);
    ASSERT_TRUE(context);
    PortDecoder_Scorpion256* decoder = GetScorpionDecoder(context);
    ASSERT_TRUE(decoder);

    EXPECT_EQ(decoder->DecodePortIn(0x5FBA, 0x0000), 0x3F);  // version
    EXPECT_EQ(decoder->DecodePortIn(0x5FBE, 0x0000), 0x57);  // revision
    EXPECT_EQ(decoder->DecodePortIn(0x7FBE, 0x0000), 0x57);  // 8259 PIC

    decoder->DecodePortOut(0x7FBA, 0xC0, 0x0000);            // virtual FDD latch
    EXPECT_EQ(decoder->DecodePortIn(0x7FBA, 0x0000), 0xFF);  // latch | 0x3F

    // IDE task file (#F9BE = ATA sector count): writes read back verbatim
    decoder->DecodePortOut(0xF9BE, 0x55, 0x0000);
    EXPECT_EQ(decoder->DecodePortIn(0xF9BE, 0x0000), 0x55);
    // IDE status (#FFBE = ATA #1F7): ready, never busy
    EXPECT_EQ(decoder->DecodePortIn(0xFFBE, 0x0000), 0x50);
}

/// The ProfROM boot itself must drive the SMUC probes to completion: with
/// the stubs wired in, the first-boot format path (#0D6F) programs the
/// default byte 0x61 into the EEPROM through the serial link
TEST(ScorpionSMUC_Test, ProfRomBootDrivesSmucProbes)
{
    std::shared_ptr<Emulator> emulator;
    EmulatorContext* context = CreateProfScorpContext(emulator);
    ASSERT_TRUE(context);
    EmulatorState& state = context->emulatorState;
    Z80* z80 = context->pCore->GetZ80();

    PortDecoder_Scorpion256* decoder = GetScorpionDecoder(context);
    ASSERT_TRUE(decoder);

    // Deterministic RTC (same freeze as the turbo-detect test), applied
    // before any frame runs: live host time shifts the boot timeline - the
    // NVRAM format used to land right around the first 100-frame checkpoint
    // and varied with the wall clock
    decoder->GetSMUCNvram().SetFixedTime(1767268830);  // 2026-01-01 12:00:30 UTC

    emulator->RunNFrames(10);

    // The check panel is transient: it renders around frames 100-350 and is
    // cleared once the boot proceeds - sample the peak population across
    // the run. With the SMUC board answering, the panel grows past its single
    // header line (device status + parsed NVRAM config in rows 10-15); with
    // the stubs disabled (baseline) those rows never carry text - that
    // difference is the "not found" messages gone
    int maxPanelRows = 0;
    for (int frame = 100; frame <= 1000; frame += 100)
    {
        emulator->RunNFrames(90);
        printf("f%4d: PC=%04X pFFBA=%02X EEPROM[0]=%02X [FE]=%02X [FF]=%02X\n", frame,
               z80->pc, state.pFFBA,
               decoder->GetSMUCNvram().GetEEPROMByte(0),
               decoder->GetSMUCNvram().GetEEPROMByte(0xFE),
               decoder->GetSMUCNvram().GetEEPROMByte(0xFF));

        int populatedPanelRows = 0;
        Memory* memory = context->pMemory;
        for (int row = 10; row <= 15; row++)
        {
            const int addr = 0x4000 + (row & 7) * 0x100 + (row >> 3) * 0x20;
            std::string line;
            for (int col = 0; col < 32; col++)
            {
                const uint8_t code = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(addr + col));
                if (code < 0x20)
                    line += static_cast<char>(code == 0 ? ' ' : code + 0x20);
                else if (code < 0x7F)
                    line += static_cast<char>(code);
                else
                    line += '#';
            }
            if (line.find_first_not_of(' ') != std::string::npos)
                populatedPanelRows++;
        }
        if (populatedPanelRows > maxPanelRows)
            maxPanelRows = populatedPanelRows;
    }
    EXPECT_GE(maxPanelRows, 3);

    // Final state snapshot
    printf("final: PC=%04X pFFBA=%02X maxPanelRows=%d\n", z80->pc, state.pFFBA, maxPanelRows);

    // The service ROM opened at least one serial transaction on #FFBA
    EXPECT_NE(state.pFFBA, 0x00);

    // First-boot format path: default byte 0x61 lands at EEPROM address 0
    EXPECT_EQ(decoder->GetSMUCNvram().GetEEPROMByte(0), 0x61);
}

/// Regression (2026-09-10): the first SMUC decode pattern (mask #18A3) also
/// matched 7 of the 8 keyboard row ports, and the SMUC arm precedes the #FE
/// arm - on PROFSCORP every half-row except 6-7-8-9-0 read the SMUC open bus
/// instead of the key matrix, and the ProfROM service monitor saw a dead
/// keyboard. The row ports must always decode as the ULA #FE arm, even with
/// the SMUC board present. D4-D0 per half-row, pressed key reads 0
TEST(ScorpionSMUC_Test, KeyboardRowsAreNotShadowedBySmuc)
{
    std::shared_ptr<Emulator> emulator;
    EmulatorContext* context = CreateProfScorpContext(emulator); // board PRESENT
    ASSERT_TRUE(context);
    PortDecoder_Scorpion256* decoder = GetScorpionDecoder(context);
    ASSERT_TRUE(decoder);
    Keyboard* keyboard = context->pKeyboard;
    ASSERT_TRUE(keyboard);

    // One key per previously shadowed half-row (rows: #F7FE 1-5, #FBFE QWERT,
    // #FDFE ASDFG, #FEFE Caps-ZXCV, #DFFE YUIOP, #BFFE HJKL-Enter,
    // #7FFE BNM-Space). #EFFE 6-0 is the one row the old pattern missed
    keyboard->PressKey(ZXKEY_1);     // #F7FE bit 0
    keyboard->PressKey(ZXKEY_E);     // #FBFE bit 2
    keyboard->PressKey(ZXKEY_A);     // #FDFE bit 0
    keyboard->PressKey(ZXKEY_V);     // #FEFE bit 4
    keyboard->PressKey(ZXKEY_Y);     // #DFFE bit 4
    keyboard->PressKey(ZXKEY_ENTER); // #BFFE bit 0
    keyboard->PressKey(ZXKEY_SPACE); // #7FFE bit 0

    EXPECT_EQ(decoder->DecodePortIn(0xF7FE, 0x0000) & 0x1F, 0x1E);
    EXPECT_EQ(decoder->DecodePortIn(0xFBFE, 0x0000) & 0x1F, 0x1B);
    EXPECT_EQ(decoder->DecodePortIn(0xFDFE, 0x0000) & 0x1F, 0x1E);
    EXPECT_EQ(decoder->DecodePortIn(0xFEFE, 0x0000) & 0x1F, 0x0F);
    EXPECT_EQ(decoder->DecodePortIn(0xDFFE, 0x0000) & 0x1F, 0x0F);
    EXPECT_EQ(decoder->DecodePortIn(0xBFFE, 0x0000) & 0x1F, 0x1E);
    EXPECT_EQ(decoder->DecodePortIn(0x7FFE, 0x0000) & 0x1F, 0x1E);

    // The never-shadowed row decodes the same way ('6' = #EFFE bit 4)
    keyboard->PressKey(ZXKEY_6);
    EXPECT_EQ(decoder->DecodePortIn(0xEFFE, 0x0000) & 0x1F, 0x0F);

    // Both arms coexist: with the board present the SMUC register stubs keep
    // answering their own ports while the rows read the key matrix
    EXPECT_EQ(decoder->DecodePortIn(0x5FBA, 0x0000), 0x3F);
}
