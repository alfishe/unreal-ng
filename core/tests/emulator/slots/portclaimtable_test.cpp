// ZX-bus slots port claim table (core/src/emulator/slots/portclaimtable.h): the cycle resolution of
// docs/inprogress/2026-10-03-zx-bus-slots/architecture.md §4.3 (IORQGE, arbitration, cycle detection, shadowing,
// read rule, ROM-fetch lock) over the SL-1 reference data, and its use by PortDecoder's full-decode observers.
// Test plan: tdd.md §2.2.

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/ports/models/portdecoder_pentagon128.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/portclaimtable.h"
#include "emulator/slots/refdata/refdata.h"

using namespace slots;

namespace
{

/// A card (or the stand-in of a board device): records every cycle it sees
class FakeDevice final : public PortDevice
{
public:
    explicit FakeDevice(uint8_t readValue = 0xFF) : readValue(readValue) {}

    uint8_t portDeviceInMethod(uint16_t port) override
    {
        reads.push_back(port);
        return readValue;
    }

    void portDeviceOutMethod(uint16_t port, uint8_t value) override
    {
        writes.emplace_back(port, value);
    }

    uint8_t readValue;
    std::vector<uint16_t> reads;
    std::vector<std::pair<uint16_t, uint8_t>> writes;
};

/// The ROM-fetch lock and DOS-gated claims read their signals from here
class FakeSignals final : public IClaimSignals
{
public:
    uint16_t LastM1Address() const override { return m1; }
    bool DosActive() const override { return dos; }

    uint16_t m1 = 0x8000;
    bool dos = false;
};

const MachineDef& Machine(MEM_MODEL model)
{
    for (const MachineDef& machine : refdata::All().machines)
    {
        if (machine.model == model)
            return machine;
    }
    ADD_FAILURE() << "no MachineDef for model " << static_cast<int>(model);
    static const MachineDef none{};
    return none;
}

const BusDef& Bus(MEM_MODEL model, const char* id)
{
    for (const BusDef& bus : Machine(model).buses)
    {
        if (std::strcmp(bus.id, id) == 0)
            return bus;
    }
    ADD_FAILURE() << "no bus " << id;
    static const BusDef none{};
    return none;
}

const BuiltInDef& BuiltIn(MEM_MODEL model, const char* id)
{
    for (const BuiltInDef& builtIn : Machine(model).builtIns)
    {
        if (std::strcmp(builtIn.id, id) == 0)
            return builtIn;
    }
    ADD_FAILURE() << "no built-in " << id;
    static const BuiltInDef none{};
    return none;
}

const CardDef& Card(const char* id)
{
    for (const CardDef& card : refdata::All().cards)
    {
        if (std::strcmp(card.id, id) == 0)
            return card;
    }
    ADD_FAILURE() << "no card " << id;
    static const CardDef none{};
    return none;
}

/// Every claim of a card (all DIP functions on: the MultiSound's default) into slot `slot`
void Fit(PortClaimTable& table, const CardDef& card, uint8_t slot, PortDevice* owner)
{
    for (const PortClaim& claim : card.claims)
        table.Add(MakeClaimEntry(claim, slot, owner, card.detection));
}

/// The board decoder stand-in of a read: records the cycle, drives `value` when `drives`
struct BoardRead
{
    bool drives = true;
    uint8_t value = 0xFF;
    int calls = 0;
    BoardCycle lastCycle = BoardCycle::Full;

    bool operator()(BoardCycle cycle, uint8_t& out)
    {
        calls++;
        lastCycle = cycle;
        out = value;
        return drives;
    }
};

} // namespace

