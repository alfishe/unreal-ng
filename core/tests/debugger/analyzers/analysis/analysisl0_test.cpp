/// @file analysisl0_test.cpp
/// @brief The L0 access facts of the analysis layers (docs/emulator/analysis/analysis-layers.md) on a real CPU: the
/// access kinds (opcode vs operand fetch, stack and block accesses, self-modifying code), the last reader and writer
/// with the multi flags, the frames, the values written, physical mode pages, the counters-only level and what a reset
/// or a segment does to the facts. Each test loads a few instructions, starts the analysis through analysis::Execute
/// (the shared core of every plane) and reads the facts back.

#include "pch.h"

#include <cstdint>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "debugger/analyzers/analysis/analysiscontrol.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccessfacts.h"
#include "emulator/memory/memoryaccesstracker.h"

namespace
{
constexpr uint8_t Kinds(std::initializer_list<AccessKind> kinds)
{
    uint8_t mask = 0;
    for (const AccessKind kind : kinds)
        mask = mask | kind;
    return mask;
}

const StateNode& Member(const StateNode& node, const std::string& name)
{
    static const StateNode kNull;
    const StateNode* found = node.find(name);
    return found ? *found : kNull;
}
}  // namespace

class AnalysisL0_Test : public ::testing::Test
{
protected:
    // One machine for the suite (a machine costs ~15 ms): every test loads its code, sets the registers it uses and
    // starts a fresh session, which clears the facts
    static void SetUpTestSuite() { _shared = EmulatorTestHelper::CreateDebugEmulator({"debugmode"}, "PENTAGON"); }
    static void TearDownTestSuite()
    {
        EmulatorTestHelper::CleanupEmulator(_shared);
        _shared = nullptr;
    }

    void SetUp() override
    {
        _emulator = _shared;
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _memory = _emulator->GetMemory();
        _z80 = _context->pCore->GetZ80();
        _tracker = &_memory->GetAccessTracker();
        _z80->iff1 = 0;
        _z80->iff2 = 0;
        _z80->sp = 0x9000;
    }

    void TearDown() override
    {
        if (_emulator)
            analysis::Execute(*_emulator, "start", {{"level", "counters"}});  // the level back to the default
    }

    void Load(uint16_t address, const std::vector<uint8_t>& bytes)
    {
        for (size_t i = 0; i < bytes.size(); i++)
            _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(address + i), bytes[i]);
    }

    void Start(const analysis::Options& options = {})
    {
        const analysis::Reply reply = analysis::Execute(*_emulator, "start", options);
        ASSERT_TRUE(reply.Ok()) << reply.message;
    }

    void Run(uint16_t pc, unsigned instructions)
    {
        _z80->pc = pc;
        _emulator->RunNCPUCycles(instructions, true);
    }

    ByteFacts Facts(uint16_t address) const
    {
        ByteFacts facts{};
        EXPECT_TRUE(_tracker->ReadZ80Facts(address, facts)) << "no facts at " << address;
        return facts;
    }

    analysis::Reply Explain(const analysis::Options& options) { return analysis::Execute(*_emulator, "explain", options); }

    static inline Emulator* _shared = nullptr;
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;
    Z80* _z80 = nullptr;
    MemoryAccessTracker* _tracker = nullptr;
};

/// An instruction's first byte is an opcode fetch, every other byte (prefixed opcode, immediate, displacement) an
/// operand fetch, never a data read - DJNZ's displacement included
TEST_F(AnalysisL0_Test, KindsFetchVsOperand)
{
    // LD A,$12 | LD HL,$9000 | LD IX,$1234 | SLA A | DJNZ $ (B = 1: falls through) | NOP
    Load(0x8000, {0x3E, 0x12, 0x21, 0x00, 0x90, 0xDD, 0x21, 0x34, 0x12, 0xCB, 0x27, 0x10, 0xFE, 0x00});
    Start();
    _z80->b = 1;
    Run(0x8000, 5);
    ASSERT_EQ(_z80->pc, 0x800D);

    const uint8_t opcode = Kinds({AccessKind::OpcodeFetch});
    const uint8_t operand = Kinds({AccessKind::OperandFetch});
    for (const uint16_t address : {0x8000, 0x8002, 0x8005, 0x8009, 0x800B})
        EXPECT_EQ(Facts(address).kinds, opcode) << std::hex << address;
    for (const uint16_t address : {0x8001, 0x8003, 0x8004, 0x8006, 0x8007, 0x8008, 0x800A, 0x800C})
        EXPECT_EQ(Facts(address).kinds, operand) << std::hex << address;
    EXPECT_EQ(Facts(0x800D).kinds, 0) << "not executed yet";
}

