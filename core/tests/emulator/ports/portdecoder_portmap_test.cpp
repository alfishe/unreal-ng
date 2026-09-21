#include "stdafx.h"
#include "gtest/gtest.h"

#include <string>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/ports/models/portdecoder_pentagon128.h"
#include "emulator/ports/models/portdecoder_profi.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"
#include "emulator/ports/models/portdecoder_spectrum128.h"
#include "emulator/ports/models/portdecoder_spectrum3.h"
#include "emulator/ports/models/portdecoder_spectrum48.h"

/// @file portdecoder_portmap_test.cpp
/// @brief Static port-map introspection (P1-5, GET /ports) unit tests.
///
/// getPortMapEntries() must mirror the per-model decode equations that the
/// portdecoder_models_test suite pins down with full 65536-port sweeps, keep
/// the fitment-conditional rows (mouse / Beta128) tied to their config, and
/// GetMouseRoutingState() must answer the triage question "would a mouse read
/// be decoded right now" - including the Scorpion deviations that live in the
/// IsPort_KempstonMouse override, not in the gated standard decode.

namespace
{
/// Row lookup by canonical port (nullptr when the row is absent)
const PortMapEntry* FindEntry(const std::vector<PortMapEntry>& entries, uint16_t port)
{
    for (const PortMapEntry& entry : entries)
    {
        if (entry.port == port)
            return &entry;
    }
    return nullptr;
}
}  // namespace

class PortDecoder_PortMap_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _mouse = new Mouse(_context);
        _context->pMouse = _mouse;  // fitted: Mouse::_present defaults to true
    }

    void TearDown() override
    {
        delete _mouse;
        delete _context;
    }

    EmulatorContext* _context = nullptr;
    Mouse* _mouse = nullptr;
};

/// region <Static rows per model>

TEST_F(PortDecoder_PortMap_Test, Spectrum48_NoPagingRows_MouseRowsFollowFitment)
{
    _context->config.mem_model = MM_SPECTRUM48;
    PortDecoder_Spectrum48 decoder(_context);

    std::vector<PortMapEntry> entries = decoder.getPortMapEntries();

    // Universal rows: #FE (keyboard/border), AY register select / data
    const PortMapEntry* fe = FindEntry(entries, 0x00FE);
    ASSERT_NE(fe, nullptr);
    EXPECT_EQ(fe->mask, 0x0001);
    EXPECT_EQ(fe->match, 0x0000);
    EXPECT_NE(FindEntry(entries, 0xFFFD), nullptr);
    EXPECT_NE(FindEntry(entries, 0xBFFD), nullptr);

    // 48K has no paging latch and no TR-DOS interface configured
    EXPECT_EQ(FindEntry(entries, 0x7FFD), nullptr);
    EXPECT_EQ(FindEntry(entries, 0x001F), nullptr);

    // Mouse rows while fitted (standard decode: A5-A0 = #1F, A9 = 1)
    const PortMapEntry* buttons = FindEntry(entries, 0xFADF);
    ASSERT_NE(buttons, nullptr);
    EXPECT_EQ(buttons->mask, 0x023F);
    EXPECT_EQ(buttons->match, 0x021F);
    EXPECT_NE(FindEntry(entries, 0xFBDF), nullptr);
    EXPECT_NE(FindEntry(entries, 0xFFDF), nullptr);

    // Unfitted ([INPUT] Mouse= off): rows disappear
    _mouse->SetPresent(false);
    entries = decoder.getPortMapEntries();
    EXPECT_EQ(FindEntry(entries, 0xFADF), nullptr);
    EXPECT_EQ(FindEntry(entries, 0xFBDF), nullptr);
    EXPECT_EQ(FindEntry(entries, 0xFFDF), nullptr);
}

TEST_F(PortDecoder_PortMap_Test, Spectrum128_PagingRowMatchesDecodeEquation)
{
    _context->config.mem_model = MM_SPECTRUM128;
    PortDecoder_Spectrum128 decoder(_context);

    const std::vector<PortMapEntry> entries = decoder.getPortMapEntries();
    const PortMapEntry* paging = FindEntry(entries, 0x7FFD);
    ASSERT_NE(paging, nullptr);
    EXPECT_EQ(paging->mask, 0x8006);
    EXPECT_EQ(paging->match, 0x0004);
}