/// No claim on a port: the access is the board's alone and the table is not scanned (one bit test)
TEST(PortClaimTable_Test, UnclaimedPortSkipsTable)
{
    PortClaimTable table;
    for (uint32_t port = 0; port < 0x10000; port++)
        ASSERT_FALSE(table.IsClaimed(static_cast<uint16_t>(port)));

    FakeDevice card;
    table.Configure(Bus(MM_PENTAGON, "zxbus"));
    Fit(table, Card("zxnetusb"), 0, &card);
    table.Build();

    EXPECT_TRUE(table.IsClaimed(0x00AB));
    EXPECT_TRUE(table.IsClaimed(0xFFAB));
    EXPECT_FALSE(table.IsClaimed(0x00AA));
    EXPECT_FALSE(table.IsClaimed(0x00FE));

    // The scan trap sits at the head of every bucket: any bucket scan reaches it
    FakeDevice trap;
    table.SetScanTrapForTests(&trap);
    table.Build();
    EXPECT_FALSE(table.IsClaimed(0x00FE)) << "the trap leaves the claimed-port bitmap alone";

    int boardWrites = 0;
    table.Write(0x00FE, 0x07, [&](BoardCycle cycle)
    {
        EXPECT_EQ(cycle, BoardCycle::Full);
        boardWrites++;
    });
    BoardRead board;
    board.value = 0xBF;
    const ReadResult read = table.Read(0x7FFE, board);

    EXPECT_EQ(boardWrites, 1);
    EXPECT_EQ(board.calls, 1);
    EXPECT_EQ(read.value, 0xBF);
    EXPECT_EQ(read.drivers, 1);
    EXPECT_TRUE(card.writes.empty());
    EXPECT_TRUE(card.reads.empty());
    EXPECT_TRUE(trap.writes.empty()) << "an unclaimed write must not scan the table";
    EXPECT_TRUE(trap.reads.empty()) << "an unclaimed read must not scan the table";

    // A claimed port does scan: the trap sees it (proof the trap works)
    table.Write(0x00AB, 0x01, [](BoardCycle) {});
    EXPECT_EQ(trap.writes.size(), 1u);
    ASSERT_NE(table.FirstMatch(0x00AB), nullptr);
    EXPECT_EQ(table.FirstMatch(0x00AB)->owner, &trap);
}

/// The same through PortDecoder's Z80 funnel taps: an unclaimed IN / OUT never reaches the table
TEST(PortClaimTable_Test, UnclaimedPortSkipsTableInPortDecoder)
{
    EmulatorContext context(LoggerLevel::LogError);
    PortDecoder_Pentagon128 decoder(&context);
    FakeDevice card(0x5A);
    ASSERT_TRUE(decoder.RegisterFullDecodeLowBytePort(0xC4, &card));

    FakeDevice trap(0x77);
    decoder.SetFullDecodeScanTrapForTests(&trap);
    bool handled = true;
    bool claimsBus = true;
    EXPECT_EQ(decoder.NotifyFullDecodeIn(0x00FE, handled, claimsBus), 0xFF);
    EXPECT_FALSE(handled);
    EXPECT_FALSE(claimsBus);
    decoder.NotifyFullDecodeOut(0x7FFD, 0x10);
    EXPECT_FALSE(decoder.IsLowByteClaimedByFullDecodeDevice(0x00FE));
    uint16_t decoded = 0x00FE;
    PortDecodeDisposition disp;
    EXPECT_FALSE(decoder.OverrideDecodeForFullDecodeClaim(0x00FE, decoded, disp, true));
    EXPECT_TRUE(trap.reads.empty()) << "an unclaimed IN must not scan the table";
    EXPECT_TRUE(trap.writes.empty()) << "an unclaimed OUT must not scan the table";
    EXPECT_TRUE(card.reads.empty());
    EXPECT_TRUE(card.writes.empty());

    // A claimed port scans: the trap (first in the bucket) answers instead of the card
    EXPECT_EQ(decoder.NotifyFullDecodeIn(0x12C4, handled, claimsBus), 0x77);
    EXPECT_EQ(trap.reads.size(), 1u);

    // Without the trap the card answers (every high-byte alias of the low byte)
    decoder.SetFullDecodeScanTrapForTests(nullptr);
    EXPECT_EQ(decoder.NotifyFullDecodeIn(0x12C4, handled, claimsBus), 0x5A);
    EXPECT_TRUE(handled);
    EXPECT_EQ(decoder.GetCachedFullDecodeInValue(0x12C4), 0x5A);

    // The cache follows every IN, also an unclaimed one (as before the table)
    decoder.NotifyFullDecodeIn(0x00FE, handled, claimsBus);
    EXPECT_EQ(decoder.GetCachedFullDecodeInValue(0x00FE), 0xFF);
    EXPECT_EQ(decoder.GetCachedFullDecodeInValue(0x12C4), 0xFF);
}

