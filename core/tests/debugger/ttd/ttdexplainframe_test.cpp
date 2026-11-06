#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdexplainframe.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

/// TTD explain frame (docs/inprogress/2026-11-02-ttd-explain-frame, plan.md T1-T11): small programs run by the CPU on
/// a Pentagon, recorded a few frames, then one frame explained; routines, times, writes, ports, paging and keys checked
/// against what the program did (T-states of the Pentagon: no contention)
class TTDExplainFrame_Test : public ::testing::Test
{
protected:
    static constexpr uint16_t kOrg = 0x8000;   // the program (page 2)
    static constexpr uint16_t kIsr = 0xFDFD;   // the IM 2 handler: I = #FE, the table at #FE00 all #FD

    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    std::vector<uint64_t> _frames;   // the frame each recorded step ran
    std::string _model = "PENTAGON";
    std::function<void()> _beforeRecording;   // after the program is loaded

    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }

    /// The IM 2 preamble (7 bytes, the body at #8007): di; ld a,#FE; ld i,a; im 2
    static std::vector<uint8_t> Im2(std::vector<uint8_t> body)
    {
        std::vector<uint8_t> code = {0xF3, 0x3E, 0xFE, 0xED, 0x47, 0xED, 0x5E};
        code.insert(code.end(), body.begin(), body.end());
        return code;
    }

    /// Loads @p code at kOrg and @p blocks at their addresses, the ISR (@p isr, default ei; reti), records @p frames
    /// frames (@p perFrame runs before each)
    void Record(const std::vector<uint8_t>& code, unsigned frames, const std::map<uint16_t, std::vector<uint8_t>>& blocks = {},
                const std::function<void(unsigned)>& perFrame = nullptr,
                const std::vector<uint8_t>& isr = {0xFB, 0xED, 0x4D}, uint16_t startInIsr = 0)
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("ttd-explain-frame", _model,
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
        for (uint16_t a = 0x4000; a < 0x5B00; ++a)
            memory->DirectWriteToZ80Memory(a, 0x00);   // a known picture: all zero
        for (uint16_t a = 0xA000; a < 0xA200; ++a)
            memory->DirectWriteToZ80Memory(a, 0x00);
        for (size_t i = 0; i < code.size(); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kOrg + i), code[i]);
        for (const auto& [at, bytes] : blocks)
            for (size_t i = 0; i < bytes.size(); ++i)
                memory->DirectWriteToZ80Memory(static_cast<uint16_t>(at + i), bytes[i]);
        for (uint16_t i = 0; i <= 0x100; ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0xFE00 + i), 0xFD);
        for (size_t i = 0; i < isr.size(); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kIsr + i), isr[i]);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = kOrg;
        z80->sp = 0xBF00;
        z80->iff1 = z80->iff2 = 0;
        z80->halted = 0;
        if (startInIsr)
        {
            // As right after an IM 2 acknowledge: at the routine's entry, IFF1 off, the interrupted PC on the stack
            z80->i = 0xFE;
            z80->im = 2;
            z80->sp = 0xBF00 - 2;
            memory->DirectWriteToZ80Memory(z80->sp, static_cast<uint8_t>(startInIsr));
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(z80->sp + 1), static_cast<uint8_t>(startInIsr >> 8));
            z80->pc = kIsr;
        }

        if (_beforeRecording)
            _beforeRecording();
        ASSERT_TRUE(_ttd->StartRecording());
        _frames.clear();
        for (unsigned n = 0; n < frames; ++n)
        {
            if (perFrame)
                perFrame(n);
            _frames.push_back(_ttd->CurrentPosition().frame);
            _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        }
        _ttd->StopRecording();
        _ttd->SeekTo(_ttd->SessionEndPosition());
    }

    ttd::TTDExplainFrameResult Explain(unsigned step, uint32_t sections = ttd::kExplainAll, uint32_t limit = 8)
    {
        ttd::TTDExplainFrameQuery q;
        q.hasFrame = true;
        q.frame = _frames.at(step);
        q.sections = sections;
        q.limit = limit;
        const ttd::TTDTimePoint before = _ttd->CurrentPosition();
        const uint16_t pc = _context->pCore->GetZ80()->pc;
        ttd::TTDExplainFrameResult r = _ttd->QueryExplainFrame(q);
        EXPECT_EQ(_ttd->CurrentPosition().frame, before.frame);
        EXPECT_EQ(_ttd->CurrentPosition().tInFrame, before.tInFrame);
        EXPECT_EQ(_context->pCore->GetZ80()->pc, pc);
        return r;
    }

    static const ttd::TTDExplainRoutine* Routine(const ttd::TTDExplainFrameResult& r, uint16_t pc, bool start = false)
    {
        for (const ttd::TTDExplainRoutine& x : r.routines)
            if (x.pc == pc && x.start == start)
                return &x;
        return nullptr;
    }

    static const ttd::TTDExplainRange* Range(const ttd::TTDExplainFrameResult& r, ttd::TTDExplainRegion region,
                                             uint16_t address = 0)
    {
        for (const ttd::TTDExplainRange& m : r.memory)
            if (m.region == region && (region != ttd::TTDExplainRegion::Ram || (m.hasAddress && m.address == address)))
                return &m;
        return nullptr;
    }
};

