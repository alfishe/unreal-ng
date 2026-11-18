// VDAC4 ESP32-S3 HLE, link level: boot, the slave HD mailbox, transactions, bulk DMA, PUT and raw media, rings.
// Driven only through SpiDevice select / exchange by a mirror of zifi_spi.asm (vdac4espdriver.h).

#include "vdac4espfixture.h"

using namespace Vdac4EspTest;

class Vdac4EspLink_Test : public Vdac4EspFixture
{
};

/// Before EspBootMs the slave is not configured: #FF and ZiFi_Init gives up; after it the state word is there
TEST_F(Vdac4EspLink_Test, BootDelayThenStateWord)
{
    _esp->PowerOn();
    EXPECT_EQ(_z80->ReadState(), 0xFF);
    EXPECT_FALSE(_z80->Init());  // 16384 state reads take ~150 ms of SPI time, well inside the 400 ms boot
    EXPECT_EQ(_esp->GetPhase(), Vdac4Esp::Phase::Booting);
    AdvanceMs(Vdac4Esp::kDefaultBootMs);
    ASSERT_TRUE(_esp->Running());
    const std::vector<uint8_t> word = _z80->ReadStateWord();
    EXPECT_EQ(word[0], 0x5A);
    EXPECT_EQ(word[1], 0xD7);
    EXPECT_EQ(word[2], 3);
    EXPECT_EQ(word[3], _esp->BootNonce());
    EXPECT_TRUE(_z80->Init());
    EXPECT_TRUE(_z80->PutCapable());
}

TEST_F(Vdac4EspLink_Test, PingAnswersReady)
{
    BootAndInit();
    Frame frame = Exchange(0x04, {}, 0xF0);
    EXPECT_EQ(frame.command, 0xF0);
    EXPECT_TRUE(frame.data.empty());
}

/// The same transaction number again gets the stored response; its data is not taken twice
TEST_F(Vdac4EspLink_Test, RepeatedTidGetsTheOldResponse)
{
    BootAndInit();
    const uint8_t ping[] = {0x5A, 0x04, 0x00, 0x00, 0x04};
    _z80->SetTx(ping, sizeof(ping));
    ASSERT_TRUE(_z80->Send(0));
    const uint8_t first[3] = {_z80->LastResponse()[0], _z80->LastResponse()[1], _z80->LastResponse()[2]};
    EXPECT_EQ(first[2], static_cast<uint8_t>(sizeof(ping)));  // accepted
    const uint32_t repeats = _esp->Link().GetStats().repeats;
    uint8_t sum[2] = {};
    ASSERT_TRUE(_z80->ResendLast(0, sum));
    EXPECT_EQ(_z80->LastResponse()[2], first[2]);
    EXPECT_EQ(_esp->Link().GetStats().repeats, repeats + 1);
    Frame frame;
    ASSERT_TRUE(_z80->ReceiveFrame(frame));
    EXPECT_EQ(frame.command, 0xF0);
    EXPECT_FALSE(_z80->ReceiveFrame(frame, 3));  // one PING, one answer
}

/// A request whose sum is wrong is left alone: no response, counted, the next good one works
TEST_F(Vdac4EspLink_Test, BadChecksumGetsNoResponse)
{
    BootAndInit();
    uint8_t request[6] = {0xC3, 0x77, 0x00, 0x00, 0x00, 0x00};
    request[5] = static_cast<uint8_t>(~(0xC3 + 0x77) + 1);  // off by one
    _z80->WriteBuffer(request, sizeof(request));
    EXPECT_EQ(_z80->OpenRead(0), 0xC3);  // still the request: nothing written over it
    _z80->Deselect();
    EXPECT_EQ(_esp->Link().GetStats().badRequests, 1u);
    Exchange(0x04, {}, 0xF0);
}

/// The response carries no more than the room the Z80 gave
TEST_F(Vdac4EspLink_Test, ResponseRespectsRoom)
{
    BootAndInit();
    ASSERT_TRUE(_z80->SendFrame(0x00, std::vector<uint8_t>(40, 0x11)));  // ECHO: 46 bytes come back
    uint8_t buffer[64] = {};
    ASSERT_TRUE(_z80->Turn(0, 10, buffer));
    EXPECT_EQ(_z80->LastResponse()[1], 10);
    EXPECT_NE(_z80->LastResponse()[0] & EspZ80Driver::kStatusMore, 0);
    // The rest: 46 bytes in all (45 and the pad), at most 54 per plain response
    size_t rest = 0;
    uint8_t burst[EspZ80Driver::kBulk + 1];
    for (size_t got = _z80->ReadBurst(burst); got != 0; got = _z80->ReadBurst(burst))
    {
        EXPECT_LE(got, static_cast<size_t>(EspZ80Driver::kChunk));
        rest += got;
    }
    EXPECT_EQ(rest, 36u);
}

