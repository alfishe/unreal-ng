#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "debugger/ttd/ttdpit8253.h"
#include "emulator/io/timer/pit8253.h"

/// @brief The 8253's TTD blob: save, disturb, load - the chip counts on from the same point
TEST(TTDPit8253_Test, SaveLoadRoundTrip)
{
    Pit8253 pit;
    pit.Write(Pit8253::kControl, 0x36, 0);
    pit.Write(0, 156, 0);
    pit.Write(0, 0, 0);
    pit.Write(Pit8253::kControl, 0x70, 0);
    pit.Write(1, 0x34, 0);
    pit.Write(1, 0x12, 0);
    pit.Advance(10001);

    ttd::TTDPit8253 serializer(pit);
    EXPECT_EQ(serializer.TTDPeripheralId(), ttd::PeripheralId::Pit8253);
    EXPECT_EQ(serializer.TTDDeviceName(), "Pit8253");
    ASSERT_EQ(serializer.TTDStateSize(), sizeof(Pit8253::State));
    std::vector<uint8_t> blob(serializer.TTDStateSize());
    serializer.TTDSaveState(blob.data());
    const uint16_t c0 = pit.PeekCount(0);
    const uint16_t c1 = pit.PeekCount(1);

    pit.Advance(77777);
    pit.Write(Pit8253::kControl, 0x30, 77777);
    serializer.TTDLoadState(blob.data());
    EXPECT_EQ(pit.PeekCount(0), c0);
    EXPECT_EQ(pit.PeekCount(1), c1);
    std::vector<uint8_t> again(serializer.TTDStateSize());
    serializer.TTDSaveState(again.data());
    EXPECT_EQ(blob, again) << "the blob comes back byte for byte";
}
