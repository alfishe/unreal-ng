#include "stdafx.h"
#include "pch.h"

#include <common/stringhelper.h>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/ports/models/portdecoder_pentagon128.h"
#include "emulator/ports/models/portdecoder_pentagon1024.h"
#include "emulator/ports/models/portdecoder_spectrum128.h"
#include "emulator/ports/models/portdecoder_atm710.h"
#include "emulator/ports/models/portdecoder_atm3.h"

/// region <Test doubles>

namespace
{
    /// Minimal full-decode bus-card stand-in (ZXM-MoonSound shape): a low-byte
    /// CPLD decode with a stateful status register, recording every access so
    /// tests can see exactly which cycles the card observed
    class MockFullDecodeCard final : public PortDevice
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

        /// FM status reads are claimed by the real card; wave data only once
        /// OPL4 NEW2 is armed - the mock mirrors the C4 half of that rule
        bool portDeviceClaimsRead(uint16_t port) override
        {
            return (port & 0xFF) == 0xC4;
        }

        uint8_t statusByte = 0x5A;
        std::vector<uint16_t> inPorts;
        std::vector<std::pair<uint16_t, uint8_t>> outPorts;
    };

    /// Generic registered-device stand-in (AY / FDC): records every access
    class MockPortDevice final : public PortDevice
    {
    public:
        uint8_t portDeviceInMethod(uint16_t port) override
        {
            inPorts.push_back(port);
            return 0xA5;
        }

        void portDeviceOutMethod(uint16_t port, uint8_t value) override
        {
            outPorts.emplace_back(port, value);
        }

        std::vector<uint16_t> inPorts;
        std::vector<std::pair<uint16_t, uint8_t>> outPorts;
    };

    // ZXM-MoonSound card ports as registered by SoundManager (low bytes)
    const uint8_t moonsoundLowPorts[] = { 0xC4, 0xC5, 0xC6, 0xC7, 0x7E, 0x7F };

    void RegisterMoonSoundClaims(PortDecoder* decoder, PortDevice* card)
    {
        for (uint8_t low : moonsoundLowPorts)
        {
            ASSERT_TRUE(decoder->RegisterFullDecodeLowBytePort(low, card))
                << StringHelper::Format("Claim for low byte #%02X must be accepted", low);
        }
    }

    /// Emulate the Z80 I/O funnel order for an OUT: observer tap first, then
    /// the model decode - the contract the claim override is built on
    void FunnelOut(PortDecoder* decoder, uint16_t port, uint8_t value)
    {
        decoder->NotifyFullDecodeOut(port, value);
        decoder->DecodePortOut(port, value, 0x0000);
    }

    /// Emulate the Z80 I/O funnel order for an IN: observer tap first (its
    /// value is cached), then the model decode whose return value the guest sees
    uint8_t FunnelIn(PortDecoder* decoder, uint16_t port)
    {
        bool handled = false;
        bool claimsBus = false;
        uint8_t observerValue = decoder->NotifyFullDecodeIn(port, handled, claimsBus);

        uint8_t result = decoder->DecodePortIn(port, 0x0000);

        // Same arbitration as Z80::in(): an undecoded port or a claimed port
        // is driven by the observer's value
        if (handled && (claimsBus || !decoder->WasLastPortDecoded()))
            result = observerValue;

        return result;
    }
}

/// endregion </Test doubles>

/// region <Clash analysis API>

class FullDecodeClashAnalysis_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    PortDecoder_Pentagon128* _portDecoder = nullptr;
    MockFullDecodeCard _card;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _portDecoder = new PortDecoder_Pentagon128(_context);
    }

    void TearDown() override
    {
        delete _portDecoder;
        _portDecoder = nullptr;
    }

    /// All (rulePort, claimLowByte) overlap pairs the six MoonSound claims
    /// produce against the Pentagon decode table
    static std::set<std::pair<uint16_t, uint8_t>> ExpectedPentagonClashes()
    {
        return {
            { 0xFFFD, 0xC4 }, { 0xFFFD, 0xC5 },   // AY #FFFD (A1=0 aliases)
            { 0xBFFD, 0xC4 }, { 0xBFFD, 0xC5 },   // AY #BFFD
            { 0x7FFD, 0xC4 }, { 0x7FFD, 0xC5 },   // Paging #7FFD (A2=1, A1=0)
            { 0x00FE, 0xC4 }, { 0x00FE, 0xC6 }, { 0x00FE, 0x7E },  // ULA #FE (even)
            { 0x00FF, 0xC7 },                     // Beta128 system port (A7=1,A1=1,A0=1)
        };
    }
};