/// Exact 16-bit observers come before low-byte observers (the lookup order of the two tables the claim table
/// replaced); duplicates are refused; unregistering needs the registering device
TEST(PortClaimTable_Test, LegacyObserverOrderAndRegistration)
{
    EmulatorContext context(LoggerLevel::LogError);
    PortDecoder_Pentagon128 decoder(&context);
    FakeDevice lowByte(0x11);
    FakeDevice exact(0x22);
    FakeDevice other;

    ASSERT_TRUE(decoder.RegisterFullDecodeLowBytePort(0x7F, &lowByte));
    EXPECT_FALSE(decoder.RegisterFullDecodeLowBytePort(0x7F, &other));
    ASSERT_TRUE(decoder.RegisterFullDecodePort(0xFF7F, &exact));
    EXPECT_FALSE(decoder.RegisterFullDecodePort(0xFF7F, &other));
    EXPECT_FALSE(decoder.RegisterFullDecodePort(0x1234, nullptr));

    bool handled = false;
    bool claimsBus = false;
    EXPECT_EQ(decoder.NotifyFullDecodeIn(0xFF7F, handled, claimsBus), 0x22);
    EXPECT_EQ(decoder.NotifyFullDecodeIn(0xFE7F, handled, claimsBus), 0x11);
    decoder.NotifyFullDecodeOut(0xFF7F, 0x01);
    decoder.NotifyFullDecodeOut(0x007F, 0x02);
    ASSERT_EQ(exact.writes.size(), 1u);
    ASSERT_EQ(lowByte.writes.size(), 1u);
    EXPECT_EQ(lowByte.writes[0].first, 0x007F);
    EXPECT_TRUE(decoder.IsLowByteClaimedByFullDecodeDevice(0xFF7F));

    decoder.UnregisterFullDecodeLowBytePort(0x7F, &other);   // not the owner: no effect
    EXPECT_TRUE(decoder.IsLowByteClaimedByFullDecodeDevice(0x007F));
    decoder.UnregisterFullDecodeLowBytePort(0x7F, &lowByte);
    EXPECT_FALSE(decoder.IsLowByteClaimedByFullDecodeDevice(0x007F));
    EXPECT_FALSE(decoder.GetFullDecodeClaims().IsClaimed(0x007F));
    EXPECT_TRUE(decoder.GetFullDecodeClaims().IsClaimed(0xFF7F));
    decoder.UnregisterFullDecodePort(0xFF7F, &exact);
    EXPECT_FALSE(decoder.GetFullDecodeClaims().IsClaimed(0xFF7F));
}

/// The claim found by the Z80 tap is reused by the model decode's override of the same cycle; a registration
/// change drops it (it must never answer for a device that left)
TEST(PortClaimTable_Test, ClaimMemoFollowsRegistrationChanges)
{
    class ReadClaimingDevice final : public PortDevice
    {
    public:
        uint8_t portDeviceInMethod(uint16_t) override { return 0x5A; }
        void portDeviceOutMethod(uint16_t, uint8_t) override {}
        bool portDeviceClaimsRead(uint16_t) override { return true; }
    };

    EmulatorContext context(LoggerLevel::LogError);
    PortDecoder_Pentagon128 decoder(&context);
    ReadClaimingDevice claiming;
    FakeDevice silent;   // portDeviceClaimsRead: false
    ASSERT_TRUE(decoder.RegisterFullDecodeLowBytePort(0xC4, &claiming));

    bool handled = false;
    bool claimsBus = false;
    decoder.NotifyFullDecodeIn(0x00C4, handled, claimsBus);
    EXPECT_TRUE(claimsBus);
    uint16_t decoded = 0x00FE;
    PortDecodeDisposition disp;
    EXPECT_TRUE(decoder.OverrideDecodeForFullDecodeClaim(0x00C4, decoded, disp, true));
    EXPECT_EQ(decoded, 0x0000);

    // Swap the card without a new tap: the override must see the new (non-claiming) device
    decoder.UnregisterFullDecodeLowBytePort(0xC4, &claiming);
    ASSERT_TRUE(decoder.RegisterFullDecodeLowBytePort(0xC4, &silent));
    decoded = 0x00FE;
    PortDecodeDisposition disp2;
    EXPECT_FALSE(decoder.OverrideDecodeForFullDecodeClaim(0x00C4, decoded, disp2, true));
    EXPECT_EQ(decoded, 0x00FE);

    // A write always stands the model decode down for a registered low byte, memo or not
    decoder.NotifyFullDecodeOut(0x01C4, 0x00);
    EXPECT_TRUE(decoder.OverrideDecodeForFullDecodeClaim(0x01C4, decoded, disp2, false));
}