/// ESP -> Z80 ring (4096): frames that do not fit wait whole, in order; the firmware takes no command meanwhile;
/// Z80 -> ESP ring (16384) then fills and takes no more
TEST_F(Vdac4EspLink_Test, RingRoomLimitsBothWays)
{
    BootAndInit();
    std::vector<uint8_t> echo(1000);
    for (int i = 0; i < 5; ++i)
    {
        std::fill(echo.begin(), echo.end(), static_cast<uint8_t>(i));
        ASSERT_TRUE(_z80->SendFrame(0x00, echo));
    }
    EXPECT_LE(_esp->Link().PendingToZ80(), EspSpiLink::kRingBytes);
    EXPECT_TRUE(_esp->Transport().Blocked());
    ASSERT_TRUE(_z80->SendFrame(0x04));  // PING behind the blocked send
    // Fill the input ring with pad bytes the parser skips
    const uint8_t pad[EspZ80Driver::kChunk] = {};
    for (int turn = 0; turn < 400; ++turn)
    {
        _z80->SetTx(pad, sizeof(pad));
        ASSERT_TRUE(_z80->Send(0));
        if (_z80->LastResponse()[2] < sizeof(pad))
            break;
    }
    EXPECT_EQ(_esp->Link().PendingFromZ80(), EspSpiLink::kInputRingBytes);
    _z80->SetTx(pad, 1);
    ASSERT_TRUE(_z80->Send(0));
    EXPECT_EQ(_z80->LastResponse()[2], 0);  // full
    // Read everything: the echoes in order, then the PING's answer
    Frame frame;
    for (int i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(_z80->ReceiveFrame(frame)) << i;
        EXPECT_EQ(frame.command, 0x00);
        ASSERT_EQ(frame.data.size(), 1000u);
        EXPECT_EQ(frame.data[0], i);
    }
    ASSERT_TRUE(_z80->ReceiveFrame(frame));
    EXPECT_EQ(frame.command, 0xF0);
    EXPECT_EQ(_esp->Transport().DroppedFrames(), 0u);
}

/// A waiting frame is lost when the Z80 stays quiet for 5 s (SpiLink::reserve); the firmware goes on
TEST_F(Vdac4EspLink_Test, WaitingFrameDroppedAfterPeerQuiet)
{
    BootAndInit();
    std::vector<uint8_t> echo(1000, 0x22);
    for (int i = 0; i < 5; ++i)
        ASSERT_TRUE(_z80->SendFrame(0x00, echo));
    ASSERT_TRUE(_esp->Transport().Blocked());
    AdvanceMs(EspSpiLink::kPeerQuietMs - 10);
    EXPECT_TRUE(_esp->Transport().Blocked());
    AdvanceMs(20);
    EXPECT_FALSE(_esp->Transport().Blocked());
    EXPECT_EQ(_esp->Transport().DroppedFrames(), 1u);
}

/// Output of 512 bytes and more goes by bulk DMA blocks (04 + 512 + 08, echo 03 + 512 + 07)
TEST_F(Vdac4EspLink_Test, BulkBlocksCarryLargeFrames)
{
    BootAndInit();
    std::vector<uint8_t> echo(1000);
    for (size_t i = 0; i < echo.size(); ++i)
        echo[i] = static_cast<uint8_t>(i * 13);
    Frame frame = Exchange(0x00, echo, 0x00);
    EXPECT_EQ(frame.data, echo);
    EXPECT_GE(_z80->bulkBlocksRead, 1u);
    EXPECT_GE(_esp->Link().GetStats().bulkBlocks, 1u);
    EXPECT_EQ(_esp->Link().GetStats().bulkBad, 0u);
}

/// A bad echo three times: the ESP gives up the DMA for the session, the block arrives by plain responses
TEST_F(Vdac4EspLink_Test, BadEchoThreeTimesDisablesBulk)
{
    BootAndInit();
    _z80->corruptEcho = true;
    std::vector<uint8_t> echo(1000);
    for (size_t i = 0; i < echo.size(); ++i)
        echo[i] = static_cast<uint8_t>(i * 7 + 1);
    Frame frame = Exchange(0x00, echo, 0x00);
    EXPECT_EQ(frame.data, echo);
    EXPECT_EQ(_z80->bulkBlocksRead, 0u);
    EXPECT_EQ(_esp->Link().GetStats().bulkGaveUp, 1u);
    EXPECT_EQ(_esp->Link().GetStats().bulkBad, EspSpiLinkCore::kBulkAttempts);
    // Still off: the next large frame goes inline
    const uint32_t blocks = _esp->Link().GetStats().bulkBlocks;
    frame = Exchange(0x00, echo, 0x00);
    EXPECT_EQ(frame.data, echo);
    EXPECT_EQ(_esp->Link().GetStats().bulkBlocks, blocks);
    // A new session turns it on again
    _z80->corruptEcho = false;
    ASSERT_TRUE(_z80->Init());
    frame = Exchange(0x00, echo, 0x00);
    EXPECT_EQ(frame.data, echo);
    EXPECT_GE(_z80->bulkBlocksRead, 1u);
}