static std::string Text(const ttd::TTDExplainFrameResult& r)
{
    return ttd::TTDExplainFrameText(ttd::TTDExplainFrameQuery{}, r);
}

// #9000: ld b,10; loop: djnz loop; ret  (7 + 9 x 13 + 8 + 10 = 142 T, 12 instructions)
static const std::vector<uint8_t> kCount10 = {0x06, 0x0A, 0x10, 0xFE, 0xC9};
// #9100: ld a,(#A000); inc a; ld (#A000),a; ret  (13 + 4 + 13 + 10 = 40 T, 4 instructions)
static const std::vector<uint8_t> kIncA000 = {0x3A, 0x00, 0xA0, 0x3C, 0x32, 0x00, 0xA0, 0xC9};

TEST_F(TTDExplainFrame_Test, T1_RoutinesTimesAndTheHalt)
{
    // call #9000; call #9100; call #9100; halt   (interrupts off)
    Record(Im2({0xCD, 0x00, 0x90, 0xCD, 0x00, 0x91, 0xCD, 0x00, 0x91, 0x76}), 3,
           {{0x9000, kCount10}, {0x9100, kIncA000}});
    const ttd::TTDExplainFrameResult r = Explain(0);
    ASSERT_TRUE(r.error.empty()) << r.error;

    const ttd::TTDExplainRoutine* count = Routine(r, 0x9000);
    ASSERT_NE(count, nullptr) << Text(r);
    EXPECT_EQ(count->calls, 1u);
    EXPECT_EQ(count->t, 142u);
    EXPECT_EQ(count->ownT, 142u);
    EXPECT_EQ(count->insns, 12u);
    EXPECT_TRUE(count->hasCaller);
    EXPECT_EQ(count->caller, 0x8007);
    const ttd::TTDExplainRoutine* inc = Routine(r, 0x9100);
    ASSERT_NE(inc, nullptr) << Text(r);
    EXPECT_EQ(inc->calls, 2u);
    EXPECT_EQ(inc->t, 80u);
    EXPECT_EQ(inc->insns, 8u);
    const ttd::TTDExplainRoutine* start = Routine(r, kOrg, true);
    ASSERT_NE(start, nullptr) << Text(r);
    EXPECT_EQ(start->t, r.endT) << "the starting context holds the whole frame";
    EXPECT_EQ(r.routines.front().pc, kOrg) << "sorted by time";

    // The time adds up; the HALT holds the rest of the frame
    EXPECT_EQ(r.isrT, 0u);
    EXPECT_EQ(r.isrT + r.haltT + r.codeT, r.endT);
    // The code: the preamble, three CALLs, the routines; plus the lead-in before the first instruction starts (the
    // frame boundary falls inside an instruction: a few T)
    EXPECT_GE(r.codeT, 4u + 7 + 9 + 8 + 3 * 17 + 142 + 80) << Text(r);
    EXPECT_LE(r.codeT, 4u + 7 + 9 + 8 + 3 * 17 + 142 + 80 + 23) << Text(r);
    EXPECT_FALSE(r.interruptsEnabled);
    EXPECT_TRUE(r.interrupts.empty());
    EXPECT_NE(Text(r).find("interrupt: none (DI)"), std::string::npos) << Text(r);

    // #A000 0 -> 2 by the two stores; the stack: one range, the return addresses
    const ttd::TTDExplainRange* a000 = Range(r, ttd::TTDExplainRegion::Ram, 0xA000);
    ASSERT_NE(a000, nullptr) << Text(r);
    EXPECT_EQ(a000->changed, 1u);
    EXPECT_EQ(a000->written, 1u);
    ASSERT_TRUE(a000->hasValues);
    EXPECT_EQ(a000->before, 0x00);
    EXPECT_EQ(a000->after, 0x02);
    ASSERT_EQ(a000->writers.size(), 1u);
    EXPECT_EQ(a000->writers[0].pc, 0x9104);
    EXPECT_EQ(a000->writers[0].count, 2u);
    const ttd::TTDExplainRange* stack = Range(r, ttd::TTDExplainRegion::Stack);
    ASSERT_NE(stack, nullptr) << Text(r);
    EXPECT_EQ(stack->written, 2u);
    EXPECT_EQ(std::count_if(r.memory.begin(), r.memory.end(),
                            [](const ttd::TTDExplainRange& m) { return m.region == ttd::TTDExplainRegion::Stack; }),
              1);
    EXPECT_EQ(r.framesReplayed, 1u);
}

