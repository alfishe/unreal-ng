#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/soundcardscope.h"
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
#include "emulator/io/fdc/wd1793.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include "emulator/sound/chips/soundchip_turbosoundfm.h"
#include "emulator/sound/covox.h"
#include "emulator/sound/soundmanager.h"

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

// ---- the fields inside a device (DM limit, plan.md "Device fields") ----

namespace
{

const ttd::TTDDiffDevice* DeviceWithField(const ttd::TTDDiffMomentsResult& r, const std::string& prefix)
{
    for (const ttd::TTDDiffDevice& d : r.devices)
        for (const ttd::TTDDiffField& f : d.fields)
            if (f.name.compare(0, prefix.size(), prefix) == 0)
                return &d;
    return nullptr;
}

const ttd::TTDDiffField* Field(const ttd::TTDDiffDevice& d, const std::string& name)
{
    for (const ttd::TTDDiffField& f : d.fields)
        if (f.name == name)
            return &f;
    return nullptr;
}

ttd::TTDDiffDevice DiffFields(const ttd::TTDStateFieldList& layout, const std::vector<ttd::TTDTimeField>& timeFields,
                              const std::vector<uint8_t>& before, const std::vector<uint8_t>& after,
                              uint32_t limit = 16)
{
    ttd::TTDDiffDevice d;
    ttd::TTDDiffDeviceFields(layout, timeFields, before, after, limit, d);
    return d;
}

}  // namespace

/// D8: the AY's layout: a register the program wrote is a field with both values, the tone and envelope counters
/// are counters listed after it; the counters alone are "only counters"
TEST(TTDDiffDeviceFields_Test, D8_AyRegistersAndCounters)
{
    ttd::TTDStateFieldList layout;
    SoundChip_AY8910::TTDFields(layout, "ay0.", 1);
    std::vector<uint8_t> a(1 + 73, 0);
    std::vector<uint8_t> b = a;
    b[1 + 7] = 0x3E;    // R7: the mixer
    b[1 + 19] = 0x05;   // tone_a.counter (17 + 2)
    b[1 + 52] = 0x01;   // envelope.counter (51, byte 1)
    const ttd::TTDDiffDevice d = DiffFields(layout, {}, a, b);
    EXPECT_TRUE(d.described);
    EXPECT_FALSE(d.CountersOnly());
    EXPECT_EQ(d.fieldsChanged, 1u);
    EXPECT_EQ(d.countersChanged, 2u);
    ASSERT_EQ(d.fields.size(), 3u);
    EXPECT_EQ(d.fields[0].name, "ay0.R7") << "the other fields first";
    EXPECT_FALSE(d.fields[0].counter);
    ASSERT_TRUE(d.fields[0].hasValues);
    EXPECT_EQ(d.fields[0].before, 0u);
    EXPECT_EQ(d.fields[0].after, 0x3Eu);
    EXPECT_EQ(d.fields[1].name, "ay0.tone_a.counter");
    EXPECT_TRUE(d.fields[1].counter);
    EXPECT_EQ(d.fields[1].after, 5u);
    EXPECT_EQ(d.fields[2].name, "ay0.envelope.counter");
    EXPECT_EQ(d.fields[2].after, 0x100u) << "little endian";

    b[1 + 7] = 0;
    const ttd::TTDDiffDevice counters = DiffFields(layout, {}, a, b);
    EXPECT_TRUE(counters.CountersOnly());
    EXPECT_EQ(counters.fieldsChanged, 0u);

    const ttd::TTDDiffDevice one = DiffFields(layout, {}, a, b, 1);
    EXPECT_EQ(one.fields.size(), 1u) << "bounded by the limit";
    EXPECT_EQ(one.moreFields, 1u);
    EXPECT_EQ(one.countersChanged, 2u) << "counted past the limit too";
}

