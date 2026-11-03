// A sound card's CPU as the automation surfaces reach it (core/src/debugger/targets/cpuview.h): the `cpu` selector,
// the card CPU's registers, memory, disassembly and windows, and the tool edits of its registers and memory.
//
// A Pentagon with the classic General Sound (gs105a): ROM at #0000, RAM page 0 at #4000, port #00 pages #8000-#FFFF.

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "debugger/debugmanager.h"
#include "debugger/memory/memoryread.h"
#include "debugger/targets/cpuview.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/soundmanager.h"

namespace
{
class CpuView_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _sound = std::make_unique<SoundCardScope>(TestSound::GeneralSound);
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        ASSERT_TRUE(FitGeneralSoundCard(_context->pSoundManager, GSTypeKind::Z80));
        _emulator->RunNFrames(2);
        _card = _context->pSoundManager->getGeneralSound();
        ASSERT_NE(_card, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    std::unique_ptr<SoundCardScope> _sound;
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    GeneralSoundCard* _card = nullptr;
};
}  // namespace

TEST_F(CpuView_Test, TheSelectorNamesACpuThatIsThere)
{
    DebugCpuId cpu = DebugCpuId::Main;
    EXPECT_EQ(CpuView::Select(_context, "", cpu), "");
    EXPECT_EQ(cpu, DebugCpuId::Main);
    EXPECT_EQ(CpuView::Select(_context, "GS", cpu), "");
    EXPECT_EQ(cpu, DebugCpuId::GS);
    EXPECT_EQ(CpuView::Select(_context, "z81", cpu), "'cpu': main, gs or ms");
    EXPECT_EQ(CpuView::Select(_context, "ms", cpu), "cpu ms not available: no card with a CPU in its slot");
}

TEST_F(CpuView_Test, TheRegistersAreTheCardCpus)
{
    Z80CpuRegisters regs{};
    ASSERT_TRUE(_card->cardRegisters(regs));
    StateNode node = CpuView::Registers(_context, DebugCpuId::GS);
    EXPECT_EQ(node["cpu"].s, "gs");
    EXPECT_EQ(node["special"]["pc"].i, regs.pc);
    EXPECT_EQ(node["special"]["sp"].i, regs.sp);
    EXPECT_EQ(node["main"]["af"].i, regs.af);
    EXPECT_EQ(node["alternate"]["hl_"].i, regs.hlAlt);
    EXPECT_EQ(node["flags"]["c"].i, regs.af & 1);

    uint16_t value = 0;
    ASSERT_TRUE(CpuView::GetRegister(_context, DebugCpuId::GS, "PC", value));
    EXPECT_EQ(value, regs.pc);
    EXPECT_FALSE(CpuView::GetRegister(_context, DebugCpuId::GS, "pq", value));
    EXPECT_TRUE(CpuView::Registers(_context, DebugCpuId::MultiSoundGS).size() == 0);
}

TEST_F(CpuView_Test, AToolSetsACardRegister)
{
    EXPECT_EQ(CpuView::SetRegister(_emulator, DebugCpuId::GS, "test", "hl", 0x1234), "");
    EXPECT_EQ(CpuView::SetRegister(_emulator, DebugCpuId::GS, "test", "B", 0x56), "");
    Z80CpuRegisters regs{};
    ASSERT_TRUE(_card->cardRegisters(regs));
    EXPECT_EQ(regs.hl, 0x1234);
    EXPECT_EQ(regs.bc >> 8, 0x56);

    EXPECT_NE(CpuView::SetRegister(_emulator, DebugCpuId::GS, "test", "pq", 1), "");
    EXPECT_NE(CpuView::SetRegister(_emulator, DebugCpuId::GS, "test", "im", 3), "");
    EXPECT_NE(CpuView::SetRegister(_emulator, DebugCpuId::GS, "test", "a", 0x100), "");
    EXPECT_NE(CpuView::SetRegister(_emulator, DebugCpuId::MultiSoundGS, "test", "hl", 0), "");
}

TEST_F(CpuView_Test, MemoryIsTheCardCpusView)
{
    // #4000 is RAM page 0: a write reads back; #0000 is ROM: it keeps its byte
    uint8_t rom = 0;
    ASSERT_TRUE(_card->peekCardMemory(0x0000, rom));
    size_t written = 0;
    EXPECT_EQ(CpuView::Write(_emulator, DebugCpuId::GS, "test", 0x4000, {0xA5, 0x5A}, &written), "");
    EXPECT_EQ(written, 2u);
    EXPECT_EQ(CpuView::Write(_emulator, DebugCpuId::GS, "test", 0x0000, {static_cast<uint8_t>(rom ^ 0xFF)}, &written), "");
    EXPECT_EQ(written, 0u) << "the ROM window keeps its byte";

    const MemoryRead::Result ram = MemoryRead::Bytes(_context, "gs", 0x4000, 2);
    ASSERT_EQ(ram.error, "");
    EXPECT_EQ(ram.space, "gs");
    ASSERT_EQ(ram.bytes.size(), 2u);
    EXPECT_EQ(ram.bytes[0], 0xA5);
    EXPECT_EQ(ram.bytes[1], 0x5A);
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(CpuView::Read(_context, DebugCpuId::GS, 0x0000, 1, bytes));
    EXPECT_EQ(bytes[0], rom);

    // The view wraps at #FFFF; a card that is not there reads nothing
    const MemoryRead::Result wrap = MemoryRead::Bytes(_context, "gs", 0xFFFF, 2);
    ASSERT_EQ(wrap.error, "");
    EXPECT_EQ(wrap.bytes[1], rom);
    EXPECT_NE(MemoryRead::Bytes(_context, "ms", 0, 1).error, "");
    EXPECT_NE(MemoryRead::Bytes(_context, "gs", 0x10000, 1).error, "");
}

TEST_F(CpuView_Test, TheDisassemblyStartsAtTheCardsPc)
{
    Z80CpuRegisters regs{};
    ASSERT_TRUE(_card->cardRegisters(regs));
    StateNode node = CpuView::Disasm(_context, DebugCpuId::GS, regs.pc, 3);
    EXPECT_EQ(node["cpu"].s, "gs");
    ASSERT_EQ(node["instructions"].size(), 3u);
    StateNode& first = node["instructions"].items[0];
    EXPECT_EQ(first["address"].i, regs.pc);
    EXPECT_FALSE(first["mnemonic"].s.empty());
    EXPECT_EQ(node["instructions"].items[1]["address"].i, regs.pc + first["size"].i);
    EXPECT_FALSE(CpuView::Disasm(_context, DebugCpuId::MultiSoundGS, 0, 1)["available"].b);
}

TEST_F(CpuView_Test, TheWindowsAreTheCardsPages)
{
    StateNode node = CpuView::Windows(_context, DebugCpuId::GS);
    ASSERT_EQ(node["windows"].size(), 4u);
    EXPECT_EQ(node["windows"].items[0]["kind"].s, "rom");
    EXPECT_EQ(node["windows"].items[0]["page"].i, 0);
    EXPECT_EQ(node["windows"].items[1]["kind"].s, "ram");
    EXPECT_EQ(node["windows"].items[1]["page"].i, 1) << "the fixed window is the upper half of MPAG 1";
    EXPECT_EQ(node["windows"].items[3]["start"].i, 0xC000);
    EXPECT_TRUE(CpuView::Windows(_context, DebugCpuId::MultiSoundGS).size() == 0);
}
