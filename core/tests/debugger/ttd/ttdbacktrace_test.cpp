#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdbacktrace.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

/// The TTD backtrace (docs/inprogress/2026-11-07-ttd-backtrace/README.md): small programs run by the CPU on a
/// Pentagon, recorded a few frames, then asked for the call stack; every frame checked
class TTDBacktrace_Test : public ::testing::Test
{
protected:
    static constexpr uint16_t kOrg = 0x8000;   // the program (page 2)
    static constexpr uint16_t kIsr = 0xFDFD;   // the IM 2 handler: I = #FE, the table at #FE00 all #FD

    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }

    /// Loads the pieces of code at their addresses (and @p isr at kIsr), records @p frames frames from kOrg and stops
    /// at the recording's end
    void Record(const std::vector<std::pair<uint16_t, std::vector<uint8_t>>>& code, unsigned frames,
                const std::vector<uint8_t>& isr = {0x18, 0xFE})
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("ttd-backtrace", "PENTAGON",
                                                                            LoggerLevel::LogNone);
        ASSERT_TRUE(_emulator);
        _context = _emulator->GetContext();
        _context->pFeatureManager->setFeature(Features::kDebugMode, true);
        _context->pFeatureManager->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        _emulator->EnableTurboMode();
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);

        Memory* memory = _context->pMemory;
        for (const auto& [org, bytes] : code)
            for (size_t i = 0; i < bytes.size(); ++i)
                memory->DirectWriteToZ80Memory(static_cast<uint16_t>(org + i), bytes[i]);
        for (uint16_t i = 0; i <= 0x100; ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0xFE00 + i), 0xFD);
        for (size_t i = 0; i < isr.size(); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kIsr + i), isr[i]);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = kOrg;
        z80->iff1 = z80->iff2 = 0;

        ASSERT_TRUE(_ttd->StartRecording());
        for (unsigned n = 0; n < frames; ++n)
            _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        _ttd->StopRecording();
        _ttd->SeekTo(_ttd->SessionEndPosition());
    }

    ttd::TTDBacktraceResult Ask(uint32_t maxDepth = 16)
    {
        ttd::TTDBacktraceQuery q;
        q.maxDepth = maxDepth;
        return _ttd->QueryBacktrace(q);
    }
};

/// Two nested CALLs with a PUSH between them, the stack's base not written in the history: two frames innermost
/// first, the pushed word counted as data, the walk ends at the base
TEST_F(TTDBacktrace_Test, NestedCallsWithAPushBetween)
{
    Record({{0x8000, {0x31, 0x00, 0xC0,     // LD SP,#C000
                      0xCD, 0x10, 0x80,     // CALL #8010
                      0x18, 0xFE}},         // JR $
            {0x8010, {0xC5,                 // PUSH BC
                      0xCD, 0x20, 0x80,     // CALL #8020
                      0x18, 0xFE}},         // JR $
            {0x8020, {0x18, 0xFE}}},        // JR $ - the moment is here
           3);
    const ttd::TTDBacktraceResult r = Ask();
    ASSERT_TRUE(r.error.empty()) << r.error;
    const std::string text = ttd::TTDBacktraceText(r);
    EXPECT_EQ(r.pc, 0x8020) << text;
    EXPECT_EQ(r.sp, 0xBFFA) << text;
    ASSERT_EQ(r.frames.size(), 2u) << text;

    EXPECT_EQ(r.frames[0].kind, ttd::TTDBacktraceKind::Call) << text;
    EXPECT_EQ(r.frames[0].slot, 0xBFFA) << text;
    EXPECT_EQ(r.frames[0].callerPc, 0x8011) << text;
    EXPECT_EQ(r.frames[0].returnAddress, 0x8014) << text;
    ASSERT_TRUE(r.frames[0].hasCallee) << text;
    EXPECT_EQ(r.frames[0].callee, 0x8020) << text;
    EXPECT_EQ(r.frames[0].dataWords, 0u) << text;
    EXPECT_EQ(r.frames[0].calledAt.frame, r.frames[1].calledAt.frame) << "both calls run in the first frame\n" << text;

    EXPECT_EQ(r.frames[1].slot, 0xBFFE) << text;
    EXPECT_EQ(r.frames[1].callerPc, 0x8003) << text;
    EXPECT_EQ(r.frames[1].returnAddress, 0x8006) << text;
    EXPECT_EQ(r.frames[1].callee, 0x8010) << text;
    EXPECT_EQ(r.frames[1].dataWords, 1u) << "PUSH BC between the two return addresses\n" << text;
    EXPECT_LT(r.frames[1].calledAt.tInFrame, r.frames[0].calledAt.tInFrame) << text;

    EXPECT_EQ(r.dataWords, 1u) << text;
    EXPECT_EQ(r.stop, "before_session") << text;
    EXPECT_EQ(r.stopSlot, 0xC000) << text;
    EXPECT_NE(text.find("call #8020"), std::string::npos) << text;
}

