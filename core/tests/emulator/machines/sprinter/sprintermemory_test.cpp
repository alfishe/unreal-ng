// SprinterMemory: the bank formula, graphics pages, the Spectrum screen
// shadow, the reset page, the ISA view and the loader layout (Sprinter
// test-plan §2.2 T-MEM-3..9; tdd-ports-memory §5).

#include "sprinterfixture.h"

#include <memory>
#include <vector>

#include "emulator/video/sprinter/sprintervideoram.h"

class SprinterMemory_Test : public SprinterFixture
{
protected:
    /// MAME update_memory window 0 (sprinter.cpp:320-357), transcribed independently of
    /// SprinterMemory: kind 0 = ROM page, 1 = fast RAM page, 2 = RAM from cell `value`
    struct Window0
    {
        int kind;
        uint8_t value;
        bool writable;
    };
    static Window0 MameWindow0(bool romSys, bool cashOn, bool ramSys, uint8_t sc, uint8_t pn, bool dos, bool arom16,
                               bool sysPg, uint8_t romRg)
    {
        const bool preRom = romSys || cashOn;
        const bool preCash = !cashOn;
        if (!preRom && preCash)
            return {0, static_cast<uint8_t>((romRg & 0x0F) ^ (!sysPg << 3)), false};
        if (preRom && !preCash)
            return {1, static_cast<uint8_t>(romRg & 3), true};
        const bool sc0 = sc & 1;
        const bool scLc = !(sc0 && ramSys);
        const uint8_t spr = (sc & 2) ? 0 : static_cast<uint8_t>((dos << 1) | (((pn >> 4) & 1) || !dos));
        const uint8_t pg0 = static_cast<uint8_t>(0x20 | ((sc0 || !ramSys) << 3) | ((arom16 && !(sc0 && ramSys)) << 2) |
                                                 ((((spr >> 1) & 1) && scLc) || !ramSys) << 1 |
                                                 (((spr & 1) && scLc) || !ramSys));
        return {2, pg0, sc0 && ramSys};
    }
};

// T-MEM-3: window 0 over romOff x cacheOn x ramSys x #1FFD b0/b1 x #7FFD b4 x DOS x arom16
TEST_F(SprinterMemory_Test, Window0_TruthTableMatchesMame)
{
    // Distinct pages in the vROM cells, so the chosen cell is visible
    for (uint8_t i = 0; i < 16; i++)
        Pld().cells[0x20 + i] = static_cast<uint8_t>(0x60 + i);

    int checked = 0;
    for (int bits = 0; bits < 256; bits++)
    {
        SprinterPldState& pld = Pld();
        pld.romOff = bits & 1;
        pld.cacheOn = (bits >> 1) & 1;
        pld.ramSys = (bits >> 2) & 1;
        pld.sc = static_cast<uint8_t>((bits >> 3) & 3);
        pld.pn = static_cast<uint8_t>(((bits >> 5) & 1) << 4);
        pld.dos = (bits >> 6) & 1;
        pld.arom16 = (bits >> 7) & 1;
        pld.sysPg = 0;
        pld.romRg = 0x02;
        _decoder->UpdateBanks();

        const Window0 expected = MameWindow0(pld.romOff, pld.cacheOn, pld.ramSys, pld.sc, pld.pn, pld.dos, pld.arom16,
                                             pld.sysPg, pld.romRg);
        switch (expected.kind)
        {
            case 0:
                ASSERT_EQ(_memory->GetMemoryBankMode(0), BANK_ROM) << bits;
                ASSERT_EQ(_memory->GetROMPageForBank(0), expected.value) << bits;
                break;
            case 1:
                ASSERT_EQ(_memory->GetMemoryBankMode(0), BANK_CACHE) << bits;
                break;
            default:
            {
                ASSERT_EQ(_memory->GetMemoryBankMode(0), BANK_RAM) << bits;
                ASSERT_EQ(_memory->GetRAMPageForBank0(), pld.cells[expected.value]) << bits;
                const uint8_t before = Ram(pld.cells[expected.value], 0x0100);
                Poke(0x0100, static_cast<uint8_t>(before ^ 0xFF));
                ASSERT_EQ(Ram(pld.cells[expected.value], 0x0100) != before, expected.writable) << bits;
                Ram(pld.cells[expected.value], 0x0100) = before;
                break;
            }
        }
        checked++;
    }
    EXPECT_EQ(checked, 256);
}