TEST_F(TTDExplainFrame_Test, T2_NestedCallsAndAPopJpExit)
{
    // call #9000 (which calls #9100); call #9200 (pop hl; jp (hl): leaves without a RET); call #9100; halt
    Record(Im2({0xCD, 0x00, 0x90, 0xCD, 0x00, 0x92, 0xCD, 0x00, 0x91, 0x76}), 2,
           {{0x9000, {0xCD, 0x00, 0x91, 0xC9}}, {0x9100, kIncA000}, {0x9200, {0xE1, 0xE9}}});
    const ttd::TTDExplainFrameResult r = Explain(0);
    ASSERT_TRUE(r.error.empty()) << r.error;
    const ttd::TTDExplainRoutine* outer = Routine(r, 0x9000);
    const ttd::TTDExplainRoutine* inner = Routine(r, 0x9100);
    const ttd::TTDExplainRoutine* popjp = Routine(r, 0x9200);
    ASSERT_TRUE(outer && inner && popjp) << Text(r);
    EXPECT_EQ(outer->t, 17u + 40 + 10) << "its CALL, the inner routine, its RET";
    EXPECT_EQ(outer->ownT, 27u);
    EXPECT_EQ(inner->calls, 2u);
    EXPECT_EQ(inner->t, 80u);
    // pop hl; jp (hl) has no RET: the routine stays open until a return at or above its return address's slot; the
    // next CALL pushes into that slot, so the called routine's RET closes both. Its time: pop, jp, that CALL and the
    // routine (14 + 17 + 40); the HALT after it is outside
    EXPECT_EQ(popjp->calls, 1u);
    EXPECT_EQ(popjp->t, 14u + 17 + 40) << Text(r);
    EXPECT_EQ(popjp->ownT, 14u + 17) << Text(r);
}