TEST_F(FullDecodeClashAnalysis_Test, NoClaims_NoClashes)
{
    EXPECT_TRUE(_portDecoder->FindFullDecodeClashes().empty());
}

TEST_F(FullDecodeClashAnalysis_Test, MoonSoundClaims_ReportExactOverlapSet)
{
    RegisterMoonSoundClaims(_portDecoder, &_card);

    std::set<std::pair<uint16_t, uint8_t>> actual;
    for (const PortDecodeClash& clash : _portDecoder->FindFullDecodeClashes())
        actual.insert({ clash.rulePort, clash.claimLowByte });

    EXPECT_EQ(actual, ExpectedPentagonClashes());
}

TEST_F(FullDecodeClashAnalysis_Test, MoonSoundClaims_ClashFields)
{
    RegisterMoonSoundClaims(_portDecoder, &_card);

    for (const PortDecodeClash& clash : _portDecoder->FindFullDecodeClashes())
    {
        // The sample address must satisfy BOTH the rule decode and the claim
        EXPECT_EQ(clash.sampleAddress & clash.ruleMask, clash.ruleMatch)
            << StringHelper::Format("Sample #%04X must hit rule %04X/%04X", clash.sampleAddress,
                                    clash.ruleMask, clash.ruleMatch);
        EXPECT_EQ(clash.sampleAddress & 0xFF, clash.claimLowByte);
        EXPECT_EQ(clash.device, &_card);

        // Only the Beta-128 #FF rule is session-arbitrated (R6)
        EXPECT_EQ(clash.fdcProtected, clash.rulePort == 0x00FF);
    }
}

TEST_F(FullDecodeClashAnalysis_Test, MoonSoundClaims_MinimalSeparatingMasks)
{
    RegisterMoonSoundClaims(_portDecoder, &_card);

    std::map<uint16_t, PortDecodeRuleOverride> byPort;
    for (const PortDecodeRuleOverride& resolution : _portDecoder->FindFullDecodeRuleResolutions())
        byPort.emplace(resolution.rulePort, resolution);

    // Rules without clashes are not reported at all (#FB COVOX decode keeps
    // its whole low-byte space)
    EXPECT_EQ(byPort.count(0x00FB), 0u);

    ASSERT_EQ(byPort.count(0x7FFD), 1u);
    const PortDecodeRuleOverride& paging = byPort.at(0x7FFD);
    EXPECT_EQ(paging.clashingClaims, (std::vector<uint8_t>{ 0xC4, 0xC5 }));
    EXPECT_EQ(paging.separatingMask, 0x0008);  // +2A/+3-style A3 refinement: 0x8006|0x08 / 0x0004|0x08
    EXPECT_EQ(paging.separatingMatch, 0x0008);
    EXPECT_EQ(paging.separatingBitCount, 1);
    EXPECT_FALSE(paging.precedenceRequired);

    // #FFFD/#BFFD separate with the same single bit (canonical low byte #FD)
    ASSERT_EQ(byPort.count(0xFFFD), 1u);
    EXPECT_EQ(byPort.at(0xFFFD).separatingMask, 0x0008);
    EXPECT_EQ(byPort.at(0xFFFD).separatingBitCount, 1);
    ASSERT_EQ(byPort.count(0xBFFD), 1u);
    EXPECT_EQ(byPort.at(0xBFFD).separatingMask, 0x0008);
    EXPECT_EQ(byPort.at(0xBFFD).separatingBitCount, 1);

    // ULA #FE vs {C4, C6, 7E}: no single bit separates all three - the
    // minimal tightening is the two-bit set {A3, A7}
    ASSERT_EQ(byPort.count(0x00FE), 1u);
    const PortDecodeRuleOverride& ula = byPort.at(0x00FE);
    EXPECT_EQ(ula.clashingClaims, (std::vector<uint8_t>({ 0x7E, 0xC4, 0xC6 })));
    EXPECT_EQ(ula.separatingMask, 0x0088);
    EXPECT_EQ(ula.separatingMatch, 0x0088);
    EXPECT_EQ(ula.separatingBitCount, 2);
    EXPECT_FALSE(ula.precedenceRequired);

    // The Beta-128 #FF rule resolves through its TR-DOS session gate, never
    // through mask tightening
    ASSERT_EQ(byPort.count(0x00FF), 1u);
    const PortDecodeRuleOverride& fdc = byPort.at(0x00FF);
    EXPECT_EQ(fdc.clashingClaims, (std::vector<uint8_t>{ 0xC7 }));
    EXPECT_EQ(fdc.separatingBitCount, 0);
    EXPECT_TRUE(fdc.precedenceRequired);
}

