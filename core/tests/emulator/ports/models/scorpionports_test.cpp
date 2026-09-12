#include "stdafx.h"
#include "pch.h"

#include "scorpionfixture.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"
#include "debugger/breakpoints/breakpointmanager.h"

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

    // #1F is shared: the Beta128 FDC owns it inside a TR-DOS session, the
    // Kempston joystick owns it outside one. So "the FDC is off the bus" can no
    // longer be expressed as "nobody decoded the port" - the joystick arm now
    // legitimately answers here (profrom-service-monitor-menu-flashing.md 9).
    // An idle joystick reads 0x00; the WD1793 status register never does.
    EXPECT_EQ(ReadPort(0xFF1F), 0x00)
        << "outside a session #1F must answer as the joystick, not the FDC";

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

/// region <Hardware turbo flip-flop (hardware-reference 13)>

/// @brief IN from the #7FFD register family sets the turbo flip-flop (7 MHz).
///        The strobe decode is A15:A14 / A5 / A1 / A0 only, so every address
///        mirror clocks it - including the ProfROM #7EFD window port
TEST_F(ScorpionPorts_Test, InSevenFFDFamilySetsTurboFlipFlop)
{
    EmulatorState& state = _context->emulatorState;

    ReadPort(0x7FFD);
    EXPECT_EQ(state.scorpion_turbo, 1) << "IN (#7FFD) - canonical turbo-on strobe";

    state.scorpion_turbo = 0;
    ReadPort(0x7EFD);
    EXPECT_EQ(state.scorpion_turbo, 1) << "IN (#7EFD) - same 01xxxxxxxx1xxx01 decode family";

    state.scorpion_turbo = 0;
    ReadPort(0x5FF5);
    EXPECT_EQ(state.scorpion_turbo, 1) << "wild mirror 0101'1111'1111'0101 clocks it too";
}

/// @brief IN from the #1FFD register family clears the flip-flop (3.5 MHz)
TEST_F(ScorpionPorts_Test, InOneFFDFamilyClearsTurboFlipFlop)
{
    EmulatorState& state = _context->emulatorState;
    state.scorpion_turbo = 1;

    ReadPort(0x1FFD);
    EXPECT_EQ(state.scorpion_turbo, 0) << "IN (#1FFD) - canonical turbo-off strobe";

    state.scorpion_turbo = 1;
    ReadPort(0x3FFD);
    EXPECT_EQ(state.scorpion_turbo, 0) << "IN (#3FFD) - 00xxxxxxxx1xxx01 mirror clears";
}

/// @brief Reads of unrelated ports never clock the flip-flop: keyboard
///        half-rows (A1=1), AY register ports (A15:A14=11), Beta128
///        (#xx1F/#xxFF with A5=0) and #FE (A0=0) all miss the decode
TEST_F(ScorpionPorts_Test, TurboStrobeInertForUnrelatedPorts)
{
    EmulatorState& state = _context->emulatorState;
    const uint16_t ports[] = {0xFEFE, 0xFDFE, 0xFFFD, 0xBFFD, 0x00FF, 0x001F, 0x00FE, 0x7FFE};

    for (uint16_t port : ports)
    {
        state.scorpion_turbo = 1;
        ReadPort(port);
        EXPECT_EQ(state.scorpion_turbo, 1) << "port " << std::hex << port << " must not clear the flip-flop";

        state.scorpion_turbo = 0;
        ReadPort(port);
        EXPECT_EQ(state.scorpion_turbo, 0) << "port " << std::hex << port << " must not set the flip-flop";
    }
}

/// @brief The strobe is a side effect only: the paging registers stay
///        write-only open-bus reads (#FF) exactly as before the turbo hook
TEST_F(ScorpionPorts_Test, TurboStrobeDoesNotChangeInResults)
{
    EXPECT_EQ(ReadPort(0x1FFD), 0xFF);
    EXPECT_EQ(ReadPort(0x7FFD), 0xFF);
}

/// @brief RESET clears the flip-flop - the machine always comes up at 3.5 MHz
TEST_F(ScorpionPorts_Test, ResetClearsTurboFlipFlop)
{
    EmulatorState& state = _context->emulatorState;
    state.scorpion_turbo = 1;

    _context->pPortDecoder->reset();

    EXPECT_EQ(state.scorpion_turbo, 0);
}

/// endregion <Hardware turbo flip-flop (hardware-reference 13)>

/// region <Kempston Joystick & Mouse stubs (profrom-service-monitor-menu-flashing.md)>

/// @brief Kempston Joystick & Mouse stubs: Port #FF1F returns 0x00 (active-high Fire released)
///        while the monitor is paged, #FADF returns 0xFF (active-low buttons released), and #FBDF/#FFDF return 0x00.
TEST_F(ScorpionPorts_Test, KempstonStubsAnswerNeutralValues)
{
    EmulatorState& state = _context->emulatorState;

    // Port #FF1F (Kempston Joystick read by Service Monitor sub_0260h while paged)
    state.p1FFD = 0x02;
    _memory->UpdateZ80Banks();
    EXPECT_EQ(ReadPort(0xFF1F), 0x00) << "Kempston joystick returns 0x00 when idle in monitor";
    EXPECT_TRUE(_context->pPortDecoder->WasLastPortDecoded());

    // Port #FADF (Kempston Mouse buttons read by sub_021bh)
    EXPECT_EQ(ReadPort(0xFADF), 0xFF) << "Kempston mouse buttons return 0xFF (all released)";
    EXPECT_TRUE(_context->pPortDecoder->WasLastPortDecoded());

    // Port #FBDF (Mouse X) and #FFDF (Mouse Y)
    EXPECT_EQ(ReadPort(0xFBDF), 0x00) << "Kempston mouse X returns stable coordinate";
    EXPECT_TRUE(_context->pPortDecoder->WasLastPortDecoded());
    EXPECT_EQ(ReadPort(0xFFDF), 0x00) << "Kempston mouse Y returns stable coordinate";
    EXPECT_TRUE(_context->pPortDecoder->WasLastPortDecoded());
}