// ROM page select: page 8 after reset, page 0 with SYS_PG (the BIOS page stubs, OUT (#7C),1 / 0)
TEST_F(SprinterMemory_Test, Window0_BiosPageStubs)
{
    OpenDcp();
    EXPECT_EQ(Tag(0x0000), kRomTagBase + 8);
    SetCode(0x007C, false, 0xC6);
    Out(0x007C, 0x01);
    EXPECT_EQ(Tag(0x0000), kRomTagBase + 0) << "OUT (#7C),1: ROM page 0";
    Out(0x007C, 0x00);
    EXPECT_EQ(Tag(0x0000), kRomTagBase + 8) << "OUT (#7C),0: ROM page 8";
    Out(0x003C, 0x00);
    EXPECT_TRUE(IsRam(0x0000)) << "OUT (#3C): Spectrum mode, a vROM RAM page";
}

// T-MEM-4 / T-MEM-5 / T-MEM-6: graphics pages #50-#5F
TEST_F(SprinterMemory_Test, GraphicsPage_VideoAddressTransparencyAndReads)
{
    OpenDcp();
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    Pld().portY = 3;

    // Plain graphics page #50: main RAM and video RAM at PORT_Y x 1024 + A[9:0]
    Pld().Cell(0xEA) = 0x50;
    _decoder->UpdateBanks();
    Poke(0x8405, 0x12);  // A[9:0] = #005
    EXPECT_EQ(vram.Read(3 * 1024 + 5), 0x12);
    EXPECT_EQ(Ram(0x50, 3 * 1024 + 5), 0x12);
    EXPECT_EQ(Ram(0x50, 0x0405), 0x50) << "the plain store must not land at the CPU address";
    EXPECT_EQ(Peek(0x8005), 0x12) << "T-MEM-6: reads come from main RAM at the video address";

    // Bit 3: #FF is transparent
    Pld().Cell(0xEA) = 0x58;
    _decoder->UpdateBanks();
    Poke(0x8005, 0xFF);
    EXPECT_EQ(vram.Read(3 * 1024 + 5), 0x12);
    Poke(0x8005, 0x34);
    EXPECT_EQ(vram.Read(3 * 1024 + 5), 0x34);

    // Bit 2: video RAM only
    Pld().Cell(0xEA) = 0x54;
    _decoder->UpdateBanks();
    Poke(0x8005, 0x56);
    EXPECT_EQ(vram.Read(3 * 1024 + 5), 0x56);
    EXPECT_EQ(Ram(0x50, 3 * 1024 + 5), 0x34) << "main RAM unchanged";

    // PORT_Y moves the line
    Pld().portY = 200;
    Pld().Cell(0xEA) = 0x50;
    _decoder->UpdateBanks();
    Poke(0x83FF, 0x77);
    EXPECT_EQ(vram.Read(200 * 1024 + 0x3FF), 0x77);
    EXPECT_EQ(Ram(0x50 + (200 * 1024 + 0x3FF) / PAGE_SIZE, (200 * 1024 + 0x3FF) % PAGE_SIZE), 0x77);
}

// T-MEM-7: the Spectrum screen shadow address (MAME :1212-1216)
TEST_F(SprinterMemory_Test, SpectrumShadow_Address)
{
    EXPECT_EQ(SprinterMemory::ZxShadowAddress(0x4000, 0, 0x35), 0u);
    EXPECT_EQ(SprinterMemory::ZxShadowAddress(0x57FF, 0, 0x35), (0xFFu << 10) | 0x17u);
    EXPECT_EQ(SprinterMemory::ZxShadowAddress(0x4000, 1, 0x35), 1u << 5) << "RGADR odd: the other half";
    EXPECT_EQ(SprinterMemory::ZxShadowAddress(0x4000, 2, 0x35), 1u << 6) << "RGADR bits 4-1 pick the block";
    // Window 3 with Spectrum page 7: zxA15 = pg3 bit 1
    EXPECT_EQ(SprinterMemory::ZxShadowAddress(0xC000, 0, 0x37), 1u << 5);
    EXPECT_EQ(SprinterMemory::ZxShadowAddress(0xC000, 0, 0x35), 0u);
}

