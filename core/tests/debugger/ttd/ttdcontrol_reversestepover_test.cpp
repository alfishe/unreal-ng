// Reverse step over (the debuggers' Shift+F8, debuggers.md D-2): one instruction back, a whole routine back when the
// instruction stepped over is the return that brought the stack to where it is

#include <gtest/gtest.h>

#include <map>
#include <string>

#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

using ttd::TTDControl;
using ttd::TTDReply;

class TTDControl_ReverseStepOver_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _controller = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        _controller = _context->pTimeTravelController;
        ASSERT_NE(_controller, nullptr);
    }

    void TearDown() override
    {
        if (_controller)
            _controller->StopRecording();
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }

    TTDReply Run(const std::string& verb, std::map<std::string, std::string> options = {})
    {
        return TTDControl(_context).Execute({verb, std::move(options)});
    }

    static bool Bool(const TTDReply& r, const char* key)
    {
        const StateNode* n = r.body.find(key);
        return n && n->b;
    }
    static int64_t Int(const TTDReply& r, const char* key)
    {
        const StateNode* n = r.body.find(key);
        return n ? n->i : -1;
    }
    static std::string Str(const TTDReply& r, const char* key)
    {
        const StateNode* n = r.body.find(key);
        return n ? n->s : std::string("<missing>");
    }

    Z80* Cpu() { return _context->pCore->GetZ80(); }

    void Poke(uint16_t at, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes)
            _context->pMemory->DirectWriteToZ80Memory(at++, b);
    }

    /// The program: a loop calling a routine that saves BC, calls a second routine and returns
    ///   #8000 DI / LD SP,#C000 / #8004 CALL #8010 / #8007 NOP / #8008 JR #8004
    ///   #8010 PUSH BC / CALL #8020 / #8014 POP BC / #8015 RET      #8020 NOP / RET
    void RecordTheLoop()
    {
        Poke(0x8000, {0xF3, 0x31, 0x00, 0xC0, 0xCD, 0x10, 0x80, 0x00, 0x18, 0xFA});
        Poke(0x8010, {0xC5, 0xCD, 0x20, 0x80, 0xC1, 0xC9});
        Poke(0x8020, {0x00, 0xC9});
        Cpu()->pc = 0x8000;
        ASSERT_TRUE(Run("start").Ok());
        _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
        ASSERT_TRUE(Run("stop").Ok());
    }

    /// Back to the last time an instruction started at pc
    void BackTo(uint16_t pc)
    {
        const TTDReply r = Run("reverse-continue", {{"pcs", std::to_string(pc)}});
        ASSERT_TRUE(r.Ok()) << r.message;
        ASSERT_EQ(Cpu()->pc, pc);
    }
};

TEST_F(TTDControl_ReverseStepOver_Test, AfterARoutineReturnsItGoesBackToTheCall)
{
    RecordTheLoop();
    BackTo(0x8007);   // the routine has just returned
    ASSERT_EQ(Cpu()->sp, 0xC000);

    const TTDReply r = Run("reverse-step-over");
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(Bool(r, "stepped"));
    EXPECT_TRUE(Bool(r, "over_call"));
    EXPECT_EQ(Cpu()->pc, 0x8004) << "the CALL that entered the routine";
    EXPECT_EQ(Cpu()->sp, 0xC000);
    // RET, POP BC, RET (inner), NOP, CALL (inner), PUSH BC: the nested call is passed over with the rest
    EXPECT_EQ(Int(r, "skipped"), 6);
    EXPECT_EQ(Int(r, "pc"), 0x8004);
    EXPECT_NE(Str(r, "text").find("over a routine"), std::string::npos) << Str(r, "text");
}

TEST_F(TTDControl_ReverseStepOver_Test, AnyOtherInstructionIsOneStepBack)
{
    RecordTheLoop();
    BackTo(0x8004);   // at the CALL: the instruction before it is the JR

    TTDReply r = Run("reverse-step-over");
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(Bool(r, "stepped"));
    EXPECT_FALSE(Bool(r, "over_call"));
    EXPECT_EQ(Cpu()->pc, 0x8008);

    // A POP lifts the stack too, but it closes no routine: one step back
    BackTo(0x8015);
    r = Run("reverse-step-over");
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_FALSE(Bool(r, "over_call"));
    EXPECT_EQ(Cpu()->pc, 0x8014);
}

TEST_F(TTDControl_ReverseStepOver_Test, InsideARoutineTheInnerCallIsPassedOver)
{
    RecordTheLoop();
    BackTo(0x8014);   // the inner routine has just returned
    ASSERT_EQ(Cpu()->sp, 0xBFFC);

    const TTDReply r = Run("reverse-step-over");
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(Bool(r, "over_call"));
    EXPECT_EQ(Cpu()->pc, 0x8011);
    EXPECT_EQ(Int(r, "skipped"), 2);   // RET and NOP of the inner routine
}

TEST_F(TTDControl_ReverseStepOver_Test, WithoutAHistoryItMovesNothingLikeTheOtherSteps)
{
    const uint16_t pc = Cpu()->pc;
    const TTDReply back = Run("step-instruction", {{"dir", "back"}});
    const TTDReply r = Run("reverse-step-over");
    EXPECT_EQ(r.Ok(), back.Ok());
    EXPECT_FALSE(Bool(r, "stepped"));
    EXPECT_FALSE(Bool(r, "over_call"));
    EXPECT_EQ(Cpu()->pc, pc);
    EXPECT_NE(Str(r, "text").find("nothing changed"), std::string::npos) << Str(r, "text");
}
