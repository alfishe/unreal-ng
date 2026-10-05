// DebugSnapshot (debugsnapshot.h): the builders GET /registers, GET /disasm and the snapshot share. A 48K emulator
// that is created but not run: its registers and memory are set by the test.

#include <gtest/gtest.h>

#include "_helpers/testwaithelper.h"

#include <memory>
#include <string>

#include "debugger/snapshot/debugsnapshot.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

class DebugSnapshot_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("snapshot-test", "48K", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }

    void Poke(uint16_t address, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes)
            _context->pMemory->DirectWriteToZ80Memory(address++, b);
    }

    static int64_t Int(const StateNode& node, const char* group, const char* key)
    {
        const StateNode* g = node.find(group);
        const StateNode* v = g ? g->find(key) : nullptr;
        return v ? v->i : -1;
    }

    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
};

TEST_F(DebugSnapshot_Test, RegistersCarryEveryGroup)
{
    _z80->af = 0x12C5;   // F = C5: S Z . . . PV . C
    _z80->bc = 0x3456;
    _z80->alt.hl = 0xBEEF;
    _z80->ix = 0x1111;
    _z80->pc = 0x8000;
    _z80->sp = 0xFF00;
    _z80->im = 2;
    _z80->iff1 = 1;
    const StateNode r = DebugSnapshot::Registers(_context);
    EXPECT_EQ(Int(r, "main", "af"), 0x12C5);
    EXPECT_EQ(Int(r, "main", "bc"), 0x3456);
    EXPECT_EQ(Int(r, "alternate", "hl_"), 0xBEEF);
    EXPECT_EQ(Int(r, "index", "ix"), 0x1111);
    EXPECT_EQ(Int(r, "special", "pc"), 0x8000);
    EXPECT_EQ(Int(r, "special", "sp"), 0xFF00);
    EXPECT_EQ(Int(r, "interrupt", "im"), 2);
    EXPECT_EQ(Int(r, "interrupt", "iff1"), 1);
    EXPECT_EQ(Int(r, "flags", "s"), 1);
    EXPECT_EQ(Int(r, "flags", "z"), 1);
    EXPECT_EQ(Int(r, "flags", "h"), 0);
    EXPECT_EQ(Int(r, "flags", "pv"), 1);
    EXPECT_EQ(Int(r, "flags", "c"), 1);
    ASSERT_NE(r.find("interrupt")->find("halted"), nullptr);
    EXPECT_EQ(r.find("interrupt")->find("halted")->kind, StateNode::Kind::Bool);
    EXPECT_NE(r.find("interrupt")->find("boundary"), nullptr);
}

TEST_F(DebugSnapshot_Test, DisasmLinesWithTargets)
{
    Poke(0x8000, {0x00, 0xCD, 0x10, 0x80, 0x18, 0xFE, 0xDD, 0x7E, 0x05});   // NOP; CALL #8010; JR $; LD A,(IX+5)
    _z80->ix = 0x9000;
    const StateNode d = DebugSnapshot::Disasm(_context, 0x8000, 4);
    EXPECT_EQ(d.find("address")->i, 0x8000);
    EXPECT_EQ(d.find("count")->i, 4);
    const StateNode& lines = *d.find("instructions");
    ASSERT_EQ(lines.items.size(), 4u);
    EXPECT_EQ(lines.items[0].find("size")->i, 1);
    EXPECT_EQ(lines.items[1].find("address")->i, 0x8001);
    EXPECT_EQ(lines.items[1].find("bytes")->s, "CD1080");
    EXPECT_EQ(lines.items[1].find("target")->i, 0x8010);
    EXPECT_EQ(lines.items[2].find("target")->i, 0x8004) << "JR $: the instruction itself";
    ASSERT_NE(lines.items[3].find("effectiveAddress"), nullptr);
    EXPECT_EQ(lines.items[3].find("effectiveAddress")->i, 0x9005) << "IX + 5 with the runtime registers";
}

