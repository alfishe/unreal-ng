#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcausechain.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

/// The TTD cause chain (docs/inprogress/2026-11-02-ttd-cause-chain, plan.md S1-S10): small programs run by the CPU
/// on a Pentagon, recorded a few frames, then asked where a value came from; every link checked
class TTDCauseChain_Test : public ::testing::Test
{
protected:
    static constexpr uint16_t kOrg = 0x8000;   // the program (page 2)
    static constexpr uint16_t kSrc = 0x9000;   // data, never written (page 2)
    static constexpr uint16_t kDst = 0xA000;   // where the programs store
    static constexpr uint16_t kIsr = 0xFDFD;   // the IM 2 handler: I = #FE, the table at #FE00 all #FD

    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }

    /// Loads @p code at kOrg (and @p isr at kIsr), @p data at kSrc, records @p frames frames from the program's
    /// start and stops at the recording's end
    void Record(const std::vector<uint8_t>& code, unsigned frames, const std::vector<uint8_t>& data = {0x03},
                const std::vector<uint8_t>& isr = {0xFB, 0xED, 0x4D})
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("ttd-cause-chain", "PENTAGON",
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
        for (size_t i = 0; i < data.size(); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kSrc + i), data[i]);
        for (uint16_t i = 0; i <= 0x100; ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0xFE00 + i), 0xFD);
        for (size_t i = 0; i < isr.size(); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kIsr + i), isr[i]);
        memory->DirectWriteToZ80Memory(kDst, 0xEE);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = kOrg;
        z80->iff1 = z80->iff2 = 0;

        ASSERT_TRUE(_ttd->StartRecording());
        for (unsigned n = 0; n < frames; ++n)
            _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        _ttd->StopRecording();
        _ttd->SeekTo(_ttd->SessionEndPosition());
    }

    ttd::TTDCauseResult AskMemory(uint16_t address, uint32_t depth = 4, uint32_t maxLinks = 32)
    {
        ttd::TTDCauseQuery q;
        q.memory = true;
        q.address = address;
        q.depth = depth;
        q.maxLinks = maxLinks;
        return _ttd->QueryCauseChain(q);
    }

    ttd::TTDCauseResult AskRegister(const std::string& reg, uint32_t depth = 4)
    {
        ttd::TTDCauseQuery q;
        q.memory = false;
        q.reg = reg;
        q.depth = depth;
        return _ttd->QueryCauseChain(q);
    }

    static std::string Name(const ttd::TTDCauseLink& l) { return ttd::TTDCauseNameText(l.name); }
    static std::string Kind(const ttd::TTDCauseLink& l) { return ttd::TTDCauseKindName(l.kind); }
};

#define EXPECT_LINK(r, i, wantName, wantKind, wantValue, wantPc)                                    \
    do                                                                                               \
    {                                                                                                \
        ASSERT_LT(size_t(i), (r).links.size()) << ttd::TTDCauseChainText(r);                         \
        const ttd::TTDCauseLink& l_ = (r).links[i];                                                  \
        EXPECT_EQ(Name(l_), wantName) << "link " << i << "\n" << ttd::TTDCauseChainText(r);          \
        EXPECT_EQ(Kind(l_), wantKind) << "link " << i << "\n" << ttd::TTDCauseChainText(r);          \
        EXPECT_EQ(int(l_.value), int(wantValue)) << "link " << i << "\n" << ttd::TTDCauseChainText(r); \
        if ((wantPc) >= 0)                                                                           \
        {                                                                                            \
            EXPECT_TRUE(l_.hasProducer) << "link " << i;                                             \
            EXPECT_EQ(int(l_.pc), int(wantPc)) << "link " << i << "\n" << ttd::TTDCauseChainText(r); \
        }                                                                                            \
    } while (0)

