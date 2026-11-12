// The CPU's interrupt events on every model: int_accept after the acknowledge, int_return at the end of the return
// that pops the address the interrupt pushed. Checked against exec taps at the interrupted code: the return's T is
// the T at which the interrupted instruction starts

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/gs/gsdebughooks.h"

namespace
{
/// EI, then a loop of two instructions the interrupts land in (or EI : HALT : JR back, halted)
const std::vector<uint8_t> kLoop = {
    0xFB,        // 8000 EI
    0x3C,        // 8001 INC A
    0x18, 0xFD,  // 8002 JR #8001
};
const std::vector<uint8_t> kHaltLoop = {
    0xFB,        // 8000 EI
    0x76,        // 8001 HALT
    0x18, 0xFC,  // 8002 JR #8000
};
/// The IM 2 handler at #A0A0 (a table of #A0 bytes gives it for any vector): a push and a nested call before EI : RETI
constexpr uint16_t kIm2Handler = 0xA0A0;
const std::vector<uint8_t> kHandler = {
    0xF5,              // A0A0 PUSH AF
    0xCD, 0x00, 0xA1,  // A0A1 CALL #A100
    0xF1,              // A0A4 POP AF
    0xFB,              // A0A5 EI
    0xED, 0x4D,        // A0A6 RETI
};

struct Record
{
    enum Kind : uint8_t
    {
        Accept,
        Return,
        Exec,
    } kind;
    uint64_t frame;
    uint32_t t;
    uint16_t pc;
    uint16_t sp;
    uint8_t value;
    int arg;
    uint16_t pushed;  ///< the word at SP at the hit (an accept: the return address it pushed)
};

class Recorder : public IBreakpointTapSink
{
public:
    EmulatorContext* context = nullptr;
    std::vector<Record> records;
    void OnTap(const BreakpointTapHit& hit) override
    {
        const Z80* z80 = context->pCore->GetZ80();
        Record r{};
        if (hit.kind == BRK_TAP_EXEC)
            r.kind = Record::Exec;
        else
            r.kind = hit.address == static_cast<uint16_t>(GSCardEvent::IntAccept) ? Record::Accept : Record::Return;
        r.frame = context->emulatorState.frame_counter;
        r.t = z80->t;
        r.pc = hit.kind == BRK_TAP_EXEC ? hit.address : z80->pc;
        r.sp = z80->sp;
        r.value = static_cast<uint8_t>(hit.value);
        r.arg = hit.arg;
        Memory* memory = context->pMemory;
        r.pushed = static_cast<uint16_t>(memory->DirectReadFromZ80Memory(r.sp) |
                                         memory->DirectReadFromZ80Memory(static_cast<uint16_t>(r.sp + 1)) << 8);
        records.push_back(r);
    }
};
}  // namespace

