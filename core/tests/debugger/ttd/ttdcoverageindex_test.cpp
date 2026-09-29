/// @file ttdcoverageindex_test.cpp
/// @brief Unit tests for the per-frame coverage index.
///
/// The index exists so reverse search can skip frames without replaying them.
/// Two properties matter and are easy to lose:
///
///   1. It must never say "this frame did not touch X" when it did. A false
///      negative silently drops the answer a user is searching for — far worse
///      than a false positive, which only costs one wasted replay.
///   2. Its cost must follow set cardinality, not address-space width. That is
///      the whole reason a 4 MB clone can be indexed at all.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "debugger/ttd/ttdcoverageindex.h"

using ttd::MakeCoverageKey;
using ttd::TTDCoverageIndex;
using ttd::TTDCoverageKey;
using ttd::TTDCoverageKind;

namespace
{

/// Record a whole frame's worth of keys and seal it.
void SealFrameWith(TTDCoverageIndex& index, uint64_t frame, TTDCoverageKind kind,
                   const std::vector<TTDCoverageKey>& keys)
{
    for (TTDCoverageKey k : keys)
        index.Record(kind, k);
    index.SealFrame(frame);
}

}  // namespace

TEST(TTD_CoverageIndex_Test, KeyPacksPageAndOffsetWithoutCollision)
{
    // Same offset in different pages must be different keys — the defect an
    // address-keyed index would have on any banked machine.
    EXPECT_NE(MakeCoverageKey(0, 0xC000), MakeCoverageKey(3, 0xC000));
    EXPECT_NE(MakeCoverageKey(3, 0xC000), MakeCoverageKey(7, 0xC000));

    // Only the offset within the 16 KB page participates: 0xC000 and 0x8000
    // are the same offset in whatever page is banked there.
    EXPECT_EQ(MakeCoverageKey(5, 0x0000), MakeCoverageKey(5, 0x4000));

    // The full 256-page range has to fit.
    EXPECT_NE(MakeCoverageKey(255, 0x3FFF), MakeCoverageKey(254, 0x3FFF));
}

TEST(TTD_CoverageIndex_Test, FindsTheMostRecentTouchingFrame)
{
    TTDCoverageIndex index;
    const TTDCoverageKey target = MakeCoverageKey(2, 0x4123);

    SealFrameWith(index, 10, TTDCoverageKind::Written, {target});
    SealFrameWith(index, 11, TTDCoverageKind::Written, {MakeCoverageKey(2, 0x4999)});
    SealFrameWith(index, 12, TTDCoverageKind::Written, {target});
    SealFrameWith(index, 13, TTDCoverageKind::Written, {MakeCoverageKey(2, 0x4888)});

    uint64_t found = 0;
    ASSERT_TRUE(index.FindLastFrameTouching(TTDCoverageKind::Written, target, 13, found));
    EXPECT_EQ(found, 12u) << "returned a frame other than the newest match";

    // Bounded search must respect the bound rather than returning the newest.
    ASSERT_TRUE(index.FindLastFrameTouching(TTDCoverageKind::Written, target, 11, found));
    EXPECT_EQ(found, 10u);
}

TEST(TTD_CoverageIndex_Test, ReportsNoMatchForAnUntouchedKey)
{
    TTDCoverageIndex index;
    SealFrameWith(index, 1, TTDCoverageKind::Written, {MakeCoverageKey(0, 0x4000)});

    uint64_t found = 0;
    EXPECT_FALSE(index.FindLastFrameTouching(
        TTDCoverageKind::Written, MakeCoverageKey(0, 0x4001), 100, found));

    // Right offset, wrong page — the banking case.
    EXPECT_FALSE(index.FindLastFrameTouching(
        TTDCoverageKind::Written, MakeCoverageKey(1, 0x4000), 100, found));
}

TEST(TTD_CoverageIndex_Test, KindsDoNotBleedIntoEachOther)
{
    TTDCoverageIndex index;
    const TTDCoverageKey key = MakeCoverageKey(1, 0x5000);

    for (TTDCoverageKey k : {key})
        index.Record(TTDCoverageKind::Executed, k);
    index.SealFrame(7);

    uint64_t found = 0;
    EXPECT_TRUE(index.FindLastFrameTouching(TTDCoverageKind::Executed, key, 7, found));
    EXPECT_FALSE(index.FindLastFrameTouching(TTDCoverageKind::Written, key, 7, found))
        << "an executed address was reported as written";
    EXPECT_FALSE(index.FindLastFrameTouching(TTDCoverageKind::Read, key, 7, found))
        << "an executed address was reported as read";
}

