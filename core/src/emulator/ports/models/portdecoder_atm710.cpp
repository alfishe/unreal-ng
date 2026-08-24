#include "stdafx.h"
#include "portdecoder_atm710.h"

#include "common/modulelogger.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"
#include "emulator/sound/soundmanager.h"

/// region <Constructors / Destructors>

PortDecoder_ATM710::PortDecoder_ATM710(EmulatorContext* context) : PortDecoder(context)
{
    _context = context;
    _state = &context->emulatorState;
    _memory = context->pMemory;
    _screen = context->pScreen;
    _keyboard = context->pKeyboard;
}

PortDecoder_ATM710::~PortDecoder_ATM710()
{
    MLOGDEBUG("PortDecoder_ATM710::~PortDecoder_ATM710()");
}

/// endregion </Constructors / Destructors>

/// region <Interface methods>

void PortDecoder_ATM710::reset()
{
    _7FFD_Locked = false;

    _state->p7FFD = 0x00;
    _state->pEFF7 = 0x00;

    // FF77 / pFFF7 state is deliberately NOT touched here: the boot defaults
    // are mode-dependent and applied by ApplyBootROMDefaults() right after this
    // (mirroring the original reset(mode) ATM block). pFF77 keeps its old value
    // so that the pFF77 bit0 transition performs the physical memswap/unswap;
    // the atmMemSwapped flag itself is only ever toggled by atmMemSwap().

    // Hardware boots at 7MHz: pFF77.3 = 0 and pEFF7.4 = 0 select the x2
    // default of the turbo formula below (reference: Xpeccy pentevo.c
    // compReset clears pEFF7, evoOut77d maps the cleared bits to x2)
    _state->next_z80_frequency_multiplier = 2;
    MLOGDEBUG("reset: multiplier=2 (7MHz hardware boot state)");
}

void PortDecoder_ATM710::ApplyBootROMDefaults(ROMModeEnum mode)
{
    // Port of the original reset(mode) ATM block (Unreal Speccy z80.cpp):
    // RM_DOS installs the TR-DOS memory-manager defaults, every other mode
    // disables the manager entirely - PEN off means all four windows read the
    // last ROM page (the ATM BIOS, which then drives FF77 itself during boot).
    if (mode == RM_DOS)
    {
        // aFF77: palette write gate off, TR-DOS ROM select, manager enabled
        // pFF77: video mode 3 (ZX compatible), INT gate on, memswap on.
        // Routed through Port_FF77_Out so the memswap / video-mode / int_gate
        // side effects fire exactly like the original set_atm_FF77().
        Port_FF77_Out(0x4000 | 0x200 | 0x100, 0x80 | 0x40 | 0x20 | 3, 0x0000);

        // Both register sets (7FFD.4 = 0 / 1) start with the same mapping:
        // window 0 = sys/TR-DOS ROM pair, windows 1-3 = RAM 5/2/0
        _state->pFFF7[0] = 0x0100 | 1;
        _state->pFFF7[1] = 0x0200 | 5;
        _state->pFFF7[2] = 0x0200 | 2;
        _state->pFFF7[3] = 0x0200 | 0;
        _state->pFFF7[4] = 0x0100 | 1;
        _state->pFFF7[5] = 0x0200 | 5;
        _state->pFFF7[6] = 0x0200 | 2;
        _state->pFFF7[7] = 0x0200 | 0;

        // set_atm_FF77() ran set_banks() before pFFF7 was initialized; the
        // final mapping is rebuilt here (set_mode() runs set_banks() again in
        // the original reset sequence)
        updateMemoryBanks();
    }
    else
    {
        // Manager disabled, ~CPM active, video mode 0
        Port_FF77_Out(0x0000, 0x00, 0x0000);
    }
}

void PortDecoder_ATM710::UpdateModelMemoryBanks()
{
    updateMemoryBanks();
}