TEST_F(FullDecodeClashAnalysis_Test, UnregisterRemovesClaims)
{
    RegisterMoonSoundClaims(_portDecoder, &_card);
    EXPECT_FALSE(_portDecoder->FindFullDecodeClashes().empty());

    for (uint8_t low : moonsoundLowPorts)
        _portDecoder->UnregisterFullDecodeLowBytePort(low, &_card);

    EXPECT_TRUE(_portDecoder->FindFullDecodeClashes().empty());
    EXPECT_FALSE(_portDecoder->IsLowByteClaimedByFullDecodeDevice(0x04C4));
}

/// endregion </Clash analysis API>

/// region <Claim override: Pentagon behavior>

class FullDecodeClaim_Pentagon_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    PortDecoder_Pentagon128* _portDecoder = nullptr;
    MockFullDecodeCard _card;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _portDecoder = new PortDecoder_Pentagon128(_context);
        _context->emulatorState.flags &= ~CF_TRDOS;
    }

    void TearDown() override
    {
        delete _portDecoder;
        _portDecoder = nullptr;
    }
};

// Without a card the loose Pentagon decode stays exactly as it was: the
// demo's dirty-high-byte FM write still resolves to the paging register
// (this is the corruption the claim override exists to prevent)
TEST_F(FullDecodeClaim_Pentagon_Test, NoClaim_LooseDecodePinned)
{
    EXPECT_EQ(_portDecoder->decodePort(0x04C4), 0x7FFD);
    EXPECT_EQ(_portDecoder->decodePort(0x81C5), 0xBFFD);
    EXPECT_EQ(_portDecoder->decodePort(0x04C6), 0x00FE);
}

// MFM Music sample 2 smoking gun: `out (#C4),a` with the register number in
// the high byte must no longer double-deliver into #7FFD paging once the
// card claims the low byte - the RAM bank at #C000 stays mapped
TEST_F(FullDecodeClaim_Pentagon_Test, Claimed_FMRegisterWrite_DoesNotPage)
{
    RegisterMoonSoundClaims(_portDecoder, &_card);
    ASSERT_EQ(_context->emulatorState.p7FFD, 0);

    FunnelOut(_portDecoder, 0x04C4, 0x04);

    EXPECT_EQ(_context->emulatorState.p7FFD, 0) << "Paging register must not see the card cycle";
    ASSERT_EQ(_card.outPorts.size(), 1u);
    EXPECT_EQ(_card.outPorts[0].first, 0x04C4);
    EXPECT_EQ(_card.outPorts[0].second, 0x04);
}

// The companion `out (#C5),a` data write must not reach the AY register
// selected through the dirty #BFFD alias
TEST_F(FullDecodeClaim_Pentagon_Test, Claimed_FMDataWrite_DoesNotTouchAY)
{
    MockPortDevice ay;
    _portDecoder->RegisterPortHandler(0xFFFD, &ay);
    _portDecoder->RegisterPortHandler(0xBFFD, &ay);

    RegisterMoonSoundClaims(_portDecoder, &_card);
    FunnelOut(_portDecoder, 0x81C5, 0x81);

    EXPECT_TRUE(ay.outPorts.empty()) << "AY must not see the card cycle";
    ASSERT_EQ(_card.outPorts.size(), 1u);
    EXPECT_EQ(_card.outPorts[0].first, 0x81C5);

    // Baseline without the claim: the same dirty write double-delivers into
    // the AY (pins the bug the override fixes)
    _portDecoder->UnregisterFullDecodeLowBytePort(0xC5, &_card);
    FunnelOut(_portDecoder, 0x81C5, 0x81);
    ASSERT_EQ(ay.outPorts.size(), 1u);
    EXPECT_EQ(ay.outPorts[0].first, 0xBFFD);
}

