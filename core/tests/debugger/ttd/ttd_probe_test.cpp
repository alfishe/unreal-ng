/// @file ttd_probe_test.cpp
/// @brief Unit tests for TTDAccessProbe (hot-path access probe).
///
/// Per parent TDD §9.2 + §9.4. Tests the probe in isolation — no emulator
/// needed. The probe is a POD-with-vector; these tests exercise Matches(),
/// Arm/Disarm, RecordHit, ExtractHits, and Reset across all four access
/// types (Write/Read/Execute/Io).

#include <gtest/gtest.h>

#include "debugger/ttd/ttd_probe.h"

using namespace ttd;

// ===========================================================================
// Arm / Disarm / IsArmed
// ===========================================================================

TEST(TTDAccessProbe_Test, Default_IsNotArmed)
{
    TTDAccessProbe probe;
    EXPECT_FALSE(probe.IsArmed());
}

TEST(TTDAccessProbe_Test, Arm_SetsArmed)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    q.access = TTDAccessType::Write;
    probe.Arm(q);
    EXPECT_TRUE(probe.IsArmed());
}

TEST(TTDAccessProbe_Test, Disarm_ClearsArmed)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    probe.Arm(q);
    EXPECT_TRUE(probe.IsArmed());
    probe.Disarm();
    EXPECT_FALSE(probe.IsArmed());
}

TEST(TTDAccessProbe_Test, Disarm_WhenNotArmed_IsSafe)
{
    TTDAccessProbe probe;
    probe.Disarm();  // Should not crash
    EXPECT_FALSE(probe.IsArmed());
}

TEST(TTDAccessProbe_Test, Reset_ClearsEverything)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    q.access = TTDAccessType::Write;
    probe.Arm(q);

    // Record a hit
    TTDTimePoint t{1, 100};
    probe.RecordHit(t, 0x2000, 0xAB, 1, TTDAccessType::Write);
    ASSERT_EQ(probe.Hits().size(), 1u);

    probe.Reset();
    EXPECT_FALSE(probe.IsArmed());
    EXPECT_EQ(probe.Hits().size(), 0u);
}

// ===========================================================================
// Matches — access type filtering
// ===========================================================================

TEST(TTDAccessProbe_Test, Matches_WriteProbe_MatchesWriteAccess)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    q.access = TTDAccessType::Write;
    probe.Arm(q);

    EXPECT_TRUE(probe.Matches(0x1000, TTDAccessType::Write, 0x42, 0x2000));
}

TEST(TTDAccessProbe_Test, Matches_WriteProbe_RejectsReadAccess)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    q.access = TTDAccessType::Write;
    probe.Arm(q);

    EXPECT_FALSE(probe.Matches(0x1000, TTDAccessType::Read, 0x42, 0x2000));
}

TEST(TTDAccessProbe_Test, Matches_ReadProbe_MatchesReadAccess)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    q.access = TTDAccessType::Read;
    probe.Arm(q);

    EXPECT_TRUE(probe.Matches(0x1000, TTDAccessType::Read, 0x42, 0x2000));
}

TEST(TTDAccessProbe_Test, Matches_ExecuteProbe_MatchesExecuteAccess)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    q.access = TTDAccessType::Execute;
    probe.Arm(q);

    EXPECT_TRUE(probe.Matches(0x2000, TTDAccessType::Execute, 0, 0x2000));
}

TEST(TTDAccessProbe_Test, Matches_IoProbe_MatchesIoAccess)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    q.access = TTDAccessType::Io;
    probe.Arm(q);

    EXPECT_TRUE(probe.Matches(0xFE, TTDAccessType::Io, 0x42, 0x2000));
}

TEST(TTDAccessProbe_Test, Matches_IoProbe_RejectsWriteAccess)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    q.access = TTDAccessType::Io;
    probe.Arm(q);

    EXPECT_FALSE(probe.Matches(0xFE, TTDAccessType::Write, 0x42, 0x2000));
}

// ===========================================================================
// Matches — when not armed
// ===========================================================================

