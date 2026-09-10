#include "stdafx.h"
#include "pch.h"

#include "portdecoder_pentagon128_test.h"
#include <common/stringhelper.h>
#include <vector>

/// region <Beta128 FDC port gating helpers>

namespace
{
    /// Minimal PortDevice stand-in for the WD1793 FDC: records every access
    /// and returns a fixed status byte, so tests can observe whether the
    /// decoder forwarded a port access to the disk interface
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

    // Beta128 FDC register ports
    const uint16_t beta128Ports[] = { 0x001F, 0x003F, 0x005F, 0x007F, 0x00FF };

    // Partially-decoded aliases that resolve to FDC registers
    const uint16_t beta128Aliases[] = { 0x0003, 0x0023, 0x01FF };
}

/// endregion </Beta128 FDC port gating helpers>

/// region <SetUp / TearDown>

void PortDecoder_Pentagon128_Test::SetUp()
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    _context = new EmulatorContext(LoggerLevel::LogError);
    _portDecoder = new PortDecoder_Pentagon128(_context);
}

void PortDecoder_Pentagon128_Test::TearDown()
{
    if (_portDecoder != nullptr)
    {
        delete _portDecoder;
        _portDecoder = nullptr;
    }
}

/// endregion </Setup / TearDown>

TEST_F(PortDecoder_Pentagon128_Test, IsPort_7FFD)
{
    // Port: #7FFD
    // Sensitivity: 0xxxxxxx xxxxx10x (A15=0, A2=1, A1=0)
    // The mask includes bit2=1 to avoid conflict with SOUNDRIVE ports F1/F9 which have bit2=0
    static const uint16_t bit15_inv = 0b1000'0000'0000'0000;
    static const uint16_t bit2_set  = 0b0000'0000'0000'0100;
    static const uint16_t bit1_inv  = 0b0000'0000'0000'0010;
    static char message[256];

    for (int i = 0; i <= 0xFFFF; i++)
    {
        uint16_t port = i & 0xFFFF;
        // Reference: A15=0 AND A2=1 AND A1=0
        bool referenceIs_7FFD = ((~port & bit15_inv) && (port & bit2_set) && (~port & bit1_inv));
        bool is_7FFD = _portDecoder->IsPort_7FFD(port);

        if (referenceIs_7FFD != is_7FFD)
        {
            snprintf(message, sizeof message, "port: #%04X. Expected: %d, returned: %d", port, referenceIs_7FFD, is_7FFD);
            FAIL() << message << std::endl;
        }

#ifdef _DEBUG
        if (is_7FFD)
        {
            snprintf(message, sizeof message, "port: #%04X. Expected: %d, returned: %d", port, referenceIs_7FFD, is_7FFD);
            std::cout << message << std::endl;
        }
#endif // _DEBUG
    }
}

// Verify SOUNDRIVE ports F1/F9 are NOT decoded as 7FFD (they have bit2=0)
TEST_F(PortDecoder_Pentagon128_Test, SoundrivePortsNotDecodedAs7FFD)
{
    // SOUNDRIVE v1.05 ports: F1, F3, F9, FB
    // F1 and F9 have bit2=0, so they must NOT match 7FFD decode
    // F3 and FB have bit2=1, but they have bit1=1 so they also don't match
    const uint16_t soundrivePorts[] = { 0x00F1, 0x00F3, 0x00F9, 0x00FB };

    for (uint16_t port : soundrivePorts)
    {
        bool is7FFD = _portDecoder->IsPort_7FFD(port);
        EXPECT_FALSE(is7FFD) << "SOUNDRIVE port 0x" << std::hex << port
                             << " should NOT be decoded as 7FFD";

        // Also verify decodePort() returns COVOX (0x00FB), not 7FFD
        uint16_t decoded = _portDecoder->decodePort(port);
        EXPECT_EQ(decoded, 0x00FB) << "SOUNDRIVE port 0x" << std::hex << port
                                    << " should decode to 0x00FB (COVOX), not 0x" << decoded;
    }
}

