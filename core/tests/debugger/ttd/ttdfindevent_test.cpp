#include <gtest/gtest.h>

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
#include "debugger/ttd/ttdfindevent.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

/// TTD find event (docs/inprogress/2026-11-02-ttd-find-event, plan.md T1-T12): small programs run by the CPU on a
/// Pentagon, recorded a few frames, then asked when something happened; the hit's frame, time, PC and values checked
/// against what the program did
class TTDFindEvent_Test : public ::testing::Test
{
protected:
    static constexpr uint16_t kOrg = 0x8000;   // the program (page 2)
    static constexpr uint16_t kSub = 0x9000;   // a routine (page 2)
    static constexpr uint16_t kDst = 0xA000;   // the counter / the value watched (page 2)
    static constexpr uint16_t kIsr = 0xFDFD;   // the IM 2 handler: I = #FE, the table at #FE00 all #FD

    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }

    /// The IM 2 preamble: di; ld a,#FE; ld i,a; im 2; then the program's own code (ei where it wants interrupts)
    static std::vector<uint8_t> Im2(std::vector<uint8_t> body)
    {
        std::vector<uint8_t> code = {0xF3, 0x3E, 0xFE, 0xED, 0x47, 0xED, 0x5E};
        code.insert(code.end(), body.begin(), body.end());
        return code;
    }

    /// Loads @p code at kOrg, @p init at kDst, @p sub at kSub, records @p frames frames (@p perFrame runs before each)
    void Record(const std::vector<uint8_t>& code, unsigned frames, const std::vector<uint8_t>& init = {0x00},
                const std::function<void(unsigned)>& perFrame = nullptr, const std::vector<uint8_t>& sub = {0xC9})
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("ttd-find-event", "PENTAGON",
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
        for (size_t i = 0; i < code.size(); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kOrg + i), code[i]);
        for (size_t i = 0; i < sub.size(); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kSub + i), sub[i]);
        for (uint16_t a = 0x4000; a < 0x5B00; ++a)
            memory->DirectWriteToZ80Memory(a, 0x00);   // a known picture: all zero
        for (size_t i = 0; i < init.size(); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kDst + i), init[i]);
        for (uint16_t i = 0; i <= 0x100; ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0xFE00 + i), 0xFD);
        const uint8_t isr[] = {0xFB, 0xED, 0x4D};   // ei; reti
        for (size_t i = 0; i < sizeof(isr); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kIsr + i), isr[i]);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = kOrg;
        z80->iff1 = z80->iff2 = 0;

        ASSERT_TRUE(_ttd->StartRecording());
        for (unsigned n = 0; n < frames; ++n)
        {
            if (perFrame)
                perFrame(n);
            _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        }
        _ttd->StopRecording();
        _ttd->SeekTo(_ttd->SessionEndPosition());
    }

    ttd::TTDFindEventResult Mem(ttd::TTDFindEventCond cond, uint16_t value = 0, uint32_t limit = 1, bool last = false,
                                bool exact = false, uint8_t size = 1)
    {
        ttd::TTDFindEventQuery q;
        q.kind = ttd::TTDFindEventKind::Mem;
        q.cond = cond;
        q.address = kDst;
        q.value = value;
        q.bit = static_cast<uint8_t>(value);
        q.limit = limit;
        q.last = last;
        q.exact = exact;
        q.size = size;
        return _ttd->QueryFindEvent(q);
    }

    ttd::TTDFindEventResult Ask(ttd::TTDFindEventQuery q) { return _ttd->QueryFindEvent(q); }

    /// The machine where the query found it
    struct Where
    {
        ttd::TTDTimePoint at;
        uint16_t pc;
        uint8_t value;
    };
    Where Here() const
    {
        return {_ttd->CurrentPosition(), _context->pCore->GetZ80()->pc, _context->pMemory->DirectReadFromZ80Memory(kDst)};
    }
    void ExpectUnmoved(const Where& w) const
    {
        const Where now = Here();
        EXPECT_EQ(now.at.frame, w.at.frame);
        EXPECT_EQ(now.at.tInFrame, w.at.tInFrame);
        EXPECT_EQ(now.pc, w.pc);
        EXPECT_EQ(now.value, w.value);
    }
};

