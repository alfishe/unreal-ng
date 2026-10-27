// PortDecoder's bus cycles after the ZX-bus slots rule migration (docs/inprogress/2026-10-03-zx-bus-slots/tdd.md,
// SL-3): one resolution pass per claimed cycle (the card tap, the board's claim override and the shared-bus rule R6),
// the self-decoding devices and the exact peripheral port map served from claim tables.
// The machine-level behavior stays pinned by fulldecodeclaim_test.cpp, portdecoder_portmap_test.cpp, covox_test.cpp
// and the per-model decoder tests; these tests pin what SL-3 changed: who decides, how often, and what is offered.

#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/ports/models/portdecoder_pentagon128.h"
#include "emulator/ports/portdecoder.h"

namespace
{

/// A card or a board device: records every cycle it sees and every read-claim question it is asked
class CountingDevice final : public PortDevice
{
public:
    explicit CountingDevice(uint8_t readValue = 0xFF, bool claimsRead = false)
        : readValue(readValue), claimsRead(claimsRead)
    {
    }

    uint8_t portDeviceInMethod(uint16_t port) override
    {
        reads.push_back(port);
        return readValue;
    }

    void portDeviceOutMethod(uint16_t port, uint8_t value) override { writes.emplace_back(port, value); }

    bool portDeviceClaimsRead(uint16_t) override
    {
        claimQuestions++;
        return claimsRead;
    }

    uint8_t readValue;
    bool claimsRead;
    int claimQuestions = 0;
    std::vector<uint16_t> reads;
    std::vector<std::pair<uint16_t, uint8_t>> writes;
};

/// A self-decoding device: takes every port tryClaimOut/In is offered when `takes`; declares `claims` (empty: the
/// PortDevice default, every port)
class SelfDecodingDevice final : public PortDevice
{
public:
    uint8_t portDeviceInMethod(uint16_t) override { return 0xFF; }
    void portDeviceOutMethod(uint16_t, uint8_t) override {}

    bool tryClaimOut(uint16_t rawPort, uint8_t value) override
    {
        offered.push_back(rawPort);
        lastValue = value;
        return takes;
    }

    bool tryClaimIn(uint16_t rawPort, uint8_t& outValue) override
    {
        offered.push_back(rawPort);
        outValue = 0x42;
        return takes;
    }

    std::vector<PortMaskMatch> selfDecodingClaims() const override
    {
        return claims.empty() ? PortDevice::selfDecodingClaims() : claims;
    }

    bool takes = true;
    std::vector<PortMaskMatch> claims;
    std::vector<uint16_t> offered;
    uint8_t lastValue = 0;
};

class PortDecoder_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context.emulatorState.flags &= ~CF_TRDOS;
    }

    EmulatorContext _context{ LoggerLevel::LogError };
    PortDecoder_Pentagon128 _decoder{ &_context };
};

} // namespace

