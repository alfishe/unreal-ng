#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstdint>

#include "emulator/video/zx/cellexpand.h"

/// @file cellexpand_test.cpp
/// @brief The cell expansion (DrawRangeZX's whole cells): the path this build compiled (NEON, SSE2 or portable)
/// against the portable one and a per-bit reference, every pixel byte, unaligned outputs included

TEST(CellExpand_Test, EveryPixelByteMatchesTheReferenceOnThisBuildsPath)
{
    const uint32_t ink = 0xFF12345Au;
    const uint32_t paper = 0x80A5C3E1u;
    const uint16_t inkB = 0xBEEF;
    const uint16_t paperB = 0x1234;
    for (unsigned value = 0; value < 256; value++)
    {
        const uint8_t pixels = static_cast<uint8_t>(value);
        for (unsigned offset = 0; offset < 4; offset++)
        {
            uint32_t out[12] = {};
            uint32_t scalar[8] = {};
            uint16_t outB[12] = {};
            uint16_t scalarB[8] = {};
            cellexpand::Expand8(pixels, ink, paper, out + offset);
            cellexpand::Expand8Scalar(pixels, ink, paper, scalar);
            cellexpand::Expand8(pixels, inkB, paperB, outB + offset);
            cellexpand::Expand8Scalar(pixels, inkB, paperB, scalarB);
            for (unsigned i = 0; i < 8; i++)
            {
                const bool set = (pixels >> (7 - i)) & 1;
                ASSERT_EQ(out[offset + i], set ? ink : paper) << "byte " << value << " pixel " << i;
                ASSERT_EQ(scalar[i], set ? ink : paper) << "byte " << value << " pixel " << i;
                ASSERT_EQ(outB[offset + i], set ? inkB : paperB) << "byte " << value << " pixel " << i;
                ASSERT_EQ(scalarB[i], set ? inkB : paperB) << "byte " << value << " pixel " << i;
            }
            for (unsigned i = 0; i < offset; i++)
            {
                ASSERT_EQ(out[i], 0u) << "nothing before the cell";
                ASSERT_EQ(outB[i], 0u) << "nothing before the cell";
            }
            for (unsigned i = offset + 8; i < 12; i++)
            {
                ASSERT_EQ(out[i], 0u) << "nothing after the cell";
                ASSERT_EQ(outB[i], 0u) << "nothing after the cell";
            }
        }
    }
}