// Canonical clean writes keep working while the card is attached: the claim
// spans only the card's own low bytes
TEST_F(FullDecodeClaim_Pentagon_Test, Claimed_CanonicalAYWriteStillWorks)
{
    MockPortDevice ay;
    _portDecoder->RegisterPortHandler(0xFFFD, &ay);
    _portDecoder->RegisterPortHandler(0xBFFD, &ay);

    RegisterMoonSoundClaims(_portDecoder, &_card);
    FunnelOut(_portDecoder, 0xFFFD, 0x07);
    FunnelOut(_portDecoder, 0xBFFD, 0x38);

    ASSERT_EQ(ay.outPorts.size(), 2u);
    EXPECT_EQ(ay.outPorts[0], (std::pair<uint16_t, uint8_t>(0xFFFD, 0x07)));
    EXPECT_EQ(ay.outPorts[1], (std::pair<uint16_t, uint8_t>(0xBFFD, 0x38)));
}

TEST_F(FullDecodeClaim_Pentagon_Test, Claimed_Canonical7FFDReadStillWorks)
{
    RegisterMoonSoundClaims(_portDecoder, &_card);

    uint8_t result = _portDecoder->DecodePortIn(0x7FFD, 0x0000);

    EXPECT_EQ(result, _context->emulatorState.p7FFD);
    EXPECT_TRUE(_portDecoder->WasLastPortDecoded());
}

// The FM status poll (`in a,(#C4)`, dirty high byte) must return the card's
// status register, read exactly once - not the keyboard line the weak #FE
// arm would serve for the even mirror
TEST_F(FullDecodeClaim_Pentagon_Test, Claimed_FMStatusRead_ServedByCardOnce)
{
    _card.statusByte = 0x5A;
    RegisterMoonSoundClaims(_portDecoder, &_card);

    uint8_t result = FunnelIn(_portDecoder, 0x04C4);

    EXPECT_EQ(result, 0x5A) << "The card status register drives the bus";
    EXPECT_TRUE(_portDecoder->WasLastPortDecoded());
    ASSERT_EQ(_card.inPorts.size(), 1u) << "Stateful status register: single read per cycle";
    EXPECT_EQ(_card.inPorts[0], 0x04C4);
}

// Wave ports #7E/#7F never clashed with paging or AY (A1=1) - the override
// must leave them claimed and observable while the ULA #FE arm stands down
// for the even #7E alias
TEST_F(FullDecodeClaim_Pentagon_Test, Claimed_WaveWrite_NoMotherboardSideEffects)
{
    RegisterMoonSoundClaims(_portDecoder, &_card);
    _context->emulatorState.border_attr = 0;

    FunnelOut(_portDecoder, 0x047E, 0x35);

    EXPECT_EQ(_context->emulatorState.border_attr, 0) << "ULA #FE arm must stand down";
    ASSERT_EQ(_card.outPorts.size(), 1u);
    EXPECT_EQ(_card.outPorts[0].first, 0x047E);
}

// Beta-128 session precedence (R6): with TR-DOS paged in, the FDC keeps the
// #FF system register even when the card claims low byte #C7 - both devices
// see the shared-bus cycle, but the session gate owns the dispatch
TEST_F(FullDecodeClaim_Pentagon_Test, Claimed_TrdosSession_FdcKeepsSystemPort)
{
    MockPortDevice fdc;
    for (uint16_t port : { 0x001F, 0x003F, 0x005F, 0x007F, 0x00FF })
        _portDecoder->RegisterPortHandler(port, &fdc);

    RegisterMoonSoundClaims(_portDecoder, &_card);
    _context->emulatorState.flags |= CF_TRDOS;

    FunnelOut(_portDecoder, 0x04C7, 0x02);

    ASSERT_EQ(fdc.outPorts.size(), 1u) << "FDC keeps the session-owned port";
    EXPECT_EQ(fdc.outPorts[0].first, 0x00FF);
    ASSERT_EQ(_card.outPorts.size(), 1u) << "The card still observes the shared-bus cycle";
    EXPECT_EQ(_card.outPorts[0].first, 0x04C7);
}