TEST_F(SprinterMemory_Test, SpectrumShadow_WritesFollowAllModeAndPortY)
{
    OpenDcp();
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    Pld().allMode = 0;
    Pld().portY = 0;

    Poke(0x4123, 0x9A);
    EXPECT_EQ(vram.Read(SprinterMemory::ZxShadowAddress(0x4123, 0, Pld().pg3)), 0x9A);
    EXPECT_EQ(Ram(Pld().Cell(0xE9), 0x0123), 0x9A) << "the plain store lands too";

    // #6000-#7FFF only with PORT_Y bit 7
    Poke(0x6123, 0x11);
    EXPECT_EQ(vram.Read(SprinterMemory::ZxShadowAddress(0x6123, 0, Pld().pg3)), 0x00);
    Pld().portY = 0x80;
    Poke(0x6123, 0x22);
    EXPECT_EQ(vram.Read(SprinterMemory::ZxShadowAddress(0x6123, 0x80, Pld().pg3)), 0x22);

    // PORT_Y bit 6 disables, ALL_MODE bit 0 disables
    Pld().portY = 0x40;
    Poke(0x4500, 0x33);
    EXPECT_EQ(vram.Read(SprinterMemory::ZxShadowAddress(0x4500, 0x40, Pld().pg3)), 0x00);
    Pld().portY = 0;
    Pld().allMode = 1;
    Poke(0x4600, 0x44);
    EXPECT_EQ(vram.Read(SprinterMemory::ZxShadowAddress(0x4600, 0, Pld().pg3)), 0x00);
}

// T-MEM-8: a write to page #A0 with #1FFD = #10 resets the CPU (soft reset)
TEST_F(SprinterMemory_Test, ResetPage_WriteRequestsSoftReset)
{
    OpenDcp();
    Pld().sc = 0x10;  // #1FFD bit 4: window 3 shows cell #F8 (ComputePg3)
    _decoder->UpdateBanks();
    ASSERT_EQ(Pld().pg3, 0x38);
    Pld().cells[Pld().pg3] = 0xA0;
    _decoder->UpdateBanks();
    ASSERT_EQ(_sprinterMemory->GetBankAction(3), SprinterMemory::BankAction::ResetPage);

    _z80->pc = 0x1234;
    Poke(0xC000, 0x01);
    EXPECT_NE(Pld().resetPending, 0);
    _decoder->OnMachineStep(0);
    EXPECT_EQ(_z80->pc, 0x0000);
    EXPECT_EQ(Pld().starting, 1);
    EXPECT_EQ(Pld().configState, SprinterConfigState::Configured) << "the PLD stays configured";

    // Another #1FFD value: a normal write
    OpenDcp();
    Pld().sc = 0x00;
    Pld().cells[Pld().pg3] = 0xA0;
    _decoder->UpdateBanks();
    EXPECT_EQ(_sprinterMemory->GetBankAction(3), SprinterMemory::BankAction::Plain);
}

namespace
{
/// An ISA card for the routing tests: I/O at #300-#30F answers the register number, writes are kept; 16 bytes of
/// memory at #DC000; counts the real reads (peeks must not count)
class RoutingCard : public sprinterisa::IIsaCard
{
public:
    const char* Kind() const override { return "ne2000"; }
    bool IoRead(const sprinterisa::IsaCycle& c, uint8_t& value) override
    {
        ++reads;
        if ((c.address & 0x3F0) != 0x300)
            return false;
        value = static_cast<uint8_t>(c.address & 0x0F);
        return true;
    }
    bool IoWrite(const sprinterisa::IsaCycle& c, uint8_t value) override
    {
        writes.push_back({c.address, value});
        return true;
    }
    bool MemRead(const sprinterisa::IsaCycle& c, uint8_t& value) override
    {
        ++reads;
        if (c.address < 0xDC000 || c.address >= 0xDC010)
            return false;
        value = ram[c.address - 0xDC000];
        return true;
    }
    bool MemWrite(const sprinterisa::IsaCycle& c, uint8_t value) override
    {
        if (c.address < 0xDC000 || c.address >= 0xDC010)
            return false;
        ram[c.address - 0xDC000] = value;
        return true;
    }
    bool IoPeek(uint32_t address, uint8_t& value) const override
    {
        if ((address & 0x3F0) != 0x300)
            return false;
        value = static_cast<uint8_t>(address & 0x0F);
        return true;
    }
    bool MemPeek(uint32_t address, uint8_t& value) const override
    {
        if (address < 0xDC000 || address >= 0xDC010)
            return false;
        value = ram[address - 0xDC000];
        return true;
    }
    void SetReset(bool) override {}

