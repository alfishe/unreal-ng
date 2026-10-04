// NE2000 boards (ne2000board.h; network tdd §15 T-NET-2): the RTL8019AS's ID and page 3 (the 93C46 EEPROM read
// the way the RTL kit's READ_WORD bit-bangs it, CONFIG1 from the slot settings), the UM9003 (its ID, page 3 = page 1,
// the reset port that hangs the bus), the NE1000 (no ID, 8 KB at #2000, the PROM not doubled), the I/O decode

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "emulator/io/network/ethernet/ne2000board.h"

namespace
{
std::unique_ptr<Ne2000Board> Make(Ne2000Board::Variant variant, uint16_t base = 0x300, uint8_t irq = 3)
{
    Ne2000Board::Settings s;
    s.variant = variant;
    s.base = base;
    s.irq = irq;
    s.mac = {0x02, 0x53, 0x50, 0x00, 0x01, 0x02};
    return std::make_unique<Ne2000Board>(s, []() { return uint64_t{0}; });
}

/// The RTL kit's RTL_EEPROM.READ_WORD: programming mode (EEM = 10), CS up, start 1, READ 10, six address bits,
/// then 16 IN_BITs: SK up, read EEDO, SK down
uint16_t KitReadWord(Ne2000Board& b, uint8_t word)
{
    constexpr uint8_t kProgram = 0x80, kCs = 0x08, kSk = 0x04, kDi = 0x02;
    b.Write(0x00, 0xE1);   // page 3, stop
    b.Write(0x01, kProgram);
    b.Write(0x01, kProgram | kCs);
    auto out = [&](bool bit) {
        const uint8_t v = static_cast<uint8_t>(kProgram | kCs | (bit ? kDi : 0));
        b.Write(0x01, v);
        b.Write(0x01, static_cast<uint8_t>(v | kSk));
        b.Write(0x01, v);
    };
    out(true);
    out(true);
    out(false);
    for (int bit = 5; bit >= 0; --bit)
        out(((word >> bit) & 1) != 0);
    uint16_t value = 0;
    for (int i = 0; i < 16; ++i)
    {
        b.Write(0x01, kProgram | kCs);
        b.Write(0x01, kProgram | kCs | kSk);
        value = static_cast<uint16_t>((value << 1) | (b.Read(0x01) & 1));
        b.Write(0x01, kProgram | kCs);
    }
    b.Write(0x01, kProgram);
    b.Write(0x01, 0x00);
    b.Write(0x00, 0x21);
    return value;
}
}  // namespace

TEST(Ne2000Board_Test, Decode_BaseAndMirrors)
{
    auto b = Make(Ne2000Board::Variant::Rtl8019as, 0x300);
    uint16_t offset = 0;
    EXPECT_TRUE(b->Decodes(0x30A, offset));
    EXPECT_EQ(offset, 0x0A);
    EXPECT_TRUE(b->Decodes(0x31F, offset));
    EXPECT_EQ(offset, 0x1F);
    EXPECT_TRUE(b->Decodes(0x0070A, offset)) << "A9-A0 decoded: #70A mirrors #30A";
    EXPECT_FALSE(b->Decodes(0x320, offset));
    EXPECT_FALSE(b->Decodes(0x2FF, offset));
    uint32_t first = 0, last = 0;
    ASSERT_TRUE(b->IoRange(first, last));
    EXPECT_EQ(first, 0x300u);
    EXPECT_EQ(last, 0x31Fu);
    EXPECT_EQ(b->IrqLine(), 3);
}

TEST(Ne2000Board_Test, Rtl8019as_IdPage3AndEeprom)
{
    auto b = Make(Ne2000Board::Variant::Rtl8019as, 0x300, 3);
    EXPECT_EQ(b->Read(0x0A), 0x50);
    EXPECT_EQ(b->Read(0x0B), 0x70);
    b->Write(0x00, 0xE1);   // page 3
    EXPECT_EQ(b->Read(0x03), 0x00) << "CONFIG0: RTL8019AS, jumperless, UTP";
    EXPECT_EQ(b->Read(0x04), 0x90) << "CONFIG1: IRQEN, IRQS = 001 (IRQ 3), IOS = 0000 (#300)";
    b->Write(0x00, 0x21);
    // The EEPROM: CONFIG1-4 in word 0-1, the MAC in words 2-4 (low byte first), as the kit's NICEEP shows it
    EXPECT_EQ(KitReadWord(*b, 0), 0x0010) << "CONFIG1 byte #10 (IRQ 3, #300), CONFIG2 0";
    EXPECT_EQ(KitReadWord(*b, 2), 0x5302);
    EXPECT_EQ(KitReadWord(*b, 3), 0x0050);
    EXPECT_EQ(KitReadWord(*b, 4), 0x0201);
    const auto words = Ne2000Board::BuildEeprom(b->GetSettings());
    EXPECT_EQ(KitReadWord(*b, 9), words[9]) << "PnP serial identifier";

    auto other = Make(Ne2000Board::Variant::Rtl8019as, 0x340, 5);
    other->Write(0x00, 0xE1);
    EXPECT_EQ(other->Read(0x04), 0x80 | (3 << 4) | 2) << "IRQ 5, #340";
}

