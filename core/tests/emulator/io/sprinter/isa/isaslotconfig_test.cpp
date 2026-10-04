// The [ISA] keys (isaslotconfig.h; Sprinter ISA tdd §5, network tdd §12): kinds, NE2000 chip / base / MAC
// parsing, the owner's default population, the automatic MAC

#include <gtest/gtest.h>

#include <cstring>

#include "emulator/io/sprinter/isa/isaslotconfig.h"

using namespace sprinterisa;

TEST(IsaSlotConfig_Test, Default_Slot1EmptySlot2Ne2000)
{
    const IsaConfig c = DefaultConfig();
    EXPECT_EQ(static_cast<CardKind>(c.slot[0].kind), CardKind::None) << "slot 1: the ZX-bus adapter waits for ISA I2";
    EXPECT_EQ(static_cast<CardKind>(c.slot[1].kind), CardKind::Ne2000) << "owner decision 2026-10-02 (network Q1 = B)";
    EXPECT_EQ(static_cast<Ne2000Chip>(c.slot[1].chip), Ne2000Chip::Rtl8019as);
    EXPECT_EQ(c.slot[1].base, 0x300);
    EXPECT_EQ(c.slot[1].irq, 3);
    EXPECT_EQ(c.slot[1].macAuto, 1);
}

TEST(IsaSlotConfig_Test, ParseKind_NamesAndCase)
{
    CardKind kind = CardKind::None;
    for (const char* name : {"NONE", "ZXBUS", "RAM", "NE2000", "EL3C509B", "SPRINTERESP", "MODEM", "DUAL16552"})
    {
        ASSERT_TRUE(ParseKind(name, kind)) << name;
        EXPECT_STREQ(KindName(kind), name);
    }
    ASSERT_TRUE(ParseKind(" ne2000 ", kind));
    EXPECT_EQ(kind, CardKind::Ne2000);
    EXPECT_EQ(KindKey(kind), "ne2000");
    EXPECT_FALSE(ParseKind("NE3000", kind));
}

TEST(IsaSlotConfig_Test, ParseChipAndBase)
{
    Ne2000Chip chip = Ne2000Chip::Ne1000;
    ASSERT_TRUE(ParseChip("rtl8019as", chip));
    EXPECT_EQ(chip, Ne2000Chip::Rtl8019as);
    ASSERT_TRUE(ParseChip("UM9003", chip));
    EXPECT_EQ(chip, Ne2000Chip::Um9003);
    EXPECT_FALSE(ParseChip("3c509", chip));

    uint16_t base = 0;
    for (const char* text : {"#300", "0x300", "300h", "768"})
    {
        ASSERT_TRUE(ParseNe2000Base(text, base)) << text;
        EXPECT_EQ(base, 0x300) << text;
    }
    ASSERT_TRUE(ParseNe2000Base("#200", base));
    ASSERT_TRUE(ParseNe2000Base("#3E0", base));
    EXPECT_FALSE(ParseNe2000Base("#310", base)) << "steps of #20";
    EXPECT_FALSE(ParseNe2000Base("#400", base));
    EXPECT_FALSE(ParseNe2000Base("#1E0", base));
}

TEST(IsaSlotConfig_Test, Mac_AutoAndExplicit)
{
    SlotConfig slot{};
    ASSERT_TRUE(ParseMac("auto", slot));
    EXPECT_EQ(slot.macAuto, 1);
    uint8_t mac[6];
    EffectiveMac(slot, 1, 3, mac);
    const uint8_t expected[6] = {0x02, 0x53, 0x50, 0x00, 0x03, 0x02};
    EXPECT_EQ(std::memcmp(mac, expected, 6), 0) << "02:53:50:00:<instance>:<slot>";

    ASSERT_TRUE(ParseMac("00:e0:4c:12:34:56", slot));
    EXPECT_EQ(slot.macAuto, 0);
    EffectiveMac(slot, 1, 3, mac);
    const uint8_t realtek[6] = {0x00, 0xE0, 0x4C, 0x12, 0x34, 0x56};
    EXPECT_EQ(std::memcmp(mac, realtek, 6), 0);
    EXPECT_TRUE(ParseMac("02-00-00-00-00-01", slot));
    EXPECT_FALSE(ParseMac("01:00:5e:00:00:01", slot)) << "a group address is no station address";
    EXPECT_FALSE(ParseMac("00:11:22:33:44", slot));
    EXPECT_FALSE(ParseMac("00:11:22:33:44:55:66", slot));
}

// Network phase SN5: the 3C509B in a slot - its two verified boards, a base in steps of #10, the kind is built
TEST(IsaSlotConfig_Test, El3c509b_ChipBaseAndAvailability)
{
    El3Chip chip = El3Chip::Tp;
    ASSERT_TRUE(ParseEl3Chip("tpo", chip));
    EXPECT_EQ(chip, El3Chip::Tpo);
    ASSERT_TRUE(ParseEl3Chip("3C509B-TP", chip));
    EXPECT_EQ(chip, El3Chip::Tp);
    EXPECT_FALSE(ParseEl3Chip("COMBO", chip)) << "only the boards the kit verified";
    EXPECT_STREQ(El3ChipName(El3Chip::Tpo), "3C509B-TPO");

    uint16_t base = 0;
    ASSERT_TRUE(ParseSlotBase("#310", CardKind::El3c509b, base)) << "the 3C509B's base: #200 + 16 x n";
    EXPECT_EQ(base, 0x310);
    EXPECT_FALSE(ParseSlotBase("#310", CardKind::Ne2000, base)) << "the NE2000's: steps of #20";
    EXPECT_FALSE(ParseSlotBase("#3F0", CardKind::El3c509b, base)) << "#3F0 would be the EISA code 1Fh";
    EXPECT_FALSE(ParseSlotBase("#308", CardKind::El3c509b, base));

    SlotConfig slot{};
    slot.kind = static_cast<uint8_t>(CardKind::El3c509b);
    slot.chip = static_cast<uint8_t>(El3Chip::Tp);
    EXPECT_EQ(SlotChipName(slot), "3C509B-TP");
    slot.kind = static_cast<uint8_t>(CardKind::Ne2000);
    slot.chip = static_cast<uint8_t>(Ne2000Chip::Um9003);
    EXPECT_EQ(SlotChipName(slot), "UM9003");

    std::string why;
    EXPECT_TRUE(KindAvailable(CardKind::El3c509b, &why)) << why;
    EXPECT_FALSE(KindAvailable(CardKind::Modem, &why));
}
