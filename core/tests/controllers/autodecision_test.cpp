/// @file autodecision_test.cpp
/// @brief The port-read watch's classes (retro-devices.md §6) and the heuristic that turns its evidence into the
/// auto connector (mapping-and-profiles.md §6.1 step 2): dedicated ports beat half-rows, the whole keyboard is a
/// hint, half a window decides nothing (C6).

#include <gtest/gtest.h>

#include <initializer_list>

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