/// D9: the disk controller: the registers, a drive's track, the clocks as counters (its declared time fields)
TEST(TTDDiffDeviceFields_Test, D9_DiskControllerAndItsDrives)
{
    ttd::TTDStateFieldList layout;
    WD1793::TTDFields(layout, "", 0);
    std::vector<uint8_t> a(WD1793::kTtdStateSize, 0);
    std::vector<uint8_t> b = a;
    b[1] = 5;                  // track
    b[4] = 0x80;               // status
    b[146 + 27 + 6] = 7;       // fdd[1].track
    b[22] = 0x40;              // time (a declared time field)
    b[146 + 18 + 2] = 1;       // fdd[0].rotation
    const ttd::TTDDiffDevice d = DiffFields(layout, {{22, 8}, {30, 8}, {114, 8}}, a, b);
    EXPECT_EQ(d.fieldsChanged, 3u);
    EXPECT_EQ(d.countersChanged, 2u);
    ASSERT_EQ(d.fields.size(), 5u);
    EXPECT_EQ(d.fields[0].name, "track");
    EXPECT_EQ(d.fields[0].after, 5u);
    EXPECT_EQ(d.fields[1].name, "status");
    EXPECT_EQ(d.fields[2].name, "fdd[1].track");
    EXPECT_EQ(d.fields[2].after, 7u);
    EXPECT_EQ(d.fields[3].name, "time");
    EXPECT_TRUE(d.fields[3].counter);
    EXPECT_EQ(d.fields[4].name, "fdd[0].rotation");
    EXPECT_TRUE(d.fields[4].counter);
    EXPECT_EQ(d.fields[4].after, 0x10000u);
}

/// D10: no layout: the bytes as `@offset` ranges, a declared time field a counter of its own; a time field inside a
/// larger named field is split out of it
TEST(TTDDiffDeviceFields_Test, D10_RangesAndTimeFieldsWithoutALayout)
{
    std::vector<uint8_t> a(32, 0);
    std::vector<uint8_t> b = a;
    b[0] = b[1] = b[3] = 1;   // one range (a gap of one byte joins)
    b[20] = 2;                // another
    b[9] = 9;                 // inside the time field at 8
    const ttd::TTDDiffDevice d = DiffFields({}, {{8, 4}}, a, b);
    EXPECT_FALSE(d.described);
    ASSERT_EQ(d.fields.size(), 3u);
    EXPECT_EQ(d.fields[0].name, "@0");
    EXPECT_EQ(d.fields[0].size, 4u);
    EXPECT_EQ(d.fields[0].changed, 3u);
    EXPECT_EQ(d.fields[1].name, "@20");
    EXPECT_EQ(d.fields[2].name, "@8");
    EXPECT_TRUE(d.fields[2].counter);
    EXPECT_EQ(d.fields[2].after, 0x900u);

    const ttd::TTDStateFieldList engine = {{"engine", 0, 16, 0}};
    std::vector<uint8_t> c = a;
    c[9] = 1;
    const ttd::TTDDiffDevice inside = DiffFields(engine, {{8, 4}}, a, c);
    ASSERT_EQ(inside.fields.size(), 1u);
    EXPECT_EQ(inside.fields[0].name, "engine+8");
    EXPECT_TRUE(inside.CountersOnly());
    c[2] = 1;
    const ttd::TTDDiffDevice both = DiffFields(engine, {{8, 4}}, a, c);
    ASSERT_EQ(both.fields.size(), 2u);
    EXPECT_EQ(both.fields[0].name, "engine");
    EXPECT_FALSE(both.fields[0].hasValues) << "16 bytes: no values";
    EXPECT_EQ(both.fields[0].changed, 1u);
}