uint8_t PortDecoder_ATM710::DecodePortIn(uint16_t port, uint16_t pc)
{
    uint8_t result = 0xFF;
    _lastPortDecoded = false;

    uint16_t decodedPort = decodePort(port);

    // Port #FE - keyboard, tape, border
    if (IsPort_FE(port))
    {
        result = Default_Port_FE_In(port, pc);
        _lastPortDecoded = true;
    }
    // Port #FFFD - AY register read
    else if (IsPort_FFFD(port))
    {
        result = PeripheralPortIn(PORT_FFFD);
        _lastPortDecoded = true;
    }
    // Port #EFF7 - Extended control read
    else if (IsPort_EFF7(port))
    {
        result = _state->pEFF7;
        _lastPortDecoded = true;
    }
    // Beta128 FDC ports
    else if (IsBeta128Port(decodedPort))
    {
        result = PeripheralPortIn(decodedPort);
        _lastPortDecoded = true;
    }

    OnPortInComplete(port, result, pc);
    return result;
}

void PortDecoder_ATM710::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    uint16_t decodedPort = decodePort(port);

    // Port #FE - border, beeper, tape
    if (IsPort_FE(port))
    {
        Default_Port_FE_Out(port, value, pc);
    }
    // Port #7FFD - 128K paging
    else if (IsPort_7FFD(port))
    {
        Port_7FFD_Out(port, value, pc);
    }
    // Port #FF77 - ATM control
    else if (IsPort_FF77(port))
    {
        Port_FF77_Out(port, value, pc);
    }
    // Port #FFF7 group - bank select
    else
    {
        // Port #EFF7 - extended control. Must be tested BEFORE the broad
        // #xFFF7 window decode: IsPort_FFF7 matches any low byte 0xF7, so
        // 0xEFF7 would otherwise land in the window-3 memory manager register
        // and never reach the system port (reference: Xpeccy pentevo.c
        // evoPortMap scans the exact-decoded EFF7 entry ahead of the x7F7
        // window entry)
        uint8_t windowIndex;
        if (IsPort_EFF7(port))
        {
            Port_EFF7_Out(port, value, pc);
        }
        else if (IsPort_FFF7(port, windowIndex))
        {
            Port_FFF7_Out(port, value, windowIndex, pc);
        }
        // Port #BFFD - AY data
        else if (IsPort_BFFD(port))
        {
            PeripheralPortOut(PORT_BFFD, value);
        }
        // Port #FFFD - AY register select
        else if (IsPort_FFFD(port))
        {
            PeripheralPortOut(PORT_FFFD, value);
        }
        // Beta128 FDC ports
        else if (IsBeta128Port(decodedPort))
        {
            PeripheralPortOut(decodedPort, value);
        }
    }

    OnPortOutComplete(port, value, pc);
}

void PortDecoder_ATM710::SetRAMPage(uint8_t page)
{
    if (_memory)
    {
        _memory->SetRAMPageToBank3(page);
    }
}

void PortDecoder_ATM710::SetROMPage(uint8_t page)
{
    if (_memory)
    {
        _memory->SetROMPage(page);
    }
}

/// endregion </Interface methods>

/// region <Port detection>

bool PortDecoder_ATM710::IsPort_FE(uint16_t port)
{
    // Port #FE: A0 = 0
    return (port & 0x0001) == 0x0000;
}

bool PortDecoder_ATM710::IsPort_7FFD(uint16_t port)
{
    // Port #7FFD: A15=0, A2=1, A1=0
    // Mask 0x8006, match 0x0004
    static const uint16_t mask  = 0b1000'0000'0000'0110;
    static const uint16_t match = 0b0000'0000'0000'0100;
    return (port & mask) == match;
}

bool PortDecoder_ATM710::IsPort_FF77(uint16_t port)
{
    // ATM710: Full decode of low byte
    // Mask 0x00FF, match 0x0077
    return (port & 0x00FF) == 0x0077;
}

