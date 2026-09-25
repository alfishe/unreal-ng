#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/models/portdecoder_scorpion256.h"
#include "emulator/sound/covox.h"

/// Regression (SQT.TRD / SQTracker v1.0, a genuine 4-channel Scorpion +
/// SoundDrive real-time player): PortDecoder_Scorpion256 had NO
/// Covox/SoundDrive routing at all, unlike PortDecoder_Pentagon128. Its
/// catch-all OUT/IN arms dispatched the RAW, unmasked port through the
/// exact-address map (`PeripheralPortOut(port, value)`), so a write meant
/// for e.g. #FB - written as `OUT (n),A`, which puts the accumulator value
/// on the address bus's upper byte (e.g. 0x8DFB) - never matched anything.
/// #1F/#5F fared worse: Scorpion's Beta128 mirror decode claims them
/// unconditionally (TryBeta128MirrorPort's exact-match set), so those
/// writes were misdirected into the real WD1793 controller as bogus
/// register writes whenever the FDC precedence gate happened to be open,
/// live-traced on real hardware content via MCP port-trace (dev=6/
/// WD1793_Status, ~130k writes captured). Covox is now a self-decoding
/// device (PortDevice::tryClaimOut/In via PortDecoder::
/// DispatchSelfDecodingOut/In), tried from both catch-alls with the raw
/// port undisturbed - see PortDecoder_Pentagon128_Test for the equivalent
/// Pentagon coverage.
class PortDecoder_Scorpion256_Soundrive_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    PortDecoder_Scorpion256* _portDecoder = nullptr;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _context->config.sound.sd = 1;
        _portDecoder = new PortDecoder_Scorpion256(_context);
    }

    void TearDown() override
    {
        delete _portDecoder;
        _portDecoder = nullptr;
        delete _context;
        _context = nullptr;
    }
};

namespace
{
    /// Minimal PortDevice stand-in for the WD1793 FDC (exact-address device)
    class MockFdcDevice final : public PortDevice
    {
    public:
        uint8_t portDeviceInMethod(uint16_t port) override
        {
            inPorts.push_back(port);
            return statusByte;
        }

        void portDeviceOutMethod(uint16_t port, uint8_t value) override
        {
            outPorts.emplace_back(port, value);
        }

        uint8_t statusByte = 0x5A;
        std::vector<uint16_t> inPorts;
        std::vector<std::pair<uint16_t, uint8_t>> outPorts;
    };
}

// Mode 2 (#F1/#F3/#F9/#FB) is a completely separate address family from
// Beta128 on Scorpion (never matches TryBeta128MirrorPort at all) - every
// OUT (n),A-style write (raw port = value<<8 | lowByte) must still reach
// its own channel
TEST_F(PortDecoder_Scorpion256_Soundrive_Test, ModeTwoQuadPortsReachHandlerViaOutNStyleAddress)
{
    Covox covox(_context);
    ASSERT_TRUE(_portDecoder->RegisterSelfDecodingDevice(&covox));

    const uint16_t lowBytes[] = { 0x00F1, 0x00F3, 0x00F9, 0x00FB };
    for (size_t i = 0; i < std::size(lowBytes); i++)
    {
        // Simulate OUT (n),A: upper byte carries the written value, exactly
        // what balldreams2.sna / SQT.TRD actually execute
        uint16_t rawPort = static_cast<uint16_t>(((0x10 + i) << 8) | lowBytes[i]);
        _portDecoder->DecodePortOut(rawPort, static_cast<uint8_t>(0x10 + i), 0x0000);
    }

    uint8_t latches[4];
    covox.TTDSaveState(latches);
    for (size_t i = 0; i < std::size(lowBytes); i++)
    {
        EXPECT_EQ(latches[i], static_cast<uint8_t>(0x10 + i))
            << "mode-2 port index " << i << " did not reach its own channel";
    }

    _portDecoder->UnregisterSelfDecodingDevice(&covox);
}

// Mode 1 (#0F/#1F/#4F/#5F): #1F/#5F alias Scorpion's Beta128 mirror decode
// unconditionally (TryBeta128MirrorPort matches them regardless of
// wideDecode); #0F/#4F never match it at all. TR-DOS/session-active must
// keep Beta128's exclusive claim on #1F; TR-DOS inactive must route every
// one of the four to Covox, each keeping its own channel identity
TEST_F(PortDecoder_Scorpion256_Soundrive_Test, ModeOnePortsRespectBeta128Precedence)
{
    MockFdcDevice fdc;
    for (uint16_t port : { 0x001F, 0x003F, 0x005F, 0x007F, 0x00FF })
        _portDecoder->RegisterPortHandler(port, &fdc);

    Covox covox(_context);
    ASSERT_TRUE(_portDecoder->RegisterSelfDecodingDevice(&covox));

    // TR-DOS session open: Beta128 keeps #1F, SoundDrive must not see it
    _context->emulatorState.flags |= CF_TRDOS;
    _portDecoder->DecodePortOut(0x001F, 0xAA, 0x0000);
    ASSERT_EQ(fdc.outPorts.size(), 1u);
    EXPECT_EQ(fdc.outPorts[0].first, 0x001F);
    uint8_t latches[4];
    covox.TTDSaveState(latches);
    EXPECT_EQ(latches[static_cast<int>(Covox::Channel::LeftB)], 0x80)
        << "Beta128 must win while a TR-DOS session is open";

    // No TR-DOS session, Shadow Monitor unpaged, DOS trigger disarmed:
    // SoundDrive claims all four addresses, Beta128 must not see them
    _context->emulatorState.flags &= ~CF_TRDOS;
    _context->emulatorState.p1FFD &= ~0x02;
    _context->emulatorState.scorpionDosTrigger = false;

    const uint16_t rawPorts[] = { 0x000F, 0x001F, 0x004F, 0x005F };
    for (size_t i = 0; i < std::size(rawPorts); i++)
    {
        _portDecoder->DecodePortOut(rawPorts[i], static_cast<uint8_t>(0x20 + i), 0x0000);
    }

    covox.TTDSaveState(latches);
    for (size_t i = 0; i < std::size(rawPorts); i++)
    {
        EXPECT_EQ(latches[i], static_cast<uint8_t>(0x20 + i))
            << "raw port 0x" << std::hex << rawPorts[i] << " should reach its own channel";
    }
    EXPECT_EQ(fdc.outPorts.size(), 1u) << "Beta128 must not see any SoundDrive write once off the bus";

    _portDecoder->UnregisterSelfDecodingDevice(&covox);
}
