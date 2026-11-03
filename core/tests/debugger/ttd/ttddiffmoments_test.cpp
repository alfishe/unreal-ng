#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttddiffmoments.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

/// TTD diff of two moments (docs/inprogress/2026-11-03-ttd-diff-moments, plan.md D1-D7): small programs recorded a few
/// frames, then two moments compared; registers, latches, the mapping, memory ranges and their last writes checked
/// against what the program did
class TTDDiffMoments_Test : public ::testing::Test
{
protected:
    static constexpr uint16_t kOrg = 0x8000;   // the program (page 2)
    static constexpr uint16_t kIsr = 0xFDFD;   // the IM 2 handler: I = #FE, the table at #FE00 all #FD

    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    std::vector<uint64_t> _frames;   // the frame each recorded step started at
    std::string _model = "PENTAGON";
    std::function<void()> _beforeRecording;

    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }

    /// di; ld a,#FE; ld i,a; im 2 (7 bytes: the body at #8007)
    static std::vector<uint8_t> Im2(std::vector<uint8_t> body)
    {
        std::vector<uint8_t> code = {0xF3, 0x3E, 0xFE, 0xED, 0x47, 0xED, 0x5E};
        code.insert(code.end(), body.begin(), body.end());
        return code;
    }

    void Record(const std::vector<uint8_t>& code, unsigned frames)
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("ttd-diff-moments", _model,
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
            memory->DirectWriteToZ80Memory(a, 0x00);
        for (uint16_t a = 0xA000; a < 0xA100; ++a)
            memory->DirectWriteToZ80Memory(a, 0x00);
        for (size_t i = 0; i < code.size(); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kOrg + i), code[i]);
        for (uint16_t i = 0; i <= 0x100; ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0xFE00 + i), 0xFD);
        const uint8_t isr[] = {0xFB, 0xED, 0x4D};   // ei; reti
        for (size_t i = 0; i < sizeof(isr); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kIsr + i), isr[i]);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = kOrg;
        z80->sp = 0xBF00;
        z80->iff1 = z80->iff2 = 0;
        z80->halted = 0;
        if (_beforeRecording)
            _beforeRecording();

        ASSERT_TRUE(_ttd->StartRecording());
        _frames.clear();
        for (unsigned n = 0; n < frames; ++n)
        {
            _frames.push_back(_ttd->CurrentPosition().frame);
            _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        }
        _ttd->StopRecording();
        _ttd->SeekTo(_ttd->SessionEndPosition());
    }

    /// The diff from @p from to @p to (to.frame == UINT64_MAX: the current position); the machine must stay put
    ttd::TTDDiffMomentsResult Diff(ttd::TTDTimePoint from, ttd::TTDTimePoint to = {UINT64_MAX, 0},
                                   uint32_t sections = ttd::kDiffAll)
    {
        ttd::TTDDiffMomentsQuery q;
        q.from = from;
        q.hasTo = to.frame != UINT64_MAX;
        q.to = to;
        q.sections = sections;
        const ttd::TTDTimePoint before = _ttd->CurrentPosition();
        const uint16_t pc = _context->pCore->GetZ80()->pc;
        ttd::TTDDiffMomentsResult r = _ttd->QueryDiffMoments(q);
        EXPECT_EQ(_ttd->CurrentPosition().frame, before.frame);
        EXPECT_EQ(_ttd->CurrentPosition().tInFrame, before.tInFrame);
        EXPECT_EQ(_context->pCore->GetZ80()->pc, pc);
        return r;
    }

    static const ttd::TTDDiffRange* Range(const ttd::TTDDiffMomentsResult& r, uint16_t address)
    {
        for (const ttd::TTDDiffRange& m : r.memory)
            if (m.hasAddress && address >= m.address && address < m.address + m.length)
                return &m;
        return nullptr;
    }
    static const ttd::TTDDiffValue* Value(const std::vector<ttd::TTDDiffValue>& list, const std::string& name)
    {
        for (const ttd::TTDDiffValue& v : list)
            if (v.name == name)
                return &v;
        return nullptr;
    }
    static std::string Text(const ttd::TTDDiffMomentsResult& r)
    {
        return ttd::TTDDiffMomentsText(ttd::TTDDiffMomentsQuery{}, r);
    }
};