TEST_F(TTDExplainFrame_Test, T3_TheInterruptRoutine)
{
    // ei; loop: halt; jr loop. The ISR: call #9300; ei; reti (#FDFD..#FE02). #9300: ret.
    // The Pentagon's INT comes at the frame's end (+71658 here): the ISR starts in one frame and ends in the next
    Record(Im2({0xFB, 0x76, 0x18, 0xFD}), 4, {{0x9300, {0xC9}}}, nullptr, {0xCD, 0x00, 0x93, 0xFB, 0xED, 0x4D});
    const ttd::TTDExplainFrameResult r = Explain(2);
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.interrupts.size(), 2u) << Text(r);
    EXPECT_EQ(r.framesReplayed, 2u) << "IFF1 off at the start: the frame before traced for the open routine";

    // The frame starts in the tail of the interrupt before (ei; reti at #FE00): carried from the frame before, it is
    // that interrupt, back to the HALT loop
    const ttd::TTDExplainInterrupt& tail = r.interrupts[0];
    EXPECT_TRUE(tail.beforeFrame);
    EXPECT_EQ(tail.mode, 2);
    EXPECT_EQ(tail.vectorAddress, 0xFEFF) << "I = #FE, the bus #FF";
    EXPECT_EQ(tail.entry, kIsr);
    EXPECT_TRUE(tail.returned);
    EXPECT_EQ(tail.returnedTo, 0x8009);
    EXPECT_EQ(Routine(r, 0xFE00, true), nullptr) << "not a starting context";

    const ttd::TTDExplainInterrupt& irq = r.interrupts[1];
    EXPECT_FALSE(irq.beforeFrame);
    EXPECT_EQ(irq.mode, 2);
    EXPECT_TRUE(irq.hasVector);
    EXPECT_EQ(irq.vectorAddress, 0xFEFF);
    EXPECT_EQ(irq.entry, kIsr);
    EXPECT_GT(irq.tInFrame, 60000u);
    EXPECT_FALSE(irq.returned) << "it returns in the next frame";
    EXPECT_EQ(irq.t, r.endT - irq.tInFrame) << Text(r);
    EXPECT_EQ(r.isrT, tail.t + irq.t);

    const ttd::TTDExplainRoutine* isr = Routine(r, kIsr);
    ASSERT_NE(isr, nullptr) << Text(r);
    EXPECT_TRUE(isr->isr);
    EXPECT_EQ(isr->calls, 2u);
    EXPECT_EQ(isr->t, r.isrT);
    const ttd::TTDExplainRoutine* sub = Routine(r, 0x9300);
    ASSERT_NE(sub, nullptr) << Text(r);
    EXPECT_EQ(sub->caller, kIsr);
    const ttd::TTDExplainRoutine* loop = Routine(r, 0x8009, true);
    ASSERT_NE(loop, nullptr) << Text(r);
    EXPECT_EQ(loop->t + r.isrT, r.endT) << "the interrupted code does not take the interrupt's time";
    EXPECT_GT(r.haltT, 60000u);
    EXPECT_EQ(r.isrT + r.haltT + r.codeT, r.endT);
    EXPECT_NE(Text(r).find("still running at the end"), std::string::npos) << Text(r);
    EXPECT_NE(Text(r).find("at the frame before's end IM 2"), std::string::npos) << Text(r);
}

TEST_F(TTDExplainFrame_Test, T3b_AnInterruptAcknowledgedAtTheFrameBeforesEnd)
{
    // An acknowledge as a frame's last step (a recording started right after one, a machine whose INT falls on the
    // frame boundary): the frame begins at the routine's entry with IFF1 off and the interrupted PC on the stack. That
    // routine is the frame's interrupt, not code running at the start.
    // The ISR: call #9300; ei; reti; it returns to the HALT loop (#8008: halt; jr #8008). #9300: ret.
    Record(Im2({0xFB, 0x76, 0x18, 0xFD}), 2, {{0x9300, {0xC9}}}, nullptr, {0xCD, 0x00, 0x93, 0xFB, 0xED, 0x4D}, 0x8008);
    const ttd::TTDExplainFrameResult r = Explain(0);
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_FALSE(r.interrupts.empty()) << Text(r);
    const ttd::TTDExplainInterrupt& irq = r.interrupts[0];
    EXPECT_TRUE(irq.beforeFrame) << Text(r);
    EXPECT_EQ(irq.mode, 2);
    EXPECT_EQ(irq.vectorAddress, 0xFEFF);
    EXPECT_EQ(irq.entry, kIsr);
    EXPECT_TRUE(irq.returned) << Text(r);
    EXPECT_EQ(irq.returnedTo, 0x8008) << "the interrupted PC";
    const ttd::TTDExplainRoutine* isr = Routine(r, kIsr);
    ASSERT_NE(isr, nullptr) << Text(r);
    EXPECT_TRUE(isr->isr);
    EXPECT_EQ(isr->caller, 0x8008) << "the interrupted PC";
    EXPECT_EQ(Routine(r, kIsr, true), nullptr) << "not a starting context";
    ASSERT_NE(Routine(r, 0x9300), nullptr) << Text(r);
    EXPECT_EQ(Routine(r, 0x9300)->caller, kIsr);
    ASSERT_NE(Routine(r, 0x8008, true), nullptr) << "the HALT loop after the RETI";
    EXPECT_GE(irq.t, 17u + 10 + 4 + 14) << "call, ret, ei, reti";
    EXPECT_LE(irq.t, 17u + 10 + 4 + 14 + 23) << "plus a lead-in at the most";
    // The Pentagon's own INT at the frame end enters the same routine again, still running at the end
    ASSERT_EQ(r.interrupts.size(), 2u) << Text(r);
    EXPECT_FALSE(r.interrupts[1].beforeFrame);
    EXPECT_FALSE(r.interrupts[1].returned);
    EXPECT_EQ(r.isrT, irq.t + r.interrupts[1].t);
    EXPECT_EQ(isr->calls, 2u);
    EXPECT_EQ(isr->t, r.isrT);
    EXPECT_GT(r.isrT, 0u);
    EXPECT_EQ(r.isrT + r.haltT + r.codeT, r.endT);
    EXPECT_NE(Text(r).find("at the frame before's end IM 2"), std::string::npos) << Text(r);
}