bool PortDecoder_ATM710::IsPort_FFF7(uint16_t port, uint8_t& windowIndex)
{
    // Port #xFFF7 group (memory manager registers): low byte = 0xF7
    // Window selected by A15:A14: 0x00F7 / 0x3FF7 = 0, 0x7FF7 = 1, 0xBFF7 = 2, 0xFFF7 = 3
    // Combined with 7FFD.4 to select one of 8 registers
    // (matches original Unreal Speccy io.cpp and ZXMAK2 MemoryAtm710.cs)

    if ((port & 0x00FF) != 0x00F7)  // Low byte = F7
        return false;

    windowIndex = (port >> 14) & 0x03;
    return true;
}

bool PortDecoder_ATM710::IsPort_EFF7(uint16_t port)
{
    // Port #EFF7: Full decode
    return port == 0xEFF7;
}

bool PortDecoder_ATM710::IsPort_BFFD(uint16_t port)
{
    // Port #BFFD: A15=1, A14=0, A1=0
    static const uint16_t mask  = 0b1100'0000'0000'0010;
    static const uint16_t match = 0b1000'0000'0000'0000;
    return (port & mask) == match;
}

bool PortDecoder_ATM710::IsPort_FFFD(uint16_t port)
{
    // Port #FFFD: A15=1, A14=1, A1=0
    static const uint16_t mask  = 0b1100'0000'0000'0010;
    static const uint16_t match = 0b1100'0000'0000'0000;
    return (port & mask) == match;
}

bool PortDecoder_ATM710::IsBeta128Port(uint16_t decodedPort)
{
    return decodedPort == 0x001F || decodedPort == 0x003F ||
           decodedPort == 0x005F || decodedPort == 0x007F ||
           decodedPort == 0x00FF;
}

uint16_t PortDecoder_ATM710::decodePort(uint16_t port)
{
    // Basic port decoding for ATM710
    // Beta128 ports: mask low 7 bits, A7 is system register
    if ((port & 0x0001) == 0x0001 && (port & 0x00FF) <= 0x00FF)
    {
        if ((port & 0x001F) == 0x001F ||
            (port & 0x003F) == 0x003F ||
            (port & 0x005F) == 0x005F ||
            (port & 0x007F) == 0x007F)
        {
            return port & 0x00FF;
        }
    }
    return port;
}

/// endregion </Port detection>

/// region <Port handlers>

void PortDecoder_ATM710::Port_7FFD_Out([[maybe_unused]] uint16_t port, uint8_t value, [[maybe_unused]] uint16_t pc)
{
    // If paging is locked, ignore writes
    if (_7FFD_Locked)
    {
        MLOGWARNING("Port_7FFD_Out: Paging locked, ignoring write of 0x%02X", value);
        return;
    }

    uint8_t oldValue = _state->p7FFD;
    _state->p7FFD = value;

    // Check lock bit
    if (value & PORT_7FFD_LOCK)
    {
        _7FFD_Locked = true;
    }

    // Full set_banks() equivalent: window mapping + TR-DOS session flag re-derivation
    if (_memory)
        _memory->UpdateZ80Banks();

    // Update screen if shadow bit changed
    if ((oldValue ^ value) & PORT_7FFD_SCREEN)
    {
        if (_screen)
        {
            bool useShadowScreen = (value & PORT_7FFD_SCREEN) != 0;
            _screen->SetActiveScreen(useShadowScreen ? SpectrumScreenEnum::SCREEN_SHADOW : SpectrumScreenEnum::SCREEN_NORMAL);
        }
    }

    MLOGDEBUG("Port_7FFD_Out: value=0x%02X %s", value, Dump_7FFD_value(value).c_str());
}