// FDC off the bus (no TR-DOS session): the #7F wave-data alias must neither
// reach the FDC nor get a motherboard decode - the card owns the cycle alone
TEST_F(FullDecodeClaim_Pentagon_Test, Claimed_NoTrdos_WaveDataSkipsFdc)
{
    MockPortDevice fdc;
    for (uint16_t port : { 0x001F, 0x003F, 0x005F, 0x007F, 0x00FF })
        _portDecoder->RegisterPortHandler(port, &fdc);

    RegisterMoonSoundClaims(_portDecoder, &_card);

    FunnelOut(_portDecoder, 0x817F, 0x9E);

    EXPECT_TRUE(fdc.outPorts.empty()) << "FDC is off the bus without a TR-DOS session";
    ASSERT_EQ(_card.outPorts.size(), 1u);
    EXPECT_EQ(_card.outPorts[0].first, 0x817F);
}

/// endregion </Claim override: Pentagon behavior>

/// region <Claim override: if-chain decoder path>

class FullDecodeClaim_Spectrum128_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    PortDecoder_Spectrum128* _portDecoder = nullptr;
    MockFullDecodeCard _card;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _portDecoder = new PortDecoder_Spectrum128(_context);
        _context->emulatorState.flags &= ~CF_TRDOS;
    }

    void TearDown() override
    {
        delete _portDecoder;
        _portDecoder = nullptr;
    }
};

TEST_F(FullDecodeClaim_Spectrum128_Test, Claimed_DirtyWrites_StandDown)
{
    MockPortDevice ay;
    _portDecoder->RegisterPortHandler(0xFFFD, &ay);
    _portDecoder->RegisterPortHandler(0xBFFD, &ay);

    RegisterMoonSoundClaims(_portDecoder, &_card);
    ASSERT_EQ(_context->emulatorState.p7FFD, 0);

    // 128K decode is even looser (A15=0, A1=0): both dirty FM writes alias
    // into paging / AY without the claim
    FunnelOut(_portDecoder, 0x04C4, 0x04);
    FunnelOut(_portDecoder, 0x81C5, 0x81);

    EXPECT_EQ(_context->emulatorState.p7FFD, 0);
    EXPECT_TRUE(ay.outPorts.empty());
    ASSERT_EQ(_card.outPorts.size(), 2u);
    EXPECT_EQ(_card.outPorts[0].first, 0x04C4);
    EXPECT_EQ(_card.outPorts[1].first, 0x81C5);
}

TEST_F(FullDecodeClaim_Spectrum128_Test, Claimed_CanonicalWritesUnaffected)
{
    MockPortDevice ay;
    _portDecoder->RegisterPortHandler(0xFFFD, &ay);
    _portDecoder->RegisterPortHandler(0xBFFD, &ay);

    RegisterMoonSoundClaims(_portDecoder, &_card);
    FunnelOut(_portDecoder, 0xBFFD, 0x38);

    ASSERT_EQ(ay.outPorts.size(), 1u);
    EXPECT_EQ(ay.outPorts[0], (std::pair<uint16_t, uint8_t>(0xBFFD, 0x38)));
}

TEST_F(FullDecodeClaim_Spectrum128_Test, Claimed_FMStatusRead_ServedByCard)
{
    _card.statusByte = 0x3C;
    RegisterMoonSoundClaims(_portDecoder, &_card);

    uint8_t result = FunnelIn(_portDecoder, 0x04C4);

    EXPECT_EQ(result, 0x3C);
    ASSERT_EQ(_card.inPorts.size(), 1u);
}

/// endregion </Claim override: if-chain decoder path>

/// region <Claim override: ATM710/ATM3 (per-model regression + no-conflict coverage)>
//
// ATM's own partial decode is A0-only for #FE and A15/A2/A1-only for the
// #7FFD/#BFFD/#FFFD family, so it double-decodes MoonSound's own ports:
//   #C4/#C6 (A0=0, bit0)      -> also matches #FE (border/beeper)
//   #C4/#C5 (A1=0, bit1)      -> also matches #7FFD (A15=0) / #BFFD (A15=1,A14=0)
//                                 / #FFFD (A15=1,A14=1), depending on the dirty
//                                 high byte the guest driver leaves on the bus
// #C6/#C7 have bit1=1 and so never alias into the 7FFD/BFFD/FFFD family; #C7
// additionally has bit0=1 and so never aliases into #FE either - only #C4 is
// exposed to both clashes at once. The claim override must stand the
// motherboard decode down for exactly these four low bytes (plus the
// non-clashing #7E/#7F wave ports) and leave everything else - including the
// ATM-specific #BF/#BE/#57 exact decodes - untouched.