// Verify 7FFD still works with various high byte combinations
TEST_F(PortDecoder_Pentagon128_Test, Port7FFD_VariousAddresses)
{
    // These should all decode as 7FFD: A15=0, A2=1, A1=0
    const uint16_t validAddresses[] = { 0x7FFD, 0x3FFD, 0x1FFD, 0x00FD, 0x04FD };

    for (uint16_t port : validAddresses)
    {
        bool is7FFD = _portDecoder->IsPort_7FFD(port);
        EXPECT_TRUE(is7FFD) << "Port 0x" << std::hex << port
                            << " should be decoded as 7FFD";

        uint16_t decoded = _portDecoder->decodePort(port);
        EXPECT_EQ(decoded, 0x7FFD) << "Port 0x" << std::hex << port
                                   << " should decode to 0x7FFD, not 0x" << decoded;
    }
}

TEST_F(PortDecoder_Pentagon128_Test, DecodePort_FF)
{
    for (int i = 0; i <= 0xFFFF; i++)
    {
        uint16_t port = i & 0xFFFF;

        if ((i & 0x00FF) == 0x00FF)
        {
            uint16_t result = _portDecoder->decodePort(i);

            EXPECT_EQ(result, 0x00FF) << StringHelper::Format("Expected 0x00FF, found 0x%04X for i: %d", result, i);
        }
    }
}

/// region <Beta128 FDC port gating (TR-DOS visibility)>

TEST_F(PortDecoder_Pentagon128_Test, DecodePortIn_Beta128Ports_TRDosOff_NotDecodedByFdc)
{
    // TR-DOS ROM paged out: the FDC does not decode its register ports.
    // The read must return 0xFF with the decoded flag cleared so that
    // Z80::in() serves the floating bus (beam-synchronous VRAM byte) -
    // the mechanism beam-locked effects sync on via IN A,($FF).
    _context->emulatorState.flags &= ~CF_TRDOS;

    MockFdcDevice fdc;
    for (uint16_t port : beta128Ports)
    {
        _portDecoder->RegisterPortHandler(port, &fdc);
    }

    for (uint16_t port : beta128Ports)
    {
        fdc.inPorts.clear();
        uint8_t result = _portDecoder->DecodePortIn(port, 0x0000);

        EXPECT_EQ(result, 0xFF) << StringHelper::Format("Port #%02X: FDC is off the bus when TR-DOS is off", port);
        EXPECT_FALSE(_portDecoder->WasLastPortDecoded())
            << StringHelper::Format("Port #%02X: must stay undecoded so the floating bus applies", port);
        EXPECT_TRUE(fdc.inPorts.empty()) << StringHelper::Format("Port #%02X: FDC must not be consulted", port);
    }
}

TEST_F(PortDecoder_Pentagon128_Test, DecodePortIn_Beta128Ports_TRDosOn_ServedByFdc)
{
    // TR-DOS ROM paged in (executing from $3Dxx): the FDC owns its ports again
    _context->emulatorState.flags |= CF_TRDOS;

    MockFdcDevice fdc;
    fdc.statusByte = 0x5A;
    for (uint16_t port : beta128Ports)
    {
        _portDecoder->RegisterPortHandler(port, &fdc);
    }

    for (uint16_t port : beta128Ports)
    {
        fdc.inPorts.clear();
        uint8_t result = _portDecoder->DecodePortIn(port, 0x0000);

        EXPECT_EQ(result, 0x5A) << StringHelper::Format("Port #%02X: FDC status must be served", port);
        EXPECT_TRUE(_portDecoder->WasLastPortDecoded())
            << StringHelper::Format("Port #%02X: port is decoded - no floating bus override", port);
        ASSERT_EQ(fdc.inPorts.size(), 1u) << StringHelper::Format("Port #%02X: exactly one FDC access expected", port);
        EXPECT_EQ(fdc.inPorts[0], port);
    }
}