TEST_F(TTDExplainFrame_Test, T5_Paging)
{
    // ld bc,#7FFD; ld a,7; out (c),a; ld a,0; out (c),a; halt
    Record(Im2({0x01, 0xFD, 0x7F, 0x3E, 0x07, 0xED, 0x79, 0x3E, 0x00, 0xED, 0x79, 0x76}), 2);
    const ttd::TTDExplainFrameResult r = Explain(0);
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.paging.size(), 2u) << Text(r);
    EXPECT_EQ(r.paging[0].bank, 3);
    EXPECT_EQ(r.paging[0].page, 7);
    EXPECT_EQ(r.paging[0].was, 0);
    EXPECT_EQ(r.paging[0].pc, 0x800C);
    EXPECT_EQ(r.paging[1].page, 0);
    EXPECT_EQ(r.paging[1].was, 7);
    EXPECT_EQ(r.paging[1].pc, 0x8010);
    EXPECT_LT(r.paging[0].tInFrame, r.paging[1].tInFrame);
}

TEST_F(TTDExplainFrame_Test, T6_PortsTheBorderAndTheKeyboard)
{
    // ld a,1; out (#FE),a; out (#FE),a; ld a,2; out (#FE),a; ld a,#7F; in a,(#FE); ld a,#FE; in a,(#FE); halt
    DebugKeyboardManager* keys = nullptr;
    Record(Im2({0x3E, 0x01, 0xD3, 0xFE, 0xD3, 0xFE, 0x3E, 0x02, 0xD3, 0xFE, 0x3E, 0x7F, 0xDB, 0xFE, 0x3E, 0xFE, 0xDB,
                0xFE, 0x76}),
           2, {}, [&](unsigned n) {
               if (n == 0)
               {
                   keys = _context->pDebugManager->GetKeyboardManager();
                   keys->PressKey(ZXKEY_SPACE);
               }
           });
    const ttd::TTDExplainFrameResult r = Explain(0);
    ASSERT_TRUE(r.error.empty()) << r.error;
    const ttd::TTDExplainPort* out = nullptr;
    const ttd::TTDExplainPort* in = nullptr;
    for (const ttd::TTDExplainPort& p : r.ports)
        (p.out ? out : in) = &p;
    ASSERT_TRUE(out && in) << Text(r);
    EXPECT_EQ(out->count, 3u);
    EXPECT_EQ(out->values, (std::vector<uint8_t>{0x01, 0x02}));
    EXPECT_EQ(out->pc, 0x8009);
    EXPECT_EQ(in->count, 2u);
    EXPECT_TRUE(in->manyPorts);
    EXPECT_EQ(in->rows, 0x81);
    ASSERT_EQ(in->keys.size(), 1u) << Text(r);
    EXPECT_EQ(in->keys[0], "space");
    ASSERT_EQ(r.border.size(), 2u) << Text(r);
    EXPECT_EQ(r.border[0].colour, 1);
    EXPECT_EQ(r.border[0].count, 2u);
    EXPECT_EQ(r.border[1].colour, 2);
    EXPECT_EQ(r.border[1].count, 1u);
}

