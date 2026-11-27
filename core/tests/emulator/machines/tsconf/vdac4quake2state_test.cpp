// The VDAC4 Tang's Quake II core (bomzh-3D) as time travel saves it (docs/inprogress/2026-11-15-tsconf-vdac4/quake2.md
// §8.1): the core's state without its SDRAM, the SDRAM apart, and the Evo side apart from the rest. A state taken
// anywhere in the command stream (inside a command, inside WRITE data, after an RD copy) and loaded into another core
// goes on to the same results as the run that was never cut.

#include <gtest/gtest.h>

#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "3rdparty/bomzh/bomzh.h"
#include "emulator/platforms/tsconf/vdac4/vdac4bomzh.h"
#include "emulator/platforms/tsconf/vdac4/vdac4espstate.h"

namespace
{
constexpr uint32_t kQueue = 0x40000;
constexpr uint32_t kRdAddr = 0x88;
constexpr uint32_t kRdLen = 0x8C;

/// One LINK write: a queue word or a register
struct Op
{
    uint32_t address;
    uint32_t value;
};

void Queue(std::vector<Op>& ops, std::initializer_list<uint32_t> words)
{
    for (uint32_t w : words)
        ops.push_back({kQueue, w});
}

/// A stream that touches every part of the state: the palette and a texture by WRITE (longer than one streamed
/// piece), SETUP (16 x 8, frames 0x10000 / 0x20000, z 0x30000), FILL, TEX, POINT, PIC, VBUF / STBUF / TRIS, TIME,
/// SCISSOR, FRAME with a blend, MARK, and RD copies into the window between them
std::vector<Op> Stream()
{
    std::vector<Op> ops;
    Queue(ops, {0x0900'0002u, 0x50000, 0x0A00'0000u, 0x0000'1E14u});  // palette: color 1 = 0A 14 1E
    // WRITE 1300 words at 0x100000: an 8 x 8 tiled texture (its first 64 bytes) and more data past one piece
    ops.push_back({kQueue, 0x0900'0000u | 1300u});
    ops.push_back({kQueue, 0x100000});
    for (uint32_t i = 0; i < 1300; ++i)
        ops.push_back({kQueue, 0x0101'0101u * ((i * 7 + 3) & 0xFF)});
    Queue(ops, {0x0A00'0000u, 0x10000, 0x20000, 0x30000, (8u << 16) | 16u, 0x40000, 0, 0x50000, 0x60000});
    Queue(ops, {0x0600'0001u, 0, (8u << 16) | 16u, 0});            // FILL z with 0
    Queue(ops, {0x0600'0000u, 0, (8u << 16) | 16u, 1});            // FILL the frame with color 1
    Queue(ops, {0x0100'0000u, 0x100000, 3u | (3u << 4)});          // TEX tiled 8 x 8
    Queue(ops, {0x0400'0205u, (2u << 16) | 3u, 1u << 28});          // POINT 2 x 2, color 5
    ops.push_back({kRdAddr, 0x10000});
    ops.push_back({kRdLen, 4});                                     // the frame drawn so far into the window
    Queue(ops, {0x0500'0000u, (1u << 16) | 4u, (4u << 16) | 6u, 0x100000, 8, 0x10000, 0x10000});  // PIC 6 x 4
    Queue(ops, {0x0C00'0003u, (1u << 16) | 1u, 1u << 28, 100, (1u << 16) | 14u, 1u << 28, 200,
                (7u << 16) | 1u, 1u << 28, 300});                   // VBUF: three vertices
    Queue(ops, {0x0D00'0003u, 0, 7u, 7u << 16});                    // STBUF: three texture points
    Queue(ops, {0x0E00'1C01u, 0u | (1u << 9) | (2u << 18), 0u | (1u << 9) | (2u << 18)});  // TRIS, no light, z
    Queue(ops, {0x0800'0000u, 77});                                 // TIME
    Queue(ops, {0x0B00'0000u, (1u << 16) | 2u, (7u << 16) | 12u});  // SCISSOR
    Queue(ops, {0x0400'0309u, (3u << 16) | 8u, 1u << 29});          // POINT 3 x 3 inside the scissor
    Queue(ops, {0x0700'0000u, 0x4010'2030u});                       // FRAME, blended
    Queue(ops, {0x0F00'0000u, 0x1234});                             // MARK
    ops.push_back({kRdAddr, 0x30000});
    ops.push_back({kRdLen, 8});                                     // z into the window
    Queue(ops, {0x0600'0000u, (2u << 16) | 2u, (3u << 16) | 5u, 9}); // the next frame begins
    return ops;
}

void Feed(Vdac4BomzhCore& core, const std::vector<Op>& ops, size_t from, size_t to)
{
    for (size_t i = from; i < to; ++i)
        core.LinkWrite(ops[i].address, ops[i].value);
}

std::vector<uint8_t> StateOf(const Vdac4BomzhCore& core)
{
    std::vector<uint8_t> bytes;
    Vdac4EspStateWriter w(bytes);
    core.Save(w);
    return bytes;
}

std::vector<uint8_t> Picture(const Vdac4BomzhCore& core)
{
    std::vector<uint8_t> rgba;
    uint16_t width = 0, height = 0;
    if (!core.ShownPicture(rgba, width, height))
        rgba.clear();
    return rgba;
}

/// The index of the first op with this address and value
size_t IndexOf(const std::vector<Op>& ops, uint32_t address, uint32_t value)
{
    for (size_t i = 0; i < ops.size(); ++i)
        if (ops[i].address == address && ops[i].value == value)
            return i;
    return ops.size();
}
}  // namespace

/// The model's state has the size the header says, the same bytes on every host
TEST(Vdac4BomzhState_Test, ModelStateHasItsFixedSize)
{
    bz_t* bz = bz_create(1u << 16);
    ASSERT_NE(bz, nullptr);
    std::vector<uint8_t> a(BZ_STATE_BYTES + 16, 0xEE);
    bz_state_save(bz, a.data());
    for (size_t i = BZ_STATE_BYTES; i < a.size(); ++i)
        EXPECT_EQ(a[i], 0xEE) << "written past BZ_STATE_BYTES at " << i;
    bz_free(bz);
}

/// Cut anywhere, saved, loaded into a fresh core with the SDRAM copied: the rest of the stream gives the same core
/// state, SDRAM, picture and window as the run never cut
TEST(Vdac4BomzhState_Test, StateCutAnywhereGoesOnAsTheUncutRun)
{
    const std::vector<Op> ops = Stream();
    Vdac4BomzhCore whole;
    ASSERT_TRUE(whole.IsReady());
    Feed(whole, ops, 0, ops.size());
    const std::vector<uint8_t> wholeState = StateOf(whole);
    const std::vector<uint8_t> wholePicture = Picture(whole);
    ASSERT_FALSE(wholePicture.empty());
    EXPECT_EQ(whole.Frames(), 1u);
    EXPECT_EQ(whole.GetStats().badWords, 0u);
    EXPECT_FALSE(whole.GetStats().badCommand);

    const size_t writeData = 7;                                     // inside the WRITE data, before its first piece
    const size_t writePiece = 6 + 1100;                             // past its first streamed piece
    const size_t setupMiddle = IndexOf(ops, kQueue, 0x10000) + 2;   // inside SETUP
    const size_t afterRd = IndexOf(ops, kRdLen, 4) + 1;             // the window holds a copy
    const size_t trisMiddle = IndexOf(ops, kQueue, 0x0E00'1C01u) + 1;
    const size_t vbufMiddle = IndexOf(ops, kQueue, 0x0C00'0003u) + 4;
    const size_t afterFrame = IndexOf(ops, kQueue, 0x0F00'0000u);
    for (size_t cut : {writeData, writePiece, setupMiddle, afterRd, vbufMiddle, trisMiddle, afterFrame})
    {
        SCOPED_TRACE("cut at op " + std::to_string(cut));
        ASSERT_LT(cut, ops.size());
        Vdac4BomzhCore first;
        Feed(first, ops, 0, cut);
        const std::vector<uint8_t> state = StateOf(first);

        Vdac4BomzhCore second;
        Feed(second, ops, 0, 40);  // another history: everything of it must go
        std::memcpy(second.Memory(), first.Memory(), Vdac4BomzhCore::kMemoryBytes);
        Vdac4EspStateReader r(state.data(), state.size());
        ASSERT_TRUE(second.Load(r));
        EXPECT_TRUE(r.AtEnd());
        EXPECT_EQ(StateOf(second), state) << "the loaded state saves the same bytes";

        Feed(second, ops, cut, ops.size());
        EXPECT_EQ(StateOf(second), wholeState);
        EXPECT_EQ(std::memcmp(second.Memory(), whole.Memory(), Vdac4BomzhCore::kMemoryBytes), 0) << "SDRAM";
        EXPECT_EQ(Picture(second), wholePicture);
        for (uint32_t i = 0; i < 1024; ++i)
            ASSERT_EQ(second.LinkRead(0x1000 + 4 * i), whole.LinkRead(0x1000 + 4 * i)) << "window word " << i;
    }
}

/// A state of another kind is refused and leaves the core as it was
TEST(Vdac4BomzhState_Test, ForeignStateIsRefused)
{
    const std::vector<Op> ops = Stream();
    Vdac4BomzhCore core;
    Feed(core, ops, 0, 30);
    const std::vector<uint8_t> before = StateOf(core);
    std::vector<uint8_t> bad = before;
    bad[0] ^= 1;
    Vdac4EspStateReader r(bad.data(), bad.size());
    EXPECT_FALSE(core.Load(r));
    std::vector<uint8_t> shortState(before.begin(), before.end() - 3);
    Vdac4EspStateReader s(shortState.data(), shortState.size());
    EXPECT_FALSE(core.Load(s));
    EXPECT_EQ(StateOf(core), before);
}

/// Reset brings the core to a fresh one's state: its SDRAM zero, the registers at reset
TEST(Vdac4BomzhState_Test, ResetIsAFreshCore)
{
    const std::vector<Op> ops = Stream();
    Vdac4BomzhCore used;
    Feed(used, ops, 0, ops.size());
    used.Reset();
    Vdac4BomzhCore fresh;
    EXPECT_EQ(StateOf(used), StateOf(fresh));
    EXPECT_EQ(std::memcmp(used.Memory(), fresh.Memory(), Vdac4BomzhCore::kMemoryBytes), 0);
}

/// The Evo side (M_SEL, the Evo's SPI, the reset rule) saves and loads apart from the rest, which the ESP's LINK
/// writes alone move: TTD stores it at every capture and computes the rest again by a replay. The state hash
/// follows the whole state
TEST(Vdac4BomzhState_Test, EvoSideGoesApartFromTheLinkSide)
{
    const std::vector<Op> ops = Stream();
    Vdac4BomzhCore a;
    Feed(a, ops, 0, ops.size() / 2);
    a.SetMsel(true, 100);
    a.EvoByte(0x68, 120);
    std::vector<uint8_t> evo;
    {
        Vdac4EspStateWriter w(evo);
        a.SaveEvoSide(w);
    }
    // The same LINK side with another Evo side: the Evo side's bytes bring the whole state back
    Vdac4BomzhCore b;
    Feed(b, ops, 0, ops.size() / 2);
    EXPECT_NE(a.StateHash(), b.StateHash());
    Vdac4EspStateReader r(evo.data(), evo.size());
    ASSERT_TRUE(b.LoadEvoSide(r));
    EXPECT_TRUE(r.AtEnd());
    EXPECT_EQ(a.StateHash(), b.StateHash());
    EXPECT_EQ(StateOf(a), StateOf(b));
    // A LINK write moves the hash, not the Evo side
    a.LinkWrite(kQueue, 0x0700'0000u);
    a.LinkWrite(kQueue, 0);
    EXPECT_NE(a.StateHash(), b.StateHash());
    std::vector<uint8_t> evoAfter;
    {
        Vdac4EspStateWriter w(evoAfter);
        a.SaveEvoSide(w);
    }
    EXPECT_EQ(evoAfter, evo);
    // Foreign bytes are refused
    evo[0] ^= 1;
    Vdac4EspStateReader bad(evo.data(), evo.size());
    EXPECT_FALSE(b.LoadEvoSide(bad));
}

/// The shown serial moves with what can change the picture (FRAME, SETUP, a WRITE over the shown frame), not with
/// drawing into the other frame or a WRITE elsewhere
TEST(Vdac4BomzhState_Test, ShownSerialFollowsThePicture)
{
    const std::vector<Op> ops = Stream();
    Vdac4BomzhCore core;
    Feed(core, ops, 0, ops.size());
    uint64_t serial = core.ShownSerial();
    core.LinkWrite(kQueue, 0x0600'0000u);  // FILL into the frame drawn: the shown one keeps its picture
    core.LinkWrite(kQueue, 0);
    core.LinkWrite(kQueue, (8u << 16) | 16u);
    core.LinkWrite(kQueue, 3);
    EXPECT_EQ(core.ShownSerial(), serial);
    for (uint32_t w : {0x0900'0001u, 0x200000u, 0x1234'5678u})  // WRITE far from the frames
        core.LinkWrite(kQueue, w);
    EXPECT_EQ(core.ShownSerial(), serial);
    // One FRAME after SETUP: the shown frame is fb[0], a WRITE over it changes the picture
    for (uint32_t w : {0x0900'0001u, 0x10000u, 0x0303'0303u})
        core.LinkWrite(kQueue, w);
    EXPECT_GT(core.ShownSerial(), serial);
    serial = core.ShownSerial();
    core.LinkWrite(kQueue, 0x0700'0000u);
    core.LinkWrite(kQueue, 0);
    EXPECT_GT(core.ShownSerial(), serial);
}
