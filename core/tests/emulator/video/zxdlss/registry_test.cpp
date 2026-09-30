#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <string>

#include "emulator/video/zxdlss/algorithm.h"

/// The built-in algorithms are registered explicitly (registry.cpp): linked from
/// the core's static archive, a self-registering object file would be dropped
/// because nothing references it - and the names below would be missing.

TEST(ZxdlssRegistry_Test, BuiltInAlgorithmsAreAvailableFromTheCoreArchive)
{
    const std::vector<std::string> names = zxdlss::algorithmNames();
    for (const char* expected : {"raw", "mod-tpgw", "mod-tpgwa", "mod-tpgwaf", "mod-tpgwafs", "mod-tpgwafsd"})
        EXPECT_NE(std::find(names.begin(), names.end(), expected), names.end()) << expected << " is not registered";
}

TEST(ZxdlssRegistry_Test, CreatesByNameWithItsLookAhead)
{
    EXPECT_EQ(zxdlss::createAlgorithm("no-such-algorithm"), nullptr);
    auto raw = zxdlss::createAlgorithm("raw");
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(raw->delay(), 0);
    auto baseline = zxdlss::createAlgorithm("mod-tpgwafsd");
    ASSERT_NE(baseline, nullptr);
    EXPECT_EQ(baseline->name(), "mod-tpgwafsd");
    EXPECT_EQ(baseline->delay(), 6);
}

TEST(ZxdlssRegistry_Test, DecodePlaneBSplitsTheFields)
{
    // attr bits 0..7, color index bits 8..11, ink bit 12
    const uint16_t planeB[2] = {static_cast<uint16_t>(0x47 | (5 << 8) | (1 << 12)), static_cast<uint16_t>(0x00 | (1 << 8))};
    std::vector<uint8_t> plane, attr, ink;
    zxdlss::decodePlaneB(planeB, 2, plane, attr, ink);
    EXPECT_EQ(attr[0], 0x47);
    EXPECT_EQ(plane[0], 5);
    EXPECT_EQ(ink[0], 1);
    EXPECT_EQ(attr[1], 0x00);
    EXPECT_EQ(plane[1], 1);
    EXPECT_EQ(ink[1], 0);
}
