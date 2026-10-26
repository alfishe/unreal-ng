// The Next's board variants against zxnext.vhd (core 3.02.03) and its three top levels: NR #0F, the core id rule of NR #10, the
// Artix-7's DNA and XADC behind NR #F0 / #F8-#FA (zxnext.vhd gen_xper_44, zxnext_top_issue4.vhd 2408-2520), read the way the
// firmware's .XPER dot command reads them (tbblue-firmware-sources c/DotCommands/xper/xper.c)
#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/z80n/nextboard.h"
#include "emulator/io/z80n/nextstatecodec.h"
#include "emulator/machinevariants.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"

namespace
{
constexpr uint16_t kPortSelect = 0x243B, kPortData = 0x253B;
}  // namespace

class NextBoardIssue_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    PortDecoder_Next* _ports = nullptr;

    /// A model or a variant: the variant's board over its base model, as EmulatorManager creates it
    void Create(const char* model)
    {
        const MachineVariant* variant = MachineVariants::Find(model);
        _emulator = EmulatorTestHelper::CreateStandardEmulator(variant ? variant->baseModel : model, LoggerLevel::LogError,
                                                               RamPowerOn::Zero, variant ? variant->apply : nullptr);
        ASSERT_NE(_emulator, nullptr) << model;
        _ports = dynamic_cast<PortDecoder_Next*>(_emulator->GetContext()->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
    NextBoard& Board() { return _ports->Board(); }
    void Nr(uint8_t reg, uint8_t value) { Board().WriteNextReg(reg, value); }
    /// A program's read: port #243B selects, #253B reads (ZXN_READ_REG)
    uint8_t ReadReg(uint8_t reg)
    {
        _ports->DecodePortOut(kPortSelect, reg, 0);
        return _ports->DecodePortIn(kPortData, 0);
    }
};

TEST_F(NextBoardIssue_Test, EachBoardReadsItsIssueInNr0F)
{
    struct Case
    {
        const char* model;
        NextBoardIssue issue;
        uint8_t nr0f;
    };
    for (const Case& c : {Case{"NEXT", NextBoardIssue::Issue4, 2}, Case{"NEXT-KS1", NextBoardIssue::Issue2, 0},
                          Case{"NEXT-KS3", NextBoardIssue::Issue5, 3}})
    {
        Create(c.model);
        EXPECT_EQ(Board().Issue(), c.issue) << c.model;
        EXPECT_EQ(ReadReg(0x0F), c.nr0f) << c.model << ": \"0000\" & g_board_issue (zxnext.vhd 5920)";
        Nr(0x0F, 0x55);
        EXPECT_EQ(ReadReg(0x0F), c.nr0f) << c.model << ": read only";
        // xper.c: board_issue = (NR #0F & 0x0F) + 2, "ISSUE 4 REQUIRED" below 4
        EXPECT_EQ((ReadReg(0x0F) & 0x0F) + 2, c.issue == NextBoardIssue::Issue2 ? 2 : c.issue == NextBoardIssue::Issue4 ? 4 : 5);
        const MachineVariant* variant = MachineVariants::Of(_emulator->GetContext()->config);
        EXPECT_STREQ(variant ? variant->name : "NEXT", c.model) << "a loaded machine is reported as its variant";
        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }
}

TEST_F(NextBoardIssue_Test, TheArtixBoardsTakeACoreIdUpTo14)
{
    Create("NEXT");
    NextMemory& memory = *static_cast<NextMemory*>(_emulator->GetContext()->pMemory);
    memory.SetConfigMode(true);
    Nr(0x10, 0x05);
    EXPECT_EQ(Board().Stored(0x10), 0x05);
    Nr(0x10, 0x0F);
    EXPECT_EQ(Board().Stored(0x10), 0x05) << "15 is not taken (gen_coreid_45)";
    Nr(0x10, 0x13);
    EXPECT_EQ(Board().Stored(0x10), 0x05) << "nor anything with bit 4";
    Board().SetIssue(NextBoardIssue::Issue2);
    Nr(0x10, 0x13);
    EXPECT_EQ(Board().Stored(0x10), 0x13) << "Issue 2 keeps 5 bits (gen_coreid_23)";
}

// xper.c read_xilinx_dna: select the DNA, enter its mode, load, then 8 reads of the length and that many reads of the id
TEST_F(NextBoardIssue_Test, TheDnaShiftsOutItsLengthThenTheId)
{
    Create("NEXT");
    EXPECT_EQ(ReadReg(0xF0), 0x80) << "after reset: select mode, both off";
    Nr(0xF0, 0xC1);
    EXPECT_EQ(ReadReg(0xF0), 0x81) << "the DNA enabled";
    Nr(0xF0, 0x00);  // enter the DNA mode
    Nr(0xF0, 0x00);  // load
    unsigned length = 0;
    for (int i = 0; i < 8; i++)
        length = (length << 1) | ReadReg(0xF0);
    ASSERT_EQ(length, NextXilinx::kDnaBits);
    uint64_t dna = 0;
    for (unsigned i = 0; i < length; i++)
        dna = (dna << 1) | ReadReg(0xF0);
    EXPECT_EQ(dna, NextXilinx::kDna);
    EXPECT_EQ(ReadReg(0xF0), 0) << "then DIN, tied to 0";
    Nr(0xF0, 0xC0);
    EXPECT_EQ(ReadReg(0xF0), 0x80) << "back to select mode, both off";

    // a tool's read (the NextREG report, a debugger) does not shift
    Nr(0xF0, 0xC1);
    Nr(0xF0, 0x00);
    Nr(0xF0, 0x00);
    for (int i = 0; i < 4; i++)
        EXPECT_EQ(Board().Read(0xF0), 0) << "the length's top bit, every time";
    EXPECT_EQ(ReadReg(0xF0), 0);
    EXPECT_EQ(ReadReg(0xF0), 0);
    EXPECT_EQ(ReadReg(0xF0), 1) << "#39: 0011 1001";
}

// xper.c read_xilinx_xadc: NR #F8 the address, the result in NR #FA:#F9 (12 bits on top); reset_xilinx_xadc writes the control
// registers through NR #F9 / #FA and bit 7 of NR #F8
TEST_F(NextBoardIssue_Test, TheXadcAnswersTheFixedDieThroughItsDrp)
{
    Create("NEXT-KS3");
    auto drp = [&](uint8_t address) {
        Nr(0xF8, address);
        return static_cast<unsigned>((ReadReg(0xFA) * 256 + ReadReg(0xF9)) / 16);
    };
    const double temperature = drp(0x00) * 503.975 / 4096.0 - 273.15;
    EXPECT_NEAR(temperature, 45.0, 0.1);
    EXPECT_EQ(drp(0x20), drp(0x00)) << "the maximum";
    EXPECT_EQ(drp(0x24), drp(0x00)) << "the minimum";
    EXPECT_NEAR(drp(0x01) / 4096.0 * 3.0, 1.0, 0.001) << "VCCINT";
    EXPECT_NEAR(drp(0x02) / 4096.0 * 3.0, 1.8, 0.001) << "VCCAUX";
    EXPECT_NEAR(drp(0x06) / 4096.0 * 3.0, 1.0, 0.001) << "VCCBRAM";
    EXPECT_EQ(ReadReg(0xF8), 0x06) << "'0' & the address";

    Nr(0xF8, 0x41);
    EXPECT_EQ(ReadReg(0xFA) * 256 + ReadReg(0xF9), 0x00F0) << "INIT_41";
    Nr(0xF9, 0x34);
    Nr(0xFA, 0x12);
    Nr(0xF8, 0x80 | 0x48);  // write
    EXPECT_EQ(ReadReg(0xF8), 0x48);
    Nr(0xF9, 0);
    Nr(0xFA, 0);
    Nr(0xF8, 0x48);
    EXPECT_EQ(ReadReg(0xFA) * 256 + ReadReg(0xF9), 0x1234);

    Nr(0xF0, 0xC2);
    EXPECT_EQ(ReadReg(0xF0), 0x82) << "the XADC enabled";
    Nr(0xF0, 0x00);
    EXPECT_EQ(ReadReg(0xF0), 0x03) << "not busy, EOC and EOS";
    Nr(0xF0, 0x40);  // the XADC reset
    Nr(0xF0, 0xC0);
    EXPECT_EQ(ReadReg(0xF0), 0x80);
}

TEST_F(NextBoardIssue_Test, TheIssue2BoardHasNoXilinxPeripherals)
{
    Create("NEXT-KS1");
    for (uint8_t reg : {0xF0, 0xF8, 0xF9, 0xFA})
    {
        Nr(reg, 0xC1);
        EXPECT_EQ(ReadReg(reg), 0) << "NR #" << std::hex << int(reg) << ": gen_xper_23 ties it to 0";
    }
}

TEST_F(NextBoardIssue_Test, AStateImageLoadsOnlyIntoTheSameBoard)
{
    Create("NEXT");
    Nr(0xF0, 0xC1);
    const std::vector<uint8_t> image = NextStateCodec::Save(*_ports);
    Nr(0xF0, 0xC0);
    ASSERT_TRUE(NextStateCodec::Load(*_ports, image.data(), image.size()));
    EXPECT_EQ(ReadReg(0xF0), 0x81) << "the DNA / XADC state is in the image";
    Board().SetIssue(NextBoardIssue::Issue5);
    EXPECT_FALSE(NextStateCodec::Load(*_ports, image.data(), image.size())) << "another board is another FPGA image";
}

TEST_F(NextBoardIssue_Test, TheBoardsAreCreatedByNameOnEverySurface)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    for (const char* name : {"NEXT-KS1", "NEXT-KS3", "next-issue5"})
    {
        std::string error;
        auto emulator = manager->CreateEmulatorWithModel("next-board-test", name, LoggerLevel::LogError, &error);
        ASSERT_NE(emulator, nullptr) << name << ": " << error;
        const MachineIdentity identity = EmulatorManager::GetMachineIdentity(*emulator);
        EXPECT_EQ(identity.Model, "NEXT") << name;
        EXPECT_EQ(identity.Variant, MachineVariants::Find(name)->name) << name;
        auto* ports = dynamic_cast<PortDecoder_Next*>(emulator->GetContext()->pPortDecoder);
        ASSERT_NE(ports, nullptr);
        EXPECT_EQ(ports->Board().Issue(), std::string(name) == "NEXT-KS1" ? NextBoardIssue::Issue2 : NextBoardIssue::Issue5);
        manager->RemoveEmulator(emulator->GetUUID());
    }
}