void PortDecoder_ATM710::Port_FF77_Out(uint16_t port, uint8_t value, [[maybe_unused]] uint16_t pc)
{
    uint8_t oldValue = _state->pFF77;

    // Check if memory swap bit changed
    if ((oldValue ^ value) & ATM_FF77_MEMSWAP)
    {
        atmMemSwap();
    }

    // Store value and full port address
    _state->pFF77 = value;
    _state->aFF77 = port;

    // Update video mode if changed (bits 0,1,2 per original Unreal Speccy)
    if ((oldValue ^ value) & ATM_FF77_VMODE_MASK)
    {
        uint8_t oldMode = oldValue & ATM_FF77_VMODE_MASK;
        uint8_t newMode = value & ATM_FF77_VMODE_MASK;
        MLOGINFO("Port_FF77_Out: Video mode changed from %d to %d (pFF77: 0x%02X -> 0x%02X)",
                 oldMode, newMode, oldValue, value);

        // Trigger video mode re-detection and framebuffer reallocation
        if (_context->pScreen)
        {
            _context->pScreen->InitRaster();
            MLOGINFO("Port_FF77_Out: InitRaster() called, new video mode: %d",
                     _context->pScreen->_vid.mode);
        }
        else
        {
            MLOGWARNING("Port_FF77_Out: pScreen is NULL, cannot trigger InitRaster()");
        }
    }

    // Update turbo mode
    updateTurboMode();

    // Full set_banks() equivalent: window mapping + TR-DOS session flag re-derivation
    if (_memory)
        _memory->UpdateZ80Banks();

    // Update INT gate (bit 5): controls whether external interrupts reach the CPU.
    // Original Unreal Speccy: cpu.int_gate = (comp.pFF77 & 0x20)
    // This is essential for proper boot sequence timing.
    if (_context && _context->pCore && _context->pCore->GetZ80())
    {
        _context->pCore->GetZ80()->int_gate = (value & ATM_FF77_INTGATE) != 0;
        MLOGDEBUG("Port_FF77_Out: int_gate=%d", _context->pCore->GetZ80()->int_gate ? 1 : 0);
    }

    MLOGDEBUG("Port_FF77_Out: port=0x%04X value=0x%02X %s", port, value, Dump_FF77_value(value).c_str());
}

void PortDecoder_ATM710::Port_FFF7_Out([[maybe_unused]] uint16_t port, uint8_t value, uint8_t windowIndex, [[maybe_unused]] uint16_t pc)
{
    // Determine which set of registers to use based on 7FFD.4
    uint8_t regSet = (_state->p7FFD & 0x10) ? 4 : 0;
    uint8_t regIndex = regSet + windowIndex;

    // The register contents come from the data bus alone: type in bits 6-7,
    // page in bits 0-5 (active-low). Expanded to the internal encoding:
    // [9:8] = type, [7:0] = page
    unsigned fullValue = (((value & 0xC0) << 2) | (value & 0x3F)) ^ 0x33F;
    _state->pFFF7[regIndex] = fullValue;

    // Full set_banks() equivalent: window mapping + TR-DOS session flag re-derivation
    if (_memory)
        _memory->UpdateZ80Banks();

    MLOGDEBUG("Port_FFF7_Out: window=%d regIndex=%d value=0x%04X %s",
              windowIndex, regIndex, fullValue, Dump_FFF7_value(fullValue).c_str());
}

void PortDecoder_ATM710::Port_EFF7_Out([[maybe_unused]] uint16_t port, uint8_t value, [[maybe_unused]] uint16_t pc)
{
    _state->pEFF7 = value;

    // Re-evaluate the turbo select on every write (reference: Xpeccy
    // pentevo.c evoOutEFF7 calls compSetTurbo unconditionally, same as
    // evoOut77d re-evaluates on FF77 writes)
    updateTurboMode();

    MLOGDEBUG("Port_EFF7_Out: value=0x%02X %s", value, Dump_EFF7_value(value).c_str());
}

// Route a RAM page mapping to the bank-specific Memory API (helper for the
// ATM memory manager window loop below)
static void SetRAMPageToBank(Memory* memory, uint8_t bank, uint16_t page)
{
    switch (bank)
    {
        case 0:  memory->SetRAMPageToBank0(page); break;
        case 1:  memory->SetRAMPageToBank1(page); break;
        case 2:  memory->SetRAMPageToBank2(page); break;
        default: memory->SetRAMPageToBank3(page); break;
    }
}