/// D11: the Covox of a live machine: a frame without writes moves its idle countdowns only; an OUT to #FB is a latch
TEST_F(TTDDiffMoments_Test, D11_CovoxLatchAndIdleCountdown)
{
    _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("ttd-diff-covox", _model, LoggerLevel::LogNone);
    ASSERT_TRUE(_emulator);
    Covox* covox = _emulator->GetContext()->pSoundManager->getCovox();
    ASSERT_NE(covox, nullptr);
    ttd::TTDStateFieldList layout;
    covox->TTDDescribeFields(layout);
    std::vector<uint8_t> a(covox->TTDStateSize());
    covox->TTDSaveState(a.data());
    covox->handleFrameStart();
    std::vector<uint8_t> b(covox->TTDStateSize());
    covox->TTDSaveState(b.data());
    const ttd::TTDDiffDevice idle = DiffFields(layout, covox->TTDDescribe().timeFields, a, b);
    EXPECT_TRUE(idle.CountersOnly());
    ASSERT_FALSE(idle.fields.empty());
    EXPECT_EQ(idle.fields[0].name.compare(0, 12, "idle_frames["), 0) << idle.fields[0].name;

    covox->portDeviceOutMethod(0x00FB, 0x40);
    covox->TTDSaveState(b.data());
    const ttd::TTDDiffDevice written = DiffFields(layout, covox->TTDDescribe().timeFields, a, b);
    EXPECT_FALSE(written.CountersOnly());
    ASSERT_FALSE(written.fields.empty());
    EXPECT_EQ(written.fields[0].name.compare(0, 4, "dac["), 0) << written.fields[0].name;
    EXPECT_EQ(written.fields[0].after, 0x40u);
}

/// D12: two moments of a recording: the AY register the program wrote is named with its values; later, with the
/// program idle, the sound and disk devices differ in counters at most
TEST_F(TTDDiffMoments_Test, D12_TheAyRegisterInTheDiffAndOnlyCountersLater)
{
    SoundCardScope turboSound(TestSound::TurboSound);   // the tests leave the AY socket empty unless asked
    // R0 = #10, R7 = #3E (tone A on), R8 = #0F; then halt forever
    // ld bc,#FFFD; ld a,0; out (c),a; ld b,#BF; ld a,#10; out (c),a; ... ei; halt; jr $-1
    // Copied into a vector of the final size: growing it by insert made gcc 13 -O3 report a write past the end of the
    // new block (-Wstringop-overflow)
    const std::pair<uint8_t, uint8_t> writes[] = {{0, 0x10}, {7, 0x3E}, {8, 0x0F}};
    const uint8_t idle[] = {0xFB, 0x76, 0x18, 0xFD};
    std::vector<uint8_t> code(std::size(writes) * 13 + std::size(idle));
    auto out = code.begin();
    for (const auto& [reg, value] : writes)
    {
        const uint8_t write[13] = {0x01, 0xFD, 0xFF, 0x3E, reg, 0xED, 0x79, 0x06, 0xBF, 0x3E, value, 0xED, 0x79};
        out = std::copy(std::begin(write), std::end(write), out);
    }
    std::copy(std::begin(idle), std::end(idle), out);
    Record(Im2(code), 6);
    const ttd::TTDDiffMomentsResult r = Diff({_frames[0], 0}, {_frames[2], 0}, ttd::kDiffDevices);
    ASSERT_TRUE(r.error.empty()) << r.error;
    // The socket holds what the config fits: TurboSound (the AY as "ay0.") or the TSFM (its SSG half "fm0.ssg.")
    std::string prefix = "ay0.";
    const ttd::TTDDiffDevice* ay = DeviceWithField(r, prefix);
    if (!ay)
        ay = DeviceWithField(r, prefix = "fm0.ssg.");
    ASSERT_NE(ay, nullptr) << Text(r);
    EXPECT_FALSE(ay->CountersOnly()) << Text(r);
    const ttd::TTDDiffField* r8 = Field(*ay, prefix + "R8");
    ASSERT_NE(r8, nullptr) << Text(r);
    EXPECT_FALSE(r8->counter);
    EXPECT_EQ(r8->after, 0x0Fu);
    EXPECT_NE(Text(r).find(prefix + "R8 #00->#0F"), std::string::npos) << Text(r);

    const ttd::TTDDiffMomentsResult later = Diff({_frames[3], 0}, {_frames[5], 0}, ttd::kDiffDevices);
    ASSERT_TRUE(later.error.empty()) << later.error;
    for (const ttd::TTDDiffDevice& d : later.devices)
        if (d.described)
            EXPECT_TRUE(d.CountersOnly()) << d.name << " in\n" << Text(later);
    // Those devices whose counters alone moved come after the others
    bool seenCounters = false;
    for (const ttd::TTDDiffDevice& d : later.devices)
    {
        if (seenCounters)
            EXPECT_TRUE(d.CountersOnly()) << Text(later);
        seenCounters = seenCounters || d.CountersOnly();
    }
}