TEST_F(TTDExplainFrame_Test, T7_MemoryRangesWritersAndTheScreen)
{
    // ld hl,#A100; ld de,#4000; ld bc,256; ldir; ld a,#33; ld (#A000),a; xor a; ld (#A040),a; halt
    std::vector<uint8_t> source(256, 0x55);
    Record(Im2({0x21, 0x00, 0xA1, 0x11, 0x00, 0x40, 0x01, 0x00, 0x01, 0xED, 0xB0, 0x3E, 0x33, 0x32, 0x00, 0xA0, 0xAF,
                0x32, 0x40, 0xA0, 0x76}),
           2, {{0xA100, source}});
    const ttd::TTDExplainFrameResult r = Explain(0);
    ASSERT_TRUE(r.error.empty()) << r.error;
    const ttd::TTDExplainRange* pixels = Range(r, ttd::TTDExplainRegion::ScreenPixels);
    ASSERT_NE(pixels, nullptr) << Text(r);
    EXPECT_EQ(pixels->changed, 256u);
    EXPECT_EQ(pixels->written, 256u);
    EXPECT_EQ(pixels->length, 256u);
    ASSERT_TRUE(pixels->hasAddress);
    EXPECT_EQ(pixels->address, 0x4000);
    ASSERT_EQ(pixels->writers.size(), 1u);
    EXPECT_EQ(pixels->writers[0].pc, 0x8010);
    EXPECT_EQ(pixels->writers[0].count, 256u);
    EXPECT_NE(pixels->writers[0].text.find("ldir"), std::string::npos) << pixels->writers[0].text;
    EXPECT_EQ(r.memory.front().region, ttd::TTDExplainRegion::ScreenPixels) << "sorted by bytes changed";

    const ttd::TTDExplainRange* a000 = Range(r, ttd::TTDExplainRegion::Ram, 0xA000);
    ASSERT_NE(a000, nullptr) << Text(r);
    EXPECT_EQ(a000->after, 0x33);
    EXPECT_EQ(a000->writers.at(0).pc, 0x8014);
    const ttd::TTDExplainRange* same = Range(r, ttd::TTDExplainRegion::Ram, 0xA040);
    ASSERT_NE(same, nullptr) << Text(r);
    EXPECT_EQ(same->changed, 0u) << "the same value written: written, not changed";
    EXPECT_EQ(same->written, 1u);

    ASSERT_TRUE(r.hasScreen);
    EXPECT_EQ(r.screenBytes, 6912u);
    EXPECT_EQ(r.screenChanged, 256u);
    EXPECT_EQ(r.pixelsChanged, 256u);
    EXPECT_EQ(r.attributesChanged, 0u);
    EXPECT_FALSE(r.screenSwitched);
}

TEST_F(TTDExplainFrame_Test, T8_PushesMakeOneStackRange)
{
    // push hl x4; pop hl x4; call #9100; halt
    Record(Im2({0xE5, 0xE5, 0xE5, 0xE5, 0xE1, 0xE1, 0xE1, 0xE1, 0xCD, 0x00, 0x91, 0x76}), 2, {{0x9100, kIncA000}});
    const ttd::TTDExplainFrameResult r = Explain(0);
    ASSERT_TRUE(r.error.empty()) << r.error;
    const ttd::TTDExplainRange* stack = Range(r, ttd::TTDExplainRegion::Stack);
    ASSERT_NE(stack, nullptr) << Text(r);
    EXPECT_EQ(stack->written, 8u);
    EXPECT_EQ(std::count_if(r.memory.begin(), r.memory.end(),
                            [](const ttd::TTDExplainRange& m) { return m.region == ttd::TTDExplainRegion::Stack; }),
              1);
}

TEST_F(TTDExplainFrame_Test, T9_KeysHeldAndPressed)
{
    // ei; loop: halt; jr loop
    DebugKeyboardManager* keys = nullptr;
    Record(Im2({0xFB, 0x76, 0x18, 0xFD}), 5, {}, [&](unsigned n) {
        if (!keys)
            keys = _context->pDebugManager->GetKeyboardManager();
        if (n == 1)
            keys->PressKey(ZXKEY_SPACE);
        if (n == 3)
            keys->PressKey(ZXKEY_Q);
    });
    const ttd::TTDExplainFrameResult r = Explain(3);
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.keysHeld.size(), 1u) << Text(r);
    EXPECT_EQ(r.keysHeld[0], DebugKeyboardManager::GetKeyDisplayName(ZXKEY_SPACE));
    ASSERT_EQ(r.keys.size(), 1u) << Text(r);
    EXPECT_EQ(r.keys[0].key, DebugKeyboardManager::GetKeyDisplayName(ZXKEY_Q));
    EXPECT_TRUE(r.keys[0].pressed);
}