TEST(TTDAccessProbe_Test, Matches_WhenNotArmed_AlwaysReturnsFalse)
{
    TTDAccessProbe probe;
    // Not armed — every check should return false regardless of query
    EXPECT_FALSE(probe.Matches(0x1000, TTDAccessType::Write, 0x42, 0x2000));
    EXPECT_FALSE(probe.Matches(0x1000, TTDAccessType::Read, 0x42, 0x2000));
    EXPECT_FALSE(probe.Matches(0x2000, TTDAccessType::Execute, 0, 0x2000));
    EXPECT_FALSE(probe.Matches(0xFE, TTDAccessType::Io, 0x42, 0x2000));
}

// ===========================================================================
// Matches — address range filtering
// ===========================================================================

TEST(TTDAccessProbe_Test, Matches_AddressInRange)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    q.access   = TTDAccessType::Write;
    q.addrFrom = 0x1000;
    q.addrTo   = 0x1FFF;
    probe.Arm(q);

    EXPECT_TRUE(probe.Matches(0x1000, TTDAccessType::Write, 0, 0));
    EXPECT_TRUE(probe.Matches(0x1500, TTDAccessType::Write, 0, 0));
    EXPECT_TRUE(probe.Matches(0x1FFF, TTDAccessType::Write, 0, 0));
}

TEST(TTDAccessProbe_Test, Matches_AddressOutOfRange)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    q.access   = TTDAccessType::Write;
    q.addrFrom = 0x1000;
    q.addrTo   = 0x1FFF;
    probe.Arm(q);

    EXPECT_FALSE(probe.Matches(0x0FFF, TTDAccessType::Write, 0, 0));
    EXPECT_FALSE(probe.Matches(0x2000, TTDAccessType::Write, 0, 0));
    EXPECT_FALSE(probe.Matches(0xFFFF, TTDAccessType::Write, 0, 0));
}

TEST(TTDAccessProbe_Test, Matches_DefaultQuery_MatchesAnyAddress)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;  // default: addrFrom=0, addrTo=0xFFFF
    q.access = TTDAccessType::Write;
    probe.Arm(q);

    EXPECT_TRUE(probe.Matches(0x0000, TTDAccessType::Write, 0, 0));
    EXPECT_TRUE(probe.Matches(0x8000, TTDAccessType::Write, 0, 0));
    EXPECT_TRUE(probe.Matches(0xFFFF, TTDAccessType::Write, 0, 0));
}

// ===========================================================================
// Matches — value filter
// ===========================================================================

TEST(TTDAccessProbe_Test, Matches_ValueFilter_MatchingValue)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    q.access         = TTDAccessType::Write;
    q.hasValueFilter = true;
    q.value          = 0x42;
    probe.Arm(q);

    EXPECT_TRUE(probe.Matches(0x1000, TTDAccessType::Write, 0x42, 0));
}

TEST(TTDAccessProbe_Test, Matches_ValueFilter_NonMatchingValue)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    q.access         = TTDAccessType::Write;
    q.hasValueFilter = true;
    q.value          = 0x42;
    probe.Arm(q);

    EXPECT_FALSE(probe.Matches(0x1000, TTDAccessType::Write, 0x99, 0));
}

TEST(TTDAccessProbe_Test, Matches_NoValueFilter_AnyValue)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    q.access         = TTDAccessType::Write;
    q.hasValueFilter = false;
    probe.Arm(q);

    EXPECT_TRUE(probe.Matches(0x1000, TTDAccessType::Write, 0x00, 0));
    EXPECT_TRUE(probe.Matches(0x1000, TTDAccessType::Write, 0xFF, 0));
}

// ===========================================================================
// Matches — PC filter
// ===========================================================================

TEST(TTDAccessProbe_Test, Matches_PcFilter_InRange)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    q.access      = TTDAccessType::Write;
    q.hasPcFilter = true;
    q.pcFrom      = 0x2000;
    q.pcTo        = 0x2FFF;
    probe.Arm(q);

    EXPECT_TRUE(probe.Matches(0x1000, TTDAccessType::Write, 0, 0x2000));
    EXPECT_TRUE(probe.Matches(0x1000, TTDAccessType::Write, 0, 0x2500));
    EXPECT_TRUE(probe.Matches(0x1000, TTDAccessType::Write, 0, 0x2FFF));
}