/// R6, now inside ReadCycle: a board-decoded port keeps the board device's value unless the card claims the read;
/// an otherwise-undecoded port is driven by the card
TEST_F(PortDecoder_Test, ReadCycleAppliesSharedBusRule)
{
    CountingDevice ay(0xA0);
    ASSERT_TRUE(_decoder.RegisterPortHandler(0xFFFD, &ay));

    // A silent card on the AY's low byte: the board decodes, the AY's value is the bus value
    CountingDevice silent(0x5A, /*claimsRead*/ false);
    ASSERT_TRUE(_decoder.RegisterFullDecodeLowBytePort(0xFD, &silent));
    bool cardDrove = false;
    EXPECT_EQ(_decoder.ReadCycle(0xFFFD, 0x0000, cardDrove), 0xA0);
    EXPECT_TRUE(cardDrove);
    EXPECT_TRUE(_decoder.WasLastPortDecoded());
    EXPECT_EQ(silent.reads.size(), 1u) << "the card sees the cycle once";
    EXPECT_EQ(ay.reads.size(), 1u);
    _decoder.UnregisterFullDecodeLowBytePort(0xFD, &silent);

    // A claiming card: the board stands down (the AY is not read), the card's value is the bus value
    CountingDevice claiming(0x5B, /*claimsRead*/ true);
    ASSERT_TRUE(_decoder.RegisterFullDecodeLowBytePort(0xFD, &claiming));
    EXPECT_EQ(_decoder.ReadCycle(0xFFFD, 0x0000, cardDrove), 0x5B);
    EXPECT_TRUE(cardDrove);
    EXPECT_EQ(ay.reads.size(), 1u) << "the board stood down";
    EXPECT_EQ(claiming.reads.size(), 1u) << "the card is read once (stateful status registers)";
    _decoder.UnregisterFullDecodeLowBytePort(0xFD, &claiming);

    // An undecoded port (#FF outside TR-DOS: the FDC is off the bus): the silent card drives it
    CountingDevice silentFf(0x3C, /*claimsRead*/ false);
    ASSERT_TRUE(_decoder.RegisterFullDecodeLowBytePort(0xFF, &silentFf));
    EXPECT_EQ(_decoder.ReadCycle(0x00FF, 0x0000, cardDrove), 0x3C);
    EXPECT_TRUE(cardDrove);
    EXPECT_FALSE(_decoder.WasLastPortDecoded());

    // A port no card claims: the board's alone
    EXPECT_EQ(_decoder.ReadCycle(0xFFFD, 0x0000, cardDrove), 0xA0);
    EXPECT_FALSE(cardDrove);
}

/// The resolution decides the board's stand-down once per claimed cycle: a read asks the card once whether it
/// claims the read (the tap and the override asked once each before SL-3); a write never asks
TEST_F(PortDecoder_Test, ClaimDecidedOncePerCycle)
{
    CountingDevice ay(0xA0);
    ASSERT_TRUE(_decoder.RegisterPortHandler(0xFFFD, &ay));
    CountingDevice card(0x5B, /*claimsRead*/ true);
    ASSERT_TRUE(_decoder.RegisterFullDecodeLowBytePort(0xFD, &card));

    bool cardDrove = false;
    _decoder.ReadCycle(0x12FD, 0x0000, cardDrove);
    EXPECT_EQ(card.claimQuestions, 1);

    _decoder.WriteCycle(0xFFFD, 0x07, 0x0000);
    EXPECT_EQ(card.claimQuestions, 1);
    ASSERT_EQ(card.writes.size(), 1u);
    EXPECT_EQ(card.writes[0], std::make_pair(uint16_t{ 0xFFFD }, uint8_t{ 0x07 }));
    EXPECT_TRUE(ay.writes.empty()) << "a write to a low-byte card's port stands the board down";

    // Outside a bus cycle (a direct decode call) the override decides by itself, with the same answer
    uint16_t decoded = 0xFFFD;
    PortDecodeDisposition disp;
    EXPECT_TRUE(_decoder.OverrideDecodeForFullDecodeClaim(0xFFFD, decoded, disp, /*isRead*/ true));
    EXPECT_EQ(decoded, 0x0000);
    EXPECT_TRUE(disp.wasFullDecodeClaimed);
}