TEST_F(DebugSnapshot_Test, DisasmLimitsAndTheWrap)
{
    EXPECT_EQ(DebugSnapshot::Disasm(_context, 0, 500).find("count")->i, 100);
    EXPECT_EQ(DebugSnapshot::Disasm(_context, 0, 0).find("count")->i, 1);
    Poke(0xFFFE, {0x00, 0x00});
    const StateNode d = DebugSnapshot::Disasm(_context, 0xFFFE, 10);
    EXPECT_EQ(d.find("instructions")->items.size(), 2u) << "stops at the 64K wrap";
}

TEST_F(DebugSnapshot_Test, StepsMoveSeqAndThePreviousStop)
{
    Poke(0x8000, {0x00, 0x00, 0x00});   // NOPs
    _z80->pc = 0x8000;
    Z80State previous;
    EXPECT_FALSE(_emulator->PreviousStopRegisters(previous)) << "nothing ran yet";
    const uint64_t seq = _emulator->DebugSeq();
    _emulator->RunSingleCPUCycle(true);
    EXPECT_GE(_emulator->DebugSeq(), seq + 2) << "a run start and a stop";
    ASSERT_TRUE(_emulator->PreviousStopRegisters(previous));
    EXPECT_EQ(previous.pc, 0x8000) << "the registers before the step";
    EXPECT_EQ(_z80->pc, 0x8001);
    _emulator->RunSingleCPUCycle(true);
    ASSERT_TRUE(_emulator->PreviousStopRegisters(previous));
    EXPECT_EQ(previous.pc, 0x8001) << "the previous stop, not the first";
    EXPECT_EQ(DebugSnapshot::RegistersOf(previous).find("special")->find("pc")->i, 0x8001);
    EXPECT_EQ(_emulator->LastStop().reason, Emulator::DebugStop::Reason::Step);

    const uint64_t quiet = _emulator->DebugSeq();
    DebugSnapshot::Registers(_context);
    EXPECT_EQ(_emulator->DebugSeq(), quiet) << "reading changes nothing";
    _emulator->EditMemoryFromTool("test", [this]() { Poke(0x9000, {0x55}); });
    EXPECT_GT(_emulator->DebugSeq(), quiet) << "a tool edit counts";
}

// --- The whole snapshot (Build) ---------------------------------------------------------------------------------

TEST_F(DebugSnapshot_Test, Base64)
{
    EXPECT_EQ(DebugSnapshot::Base64({}), "");
    EXPECT_EQ(DebugSnapshot::Base64({'f'}), "Zg==");
    EXPECT_EQ(DebugSnapshot::Base64({'f', 'o'}), "Zm8=");
    EXPECT_EQ(DebugSnapshot::Base64({'f', 'o', 'o'}), "Zm9v");
    EXPECT_EQ(DebugSnapshot::Base64({0xFF, 0x00, 0x7F, 0x80}), "/wB/gA==");
}

TEST_F(DebugSnapshot_Test, OptionsAreChecked)
{
    DebugSnapshot::Options options;
    options.stack = 129;
    EXPECT_EQ(DebugSnapshot::Validate(options), "stack: at most 128 words");
    options.stack = 8;
    options.memory = std::vector<std::string>(9, "cpu:0:1");
    EXPECT_EQ(DebugSnapshot::Validate(options), "memory: at most 8 windows");
    options.memory = {"cpu:0x8000"};
    EXPECT_FALSE(DebugSnapshot::Validate(options).empty());
    const DebugSnapshot::Result r = DebugSnapshot::Build(_emulator.get(), options);
    EXPECT_FALSE(r.error.empty());
    EXPECT_FALSE(r.busy) << "a bad request, not a timing problem";
}