class InterruptEvents_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    BreakpointManager* _manager = nullptr;
    Z80* _z80 = nullptr;
    Recorder _recorder;
    uint8_t _sink = BRK_NO_TAP;

    void TearDown() override
    {
        if (_manager && _sink != BRK_NO_TAP)
            _manager->UnregisterTapSink(_sink);
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Start(const char* model, const std::vector<uint8_t>& loop, uint8_t im)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr) << model;
        _context = _emulator->GetContext();
        _manager = _context->pDebugManager->GetBreakpointsManager();
        _z80 = _context->pCore->GetZ80();
        Memory* memory = _context->pMemory;
        auto put = [memory](uint16_t at, const std::vector<uint8_t>& bytes) {
            for (size_t i = 0; i < bytes.size(); i++)
                memory->DirectWriteToZ80Memory(static_cast<uint16_t>(at + i), bytes[i]);
        };
        put(0x8000, loop);
        put(kIm2Handler, kHandler);
        put(0xA100, {0xC9});  // RET
        for (uint32_t a = 0x9000; a <= 0x9100; a++)  // the IM 2 table (I = #90)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(a), 0xA0);
        _z80->pc = 0x8000;
        _z80->sp = 0xBF00;
        _z80->i = 0x90;
        _z80->im = im;
        _z80->iff1 = _z80->iff2 = 0;
        _z80->halted = 0;
        _recorder.context = _context;
        _sink = _manager->RegisterTapSink(&_recorder);
        for (GSCardEvent kind : {GSCardEvent::IntAccept, GSCardEvent::IntReturn})
        {
            BreakpointSpec spec;
            spec.type = BRK_EVENT;
            spec.address = static_cast<uint16_t>(kind);
            spec.tapSink = _sink;
            spec.hidden = true;
            std::string error;
            ASSERT_NE(_manager->AddBreakpoint(spec, error), BRK_INVALID) << error;
        }
        for (uint16_t at : {uint16_t{0x8000}, uint16_t{0x8001}, uint16_t{0x8002}})
        {
            BreakpointSpec spec;
            spec.type = BRK_MEMORY;
            spec.access = BRK_MEM_EXECUTE;
            spec.address = at;
            spec.tapSink = _sink;
            spec.hidden = true;
            std::string error;
            ASSERT_NE(_manager->AddBreakpoint(spec, error), BRK_INVALID) << error;
        }
    }

    /// Every accept has its return: the return pops the slot the accept pushed, and the first exec tap after it is
    /// the interrupted instruction's start at the same T
    void ExpectPairs(uint16_t handler, uint32_t acknowledge, int frames)
    {
        _emulator->RunNFrames(frames, /*skipBreakpoints=*/true);
        const std::vector<Record>& r = _recorder.records;
        int pairs = 0;
        for (size_t i = 0; i < r.size(); i++)
        {
            if (r[i].kind != Record::Accept)
                continue;
            const Record& accept = r[i];
            EXPECT_EQ(accept.pc, handler) << "the hit is told in the handler";
            EXPECT_EQ(static_cast<uint32_t>(accept.arg), acknowledge) << "the acknowledge's T-states";
            EXPECT_EQ(accept.value, _z80->im);
            const uint16_t interrupted = accept.pushed;
            size_t j = i + 1;
            while (j < r.size() && r[j].kind == Record::Exec)
                j++;  // the handler runs no tapped address
            if (j == r.size())
                break;  // the run ended inside this handler
            const Record& ret = r[j];
            ASSERT_EQ(ret.kind, Record::Return) << "the next interrupt event is its return";
            EXPECT_EQ(ret.arg, interrupted) << "the return address the acknowledge pushed";
            EXPECT_EQ(ret.value, accept.value);
            EXPECT_EQ(ret.sp, static_cast<uint16_t>(accept.sp + 2));
            if (j + 1 < r.size())
            {
                const Record& next = r[j + 1];
                EXPECT_EQ(next.kind, Record::Exec);
                EXPECT_EQ(next.pc, interrupted) << "the interrupted instruction starts next";
                EXPECT_EQ(next.frame, ret.frame);
                EXPECT_EQ(next.t, ret.t) << "the return is told at the interrupted instruction's start";
            }
            pairs++;
        }
        EXPECT_GE(pairs, frames - 1) << "one interrupt a frame";
        EXPECT_LE(_z80->IntFrameCount(), 1u);
    }
};

TEST_F(InterruptEvents_Test, Im2HandlerWithANestedCall48K)
{
    Start("48K", kLoop, 2);
    ASSERT_FALSE(HasFatalFailure());
    ExpectPairs(kIm2Handler, 19, 10);
}

TEST_F(InterruptEvents_Test, Im2HandlerHalted48K)
{
    Start("48K", kHaltLoop, 2);
    ASSERT_FALSE(HasFatalFailure());
    ExpectPairs(kIm2Handler, 19, 10);
}

TEST_F(InterruptEvents_Test, Im1RomHandlerPentagon)
{
    // The ROM's handler at #0038 (KEY-INT: pushes, a call to the keyboard scan, EI : RET)
    Start("PENTAGON", kLoop, 1);
    ASSERT_FALSE(HasFatalFailure());
    ExpectPairs(0x0038, 13, 10);
}

TEST_F(InterruptEvents_Test, Im2HandlerNext)
{
    // A machine on its own CPU engine: the returns are decoded after the engine ran them
    Start("NEXT", kLoop, 2);
    ASSERT_FALSE(HasFatalFailure());
    ExpectPairs(kIm2Handler, 19, 10);
}

// Without an event breakpoint the CPU keeps no frame and tells nothing
TEST_F(InterruptEvents_Test, NoWatchNoFrames)
{
    _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(_emulator, nullptr);
    _context = _emulator->GetContext();
    Z80* z80 = _context->pCore->GetZ80();
    _emulator->RunNFrames(5, true);
    EXPECT_FALSE(_context->machineEventWatch);
    EXPECT_EQ(z80->IntFrameCount(), 0u);
}