/// SpiLink_PutBlock: a frame body in 512-byte PUT blocks; a block with a wrong XOR is refused and goes by the
/// mailbox
TEST_F(Vdac4EspLink_Test, PutBlocksCarryFrameBodies)
{
    BootAndInit();
    std::vector<uint8_t> echo(700);
    for (size_t i = 0; i < echo.size(); ++i)
        echo[i] = static_cast<uint8_t>(i * 3);
    Frame frame;
    ASSERT_TRUE(_z80->SendFrameWithPut(0x00, echo));
    ASSERT_TRUE(_z80->ReceiveCommand(0x00, frame));
    EXPECT_EQ(frame.data, echo);
    EXPECT_EQ(_z80->putBlocksSent, 1u);
    EXPECT_EQ(_esp->Link().GetStats().putBlocks, 1u);

    _z80->corruptPut = true;
    ASSERT_TRUE(_z80->SendFrameWithPut(0x00, echo));
    ASSERT_TRUE(_z80->ReceiveCommand(0x00, frame));
    EXPECT_EQ(frame.data, echo);
    EXPECT_EQ(_esp->Link().GetStats().putBad, EspZ80Driver::kPutTries);
}

/// Raw media (Scr_RawPut): MOD_PUT of 8192 bytes by one DMA, framed A6 by the link
TEST_F(Vdac4EspLink_Test, MediaPutDeliversModuleBytes)
{
    BootAndInit();
    const std::vector<uint8_t> image = BuildElf(9000);
    Frame frame = Exchange(0x69, QueryPayload("BIG.ELF", static_cast<uint32_t>(image.size()), 7), 0xE9);
    ASSERT_EQ(frame.data[0], 1);
    // [6A][size LE16][offset LE32]
    const std::vector<uint8_t> control{0x6A, 0x00, 0x20, 0, 0, 0, 0};
    ASSERT_TRUE(_z80->MediaPut(control, image.data(), 8192));
    ASSERT_TRUE(_z80->ReceiveCommand(0xEA, frame));
    ASSERT_EQ(frame.data.size(), 5u);
    EXPECT_EQ(frame.data[0], 1);
    EXPECT_EQ(frame.data[1] | (frame.data[2] << 8), 8192);
    // The rest by a normal MOD_PUT, then the commit
    std::vector<uint8_t> put = Le32(8192);
    put.insert(put.end(), image.begin() + 8192, image.end());
    frame = Exchange(0x6A, put, 0xEA);
    EXPECT_EQ(frame.data[0], 1);
    frame = Exchange(0x6B, {}, 0xEB);
    EXPECT_EQ(frame.data[0], 1);
    EXPECT_TRUE(_esp->Firmware().Cache().Has("BIG.ELF", static_cast<uint32_t>(image.size()), 7));
}

/// Only commands the app allowed take raw media; others are refused at the request
TEST_F(Vdac4EspLink_Test, MediaRangeGatesAppMedia)
{
    BootAndInit();
    std::vector<uint8_t> data(1024, 0x5C);
    EXPECT_FALSE(_z80->MediaPut({0x74, 0x00, 0x04}, data.data(), data.size()));
    ASSERT_EQ(Deliver("APP.ELF", BuildElf(300), 1), 1);
    Frame frame = Exchange(0x6C, {'A', 'P', 'P', '.', 'E', 'L', 'F', 0}, 0xEC);
    ASSERT_EQ(frame.data[0], 0);
    frame = Exchange(0x72, {0x74, 0x74}, 0xF9);
    EXPECT_EQ(frame.data[0], 0);
    EXPECT_TRUE(_esp->MediaRange().Contains(0x74));
    // [74][size][prefix 2 bytes] -> A6 74 len [prefix][data]: the app gets command 74 with prefix + data
    ASSERT_TRUE(_z80->MediaPut({0x74, 0x00, 0x04, 0xAA, 0xBB}, data.data(), data.size()));
    for (int i = 0; i < 5 && _runtime.commands.empty(); ++i)
        AdvanceMs(1);
    ASSERT_FALSE(_runtime.commands.empty());
    EXPECT_EQ(_runtime.commands.back(), 0x74);
}
