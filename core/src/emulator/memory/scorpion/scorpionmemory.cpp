#include "scorpionmemory.h"

ScorpionMemory::ScorpionMemory(EmulatorContext* context) : Memory(context)
{
}

/// region <Read-path bus-cycle side effects>

/// Bus-cycle side effects of the ProfROM silicon (hardware-reference §5, §9).
/// Both live in the memory read path because on the board they are wired to
/// the memory-bus pins, not to any port or latch - the firmware switches
/// ProfROM planes purely by READING addresses, so no OUT hook exists to hang
/// this on instead:
/// - GAL DD41 (read strobe): while the Service ROM is paged at #0000, a CPU
///   data read (/M1 inactive) of the strobe grid - the four addresses with
///   A1:A0 = 0 (#0100/#0104/#0108/#010C) - clocks the quadrant state machine
///   BEFORE the byte is served, so the strobing read itself returns a
///   post-switch byte (mid-instruction remap, hardware-reference §12.4).
///   The exact 0xFFF3 grid and the !isExecution gate keep the monitor's own
///   bookkeeping inert: the RST 30h dispatcher in RAM reads the plane
///   signature at #0101 (LD HL,(#0101)) on every inter-plane call, and
///   off-grid or fetched bytes must never clock the GAL (Xpeccy:
///   (adr & 0xfff3) == 0x0100 && !m1)
/// - DD50.1 (magic-button DOS trigger release): resets on an instruction-
///   fetch cycle from the upper half - /M1 & /MREQ & (A15 | A14). Data reads
///   never release it: the TR-DOS NMI chain reads RAM at #C001
///   (LD HL,(#C001)) at #0814, before it latches the Service page at #0033,
///   and that operand read must leave page 3 forced (Xpeccy requires m1;
///   ZXMAK2 subscribes RdMemM1 only). Writes never release it either
/// Both effects only CLOCK the state bits and rebuild through the regular
/// UpdateModelBanks() translation - no ad-hoc mapping happens here
void ScorpionMemory::ApplyScorpionReadCycle(uint16_t addr, bool isExecution)
{
    if (_scorpProfromActive && !isExecution && (addr & 0xFFF3) == 0x0100)
    {
        if (_scorpionRomWindow.OnRomRead(_context->emulatorState, _context->temporary, addr))
            UpdateZ80Banks();
    }

    if (_scorpionDosTriggerActive && isExecution && addr >= 0x4000)
    {
        _context->emulatorState.scorpionDosTrigger = 0;
        UpdateZ80Banks();
    }
}

/// Scorpion read path with the silicon effects above. Gated on a single
/// fused branch so every unarmed state pays one predicted test per read,
/// then the generic base serves the byte through the regular bank table
uint8_t ScorpionMemory::MemoryReadFast(uint16_t addr, bool isExecution)
{
    if (_scorpProfromActive | _scorpionDosTriggerActive) [[unlikely]]
        ApplyScorpionReadCycle(addr, isExecution);

    return Memory::MemoryReadFast(addr, isExecution);
}

/// Same gate as MemoryReadFast: single-stepping must clock the silicon
/// exactly like free-running execution, or breakpoints would desynchronise it
uint8_t ScorpionMemory::MemoryReadDebug(uint16_t addr, bool isExecution)
{
    if (_scorpProfromActive | _scorpionDosTriggerActive) [[unlikely]]
        ApplyScorpionReadCycle(addr, isExecution);

    return Memory::MemoryReadDebug(addr, isExecution);
}

/// endregion </Read-path bus-cycle side effects>

/// region <Latch-to-bank translation>