/// PUSH, POP, CALL, RET, RST, an accepted interrupt and RETI touch the stack as StackRead / StackWrite, not as data
TEST_F(AnalysisL0_Test, KindsStack)
{
    // $8000 PUSH BC | POP DE | CALL $8010 | RST $38 ... $8010 RET ... $8020 RETI
    Load(0x8000, {0xC5, 0xD1, 0xCD, 0x10, 0x80, 0xFF});
    Load(0x8010, {0xC9});
    Load(0x8020, {0xED, 0x4D});
    Start();
    Run(0x8000, 5);
    ASSERT_EQ(_z80->pc, 0x0038);

    const uint8_t stack = Kinds({AccessKind::StackRead, AccessKind::StackWrite});
    for (const uint16_t address : {0x8FFE, 0x8FFF})
    {
        const ByteFacts f = Facts(address);
        EXPECT_EQ(f.kinds, stack) << std::hex << address;
        EXPECT_EQ(f.lastWriter, 0x8005) << "RST pushed last";
        EXPECT_EQ(f.lastReader, 0x8010) << "RET popped last";
        EXPECT_TRUE(f.valueClass & valueclass::kMultiWriter) << "PUSH, CALL and RST";
        EXPECT_TRUE(f.valueClass & valueclass::kMultiReader) << "POP and RET";
    }

    // An interrupt's pushes, popped by RETI
    _z80->sp = 0x9100;
    _z80->im = 1;
    _z80->iff1 = 1;
    _z80->pc = 0x8020;
    _z80->HandleINT(0xFF);
    EXPECT_EQ(Facts(0x90FE).kinds, Kinds({AccessKind::StackWrite}));
    EXPECT_EQ(Facts(0x90FF).kinds, Kinds({AccessKind::StackWrite}));
    Run(0x8020, 1);
    EXPECT_EQ(_z80->sp, 0x9100);
    EXPECT_EQ(Facts(0x90FE).kinds, stack);
    EXPECT_EQ(Facts(0x90FF).lastReader, 0x8020);
}

/// LDIR's source and destination carry BlockAccess; a plain store does not
TEST_F(AnalysisL0_Test, KindsBlock)
{
    // LDIR | LD ($B010),A
    Load(0x8000, {0xED, 0xB0, 0x32, 0x10, 0xB0});
    Start();
    _z80->hl = 0xA000;
    _z80->de = 0xB000;
    _z80->bc = 4;
    Run(0x8000, 5);
    ASSERT_EQ(_z80->pc, 0x8005);

    for (uint16_t i = 0; i < 4; i++)
    {
        EXPECT_EQ(Facts(0xA000 + i).kinds, Kinds({AccessKind::DataRead, AccessKind::BlockAccess})) << i;
        EXPECT_EQ(Facts(0xB000 + i).kinds, Kinds({AccessKind::DataWrite, AccessKind::BlockAccess})) << i;
        EXPECT_EQ(Facts(0xB000 + i).lastWriter, 0x8000);
    }
    EXPECT_EQ(Facts(0xA004).kinds, 0);
    EXPECT_EQ(Facts(0xB010).kinds, Kinds({AccessKind::DataWrite}));
    EXPECT_EQ(Facts(0x8001).kinds, Kinds({AccessKind::OperandFetch}));
}

