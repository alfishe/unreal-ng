// The DP8390 core through an NE2000 board (dp8390.h; network tdd §15 T-NET-1): pages, page-2 read-back, remote
// DMA read / write / ring wrap / RDC, ISR write-1-to-clear, IMR -> interrupt, the receive ring (header, CURR, the
// stored CRC), the address filter, overflow handling, transmit timing, loopback (datasheet behavior), tally counters

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "emulator/io/network/ethernet/dp8390.h"
#include "emulator/io/network/ethernet/ne2000board.h"

namespace
{
class Link : public IEthernetLink
{
public:
    void Transmit(IEthernetPort&, const uint8_t* frame, size_t length) override
    {
        frames.emplace_back(frame, frame + length);
    }
    std::vector<std::vector<uint8_t>> frames;
};

constexpr uint8_t kMac[6] = {0x02, 0x53, 0x50, 0x00, 0x00, 0x02};

std::vector<uint8_t> Frame(const uint8_t dst[6], size_t length, uint8_t fill = 0x11)
{
    std::vector<uint8_t> f(length, fill);
    std::memcpy(f.data(), dst, 6);
    const uint8_t src[6] = {0x52, 0x55, 0x0A, 0x00, 0x02, 0x02};
    std::memcpy(f.data() + 6, src, 6);
    f[12] = 0x08;
    f[13] = 0x00;
    return f;
}
}  // namespace

class Dp8390_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Ne2000Board::Settings s;
        s.mac = {kMac[0], kMac[1], kMac[2], kMac[3], kMac[4], kMac[5]};
        s.key = "test.eth";
        _board = std::make_unique<Ne2000Board>(s, [this]() { return _now; });
        _board->SetLink(&_link);
    }

    uint8_t R(uint16_t offset) { return _board->Read(offset); }
    void W(uint16_t offset, uint8_t value) { _board->Write(offset, value); }

    /// The RTL kit's init: stop, byte-wide DCR #48, ring #46-#60, TX at #40, BNRY #46, CURR #47, own MAC, start
    void InitLikeTheKit(uint8_t rcr = Dp8390::kRcrAb)
    {
        W(0x00, 0x21);
        W(0x0E, 0x48);
        W(0x0A, 0);
        W(0x0B, 0);
        W(0x0C, rcr);
        W(0x0D, 0x00);
        W(0x01, 0x46);
        W(0x02, 0x60);
        W(0x03, 0x46);
        W(0x04, 0x40);
        W(0x07, 0xFF);
        W(0x0F, 0x00);
        W(0x00, 0x61);   // page 1
        for (int i = 0; i < 6; ++i)
            W(static_cast<uint16_t>(0x01 + i), kMac[i]);
        W(0x07, 0x47);
        W(0x00, 0x22);   // page 0, start, abort DMA
    }

    void RemoteWrite(uint16_t address, const std::vector<uint8_t>& bytes)
    {
        W(0x08, static_cast<uint8_t>(address));
        W(0x09, static_cast<uint8_t>(address >> 8));
        W(0x0A, static_cast<uint8_t>(bytes.size()));
        W(0x0B, static_cast<uint8_t>(bytes.size() >> 8));
        W(0x00, 0x12);
        for (uint8_t b : bytes)
            W(0x10, b);
    }

    std::vector<uint8_t> RemoteRead(uint16_t address, uint16_t count)
    {
        W(0x08, static_cast<uint8_t>(address));
        W(0x09, static_cast<uint8_t>(address >> 8));
        W(0x0A, static_cast<uint8_t>(count));
        W(0x0B, static_cast<uint8_t>(count >> 8));
        W(0x00, 0x0A);
        std::vector<uint8_t> out;
        for (uint16_t i = 0; i < count; ++i)
            out.push_back(R(0x10));
        return out;
    }

    uint64_t _now = 1000;
    Link _link;
    std::unique_ptr<Ne2000Board> _board;
};

TEST_F(Dp8390_Test, ResetState_StoppedWithRst)
{
    EXPECT_EQ(R(0x00), 0x21) << "CR after reset: STP, remote DMA aborted";
    EXPECT_EQ(R(0x07) & Dp8390::kIsrRst, Dp8390::kIsrRst);
    W(0x00, 0x22);   // start
    EXPECT_EQ(R(0x07) & Dp8390::kIsrRst, 0) << "RST clears when the chip starts";
    R(0x1F);   // reset port
    EXPECT_EQ(R(0x00), 0x21);
    EXPECT_EQ(R(0x07) & Dp8390::kIsrRst, Dp8390::kIsrRst);
}