namespace
{

// ei; loop: halt; ld hl,#A000; inc (hl); ld a,(hl); and 7; out (#FE),a; ld (#4000),a; jr loop
// (inc (hl) at #800C: one increment a frame, after the interrupt)
const std::vector<uint8_t> kCounter = {0xFB, 0x76, 0x21, 0x00, 0xA0, 0x34, 0x7E, 0xE6,
                                       0x07, 0xD3, 0xFE, 0x32, 0x00, 0x40, 0x18, 0xF1};

}  // namespace

/// D1: three frames of a counter: the counter's byte with its values and its last writer, the screen byte, the
/// border, the registers; the time in between
TEST_F(TTDDiffMoments_Test, D1_CounterScreenBorderAndTheLastWrite)
{
    Record(Im2(kCounter), 6);
    const ttd::TTDDiffMomentsResult r = Diff({_frames[1], 0}, {_frames[4], 0});
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_FALSE(r.backwards);
    EXPECT_EQ(r.frames, 3u);
    EXPECT_GT(r.t, 0u);

    const ttd::TTDDiffRange* counter = Range(r, 0xA000);
    ASSERT_NE(counter, nullptr) << Text(r);
    EXPECT_EQ(counter->region, ttd::TTDDiffRegion::Ram);
    EXPECT_EQ(counter->page, 2u);
    EXPECT_EQ(counter->offset, 0x2000u);
    EXPECT_EQ(counter->length, 1u);
    ASSERT_EQ(counter->before.size(), 1u);
    EXPECT_EQ(static_cast<uint8_t>(counter->after[0] - counter->before[0]), 3) << Text(r);
    ASSERT_TRUE(counter->hasWriter) << Text(r);
    EXPECT_EQ(counter->writerPc, 0x800C);
    EXPECT_EQ(counter->writerAddress, 0xA000);
    EXPECT_EQ(counter->writerTime.frame, _frames[3]);
    EXPECT_NE(counter->writerText.find("inc"), std::string::npos) << counter->writerText;

    const ttd::TTDDiffRange* pixels = Range(r, 0x4000);
    ASSERT_NE(pixels, nullptr) << Text(r);
    EXPECT_EQ(pixels->region, ttd::TTDDiffRegion::ScreenPixels);
    EXPECT_TRUE(pixels->hasWriter);
    EXPECT_EQ(pixels->writerPc, 0x8012);
    ASSERT_TRUE(r.hasScreen);
    EXPECT_EQ(r.screenBytes, 6912u);
    EXPECT_EQ(r.pixelsChanged, 1u);
    EXPECT_EQ(r.attributesChanged, 0u);
    EXPECT_NE(r.borderBefore, r.borderAfter);
    EXPECT_EQ(r.ramChanged, 2u + 0u) << Text(r);   // the counter and the screen byte (the stack holds the same)

    EXPECT_NE(Value(r.latches, "FE"), nullptr) << Text(r);
    EXPECT_NE(Value(r.registers, "AF"), nullptr) << Text(r);
    EXPECT_EQ(Value(r.registers, "HL"), nullptr) << "HL is #A000 at both moments: " << Text(r);
    EXPECT_TRUE(r.slotMap.empty());
    EXPECT_NE(Text(r).find("last write"), std::string::npos) << Text(r);
}

