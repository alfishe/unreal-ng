#include "stdafx.h"
#include "pch.h"

#include "scorpionfixture.h"

/// @brief Scorpion ZS 256 port decoder truth table (implementation-plan Task 4):
///        #1FFD real body, #1FFD/#7EFD read arms, #FE OUT arm, #FF border arm,
///        Beta128 session gating with the monitor-paged exception.
///        Reference: docs/inprogress/2026-09-07-scorpion-zs256-clone/design.md §5.
class ScorpionPorts_Test : public ScorpionMachineFixture
{
};

/// @brief OUT #1FFD,02h pages the Shadow Monitor at #0000 through the full
///        decoder path
TEST_F(ScorpionPorts_Test, OutOneFFDPagesShadowMonitor)
{
    WritePort(0x1FFD, 0x02);
    EXPECT_EQ(_context->emulatorState.p1FFD, 0x02) << "the byte is latched verbatim";
    EXPECT_EQ(BankTag(0x0000), 0xC2) << "service ROM (bundle page 2) at #0000";
    EXPECT_EQ(BankTag(0xC000), 0x40) << "#1FFD alone leaves bank3 alone";
}

/// @brief OUT #1FFD,10h selects RAM bank 8 at #C000 (the 256K+ extension bit)
TEST_F(ScorpionPorts_Test, OutOneFFDBankBitSelectsBankEight)
{
    WritePort(0x1FFD, 0x10);
    EXPECT_EQ(BankTag(0xC000), 0x48) << "#1FFD bit4 maps RAM bank 8 at #C000";
}

/// @brief IN (#1FFD) returns 0xFF and is decoded: the write-only register
///        answers with an open bus instead of tripping the unmapped-port path
TEST_F(ScorpionPorts_Test, InOneFFDReturnsHighDecoded)
{
    EXPECT_EQ(ReadPort(0x1FFD), 0xFF);
    EXPECT_TRUE(_context->pPortDecoder->WasLastPortDecoded())
        << "the register decode itself answers (no 'no peripheral' warning)";
}

/// @brief OUT (#FE) reaches the ULA: border color, pFE latch, border_attr sync
TEST_F(ScorpionPorts_Test, OutFEReachesUla)
{
    WritePort(0x00FE, 0x02);
    EXPECT_EQ(_context->pScreen->GetBorderColor(), COLOR_RED);
    EXPECT_EQ(_context->emulatorState.pFE, 0x02);
    EXPECT_EQ(_context->emulatorState.border_attr, 0x02);
}

/// @brief OUT (#FF),A colors the border; mirrors work because A rides A15-A8
///        and the arm matches the low byte only (design §5)
TEST_F(ScorpionPorts_Test, OutFFSetsBorderColor)
{
    WritePort(0x00FF, 0x03);
    EXPECT_EQ(_context->pScreen->GetBorderColor(), COLOR_MAGENTA);
    EXPECT_EQ(_context->emulatorState.border_attr, 0x03);

    // The classic OUT (#FF),A encoding: B/A end up on the high address byte
    WritePort(0x12FF, 0x05);
    EXPECT_EQ(_context->pScreen->GetBorderColor(), COLOR_CYAN)
        << "mirrors (#nnFF) must reach the border latch too";
}

/// @brief #7FFD D5 locks the port while #1FFD stays live
TEST_F(ScorpionPorts_Test, SevenFFDLockInterplay)
{
    WritePort(0x7FFD, 0x30);  // ROM1 + lock, bank 0
    EXPECT_EQ(BankTag(0x0000), 0xC1) << "the locking write applies: ROM1";
    EXPECT_EQ(BankTag(0xC000), 0x40);
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x30);

    WritePort(0x7FFD, 0x01);  // blocked by the lock
    EXPECT_EQ(BankTag(0x0000), 0xC1) << "locked: the ROM select is frozen";
    EXPECT_EQ(BankTag(0xC000), 0x40) << "locked: the bank select is frozen";
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x30) << "the latch itself stays frozen";

    WritePort(0x1FFD, 0x10);  // #1FFD is not affected by the #7FFD lock
    EXPECT_EQ(BankTag(0xC000), 0x48) << "#1FFD bit4 still maps RAM bank 8";
}

/// @brief Decode-order regression: AY mirrors (#FF05) still resolve to the AY
///        register select ahead of every new arm
TEST_F(ScorpionPorts_Test, AyMirrorStillDecoded)
{
    ReadPort(0xFF05);
    EXPECT_TRUE(_context->pPortDecoder->WasLastPortDecoded())
        << "#FF05 must still select the AY (register readback via a mirror)";
}

/// @brief Beta128 gating: the FDC is off the bus outside a session, answers
///        during one, and keeps answering while the Shadow Monitor is paged
///        even with the session closed (hardware-reference §12.3)
TEST_F(ScorpionPorts_Test, FdcGatingWithMonitorException)
{
    EmulatorState& state = _context->emulatorState;

    // Default: no session, monitor unpaged -> undecoded (floating bus)
    ReadPort(0x001F);
    EXPECT_FALSE(_context->pPortDecoder->WasLastPortDecoded())
        << "outside a session the FDC must be off the bus";

    // Session open (Task 5 scripts the trap path; here the flag is set directly)
    state.flags |= CF_TRDOS;
    _memory->UpdateZ80Banks();
    ReadPort(0x001F);
    EXPECT_TRUE(_context->pPortDecoder->WasLastPortDecoded())
        << "during a session the FDC answers #1F";

    // Monitor-paged exception: session closed but #1FFD bit1 set
    state.flags &= ~CF_TRDOS;
    state.p1FFD = 0x02;
    _memory->UpdateZ80Banks();
    ReadPort(0x001F);
    EXPECT_TRUE(_context->pPortDecoder->WasLastPortDecoded())
        << "the monitor keeps the FDC on the bus (HW 12.3 arbitration)";
}