/// CardWins: a card driving IORQGE hides the cycle from the whole board decoder, reads and writes
TEST(PortClaimTable_Test, IorqgeSuppressesMachineDecode)
{
    PortClaimTable table;
    FakeDevice card(0x42);
    table.Configure(Bus(MM_PENTAGON, "zxbus"));
    Fit(table, Card("zxnetusb"), 0, &card);
    table.Build();

    int boardWrites = 0;
    table.Write(0x83AB, 0x99, [&](BoardCycle) { boardWrites++; });
    EXPECT_EQ(boardWrites, 0);
    ASSERT_EQ(card.writes.size(), 1u);
    EXPECT_EQ(card.writes[0], std::make_pair(uint16_t{ 0x83AB }, uint8_t{ 0x99 }));

    BoardRead board;
    board.value = 0x00;
    const ReadResult read = table.Read(0x80AB, board);
    EXPECT_EQ(board.calls, 0);
    EXPECT_EQ(read.value, 0x42);
    EXPECT_EQ(read.drivers, 1);
    EXPECT_FALSE(read.busFight);

    // None (128K edge): no suppression input, the board still decodes
    PortClaimTable plain;
    plain.Configure(Bus(MM_SPECTRUM128, "edge"));
    Fit(plain, Card("zxnetusb"), 0, &card);
    plain.Build();
    plain.Write(0x83AB, 0x01, [&](BoardCycle cycle)
    {
        EXPECT_EQ(cycle, BoardCycle::Full);
        boardWrites++;
    });
    EXPECT_EQ(boardWrites, 1);

    // UlaOnly (48K): IORQGE silences only the ULA's #FE decode
    PortClaimTable ula;
    ula.Configure(Bus(MM_SPECTRUM48, "edge"));
    Fit(ula, Card("zxnetusb"), 0, &card);
    ula.Build();
    ula.Write(0x83AB, 0x01, [&](BoardCycle cycle)
    {
        EXPECT_EQ(cycle, BoardCycle::UlaSilenced);
        boardWrites++;
    });
    EXPECT_EQ(boardWrites, 2);
}

/// A card that drives IORQGE hides the cycle from later slots; earlier slots still see it
TEST(PortClaimTable_Test, IorqgeHidesLaterSlots)
{
    PortClaimTable table;
    FakeDevice early;
    FakeDevice driver;
    FakeDevice late;
    table.Configure(Arbitration::CardWins, ReadRule::WiredAnd);
    table.Add({ .mask = 0x00FF, .match = 0x00B3, .slot = 0, .owner = &early });
    table.Add({ .mask = 0x00FF, .match = 0x00B3, .iorqge = Iorqge::Yes, .slot = 1, .owner = &driver });
    table.Add({ .mask = 0x00FF, .match = 0x00B3, .slot = 2, .owner = &late });
    table.Build();

    table.Write(0x00B3, 0x5A, [](BoardCycle) { ADD_FAILURE() << "the board is hidden"; });
    EXPECT_EQ(early.writes.size(), 1u);
    EXPECT_EQ(driver.writes.size(), 1u);
    EXPECT_TRUE(late.writes.empty());
}