TEST_F(TTDExplainFrame_Test, T10_OutsideTheHistoryIsAnError)
{
    Record(Im2({0x76}), 2);
    ttd::TTDExplainFrameQuery q;
    q.hasFrame = true;
    q.frame = _frames[0] + 1000;
    EXPECT_FALSE(_ttd->QueryExplainFrame(q).error.empty());
    q.frame = _frames[0] - 1;
    EXPECT_FALSE(_ttd->QueryExplainFrame(q).error.empty());
    // The session end (a frame boundary): nothing ran in that frame
    q.frame = _ttd->SessionEndPosition().frame;
    if (_ttd->SessionEndPosition().tInFrame == 0)
        EXPECT_FALSE(_ttd->QueryExplainFrame(q).error.empty());
}

TEST_F(TTDExplainFrame_Test, T11_LimitSectionsAndTheVerb)
{
    Record(Im2({0xCD, 0x00, 0x90, 0xCD, 0x00, 0x91, 0xCD, 0x00, 0x91, 0x76}), 2,
           {{0x9000, kCount10}, {0x9100, kIncA000}});
    const ttd::TTDExplainFrameResult one = Explain(0, ttd::kExplainAll, 1);
    ASSERT_EQ(one.routines.size(), 1u) << Text(one);
    EXPECT_EQ(one.moreRoutines, 2u);
    EXPECT_LE(one.memory.size(), 1u);
    EXPECT_NE(Text(one).find("+2 more"), std::string::npos) << Text(one);

    const ttd::TTDExplainFrameResult ports = Explain(0, ttd::kExplainPorts);
    EXPECT_TRUE(ports.routines.empty());
    EXPECT_TRUE(ports.memory.empty());

    // The verb: the same answer by name, the text, an unknown section an error
    const ttd::TTDReply reply = ttd::TTDControl(_context).Execute(
        {"explain-frame", {{"frame", std::to_string(_frames[0])}, {"sections", "routines,time"}, {"text", "true"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    const StateNode* text = reply.body.find("text");
    ASSERT_NE(text, nullptr);
    EXPECT_NE(text->s.find("#9000"), std::string::npos) << text->s;
    EXPECT_EQ(text->s.find("memory:"), std::string::npos) << text->s;
    EXPECT_FALSE(ttd::TTDControl(_context).Execute({"explain-frame", {{"sections", "nosuch"}}}).Ok());
}

TEST_F(TTDExplainFrame_Test, T12_NextOddPageInAnEvenSlot)
{
    // The Next's MMU maps 8K pages: 8K page #0F (the upper half of 16K page 7) into slot 6 (#C000), then a write to
    // #C005 lands at page 7 offset #2005, not #0005 (a 16K window's address & #3FFF)
    _model = "NEXT";
    _beforeRecording = [this]() { _context->pMemory->RAMPageAddress(7)[0x2005] = 0x00; };
    Record(Im2({0x01, 0x3B, 0x24,   // ld bc,#243B
                0x3E, 0x56,         // ld a,#56 (MMU6)
                0xED, 0x79,         // out (c),a
                0x04,               // inc b: #253B
                0x3E, 0x0F,         // ld a,#0F
                0xED, 0x79,         // out (c),a
                0x3E, 0xAA,         // ld a,#AA
                0x32, 0x05, 0xC0,   // ld (#C005),a
                0x18, 0xFE}),       // jr $
           2);
    EXPECT_EQ(_context->pMemory->RAMPageAddress(7)[0x2005], 0xAA);
    const ttd::TTDExplainFrameResult r = Explain(0, ttd::kExplainMemory);
    const ttd::TTDExplainRange* m = Range(r, ttd::TTDExplainRegion::Ram, 0xC005);
    ASSERT_NE(m, nullptr) << Text(r);
    EXPECT_EQ(m->page, 7u) << Text(r);
    EXPECT_EQ(m->offset, 0x2005u) << Text(r);
    EXPECT_EQ(m->written, 1u);
    EXPECT_EQ(m->changed, 1u);
    for (const ttd::TTDExplainRange& x : r.memory)
        EXPECT_FALSE(x.device) << Text(r);
}