/// D2: the same two moments the other way round: before and after swap, the last write is the same
TEST_F(TTDDiffMoments_Test, D2_BackwardsSwapsTheValues)
{
    Record(Im2(kCounter), 6);
    const ttd::TTDDiffMomentsResult fwd = Diff({_frames[1], 0}, {_frames[4], 0});
    const ttd::TTDDiffMomentsResult back = Diff({_frames[4], 0}, {_frames[1], 0});
    ASSERT_TRUE(back.error.empty()) << back.error;
    EXPECT_TRUE(back.backwards);
    EXPECT_EQ(back.frames, 3u);
    const ttd::TTDDiffRange* f = Range(fwd, 0xA000);
    const ttd::TTDDiffRange* b = Range(back, 0xA000);
    ASSERT_NE(f, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->before, f->after);
    EXPECT_EQ(b->after, f->before);
    ASSERT_TRUE(b->hasWriter);
    EXPECT_EQ(b->writerTime.frame, f->writerTime.frame);
    EXPECT_EQ(b->writerTime.tInFrame, f->writerTime.tInFrame);
}

/// D3: a moment against itself: nothing differs, nothing searched
TEST_F(TTDDiffMoments_Test, D3_OneMomentAgainstItself)
{
    Record(Im2(kCounter), 4);
    const ttd::TTDDiffMomentsResult r = Diff({_frames[2], 0}, {_frames[2], 0});
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_EQ(r.ramChanged, 0u);
    EXPECT_TRUE(r.memory.empty());
    EXPECT_TRUE(r.registers.empty());
    EXPECT_TRUE(r.latches.empty());
    EXPECT_TRUE(r.slotMap.empty());
    EXPECT_TRUE(r.devices.empty());
    EXPECT_EQ(r.frames, 0u);
    EXPECT_EQ(r.t, 0u);
    EXPECT_EQ(r.writerSearches, 0u);
}

/// D4: two moments inside one frame: the moment is the first instruction boundary at or after the T asked for; the
/// increment of that frame lies between
TEST_F(TTDDiffMoments_Test, D4_MomentsInsideAFrame)
{
    Record(Im2(kCounter), 5);
    const ttd::TTDDiffMomentsResult r = Diff({_frames[2], 0}, {_frames[2], 2000});
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_EQ(r.to.frame, _frames[2]);
    EXPECT_GE(r.to.tInFrame, 2000u);
    EXPECT_LT(r.to.tInFrame, 2030u) << "the boundary right after T 2000";
    EXPECT_EQ(r.frames, 0u);
    EXPECT_EQ(r.t, r.to.tInFrame) << "T counts the units between the moments";
    const ttd::TTDDiffRange* counter = Range(r, 0xA000);
    ASSERT_NE(counter, nullptr) << Text(r);
    EXPECT_EQ(static_cast<uint8_t>(counter->after[0] - counter->before[0]), 1);
    ASSERT_TRUE(counter->hasWriter);
    EXPECT_EQ(counter->writerTime.frame, _frames[2]);
    EXPECT_NE(Value(r.registers, "PC"), nullptr);
}

/// D5: the 128K paging: #7FFD switches bank 3 into #C000; the latch and the two 8K slots of the window differ
TEST_F(TTDDiffMoments_Test, D5_PagingLatchAndSlots)
{
    // ei; halt; di; ld bc,#7FFD; ld a,#13; out (c),a; jr $
    Record(Im2({0xFB, 0x76, 0xF3, 0x01, 0xFD, 0x7F, 0x3E, 0x13, 0xED, 0x79, 0x18, 0xFE}), 4);
    const ttd::TTDDiffMomentsResult r = Diff({_frames[0], 0}, {_frames[3], 0});
    ASSERT_TRUE(r.error.empty()) << r.error;
    const ttd::TTDDiffValue* latch = Value(r.latches, "7FFD");
    ASSERT_NE(latch, nullptr) << Text(r);
    EXPECT_EQ(latch->after, 0x13u);
    ASSERT_EQ(r.slotMap.size(), 2u) << Text(r);
    EXPECT_EQ(r.slotMap[0].slot, 6u);
    EXPECT_EQ(r.slotMap[1].slot, 7u);
    EXPECT_EQ(r.slotMap[0].after, 3u);
    EXPECT_EQ(r.slotMap[1].after, 3u);
    EXPECT_EQ(r.slotMap[0].afterHalf, 0u);
    EXPECT_NE(Text(r).find("mapping: #C000 RAM 0->RAM 3"), std::string::npos) << Text(r);
}

