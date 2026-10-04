#include <gtest/gtest.h>

#include "emulator/io/ppi/ppi8255.h"

/// @brief After reset every port is an input: reads come from the board, writes only fill the latches
TEST(Ppi8255_Test, ResetMakesEveryPortAnInput)
{
    Ppi8255 ppi;
    ppi.SetInputA([] { return uint8_t{0x1E}; });
    EXPECT_TRUE(ppi.IsInputA());
    EXPECT_TRUE(ppi.IsInputB());
    EXPECT_EQ(ppi.Read(Ppi8255::kPortA), 0x1E);
    EXPECT_EQ(ppi.Read(Ppi8255::kPortB), 0xFF) << "nothing drives B";
    ppi.Write(Ppi8255::kPortB, 0x55);
    EXPECT_EQ(ppi.Read(Ppi8255::kPortB), 0xFF) << "an input port reads the pins, not its latch";
}

/// @brief A mode word sets the directions and clears every output latch; an output port reads back its latch
TEST(Ppi8255_Test, ModeWordClearsLatchesAndOutputsReadBack)
{
    Ppi8255 ppi;
    ppi.Write(Ppi8255::kPortB, 0x77);
    ppi.Write(Ppi8255::kControl, 0x90);   // A in, B out, C out (ROM BIOS Plus's parallel-port test)
    EXPECT_TRUE(ppi.IsInputA());
    EXPECT_FALSE(ppi.IsInputB());
    EXPECT_EQ(ppi.Read(Ppi8255::kPortB), 0x00) << "the mode word cleared the latch";
    ppi.Write(Ppi8255::kPortB, 0x02);
    EXPECT_EQ(ppi.Read(Ppi8255::kPortB), 0x02);
}

/// @brief Port C bit set / reset through the control port (bit 7 = 0); the nibbles keep their own directions
TEST(Ppi8255_Test, PortCBitSetResetAndNibbleDirections)
{
    Ppi8255 ppi;
    ppi.SetInputC([] { return uint8_t{0xA5}; });
    ppi.Write(Ppi8255::kControl, 0x90);       // C upper and lower: outputs
    ppi.Write(Ppi8255::kControl, 0x05);       // set PC2
    EXPECT_EQ(ppi.Read(Ppi8255::kPortC) & 0x04, 0x04);
    ppi.Write(Ppi8255::kControl, 0x04);       // reset PC2
    EXPECT_EQ(ppi.Read(Ppi8255::kPortC) & 0x04, 0x00);
    ppi.Write(Ppi8255::kControl, 0x0F);       // set PC7
    EXPECT_EQ(ppi.Read(Ppi8255::kPortC), 0x80);

    ppi.Write(Ppi8255::kControl, 0x98);       // C upper input, lower output
    ppi.Write(Ppi8255::kPortC, 0x3C);
    EXPECT_EQ(ppi.Read(Ppi8255::kPortC), 0xAC) << "upper nibble from the pins (A), lower from the latch (C)";
}

/// @brief The control register cannot be read back
TEST(Ppi8255_Test, ControlReadFloats)
{
    Ppi8255 ppi;
    ppi.Write(Ppi8255::kControl, 0x80);
    EXPECT_EQ(ppi.Read(Ppi8255::kControl), 0xFF);
}

/// @brief The state round-trips (the TTD blob)
TEST(Ppi8255_Test, StateRoundTrip)
{
    Ppi8255 a;
    a.Write(Ppi8255::kControl, 0x80);
    a.Write(Ppi8255::kPortA, 0x11);
    a.Write(Ppi8255::kPortB, 0x22);
    a.Write(Ppi8255::kPortC, 0x33);
    Ppi8255 b;
    b.SetState(a.GetState());
    EXPECT_EQ(b.Read(Ppi8255::kPortA), 0x11);
    EXPECT_EQ(b.Read(Ppi8255::kPortB), 0x22);
    EXPECT_EQ(b.Read(Ppi8255::kPortC), 0x33);
}