/// S1: LD A,(src) : LD (dst),A. The store found by FindAccesses (an older frame), the load by the frame trace
TEST_F(TTDCauseChain_Test, S1_LoadThenStore)
{
    // di; ld a,(#9000); ld (#A000),a; jr $
    Record({0xF3, 0x3A, 0x00, 0x90, 0x32, 0x00, 0xA0, 0x18, 0xFE}, 3);
    const ttd::TTDCauseResult r = AskMemory(kDst);
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.links.size(), 3u) << ttd::TTDCauseChainText(r);
    EXPECT_LINK(r, 0, "(#A000 RAM 2)", "store", 0x03, 0x8004);
    EXPECT_FALSE(r.links[0].text.empty());
    ASSERT_EQ(r.links[0].inputs.size(), 1u);
    EXPECT_EQ(r.links[0].inputs[0], "A=#03");
    EXPECT_LINK(r, 1, "A", "load", 0x03, 0x8001);
    EXPECT_EQ(r.links[1].parent, 0);
    EXPECT_LINK(r, 2, "(#9000 RAM 2)", "initial", 0x03, -1);
    EXPECT_EQ(r.links[2].depth, 2u);
    EXPECT_GE(r.framesTraced, 2u);
}

/// S2: LDIR: each byte from the byte read at HL
TEST_F(TTDCauseChain_Test, S2_BlockCopy)
{
    // di; ld hl,#9000; ld de,#A000; ld bc,4; ldir; jr $
    Record({0xF3, 0x21, 0x00, 0x90, 0x11, 0x00, 0xA0, 0x01, 0x04, 0x00, 0xED, 0xB0, 0x18, 0xFE}, 2,
           {0x11, 0x22, 0x33, 0x44});
    const ttd::TTDCauseResult r = AskMemory(kDst + 2);
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.links.size(), 2u) << ttd::TTDCauseChainText(r);
    EXPECT_LINK(r, 0, "(#A002 RAM 2)", "block", 0x33, 0x800A);
    EXPECT_LINK(r, 1, "(#9002 RAM 2)", "initial", 0x33, -1);
}

/// S3: ADD A,B: a computed value with both inputs followed (B a constant)
TEST_F(TTDCauseChain_Test, S3_ComputedTwoInputs)
{
    // di; ld b,5; ld a,(#9000); add a,b; ld (#A000),a; jr $
    Record({0xF3, 0x06, 0x05, 0x3A, 0x00, 0x90, 0x80, 0x32, 0x00, 0xA0, 0x18, 0xFE}, 2);
    const ttd::TTDCauseResult r = AskMemory(kDst);
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.links.size(), 5u) << ttd::TTDCauseChainText(r);
    EXPECT_LINK(r, 0, "(#A000 RAM 2)", "store", 0x08, 0x8007);
    EXPECT_LINK(r, 1, "A", "computed", 0x08, 0x8006);
    ASSERT_EQ(r.links[1].inputs.size(), 2u);
    EXPECT_LINK(r, 2, "A", "load", 0x03, 0x8003);
    EXPECT_EQ(r.links[2].parent, 1);
    EXPECT_LINK(r, 3, "(#9000 RAM 2)", "initial", 0x03, -1);
    EXPECT_LINK(r, 4, "B", "constant", 0x05, 0x8001);
    EXPECT_EQ(r.links[4].parent, 1);
}

namespace
{
// di; ld sp,#BF00; ld a,#FE; ld i,a; im 2; ld hl,#1234; push hl; ei; halt; jr $
const std::vector<uint8_t> kStackProgram = {0xF3, 0x31, 0x00, 0xBF, 0x3E, 0xFE, 0xED, 0x47, 0xED, 0x5E, 0x21,
                                            0x34, 0x12, 0xE5, 0xFB, 0x76, 0x18, 0xFE};
// The handler, entered once: pop bc (the interrupt's return address); pop de (HL); ld a,e; ld (#A000),a; jr $
const std::vector<uint8_t> kStackIsr = {0xC1, 0xD1, 0x7B, 0x32, 0x00, 0xA0, 0x18, 0xFE};
}  // namespace

