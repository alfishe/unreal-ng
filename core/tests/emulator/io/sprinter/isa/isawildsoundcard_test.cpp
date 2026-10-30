// The ISA Wild Sound card in a Sprinter slot (isawildsoundcard.h; docs/inprogress/2026-10-09-wildsound/design.md
// §2.1, §8, §11): port decode, AEN, RESET DRV, the slot report, the state in the SprinterIsa blob, the card kind

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "emulator/io/sprinter/isa/cards/isawildsoundcard.h"
#include "emulator/io/sprinter/isa/isaslotconfig.h"
#include "emulator/io/sprinter/isa/sprinterisabus.h"

using sprinterisa::CardKind;
using sprinterisa::IsaCycle;
using sprinterisa::IsaWildSoundCard;

namespace
{

void Out(IsaWildSoundCard& card, uint32_t address, uint8_t value, bool aen = false)
{
    card.IoWrite(IsaCycle{address, aen}, value);
}

uint8_t In(IsaWildSoundCard& card, uint32_t address)
{
    uint8_t v = 0xEE;
    EXPECT_TRUE(card.IoRead(IsaCycle{address, false}, v));
    return v;
}

void Unlock(IsaWildSoundCard& card, uint32_t base = 0)
{
    Out(card, base, 0xF0);
    for (const char* c = wildsound::WildSoundCore::kLockString; *c; ++c)
        Out(card, base + 1, static_cast<uint8_t>(*c));
    Out(card, base + 1, 0);
    Out(card, base + 1, 0);
}

}  // namespace

TEST(IsaWildSoundCard_Test, ProsiakProbeAnswersDf)
{
    IsaWildSoundCard card(nullptr);
    Unlock(card);
    EXPECT_EQ(In(card, 1), 0xDF);
    EXPECT_EQ(In(card, 1), '1');  // "165931.23042021"
}

TEST(IsaWildSoundCard_Test, DecodeMirrorsEvery400AndIgnoresOtherAddresses)
{
    IsaWildSoundCard card(nullptr);
    Out(card, 0x400, 0x07);  // mirror of port 0
    Out(card, 0x401, 0x2A);
    Out(card, 0x000, 0x07);
    EXPECT_EQ(In(card, 0x001), 0x2A);

    uint8_t v = 0;
    EXPECT_FALSE(card.IoRead(IsaCycle{0x002, false}, v)) << "#002 is not the card's";
    EXPECT_FALSE(card.IoRead(IsaCycle{0x300, false}, v));

    // AEN = 1: the card's decoder ignores the cycle
    Out(card, 0x000, 0x08, true);
    EXPECT_EQ(card.Core().SelectedRegister(), 0x07);
    EXPECT_FALSE(card.IoRead(IsaCycle{0x001, true}, v));

    uint32_t first = 0, last = 0;
    ASSERT_TRUE(card.IoRange(first, last));
    EXPECT_EQ(first, 0u);
    EXPECT_EQ(last, 1u);
    EXPECT_STREQ(card.RegisterName(true, 0, true), "AY register select");
    EXPECT_STREQ(card.RegisterName(true, 1, true), "AY data");
}

TEST(IsaWildSoundCard_Test, PeekHasNoSideEffect)
{
    IsaWildSoundCard card(nullptr);
    Out(card, 0, 0xE0);
    uint8_t p = 0;
    ASSERT_TRUE(card.IoPeek(1, p));
    EXPECT_EQ(p, 0x55);
    ASSERT_TRUE(card.IoPeek(1, p));
    EXPECT_EQ(p, 0x55);
    EXPECT_EQ(In(card, 1), 0x55);
    EXPECT_EQ(In(card, 1), 0xAA);
}

TEST(IsaWildSoundCard_Test, ResetDrvRestartsTheFirmwareOnRelease)
{
    IsaWildSoundCard card(nullptr);
    Unlock(card);
    ASSERT_TRUE(card.Core().Unlocked());
    card.SetReset(true);
    EXPECT_TRUE(card.Core().Unlocked()) << "held: nothing happens yet";
    card.SetReset(false);
    EXPECT_FALSE(card.Core().Unlocked());
}

TEST(IsaWildSoundCard_Test, StateRoundTripAndReport)
{
    IsaWildSoundCard a(nullptr);
    Unlock(a);
    Out(a, 0, 0x08);
    Out(a, 1, 0x0F);
    std::vector<uint8_t> state(a.StateSize());
    a.SaveState(state.data());
    IsaWildSoundCard b(nullptr);
    b.LoadState(state.data());
    EXPECT_TRUE(b.Core().Unlocked());
    EXPECT_EQ(b.Core().ChipRegister(0, 8), 0x0F);

    StateNode out = StateNode::Object();
    a.Describe(out);
    const StateNode* ws = out.find("wild_sound");
    ASSERT_NE(ws, nullptr);
    ASSERT_NE(ws->find("unlocked"), nullptr);
    EXPECT_TRUE(ws->find("unlocked")->b);
    EXPECT_EQ(ws->find("mode")->s, "ay");
}

TEST(IsaWildSoundCard_Test, KindParsesAndIsAvailable)
{
    CardKind kind = CardKind::None;
    ASSERT_TRUE(sprinterisa::ParseKind("wildsound", kind));
    EXPECT_EQ(kind, CardKind::WildSound);
    EXPECT_EQ(static_cast<int>(kind), 8) << "stored in the TTD blob: never renumber";
    EXPECT_STREQ(sprinterisa::KindName(CardKind::WildSound), "WILDSOUND");
    std::string why;
    EXPECT_TRUE(sprinterisa::KindAvailable(CardKind::WildSound, &why)) << why;
}

TEST(IsaWildSoundCard_Test, FitsTheBusAndAnswersThroughIt)
{
    SprinterIsaBus bus;
    sprinterisa::IsaConfig config{};
    config.slot[0].kind = static_cast<uint8_t>(CardKind::WildSound);
    bus.Configure(config);
    bus.Fit(0, std::make_unique<IsaWildSoundCard>(nullptr));
    // Slot 0's I/O page, #9FBD = 0: the bus forms ISA I/O #00000 / #00001 from CPU #C000 / #C001
    bus.WriteLatch(0x00);
    bus.WriteAt(SprinterIsaBus::Space::Io, 0, 0x0000, 0xE0);
    EXPECT_EQ(bus.ReadAt(SprinterIsaBus::Space::Io, 0, 0x0001), 0x55);
    EXPECT_EQ(bus.ReadAt(SprinterIsaBus::Space::Io, 0, 0x0001), 0xAA);
}