#define TEXT(r) ttd::TTDFindEventText(ttd::TTDFindEventQuery{}, r)

namespace
{
// A counter at #A000 counting down by one every other frame: ei; loop: halt; halt; dec (hl); jr loop
// (ld hl,#A000 before the ei). dec (hl) at #800F
const std::vector<uint8_t> kCountdown = {0x21, 0x00, 0xA0, 0xFB, 0x76, 0x76, 0x35, 0x18, 0xFB};
constexpr uint16_t kDecPc = 0x800D;
}  // namespace

/// T1: decreased, first / last / limit 3, and the machine left where it stood
TEST_F(TTDFindEvent_Test, T1_CounterDecreased)
{
    Record(Im2(kCountdown), 10, {0x05});
    const Where before = Here();
    const ttd::TTDFindEventResult first = Mem(ttd::TTDFindEventCond::Decreased);
    ASSERT_TRUE(first.error.empty()) << first.error;
    ASSERT_EQ(first.hits.size(), 1u) << TEXT(first);
    const ttd::TTDFindEventHit& h = first.hits[0];
    EXPECT_TRUE(h.hasPc);
    EXPECT_EQ(h.pc, kDecPc) << TEXT(first);
    EXPECT_EQ(h.before, 5u);
    EXPECT_EQ(h.after, 4u);
    EXPECT_GT(h.time.tInFrame, 0u);
    EXPECT_NE(h.text.find("dec"), std::string::npos) << h.text;
    EXPECT_FALSE(h.device);
    EXPECT_TRUE(first.more);
    EXPECT_GT(first.nextFromFrame, h.time.frame);
    EXPECT_EQ(first.framesReplayed, 1u);
    ExpectUnmoved(before);

    const ttd::TTDFindEventResult three = Mem(ttd::TTDFindEventCond::Decreased, 0, 3);
    ASSERT_EQ(three.hits.size(), 3u) << TEXT(three);
    for (uint32_t k = 0; k < 3; ++k)
    {
        EXPECT_EQ(three.hits[k].before, 5u - k);
        EXPECT_EQ(three.hits[k].after, 4u - k);
        if (k)
            EXPECT_EQ(three.hits[k].time.frame, three.hits[k - 1].time.frame + 2) << TEXT(three);
    }

    const ttd::TTDFindEventResult last = Mem(ttd::TTDFindEventCond::Decreased, 0, 1, true);
    ASSERT_EQ(last.hits.size(), 1u) << TEXT(last);
    EXPECT_EQ(last.hits[0].after, before.value) << TEXT(last);
    EXPECT_EQ(last.hits[0].before, before.value + 1u);
    EXPECT_EQ(last.hits[0].pc, kDecPc);
    EXPECT_GT(last.hits[0].time.frame, three.hits[2].time.frame);
    EXPECT_TRUE(last.more);
    EXPECT_LT(last.nextToFrame, last.hits[0].time.frame);
    ExpectUnmoved(before);

    // Increased: never
    EXPECT_TRUE(Mem(ttd::TTDFindEventCond::Increased).hits.empty());
}