void PortDecoder_ATM710::updateMemoryBanks()
{
    if (!_memory)
        return;

    // Faithful port of the original Unreal Speccy set_banks() MM_ATM710 / MM_ATM3
    // branch (memory.cpp). Masks mirror temp.ram_mask / temp.rom_mask.

    const CONFIG& config = _context->config;

    // RAM mask from the configured RAM size (ramsize is in KiB, pages are 16KiB)
    uint16_t ramPages = config.ramsize ? (config.ramsize / 16) : MAX_RAM_PAGES;
    uint8_t ramMask = static_cast<uint8_t>(ramPages - 1);

    // ROM mask from the actually loaded ROM banks
    uint8_t romBanks = (_context->pCore && _context->pCore->GetROM()) ? _context->pCore->GetROM()->GetROMBanksLoaded() : 0;
    uint8_t romMask = romBanks ? static_cast<uint8_t>(romBanks - 1) : 0;

    // ~CPM=0 (aFF77 bit 9 clear) selects the TR-DOS half of the ROM pair
    if (!(_state->aFF77 & ATM_AFF77_CPM))
    {
        _state->flags |= CF_TRDOS;
    }
    bool trdos = (_state->flags & CF_TRDOS) != 0;

    // PEN=0 (aFF77 bit 8 clear): memory manager disabled - all four windows
    // read the last ROM page, writes go to the trash page
    if (!(_state->aFF77 & ATM_AFF77_PEN))
    {
        _memory->SetROMPageToBank(0, romMask);
        _memory->SetROMPageToBank(1, romMask);
        _memory->SetROMPageToBank(2, romMask);
        _memory->SetROMPageToBank(3, romMask);
        return;
    }

    // Register set selected by 7FFD bit 4
    unsigned regSet = (_state->p7FFD & 0x10) ? 4 : 0;

    for (uint8_t bank = 0; bank < 4; bank++)
    {
        unsigned fff7 = _state->pFFF7[regSet + bank];

        switch (fff7 & 0x300)
        {
            case 0x000:  // RAM from 7FFD
            {
                uint16_t page = (_state->p7FFD & 7) | (fff7 & 0xF8 & ramMask);
                SetRAMPageToBank(_memory, bank, page);
                break;
            }
            case 0x100:  // ROM from 7FFD (page LSB toggles the TR-DOS half)
            {
                uint16_t page = (fff7 & 0xFE & romMask) + (trdos ? 1 : 0);
                _memory->SetROMPageToBank(bank, page);
                break;
            }
            case 0x200:  // RAM from FFF7
            {
                uint16_t page = fff7 & 0xFF & ramMask;
                SetRAMPageToBank(_memory, bank, page);
                break;
            }
            case 0x300:  // ROM from FFF7
            default:
            {
                uint16_t page = fff7 & 0xFF & romMask;
                _memory->SetROMPageToBank(bank, page);
                break;
            }
        }
    }

    // ATM3: during NMI handling window 0 is forced to the top RAM page
    if (config.mem_model == MM_ATM3 && _state->nmi_in_progress)
    {
        _memory->SetRAMPageToBank0(0xFF);
    }
}

void PortDecoder_ATM710::updateTurboMode()
{
    // ZX-Evo / ATM turbo select (reference: Xpeccy pentevo.c evoOut77d):
    //   pFF77.3 = 1             -> x4 (14MHz turbo)
    //   pFF77.3 = 0, pEFF7.4 = 0 -> x2 (7MHz, hardware default)
    //   pFF77.3 = 0, pEFF7.4 = 1 -> x1 (3.5MHz compatibility)
    // The queued multiplier is applied by Z80::Z80FrameCycle at the next
    // frame boundary; SoundManager::handleFrameStart re-clocks the synths
    // (blip input clocks x multiplier, AY PLL increment / multiplier) so
    // audio stays realtime with unchanged AY pitch (fixed PSG clock)
    uint8_t multiplier;
    if (_state->pFF77 & ATM_FF77_TURBO)
        multiplier = 4;  // 14MHz
    else if (!(_state->pEFF7 & ATM_EFF7_TURBO_3_5))
        multiplier = 2;  // 7MHz
    else
        multiplier = 1;  // 3.5MHz

    _state->next_z80_frequency_multiplier = multiplier;

    MLOGDEBUG("updateTurboMode: multiplier=%d (pFF77=0x%02X pEFF7=0x%02X)", multiplier, _state->pFF77, _state->pEFF7);
}

