/// @file autodecision_test.cpp
/// @brief The port-read watch's classes (retro-devices.md §6) and the heuristic that turns its evidence into the
/// auto connector (mapping-and-profiles.md §6.1 step 2): dedicated ports beat half-rows, the whole keyboard is a
/// hint, half a window decides nothing (C6).

#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <vector>

#include "emulator/io/controllers/portreadwatch.h"
#include "profiles/autodecision.h"

using namespace controllers;

namespace
{
/// @p frames frames, each reading every port of @p ports once
PortReadEvidence Watch(std::initializer_list<uint16_t> ports, unsigned frames = kPortReadWindow)
{
    PortReadWatch watch;
    for (unsigned frame = 0; frame < frames; ++frame)
    {
        for (uint16_t port : ports)
            watch.OnRead(port);
        watch.EndFrame();
    }
    return watch.Evidence();
}
}  // namespace

TEST(AutoDecision_Test, Watch_ClassesAndTheWindow)
{
    PortReadWatch watch;
    for (uint16_t port : {0x001F, 0x001F, 0x0037, 0x007F, 0xFBDF, 0xEFFE, 0x00FE, 0xFFFE, 0xFFFD})
        watch.OnRead(port);
    watch.EndFrame();
    PortReadEvidence evidence = watch.Evidence();
    EXPECT_EQ(evidence.window, 1u);
    EXPECT_EQ(evidence.Reads(PortReadClass::Kempston), 2.0f);
    EXPECT_EQ(evidence.Frames(PortReadClass::Kempston2), 1u);
    EXPECT_EQ(evidence.Frames(PortReadClass::Fuller), 1u);
    EXPECT_EQ(evidence.Frames(PortReadClass::KMouse), 1u) << "#FBDF is the mouse, not a Kempston alias";
    EXPECT_EQ(evidence.Frames(PortReadClass::Row4), 1u) << "#EFFE";
    EXPECT_EQ(evidence.Frames(PortReadClass::RowsMulti), 1u) << "#00FE: every half-row at once";
    EXPECT_EQ(evidence.Frames(PortReadClass::Row7), 0u) << "#FFFE selects no half-row (an EAR read)";

    for (int frame = 0; frame < 60; ++frame)
        watch.EndFrame();
    evidence = watch.Evidence();
    EXPECT_EQ(evidence.framesObserved, 61u);
    EXPECT_EQ(evidence.window, kPortReadWindow);
    EXPECT_EQ(evidence.Frames(PortReadClass::Kempston), 0u) << "the first frame left the window";
}

TEST(AutoDecision_Test, Decide_PortsRowsAndTheKeyboard)
{
    EXPECT_FALSE(DecideFromPortReads(Watch({0x001F}, 30)).decisive) << "half a window";

    AutoDecision decision = DecideFromPortReads(Watch({0x001F, 0xEFFE}));
    ASSERT_TRUE(decision.decisive);
    EXPECT_EQ(decision.device, "kempston-joystick") << "the dedicated port beats the half-row";
    EXPECT_EQ(decision.connector, "kempston");
    EXPECT_NE(decision.reason.find("#1F read in 50 of 50"), std::string::npos) << decision.reason;

    decision = DecideFromPortReads(Watch({0xEFFE}));
    EXPECT_EQ(decision.label, "Sinclair 1");
    EXPECT_EQ(decision.optionValue, "1");
    EXPECT_EQ(DecideFromPortReads(Watch({0xF7FE})).label, "Sinclair 2");
    EXPECT_EQ(DecideFromPortReads(Watch({0xEFFE, 0xF7FE})).device, "cursor-joystick");
    decision = DecideFromPortReads(Watch({0xFBFE, 0xFDFE, 0xDFFE, 0x7FFE}));
    EXPECT_EQ(decision.device, "keys-joystick");
    EXPECT_EQ(decision.optionValue, "up:q,down:a,left:o,right:p,fire:space");
    EXPECT_EQ(DecideFromPortReads(Watch({0x0037})).connector, "next.right");
    EXPECT_EQ(DecideFromPortReads(Watch({0x007F})).device, "fuller-joystick");

    // The ROM's key scan: every half-row, a hint and no plug
    decision = DecideFromPortReads(Watch({0xFEFE, 0xFDFE, 0xFBFE, 0xF7FE, 0xEFFE, 0xDFFE, 0xBFFE, 0x7FFE}));
    EXPECT_FALSE(decision.decisive);
    EXPECT_TRUE(decision.keyboard);
    EXPECT_FALSE(DecideFromPortReads(Watch({0x7FFE})).decisive) << "SPACE / BREAK alone decides nothing";
}