/// Self-decoding devices are offered only the ports their declared claims cover (one bit test for the rest), in
/// registration order, each device once; a device without declared claims is offered every port
TEST_F(PortDecoder_Test, SelfDecodingDevicesOfferedTheirClaims)
{
    SelfDecodingDevice covoxFb;
    covoxFb.claims = { PortMaskMatch{ 0x00FF, 0x00FB } };
    ASSERT_TRUE(_decoder.RegisterSelfDecodingDevice(&covoxFb));
    EXPECT_FALSE(_decoder.RegisterSelfDecodingDevice(&covoxFb)) << "a device registers once";
    EXPECT_FALSE(_decoder.RegisterSelfDecodingDevice(nullptr));

    EXPECT_FALSE(_decoder.DispatchSelfDecodingOut(0x001F, 0x80));
    EXPECT_TRUE(covoxFb.offered.empty()) << "a port outside the device's claims is never offered";
    EXPECT_TRUE(_decoder.DispatchSelfDecodingOut(0x12FB, 0x80));
    ASSERT_EQ(covoxFb.offered.size(), 1u);
    EXPECT_EQ(covoxFb.offered[0], 0x12FB);
    uint8_t value = 0;
    EXPECT_TRUE(_decoder.DispatchSelfDecodingIn(0x00FB, value));
    EXPECT_EQ(value, 0x42);

    // Registration order: a declining device first, then a catch-all one; a device is asked once even when two of
    // its claims cover the port
    SelfDecodingDevice catchAll;
    ASSERT_TRUE(_decoder.RegisterSelfDecodingDevice(&catchAll));
    covoxFb.takes = false;
    covoxFb.offered.clear();
    EXPECT_TRUE(_decoder.DispatchSelfDecodingOut(0x00FB, 0x11));
    EXPECT_EQ(covoxFb.offered.size(), 1u);
    EXPECT_EQ(catchAll.offered.size(), 1u);
    EXPECT_EQ(catchAll.lastValue, 0x11);

    _decoder.UnregisterSelfDecodingDevice(&catchAll);
    _decoder.UnregisterSelfDecodingDevice(&catchAll);   // absent: no-op
    EXPECT_FALSE(_decoder.DispatchSelfDecodingOut(0x00FB, 0x22)) << "only the declining device is left";
    _decoder.UnregisterSelfDecodingDevice(&covoxFb);
    covoxFb.offered.clear();
    EXPECT_FALSE(_decoder.DispatchSelfDecodingOut(0x00FB, 0x22));
    EXPECT_TRUE(covoxFb.offered.empty());

    SelfDecodingDevice twoClaims;
    twoClaims.claims = { PortMaskMatch{ 0x00FF, 0x00FB }, PortMaskMatch{ 0x000F, 0x000B } };
    ASSERT_TRUE(_decoder.RegisterSelfDecodingDevice(&twoClaims));
    twoClaims.takes = false;
    EXPECT_FALSE(_decoder.DispatchSelfDecodingOut(0x00FB, 0x33));
    EXPECT_EQ(twoClaims.offered.size(), 1u) << "asked once although both claims cover #FB";
}

/// The exact peripheral port map as exact claims on the decoded port: one device per port, unregistering frees it
TEST_F(PortDecoder_Test, PeripheralPortsAreExactClaims)
{
    CountingDevice first(0x11);
    CountingDevice second(0x22);
    ASSERT_TRUE(_decoder.RegisterPortHandler(0xBFFD, &first));
    EXPECT_FALSE(_decoder.RegisterPortHandler(0xBFFD, &second)) << "a port has one device";
    EXPECT_FALSE(_decoder.RegisterPortHandler(0x1234, nullptr));

    EXPECT_EQ(_decoder.PeripheralPortIn(0xBFFD), 0x11);
    EXPECT_TRUE(_decoder.WasLastPortDecoded());
    _decoder.PeripheralPortOut(0xBFFD, 0x05);
    ASSERT_EQ(first.writes.size(), 1u);

    // Exact: another high byte of the same low byte is not the device's port
    EXPECT_EQ(_decoder.PeripheralPortIn(0xFFFD), 0xFF);
    _decoder.PeripheralPortOut(0x3FFD, 0x05);
    EXPECT_EQ(first.writes.size(), 1u);
    EXPECT_EQ(first.reads.size(), 1u);

    _decoder.UnregisterPortHandler(0xBFFD);
    EXPECT_EQ(_decoder.PeripheralPortIn(0xBFFD), 0xFF);
    ASSERT_TRUE(_decoder.RegisterPortHandler(0xBFFD, &second));
    EXPECT_EQ(_decoder.PeripheralPortIn(0xBFFD), 0x22);
}