TEST_F(Dp8390_Test, PagesAndPage2ReadBack)
{
    InitLikeTheKit();
    W(0x00, 0x62);   // page 1
    EXPECT_EQ(R(0x01), kMac[0]);
    EXPECT_EQ(R(0x06), kMac[5]);
    EXPECT_EQ(R(0x07), 0x47) << "CURR";
    W(0x00, 0xA2);   // page 2: the page-0 write registers
    EXPECT_EQ(R(0x01), 0x46);
    EXPECT_EQ(R(0x02), 0x60);
    EXPECT_EQ(R(0x04), 0x40);
    EXPECT_EQ(R(0x0C) & 0x3F, Dp8390::kRcrAb);
    EXPECT_EQ(R(0x0E) & 0x7F, 0x48);
    W(0x00, 0x22);
    EXPECT_EQ(R(0x0A), 0x50) << "RTL8019AS ID 'P'";
    EXPECT_EQ(R(0x0B), 0x70) << "'p'";
}

TEST_F(Dp8390_Test, RemoteDma_WriteReadAndRdc)
{
    InitLikeTheKit();
    RemoteWrite(0x4000, {1, 2, 3, 4, 5});
    EXPECT_EQ(R(0x07) & Dp8390::kIsrRdc, Dp8390::kIsrRdc);
    W(0x07, Dp8390::kIsrRdc);
    EXPECT_EQ(R(0x07) & Dp8390::kIsrRdc, 0) << "write 1 to clear";
    EXPECT_EQ(RemoteRead(0x4000, 5), std::vector<uint8_t>({1, 2, 3, 4, 5}));
    EXPECT_EQ(R(0x07) & Dp8390::kIsrRdc, Dp8390::kIsrRdc);
    // CRDA shows where the DMA is
    W(0x08, 0x10);
    W(0x09, 0x40);
    W(0x0A, 2);
    W(0x0B, 0);
    W(0x00, 0x0A);
    EXPECT_EQ(R(0x08), 0x10);
    EXPECT_EQ(R(0x09), 0x40);
}

TEST_F(Dp8390_Test, RemoteRead_WrapsAtPstop)
{
    InitLikeTheKit();
    RemoteWrite(0x5FFE, {0xA1, 0xA2});
    RemoteWrite(0x4600, {0xB1, 0xB2});
    EXPECT_EQ(RemoteRead(0x5FFE, 4), std::vector<uint8_t>({0xA1, 0xA2, 0xB1, 0xB2})) << "PSTOP #60 -> PSTART #46";
}

TEST_F(Dp8390_Test, Prom_MacDoubledWithSignature)
{
    InitLikeTheKit();
    const std::vector<uint8_t> prom = RemoteRead(0x0000, 32);
    for (int i = 0; i < 6; ++i)
    {
        EXPECT_EQ(prom[2 * i], kMac[i]);
        EXPECT_EQ(prom[2 * i + 1], kMac[i]);
    }
    EXPECT_EQ(prom[28], 0x57);
    EXPECT_EQ(prom[31], 0x57);
}

TEST_F(Dp8390_Test, ByteModeLimitsRamToPage5F)
{
    InitLikeTheKit();
    RemoteWrite(0x6000, {0x42});
    EXPECT_EQ(RemoteRead(0x6000, 1)[0], 0xFF) << "#6000 is outside the RTL8019AS's 8-bit-mode RAM";
    W(0x0E, 0x49);   // word mode
    RemoteWrite(0x6000, {0x42});
    EXPECT_EQ(RemoteRead(0x6000, 1)[0], 0x42);
}

TEST_F(Dp8390_Test, Receive_HeaderCurrCrcAndPrx)
{
    InitLikeTheKit();
    const std::vector<uint8_t> f = Frame(kMac, 100);
    ASSERT_TRUE(_board->Offer(f.data(), f.size()));
    EXPECT_EQ(R(0x07) & Dp8390::kIsrPrx, Dp8390::kIsrPrx);
    W(0x00, 0x62);
    EXPECT_EQ(R(0x07), 0x48) << "CURR advanced one page (4 + 104 bytes)";
    W(0x00, 0x22);
    const std::vector<uint8_t> stored = RemoteRead(0x4700, 4 + 104);
    EXPECT_EQ(stored[0], Dp8390::kRsrPrx);
    EXPECT_EQ(stored[1], 0x48) << "next page";
    EXPECT_EQ(stored[2] | (stored[3] << 8), 104) << "count = frame + 4 CRC bytes";
    EXPECT_TRUE(std::equal(f.begin(), f.end(), stored.begin() + 4));
    const uint32_t crc = Dp8390::Crc32(f.data(), f.size());
    EXPECT_EQ(stored[104] | (stored[105] << 8) | (stored[106] << 16) | (static_cast<uint32_t>(stored[107]) << 24), crc);
}

