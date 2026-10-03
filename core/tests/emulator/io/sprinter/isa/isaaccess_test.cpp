// The ISA slot report and cycles every automation interface uses (isaaccess.h; Sprinter ISA tdd §10, T-ISA-14
// core part): DeviceState::Isa on the Sprinter and elsewhere, IsaAccess::Execute's actions and errors

#include <gtest/gtest.h>

#include <memory>

#include "emulator/io/sprinter/isa/isaaccess.h"
#include "emulator/io/sprinter/isa/sprinterisabus.h"
#include "emulator/machines/sprinter/sprinterfixture.h"
#include "emulator/state/devicestate.h"

namespace
{
class EchoCard : public sprinterisa::IIsaCard
{
public:
    const char* Kind() const override { return "ne2000"; }
    bool IoRead(const sprinterisa::IsaCycle& c, uint8_t& value) override
    {
        ++reads;
        value = static_cast<uint8_t>(c.address);
        return true;
    }
    bool IoWrite(const sprinterisa::IsaCycle& c, uint8_t value) override
    {
        lastWrite = c.address;
        lastValue = value;
        return true;
    }
    bool IoPeek(uint32_t address, uint8_t& value) const override
    {
        value = static_cast<uint8_t>(address);
        return true;
    }
    void SetReset(bool asserted) override { resets += asserted ? 1 : 0; }
    int reads = 0;
    int resets = 0;
    uint32_t lastWrite = 0;
    uint8_t lastValue = 0;
};
}  // namespace

class IsaAccess_Test : public SprinterFixture
{
};

TEST_F(IsaAccess_Test, Report_SlotsWindowAndLatch)
{
    _decoder->GetIsaBus().Fit(1, std::make_unique<EchoCard>());
    OpenDcp();
    Pld().sc = 0x10;
    _decoder->UpdateBanks();  // window 3's cell index follows #1FFD
    Pld().cells[Pld().pg3] = 0xD6;
    _decoder->UpdateBanks();

    const StateNode r = DeviceState::Isa(_context);
    ASSERT_TRUE(r.find("available")->b);
    const StateNode* window = r.find("window");
    ASSERT_NE(window, nullptr);
    ASSERT_TRUE(window->find("mapped")->b);
    EXPECT_EQ(window->find("slot")->i, 2);
    EXPECT_EQ(window->find("space")->s, "io");
    EXPECT_EQ(window->find("page")->s, "#D6");
    EXPECT_EQ(r.find("latch")->find("value")->s, "#00");
    EXPECT_EQ(r.find("slots")->items[1].find("card")->s, "ne2000");

    // The Sprinter report embeds the same section
    const StateNode sprinter = DeviceState::Sprinter(_context);
    ASSERT_NE(sprinter.find("isa"), nullptr);
    EXPECT_EQ(sprinter.find("isa")->find("slots")->items.size(), 2u);
}

TEST_F(IsaAccess_Test, Execute_CyclesPeekResetLatch)
{
    auto owned = std::make_unique<EchoCard>();
    EchoCard* card = owned.get();
    _decoder->GetIsaBus().Fit(1, std::move(owned));
    StateNode result;
    std::string error;

    ASSERT_TRUE(IsaAccess::Execute(_context, "io_read", 2, 0x30A, -1, "test", result, error)) << error;
    EXPECT_EQ(result.find("value")->s, "#0A");
    EXPECT_EQ(result.find("address")->s, "#0030A");
    EXPECT_EQ(card->reads, 1);
    ASSERT_TRUE(IsaAccess::Execute(_context, "io_peek", 2, 0x30B, -1, "test", result, error)) << error;
    EXPECT_EQ(result.find("value")->s, "#0B");
    EXPECT_EQ(card->reads, 1) << "a peek is no cycle";
    ASSERT_TRUE(IsaAccess::Execute(_context, "io_write", 2, 0x300, 0x21, "test", result, error)) << error;
    EXPECT_EQ(card->lastWrite, 0x300u);
    EXPECT_EQ(card->lastValue, 0x21);
    ASSERT_TRUE(IsaAccess::Execute(_context, "reset", 0, 0, -1, "test", result, error)) << error;
    EXPECT_EQ(card->resets, 1);
    EXPECT_EQ(_decoder->GetIsaBus().Latch(), 0x00) << "the pulse restores the latch";
    ASSERT_TRUE(IsaAccess::Execute(_context, "latch", 0, 0, 0x37, "test", result, error)) << error;
    EXPECT_EQ(_decoder->GetIsaBus().Latch(), 0x37);
    EXPECT_EQ(Pld().isaAddrExt, 0x37) << "the PLD's copy of A19-A14 follows";
    ASSERT_TRUE(IsaAccess::Execute(_context, "mem_read", 1, 0xDC000, -1, "test", result, error)) << error;
    EXPECT_EQ(result.find("value")->s, "#FF") << "slot 1 is empty";

    EXPECT_FALSE(IsaAccess::Execute(_context, "io_read", 3, 0x300, -1, "test", result, error));
    EXPECT_EQ(error, "slot: 1 or 2");
    EXPECT_FALSE(IsaAccess::Execute(_context, "io_write", 2, 0x300, 300, "test", result, error));
    EXPECT_FALSE(IsaAccess::Execute(_context, "io_read", 2, 0x100000, -1, "test", result, error));
    EXPECT_FALSE(IsaAccess::Execute(_context, "dma", 2, 0x300, -1, "test", result, error));
}

TEST(IsaAccessNoSlots_Test, OtherMachines_Unavailable)
{
    const StateNode r = DeviceState::Isa(nullptr);
    ASSERT_NE(r.find("available"), nullptr);
    EXPECT_FALSE(r.find("available")->b);
    EXPECT_EQ(r.find("description")->s, "no ISA slots on this machine (the Sprinter has two)");
    uint32_t a = 0;
    EXPECT_TRUE(IsaAccess::ParseAddress("#30A", a));
    EXPECT_EQ(a, 0x30Au);
    EXPECT_TRUE(IsaAccess::ParseAddress("0xDC000", a));
    EXPECT_EQ(a, 0xDC000u);
    EXPECT_TRUE(IsaAccess::ParseAddress("778", a));
    EXPECT_EQ(a, 778u);
    EXPECT_FALSE(IsaAccess::ParseAddress("#100000", a));
    EXPECT_FALSE(IsaAccess::ParseAddress("x", a));
}