TEST(Ne2000Board_Test, Rtl8019as_AutoLoadAndConfigWriteEnable)
{
    auto b = Make(Ne2000Board::Variant::Rtl8019as);
    b->Write(0x00, 0xE1);
    b->Write(0x04, 0x00);   // not write-enabled: ignored
    EXPECT_EQ(b->Read(0x04) & 0x80, 0x80);
    b->Write(0x01, 0xC0);   // config write enable
    b->Write(0x04, 0x00);
    EXPECT_EQ(b->Read(0x04) & 0x80, 0x00) << "IRQEN cleared";
    EXPECT_FALSE(b->Irq());
    b->Write(0x01, 0x40);   // auto-load: CONFIG back from the EEPROM, CR = #21
    EXPECT_EQ(b->Read(0x00), 0x21);
    b->Write(0x00, 0xE1);
    EXPECT_EQ(b->Read(0x04) & 0x80, 0x80);
    EXPECT_EQ(b->Read(0x01) & 0xC0, 0x00) << "back to normal mode";
}

TEST(Ne2000Board_Test, Um9003_IdMirrorAndResetPortStall)
{
    auto b = Make(Ne2000Board::Variant::Um9003);
    EXPECT_EQ(b->Read(0x0A), 0x20);
    EXPECT_EQ(b->Read(0x0B), 0x01);
    b->Write(0x00, 0xE1);   // page 3 = page 1 on this clone
    EXPECT_EQ(b->Read(0x01), 0x00) << "PAR0 (not set yet)";
    b->Write(0x01, 0x5A);
    b->Write(0x00, 0x61);
    EXPECT_EQ(b->Read(0x01), 0x5A) << "the write went to PAR0";
    b->Write(0x00, 0x21);
    EXPECT_FALSE(b->Stalled());
    b->Read(0x1F);
    EXPECT_TRUE(b->Stalled()) << "the reset port read never finishes on the UM9003";
    StateNode report = StateNode::Object();
    b->Describe(report);
    ASSERT_NE(report.find("stalled"), nullptr);
    b->Reset();
    EXPECT_FALSE(b->Stalled()) << "RESET DRV ends it";
}

TEST(Ne2000Board_Test, Ne1000_NoIdRamAt2000PromDirect)
{
    auto b = Make(Ne2000Board::Variant::Ne1000);
    EXPECT_EQ(b->Read(0x0A), 0xFF);
    b->Write(0x00, 0x22);
    // Remote write to #2000, read back; the PROM at #0000 is the MAC straight
    auto dma = [&](uint16_t address, uint16_t count, uint8_t cmd) {
        b->Write(0x08, static_cast<uint8_t>(address));
        b->Write(0x09, static_cast<uint8_t>(address >> 8));
        b->Write(0x0A, static_cast<uint8_t>(count));
        b->Write(0x0B, 0);
        b->Write(0x00, cmd);
    };
    dma(0x2000, 2, 0x12);
    b->Write(0x10, 0xAB);
    b->Write(0x10, 0xCD);
    dma(0x2000, 2, 0x0A);
    EXPECT_EQ(b->Read(0x10), 0xAB);
    EXPECT_EQ(b->Read(0x10), 0xCD);
    dma(0x0000, 6, 0x0A);
    for (uint8_t expected : {0x02, 0x53, 0x50, 0x00, 0x01, 0x02})
        EXPECT_EQ(b->Read(0x10), expected);
    dma(0x4000, 1, 0x0A);
    EXPECT_EQ(b->Read(0x10), 0xFF) << "no RAM at #4000 on the 8 KB NE1000";
}

TEST(Ne2000Board_Test, PeekHasNoSideEffect)
{
    auto b = Make(Ne2000Board::Variant::Rtl8019as);
    b->Write(0x00, 0x22);
    b->Write(0x08, 0x00);
    b->Write(0x09, 0x00);
    b->Write(0x0A, 4);
    b->Write(0x0B, 0);
    b->Write(0x00, 0x0A);
    EXPECT_EQ(b->Peek(0x10), 0x02) << "the next PROM byte";
    EXPECT_EQ(b->Peek(0x10), 0x02);
    EXPECT_EQ(b->Read(0x10), 0x02);
    EXPECT_EQ(b->Read(0x10), 0x02);
    EXPECT_EQ(b->Read(0x10), 0x53) << "the peeks did not advance the DMA";
    EXPECT_EQ(b->Peek(0x1F), 0xFF);
    EXPECT_EQ(b->Read(0x00) & 0x01, 0x00) << "a peek of the reset port resets nothing";
}

TEST(Ne2000Board_Test, RegisterNamesFollowThePage)
{
    auto b = Make(Ne2000Board::Variant::Rtl8019as);
    EXPECT_STREQ(b->RegisterName(0x07, false), "ISR");
    EXPECT_STREQ(b->RegisterName(0x01, true), "PSTART");
    EXPECT_STREQ(b->RegisterName(0x01, false), "CLDA0");
    EXPECT_STREQ(b->RegisterName(0x10, false), "data port");
    EXPECT_STREQ(b->RegisterName(0x1F, false), "reset port");
    b->Write(0x00, 0x61);
    EXPECT_STREQ(b->RegisterName(0x07, false), "CURR");
    b->Write(0x00, 0xE1);
    EXPECT_STREQ(b->RegisterName(0x01, false), "9346CR");
}