TEST_F(Dp8390_Test, Receive_FrameWrapsTheRing)
{
    InitLikeTheKit();
    W(0x03, 0x50);   // BNRY
    W(0x00, 0x62);
    W(0x07, 0x5F);   // CURR: the last ring page
    W(0x00, 0x22);
    const std::vector<uint8_t> f = Frame(kMac, 400);
    ASSERT_TRUE(_board->Offer(f.data(), f.size()));
    W(0x00, 0x62);
    EXPECT_EQ(R(0x07), 0x47) << "4 + 404 bytes = 2 pages, #5F and #46: CURR = #47";
    W(0x00, 0x22);
    const std::vector<uint8_t> stored = RemoteRead(0x5F00, 4 + 404);   // the remote read wraps the same way
    EXPECT_TRUE(std::equal(f.begin(), f.end(), stored.begin() + 4));
}

TEST_F(Dp8390_Test, AddressFilter)
{
    InitLikeTheKit(Dp8390::kRcrAb);
    const uint8_t other[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x09};
    const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t multicast[6] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01};
    auto offer = [&](const uint8_t* dst) {
        const std::vector<uint8_t> f = Frame(dst, 64);
        _board->Offer(f.data(), f.size());
    };
    offer(other);
    EXPECT_EQ(_board->Chip().GetState().framesIn, 0u) << "someone else's unicast";
    offer(broadcast);
    EXPECT_EQ(_board->Chip().GetState().framesIn, 1u);
    EXPECT_EQ(_board->Chip().GetState().rsr & Dp8390::kRsrPhy, Dp8390::kRsrPhy);
    offer(multicast);
    EXPECT_EQ(_board->Chip().GetState().framesIn, 1u) << "RCR.AM off";
    // Multicast with the hash bit set
    W(0x0C, Dp8390::kRcrAb | Dp8390::kRcrAm);
    const unsigned index = Dp8390::MulticastHashIndex(multicast);
    W(0x00, 0x62);
    W(static_cast<uint16_t>(0x08 + index / 8), static_cast<uint8_t>(1u << (index % 8)));
    W(0x00, 0x22);
    offer(multicast);
    EXPECT_EQ(_board->Chip().GetState().framesIn, 2u);
    // Promiscuous takes everything
    W(0x0C, Dp8390::kRcrPro);
    offer(other);
    EXPECT_EQ(_board->Chip().GetState().framesIn, 3u);
    // Runts only with AR
    W(0x0C, Dp8390::kRcrAb);
    const std::vector<uint8_t> runt = Frame(kMac, 40);
    _board->Offer(runt.data(), runt.size());
    EXPECT_EQ(_board->Chip().GetState().framesIn, 3u);
    W(0x0C, Dp8390::kRcrAb | Dp8390::kRcrAr);
    _board->Offer(runt.data(), runt.size());
    EXPECT_EQ(_board->Chip().GetState().framesIn, 4u);
}

TEST_F(Dp8390_Test, KnownMulticastHash)
{
    // Linux 8390.c programs MAR bit ether_crc(addr) >> 26: all-hosts 01:00:5E:00:00:01 -> 31, broadcast -> 63,
    // IPv6 all-nodes 33:33:00:00:00:01 -> 62 (computed with the kernel's ether_crc)
    const uint8_t allHosts[6] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01};
    const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t allNodes[6] = {0x33, 0x33, 0x00, 0x00, 0x00, 0x01};
    EXPECT_EQ(Dp8390::MulticastHashIndex(allHosts), 31u);
    EXPECT_EQ(Dp8390::MulticastHashIndex(broadcast), 63u);
    EXPECT_EQ(Dp8390::MulticastHashIndex(allNodes), 62u);
}

TEST_F(Dp8390_Test, FullRing_FrameWaitsForTheDriver)
{
    InitLikeTheKit();
    const std::vector<uint8_t> big = Frame(kMac, 1514);   // 4 + 1518 bytes: 6 pages each
    int taken = 0;
    while (_board->Offer(big.data(), big.size()) && taken < 10)
        ++taken;
    EXPECT_EQ(taken, 4) << "25 free pages hold four 6-page frames";
    EXPECT_EQ(R(0x07) & Dp8390::kIsrOvw, 0) << "a switch keeps the frame: no overflow";
    // The driver frees the ring: BNRY up to CURR - 1
    W(0x00, 0x62);
    const uint8_t curr = R(0x07);
    W(0x00, 0x22);
    W(0x03, static_cast<uint8_t>(curr == 0x46 ? 0x5F : curr - 1));
    EXPECT_TRUE(_board->Offer(big.data(), big.size()));
}