    int reads = 0;
    std::vector<std::pair<uint32_t, uint8_t>> writes;
    uint8_t ram[16] = {};
};
}  // namespace

// T-ISA-7 (was T-MEM-9): window 3 = #D2 with #1FFD bit 4 and empty slots: reads #FF, writes reach no RAM
TEST_F(SprinterMemory_Test, IsaView_EmptySlotsReadFFWritesIgnored)
{
    OpenDcp();
    Pld().sc = 0x10;
    _decoder->UpdateBanks();
    Pld().cells[Pld().pg3] = 0xD2;
    _decoder->UpdateBanks();
    EXPECT_EQ(Peek(0xC010), 0xFF);
    Poke(0xC010, 0x12);
    EXPECT_EQ(Ram(0xD2, 0x0010), 0xD2);
    EXPECT_EQ(_decoder->GetIsaBus().GetCounters(1).memReads, 1u) << "the read was a cycle of slot 2's memory space";
    EXPECT_EQ(_decoder->GetIsaBus().GetCounters(1).memWrites, 1u);

    Pld().cells[Pld().pg3] = 0xD1;  // not an ISA pattern ((page & #F9) != #D0)
    _decoder->UpdateBanks();
    EXPECT_EQ(Peek(0xC010), 0xD1);
}

// T-ISA-7: window 3 in ISA mode routes CPU reads, writes and opcode fetches to the card of the slot the page names,
// at the address #9FBD bits 5-0 << 14 | A13-A0; tool reads peek without side effects; no Spectrum shadow write
TEST_F(SprinterMemory_Test, IsaView_RoutesCyclesToTheCard)
{
    OpenDcp();
    auto owned = std::make_unique<RoutingCard>();
    RoutingCard* card = owned.get();
    _decoder->GetIsaBus().Fit(1, std::move(owned));

    // Slot 2 I/O (#D6): the RTL8019AS kit's ID read at #C30A and a command write at #C300
    Pld().sc = 0x10;
    _decoder->UpdateBanks();  // window 3's cell index follows #1FFD
    Pld().cells[Pld().pg3] = 0xD6;
    _decoder->UpdateBanks();
    EXPECT_EQ(Peek(0xC30A), 0x0A);
    EXPECT_EQ(card->reads, 1);
    Poke(0xC300, 0x21);
    ASSERT_EQ(card->writes.size(), 1u);
    EXPECT_EQ(card->writes[0].first, 0x00300u);
    EXPECT_EQ(card->writes[0].second, 0x21);
    EXPECT_EQ(Ram(0xD6, 0x0300), 0xD6) << "the RAM page under the view is not written";
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(0xC30B), 0x0B) << "a tool read peeks";
    EXPECT_EQ(card->reads, 1) << "peeks have no side effect";
    EXPECT_EQ(Peek(0xC10A), 0xFF) << "#00 10A is outside the card's window";

    // Slot 2 memory (#D2) with #9FBD = #37: #C000 is ISA #DC000; code runs from it (opcode fetches are ISA cycles)
    SetCodeAll(0x9FBD, false, SprinterCode::IsaControl);
    Out(0x9FBD, 0x37);
    EXPECT_EQ(_decoder->GetIsaBus().Latch(), 0x37) << "port-table code #1B";
    Pld().cells[Pld().pg3] = 0xD2;
    _decoder->UpdateBanks();
    Poke(0xC000, 0x3E);  // LD A,#77 : RET
    Poke(0xC001, 0x77);
    Poke(0xC002, 0xC9);
    EXPECT_EQ(card->ram[0], 0x3E);
    _z80->a = 0;
    _z80->sp = 0x9000;
    RunCode({0xCD, 0x00, 0xC0});  // CALL #C000
    EXPECT_EQ(_z80->a, 0x77) << "the CPU executed card memory";

    Out(0x9FBD, 0x00);
    _decoder->GetIsaBus().Fit(1, nullptr);
}
// Loader layout: before the PLD is configured the windows show ROM pages #C-#F;
// the Z84C15's CS0 boundary gives the top to the fast RAM
TEST_F(SprinterMemory_Test, LoaderLayout_RomPagesAndFastRamAboveCs0)
{
    _decoder->BeginLoading();
    _decoder->GetZ84().PowerOn();
    _decoder->UpdateBanks();
    EXPECT_EQ(Tag(0x0000), kRomTagBase + 0x0C);
    EXPECT_EQ(Tag(0x4000), kRomTagBase + 0x0D);
    EXPECT_EQ(Tag(0x8000), kRomTagBase + 0x0E);
    EXPECT_EQ(Peek(0xFE10), kRomTagBase + 0x0F) << "CSBR = #FF at power-on: everything is CS0 (ROM)";

    // The loader: SCRP = 2, CSBR = #FE (CS0 = #0000-#EFFF)
    Out(0x00EE, 0x02);
    Out(0x00EF, 0xFE);
    _sprinterMemory->FastRam()[0xFE10] = 0x3C;
    EXPECT_EQ(Peek(0xFE10), 0x3C) << "above CS0: fast RAM";
    EXPECT_EQ(Peek(0xEFFF), kRomTagBase + 0x0F);
    Poke(0xFE20, 0x5D);
    EXPECT_EQ(_sprinterMemory->FastRam()[0xFE20], 0x5D);
    EXPECT_EQ(Pld().bitstreamCount, 1u) << "every write is a configuration bit";
}