namespace
{
/// Guest code for the row-bit sampler: @p bytes at address 0x8000, #FF elsewhere
struct Code
{
    std::vector<uint8_t> bytes;
    static uint8_t Read(const void* context, uint16_t address)
    {
        const auto& code = static_cast<const Code*>(context)->bytes;
        const unsigned offset = static_cast<uint16_t>(address - 0x8000);
        return offset < code.size() ? code[offset] : uint8_t{0xFF};
    }
    /// The bits tested after the IN at 0x8000 (two bytes long)
    uint8_t Tested() const
    {
        return TestedBitsAfterIn(&Code::Read, this, 0x8002);
    }
};

/// A full window: @p every classes read in each frame, half-rows with the row bits of @p bits (Row0 .. Row7)
PortReadEvidence Evidence(std::initializer_list<PortReadClass> every, std::array<uint8_t, 8> bits = {})
{
    PortReadEvidence evidence;
    evidence.active = true;
    evidence.framesObserved = kPortReadWindow;
    evidence.window = kPortReadWindow;
    for (PortReadClass cls : every)
    {
        evidence.framesWithReads[static_cast<size_t>(cls)] = kPortReadWindow;
        evidence.readsPerFrame[static_cast<size_t>(cls)] = 1.0f;
    }
    evidence.rowBits = bits;
    return evidence;
}
}  // namespace

TEST(AutoDecision_Test, RowBits_TheCodeAfterTheIn)
{
    // IN A,(#FE); CPL; AND #1F
    EXPECT_EQ((Code{{0xDB, 0xFE, 0x2F, 0xE6, 0x1F}}.Tested()), 0x1F);
    // IN A,(C); RRA; JR NC; RRA; JR NC; RRA; JP NC,nn: the shifts into the carry test bits 0, 1, 2
    EXPECT_EQ((Code{{0xED, 0x78, 0x1F, 0x30, 0x10, 0x1F, 0x30, 0x10, 0x1F, 0xD2, 0x00, 0x90}}.Tested()), 0x07);
    // IN E,(C); BIT 4,E; JR Z; BIT 0,E
    EXPECT_EQ((Code{{0xED, 0x58, 0xCB, 0x63, 0x28, 0x02, 0xCB, 0x43}}.Tested()), 0x11);
    // IN A,(#FE); RRA; AND 1: bit 1 of the read
    EXPECT_EQ((Code{{0xDB, 0xFE, 0x1F, 0xE6, 0x01}}.Tested()), 0x02);
    // IN D,(C); LD A,D; AND #03 (LD (IX+5),#12 in between: four bytes skipped)
    EXPECT_EQ((Code{{0xED, 0x50, 0xDD, 0x36, 0x05, 0x12, 0x7A, 0xE6, 0x03}}.Tested()), 0x03);
    // IN A,(#FE); JP nn; AND 1: the scan stops at the jump
    EXPECT_EQ((Code{{0xDB, 0xFE, 0xC3, 0x00, 0x90, 0xE6, 0x01}}.Tested()), 0);
    // IN A,(#FE); AND 1 past the eighth instruction: not seen
    EXPECT_EQ((Code{{0xDB, 0xFE, 0, 0, 0, 0, 0, 0, 0, 0, 0xE6, 0x01}}.Tested()), 0);
    // INI (ED A2) loads no register
    EXPECT_EQ((Code{{0xED, 0xA2, 0xE6, 0x01}}.Tested()), 0);
}

