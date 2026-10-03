// The Sprinter's ISA bus on its own (sprinterisabus.h; docs/inprogress/2026-10-02-sprinter-isa/tdd.md §11
// T-ISA-1..5, the TTD blob of §9 and the population rules of §5)

#include <gtest/gtest.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "emulator/io/sprinter/isa/sprinterisabus.h"

using sprinterisa::CardKind;
using sprinterisa::IIsaCard;
using sprinterisa::IsaCycle;

namespace
{
/// A card that records what reaches it: I/O answers at `ioBase .. ioBase + 15` with `ioBase + register`'s
/// low byte, memory answers from a small RAM at `memBase`, reads count (Peek must not)
class FakeCard : public IIsaCard
{
public:
    explicit FakeCard(const char* kind = "ne2000") : _kind(kind) {}

    const char* Kind() const override { return _kind; }
    bool IoRead(const IsaCycle& c, uint8_t& value) override
    {
        ++ioReads;
        last = c;
        if (c.aen || !Decodes(c.address))
            return false;
        value = static_cast<uint8_t>(c.address & 0xFF);
        return true;
    }
    bool IoWrite(const IsaCycle& c, uint8_t value) override
    {
        last = c;
        if (c.aen || !Decodes(c.address))
            return false;
        writes.push_back({c.address, value});
        return true;
    }
    bool MemRead(const IsaCycle& c, uint8_t& value) override
    {
        ++memReads;
        if (c.address < memBase || c.address >= memBase + ram.size())
            return false;
        value = ram[c.address - memBase];
        return true;
    }
    bool MemWrite(const IsaCycle& c, uint8_t value) override
    {
        if (c.address < memBase || c.address >= memBase + ram.size())
            return false;
        ram[c.address - memBase] = value;
        return true;
    }
    bool IoPeek(uint32_t address, uint8_t& value) const override
    {
        if (!Decodes(address))
            return false;
        value = static_cast<uint8_t>(address & 0xFF);
        return true;
    }
    bool MemPeek(uint32_t address, uint8_t& value) const override
    {
        if (address < memBase || address >= memBase + ram.size())
            return false;
        value = ram[address - memBase];
        return true;
    }
    void SetReset(bool asserted) override { resets.push_back(asserted); }

    bool Decodes(uint32_t address) const { return (address & 0x3FF) >= ioBase && (address & 0x3FF) < ioBase + 16u; }

    const char* _kind;
    uint32_t ioBase = 0x300;
    uint32_t memBase = 0xDC000;
    std::vector<uint8_t> ram = std::vector<uint8_t>(16, 0xFF);
    int ioReads = 0;
    int memReads = 0;
    IsaCycle last;
    std::vector<std::pair<uint32_t, uint8_t>> writes;
    std::vector<bool> resets;
};
}  // namespace

// T-ISA-1: page -> slot / space for #D0 / #D2 / #D4 / #D6; nothing for other pages
TEST(SprinterIsaBus_Test, PageToSlot_OnlyTheFourIsaPages)
{
    struct Row
    {
        uint8_t page;
        SprinterIsaBus::Space space;
        int slot;
    };
    for (const Row& r : {Row{0xD0, SprinterIsaBus::Space::Memory, 0}, Row{0xD2, SprinterIsaBus::Space::Memory, 1},
                         Row{0xD4, SprinterIsaBus::Space::Io, 0}, Row{0xD6, SprinterIsaBus::Space::Io, 1}})
    {
        SprinterIsaBus::Space space = SprinterIsaBus::Space::Memory;
        int slot = -1;
        ASSERT_TRUE(SprinterIsaBus::PageToSlot(r.page, space, slot)) << int(r.page);
        EXPECT_EQ(space, r.space) << int(r.page);
        EXPECT_EQ(slot, r.slot) << int(r.page);
        EXPECT_EQ(SprinterIsaBus::SlotPage(r.space, r.slot), r.page);
    }
    for (int page = 0; page < 256; ++page)
    {
        if (page == 0xD0 || page == 0xD2 || page == 0xD4 || page == 0xD6)
            continue;
        SprinterIsaBus::Space space;
        int slot;
        EXPECT_FALSE(SprinterIsaBus::PageToSlot(static_cast<uint8_t>(page), space, slot)) << page;
    }
}

// T-ISA-2: address = latch bits 5-0 << 14 | A13-A0 (research §4.3 worked examples)
TEST(SprinterIsaBus_Test, Address_LatchHighBitsAndWindowOffset)
{
    SprinterIsaBus bus;
    bus.WriteLatch(0x00);
    EXPECT_EQ(bus.Address(0x00BB), 0x000BBu) << "GS status, slot 1";
    EXPECT_EQ(bus.Address(0x03E8), 0x003E8u) << "16550 at COM3";
    EXPECT_EQ(bus.Address(0x0224), 0x00224u) << "Sound Blaster mixer";
    bus.WriteLatch(0x37);
    EXPECT_EQ(bus.Address(0x0000), 0xDC000u) << "ISA memory #DC000";
    EXPECT_EQ(bus.Address(0x3FFF), 0xDFFFFu);
    bus.WriteLatch(0x3F | SprinterIsaBus::kLatchAen);
    EXPECT_EQ(bus.Address(0x1234), 0xFD234u) << "AEN is not an address bit";
}