TEST(TTDAccessProbe_Test, Matches_PcFilter_OutOfRange)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    q.access      = TTDAccessType::Write;
    q.hasPcFilter = true;
    q.pcFrom      = 0x2000;
    q.pcTo        = 0x2FFF;
    probe.Arm(q);

    EXPECT_FALSE(probe.Matches(0x1000, TTDAccessType::Write, 0, 0x1FFF));
    EXPECT_FALSE(probe.Matches(0x1000, TTDAccessType::Write, 0, 0x3000));
}

// ===========================================================================
// RecordHit / Hits / ExtractHits
// ===========================================================================

TEST(TTDAccessProbe_Test, RecordHit_AppendsToHits)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    q.access = TTDAccessType::Write;
    probe.Arm(q);

    TTDTimePoint t1{1, 100};
    TTDTimePoint t2{1, 200};

    probe.RecordHit(t1, 0x2000, 0x01, 1, TTDAccessType::Write);
    probe.RecordHit(t2, 0x2100, 0x02, 2, TTDAccessType::Write);

    const auto& hits = probe.Hits();
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0].pc, 0x2000u);
    EXPECT_EQ(hits[0].value, 0x01u);
    EXPECT_EQ(hits[1].pc, 0x2100u);
    EXPECT_EQ(hits[1].value, 0x02u);
}

TEST(TTDAccessProbe_Test, ExtractHits_ReturnsAndClears)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    q.access = TTDAccessType::Write;
    probe.Arm(q);

    TTDTimePoint t{1, 100};
    probe.RecordHit(t, 0x2000, 0x42, 5, TTDAccessType::Write);
    ASSERT_EQ(probe.Hits().size(), 1u);

    auto hits = probe.ExtractHits();
    EXPECT_EQ(hits.size(), 1u);
    EXPECT_EQ(probe.Hits().size(), 0u);  // Cleared after extract
}

TEST(TTDAccessProbe_Test, Arm_ClearsPreviousHits)
{
    TTDAccessProbe probe;
    TTDSearchQuery q;
    q.access = TTDAccessType::Write;
    probe.Arm(q);

    TTDTimePoint t{1, 100};
    probe.RecordHit(t, 0x2000, 0x42, 5, TTDAccessType::Write);
    ASSERT_EQ(probe.Hits().size(), 1u);

    // Re-arm — previous hits should be cleared
    probe.Arm(q);
    EXPECT_EQ(probe.Hits().size(), 0u);
}

// ===========================================================================
// TTDAccessType string conversion (automation contract)
// ===========================================================================

TEST(TTDAccessProbe_Test, AccessTypeToString_AllValues)
{
    EXPECT_STREQ(TTDAccessTypeToString(TTDAccessType::Write), "write");
    EXPECT_STREQ(TTDAccessTypeToString(TTDAccessType::Read), "read");
    EXPECT_STREQ(TTDAccessTypeToString(TTDAccessType::Execute), "execute");
    EXPECT_STREQ(TTDAccessTypeToString(TTDAccessType::Io), "io");
}

TEST(TTDAccessProbe_Test, AccessTypeFromString_AllValues)
{
    EXPECT_EQ(TTDAccessTypeFromString("write"), TTDAccessType::Write);
    EXPECT_EQ(TTDAccessTypeFromString("read"), TTDAccessType::Read);
    EXPECT_EQ(TTDAccessTypeFromString("execute"), TTDAccessType::Execute);
    EXPECT_EQ(TTDAccessTypeFromString("io"), TTDAccessType::Io);
}

TEST(TTDAccessProbe_Test, AccessTypeFromString_DefaultFallback)
{
    // Unknown string defaults to Write
    EXPECT_EQ(TTDAccessTypeFromString("unknown"), TTDAccessType::Write);
    EXPECT_EQ(TTDAccessTypeFromString(nullptr), TTDAccessType::Write);
}