/// A routine called and returned, then another CALL from elsewhere over the same stack word: only the live call is
/// a frame (the word's last writer), the returned one is gone
TEST_F(TTDBacktrace_Test, AReturnedCallIsNotAFrame)
{
    Record({{0x8000, {0x31, 0x00, 0xC0,     // LD SP,#C000
                      0xCD, 0x10, 0x80,     // CALL #8010 (returns at once)
                      0xCD, 0x20, 0x80,     // CALL #8020
                      0x18, 0xFE}},
            {0x8010, {0xC9}},               // RET
            {0x8020, {0x18, 0xFE}}},        // JR $
           2);
    const ttd::TTDBacktraceResult r = Ask();
    ASSERT_TRUE(r.error.empty()) << r.error;
    const std::string text = ttd::TTDBacktraceText(r);
    ASSERT_EQ(r.frames.size(), 1u) << text;
    EXPECT_EQ(r.frames[0].callerPc, 0x8006) << text;
    EXPECT_EQ(r.frames[0].callee, 0x8020) << text;
    EXPECT_EQ(r.stop, "before_session") << text;
}

/// An IM 2 interrupt taken inside a called routine, its handler looping: the interrupt is the innermost frame (the
/// interrupted PC, the handler entered), the CALL below it
TEST_F(TTDBacktrace_Test, AnInterruptIsAFrame)
{
    Record({{0x8000, {0x31, 0x00, 0xC0,     // LD SP,#C000
                      0xCD, 0x10, 0x80,     // CALL #8010
                      0x18, 0xFE}},
            {0x8010, {0x3E, 0xFE,           // LD A,#FE
                      0xED, 0x47,           // LD I,A
                      0xED, 0x5E,           // IM 2
                      0xFB,                 // EI
                      0x18, 0xFE}}},        // JR $ (#8017) - the interrupt lands here
           3);
    const ttd::TTDBacktraceResult r = Ask();
    ASSERT_TRUE(r.error.empty()) << r.error;
    const std::string text = ttd::TTDBacktraceText(r);
    EXPECT_EQ(r.pc, kIsr) << text;
    ASSERT_EQ(r.frames.size(), 2u) << text;
    EXPECT_EQ(r.frames[0].kind, ttd::TTDBacktraceKind::Interrupt) << text;
    EXPECT_EQ(r.frames[0].returnAddress, 0x8017) << text;
    ASSERT_TRUE(r.frames[0].hasCallee) << text;
    EXPECT_EQ(r.frames[0].callee, kIsr) << text;
    EXPECT_TRUE(r.frames[1].calledAt < r.frames[0].calledAt) << "the interrupt comes after the call\n" << text;
    EXPECT_EQ(r.frames[1].kind, ttd::TTDBacktraceKind::Call) << text;
    EXPECT_EQ(r.frames[1].callerPc, 0x8003) << text;
    EXPECT_NE(text.find("interrupt"), std::string::npos) << text;
}

/// max_depth cuts the walk and says so
TEST_F(TTDBacktrace_Test, MaxDepthStops)
{
    Record({{0x8000, {0x31, 0x00, 0xC0, 0xCD, 0x10, 0x80, 0x18, 0xFE}},
            {0x8010, {0xCD, 0x20, 0x80, 0x18, 0xFE}},
            {0x8020, {0x18, 0xFE}}},
           2);
    const ttd::TTDBacktraceResult r = Ask(1);
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.frames.size(), 1u) << ttd::TTDBacktraceText(r);
    EXPECT_EQ(r.frames[0].callerPc, 0x8010) << ttd::TTDBacktraceText(r);
    EXPECT_EQ(r.stop, "max_depth");
}