TEST_F(PortDecoder_Pentagon128_Test, DecodePortIn_Beta128Aliases_TRDosOff_NotDecoded)
{
    // Partial decoding: $0003/$0023 resolve to #1F, $01FF resolves to #FF.
    // The gate must also neutralize these aliases.
    _context->emulatorState.flags &= ~CF_TRDOS;

    MockFdcDevice fdc;
    for (uint16_t port : beta128Ports)
    {
        _portDecoder->RegisterPortHandler(port, &fdc);
    }

    for (uint16_t alias : beta128Aliases)
    {
        fdc.inPorts.clear();
        uint8_t result = _portDecoder->DecodePortIn(alias, 0x0000);

        EXPECT_EQ(result, 0xFF) << StringHelper::Format("Alias #%04X must stay undecoded with TR-DOS off", alias);
        EXPECT_FALSE(_portDecoder->WasLastPortDecoded());
        EXPECT_TRUE(fdc.inPorts.empty());
    }
}

TEST_F(PortDecoder_Pentagon128_Test, DecodePortOut_Beta128Ports_TRDosOff_WritesIgnored)
{
    // With the FDC off the bus its registers cannot be written -
    // hardware would silently ignore the OUT (e.g. Beta128 control $FF)
    _context->emulatorState.flags &= ~CF_TRDOS;

    MockFdcDevice fdc;
    for (uint16_t port : beta128Ports)
    {
        _portDecoder->RegisterPortHandler(port, &fdc);
    }

    for (uint16_t port : beta128Ports)
    {
        _portDecoder->DecodePortOut(port, 0x02, 0x0000); // 0x02 = drive select
    }

    EXPECT_TRUE(fdc.outPorts.empty()) << "FDC writes must be dropped when TR-DOS is off";
}

TEST_F(PortDecoder_Pentagon128_Test, DecodePortOut_Beta128Ports_TRDosOn_WritesForwarded)
{
    _context->emulatorState.flags |= CF_TRDOS;

    MockFdcDevice fdc;
    for (uint16_t port : beta128Ports)
    {
        _portDecoder->RegisterPortHandler(port, &fdc);
    }

    for (uint16_t port : beta128Ports)
    {
        _portDecoder->DecodePortOut(port, 0x02, 0x0000);
    }

    ASSERT_EQ(fdc.outPorts.size(), 5u);
    for (size_t i = 0; i < 5; i++)
    {
        EXPECT_EQ(fdc.outPorts[i].first, beta128Ports[i]);
        EXPECT_EQ(fdc.outPorts[i].second, 0x02);
    }
}

TEST_F(PortDecoder_Pentagon128_Test, DecodePortIn_7FFD_UnaffectedByTrdosGate)
{
    // The gate must be scoped to the Beta128 port set only
    _context->emulatorState.flags &= ~CF_TRDOS;

    uint8_t result = _portDecoder->DecodePortIn(0x7FFD, 0x0000);

    EXPECT_EQ(result, _context->emulatorState.p7FFD);
    EXPECT_TRUE(_portDecoder->WasLastPortDecoded());
}

TEST_F(PortDecoder_Pentagon128_Test, DecodePort_Beta128AddressMatching_StateIndependent)
{
    // The gate lives in IN/OUT dispatch, not in address matching:
    // decodePort() must keep resolving FDC addresses regardless of CF_TRDOS
    EXPECT_EQ(_portDecoder->decodePort(0x00FF), 0x00FF);
    EXPECT_EQ(_portDecoder->decodePort(0x01FF), 0x00FF);
    EXPECT_EQ(_portDecoder->decodePort(0x001F), 0x001F);
    EXPECT_EQ(_portDecoder->decodePort(0x0003), 0x001F);

    _context->emulatorState.flags |= CF_TRDOS;
    EXPECT_EQ(_portDecoder->decodePort(0x00FF), 0x00FF);
    EXPECT_EQ(_portDecoder->decodePort(0x0003), 0x001F);
}

/// endregion </Beta128 FDC port gating (TR-DOS visibility)>