/// T2: equals / becomes / bit_clear
TEST_F(TTDFindEvent_Test, T2_CounterConditions)
{
    Record(Im2(kCountdown), 10, {0x05});
    const ttd::TTDFindEventResult eq = Mem(ttd::TTDFindEventCond::Equals, 2);
    ASSERT_EQ(eq.hits.size(), 1u) << TEXT(eq);
    EXPECT_EQ(eq.hits[0].after, 2u);
    EXPECT_EQ(eq.hits[0].pc, kDecPc);
    const ttd::TTDFindEventResult becomes = Mem(ttd::TTDFindEventCond::Becomes, 2);
    ASSERT_EQ(becomes.hits.size(), 1u);
    EXPECT_EQ(becomes.hits[0].time.frame, eq.hits[0].time.frame);
    EXPECT_EQ(becomes.hits[0].time.tInFrame, eq.hits[0].time.tInFrame);
    const ttd::TTDFindEventResult differs = Mem(ttd::TTDFindEventCond::Differs, 5);
    ASSERT_EQ(differs.hits.size(), 1u);
    EXPECT_EQ(differs.hits[0].before, 5u);

    // Equals: already 5 when the window starts
    const ttd::TTDFindEventResult eq5 = Mem(ttd::TTDFindEventCond::Equals, 5);
    ASSERT_EQ(eq5.hits.size(), 1u) << TEXT(eq5);
    EXPECT_FALSE(eq5.hits[0].hasPc);
    EXPECT_EQ(eq5.hits[0].time.frame, eq5.fromFrame);

    // Bit 0 goes 1 -> 0 at 5 -> 4 first; 1 -> 0 sets it again only at 4 -> 3? no: 4 = 100b, 3 = 011b sets it
    const ttd::TTDFindEventResult clear0 = Mem(ttd::TTDFindEventCond::BitClear, 0, 2);
    ASSERT_EQ(clear0.hits.size(), 2u) << TEXT(clear0);
    EXPECT_EQ(clear0.hits[0].before, 5u);
    EXPECT_EQ(clear0.hits[1].before, 3u);
    const ttd::TTDFindEventResult set0 = Mem(ttd::TTDFindEventCond::BitSet, 0);
    ASSERT_EQ(set0.hits.size(), 1u);
    EXPECT_EQ(set0.hits[0].after, 3u);

    // exact follows every write: the same changes, each with its PC
    const ttd::TTDFindEventResult exact = Mem(ttd::TTDFindEventCond::Decreased, 0, 3, false, true);
    ASSERT_EQ(exact.hits.size(), 3u) << TEXT(exact);
    const ttd::TTDFindEventResult bounds = Mem(ttd::TTDFindEventCond::Decreased, 0, 3);
    for (size_t k = 0; k < 3; ++k)
    {
        EXPECT_EQ(exact.hits[k].pc, kDecPc);
        EXPECT_EQ(exact.hits[k].time.frame, bounds.hits[k].time.frame);
        EXPECT_EQ(exact.hits[k].before, bounds.hits[k].before);
        EXPECT_FALSE(exact.hits[k].text.empty()) << TEXT(exact);
    }
}

/// T3: a byte set to #FF and back within one frame: boundaries see nothing, exact finds the write
TEST_F(TTDFindEvent_Test, T3_ChangeInsideOneFrame)
{
    // ei; loop: halt; ld a,#FF; ld (#A000),a; xor a; ld (#A000),a; jr loop
    Record(Im2({0xFB, 0x76, 0x3E, 0xFF, 0x32, 0x00, 0xA0, 0xAF, 0x32, 0x00, 0xA0, 0x18, 0xF4}), 5);
    const ttd::TTDFindEventResult bounds = Mem(ttd::TTDFindEventCond::Changed);
    ASSERT_TRUE(bounds.error.empty()) << bounds.error;
    EXPECT_TRUE(bounds.hits.empty()) << TEXT(bounds);
    const ttd::TTDFindEventResult exact = Mem(ttd::TTDFindEventCond::Becomes, 0xFF, 1, false, true);
    ASSERT_EQ(exact.hits.size(), 1u) << TEXT(exact);
    EXPECT_EQ(exact.hits[0].pc, 0x800B);
    EXPECT_EQ(exact.hits[0].after, 0xFFu);
    EXPECT_NE(exact.hits[0].text.find("ld"), std::string::npos) << exact.hits[0].text;
}