/// #DFFD (MultiSound on a CardWins machine): a passive claim; the write reaches the card and the machine's paging
TEST(PortClaimTable_Test, PassiveWriteReachesBoth)
{
    PortClaimTable table;
    FakeDevice multisound;
    table.Configure(Bus(MM_PENTAGON, "zxbus"));
    Fit(table, Card("multisound"), 0, &multisound);
    table.Build();

    uint8_t boardLatch = 0;
    table.Write(0xDFFD, 0x03, [&](BoardCycle cycle)
    {
        EXPECT_EQ(cycle, BoardCycle::Full);
        boardLatch = 0x03;
    });
    EXPECT_EQ(boardLatch, 0x03);
    ASSERT_EQ(multisound.writes.size(), 1u) << "one write per device, however many of its claims match";
    EXPECT_EQ(multisound.writes[0], std::make_pair(uint16_t{ 0xDFFD }, uint8_t{ 0x03 }));

    // #FFFD is the card's IORQGE claim: the board AY gets nothing
    bool boardSaw = false;
    table.Write(0xFFFD, 0x07, [&](BoardCycle) { boardSaw = true; });
    EXPECT_FALSE(boardSaw);
    EXPECT_EQ(multisound.writes.size(), 2u);
}

/// Two drivers on one read combine by the bus's read rule and are reported as a bus fight
TEST(PortClaimTable_Test, PassiveReadWiredAnd)
{
    FakeDevice multisound(0xF3);

    // Pentagon (WiredAnd): #DFFD is passive and readable on the card; the board drives it too
    PortClaimTable table;
    table.Configure(Bus(MM_PENTAGON, "zxbus"));
    Fit(table, Card("multisound"), 0, &multisound);
    table.Build();
    ASSERT_EQ(table.GetReadRule(), ReadRule::WiredAnd);

    BoardRead board;
    board.value = 0x3E;
    ReadResult read = table.Read(0xDFFD, board);
    EXPECT_EQ(board.calls, 1);
    EXPECT_EQ(read.value, 0xF3 & 0x3E);
    EXPECT_EQ(read.drivers, 2);
    EXPECT_TRUE(read.busFight);

    // The board does not drive: the card's value alone, no fight
    board.drives = false;
    read = table.Read(0xDFFD, board);
    EXPECT_EQ(read.value, 0xF3);
    EXPECT_FALSE(read.busFight);

    // Nobody drives: the machine's floating-bus rule applies
    BoardRead silent;
    silent.drives = false;
    read = table.Read(0x00FE, silent);
    EXPECT_TRUE(read.nobody);
    EXPECT_EQ(read.drivers, 0);

    // CardOverUla (128K edge): the card wins against the board behind its resistors
    PortClaimTable edge;
    edge.Configure(Bus(MM_SPECTRUM128, "edge"));
    Fit(edge, Card("multisound"), 0, &multisound);
    edge.Build();
    BoardRead ula;
    ula.value = 0x3E;
    read = edge.Read(0xDFFD, ula);
    EXPECT_EQ(read.value, 0xF3);
    EXPECT_TRUE(read.busFight);

    // SlotOrder (ZX-Evo): the board first (modeling choice)
    PortClaimTable evo;
    evo.Configure(Arbitration::BoardWins, ReadRule::SlotOrder);
    evo.Add({ .mask = 0x00FF, .match = 0x0033, .slot = 0, .owner = &multisound });
    evo.Build();
    BoardRead fpga;
    fpga.value = 0x12;
    read = evo.Read(0x0033, fpga);
    EXPECT_EQ(read.value, 0x12);
    EXPECT_TRUE(read.busFight);
}