TEST_F(PortDecoder_PortMap_Test, Plus3_BothPagingLatchesListed)
{
    _context->config.mem_model = MM_PLUS3;
    PortDecoder_Spectrum3 decoder(_context);

    const std::vector<PortMapEntry> entries = decoder.getPortMapEntries();
    const PortMapEntry* paging = FindEntry(entries, 0x7FFD);
    ASSERT_NE(paging, nullptr);
    EXPECT_EQ(paging->mask, 0xC002);
    EXPECT_EQ(paging->match, 0x4000);

    const PortMapEntry* disk = FindEntry(entries, 0x1FFD);
    ASSERT_NE(disk, nullptr);
    EXPECT_EQ(disk->mask, 0xF002);
    EXPECT_EQ(disk->match, 0x1000);
}

TEST_F(PortDecoder_PortMap_Test, Profi_ExtendedPagingRow)
{
    _context->config.mem_model = MM_PROFI;
    PortDecoder_Profi decoder(_context);

    const std::vector<PortMapEntry> entries = decoder.getPortMapEntries();
    ASSERT_NE(FindEntry(entries, 0x7FFD), nullptr);

    const PortMapEntry* extended = FindEntry(entries, 0xDFFD);
    ASSERT_NE(extended, nullptr);
    EXPECT_EQ(extended->mask, 0x2002);
    EXPECT_EQ(extended->match, 0x0000);
}

TEST_F(PortDecoder_PortMap_Test, Scorpion_NarrowedFeJoystickRowAndMouseGate)
{
    _context->config.mem_model = MM_SCORP;
    PortDecoder_Scorpion256 decoder(_context);

    const std::vector<PortMapEntry> entries = decoder.getPortMapEntries();

    // #FE narrowed to two extra address bits (PortDecoder_Scorpion256::IsPort_FE)
    const PortMapEntry* fe = FindEntry(entries, 0x00FE);
    ASSERT_NE(fe, nullptr);
    EXPECT_EQ(fe->mask, 0x0023);
    EXPECT_EQ(fe->match, 0x0022);

    // Paging latches carry the Scorpion qualifier set
    ASSERT_NE(FindEntry(entries, 0x7FFD), nullptr);
    ASSERT_NE(FindEntry(entries, 0x1FFD), nullptr);

    // Joystick #FF1F gives way to the FDC under TR-DOS / Shadow Monitor
    const PortMapEntry* joystick = FindEntry(entries, 0xFF1F);
    ASSERT_NE(joystick, nullptr);
    ASSERT_NE(joystick->gate, nullptr);

    // Mouse rows carry the Scorpion deviation in their gate
    const PortMapEntry* buttons = FindEntry(entries, 0xFADF);
    ASSERT_NE(buttons, nullptr);
    ASSERT_NE(buttons->gate, nullptr);
    EXPECT_NE(std::string(buttons->gate).find("TR-DOS"), std::string::npos);
}

TEST_F(PortDecoder_PortMap_Test, Pentagon_CovoxRowResolvesToFB)
{
    _context->config.mem_model = MM_PENTAGON;
    PortDecoder_Pentagon128 decoder(_context);

    const std::vector<PortMapEntry> entries = decoder.getPortMapEntries();
    ASSERT_NE(FindEntry(entries, 0x7FFD), nullptr);

    const PortMapEntry* covox = FindEntry(entries, 0x00FB);
    ASSERT_NE(covox, nullptr);
    EXPECT_EQ(covox->mask, 0x00F5);
    EXPECT_EQ(covox->match, 0x00F1);
}

/// endregion </Static rows per model>

/// region <Beta128 rows>