/// T4: a word written by LD (nn),HL going #00FF -> #0100: one increase, at the store
TEST_F(TTDFindEvent_Test, T4_WordIncreased)
{
    // ei; halt; ld hl,#0100; ld (#A000),hl; loop: halt; jr loop
    Record(Im2({0xFB, 0x76, 0x21, 0x00, 0x01, 0x22, 0x00, 0xA0, 0x76, 0x18, 0xFD}), 4, {0xFF, 0x00});
    const ttd::TTDFindEventResult inc = Mem(ttd::TTDFindEventCond::Increased, 0, 5, false, false, 2);
    ASSERT_EQ(inc.hits.size(), 1u) << TEXT(inc);
    EXPECT_EQ(inc.hits[0].before, 0x00FFu);
    EXPECT_EQ(inc.hits[0].after, 0x0100u);
    EXPECT_EQ(inc.hits[0].pc, 0x800C);
    // The low byte alone went down: no decrease of the word
    EXPECT_TRUE(Mem(ttd::TTDFindEventCond::Decreased, 0, 5, false, false, 2).hits.empty());
    const ttd::TTDFindEventResult exact = Mem(ttd::TTDFindEventCond::Increased, 0, 5, false, true, 2);
    ASSERT_EQ(exact.hits.size(), 1u) << TEXT(exact);
    EXPECT_EQ(exact.hits[0].after, 0x0100u);
}

namespace
{
// A frame counter at #A000; every frame from the 4th on calls #9000:
// ei; loop: halt; ld hl,#A000; inc (hl); ld a,(hl); cp 4; call nc,#9000; jr loop
const std::vector<uint8_t> kCaller = {0xFB, 0x76, 0x21, 0x00, 0xA0, 0x34, 0x7E, 0xFE, 0x04, 0xD4, 0x00, 0x90, 0x18, 0xF3};
}  // namespace

/// T7: exec: the first run of #9000 is in the frame the counter reaches 4, the last in the last frame
TEST_F(TTDFindEvent_Test, T7_FirstRun)
{
    Record(Im2(kCaller), 8);
    const Where before = Here();
    const ttd::TTDFindEventResult four = Mem(ttd::TTDFindEventCond::Becomes, 4);
    ASSERT_EQ(four.hits.size(), 1u) << TEXT(four);

    ttd::TTDFindEventQuery q;
    q.kind = ttd::TTDFindEventKind::Exec;
    q.address = q.addressTo = kSub;
    const ttd::TTDFindEventResult first = Ask(q);
    ASSERT_TRUE(first.error.empty()) << first.error;
    ASSERT_EQ(first.hits.size(), 1u) << TEXT(first);
    EXPECT_EQ(first.hits[0].pc, kSub);
    EXPECT_EQ(first.hits[0].time.frame, four.hits[0].time.frame) << TEXT(first);
    EXPECT_GT(first.hits[0].time.tInFrame, four.hits[0].time.tInFrame);
    EXPECT_NE(first.hits[0].text.find("ret"), std::string::npos) << first.hits[0].text;
    EXPECT_TRUE(first.more);

    q.last = true;
    q.limit = 2;
    const ttd::TTDFindEventResult last = Ask(q);
    ASSERT_EQ(last.hits.size(), 2u) << TEXT(last);
    EXPECT_GT(last.hits[0].time.frame, last.hits[1].time.frame);
    EXPECT_GT(last.hits[0].time.frame, first.hits[0].time.frame);
    ExpectUnmoved(before);

    // A range that never runs
    q.address = q.addressTo = 0x9100;
    EXPECT_TRUE(Ask(q).hits.empty());
}