/// A built-in covered by a card's IORQGE claim is shadowed (CardWins): it gets no cycle and the report says so
TEST(PortClaimTable_Test, ShadowedBuiltInSilent)
{
    PortClaimTable table;
    FakeDevice multisound;
    table.Configure(Bus(MM_PENTAGON, "zxbus"));
    for (const BuiltInDef& builtIn : Machine(MM_PENTAGON).builtIns)
        table.AddBuiltIn(builtIn.id, builtIn.claims);
    Fit(table, Card("multisound"), 3, &multisound);
    table.Build();

    const PortClaimTable::BuiltInState* ay = table.FindBuiltIn("ay");
    ASSERT_NE(ay, nullptr);
    EXPECT_TRUE(ay->shadowed);
    EXPECT_EQ(ay->shadowedBySlot, 3);
    const PortClaimTable::BuiltInState* beta = table.FindBuiltIn("beta128");
    ASSERT_NE(beta, nullptr);
    EXPECT_FALSE(beta->shadowed);

    // The built-in AY gets no writes on its register and data ports
    int boardAyWrites = 0;
    table.Write(0xFFFD, 0x07, [&](BoardCycle) { boardAyWrites++; });
    table.Write(0xBFFD, 0x3F, [&](BoardCycle) { boardAyWrites++; });
    EXPECT_EQ(boardAyWrites, 0);
    EXPECT_EQ(multisound.writes.size(), 2u);

    // BoardWins (ZX-Evo): a card cannot shadow a built-in, and the MultiSound's IORQGE never hides the board
    PortClaimTable evo;
    evo.Configure(Bus(MM_ATM3, "zxbus"));
    evo.AddBuiltIn("ay", BuiltIn(MM_ATM3, "ay").claims);
    Fit(evo, Card("multisound"), 0, &multisound);
    evo.Build();
    ASSERT_NE(evo.FindBuiltIn("ay"), nullptr);
    EXPECT_FALSE(evo.FindBuiltIn("ay")->shadowed);
}

/// ROM-fetch lock: a locked claim is ignored while the last opcode fetch came from #0000-#3FFF
TEST(PortClaimTable_Test, RomFetchLock)
{
    PortClaimTable table;
    FakeSignals signals;
    FakeDevice multisound;
    table.Configure(Bus(MM_PENTAGON, "zxbus"));
    table.BindSignals(&signals);
    Fit(table, Card("multisound"), 0, &multisound);
    table.Build();

    int boardWrites = 0;
    auto board = [&](BoardCycle) { boardWrites++; };

    signals.m1 = 0x8000;   // program in RAM: the SAA #FF and SounDrive #0F claims are live
    table.Write(0x00FF, 0x11, board);
    table.Write(0x000F, 0x22, board);
    EXPECT_EQ(multisound.writes.size(), 2u);

    signals.m1 = 0x3FFF;   // the ROM's own OUT (#FF in the 48K ROM, #xx0F of TR-DOS): ignored by the card
    table.Write(0x00FF, 0x33, board);
    table.Write(0x000F, 0x44, board);
    EXPECT_EQ(multisound.writes.size(), 2u);
    EXPECT_EQ(boardWrites, 4) << "passive claims never hide the board";

    // Unlocked claims of the same card are not affected by the M1 address
    table.Write(0xDFFD, 0x01, board);
    EXPECT_EQ(multisound.writes.size(), 3u);
}

/// BoardWins: the board hides its own ports from Iorq cards; the IORQGE of a card never hides the board
TEST(PortClaimTable_Test, BoardWinsHidesBoardPortsFromIorqCards)
{
    PortClaimTable table;
    FakeDevice soundrive;
    table.Configure(Bus(MM_ATM3, "zxbus"));
    ASSERT_EQ(table.GetArbitration(), Arbitration::BoardWins);
    Fit(table, Card("soundrive"), 0, &soundrive);
    table.Build();

    // #FD is a ZX-Evo board port (porthit): dead for an Iorq card even when its claim covers it
    EXPECT_TRUE(table.IsBoardPort(0xFFFD, Dir::Out));
    EXPECT_FALSE(table.IsBoardPort(0x00F3, Dir::Out));

    PortClaimTable claimAll;
    FakeDevice iorqCard;
    claimAll.Configure(Bus(MM_ATM3, "zxbus"));
    claimAll.Add({ .mask = 0x00FF, .match = 0x00FD, .dir = Dir::InOut, .iorqge = Iorqge::Yes, .slot = 0,
                   .owner = &iorqCard });
    claimAll.Add({ .mask = 0x00FF, .match = 0x00B3, .dir = Dir::InOut, .iorqge = Iorqge::Yes, .slot = 0,
                   .owner = &iorqCard });
    claimAll.Build();

    int boardWrites = 0;
    claimAll.Write(0xFFFD, 0x07, [&](BoardCycle cycle)
    {
        EXPECT_EQ(cycle, BoardCycle::Full);
        boardWrites++;
    });
    EXPECT_EQ(boardWrites, 1);
    EXPECT_TRUE(iorqCard.writes.empty()) << "board port: the slots never see the cycle";

    // A port that is not a board port: the card sees it, and its IORQGE still leaves the board decoding
    claimAll.Write(0x00B3, 0x01, [&](BoardCycle cycle)
    {
        EXPECT_EQ(cycle, BoardCycle::Full);
        boardWrites++;
    });
    EXPECT_EQ(boardWrites, 2);
    EXPECT_EQ(iorqCard.writes.size(), 1u);

    // DOS-gated board port (#FF in DOS mode only)
    FakeSignals signals;
    claimAll.BindSignals(&signals);
    EXPECT_FALSE(claimAll.IsBoardPort(0x00FF, Dir::In));
    signals.dos = true;
    EXPECT_TRUE(claimAll.IsBoardPort(0x00FF, Dir::In));
}