void PortDecoder_ATM710::atmMemSwap()
{
    if (!_memory || !_context)
        return;

    // Toggle swap state
    _state->atmMemSwapped = !_state->atmMemSwapped;

    // Swap address bits A5-A7 with A8-A10 in all RAM
    // This is a physical RAM reorganization
    uint8_t* ramBase = _memory->RAMBase();
    size_t ramSize = _context->config.ramsize * 1024;  // config.ramsize is in KB

    // Process each 2KB block
    uint8_t buffer[2048];
    for (size_t page = 0; page < ramSize; page += 2048)
    {
        uint8_t* bank = ramBase + page;

        for (unsigned addr = 0; addr < 2048; addr++)
        {
            // Swap: A10 A9 A8 A7 A6 A5 A4 A3 A2 A1 A0
            //    -> A7 A6 A5 A10 A9 A8 A4 A3 A2 A1 A0
            unsigned newAddr = (addr & 0x1F)           // Keep A0-A4
                             | ((addr >> 3) & 0xE0)    // Move A8-A10 to A5-A7
                             | ((addr << 3) & 0x700);  // Move A5-A7 to A8-A10
            buffer[addr] = bank[newAddr];
        }
        memcpy(bank, buffer, 2048);
    }

    MLOGINFO("atmMemSwap: Memory swap performed, state=%d", _state->atmMemSwapped);
}

/// endregion </Port handlers>

/// region <Debug methods>

std::string PortDecoder_ATM710::Dump_7FFD_value(uint8_t value)
{
    std::string result;

    uint8_t ramPage = value & 0x07;
    bool shadowScreen = value & 0x08;
    bool romBank = value & 0x10;
    bool locked = value & 0x20;

    result = StringHelper::Format("RAM:%d SCR:%d ROM:%d LOCK:%d",
                                  ramPage, shadowScreen ? 1 : 0, romBank ? 1 : 0, locked ? 1 : 0);
    return result;
}

std::string PortDecoder_ATM710::Dump_FF77_value(uint8_t value)
{
    uint8_t videoMode = value & ATM_FF77_VMODE_MASK;

    const char* modeNames[] = { "EGA", "?", "HwMC", "ZX", "?", "?", "Text", "?" };

    return StringHelper::Format("SWAP:%d VMODE:%d(%s) TURBO:%d INT:%d",
                                (value & ATM_FF77_MEMSWAP) ? 1 : 0,
                                videoMode, modeNames[videoMode & 7],
                                (value & ATM_FF77_TURBO) ? 1 : 0,
                                (value & ATM_FF77_INTGATE) ? 1 : 0);
}

std::string PortDecoder_ATM710::Dump_FFF7_value(unsigned value)
{
    uint8_t memType = (value >> 8) & 0x03;
    uint8_t pageNum = value & 0xFF;

    const char* typeNames[] = { "RAM/7FFD", "ROM/7FFD", "RAM/FFF7", "ROM/FFF7" };

    return StringHelper::Format("TYPE:%s PAGE:%d", typeNames[memType], pageNum);
}

std::string PortDecoder_ATM710::Dump_EFF7_value(uint8_t value)
{
    return StringHelper::Format("TURBO3.5:%d LOCKMEM:%d ROCACHE:%d",
                                (value & ATM_EFF7_TURBO_3_5) ? 1 : 0,
                                (value & ATM_EFF7_LOCKMEM) ? 1 : 0,
                                (value & ATM_EFF7_ROCACHE) ? 1 : 0);
}

/// endregion </Debug methods>