namespace
{

/// Every byte of a blob of @p size in exactly one field, and every declared time field one of them
void ExpectCovers(const ttd::TTDStateFieldList& layout, size_t size, const std::vector<ttd::TTDTimeField>& timeFields,
                  const std::string& what)
{
    std::vector<int> owners(size, 0);
    for (const ttd::TTDStateField& f : layout)
    {
        ASSERT_LE(size_t(f.offset) + f.size, size) << what << ": " << f.name << " past the blob";
        for (uint32_t b = 0; b < f.size; ++b)
            ++owners[f.offset + b];
    }
    std::string wrong;   // the first bytes owned by no field or by more than one
    for (size_t b = 0; b < size && wrong.size() < 80; ++b)
        if (owners[b] != 1)
            wrong += std::to_string(b) + "(" + std::to_string(owners[b]) + ") ";
    EXPECT_TRUE(wrong.empty()) << what << ": bytes not in exactly one field: " << wrong;
    for (const ttd::TTDTimeField& t : timeFields)
    {
        bool exact = false;
        for (const ttd::TTDStateField& f : layout)
            exact = exact || (f.offset == t.offset && f.size == t.width && (f.flags & ttd::kStateFieldCounter));
        EXPECT_TRUE(exact) << what << ": the time field at " << t.offset << " is not a counter field of its own";
    }
}

}  // namespace

/// D13: the described layouts cover their blobs byte for byte, and their time fields are counter fields
TEST_F(TTDDiffMoments_Test, D13_LayoutsCoverTheirBlobs)
{
    ttd::TTDStateFieldList ay;
    SoundChip_AY8910::TTDFields(ay, "", 0);
    ExpectCovers(ay, 73, {}, "AY");
    ttd::TTDStateFieldList wd;
    WD1793::TTDFields(wd, "", 0);
    ExpectCovers(wd, WD1793::kTtdStateSize, {{22, 8}, {30, 8}, {114, 8}}, "WD1793");

    // The sound devices as a machine builds them: TurboSound (two AYs), the TSFM (two YM2203), the Covox
    _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("ttd-diff-layouts", _model, LoggerLevel::LogNone);
    ASSERT_TRUE(_emulator);
    EmulatorContext* context = _emulator->GetContext();
    SoundChip_TurboSound ts(context);
    SoundChip_TurboSoundFM tsfm(context);
    for (const ttd::TTDSerializable* device : {static_cast<const ttd::TTDSerializable*>(&ts),
                                               static_cast<const ttd::TTDSerializable*>(&tsfm),
                                               static_cast<const ttd::TTDSerializable*>(context->pSoundManager->getCovox())})
    {
        ASSERT_NE(device, nullptr);
        ttd::TTDStateFieldList layout;
        device->TTDDescribeFields(layout);
        EXPECT_FALSE(layout.empty()) << device->TTDDeviceName();
        ExpectCovers(layout, device->TTDStateSize(), device->TTDDescribe().timeFields, device->TTDDeviceName());
    }
}