class FullDecodeClaim_ATM710_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;
    PortDecoder_ATM710* _portDecoder = nullptr;
    MockFullDecodeCard _card;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);

        // Memory must be attached before the decoder: PortDecoder caches the
        // pointer in its constructor (mirrors portdecoder_atm710_test.cpp)
        _memory = new Memory(_context);
        _context->pMemory = _memory;

        _portDecoder = new PortDecoder_ATM710(_context);
        _context->config.mem_model = MM_ATM710;
        _context->pPortDecoder = _portDecoder;
        _context->emulatorState.flags &= ~CF_TRDOS;
    }

    void TearDown() override
    {
        delete _portDecoder;
        _portDecoder = nullptr;
        delete _memory;
        _memory = nullptr;
        _context->pMemory = nullptr;
        _context->pPortDecoder = nullptr;
        delete _context;
        _context = nullptr;
    }
};

// Baseline (MoonSound not attached / feature off): the known ATM decode quirk
// stays exactly as it was pre-fix. This pins "no regression when MoonSound is
// off": the override check only ever intervenes for a registered card low
// byte, so with none registered the motherboard decode predicates - and thus
// their outcome - are completely unchanged by the fix.
//
// The #C4/#C6 -> #FE alias is asserted at the predicate level rather than by
// executing the OUT: Default_Port_FE_Out reaches the Z80/tape/beeper/screen
// objects a bare PortDecoder unit test does not wire up (out of scope for a
// port-decode-only fixture; the #FE side effects themselves are exercised by
// the dedicated screen/beeper test suites).
TEST_F(FullDecodeClaim_ATM710_Test, NoClaim_LegacyDoubleDecodeUnchanged)
{
    EXPECT_TRUE(_portDecoder->IsPort_FE(0x04C4)) << "Pre-fix behavior: #C4 still decodes as #FE (A0=0)";
    EXPECT_TRUE(_portDecoder->IsPort_FE(0x04C6)) << "Pre-fix behavior: #C6 still decodes as #FE (A0=0)";

    ASSERT_EQ(_context->emulatorState.p7FFD, 0);
    FunnelOut(_portDecoder, 0x04C5, 0x81);
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x81) << "Pre-fix behavior: #C5 (A15=0) still pages via #7FFD";
}

TEST_F(FullDecodeClaim_ATM710_Test, Claimed_AddrWrite_DoesNotHitBorder)
{
    RegisterMoonSoundClaims(_portDecoder, &_card);
    _context->emulatorState.border_attr = 0;

    FunnelOut(_portDecoder, 0x04C4, 0x02);
    FunnelOut(_portDecoder, 0x04C6, 0x03);

    EXPECT_EQ(_context->emulatorState.border_attr, 0) << "ULA #FE arm must stand down for #C4/#C6";
    ASSERT_EQ(_card.outPorts.size(), 2u);
    EXPECT_EQ(_card.outPorts[0].first, 0x04C4);
    EXPECT_EQ(_card.outPorts[1].first, 0x04C6);
}

TEST_F(FullDecodeClaim_ATM710_Test, Claimed_DataWrite_DoesNotPage)
{
    RegisterMoonSoundClaims(_portDecoder, &_card);
    ASSERT_EQ(_context->emulatorState.p7FFD, 0);

    // #C5 with A15=0 is the dirty write that would otherwise page RAM/ROM
    // (bit1=0 matches #7FFD's mask); #C7 never collided with paging on ATM
    // (bit1=1), so this half only proves the card still receives its own
    // cycle rather than pinning a regression
    FunnelOut(_portDecoder, 0x04C5, 0x81);
    FunnelOut(_portDecoder, 0x04C7, 0x82);

    EXPECT_EQ(_context->emulatorState.p7FFD, 0) << "#7FFD paging must not see the card cycle";
    ASSERT_EQ(_card.outPorts.size(), 2u);
    EXPECT_EQ(_card.outPorts[0].first, 0x04C5);
    EXPECT_EQ(_card.outPorts[1].first, 0x04C7);
}