/// T8: border: 1, 1, 2 - the repeat of 1 is no change
TEST_F(TTDFindEvent_Test, T8_BorderChanges)
{
    // ei; halt; ld a,1; out (#FE),a; halt; out (#FE),a; halt; ld a,2; out (#FE),a; loop: halt; jr loop
    Record(Im2({0xFB, 0x76, 0x3E, 0x01, 0xD3, 0xFE, 0x76, 0xD3, 0xFE, 0x76, 0x3E, 0x02, 0xD3, 0xFE, 0x76, 0x18, 0xFD}), 6);
    ttd::TTDFindEventQuery q;
    q.kind = ttd::TTDFindEventKind::Border;
    q.limit = 5;
    const ttd::TTDFindEventResult all = Ask(q);
    ASSERT_TRUE(all.error.empty()) << all.error;
    ASSERT_GE(all.hits.size(), 2u) << TEXT(all);
    const ttd::TTDFindEventHit& two = all.hits[all.hits.size() - 1];
    const ttd::TTDFindEventHit& one = all.hits[all.hits.size() - 2];
    EXPECT_EQ(one.after, 1u) << TEXT(all);
    EXPECT_EQ(one.pc, 0x800B);
    EXPECT_EQ(two.after, 2u);
    EXPECT_EQ(two.before, 1u);
    EXPECT_EQ(two.pc, 0x8013);

    q.hasValue = true;
    q.value = 2;
    q.limit = 1;
    const ttd::TTDFindEventResult red = Ask(q);
    ASSERT_EQ(red.hits.size(), 1u) << TEXT(red);
    EXPECT_EQ(red.hits[0].pc, 0x8013);
    EXPECT_EQ(red.hits[0].time.frame, two.time.frame);
}

/// T9: a 7FFD write of #17 by port mask and value
TEST_F(TTDFindEvent_Test, T9_PortWrite)
{
    // di; halt-free: ld bc,#7FFD; ld a,#17; out (c),a; jr $
    Record({0xF3, 0x01, 0xFD, 0x7F, 0x3E, 0x17, 0xED, 0x79, 0x18, 0xFE}, 3);
    ttd::TTDFindEventQuery q;
    q.kind = ttd::TTDFindEventKind::Port;
    q.portOut = true;
    q.port = 0x7FFD;
    q.portMask = 0x8002;
    q.hasValue = true;
    q.value = 0x17;
    const ttd::TTDFindEventResult r = Ask(q);
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.hits.size(), 1u) << TEXT(r);
    EXPECT_EQ(r.hits[0].pc, 0x8006);
    EXPECT_EQ(r.hits[0].after, 0x17u);
    q.value = 0x10;
    EXPECT_TRUE(Ask(q).hits.empty());
}