/// An RdWr card (ZX-MultiSound) sees the cycles the board hides from the slots: both answer a read (bus fight)
TEST(PortClaimTable_Test, RdWrCardSeesBoardPorts)
{
    PortClaimTable table;
    FakeDevice multisound(0xF0);
    table.Configure(Bus(MM_ATM3, "zxbus"));
    ASSERT_EQ(Card("multisound").detection, CycleDetection::RdWr);
    Fit(table, Card("multisound"), 0, &multisound);
    table.Build();

    int boardWrites = 0;
    table.Write(0xFFFD, 0x07, [&](BoardCycle cycle)
    {
        EXPECT_EQ(cycle, BoardCycle::Full);
        boardWrites++;
    });
    EXPECT_EQ(boardWrites, 1);
    ASSERT_EQ(multisound.writes.size(), 1u) << "RD / WR detection: the board's /IORQ masking does not hide the cycle";

    BoardRead fpga;
    fpga.value = 0x0F;
    const ReadResult read = table.Read(0xFFFD, fpga);
    EXPECT_EQ(fpga.calls, 1);
    EXPECT_EQ(read.drivers, 2);
    EXPECT_TRUE(read.busFight);
    EXPECT_EQ(read.value, 0x0F) << "SlotOrder: the board first";

    // The same claims as an Iorq card: dead on the board port
    PortClaimTable iorq;
    FakeDevice plain;
    iorq.Configure(Bus(MM_ATM3, "zxbus"));
    for (const PortClaim& claim : Card("multisound").claims)
        iorq.Add(MakeClaimEntry(claim, 0, &plain, CycleDetection::Iorq));
    iorq.Build();
    iorq.Write(0xFFFD, 0x07, [](BoardCycle) {});
    EXPECT_TRUE(plain.writes.empty());
}

/// The table is built on the control path only: accesses never rebuild it, never move its storage, and the
/// results are plain values (no allocation on the access path)
TEST(PortClaimTable_Test, RebuildOnlyOnStart)
{
    static_assert(std::is_trivially_copyable_v<ReadResult>);
    static_assert(std::is_trivially_copyable_v<ClaimEntry>);

    PortClaimTable table;
    FakeSignals signals;
    FakeDevice multisound(0x55);
    FakeDevice net(0x66);
    table.Configure(Bus(MM_PENTAGON, "zxbus"));
    table.BindSignals(&signals);
    Fit(table, Card("multisound"), 0, &multisound);
    Fit(table, Card("zxnetusb"), 1, &net);
    table.Build();

    const uint32_t builds = table.BuildCount();
    const ClaimEntry* storage = table.BucketStorage();
    const size_t entries = table.Entries().size();

    for (uint32_t port = 0; port < 0x10000; port += 7)
    {
        const uint16_t p = static_cast<uint16_t>(port);
        table.Write(p, 0x00, [](BoardCycle) {});
        BoardRead board;
        (void)table.Read(p, board);
        (void)table.FirstMatch(p);
    }

    EXPECT_EQ(table.BuildCount(), builds);
    EXPECT_EQ(table.BucketStorage(), storage);
    EXPECT_EQ(table.Entries().size(), entries);

    // A registration change is a control-path rebuild
    table.RemoveOwner(&net);
    table.Build();
    EXPECT_EQ(table.BuildCount(), builds + 1);
    EXPECT_FALSE(table.IsClaimed(0x00AB));
}