/// @brief End-to-end verification of the menu highlight fix (profrom-service-monitor-menu-flashing.md):
///        Boots PROFSCORP into the Service Monitor via MNI.
///        Checks that the active menu item attribute row 6 (0x58C1) remains continuously
///        highlighted (0x31) across 30 consecutive frames and never flashes to unhighlighted (0x29).
TEST(ScorpionServiceMonitor_Test, ProfRomServiceMonitorHighlightDoesNotBlink)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("", "PROFSCORP", LoggerLevel::LogError);
    ASSERT_TRUE(emulator) << "PROFSCORP could not be created";

    EmulatorContext* context = emulator->GetContext();
    Memory* memory = context->pMemory;

    // Boot until RAM and basic vectors are initialized (70 frames = 1.4s virtual time)
    emulator->RunNFrames(70);

    // Enter Service Monitor via MNI
    emulator->RequestMNI();
    // Allow 20 frames for the monitor UI to render (draw completes by frame 16)
    emulator->RunNFrames(20);

    auto bpDesc = new BreakpointDescriptor();
    bpDesc->type = BRK_MEMORY;
    bpDesc->memoryType = BRK_MEM_WRITE;
    bpDesc->matchType = BRK_MATCH_ADDR;
    bpDesc->z80address = 0x58C1;
    bpDesc->active = true;
    // Emulator exposes the manager directly; going through pDebugManager would
    // need the full DebugManager definition (emulatorcontext.h only forward-
    // declares it).
    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    ASSERT_NE(bpManager, nullptr);
    bpManager->AddBreakpoint(bpDesc);

    for (int frame = 0; frame < 30; frame++)
    {
        emulator->RunNFrames(1, false);
        const bool hit = bpManager->GetLastTriggeredBreakpointID() != BRK_INVALID;
        uint8_t attr = memory->DirectReadFromZ80Memory(0x58C1);
        EXPECT_FALSE(hit) << "Frame " << frame
                          << ": 0x58C1 written mid-frame (phantom input redraw storm)!";
        EXPECT_EQ(attr, 0x31) << "Frame " << frame
                              << ": active menu item must remain steadily highlighted (0x31) and never flash to 0x29";
        if (hit)
        {
            bpManager->ClearLastTriggeredBreakpoint();
            emulator->Resume();
        }
    }

    manager->RemoveEmulator(emulator->GetId());
}

/// @brief Regression (profrom-service-monitor-menu-flashing.md 7.1):
///        #FF1F must decode as the Kempston joystick whenever TR-DOS is not
///        active - INCLUDING while #1FFD bit 1 is clear.
///
///        The poll that matters (Page 5 sub_0260h) is reached by RST 30h and
///        runs with #1FFD = 0x10, not 0x12. Gating the arm on the Shadow
///        Monitor bit therefore disabled it for exactly that read: the poll
///        fell through to the floating bus, returned 0xFF, and the firmware
///        read Fire as held - enqueuing a phantom 0x80 click every 5 frames and
///        driving a continuous menu redraw. A VRAM-attribute assertion cannot
///        see this (the attribute is rewritten to 0x31 within the same frame),
///        so the contract is pinned at the port level here.
TEST_F(ScorpionPorts_Test, KempstonJoystick_Port1F_ReadsZeroWhateverThePagingLatch)
{
    ASSERT_TRUE(RebuildWithModel(MM_PROFSCORP, RAM_256));

    _context->emulatorState.flags &= ~CF_TRDOS;
    _context->emulatorState.scorpionDosTrigger = 0;

    // Page 5 (driver plane, where sub_0260h lives): service bit CLEAR.
    _context->emulatorState.p1FFD = 0x10;
    EXPECT_EQ(ReadPort(0xFF1F), 0x00)
        << "#FF1F must read as an idle Kempston joystick from the driver plane";

    // Page 2 (Shadow Monitor): service bit SET - must behave identically.
    _context->emulatorState.p1FFD = 0x12;
    EXPECT_EQ(ReadPort(0xFF1F), 0x00)
        << "#FF1F must read as an idle Kempston joystick from the monitor plane";
}

/// @brief The joystick arm must not steal #1F from the FDC inside a TR-DOS
///        session - that is the one case where Beta128 owns the port.
TEST_F(ScorpionPorts_Test, KempstonJoystick_DoesNotStealPort1FFromBeta128InTrdos)
{
    ASSERT_TRUE(RebuildWithModel(MM_PROFSCORP, RAM_256));

    _context->emulatorState.scorpionDosTrigger = 0;
    _context->emulatorState.p1FFD = 0x10;
    _context->emulatorState.flags |= CF_TRDOS;

    // The WD1793 status register drives the bus here; the idle controller never
    // reports the 0x00 the joystick stub would return.
    EXPECT_NE(ReadPort(0x001F), 0x00)
        << "inside a TR-DOS session #1F must reach the FDC, not the joystick stub";
}

/// endregion <Kempston Joystick & Mouse stubs>