/// T10: keys pressed while recording: SPACE at frame 3, Q at frame 6, from the input journal
TEST_F(TTDFindEvent_Test, T10_KeyPresses)
{
    uint64_t pressedAt[2] = {};
    DebugKeyboardManager* keys = nullptr;
    // ei; loop: halt; jr loop
    Record(Im2({0xFB, 0x76, 0x18, 0xFD}), 9, {0x00}, [&](unsigned n) {
        if (!keys)
            keys = _context->pDebugManager->GetKeyboardManager();
        if (n == 3)
        {
            pressedAt[0] = _ttd->CurrentPosition().frame;
            keys->PressKey(ZXKEY_SPACE);
        }
        if (n == 4)
            keys->ReleaseKey(ZXKEY_SPACE);
        if (n == 6)
        {
            pressedAt[1] = _ttd->CurrentPosition().frame;
            keys->PressKey(ZXKEY_Q);
        }
        if (n == 7)
            keys->ReleaseKey(ZXKEY_Q);
    });
    ttd::TTDFindEventQuery q;
    q.kind = ttd::TTDFindEventKind::Key;
    q.limit = 5;
    const ttd::TTDFindEventResult any = Ask(q);
    ASSERT_TRUE(any.error.empty()) << any.error;
    ASSERT_EQ(any.hits.size(), 2u) << TEXT(any);
    EXPECT_EQ(any.hits[0].time.frame, pressedAt[0]);
    EXPECT_EQ(any.hits[1].time.frame, pressedAt[1]);
    EXPECT_FALSE(any.more);

    q.key = "Q";
    const ttd::TTDFindEventResult qKey = Ask(q);
    ASSERT_EQ(qKey.hits.size(), 1u) << TEXT(qKey);
    EXPECT_EQ(qKey.hits[0].time.frame, pressedAt[1]);

    q.key = "SPACE";
    q.pressed = false;
    const ttd::TTDFindEventResult released = Ask(q);
    ASSERT_EQ(released.hits.size(), 1u) << TEXT(released);
    EXPECT_EQ(released.hits[0].time.frame, pressedAt[0] + 1);

    q.key = "NOSUCHKEY";
    EXPECT_FALSE(Ask(q).error.empty());

    // The verb: without "pressed" a press is asked for (an absent flag must not read as false)
    for (const auto& [options, want] : std::vector<std::pair<std::map<std::string, std::string>, const char*>>{
             {{{"kind", "key"}, {"key", "SPACE"}, {"text", "true"}}, "SPACE pressed"},
             {{{"kind", "key"}, {"key", "SPACE"}, {"pressed", "false"}, {"text", "true"}}, "SPACE released"}})
    {
        const ttd::TTDReply r = ttd::TTDControl(_context).Execute({"find-event", options});
        ASSERT_TRUE(r.Ok()) << r.message;
        const StateNode* text = r.body.find("text");
        ASSERT_NE(text, nullptr);
        EXPECT_NE(text->s.find(want), std::string::npos) << text->s;
    }
}

namespace
{
// The screen: frame counter at #A000; at count 3 the whole screen (6912 bytes) filled with #AA, at count 5 the
// pixels (6144) cleared; every frame one byte of a "sprite" row at #4800 + count:
// ei; loop: halt; ld hl,#A000; inc (hl); ld a,(hl); ld e,a; ld d,#48; ld (de),a;
//   cp 3; jr nz,+11 (skip fill); ld hl,#4000; ld de,#4001; ld bc,6911; ld (hl),#AA; ldir;
//   ld a,(#A000); cp 5; jr nz,loop; ld hl,#4000; ld de,#4001; ld bc,6143; ld (hl),0; ldir; jr loop
std::vector<uint8_t> ScreenProgram()
{
    std::vector<uint8_t> c = {0xFB};
    const size_t loop = c.size();
    auto jrTo = [&](size_t target) { c.push_back(static_cast<uint8_t>(static_cast<int>(target) - static_cast<int>(c.size() + 1))); };
    c.insert(c.end(), {0x76, 0x21, 0x00, 0xA0, 0x34, 0x7E, 0x5F, 0x16, 0x48, 0x12, 0xFE, 0x03, 0x20});
    const size_t skipFill = c.size();
    c.push_back(0);
    c.insert(c.end(), {0x21, 0x00, 0x40, 0x11, 0x01, 0x40, 0x01, 0xFF, 0x1A, 0x36, 0xAA, 0xED, 0xB0});
    c[skipFill] = static_cast<uint8_t>(c.size() - (skipFill + 1));
    c.insert(c.end(), {0x3A, 0x00, 0xA0, 0xFE, 0x05, 0x20});
    jrTo(loop);
    c.insert(c.end(), {0x21, 0x00, 0x40, 0x11, 0x01, 0x40, 0x01, 0xFF, 0x17, 0x36, 0x00, 0xED, 0xB0, 0x18});
    jrTo(loop);
    return c;
}
}  // namespace