/// The hot path appends without deduplicating, so a frame that hammers one
/// address must still encode it once.
TEST(TTD_CoverageIndex_Test, RepeatedAccessesCollapseToOneEntry)
{
    TTDCoverageIndex index;
    const TTDCoverageKey key = MakeCoverageKey(4, 0x6000);

    for (int i = 0; i < 5000; ++i)
        index.Record(TTDCoverageKind::Written, key);
    index.SealFrame(3);

    // One key, encoded as a single varint delta.
    EXPECT_LE(index.EncodedBytes(TTDCoverageKind::Written), 8u)
        << "duplicates were not collapsed at seal time";

    uint64_t found = 0;
    EXPECT_TRUE(index.FindLastFrameTouching(TTDCoverageKind::Written, key, 3, found));
}

/// Exhaustive round-trip: every key put in must be found, and nothing else.
/// This is the false-negative guard.
TEST(TTD_CoverageIndex_Test, EveryRecordedKeyIsFoundAndNoOthers)
{
    TTDCoverageIndex index;
    std::set<TTDCoverageKey> expected;

    // A spread that mixes clustered and scattered keys across several pages.
    for (uint8_t page = 0; page < 8; ++page)
    {
        for (uint16_t off = 0; off < 0x3FFF; off += 517)
        {
            const TTDCoverageKey k = MakeCoverageKey(page, off);
            index.Record(TTDCoverageKind::Read, k);
            expected.insert(k);
        }
    }
    index.SealFrame(42);

    for (TTDCoverageKey k : expected)
    {
        uint64_t found = 0;
        EXPECT_TRUE(index.FindLastFrameTouching(TTDCoverageKind::Read, k, 42, found))
            << "recorded key 0x" << std::hex << k << " was not found";
    }

    // Probe keys that were deliberately not recorded.
    size_t falsePositives = 0;
    for (uint8_t page = 0; page < 8; ++page)
    {
        for (uint16_t off = 1; off < 0x3FFF; off += 519)
        {
            const TTDCoverageKey k = MakeCoverageKey(page, off);
            if (expected.count(k) != 0)
                continue;
            uint64_t found = 0;
            if (index.FindLastFrameTouching(TTDCoverageKind::Read, k, 42, found))
                ++falsePositives;
        }
    }
    EXPECT_EQ(falsePositives, 0u);
}

/// Idle frames still occupy a slot — the block-to-frame-range mapping depends
/// on frame numbering staying dense — but that slot is one varint of zero, and
/// a run of them compresses to almost nothing.
TEST(TTD_CoverageIndex_Test, IdleFramesAreNearlyFree)
{
    TTDCoverageIndex index;

    constexpr uint64_t kIdleFrames = 1000;
    for (uint64_t f = 0; f < kIdleFrames; ++f)
        index.SealFrame(f);
    index.FlushOpenBlocks();

    EXPECT_EQ(index.SealedFrameCount(TTDCoverageKind::Written), kIdleFrames)
        << "idle frames must still be counted, or block frame ranges drift";

    // One zero byte per frame before compression...
    EXPECT_LE(index.RawEncodedBytes(TTDCoverageKind::Written), kIdleFrames + 64)
        << "an empty frame should cost a single varint";

    // ...and well under a byte per frame after it. The floor is not zero: each
    // block carries a zstd frame header of roughly 17 bytes, so 64-frame blocks
    // bottom out near 0.27 B/frame no matter how empty the content is.
    const size_t compressed = index.EncodedBytes(TTDCoverageKind::Written);
    EXPECT_LT(compressed, kIdleFrames)
        << "a run of idle frames cost more than a byte each: " << compressed << " bytes";

    // And they must still answer queries correctly.
    uint64_t found = 0;
    EXPECT_FALSE(index.FindLastFrameTouching(
        TTDCoverageKind::Written, MakeCoverageKey(0, 0x4000), kIdleFrames, found));
}