// T-ISA-3: empty slot reads #FF, writes vanish; a card that does not drive the bus reads #FF
TEST(SprinterIsaBus_Test, EmptySlotAndSilentCard_ReadFF)
{
    SprinterIsaBus bus;
    EXPECT_EQ(bus.Read(SprinterIsaBus::Space::Io, 0, 0x300), 0xFF);
    EXPECT_EQ(bus.Read(SprinterIsaBus::Space::Memory, 1, 0x0000), 0xFF);
    bus.Write(SprinterIsaBus::Space::Io, 0, 0x300, 0x21);  // nothing to receive it

    auto card = std::make_unique<FakeCard>();
    FakeCard* fake = card.get();
    bus.Fit(1, std::move(card));
    EXPECT_EQ(bus.Read(SprinterIsaBus::Space::Io, 1, 0x30A), 0x0A) << "the card answers its window";
    EXPECT_EQ(bus.Read(SprinterIsaBus::Space::Io, 1, 0x200), 0xFF) << "outside its window: the pull-ups";
    EXPECT_EQ(bus.Read(SprinterIsaBus::Space::Io, 0, 0x30A), 0xFF) << "the other slot is empty";
    bus.Write(SprinterIsaBus::Space::Io, 1, 0x300, 0x21);
    ASSERT_EQ(fake->writes.size(), 1u);
    EXPECT_EQ(fake->writes[0].first, 0x300u);
    EXPECT_EQ(fake->writes[0].second, 0x21);
    EXPECT_EQ(bus.GetCounters(1).ioReads, 2u);
    EXPECT_EQ(bus.GetCounters(1).ioWrites, 1u);
    EXPECT_EQ(bus.GetCounters(0).ioReads, 2u);
}

// T-ISA-4: the RESET edge reaches both cards; while it is held every cycle reads #FF; AEN travels with the cycle
TEST(SprinterIsaBus_Test, ResetEdgeReachesBothSlots_AenTravels)
{
    SprinterIsaBus bus;
    auto a = std::make_unique<FakeCard>();
    auto b = std::make_unique<FakeCard>();
    FakeCard* first = a.get();
    FakeCard* second = b.get();
    bus.Fit(0, std::move(a));
    bus.Fit(1, std::move(b));

    bus.WriteLatch(0xC0);  // RESET + AEN (the Wi-Fi kit's pulse)
    EXPECT_EQ(first->resets, std::vector<bool>({true}));
    EXPECT_EQ(second->resets, std::vector<bool>({true}));
    EXPECT_EQ(bus.Read(SprinterIsaBus::Space::Io, 0, 0x30A), 0xFF) << "held in reset";
    EXPECT_EQ(first->ioReads, 0) << "no cycle reaches a card in reset";
    bus.WriteLatch(0xC0);  // no edge
    EXPECT_EQ(first->resets.size(), 1u);
    bus.WriteLatch(0x40);  // RESET released, AEN still high
    EXPECT_EQ(first->resets, std::vector<bool>({true, false}));
    EXPECT_EQ(bus.Read(SprinterIsaBus::Space::Io, 0, 0x30A), 0xFF) << "an I/O card ignores AEN cycles";
    EXPECT_TRUE(first->last.aen);
    bus.WriteLatch(0x00);
    EXPECT_EQ(bus.Read(SprinterIsaBus::Space::Io, 0, 0x30A), 0x0A);
    EXPECT_FALSE(first->last.aen);
    EXPECT_EQ(bus.GetCounters(0).resetPulses, 1u);
    EXPECT_EQ(bus.GetCounters(1).resetPulses, 1u);
}

// T-ISA-5: Peek has no side effects
TEST(SprinterIsaBus_Test, Peek_NoSideEffect)
{
    SprinterIsaBus bus;
    auto card = std::make_unique<FakeCard>();
    FakeCard* fake = card.get();
    bus.Fit(0, std::move(card));
    EXPECT_EQ(bus.Peek(SprinterIsaBus::Space::Io, 0, 0x30B), 0x0B);
    bus.WriteLatch(0x37);
    fake->ram[5] = 0x42;
    EXPECT_EQ(bus.Peek(SprinterIsaBus::Space::Memory, 0, 0x0005), 0x42);
    EXPECT_EQ(bus.PeekAt(SprinterIsaBus::Space::Memory, 0, 0xDC005), 0x42);
    EXPECT_EQ(fake->ioReads, 0);
    EXPECT_EQ(fake->memReads, 0);
    EXPECT_EQ(bus.GetCounters(0).ioReads + bus.GetCounters(0).memReads, 0u);
    EXPECT_EQ(bus.Peek(SprinterIsaBus::Space::Io, 1, 0x30B), 0xFF) << "empty slot";
}

