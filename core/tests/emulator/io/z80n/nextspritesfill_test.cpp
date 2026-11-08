#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>

#include "emulator/io/z80n/nextsprites.h"

/// NextSprites::FillClocks keeps the sprites resolved between calls and resolves again only the ones whose attributes changed
/// (with the relative sprites of a changed anchor). Every answer must be what a full decode of the same attributes gives: a
/// second engine that gets the same table at once and is asked only at the end is the reference
class NextSpritesFill_Test : public ::testing::Test
{
protected:
    /// One attribute byte through NR #75-#79 style access (no index stepping), for both engines
    static void Put(NextSprites& s, unsigned sprite, unsigned byte, uint8_t value)
    {
        s.WriteMirrorSprite(static_cast<uint8_t>(sprite), false);
        s.WriteMirrorAttribute(byte, value, false, false);
    }

    static void ExpectSameClocks(const NextSprites& incremental, const NextSprites& reference, int round)
    {
        for (unsigned y = 0; y < 512; y++)
            ASSERT_EQ(incremental.FillClocks(y), reference.FillClocks(y)) << "round " << round << " line " << y;
    }
};

TEST_F(NextSpritesFill_Test, IncrementalMatchesAFullDecode)
{
    std::mt19937 rng(20261107);
    NextSprites incremental;
    incremental.Reset();
    uint8_t table[NextSprites::kSprites][5] = {};
    for (int round = 0; round < 60; round++)
    {
        // a burst of writes: positions, visibility, the 4-bit / relative / anchor-type bits, the scales; asked between writes as
        // the screen asks after every write
        const int writes = 1 + static_cast<int>(rng() % 40);
        for (int w = 0; w < writes; w++)
        {
            const unsigned sprite = rng() % 24;  // a few anchors with their relatives
            const unsigned byte = rng() % 5;
            uint8_t value = static_cast<uint8_t>(rng());
            if (byte == 3)
                value |= 0x40;  // extended: byte 4 counts
            if (byte == 4 && (rng() & 1))
                value = static_cast<uint8_t>((value & ~0xC0) | 0x40);  // a relative sprite
            table[sprite][byte] = value;
            Put(incremental, sprite, byte, value);
            (void)incremental.FillClocks(rng() % 512);
        }
        NextSprites reference;
        reference.Reset();
        for (unsigned s = 0; s < NextSprites::kSprites; s++)
            for (unsigned b = 0; b < 5; b++)
                Put(reference, s, b, table[s][b]);
        ExpectSameClocks(incremental, reference, round);
    }
}

TEST_F(NextSpritesFill_Test, RewritingTheSameTableKeepsTheEpoch)
{
    NextSprites s;
    s.Reset();
    Put(s, 3, 0, 0x40);
    Put(s, 3, 3, 0x80);
    const uint32_t epoch = s.Epoch();
    Put(s, 3, 0, 0x40);  // a program that uploads its table every frame
    Put(s, 3, 3, 0x80);
    s.WritePattern(0x12);  // the patterns do not move the fill clocks
    EXPECT_EQ(s.Epoch(), epoch);
    Put(s, 3, 0, 0x41);
    EXPECT_NE(s.Epoch(), epoch);
}

/// A whole line in one call (FillLine, over the resolved sprites) gives what the engine stepped one clock at a time gives (the
/// FillUntil path a write splitting the line takes, which decodes each sprite as it qualifies): pixels, collision clocks, the
/// "too many" answer; with a short line period too, where the engine is cut off
TEST_F(NextSpritesFill_Test, AWholeLineMatchesTheSteppedEngine)
{
    std::mt19937 rng(20261108);
    NextSprites s;
    s.Reset();
    for (unsigned i = 0; i < NextSprites::kPatternBytes; i++)
        s.WritePattern(static_cast<uint8_t>(rng() % 4 == 0 ? 0xE3 : rng()));  // some transparent pixels
    std::vector<uint16_t> fastHits, stepHits;
    NextSprites::LineBuffer fast, step;
    for (int round = 0; round < 12; round++)
    {
        for (unsigned sprite = 0; sprite < NextSprites::kSprites; sprite++)
        {
            Put(s, sprite, 0, static_cast<uint8_t>(rng()));
            Put(s, sprite, 1, static_cast<uint8_t>(rng() % 64));  // most on the first lines: they overlap
            Put(s, sprite, 2, static_cast<uint8_t>(rng()));
            Put(s, sprite, 3, static_cast<uint8_t>((rng() % 3 ? 0x80 : 0) | 0x40 | (rng() % 64)));
            uint8_t b4 = static_cast<uint8_t>(rng());
            if (rng() % 3 == 0)
                b4 = static_cast<uint8_t>((b4 & ~0xC0) | 0x40);  // relative
            Put(s, sprite, 4, b4);
        }
        const uint8_t control = (round & 1) ? 0x40 : 0x00;  // zero on top
        const unsigned lineClocks = (round % 3 == 0) ? 400 : NextSprites::kLineBudget;
        for (unsigned vcount = 0; vcount < 96; vcount += 3)
        {
            const bool fastBusy = s.Fill(vcount, control, 0xE3, lineClocks, fast, fastHits);
            NextSprites::FillCursor cursor;
            bool stepBusy = false;
            for (unsigned stop = 1; !cursor.done; stop++)
                stepBusy = s.FillUntil(cursor, stop, vcount, control, 0xE3, lineClocks, step, stepHits);
            ASSERT_EQ(fastBusy, stepBusy) << "round " << round << " line " << vcount;
            ASSERT_EQ(fastHits, stepHits) << "round " << round << " line " << vcount;
            for (unsigned x = 0; x < NextSprites::kGridWidth; x++)
            {
                const bool fastOpaque = fast.any && fast.pixel[x].opaque;
                const bool stepOpaque = step.any && step.pixel[x].opaque;
                ASSERT_EQ(fastOpaque, stepOpaque) << "round " << round << " line " << vcount << " x " << x;
                if (fastOpaque)
                {
                    ASSERT_EQ(fast.pixel[x].index, step.pixel[x].index) << "round " << round << " line " << vcount << " x " << x;
                    ASSERT_EQ(fast.pixel[x].sprite, step.pixel[x].sprite) << "round " << round << " line " << vcount << " x " << x;
                }
            }
        }
    }
}