/// The property that makes 4 MB clones affordable: encoded size follows how
/// many distinct addresses a frame touched, not how wide the address space is.
TEST(TTD_CoverageIndex_Test, CostFollowsCardinalityNotAddressSpaceWidth)
{
    constexpr int kKeysPerFrame = 300;

    // Same cardinality, same clustering, but spread across a 128K machine's
    // 8 pages versus a 4 MB machine's 256 pages.
    TTDCoverageIndex narrow;
    for (int i = 0; i < kKeysPerFrame; ++i)
        narrow.Record(TTDCoverageKind::Executed,
                      MakeCoverageKey(static_cast<uint8_t>(i % 8),
                                      static_cast<uint16_t>(i * 13)));
    narrow.SealFrame(0);

    TTDCoverageIndex wide;
    for (int i = 0; i < kKeysPerFrame; ++i)
        wide.Record(TTDCoverageKind::Executed,
                    MakeCoverageKey(static_cast<uint8_t>((i * 31) % 256),
                                    static_cast<uint16_t>(i * 13)));
    wide.SealFrame(0);

    const size_t narrowBytes = narrow.EncodedBytes(TTDCoverageKind::Executed);
    const size_t wideBytes   = wide.EncodedBytes(TTDCoverageKind::Executed);

    ASSERT_GT(narrowBytes, 0u);
    ASSERT_GT(wideBytes, 0u);

    // A flat bitmap would grow by 64x here (8 KB -> 512 KB of scratch). Sparse
    // encoding is allowed to grow — wider deltas need more varint bytes — but
    // it must stay the same order of magnitude, a few bytes per key.
    EXPECT_LT(wideBytes, narrowBytes * 4)
        << "encoded size scaled with address-space width (" << narrowBytes
        << " -> " << wideBytes << " bytes); the sparse property is gone";
    EXPECT_LT(wideBytes, static_cast<size_t>(kKeysPerFrame) * 4)
        << "more than 4 bytes per key on a 4 MB-wide key space";
}

TEST(TTD_CoverageIndex_Test, ClearDropsEverything)
{
    TTDCoverageIndex index;
    SealFrameWith(index, 1, TTDCoverageKind::Written, {MakeCoverageKey(0, 0x4000)});
    ASSERT_GT(index.SealedFrameCount(TTDCoverageKind::Written), 0u);

    index.Clear();

    EXPECT_EQ(index.SealedFrameCount(TTDCoverageKind::Written), 0u);
    EXPECT_EQ(index.EncodedBytes(TTDCoverageKind::Written), 0u);

    uint64_t found = 0;
    EXPECT_FALSE(index.FindLastFrameTouching(
        TTDCoverageKind::Written, MakeCoverageKey(0, 0x4000), 100, found));
}

// ---------------------------------------------------------------------------
// Regressions from the repeat-filter optimisation
// ---------------------------------------------------------------------------
//
// A direct-mapped L1 filter sits in front of the membership bitmap to absorb
// repeat accesses (an instruction fetch hits the same ~318 addresses tens of
// thousands of times per frame; going to the 512 KB bitmap each time cost
// ~175 us/frame in cache misses). Both bugs below came from that filter, and
// both are false negatives — the failure mode that loses a user's answer.

/// Key 0 is a real address: page 0, offset 0. It once shared the filter's
/// "empty slot" marker and was therefore never recorded at all.
TEST(TTD_CoverageIndex_Test, KeyZeroIsRecordedLikeAnyOther)
{
    TTDCoverageIndex index;
    const TTDCoverageKey zero = MakeCoverageKey(0, 0x0000);
    ASSERT_EQ(zero, 0u) << "test premise: this is the key that collided with the empty marker";

    index.Record(TTDCoverageKind::Written, zero);
    index.SealFrame(1);

    uint64_t found = 0;
    EXPECT_TRUE(index.FindLastFrameTouching(TTDCoverageKind::Written, zero, 1, found))
        << "page 0 offset 0 was swallowed by the repeat filter";
    EXPECT_EQ(found, 1u);
}