/// region <Tool reads (debugger, WebAPI) - every window, every mapping>

namespace
{
/// The memory reads the debugger's widgets make (disassembler, stack, memory views, breakpoint
/// list, interrupt vector) for window `window`: each must neither crash nor leave the window
struct ToolReadProbe
{
    static void Check(Memory& memory, uint8_t window)
    {
        const uint16_t base = static_cast<uint16_t>(window << 14);
        uint8_t* page = memory.MapZ80AddressToPhysicalAddress(base);
        ASSERT_NE(page, nullptr) << "window " << int(window);
        ASSERT_GE(page, memory.RAMBase()) << "window " << int(window);
        ASSERT_LT(page, memory.RAMBase() + PAGE_SIZE * MAX_PAGES) << "window " << int(window);
        ASSERT_EQ(memory.GetPhysicalAddressForZ80Page(window), page) << "window " << int(window);
        for (uint16_t offset : {0x0000, 0x0001, 0x2000, 0x3FFE, 0x3FFF})
            (void)memory.DirectReadFromZ80Memory(static_cast<uint16_t>(base + offset));
    }
};
}  // namespace

// The 2026-10-02 crash: Core::Reset runs Memory::Reset (the generic 48K layout) before the
// decoder maps the Sprinter's own; the Sprinter has no 48K ROM role (base_sos_rom == null), so
// window 0 was null in between and the debugger, refreshing on the UI thread (disassembler around
// PC = 0, stack at SP), read address 0. This is that intermediate state
TEST_F(SprinterMemory_Test, ToolReads_ResetIntermediateStateHasNoNullWindow)
{
    _memory->Reset();
    for (uint8_t window = 0; window < 4; window++)
        ToolReadProbe::Check(*_memory, window);
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(0x0000), kRomTagBase + 0) << "window 0: ROM page 0";

    // The ROM-role switches keep the current bank when the model has no such ROM
    _memory->SetROM48k();
    _memory->SetROM128k();
    _memory->SetROMDOS();
    _memory->SetROMSystem();
    ToolReadProbe::Check(*_memory, 0);

    // The full reset ends in the Sprinter's own layout
    _core->Reset();
    for (uint8_t window = 0; window < 4; window++)
        ToolReadProbe::Check(*_memory, window);
}

