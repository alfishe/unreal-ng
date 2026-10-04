#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "debugger/ttd/ttdusart8251.h"
#include "emulator/io/serial/serialpeer.h"
#include "emulator/io/serial/usart8251.h"

/// @brief The 8251's TTD blob: registers, the characters on both lines and the board latch come back
TEST(TTDUsart8251_Test, SaveLoadRoundTrip)
{
    Usart8251 usart;
    usart.SetClockSource([]() { return Usart8251::ClockRate{1500000, 156}; });
    LoopbackPeer peer;
    usart.SetPeer(&peer);
    usart.Reset(0);
    usart.Write(Usart8251::kControl, 0x4D, 0);
    usart.Write(Usart8251::kControl, 0x27, 0);
    usart.SetBoardLatch(1);
    usart.Write(Usart8251::kData, 0xA1, 0);
    usart.Write(Usart8251::kData, 0xB2, 0);
    usart.Advance(4000);   // 0xA1 sent and on its way back, 0xB2 on the TX line

    ttd::TTDUsart8251 serializer(usart);
    EXPECT_EQ(serializer.TTDPeripheralId(), ttd::PeripheralId::Usart8251);
    EXPECT_EQ(serializer.TTDDeviceName(), "Usart8251");
    ASSERT_EQ(serializer.TTDStateSize(), sizeof(Usart8251::State));
    std::vector<uint8_t> blob(serializer.TTDStateSize());
    serializer.TTDSaveState(blob.data());

    usart.Write(Usart8251::kControl, 0x40, 5000);   // internal reset: everything dropped
    usart.SetBoardLatch(0);
    serializer.TTDLoadState(blob.data());
    EXPECT_EQ(usart.BoardLatch(), 1);
    EXPECT_EQ(usart.GetState().command, 0x27);
    usart.Advance(8000);
    EXPECT_NE(usart.Read(Usart8251::kControl, 8000) & Usart8251::kRxRdy, 0);
    EXPECT_EQ(usart.Read(Usart8251::kData, 8000), 0xA1) << "the character on the RX line arrived after the restore";
}