TEST_F(FullDecodeClaim_ATM710_Test, Claimed_CanonicalPortsStillWork)
{
    RegisterMoonSoundClaims(_portDecoder, &_card);

    FunnelOut(_portDecoder, 0x7FFD, 0x05);
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x05);

    // #FE (canonical, low byte != any of the six claimed bytes) must stay
    // outside the claim - checked at the predicate level, see
    // NoClaim_LegacyDoubleDecodeUnchanged for why the OUT itself is not run
    EXPECT_FALSE(_portDecoder->IsLowByteClaimedByFullDecodeDevice(0x00FE));
}

TEST_F(FullDecodeClaim_ATM710_Test, Claimed_FMStatusRead_ServedByCard)
{
    _card.statusByte = 0x77;
    RegisterMoonSoundClaims(_portDecoder, &_card);

    uint8_t result = FunnelIn(_portDecoder, 0x04C4);

    EXPECT_EQ(result, 0x77);
    ASSERT_EQ(_card.inPorts.size(), 1u);
    EXPECT_EQ(_card.inPorts[0], 0x04C4);
}

// Wave ports #7E/#7F alias into ATM's own #FE (even) / Beta128 #7F decode;
// the claim must stand both down without disturbing the FDC's TR-DOS-session
// arbitration for the other Beta128 ports.
TEST_F(FullDecodeClaim_ATM710_Test, Claimed_WaveWrite_NoMotherboardSideEffects)
{
    RegisterMoonSoundClaims(_portDecoder, &_card);
    _context->emulatorState.border_attr = 0;

    FunnelOut(_portDecoder, 0x047E, 0x35);

    EXPECT_EQ(_context->emulatorState.border_attr, 0) << "ULA #FE arm must stand down for #7E";
    ASSERT_EQ(_card.outPorts.size(), 1u);
    EXPECT_EQ(_card.outPorts[0].first, 0x047E);
}

class FullDecodeClaim_ATM3_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;
    PortDecoder_ATM3* _portDecoder = nullptr;
    MockFullDecodeCard _card;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);

        _memory = new Memory(_context);
        _context->pMemory = _memory;

        _portDecoder = new PortDecoder_ATM3(_context);
        _context->config.mem_model = MM_ATM3;
        _context->pPortDecoder = _portDecoder;
        _context->emulatorState.flags &= ~CF_TRDOS;
    }

    void TearDown() override
    {
        delete _portDecoder;
        _portDecoder = nullptr;
        delete _memory;
        _memory = nullptr;
        _context->pMemory = nullptr;
        _context->pPortDecoder = nullptr;
        delete _context;
        _context = nullptr;
    }
};

// ATM3 delegates unhandled ports to PortDecoder_ATM710::DecodePortIn/Out, so
// the same fix must reach it. This is the exact bug report reproduction: a
// MoonSound-equipped ATM3/ATM710 config with the card's ports wired up.
TEST_F(FullDecodeClaim_ATM3_Test, Claimed_DirtyWrites_StandDown)
{
    RegisterMoonSoundClaims(_portDecoder, &_card);
    _context->emulatorState.border_attr = 0;
    ASSERT_EQ(_context->emulatorState.p7FFD, 0);

    FunnelOut(_portDecoder, 0x04C4, 0x02);
    FunnelOut(_portDecoder, 0x04C5, 0x81);

    EXPECT_EQ(_context->emulatorState.border_attr, 0);
    EXPECT_EQ(_context->emulatorState.p7FFD, 0);
    ASSERT_EQ(_card.outPorts.size(), 2u);
}

// ATM3's own exact-decoded ports (#xBF control, #x57 Z-Controller) sit outside
// the C4-C7/7E/7F claim space and must be completely unaffected by a card
// being attached.
TEST_F(FullDecodeClaim_ATM3_Test, Claimed_OwnPortsUnaffected)
{
    RegisterMoonSoundClaims(_portDecoder, &_card);

    FunnelOut(_portDecoder, 0x00BF, 0x01);
    EXPECT_EQ(_context->emulatorState.pBF, 0x01);

    uint8_t sdResult = FunnelIn(_portDecoder, 0x0057);
    EXPECT_EQ(sdResult, 0xFF) << "Z-Controller with no SD image reads back 'no card'";

    EXPECT_TRUE(_card.outPorts.empty());
    EXPECT_TRUE(_card.inPorts.empty());
}