/// T5: a whole-screen fill is a scene change, also when it takes two frames (an LDIR of 6912 bytes runs ~145k T);
/// a one-byte sprite is not; cleared finds the frame the pixels became one value
TEST_F(TTDFindEvent_Test, T5_SceneChange)
{
    Record(Im2(ScreenProgram()), 12);
    const Where before = Here();
    const ttd::TTDFindEventResult three = Mem(ttd::TTDFindEventCond::Becomes, 3);
    const ttd::TTDFindEventResult five = Mem(ttd::TTDFindEventCond::Becomes, 5);
    ASSERT_EQ(three.hits.size(), 1u);
    ASSERT_EQ(five.hits.size(), 1u);

    ttd::TTDFindEventQuery q;
    q.kind = ttd::TTDFindEventKind::Screen;
    q.cond = ttd::TTDFindEventCond::Change;
    q.minChanged = 50;
    q.limit = 10;
    const ttd::TTDFindEventResult scene = Ask(q);
    ASSERT_TRUE(scene.error.empty()) << scene.error;
    ASSERT_EQ(scene.hits.size(), 2u) << TEXT(scene);   // the fill and the pixel clear, each over two frames
    EXPECT_EQ(scene.hits[0].time.frame, three.hits[0].time.frame) << TEXT(scene);
    EXPECT_GE(scene.hits[0].before, 6900u) << TEXT(scene);
    EXPECT_NE(scene.hits[0].why.find("over frames"), std::string::npos) << scene.hits[0].why;
    EXPECT_GE(scene.hits[1].time.frame, five.hits[0].time.frame) << TEXT(scene);
    EXPECT_LE(scene.hits[1].time.frame, five.hits[0].time.frame + 2) << TEXT(scene);
    EXPECT_GE(scene.hits[1].before, 6000u) << TEXT(scene);
    EXPECT_EQ(scene.framesReplayed, 0u);

    q.minChanged = 0;   // any change, frame by frame: every frame writes the sprite byte
    q.limit = 100;
    const ttd::TTDFindEventResult every = Ask(q);
    EXPECT_GE(every.hits.size(), 10u) << TEXT(every);

    // Cleared: every pixel byte one value - the #AA fill, then the clear (the sprite byte comes before it)
    q.cond = ttd::TTDFindEventCond::Cleared;
    q.limit = 5;
    const ttd::TTDFindEventResult cleared = Ask(q);
    ASSERT_EQ(cleared.hits.size(), 2u) << TEXT(cleared);
    EXPECT_EQ(cleared.hits[0].time.frame, scene.hits[0].time.frame + 1) << TEXT(cleared);   // the fill's last frame
    EXPECT_GT(cleared.hits[1].time.frame, scene.hits[1].time.frame) << TEXT(cleared);
    EXPECT_LE(cleared.hits[1].time.frame, scene.hits[1].time.frame + 2) << TEXT(cleared);
    ExpectUnmoved(before);
}

/// T6: page 7 holds another picture; the 7FFD switch to the shadow screen is the scene change, not its drawing
TEST_F(TTDFindEvent_Test, T6_ShadowScreenSwitch)
{
    // di; ld bc,#7FFD; ld a,#07; out (c),a; ld hl,#C000; ld de,#C001; ld bc,6911; ld (hl),#55; ldir;
    // ld bc,#2000; wait: dec bc; ld a,b; or c; jr nz,wait; ld bc,#7FFD; ld a,#0F; out (c),a; jr $
    Record({0xF3, 0x01, 0xFD, 0x7F, 0x3E, 0x07, 0xED, 0x79, 0x21, 0x00, 0xC0, 0x11, 0x01, 0xC0, 0x01, 0xFF, 0x1A,
            0x36, 0x55, 0xED, 0xB0, 0x01, 0x00, 0x20, 0x0B, 0x78, 0xB1, 0x20, 0xFB, 0x01, 0xFD, 0x7F, 0x3E, 0x0F,
            0xED, 0x79, 0x18, 0xFE},
           8);
    ttd::TTDFindEventQuery q;
    q.kind = ttd::TTDFindEventKind::Screen;
    q.cond = ttd::TTDFindEventCond::Change;
    q.limit = 10;
    const ttd::TTDFindEventResult r = Ask(q);
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.hits.size(), 1u) << TEXT(r);

    ttd::TTDFindEventQuery p;
    p.kind = ttd::TTDFindEventKind::Port;
    p.port = 0x7FFD;
    p.portMask = 0x8002;
    p.hasValue = true;
    p.value = 0x0F;
    const ttd::TTDFindEventResult sw = Ask(p);
    ASSERT_EQ(sw.hits.size(), 1u) << TEXT(sw);
    EXPECT_EQ(r.hits[0].time.frame, sw.hits[0].time.frame) << TEXT(r);
}