TEST_F(PortDecoder_PortMap_Test, Beta128Rows_RequireTrdosConfig_PentagonUngated)
{
    _context->config.mem_model = MM_PENTAGON;
    _context->config.trdos_present = true;
    PortDecoder_Pentagon128 decoder(_context);

    std::vector<PortMapEntry> entries = decoder.getPortMapEntries();
    const uint16_t betaPorts[] = {0x001F, 0x003F, 0x005F, 0x007F, 0x00FF};
    for (uint16_t port : betaPorts)
    {
        ASSERT_NE(FindEntry(entries, port), nullptr) << "missing Beta128 row for #" << static_cast<int>(port);
    }

    // Data registers answer through exact registered keys; the system register also
    // requires the full low byte 0xFF (#xxF7 must not reset the FDC)
    EXPECT_EQ(FindEntry(entries, 0x001F)->mask, 0xFFFF);
    EXPECT_EQ(FindEntry(entries, 0x001F)->match, 0x001F);
    EXPECT_EQ(FindEntry(entries, 0x00FF)->mask, 0x00FF);
    EXPECT_EQ(FindEntry(entries, 0x00FF)->match, 0x00FF);

    // Pentagon: the FDC is always on the bus once configured - no gate
    EXPECT_EQ(FindEntry(entries, 0x001F)->gate, nullptr);

    // Without the TR-DOS interface the rows are gone
    _context->config.trdos_present = false;
    entries = decoder.getPortMapEntries();
    EXPECT_EQ(FindEntry(entries, 0x001F), nullptr);
    EXPECT_EQ(FindEntry(entries, 0x00FF), nullptr);
}

TEST_F(PortDecoder_PortMap_Test, Scorpion_Beta128RowsCarrySessionGate)
{
    _context->config.mem_model = MM_SCORP;
    _context->config.trdos_present = true;
    PortDecoder_Scorpion256 decoder(_context);

    const std::vector<PortMapEntry> entries = decoder.getPortMapEntries();
    const PortMapEntry* status = FindEntry(entries, 0x001F);
    ASSERT_NE(status, nullptr);
    ASSERT_NE(status->gate, nullptr);
    EXPECT_NE(std::string(status->gate).find("CF_TRDOS"), std::string::npos);
}

/// endregion </Beta128 rows>

/// region <Mouse routing (design Q4)>

TEST_F(PortDecoder_PortMap_Test, MouseRouting_DecodedByDefault_CfDosportsHides)
{
    _context->config.mem_model = MM_SPECTRUM48;
    PortDecoder_Spectrum48 decoder(_context);

    bool decoded = false;
    std::string note;
    decoder.GetMouseRoutingState(decoded, note);
    EXPECT_TRUE(decoded);

    // TR-DOS ports accessible: only Beta Disk operations answer
    _context->emulatorState.flags |= CF_DOSPORTS;
    decoder.GetMouseRoutingState(decoded, note);
    EXPECT_FALSE(decoded);
    EXPECT_NE(note.find("CF_DOSPORTS"), std::string::npos);
}

TEST_F(PortDecoder_PortMap_Test, MouseRouting_NotFittedNote)
{
    _context->config.mem_model = MM_SPECTRUM48;
    _mouse->SetPresent(false);
    PortDecoder_Spectrum48 decoder(_context);

    bool decoded = true;
    std::string note;
    decoder.GetMouseRoutingState(decoded, note);
    EXPECT_FALSE(decoded);
    EXPECT_NE(note.find("not fitted"), std::string::npos);
}

TEST_F(PortDecoder_PortMap_Test, MouseRouting_ScorpionDosTriggerHides_ShadowMonitorKeepsCanonical)
{
    _context->config.mem_model = MM_SCORP;
    PortDecoder_Scorpion256 decoder(_context);

    bool decoded = false;
    std::string note;
    decoder.GetMouseRoutingState(decoded, note);
    EXPECT_TRUE(decoded);

    // Armed magic-button DOS trigger without CF_DOSPORTS: pure Scorpion session
    // gating, visible only through the IsPort_KempstonMouse override
    _context->emulatorState.scorpionDosTrigger = 1;
    decoder.GetMouseRoutingState(decoded, note);
    EXPECT_FALSE(decoded);
    EXPECT_NE(note.find("model-specific"), std::string::npos);

    // Shadow Monitor paged alone keeps the canonical #xxDF mouse ports: only
    // the five exact Beta low-byte mirrors move to the FDC (hardware-reference
    // 12.3) - the probe port #FADF is not one of them
    _context->emulatorState.scorpionDosTrigger = 0;
    _context->emulatorState.p1FFD = 0x02;
    decoder.GetMouseRoutingState(decoded, note);
    EXPECT_TRUE(decoded);

    _context->emulatorState.p1FFD = 0x00;
    decoder.GetMouseRoutingState(decoded, note);
    EXPECT_TRUE(decoded);
}

/// endregion </Mouse routing (design Q4)>