/// S4: PUSH HL in the main program, POP DE in the handler, E stored
TEST_F(TTDCauseChain_Test, S4_PushPopThroughTheHandler)
{
    Record(kStackProgram, 3, {0x03}, kStackIsr);
    const ttd::TTDCauseResult r = AskMemory(kDst);
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.links.size(), 5u) << ttd::TTDCauseChainText(r);
    EXPECT_LINK(r, 0, "(#A000 RAM 2)", "store", 0x34, kIsr + 3);
    EXPECT_LINK(r, 1, "A", "copy", 0x34, kIsr + 2);
    EXPECT_LINK(r, 2, "E", "load", 0x34, kIsr + 1);
    EXPECT_LINK(r, 3, "(#BEFE RAM 2)", "store", 0x34, 0x800D);
    EXPECT_LINK(r, 4, "L", "constant", 0x34, 0x800A);
}

/// S10: the interrupt's push read back with POP: the interrupted PC, a leaf
TEST_F(TTDCauseChain_Test, S10_InterruptPush)
{
    Record(kStackProgram, 3, {0x03}, kStackIsr);
    const ttd::TTDCauseResult r = AskRegister("BC");
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.links.size(), 4u) << ttd::TTDCauseChainText(r);
    // The HALT at #800F: the interrupt pushed the address after it
    EXPECT_LINK(r, 0, "B", "load", 0x80, kIsr);
    EXPECT_LINK(r, 1, "(#BEFD RAM 2)", "interrupt", 0x80, 0x8010);
    EXPECT_NE(r.links[1].detail.find("#8010"), std::string::npos) << r.links[1].detail;
    EXPECT_LINK(r, 2, "C", "load", 0x10, kIsr);
    EXPECT_LINK(r, 3, "(#BEFC RAM 2)", "interrupt", 0x10, 0x8010);
}

/// S5: the copies of EX DE,HL, EXX and EX AF,AF'
TEST_F(TTDCauseChain_Test, S5_Exchanges)
{
    // di; ld a,(#9000); ld l,a; ex de,hl; exx; ex af,af'; xor a; jr $
    Record({0xF3, 0x3A, 0x00, 0x90, 0x6F, 0xEB, 0xD9, 0x08, 0xAF, 0x18, 0xFE}, 2);
    const ttd::TTDCauseResult r = AskRegister("E'");
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.links.size(), 5u) << ttd::TTDCauseChainText(r);
    EXPECT_LINK(r, 0, "E'", "copy", 0x03, 0x8006);
    EXPECT_LINK(r, 1, "E", "copy", 0x03, 0x8005);
    EXPECT_LINK(r, 2, "L", "copy", 0x03, 0x8004);
    EXPECT_LINK(r, 3, "A", "load", 0x03, 0x8001);
    EXPECT_LINK(r, 4, "(#9000 RAM 2)", "initial", 0x03, -1);

    const ttd::TTDCauseResult a = AskRegister("A'");
    ASSERT_GE(a.links.size(), 2u) << ttd::TTDCauseChainText(a);
    EXPECT_LINK(a, 0, "A'", "copy", 0x03, 0x8007);
    EXPECT_LINK(a, 1, "A", "load", 0x03, 0x8001);

    const ttd::TTDCauseResult zero = AskRegister("A");
    ASSERT_EQ(zero.links.size(), 1u) << ttd::TTDCauseChainText(zero);
    EXPECT_LINK(zero, 0, "A", "constant", 0x00, 0x8008);
}