/// A byte written after it ran is self-modified code; one written before it runs (a loader) is not
TEST_F(AnalysisL0_Test, SmcFlag)
{
    // $8000 CALL $8100 | LD HL,$8100 | LD (HL),$C9 | LD ($8300),A | JP $8300 ... $8100 RET ... $8300 (NOP written)
    Load(0x8000, {0xCD, 0x00, 0x81, 0x21, 0x00, 0x81, 0x36, 0xC9, 0x32, 0x00, 0x83, 0xC3, 0x00, 0x83});
    Load(0x8100, {0xC9});
    Load(0x8300, {0xFF});
    Start();
    _z80->a = 0x00;
    Run(0x8000, 7);
    ASSERT_EQ(_z80->pc, 0x8301);

    EXPECT_EQ(Facts(0x8100).kinds, Kinds({AccessKind::OpcodeFetch, AccessKind::DataWrite, AccessKind::WrittenWhileCode}));
    EXPECT_EQ(Facts(0x8300).kinds, Kinds({AccessKind::OpcodeFetch, AccessKind::DataWrite}));

    const analysis::Reply reply = Explain({{"address", "$8100"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_NE(reply.text.find("WrittenWhileCode"), std::string::npos) << reply.text;
}

/// The last reader and writer are instruction PCs; the multi flags tell one instruction from several
TEST_F(AnalysisL0_Test, LastReaderWriterAndMultiFlags)
{
    Load(0x8000, {
                     0x3A, 0x00, 0xA0,  // $8000 LD A,($A000)
                     0x3A, 0x00, 0xA0,  // $8003 LD A,($A000)
                     0x32, 0x01, 0xA0,  // $8006 LD ($A001),A
                     0x32, 0x01, 0xA0,  // $8009 LD ($A001),A
                     0x32, 0x02, 0xA0,  // $800C LD ($A002),A
                     0x18, 0xFB,        // $800F JR $800C
                 });
    Start();
    Run(0x8000, 7);
    ASSERT_EQ(_z80->pc, 0x800F);

    const ByteFacts read = Facts(0xA000);
    EXPECT_EQ(read.kinds, Kinds({AccessKind::DataRead}));
    EXPECT_EQ(read.lastReader, 0x8003);
    EXPECT_TRUE(read.valueClass & valueclass::kMultiReader);

    const ByteFacts twoWriters = Facts(0xA001);
    EXPECT_EQ(twoWriters.lastWriter, 0x8009);
    EXPECT_TRUE(twoWriters.valueClass & valueclass::kMultiWriter);
    EXPECT_FALSE(twoWriters.valueClass & valueclass::kMultiReader);

    const ByteFacts oneWriterTwice = Facts(0xA002);
    EXPECT_EQ(oneWriterTwice.lastWriter, 0x800C);
    EXPECT_FALSE(oneWriterTwice.valueClass & valueclass::kMultiWriter) << "the same instruction twice";

    const analysis::Reply reply = Explain({{"address", "0xA001"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(Member(Member(reply.body, "last_writer"), "pc").s, "$8009");
    EXPECT_TRUE(Member(reply.body, "multi_writer").b);
    EXPECT_EQ(Member(reply.body, "last_reader").kind, StateNode::Kind::Null);
    EXPECT_EQ(Member(Member(reply.body, "counters"), "writes").i, 2);
}

/// First and last frame of the accesses to a byte
TEST_F(AnalysisL0_Test, FirstLastFrame)
{
    Load(0x8000, {0x3A, 0x00, 0xA0, 0x32, 0x00, 0xA0});  // LD A,($A000) | LD ($A000),A
    Start();
    _context->emulatorState.frame_counter = 10;
    Run(0x8000, 1);
    _context->emulatorState.frame_counter = 12;
    Run(0x8000, 1);
    _context->emulatorState.frame_counter = 15;
    Run(0x8003, 1);

    const ByteFacts f = Facts(0xA000);
    EXPECT_EQ(f.firstFrame, 10u);
    EXPECT_EQ(f.lastFrame, 15u);
    EXPECT_EQ(Facts(0x8000).firstFrame, 10u);
    EXPECT_EQ(Facts(0x8000).lastFrame, 12u);
}

/// The values written: one, two (they are the min and max), three or more; a repeat of min or max adds none
TEST_F(AnalysisL0_Test, ValueSummary)
{
    Load(0x8000, {0x77});  // LD (HL),A
    Start();
    const auto store = [this](uint16_t address, uint8_t value) {
        _z80->hl = address;
        _z80->a = value;
        Run(0x8000, 1);
    };
    store(0xA000, 0x24);
    store(0xA000, 0x24);
    store(0xA001, 0x30);
    store(0xA001, 0x10);
    store(0xA002, 0x30);
    store(0xA002, 0x10);
    store(0xA002, 0x20);
    store(0xA003, 0x10);
    store(0xA003, 0x30);
    store(0xA003, 0x10);

    const auto distinct = [this](uint16_t address) { return Facts(address).valueClass & valueclass::kDistinctMask; };
    EXPECT_EQ(distinct(0xA000), 1);
    EXPECT_EQ(Facts(0xA000).minWritten, 0x24);
    EXPECT_EQ(Facts(0xA000).maxWritten, 0x24);
    EXPECT_EQ(distinct(0xA001), 2);
    EXPECT_EQ(Facts(0xA001).minWritten, 0x10);
    EXPECT_EQ(Facts(0xA001).maxWritten, 0x30);
    EXPECT_EQ(distinct(0xA002), 3);
    EXPECT_EQ(distinct(0xA003), 2);
    EXPECT_EQ(distinct(0xA004), 0);

    const analysis::Reply reply = Explain({{"address", "$A002"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(Member(Member(reply.body, "written"), "distinct").s, "3+");
    EXPECT_NE(reply.text.find("$10..$30 (3+ distinct)"), std::string::npos) << reply.text;
}

/// Physical mode: the same Z80 address in two RAM pages keeps two sets of facts
TEST_F(AnalysisL0_Test, PhysicalModePages)
{
    Load(0x8000, {0x32, 0x00, 0xC0});  // LD ($C000),A
    Start({{"mode", "physical"}});
    _memory->SetRAMPageToBank3(1);
    _z80->a = 0x11;
    Run(0x8000, 1);
    _memory->SetRAMPageToBank3(3);
    _z80->a = 0x22;
    Run(0x8000, 1);

    ByteFacts page1{}, page3{}, code{};
    ASSERT_TRUE(_tracker->ReadPhysicalFacts(1, 0, page1));
    ASSERT_TRUE(_tracker->ReadPhysicalFacts(3, 0, page3));
    ASSERT_TRUE(_tracker->ReadPhysicalFacts(2, 0, code));  // $8000 is RAM 2 on a 128K machine
    EXPECT_EQ(page1.kinds, Kinds({AccessKind::DataWrite}));
    EXPECT_EQ(page1.minWritten, 0x11);
    EXPECT_EQ(page3.minWritten, 0x22);
    EXPECT_EQ(code.kinds, Kinds({AccessKind::OpcodeFetch}));

    analysis::Reply reply = Explain({{"address", "$C000"}, {"page", "ram:1"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(Member(Member(reply.body, "page"), "name").s, "RAM 1");
    EXPECT_EQ(Member(Member(reply.body, "written"), "min").s, "$11");
    reply = Explain({{"address", "$C000"}});  // the page mapped now: RAM 3
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(Member(Member(reply.body, "page"), "name").s, "RAM 3");
    EXPECT_EQ(Member(Member(reply.body, "written"), "min").s, "$22");

    const analysis::Reply status = analysis::Execute(*_emulator, "status");
    ASSERT_TRUE(status.Ok());
    EXPECT_EQ(Member(Member(status.body, "facts"), "pages").size(), 3u) << status.text;
    _memory->SetRAMPageToBank3(0);  // the machine is shared by the suite
}

/// The counters level keeps no facts and the counters as before; explain refuses
TEST_F(AnalysisL0_Test, LevelCountersUnchanged)
{
    Load(0x8000, {0x3A, 0x00, 0xA0});  // LD A,($A000)
    Start({{"level", "counters"}});
    EXPECT_EQ(_tracker->GetLevel(), TrackerLevel::Counters);
    Run(0x8000, 1);

    EXPECT_FALSE(_tracker->SummarizeFacts().allocated);
    EXPECT_EQ(_tracker->SummarizeFacts().bytes, 0u);
    EXPECT_EQ(_tracker->GetZ80AddressReadCount(0xA000), 1u);
    EXPECT_EQ(_tracker->GetZ80AddressExecuteCount(0x8000), 1u);
    EXPECT_EQ(Explain({{"address", "$A000"}}).error, analysis::Error::Conflict);

    // Facts on top: the same counters
    Start({{"level", "facts"}});
    Run(0x8000, 1);
    EXPECT_EQ(_tracker->GetZ80AddressReadCount(0xA000), 1u);
    EXPECT_EQ(_tracker->GetZ80AddressExecuteCount(0x8000), 1u);
    EXPECT_EQ(Facts(0xA000).kinds, Kinds({AccessKind::DataRead}));
}

/// A reset clears the facts with the counters, a new session too; segments leave both alone; stop keeps them
TEST_F(AnalysisL0_Test, ResetAndSegments)
{
    Load(0x8000, {0x3A, 0x00, 0xA0});  // LD A,($A000)
    Start();
    _tracker->EnableSegmentTracking(true);
    _tracker->StartSegment("first", TrackingEvent::Custom, 1);
    Run(0x8000, 1);
    _tracker->EndSegment();
    EXPECT_EQ(Facts(0xA000).kinds, Kinds({AccessKind::DataRead})) << "a segment keeps the facts";

    ASSERT_TRUE(analysis::Execute(*_emulator, "stop").Ok());
    EXPECT_EQ(Facts(0xA000).kinds, Kinds({AccessKind::DataRead})) << "stop keeps the facts";
    EXPECT_TRUE(Explain({{"address", "$A000"}}).Ok());

    _tracker->ResetCounters();
    EXPECT_EQ(Facts(0xA000).kinds, 0);
    EXPECT_EQ(_tracker->GetZ80AddressReadCount(0xA000), 0u);

    Start();
    Run(0x8000, 1);
    EXPECT_EQ(Facts(0xA000).kinds, Kinds({AccessKind::DataRead}));
    Start();
    EXPECT_EQ(Facts(0xA000).kinds, 0) << "a new session starts empty";
    EXPECT_EQ(_tracker->SummarizeFacts().z80Touched, 0u);
}