/// T11: a debugger edit (a barrier) inside the recording: the jump it makes is no hit, the barrier is listed
TEST_F(TTDFindEvent_Test, T11_BarrierIsNotAChange)
{
    // ei; loop: halt; jr loop - nothing writes #A000
    Record(Im2({0xFB, 0x76, 0x18, 0xFD}), 6, {0x00}, [&](unsigned n) {
        if (n == 3)
        {
            _context->pMemory->DirectWriteToZ80Memory(kDst, 0x42);
            _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::DebuggerEdit, "poke #A000");
        }
    });
    const ttd::TTDFindEventResult r = Mem(ttd::TTDFindEventCond::Changed);
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_TRUE(r.hits.empty()) << TEXT(r);
    ASSERT_EQ(r.barriers.size(), 1u) << TEXT(r);
    EXPECT_NE(r.barriers[0].find("poke"), std::string::npos);
}

/// T12: the window, limit, next_from_frame, and bad queries
TEST_F(TTDFindEvent_Test, T12_WindowAndLimits)
{
    Record(Im2(kCountdown), 10, {0x05});
    const ttd::TTDFindEventResult two = Mem(ttd::TTDFindEventCond::Decreased, 0, 2);
    ASSERT_EQ(two.hits.size(), 2u);
    ASSERT_TRUE(two.more);
    ttd::TTDFindEventQuery q;
    q.kind = ttd::TTDFindEventKind::Mem;
    q.cond = ttd::TTDFindEventCond::Decreased;
    q.address = kDst;
    q.fromFrame = two.nextFromFrame;
    const ttd::TTDFindEventResult third = Ask(q);
    ASSERT_EQ(third.hits.size(), 1u) << TEXT(third);
    EXPECT_EQ(third.hits[0].before, 3u);
    EXPECT_EQ(third.fromFrame, two.nextFromFrame);

    // A window that ends before the first decrease
    q.fromFrame = 0;
    q.toFrame = two.hits[0].time.frame - 1;
    EXPECT_TRUE(Ask(q).hits.empty());
    // The limit is at most 100
    q.toFrame = UINT64_MAX;
    q.limit = 1000;
    EXPECT_LE(Ask(q).hits.size(), 100u);
    // A window past the history
    q.fromFrame = 1'000'000;
    EXPECT_FALSE(Ask(q).error.empty());
    // A screen condition on mem
    q.fromFrame = 0;
    q.cond = ttd::TTDFindEventCond::Cleared;
    EXPECT_FALSE(Ask(q).error.empty());

    ttd::TTDFindEventKind kind;
    ttd::TTDFindEventCond cond;
    EXPECT_TRUE(ttd::TTDFindEventKindByName("screen", kind));
    EXPECT_EQ(kind, ttd::TTDFindEventKind::Screen);
    EXPECT_FALSE(ttd::TTDFindEventKindByName("nope", kind));
    EXPECT_TRUE(ttd::TTDFindEventCondByName("bit_set", cond));
    EXPECT_EQ(cond, ttd::TTDFindEventCond::BitSet);
    EXPECT_FALSE(ttd::TTDFindEventCondByName("nope", cond));
    EXPECT_NE(ttd::TTDFindEventText(q, two).find("2 hits"), std::string::npos) << ttd::TTDFindEventText(q, two);
}