TEST_F(DebugSnapshot_Test, AStoppedMachineIsReadDirectly)
{
    Poke(0x8000, {0xC3, 0x00, 0x80});   // JP #8000
    _z80->pc = 0x8000;
    _z80->sp = 0xFF00;
    Poke(0xFF00, {0x34, 0x12, 0x78, 0x56});
    DebugSnapshot::Options options;
    options.disasm = 2;
    options.stack = 2;
    options.memory = {"cpu:0x8000:3", "ram9:0:1"};
    const DebugSnapshot::Result r = DebugSnapshot::Build(_emulator.get(), options);
    ASSERT_TRUE(r.error.empty()) << r.error;
    const StateNode& s = r.snapshot;
    EXPECT_EQ(s.find("consistency")->s, "stopped");
    EXPECT_EQ(s.find("cpu")->s, "z80");
    EXPECT_EQ(s.find("regs")->find("special")->find("pc")->i, 0x8000);
    EXPECT_EQ(s.find("prev_regs")->kind, StateNode::Kind::Null) << "no stop yet";
    EXPECT_EQ(s.find("pages")->items.size(), 4u);
    EXPECT_EQ(s.find("pages")->items[0].find("kind")->s, "rom");
    EXPECT_EQ(s.find("stack")->find("words")->items[0].i, 0x1234);
    EXPECT_EQ(s.find("stack")->find("words")->items[1].i, 0x5678);
    ASSERT_EQ(s.find("disasm")->items.size(), 2u);
    EXPECT_EQ(s.find("disasm")->items[0].find("target")->i, 0x8000);
    const StateNode& windows = *s.find("memory");
    ASSERT_EQ(windows.items.size(), 2u);
    EXPECT_EQ(windows.items[0].find("base64")->s, DebugSnapshot::Base64({0xC3, 0x00, 0x80}));
    EXPECT_NE(windows.items[1].find("error"), nullptr) << "a 48K has no RAM page 9: that window says so, the rest stands";
    EXPECT_NE(s.find("time")->find("frame_t"), nullptr);
}

TEST_F(DebugSnapshot_Test, PausedAndRunningMachines)
{
    // A real run: the 48K ROM boots; booting is not awaited (a few frames are enough to be "running")
    _emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return _emulator->IsRunning(); }));
    DebugSnapshot::Options options;
    options.disasm = 1;
    options.memory = {"cpu:0:4"};

    const DebugSnapshot::Result running = DebugSnapshot::Build(_emulator.get(), options);
    ASSERT_TRUE(running.error.empty()) << running.error;
    EXPECT_EQ(running.snapshot.find("consistency")->s, "frame") << "taken between two frames, without a pause";
    EXPECT_EQ(running.snapshot.find("state")->s, "running");
    // One moment: the PC in regs is the address of the first disassembled line
    EXPECT_EQ(running.snapshot.find("regs")->find("special")->find("pc")->i,
              running.snapshot.find("disasm")->items[0].find("address")->i);
    EXPECT_EQ(running.snapshot.find("regs")->find("special")->find("t")->i, running.snapshot.find("time")->find("t")->i);

    _emulator->Pause();
    ASSERT_TRUE(TestWait::For([&] { return _emulator->IsEmulationParked(); }));
    const DebugSnapshot::Result paused = DebugSnapshot::Build(_emulator.get(), options);
    ASSERT_TRUE(paused.error.empty()) << paused.error;
    EXPECT_EQ(paused.snapshot.find("consistency")->s, "paused");
    EXPECT_EQ(paused.snapshot.find("pause")->find("reason")->s, "pause");
    EXPECT_EQ(paused.snapshot.find("prev_regs")->kind, StateNode::Kind::Null) << "StartAsync is no resume";
    const uint64_t seq = static_cast<uint64_t>(paused.snapshot.find("seq")->i);
    EXPECT_EQ(static_cast<uint64_t>(DebugSnapshot::Build(_emulator.get(), options).snapshot.find("seq")->i), seq)
        << "nothing happened: the same seq";
    _emulator->Stop();
}