/// D6: the Next's 8K slots: MMU6 = #0F shows page 7's upper half at #C000; the byte written there is named in page 7
/// at offset #2005 with its Z80 address and its writer
TEST_F(TTDDiffMoments_Test, D6_NextEightKHalf)
{
    _model = "NEXT";
    _beforeRecording = [this]() { _context->pMemory->RAMPageAddress(7)[0x2005] = 0x00; };
    Record(Im2({0x01, 0x3B, 0x24, 0x3E, 0x56, 0xED, 0x79, 0x04, 0x3E, 0x0F, 0xED, 0x79,   // MMU6 = #0F
                0x3E, 0xAA, 0x32, 0x05, 0xC0,                                             // ld (#C005),#AA at #8015
                0x18, 0xFE}),
           2);
    const ttd::TTDDiffMomentsResult r = Diff({_frames[0], 0});
    ASSERT_TRUE(r.error.empty()) << r.error;
    const ttd::TTDDiffSlot* slot6 = nullptr;
    for (const ttd::TTDDiffSlot& s : r.slotMap)
        if (s.slot == 6)
            slot6 = &s;
    ASSERT_NE(slot6, nullptr) << Text(r);
    EXPECT_EQ(slot6->after, 7u);
    EXPECT_EQ(slot6->afterHalf, 1u) << "an odd 8K page in an even slot";
    const ttd::TTDDiffRange* m = Range(r, 0xC005);
    ASSERT_NE(m, nullptr) << Text(r);
    EXPECT_EQ(m->page, 7u);
    EXPECT_EQ(m->offset, 0x2005u);
    ASSERT_EQ(m->after.size(), 1u);
    EXPECT_EQ(m->before[0], 0x00);
    EXPECT_EQ(m->after[0], 0xAA);
    ASSERT_TRUE(m->hasWriter) << Text(r);
    EXPECT_EQ(m->writerPc, 0x8015);
}

/// D7: the verb (the CLI / WebAPI / MCP path): the moments and options parsed, the text and the refusals
TEST_F(TTDDiffMoments_Test, D7_TheVerb)
{
    Record(Im2(kCounter), 5);
    ttd::TTDControl control(_context);
    const ttd::TTDReply ok = control.Execute({"diff", {{"from_frame", std::to_string(_frames[1])},
                                                        {"to_frame", std::to_string(_frames[3])},
                                                        {"sections", "memory,between"},
                                                        {"text", "true"}}});
    ASSERT_TRUE(ok.Ok()) << ok.message;
    const StateNode* text = ok.body.find("text");
    ASSERT_NE(text, nullptr);
    EXPECT_NE(text->s.find("memory: "), std::string::npos) << text->s;
    EXPECT_EQ(text->s.find("cpu:"), std::string::npos) << "sections: memory and between only\n" << text->s;
    EXPECT_NE(ok.body.find("memory"), nullptr);
    EXPECT_EQ(ok.body.find("registers"), nullptr);

    const ttd::TTDReply noWriters =
        control.Execute({"diff", {{"from_frame", std::to_string(_frames[1])}, {"writers", "false"}}});
    ASSERT_TRUE(noWriters.Ok()) << noWriters.message;
    EXPECT_EQ(noWriters.body.find("writer_searches")->i, 0);

    EXPECT_FALSE(control.Execute({"diff", {}}).Ok()) << "from_frame is required";
    EXPECT_FALSE(control.Execute({"diff", {{"from_tinframe", "5"}}}).Ok());
    EXPECT_FALSE(control.Execute({"diff", {{"from_frame", "1"}, {"sections", "bogus"}}}).Ok());
    EXPECT_FALSE(control.Execute({"diff", {{"from_frame", "999999"}}}).Ok()) << "outside the history";
}