/// Scorpion ZS 256 latch-to-bank translation (design §3). Called from
/// Memory::UpdateZ80Banks() instead of the generic body; always returns true
bool ScorpionMemory::UpdateModelBanks()
{
    EmulatorState& state = _context->emulatorState;
    const CONFIG& config = _context->config;

    // ProfROM variant: resolve the four ROM role pointers from the current
    // quadrant FIRST (§4.2) — quadrant 0 equals the plain 64 KB bundle mapping.
    // state.profrom_bank stays 0 until the ProfROM read-strobe hook (Task 7)
    // writes it; resolving here on every rebuild is what makes snapshot load
    // and TTD restore land in the right quadrant with no extra code
    if (config.mem_model == MM_PROFSCORP)
        ResolveScorpionRomBases(state.profrom_bank);

    // TR-DOS session machinery requires both the DOS and service ROMs to be
    // present (models without them can never enter a TR-DOS session)
    bool dosAvailable = base_dos_rom != nullptr && base_sys_rom != nullptr;

    // Derived session flags are recalculated from scratch on every rebuild;
    // CF_TRDOS itself is preserved, exactly like the generic path
    state.flags &= ~(CF_DOSPORTS | CF_Z80FBUS | CF_LEAVEDOSRAM | CF_LEAVEDOSADR | CF_SETDOSROM | CF_PROFROM);

    // --- bank3 (#C000): #7FFD[2:0] + #1FFD[4]→bit3 + #1FFD[7:6]→bits[5:4],
    // clamped to the physical RAM size ---
    uint8_t ram_mask = GetRamMask();
    uint8_t bank3 = static_cast<uint8_t>((state.p7FFD & 0b111)
                                         | ((state.p1FFD & 0x10) >> 1)
                                         | ((state.p1FFD & 0xC0) >> 2));
    SetRAMPageToBank3(static_cast<uint8_t>(bank3 & ram_mask));

    // --- bank0 (#0000) priority chain ---
    // #1FFD bit 2 is NOT consulted: on hardware it is the RS-232 line, and the
    // heritage "force TR-DOS session" meaning has no hardware backing
    // (hardware-reference §12 item 9)
    //
    // Magic button first (DD50.1 "1-Dos/0-SOS", hardware-reference §9): while
    // the DOS trigger is armed, page 3 (TR-DOS) of the CURRENT plane stands at
    // #0000 - above the RAM-at-#0000 latch and every session selection. The
    // service latch still outranks it: the firmware's entry trick runs
    // LD BC,#1FFD / LD A,#12 / JP #0033 inside the TR-DOS page and expects the
    // OUT (C),A at #0033 to page the service monitor immediately (both pages
    // carry compatible code around #0033). The ProfROM plane register is not
    // involved - the button never clocks the GAL, so the plane survives the
    // whole session. MAME instead keeps the RAM0 latch above the trigger and
    // only suspends it for its pending-NMI window; overriding RAM0 for the
    // whole armed window is the hardware-faithful reading ("page 3 stands at
    // #0000") and keeps the entry chain intact
    if (state.scorpionDosTrigger && dosAvailable)
    {
        if (state.p1FFD & 0x02)
            SetROMSystem();         // service latch wins - the #0033 trick
        else
            SetROMDOS();            // page 3 of the current plane
    }
    else if (state.p1FFD & 0x01)
    {
        SetRAMPageToBank0(0);   // RAM bank 0 mapped at #0000
    }
    else if (state.p1FFD & 0x02)
    {
        SetROMSystem();         // Shadow Monitor (service ROM, bundle page 2)
    }
    else if (state.flags & CF_TRDOS)
    {
        // Open TR-DOS session maps ROM3 regardless of p7FFD[4] — deliberate
        // divergence from the generic path (which maps the service ROM when
        // bit 4 is clear): otherwise the Shadow-monitor "128 TR-DOS" boot-menu
        // path lands in the monitor instead of TR-DOS (hardware-reference
        // §4.4 rule 3, §12 item 10)
        SetROMDOS();
    }
    else if (state.p7FFD & 0x10)
    {
        SetROM48k();            // ROM1: 48K BASIC (bundle page 1)
    }
    else
    {
        SetROM128k();           // ROM0: BASIC 128 (bundle page 0)
    }

    // --- session flags ---
    // Exclusive like the generic path: while a session is open only the close
    // machinery may be armed (CF_LEAVEDOSRAM — unpage once execution runs from
    // RAM, consumed by Z80Step); otherwise the #3Dxx fetch trap is armed while
    // a DOS-capable ROM slot (48K BASIC or Shadow Monitor — never RAM at #0000)
    // is selected with a Beta128 attached
    if ((state.flags & CF_TRDOS) && dosAvailable)
    {
        state.flags |= CF_DOSPORTS | CF_LEAVEDOSRAM;
    }
    else if (state.scorpionDosTrigger && dosAvailable)
    {
        // The armed button also puts the FDC ports on the bus (MAME selects its
        // DOS I/O shadow view on the same trigger) - but the software session's
        // unpage-on-RAM machinery must stay disarmed: the trigger has its own
        // release path (the next instruction fetch from >= #4000, the read-path
        // override above)
        state.flags |= CF_DOSPORTS;
    }
    else if (!(state.p1FFD & 0x01) && ((state.p1FFD & 0x02) || (state.p7FFD & 0x10))
             && config.trdos_present && dosAvailable)
    {
        state.flags |= CF_SETDOSROM;
    }

    // --- ProfROM variant bookkeeping: the read-strobe window is active while
    // the Shadow Monitor is paged at #0000 (design §3) ---
    _scorpProfromActive = config.mem_model == MM_PROFSCORP && _bank_read[0] == base_sys_rom;
    if (_scorpProfromActive)
        state.flags |= CF_PROFROM;

    // Magic-button release gate for the CPU read path (hardware-reference §9)
    _scorpionDosTriggerActive = state.scorpionDosTrigger != 0;

    return true;
}

/// endregion </Latch-to-bank translation>

/// region <ROM geometry>

void ScorpionMemory::ResolveScorpionRomBases(uint8_t quadrant)
{
    uint8_t basePage = static_cast<uint8_t>((quadrant & 0x1F) * ROM_QUADRANT_PAGES);

    base_128_rom = ROMPageHostAddress(basePage);
    base_sos_rom = ROMPageHostAddress(static_cast<uint8_t>(basePage + 1));
    base_sys_rom = ROMPageHostAddress(static_cast<uint8_t>(basePage + 2));
    base_dos_rom = ROMPageHostAddress(static_cast<uint8_t>(basePage + 3));
}

/// ROM loader completion hook (design §4.2): derive the ProfROM image geometry
/// masks (state-machine bits + #7EFD window select) from the validated bank
/// count. Quadrant state itself is untouched - it stays 0 (power-on) unless a
/// snapshot or TTD restore writes it
void ScorpionMemory::OnRomLoaded(uint16_t imageBanks)
{
    if (_context->config.mem_model == MM_PROFSCORP)
        _scorpionRomWindow.Configure(_context->temporary, imageBanks);
}

/// endregion </ROM geometry>
