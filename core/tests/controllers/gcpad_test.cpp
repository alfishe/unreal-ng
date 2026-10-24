/// @file gcpad_test.cpp
/// @brief A pad only GameController sees (architecture.md §5, `gc:` devices): GCExtendedGamepad's snapshot as control
/// events, and the CoreHaptics sharpness of an HD frequency (haptics.md §9).

#include <gtest/gtest.h>

#include <map>
#include <string>

#include "gcpad.h"

using namespace controllers;

TEST(GcPad_Test, SnapshotChangesBecomeControls)
{
    GcPad pad;
    GcPadSnapshot snapshot;
    for (size_t element = 0; element < GcPadSnapshot::kElementCount; element++)
        snapshot.present[element] = element != GcPadSnapshot::kButtonHome;  // a pad without a home button
    snapshot.values[GcPadSnapshot::kLeftY] = 0.5f;  // pushed up a little
    snapshot.values[GcPadSnapshot::kButtonB] = 1.0f;
    std::map<std::string, float> events;
    const ControlSink sink = [&events](const std::string& control, float value) { events[control] = value; };
    pad.Update(snapshot, sink);
    EXPECT_FLOAT_EQ(events.at("lstick.y"), -0.5f) << "up is negative here";
    EXPECT_FLOAT_EQ(events.at("lstick.x"), 0.0f) << "the first snapshot gives the axes";
    EXPECT_FLOAT_EQ(events.at("lt"), 0.0f);
    EXPECT_FLOAT_EQ(events.at("east"), 1.0f) << "buttonB is east";
    EXPECT_EQ(events.count("south"), 0u) << "and the pressed buttons only";
    EXPECT_EQ(events.count("guide"), 0u) << "no home button";

    events.clear();
    snapshot.values[GcPadSnapshot::kButtonB] = 0.0f;
    snapshot.values[GcPadSnapshot::kButtonA] = 0.7f;  // analog pressure: a press
    snapshot.values[GcPadSnapshot::kButtonMenu] = 1.0f;
    snapshot.values[GcPadSnapshot::kRightTrigger] = 0.25f;
    pad.Update(snapshot, sink);
    EXPECT_EQ(events.size(), 4u);
    EXPECT_FLOAT_EQ(events.at("east"), 0.0f);
    EXPECT_FLOAT_EQ(events.at("south"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("menu"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("rt"), 0.25f);

    events.clear();
    snapshot.values[GcPadSnapshot::kButtonA] = 0.9f;  // still pressed
    pad.Update(snapshot, sink);
    EXPECT_TRUE(events.empty());

    pad.Reset();
    events.clear();
    pad.Update(snapshot, sink);
    EXPECT_FLOAT_EQ(events.at("south"), 1.0f) << "after Reset: the state again";
    EXPECT_EQ(events.count("east"), 0u);
}

TEST(GcPad_Test, ReleaseLetsGoOfWhatIsHeld)
{
    // The input stops while a button is down and a stick is pushed: the router sees them come back
    GcPad pad;
    GcPadSnapshot snapshot;
    for (size_t element = 0; element < GcPadSnapshot::kElementCount; element++)
        snapshot.present[element] = true;
    snapshot.values[GcPadSnapshot::kButtonA] = 1.0f;
    snapshot.values[GcPadSnapshot::kLeftX] = 0.8f;
    std::map<std::string, float> events;
    const ControlSink sink = [&events](const std::string& control, float value) { events[control] = value; };
    pad.Update(snapshot, sink);
    events.clear();
    pad.Release(sink);
    EXPECT_EQ(events.size(), 2u);
    EXPECT_FLOAT_EQ(events.at("south"), 0.0f);
    EXPECT_FLOAT_EQ(events.at("lstick.x"), 0.0f);
    events.clear();
    pad.Release(sink);
    EXPECT_TRUE(events.empty()) << "nothing is held after a release";
}

TEST(GcPad_Test, SharpnessFollowsTheSwitchFrequencyCodes)
{
    EXPECT_FLOAT_EQ(GcPad::SharpnessFor(0), GcPad::kDefaultSharpness);
    EXPECT_FLOAT_EQ(GcPad::SharpnessFor(41), 0.0f) << "the lowest HD code";
    EXPECT_FLOAT_EQ(GcPad::SharpnessFor(1253), 1.0f) << "the highest";
    EXPECT_FLOAT_EQ(GcPad::SharpnessFor(10), 0.0f) << "clamped";
    EXPECT_FLOAT_EQ(GcPad::SharpnessFor(160), (0x80 - 0x41) / static_cast<float>(0xDF - 0x41));
    EXPECT_LT(GcPad::SharpnessFor(160), GcPad::SharpnessFor(320));
}