/// The filter must not carry state across frames: a key touched in frame N and
/// again in frame N+1 has to appear in BOTH sets.
TEST(TTD_CoverageIndex_Test, RepeatFilterResetsBetweenFrames)
{
    TTDCoverageIndex index;
    const TTDCoverageKey key = MakeCoverageKey(3, 0x4321);

    for (uint64_t frame = 1; frame <= 5; ++frame)
    {
        index.Record(TTDCoverageKind::Executed, key);
        index.SealFrame(frame);
    }

    EXPECT_EQ(index.SealedFrameCount(TTDCoverageKind::Executed), 5u)
        << "later frames recorded nothing — the filter leaked across frames";

    for (uint64_t frame = 1; frame <= 5; ++frame)
    {
        EXPECT_TRUE(index.FrameTouches(TTDCoverageKind::Executed, frame, key))
            << "frame " << frame << " lost a key it touched";
    }
}

/// Filter collisions must cost work, never correctness: two keys landing in the
/// same slot have to both survive.
TEST(TTD_CoverageIndex_Test, FilterCollisionsDoNotLoseKeys)
{
    TTDCoverageIndex index;

    // Keys 4096 apart collide in a 4096-slot direct-mapped filter.
    std::vector<TTDCoverageKey> colliding;
    for (uint32_t i = 0; i < 32; ++i)
        colliding.push_back(static_cast<TTDCoverageKey>(0x1000 + i * 4096));

    // Interleave them so each one repeatedly evicts the next.
    for (int round = 0; round < 10; ++round)
        for (TTDCoverageKey k : colliding)
            index.Record(TTDCoverageKind::Read, k);
    index.SealFrame(9);

    for (TTDCoverageKey k : colliding)
    {
        EXPECT_TRUE(index.FrameTouches(TTDCoverageKind::Read, 9, k))
            << "colliding key 0x" << std::hex << k << " was lost";
    }
}

// ---------------------------------------------------------------------------
// The "no RAM page" bucket (PLAN #40 V0)
// ---------------------------------------------------------------------------

/// ROM/cache accesses share one bucket. It used to be page field 0xFF - the
/// same key as RAM page 255 of a 4 MB machine.
TEST(TTD_CoverageIndex_Test, NoPageBucketIsNotRamPage255)
{
    EXPECT_NE(MakeCoverageKey(ttd::kPhysPageNone, 0x0038), MakeCoverageKey(255, 0xC038));
    EXPECT_EQ(MakeCoverageKey(ttd::kPhysPageNone, 0x0038) >> 14, ttd::kCoverageNoPageField);
    EXPECT_EQ(MakeCoverageKey(255, 0xC038) >> 14, 255u);

    TTDCoverageIndex index;
    SealFrameWith(index, 1, TTDCoverageKind::Executed, {MakeCoverageKey(ttd::kPhysPageNone, 0x0038)});

    EXPECT_FALSE(index.FrameMayContain(TTDCoverageKind::Executed, 1, 0x0038, 0x0038, true, 255))
        << "a ROM fetch answered a RAM page 255 query";
    EXPECT_TRUE(index.FrameMayContain(TTDCoverageKind::Executed, 1, 0x0038, 0x0038, true, ttd::kPhysPageNone));
    EXPECT_TRUE(index.FrameMayContain(TTDCoverageKind::Executed, 1, 0x0038, 0x0038, false, 0));

    TTDCoverageIndex ram;
    SealFrameWith(ram, 1, TTDCoverageKind::Executed, {MakeCoverageKey(255, 0xC038)});
    EXPECT_TRUE(ram.FrameMayContain(TTDCoverageKind::Executed, 1, 0x0038, 0x0038, true, 255));
    EXPECT_FALSE(ram.FrameMayContain(TTDCoverageKind::Executed, 1, 0x0038, 0x0038, true, ttd::kPhysPageNone))
        << "a page-255 fetch answered a ROM query";
}