// Every real cycle reaches the tracer (the port trace's isa_io / isa_mem rows), peeks do not
TEST(SprinterIsaBus_Test, Tracer_SeesCyclesNotPeeks)
{
    SprinterIsaBus bus;
    bus.Fit(1, std::make_unique<FakeCard>());
    std::vector<std::string> seen;
    bus.SetTracer([&](bool write, SprinterIsaBus::Space space, int slot, uint32_t address, uint8_t value) {
        char line[64];
        std::snprintf(line, sizeof(line), "%s %s %d %05X %02X", write ? "W" : "R",
                      space == SprinterIsaBus::Space::Io ? "io" : "mem", slot, address, value);
        seen.push_back(line);
    });
    bus.Read(SprinterIsaBus::Space::Io, 1, 0x30A);
    bus.Write(SprinterIsaBus::Space::Io, 1, 0x300, 0x21);
    bus.Peek(SprinterIsaBus::Space::Io, 1, 0x30A);
    EXPECT_EQ(seen, std::vector<std::string>({"R io 1 0030A 0A", "W io 1 00300 21"}));
}

// T-ISA-13 (bus part): a configured kind this build does not have is refused with the reason; a fitted card clears it
TEST(SprinterIsaBus_Test, Configure_UnavailableKindRefusedWithReason)
{
    SprinterIsaBus bus;
    sprinterisa::IsaConfig config = sprinterisa::DefaultConfig();
    config.slot[0].kind = static_cast<uint8_t>(CardKind::Ram);
    bus.Configure(config);
    EXPECT_NE(bus.Refusal(0).find("I3"), std::string::npos) << bus.Refusal(0);
    EXPECT_EQ(bus.Card(0), nullptr);
    const StateNode report = bus.Describe();
    const StateNode* slots = report.find("slots");
    ASSERT_NE(slots, nullptr);
    ASSERT_EQ(slots->items.size(), 2u);
    EXPECT_EQ(slots->items[0].find("configured")->s, "ram");
    EXPECT_EQ(slots->items[0].find("card")->s, "none");
    ASSERT_NE(slots->items[0].find("not_fitted"), nullptr);
    EXPECT_EQ(slots->items[0].find("page_io")->s, "#D4");
    EXPECT_EQ(slots->items[1].find("page_io")->s, "#D6");
    EXPECT_EQ(slots->items[1].find("page_mem")->s, "#D2");

    bus.Fit(0, std::make_unique<FakeCard>("ram"));
    EXPECT_TRUE(bus.Refusal(0).empty());
}

// TTD blob 33: round trip of the latch; another population is refused (nothing loaded)
TEST(SprinterIsaBus_Test, State_RoundTripAndPopulationGuard)
{
    SprinterIsaBus bus;
    bus.Fit(1, std::make_unique<FakeCard>("ne2000"));
    bus.WriteLatch(0x37);
    std::vector<uint8_t> blob(bus.StateSize());
    ASSERT_EQ(blob.size(), 4u);
    bus.SaveState(blob.data());
    EXPECT_EQ(blob, std::vector<uint8_t>({SprinterIsaBus::kStateVersion, 0x37, 0, 3}));

    bus.WriteLatch(0x00);
    std::string why;
    ASSERT_TRUE(bus.LoadState(blob.data(), blob.size(), why)) << why;
    EXPECT_EQ(bus.Latch(), 0x37);

    SprinterIsaBus other;  // both slots empty
    EXPECT_FALSE(other.PopulationMatches(blob.data(), blob.size(), why));
    EXPECT_NE(why.find("ISA slot 2 mismatch: recorded with ne2000, fitted: none"), std::string::npos) << why;
    EXPECT_FALSE(other.LoadState(blob.data(), blob.size(), why));
    EXPECT_EQ(other.Latch(), 0x00) << "a refused blob changes nothing";
}

// The latch has no reset input; power-on starts it at 0
TEST(SprinterIsaBus_Test, PowerOn_LatchZero)
{
    SprinterIsaBus bus;
    auto card = std::make_unique<FakeCard>();
    FakeCard* fake = card.get();
    bus.Fit(0, std::move(card));
    bus.WriteLatch(0x80);
    bus.PowerOn();
    EXPECT_EQ(bus.Latch(), 0x00);
    EXPECT_EQ(fake->resets, std::vector<bool>({true, false})) << "power-on releases RESET";
}