// A tool read sees what the CPU reads, in every mapping the PLD makes (without the CPU's side
// effects): ROM, fast RAM, vROM, plain RAM, graphics pages (the video address), the ISA view
// (#FF), the reset page, the Covox-Blaster page, the port table page while starting
TEST_F(SprinterMemory_Test, ToolReads_MatchCpuReadsInEveryMapping)
{
    const auto expectAllMatch = [&](const char* what) {
        for (uint8_t window = 0; window < 4; window++)
        {
            ToolReadProbe::Check(*_memory, window);
            for (uint16_t offset : {0x0000, 0x0010, 0x0405, 0x2000, 0x3FFF})
            {
                const uint16_t addr = static_cast<uint16_t>((window << 14) | offset);
                ASSERT_EQ(_memory->DirectReadFromZ80Memory(addr), Peek(addr)) << what << " #" << std::hex << addr;
            }
        }
    };

    expectAllMatch("after reset (starting: page #40 in window 3)");
    OpenDcp();
    expectAllMatch("system ROM");

    Pld().cacheOn = 1;
    _decoder->UpdateBanks();
    ASSERT_EQ(_memory->GetMemoryBankMode(0), BANK_CACHE);
    expectAllMatch("fast RAM in window 0");
    Pld().cacheOn = 0;
    Pld().romOff = 1;
    _decoder->UpdateBanks();
    ASSERT_EQ(_memory->GetMemoryBankMode(0), BANK_RAM);
    expectAllMatch("vROM in window 0");

    // Graphics pages in windows 1 and 2: the tool read follows the video address, not the CPU address
    Pld().portY = 3;
    Ram(0x50, 3 * 1024 + 5) = 0xA5;
    Pld().Cell(0xE9) = 0x58;
    Pld().Cell(0xEA) = 0x50;
    _decoder->UpdateBanks();
    expectAllMatch("graphics pages");
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(0x8005), 0xA5) << "the video address PORT_Y x 1024 + A[9:0]";
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(0x4405), 0xA5);

    // The ISA view in window 3: #FF, as the CPU reads it
    Pld().sc = 0x10;
    _decoder->UpdateBanks();
    for (uint8_t isaPage : {0xD0, 0xD2, 0xD4, 0xD6})
    {
        Pld().cells[Pld().pg3] = isaPage;
        _decoder->UpdateBanks();
        ASSERT_EQ(_sprinterMemory->GetReadRedirect(3), SprinterMemory::ReadRedirect::Isa);
        expectAllMatch("ISA view");
        EXPECT_EQ(_memory->DirectReadFromZ80Memory(0xC010), 0xFF);
    }

    // The reset page (#A0 with #1FFD = #10): a tool read has no side effect
    Pld().cells[Pld().pg3] = SprinterMemory::kResetPage;
    _decoder->UpdateBanks();
    expectAllMatch("reset page");
    EXPECT_EQ(Pld().resetPending, 0);

    // The Covox-Blaster page and the last RAM page
    Pld().sc = 0;
    _decoder->UpdateBanks();
    for (uint8_t page : {SprinterMemory::kCblPage, uint8_t{0xFF}})
    {
        Pld().cells[Pld().pg3] = page;
        _decoder->UpdateBanks();
        expectAllMatch("window 3 RAM");
    }

    // Plain RAM back in windows 1 and 2: no redirect left behind
    Pld().Cell(0xE9) = 0x05;
    Pld().Cell(0xEA) = 0x02;
    _decoder->UpdateBanks();
    expectAllMatch("plain RAM");
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(0x8010), 0x02);
}

// The loader layout (PLD not configured): ROM pages #C-#F, fast RAM above the Z84C15 CS0 boundary
TEST_F(SprinterMemory_Test, ToolReads_LoaderLayoutSeesFastRamAboveCs0)
{
    _decoder->BeginLoading();
    _decoder->GetZ84().PowerOn();
    _decoder->UpdateBanks();
    Out(0x00EE, 0x02);
    Out(0x00EF, 0xFE);
    _sprinterMemory->FastRam()[0xFE10] = 0x3C;
    for (uint8_t window = 0; window < 4; window++)
        ToolReadProbe::Check(*_memory, window);
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(0xFE10), 0x3C);
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(0xFE10), Peek(0xFE10));
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(0xEFFF), kRomTagBase + 0x0F);
}

/// endregion </Tool reads>