TEST(AutoDecision_Test, Watch_RowBitsAndRegisterReads)
{
    const Code code{{0xDB, 0xFE, 0xE6, 0x1F}};
    PortReadWatch watch;
    watch.SetCodeReader(&Code::Read, &code);
    for (int i = 0; i < 3; ++i)
        watch.OnRead(0xEFFE, 0x8002);  // the second and third reads take the first scan's bits
    watch.OnRead(0x7FFE);              // no code address: no row bits
    watch.OnRegisterRead(PortReadClass::AyR14);
    watch.OnRegisterRead(PortReadClass::NextB2);
    watch.EndFrame();
    const PortReadEvidence evidence = watch.Evidence();
    EXPECT_EQ(evidence.RowBits(PortReadClass::Row4), 0x1F);
    EXPECT_EQ(evidence.RowBits(PortReadClass::Row7), 0);
    EXPECT_EQ(evidence.RowBits(PortReadClass::Kempston), 0) << "not a half-row";
    EXPECT_EQ(evidence.Frames(PortReadClass::AyR14), 1u);
    EXPECT_EQ(evidence.Frames(PortReadClass::NextB2), 1u);
    EXPECT_STREQ(PortReadClassName(PortReadClass::AyR14), "ay.r14");
    EXPECT_STREQ(PortReadClassName(PortReadClass::NextB2), "nr.b2");
}

TEST(AutoDecision_Test, Decide_NextB2RowBitsAndAyR14)
{
    AutoDecision decision = DecideFromPortReads(Evidence({PortReadClass::NextB2, PortReadClass::Kempston}));
    ASSERT_TRUE(decision.decisive);
    EXPECT_EQ(decision.device, "md6-pad");
    EXPECT_EQ(decision.connector, "next.left") << "NR #B2 beats #1F: a 6-button pad on the left";
    EXPECT_NE(decision.reason.find("NR #B2 read in 50 of 50"), std::string::npos) << decision.reason;
    decision = DecideFromPortReads(Evidence({PortReadClass::NextB2, PortReadClass::Kempston2}));
    EXPECT_EQ(decision.connector, "next.right");

    // #EFFE and #F7FE: key 5 alone on #F7FE is Cursor, keys 1-5 are a second Sinclair port
    std::array<uint8_t, 8> bits{};
    bits[4] = 0x1D;
    bits[3] = 0x10;
    decision = DecideFromPortReads(Evidence({PortReadClass::Row4, PortReadClass::Row3}, bits));
    EXPECT_EQ(decision.device, "cursor-joystick");
    EXPECT_NE(decision.reason.find("bits 4 tested"), std::string::npos) << decision.reason;
    bits[3] = 0x1F;
    decision = DecideFromPortReads(Evidence({PortReadClass::Row4, PortReadClass::Row3}, bits));
    EXPECT_EQ(decision.label, "Sinclair 1");
    EXPECT_EQ(decision.optionValue, "1");
    EXPECT_NE(decision.reason.find("two players"), std::string::npos) << decision.reason;

    // AY port A: a hint, nothing to plug
    decision = DecideFromPortReads(Evidence({PortReadClass::AyR14}));
    EXPECT_FALSE(decision.decisive);
    EXPECT_NE(decision.hint.find("AY R14"), std::string::npos) << decision.hint;
    EXPECT_TRUE(DecideFromPortReads(Evidence({PortReadClass::AyR14, PortReadClass::Kempston})).hint.empty())
        << "a decision leaves no hint";
}

TEST(AutoDecision_Test, RowBits_StopWhereTheValueIsGone)
{
    // IN A,(#FE); LD A,#0E; AND #01: A holds something else
    EXPECT_EQ((Code{{0xDB, 0xFE, 0x3E, 0x0E, 0xE6, 0x01}}.Tested()), 0);
    // IN A,(#FE); OR #E0; AND #01
    EXPECT_EQ((Code{{0xDB, 0xFE, 0xF6, 0xE0, 0xE6, 0x01}}.Tested()), 0);
    // IN E,(C); LD E,B; BIT 0,E
    EXPECT_EQ((Code{{0xED, 0x58, 0x58, 0xCB, 0x43}}.Tested()), 0);
    // IN A,(#FE); CP #FF; AND #01: a compare leaves A
    EXPECT_EQ((Code{{0xDB, 0xFE, 0xFE, 0xFF, 0xE6, 0x01}}.Tested()), 0x01);
}