/// S11: a loop's self update is one link: LDIR's HL repeated 16 times, then the LD HL,nn before the loop
TEST_F(TTDCauseChain_Test, S11_LoopRunIsOneLink)
{
    // di; ld hl,#9000; ld de,#A000; ld bc,#0010; ldir; jr $
    Record({0xF3, 0x21, 0x00, 0x90, 0x11, 0x00, 0xA0, 0x01, 0x10, 0x00, 0xED, 0xB0, 0x18, 0xFE}, 2);
    const ttd::TTDCauseResult r = AskRegister("HL");
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.links.size(), 5u) << ttd::TTDCauseChainText(r);
    EXPECT_LINK(r, 0, "H", "computed", 0x90, 0x800A);
    EXPECT_NE(r.links[0].detail.find("repeated 16 times"), std::string::npos) << r.links[0].detail;
    EXPECT_LINK(r, 1, "H", "constant", 0x90, 0x8001);
    EXPECT_LINK(r, 2, "L", "constant", 0x00, 0x8001);
    // The low byte from itself alone: LDIR's L is not from H
    EXPECT_LINK(r, 3, "L", "computed", 0x10, 0x800A);
    ASSERT_EQ(r.links[3].inputs.size(), 1u) << ttd::TTDCauseChainText(r);
    EXPECT_EQ(r.links[3].inputs[0], "L=#00");
    EXPECT_LINK(r, 4, "L", "constant", 0x00, 0x8001);
    EXPECT_NE(r.links[4].stop.find("seen"), std::string::npos) << ttd::TTDCauseChainText(r);
}

/// A register never written in the history: held since the session start; the second byte reuses the frames the
/// first one traced
TEST_F(TTDCauseChain_Test, RegisterNeverWrittenIsInitial)
{
    Record({0xF3, 0x3A, 0x00, 0x90, 0x32, 0x00, 0xA0, 0x18, 0xFE}, 3, {0x03});
    const ttd::TTDCauseResult r = AskRegister("IX");
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.links.size(), 2u) << ttd::TTDCauseChainText(r);
    EXPECT_EQ(Kind(r.links[0]), "initial") << ttd::TTDCauseChainText(r);
    EXPECT_EQ(Kind(r.links[1]), "initial") << ttd::TTDCauseChainText(r);
    EXPECT_LE(r.framesTraced, 4u) << ttd::TTDCauseChainText(r);
}

/// S6: IN A,(#FE) stored: a port read, its port and device
TEST_F(TTDCauseChain_Test, S6_PortRead)
{
    // di; ld a,#7F; in a,(#FE); ld (#A000),a; jr $
    Record({0xF3, 0x3E, 0x7F, 0xDB, 0xFE, 0x32, 0x00, 0xA0, 0x18, 0xFE}, 2);
    const ttd::TTDCauseResult r = AskMemory(kDst);
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.links.size(), 2u) << ttd::TTDCauseChainText(r);
    EXPECT_EQ(Kind(r.links[0]), "store");
    EXPECT_EQ(Kind(r.links[1]), "port") << ttd::TTDCauseChainText(r);
    EXPECT_EQ(r.links[1].pc, 0x8003);
    EXPECT_EQ(r.links[1].value, r.links[0].value);
    EXPECT_NE(r.links[1].detail.find("port #7FFE"), std::string::npos) << r.links[1].detail;
}

/// S7: loaded in the first frame, stored three frames later: the register search crosses frames
TEST_F(TTDCauseChain_Test, S7_AcrossFrames)
{
    // di; ld sp,#BF00; ld a,#FE; ld i,a; im 2; ld a,(#9000); ei; halt; halt; halt; ld (#A000),a; di; jr $
    Record({0xF3, 0x31, 0x00, 0xBF, 0x3E, 0xFE, 0xED, 0x47, 0xED, 0x5E, 0x3A, 0x00, 0x90, 0xFB, 0x76, 0x76, 0x76,
            0x32, 0x00, 0xA0, 0xF3, 0x18, 0xFE},
           6);
    const ttd::TTDCauseResult r = AskMemory(kDst);
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.links.size(), 3u) << ttd::TTDCauseChainText(r);
    EXPECT_LINK(r, 0, "(#A000 RAM 2)", "store", 0x03, 0x8011);
    EXPECT_LINK(r, 1, "A", "load", 0x03, 0x800A);
    EXPECT_GE(r.links[0].time.frame, r.links[1].time.frame + 2) << ttd::TTDCauseChainText(r);
    EXPECT_LINK(r, 2, "(#9000 RAM 2)", "initial", 0x03, -1);
}