TEST_F(FullDecodeClaim_ATM3_Test, Claimed_FMStatusRead_ServedByCard)
{
    _card.statusByte = 0x99;
    RegisterMoonSoundClaims(_portDecoder, &_card);

    uint8_t result = FunnelIn(_portDecoder, 0x04C4);

    EXPECT_EQ(result, 0x99);
    ASSERT_EQ(_card.inPorts.size(), 1u);
}

/// endregion </Claim override: ATM710/ATM3>

/// region <Claim override: Pentagon1024 (subclass intercepts #7FFD ahead of base)>
//
// Pentagon1024::DecodePortOut intercepts #7FFD itself (6-bit bank extension)
// before delegating to Pentagon128's claim-override-aware dispatch. Without
// its own override check, a MoonSound dirty FM write aliasing into #7FFD
// (same loose A15/A2/A1 decode as plain Pentagon128) would page RAM one class
// earlier than the override could intervene.

class FullDecodeClaim_Pentagon1024_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;
    PortDecoder_Pentagon1024* _portDecoder = nullptr;
    MockFullDecodeCard _card;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);

        // Pentagon1024's #7FFD handler dereferences pMemory unconditionally
        // (Port_7FFD_Out / switchRAMPage), unlike ATM's null-guarded version
        _memory = new Memory(_context);
        _context->pMemory = _memory;

        _portDecoder = new PortDecoder_Pentagon1024(_context);
        _context->config.mem_model = MM_PENTAGON;
        _context->pPortDecoder = _portDecoder;
        _context->emulatorState.flags &= ~CF_TRDOS;
    }

    void TearDown() override
    {
        delete _portDecoder;
        _portDecoder = nullptr;
        delete _memory;
        _memory = nullptr;
        _context->pMemory = nullptr;
        _context->pPortDecoder = nullptr;
        delete _context;
        _context = nullptr;
    }
};

// Baseline (MoonSound off): the loose Pentagon-style #7FFD alias still pages,
// same as Pentagon128's own NoClaim_LooseDecodePinned - no regression from
// adding the override check ahead of the #7FFD/#EFF7 intercepts.
TEST_F(FullDecodeClaim_Pentagon1024_Test, NoClaim_LooseDecodeStillPages)
{
    ASSERT_EQ(_context->emulatorState.p7FFD, 0);
    FunnelOut(_portDecoder, 0x04C4, 0x04);
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x04);
}

TEST_F(FullDecodeClaim_Pentagon1024_Test, Claimed_FMRegisterWrite_DoesNotPage)
{
    RegisterMoonSoundClaims(_portDecoder, &_card);
    ASSERT_EQ(_context->emulatorState.p7FFD, 0);

    FunnelOut(_portDecoder, 0x04C4, 0x04);

    EXPECT_EQ(_context->emulatorState.p7FFD, 0) << "Paging register must not see the card cycle";
    ASSERT_EQ(_card.outPorts.size(), 1u);
    EXPECT_EQ(_card.outPorts[0].first, 0x04C4);
}

// #EFF7 (low byte F7) never clashes with the card's C4-C7/7E/7F claims, so it
// must keep working exactly as before while the card is attached.
TEST_F(FullDecodeClaim_Pentagon1024_Test, Claimed_EFF7StillWorks)
{
    RegisterMoonSoundClaims(_portDecoder, &_card);

    FunnelOut(_portDecoder, 0xEFF7, 0x04);
    EXPECT_EQ(_context->emulatorState.pEFF7, 0x04);

    uint8_t result = FunnelIn(_portDecoder, 0xEFF7);
    EXPECT_EQ(result, 0x04);
}

TEST_F(FullDecodeClaim_Pentagon1024_Test, Claimed_Canonical7FFDStillWorks)
{
    RegisterMoonSoundClaims(_portDecoder, &_card);

    FunnelOut(_portDecoder, 0x7FFD, 0x07);

    EXPECT_EQ(_context->emulatorState.p7FFD, 0x07);
}

/// endregion </Claim override: Pentagon1024>