TEST_F(Dp8390_Test, Overflow_DropsAndCountsUntilRecovery)
{
    InitLikeTheKit();
    W(0x07, 0xFF);
    // Overflow latched (as a real wire would leave it): frames are lost and counted, OVW stays until cleared
    Dp8390::State s = _board->Chip().GetState();
    s.isr |= Dp8390::kIsrOvw;
    _board->Chip().LoadState(s);
    const std::vector<uint8_t> f = Frame(kMac, 64);
    EXPECT_TRUE(_board->Offer(f.data(), f.size()));
    EXPECT_EQ(_board->Chip().GetState().framesIn, 0u);
    EXPECT_EQ(R(0x0F), 1) << "missed packet tally";
    EXPECT_EQ(R(0x0F), 0) << "tally counters clear on read";
    W(0x07, Dp8390::kIsrOvw);
    EXPECT_TRUE(_board->Offer(f.data(), f.size()));
    EXPECT_EQ(_board->Chip().GetState().framesIn, 1u);
}

TEST_F(Dp8390_Test, Transmit_ToTheLinkThenPtxAfterTheWireTime)
{
    InitLikeTheKit();
    const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const std::vector<uint8_t> f = Frame(broadcast, 342);
    RemoteWrite(0x4000, f);
    W(0x04, 0x40);
    W(0x05, static_cast<uint8_t>(f.size()));
    W(0x06, static_cast<uint8_t>(f.size() >> 8));
    W(0x00, 0x26);
    ASSERT_EQ(_link.frames.size(), 1u) << "on the wire at TXP";
    EXPECT_EQ(_link.frames[0], f);
    EXPECT_EQ(R(0x00) & Dp8390::kCrTxp, Dp8390::kCrTxp) << "still sending";
    EXPECT_EQ(R(0x07) & Dp8390::kIsrPtx, 0);
    _now += Dp8390::WireTime(342 + 4) - 1;
    EXPECT_EQ(R(0x07) & Dp8390::kIsrPtx, 0);
    _now += 1;   // 0.8 us x (346 + 20) = 292.8 us = 1025 base T
    EXPECT_EQ(R(0x07) & Dp8390::kIsrPtx, Dp8390::kIsrPtx);
    EXPECT_EQ(R(0x04) & Dp8390::kTsrPtx, Dp8390::kTsrPtx);
    EXPECT_EQ(R(0x00) & Dp8390::kCrTxp, 0);
    EXPECT_EQ(Dp8390::WireTime(346), 1025u);
}

TEST_F(Dp8390_Test, Loopback_NeverOnTheWireNeverInTheRing)
{
    InitLikeTheKit();
    W(0x0D, 0x02);   // internal loopback
    const std::vector<uint8_t> f = Frame(kMac, 64);
    RemoteWrite(0x4000, f);
    W(0x04, 0x40);
    W(0x05, 64);
    W(0x06, 0);
    W(0x00, 0x26);
    EXPECT_TRUE(_link.frames.empty());
    EXPECT_EQ(R(0x0C) & Dp8390::kRsrPrx, Dp8390::kRsrPrx) << "the receiver checked it (RSR)";
    W(0x00, 0x62);
    EXPECT_EQ(R(0x07), 0x47) << "CURR unchanged: datasheet loopback stays in the FIFO";
    W(0x00, 0x22);
    // The FIFO holds the CRC's bytes at its end
    const uint32_t crc = Dp8390::Crc32(f.data(), f.size());
    uint8_t fifo[8];
    for (uint8_t& b : fifo)
        b = R(0x06);
    EXPECT_EQ(fifo[4], static_cast<uint8_t>(crc));
}

TEST_F(Dp8390_Test, Interrupt_IsrAndImr)
{
    InitLikeTheKit();
    int changes = 0;
    _board->SetIrqListener([&]() { ++changes; });
    const std::vector<uint8_t> f = Frame(kMac, 64);
    _board->Offer(f.data(), f.size());
    EXPECT_FALSE(_board->Irq()) << "IMR masks PRX";
    W(0x0F, 0x01);
    EXPECT_TRUE(_board->Irq());
    EXPECT_EQ(changes, 1);
    W(0x07, 0x01);
    EXPECT_FALSE(_board->Irq());
    EXPECT_EQ(changes, 2);
}

TEST_F(Dp8390_Test, State_RoundTripIsByteExact)
{
    InitLikeTheKit();
    RemoteWrite(0x4100, {9, 8, 7});
    const std::vector<uint8_t> f = Frame(kMac, 300);
    _board->Offer(f.data(), f.size());
    std::vector<uint8_t> a(_board->StateSize());
    _board->SaveState(a.data());
    R(0x1F);   // reset: state changes
    RemoteWrite(0x4100, {0, 0, 0});
    ASSERT_TRUE(_board->LoadState(a.data(), a.size()));
    std::vector<uint8_t> b(_board->StateSize());
    _board->SaveState(b.data());
    EXPECT_EQ(a, b);
    EXPECT_EQ(RemoteRead(0x4100, 3), std::vector<uint8_t>({9, 8, 7}));
}