/// S8: LD (HL),n: a constant
TEST_F(TTDCauseChain_Test, S8_ImmediateStore)
{
    // di; ld hl,#A000; ld (hl),#5A; jr $
    Record({0xF3, 0x21, 0x00, 0xA0, 0x36, 0x5A, 0x18, 0xFE}, 2);
    const ttd::TTDCauseResult r = AskMemory(kDst);
    ASSERT_EQ(r.links.size(), 1u) << ttd::TTDCauseChainText(r);
    EXPECT_LINK(r, 0, "(#A000 RAM 2)", "constant", 0x5A, 0x8004);
}

/// S9: a byte never written: held since the session start; a ROM byte: rom
TEST_F(TTDCauseChain_Test, S9_NeverWrittenAndRom)
{
    Record({0xF3, 0x18, 0xFE}, 2, {0x03, 0x77});
    const ttd::TTDCauseResult r = AskMemory(kSrc + 1);
    ASSERT_EQ(r.links.size(), 1u) << ttd::TTDCauseChainText(r);
    EXPECT_LINK(r, 0, "(#9001 RAM 2)", "initial", 0x77, -1);

    const ttd::TTDCauseResult rom = AskMemory(0x0010);
    ASSERT_EQ(rom.links.size(), 1u) << ttd::TTDCauseChainText(rom);
    EXPECT_EQ(Kind(rom.links[0]), "rom");
}

/// The limits: depth and max_links cut the tree and say so
TEST_F(TTDCauseChain_Test, DepthAndLinkLimits)
{
    Record({0xF3, 0x06, 0x05, 0x3A, 0x00, 0x90, 0x80, 0x32, 0x00, 0xA0, 0x18, 0xFE}, 2);
    const ttd::TTDCauseResult shallow = AskMemory(kDst, 1);
    ASSERT_EQ(shallow.links.size(), 2u) << ttd::TTDCauseChainText(shallow);
    EXPECT_EQ(shallow.links[1].stop, "depth");

    const ttd::TTDCauseResult narrow = AskMemory(kDst, 4, 3);
    ASSERT_EQ(narrow.links.size(), 3u) << ttd::TTDCauseChainText(narrow);
    EXPECT_EQ(narrow.links[1].stop, "max_links") << ttd::TTDCauseChainText(narrow);

    ttd::TTDCauseQuery bad;
    bad.memory = false;
    bad.reg = "Q";
    EXPECT_FALSE(_ttd->QueryCauseChain(bad).error.empty());
}

/// A register target at an earlier moment, and the machine where it stood after the queries
TEST_F(TTDCauseChain_Test, RegisterAtMomentAndMachineRestored)
{
    Record({0xF3, 0x06, 0x05, 0x3A, 0x00, 0x90, 0x80, 0x32, 0x00, 0xA0, 0x18, 0xFE}, 3);
    Z80* z80 = _context->pCore->GetZ80();
    const ttd::TTDTimePoint before = _ttd->CurrentPosition();
    const uint16_t pc = z80->pc, sp = z80->sp, af = z80->af, bc = z80->bc;
    const uint8_t dst = _context->pMemory->DirectReadFromZ80Memory(kDst);

    const ttd::TTDCauseResult store = AskMemory(kDst);
    ASSERT_FALSE(store.links.empty());
    ttd::TTDCauseQuery q;
    q.memory = false;
    q.reg = "A";
    q.hasTime = true;
    q.at = store.links[1].time;   // the start of ADD A,B: A holds the loaded #03
    const ttd::TTDCauseResult r = _ttd->QueryCauseChain(q);
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_GE(r.links.size(), 2u) << ttd::TTDCauseChainText(r);
    EXPECT_LINK(r, 0, "A", "load", 0x03, 0x8003);

    EXPECT_EQ(_ttd->CurrentPosition(), before);
    EXPECT_EQ(z80->pc, pc);
    EXPECT_EQ(z80->sp, sp);
    EXPECT_EQ(z80->af, af);
    EXPECT_EQ(z80->bc, bc);
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(kDst), dst);
    EXPECT_FALSE(ttd::TTDCauseChainText(r).empty());
}