TEST(TTD_CoverageIndex_Test, NoPageBucketSurvivesSerialization)
{
    TTDCoverageIndex index;
    SealFrameWith(index, 1, TTDCoverageKind::Read,
                  {MakeCoverageKey(ttd::kPhysPageNone, 0x1000), MakeCoverageKey(255, 0xD000)});

    std::stringstream buf;
    ASSERT_TRUE(index.Serialize(buf));
    TTDCoverageIndex loaded;
    ASSERT_TRUE(loaded.Deserialize(buf));

    EXPECT_TRUE(loaded.FrameTouches(TTDCoverageKind::Read, 1, MakeCoverageKey(ttd::kPhysPageNone, 0x1000)));
    EXPECT_TRUE(loaded.FrameTouches(TTDCoverageKind::Read, 1, MakeCoverageKey(255, 0xD000)));
    EXPECT_FALSE(loaded.FrameTouches(TTDCoverageKind::Read, 1, MakeCoverageKey(255, 0xD001)));
}

/// A version 1 section (page-255 keys ambiguous) is consumed so the stream
/// stays aligned for the sections after it, then dropped: queries fall back
/// to replay rather than trust it.
TEST(TTD_CoverageIndex_Test, Version1SectionIsConsumedButNotLoaded)
{
    TTDCoverageIndex index;
    SealFrameWith(index, 1, TTDCoverageKind::Written, {MakeCoverageKey(3, 0x4000)});
    SealFrameWith(index, 2, TTDCoverageKind::Written, {MakeCoverageKey(255, 0xC000)});

    std::stringstream section;
    ASSERT_TRUE(index.Serialize(section));
    std::string bytes = section.str();
    ASSERT_GE(bytes.size(), 6u);
    const uint16_t v1 = 1;
    std::memcpy(&bytes[4], &v1, sizeof(v1));  // magic (u32), then version (u16)

    const uint32_t trailer = 0xA5A5F00Du;
    std::stringstream in;
    in.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    in.write(reinterpret_cast<const char*>(&trailer), sizeof(trailer));
    in.seekg(0);

    TTDCoverageIndex loaded;
    EXPECT_FALSE(loaded.Deserialize(in)) << "a v1 section must not be trusted";
    EXPECT_FALSE(loaded.CoversFrame(TTDCoverageKind::Written, 1)) << "v1 content leaked into the index";

    uint32_t next = 0;
    in.read(reinterpret_cast<char*>(&next), sizeof(next));
    ASSERT_TRUE(static_cast<bool>(in));
    EXPECT_EQ(next, trailer) << "v1 section was not fully consumed; the next section would be misread";
}

/// Sizes read from a file are checked before anything is allocated: a flipped
/// frame count, raw size or payload size fails the load instead of requesting
/// gigabytes here or at the block's first decompression.
TEST(TTD_CoverageIndex_Test, CorruptBlockSizesAreRefusedBeforeAllocating)
{
    TTDCoverageIndex source;
    for (uint64_t frame = 0; frame < 3; ++frame)
        SealFrameWith(source, frame, TTDCoverageKind::Executed,
                      {MakeCoverageKey(1, static_cast<uint16_t>(0x4000 + frame))});
    std::ostringstream out(std::ios::binary);
    ASSERT_TRUE(source.Serialize(out));
    const std::string good = out.str();

    // Layout: magic u32, version u16, kind count u16, then kind 0 (Executed):
    // block count u32, and its first block: base frame u64, frame count u32,
    // raw size u32, compressed size u32
    constexpr size_t kFrameCountAt = 4 + 2 + 2 + 4 + 8;
    constexpr size_t kRawSizeAt = kFrameCountAt + 4;
    constexpr size_t kCompressedSizeAt = kRawSizeAt + 4;

    auto load = [](const std::string& bytes) {
        std::istringstream in(bytes, std::ios::binary);
        TTDCoverageIndex loaded;
        return loaded.Deserialize(in);
    };
    auto patched = [&](size_t at, uint32_t value) {
        std::string bytes = good;
        std::memcpy(&bytes[at], &value, sizeof(value));
        return bytes;
    };

    ASSERT_TRUE(load(good));
    EXPECT_FALSE(load(patched(kFrameCountAt, 0)));
    EXPECT_FALSE(load(patched(kFrameCountAt, TTDCoverageIndex::kFramesPerBlock + 1)));
    EXPECT_FALSE(load(patched(kRawSizeAt, 0x7FFFFFFFu)));
    EXPECT_FALSE(load(patched(kCompressedSizeAt, 0xFFFFFFFFu)));
}