/// @brief OUT (#FF) drives the border only outside a session; with the FDC on
///        the bus the byte belongs to the FDC system port and the border must
///        not move
TEST_F(ScorpionPorts_Test, OutFFReachesBorderOnlyOutsideSession)
{
    EmulatorState& state = _context->emulatorState;

    WritePort(0x00FF, 0x03);
    EXPECT_EQ(_context->pScreen->GetBorderColor(), COLOR_MAGENTA);

    state.flags |= CF_TRDOS;
    _memory->UpdateZ80Banks();
    WritePort(0x00FF, 0x05);
    EXPECT_EQ(_context->pScreen->GetBorderColor(), COLOR_MAGENTA)
        << "inside a session #FF belongs to the FDC, not the border latch";
}

/// @brief Beta128 mirror addressing (design §5, hardware-reference 12.3): the
///        FDC decodes A7-A0 only, so IN A,(#1F)-style accesses carry A on
///        A15-A8 and must resolve to the canonical device key - both IN and OUT.
///        Regression: real TR-DOS 5.03 boots died on exactly these mirrors
TEST_F(ScorpionPorts_Test, FdcMirrorPortsRoundTripInsideSession)
{
    EmulatorState& state = _context->emulatorState;
    state.flags |= CF_TRDOS;
    _memory->UpdateZ80Banks();

    // OUT (#AB3F),A - track register write through a mirror
    WritePort(0xAB3F, 0x2A);
    // IN (#FF3F) - track register read back through a different mirror
    const uint8_t track = ReadPort(0xFF3F);
    EXPECT_TRUE(_context->pPortDecoder->WasLastPortDecoded())
        << "the mirror IN must reach the canonical #3F device key";
    EXPECT_EQ(track, 0x2A)
        << "mirror OUT and mirror IN must meet at the canonical #3F track register";

    // OUT (#18FF),05h - Beta128 system port through the classic OUT (#FF),A
    // encoding (A rides A15-A8); bit2 set keeps the reset line deasserted
    WritePort(0x18FF, 0x05);
    // IN (#FF) - bits 6-7 belong to the DRQ/INTRQ latch, the low 6 read back
    EXPECT_EQ(ReadPort(0x00FF) & 0x3F, 0x05)
        << "the system-port mirror must latch the Beta128 register";
}

/// @brief Mirror OUT (#nnFF),A inside a session belongs to the FDC system port
///        and must not leak into the border latch (before the mirror fix the
///        border arm matched the low byte first and ate the drive-select byte)
TEST_F(ScorpionPorts_Test, FdcMirrorSystemPortOutBeatsBorder)
{
    EmulatorState& state = _context->emulatorState;
    state.flags |= CF_TRDOS;
    _memory->UpdateZ80Banks();

    WritePort(0x18FF, 0x05);  // TR-DOS drive-select style access
    EXPECT_EQ(_context->emulatorState.border_attr, 0x00)
        << "inside a session the mirror byte is FDC data, not a border color";
}

/// @brief Outside a session the mirrors stay gated: floating bus on IN and no
///        register state change on OUT (the FDC is physically off the bus)
TEST_F(ScorpionPorts_Test, FdcMirrorPortsGatedOutsideSession)
{
    EmulatorState& state = _context->emulatorState;

    ReadPort(0xFF1F);
    EXPECT_FALSE(_context->pPortDecoder->WasLastPortDecoded())
        << "mirror or not, outside a session the FDC is off the bus";

    WritePort(0xAB3F, 0x2A);  // must be ignored by the gated FDC
    state.flags |= CF_TRDOS;
    _memory->UpdateZ80Banks();
    EXPECT_NE(ReadPort(0x003F), 0x2A)
        << "the gated OUT must not have touched the track register";
}

/// @brief Debugger-forced page sync: SetRAMPage translates the page back into
///        the latch pair and reapplies (design §5 debug sync)
TEST_F(ScorpionPorts_Test, SetRAMPageTranslatesToLatches)
{
    _context->pPortDecoder->SetRAMPage(0x0B);  // bank 11 = 1|8
    EXPECT_EQ(_context->emulatorState.p7FFD & 0b111, 0x03);
    EXPECT_EQ(_context->emulatorState.p1FFD & 0x10, 0x10);
    EXPECT_EQ(BankTag(0xC000), 0x4B) << "forced bank 11 lands at #C000";
}

/// @brief Debugger-forced ROM page sync: the select bits follow the page role
TEST_F(ScorpionPorts_Test, SetROMPageTranslatesToSelects)
{
    EmulatorState& state = _context->emulatorState;

    _context->pPortDecoder->SetROMPage(1);  // 48K BASIC
    EXPECT_EQ(state.p7FFD & 0x10, 0x10);
    EXPECT_EQ(BankTag(0x0000), 0xC1);

    _context->pPortDecoder->SetROMPage(2);  // Shadow Monitor
    EXPECT_EQ(state.p1FFD & 0x02, 0x02);
    EXPECT_EQ(BankTag(0x0000), 0xC2);

    _context->pPortDecoder->SetROMPage(0);  // back to BASIC 128
    EXPECT_EQ(state.p7FFD & 0x10, 0x00);
    EXPECT_EQ(state.p1FFD & 0x02, 0x00);
    EXPECT_EQ(BankTag(0x0000), 0xC0);
}
