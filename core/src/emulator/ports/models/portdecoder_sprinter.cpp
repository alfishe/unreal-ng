#include "stdafx.h"

#include "common/modulelogger.h"

#include "portdecoder_sprinter.h"

#include <cstring>

#include "debugger/ttd/ttdds12887.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/sprinter/sprintermemory.h"
#include "emulator/ports/models/sprinter/sprinterpldconfig.h"
#include "emulator/video/screen.h"

namespace
{
/// Cells after power-on (MAME sprinter.cpp machine_start, port_default):
/// #Cx 0, #Dx = #10-#1F, #Ex mostly #41 (#E9 = 5, #EA = 2, #EC = #FF), #Fx = #00-#0F
constexpr uint8_t kCellsPowerOn[64] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x00, 0x05, 0x02, 0x41, 0xFF, 0x00, 0x00, 0x41,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
};

/// "ACEX_30K_LOADING" at fast RAM #FEF0: the BIOS left a new bitstream at #1000 (loader 3.04 #003B)
constexpr char kReloadSignature[16] = {'A', 'C', 'E', 'X', '_', '3', '0', 'K', '_', 'L', 'O', 'A', 'D', 'I', 'N', 'G'};

/// The loader's stream origin in CPU addresses (ROM page #C #0100, or fast RAM #1000 on a reload)
constexpr uint16_t kRomStreamStart = 0x0100;
constexpr uint16_t kFastRamStreamStart = 0x1000;
}  // namespace

/// region <Constructors / Destructors>

PortDecoder_Sprinter::PortDecoder_Sprinter(EmulatorContext* context) : PortDecoder(context)
{
    _rtc.SetCenturyRegister(0x32);
    _rtc.SetEmulatedClock([this]() { return EmulatedMicroseconds(); });

    _vram.SetIntModeListener([this]() { _intSource.Invalidate(); });

    // The Z84C15's CTC counts the CPU clock: base T-states x the current ratio
    _z84.ctc.SetClock([this]() -> uint64_t {
        const Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
        const uint32_t multiplier = _state->current_z80_frequency_multiplier ? _state->current_z80_frequency_multiplier : 1;
        return _state->t_states * multiplier + (z80 ? z80->t : 0);
    });

    // Core creates SprinterMemory for this model; the windows are mapped from our state
    _sprinterMemory = dynamic_cast<SprinterMemory*>(_memory);
    if (_sprinterMemory)
        _sprinterMemory->AttachDecoder(this);
    else
        MLOGWARNING("PortDecoder_Sprinter: the memory subsystem is not SprinterMemory - windows stay at the loader layout");

    if (_context->pCore && _context->pCore->GetZ80())
        _waits = std::make_unique<SprinterWaits>(_context->pCore->GetZ80());
}

PortDecoder_Sprinter::~PortDecoder_Sprinter()
{
    Core* core = _context->pCore;
    if (core)
    {
        if (_sprinterMemory)
            core->RemoveBusOverlay(&_sprinterMemory->GetWriteIntercept());
        if (_waits)
            core->RemoveBusOverlay(_waits.get());
        Z80* z80 = core->GetZ80();
        if (z80 && z80->GetInterruptSource() == &_intSource)
            z80->SetInterruptSource(nullptr);
        if (z80 && z80->GetMachineStepHook() == this)
            z80->SetMachineStepHook(nullptr);
        if (z80 && z80->machineM1Hook == this)
            z80->machineM1Hook = nullptr;
    }

    if (_sprinterMemory)
        _sprinterMemory->AttachDecoder(nullptr);

    // Battery-backed state outlives the machine ([SPRINTER] CmosFile)
    const char* cmosPath = _context->config.sprinter.cmos_path;
    if (_cmosLoaded && cmosPath[0] != '\0' && !_rtc.SaveNvram(cmosPath))
        MLOGWARNING("PortDecoder_Sprinter: cannot save the CMOS to '%s'", cmosPath);
}

/// endregion </Constructors / Destructors>

/// region <Resets>

void PortDecoder_Sprinter::PowerOn()
{
    std::memcpy(_pld.cells, kCellsPowerOn, sizeof(_pld.cells));
    _pld.allMode = 0;
    _pld.portY = 0;
    _pld.rgMod = 0;
    _pld.hold = 0;
    _pld.turbo = 0;
    _pld.configModule = 0;
    _pld.configState = SprinterConfigState::Unconfigured;
    _z84.PowerOn();
    _vram.Clear();
    _intSource.SetModePage(0);

    // The CMOS cells come from [SPRINTER] CmosFile once, at power-on
    if (!_cmosLoaded)
    {
        _cmosLoaded = true;
        const char* cmosPath = _context->config.sprinter.cmos_path;
        if (cmosPath[0] != '\0' && !_rtc.LoadNvram(cmosPath))
            MLOGINFO("PortDecoder_Sprinter: no CMOS file at '%s' yet, starting blank", cmosPath);
    }

    _poweredOn = true;
}

void PortDecoder_Sprinter::reset()
{
    const bool powerOn = !_poweredOn;
    if (powerOn)
        PowerOn();

    // Core::Reset has reset the Z80 already; the RESET button reloads the PLD
    _z84.Reset();
    ResetPld(powerOn ? SprinterResetKind::PowerOn : SprinterResetKind::Button);
    InstallHooks();

    if (_context->config.sprinter.fast_start)
        FastStart();
    else
        BeginLoading();
}

void PortDecoder_Sprinter::PowerCycle()
{
    _poweredOn = false;
    reset();
}

void PortDecoder_Sprinter::ResetPld(SprinterResetKind kind)
{
    // MAME machine_reset: the cells, ALL_MODE, PORT_Y, RGMOD and HOLD keep their values
    _pld.starting = 1;
    _pld.dos = 1;
    _pld.romOff = 0;
    _pld.ramSys = 0;
    _pld.sysPg = 0;
    _pld.arom16 = 0;
    _pld.cnf = 0;
    _pld.pn = 0;
    _pld.sc = 0;
    _pld.romRg = 0;
    _pld.cacheOn = 0;
    _pld.isaAddrExt = 0;
    _pld.ideChannel = 0;
    _pld.ideLatch = 0;
    _pld.frameLines = 0;
    _pld.turboHard = _context->config.sprinter.turbo_allowed ? 1 : 0;
    if (kind != SprinterResetKind::SoftReset)
        _pld.turbo = 0;  // a new configuration starts at 3.5 MHz
    _pld.resetPending = 0;
    _cblControl = 0;
    _dcpOpenedFrame = -1;

    _intSource.Reset();
    _intSource.SetFrameLines(320);

    ActiveModule().OnReset(kind, _pld);
}

void PortDecoder_Sprinter::ResetCpu()
{
    Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    if (!z80)
        return;

    z80->pc = 0x0000;
    z80->ir_ = 0;
    z80->int_flags = 0;  // IFF1, IFF2, HALT
    z80->im = 0;
    z80->boundary = Z80_BOUNDARY_NONE;
    z80->ClearInterruptRequests();
}

void PortDecoder_Sprinter::BeginLoading()
{
    SprinterPldConfig::Begin(_pld);
    _pld.configModule = static_cast<uint8_t>(SprinterPldConfigurationRegistry::kStandardIndex);
    _pld.turbo = 0;
    ApplyTurbo();
    RefreshStepHook();
    UpdateBanks();
}

void PortDecoder_Sprinter::FastStart()
{
    // What the 3.04 loader does before its stream (#0000-#0087): the Z84C15
    // system registers (WCR = 4, MCR = 3, CSBR = #FE), and the hand-over
    // registers the BIOS reads (IY = #0107, IX = #FFFD; page 8 #02B3)
    _z84.system.Write(0xEE, 0x00);
    _z84.system.Write(0xEF, 0x04);
    _z84.system.Write(0xEE, 0x03);
    _z84.system.Write(0xEF, 0x03);
    _z84.system.Write(0xEE, 0x02);
    _z84.system.Write(0xEF, 0xFE);
    if (Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr)
    {
        z80->iy = 0x0107;
        z80->ix = 0xFFFD;
    }

    SprinterPldConfig::Begin(_pld);
    LoadFastRamImage();
    FinishLoad(false);
    PerformPendingReset();
}

/// The loader's 473 720 writes without running it: the same values into the
/// sink (both hashes) and into fast RAM (#FE00-#FEFF, or #FD00-#FDFF when fast
/// RAM #FEE0 holds "IM")
void PortDecoder_Sprinter::LoadFastRamImage()
{
    if (!_sprinterMemory)
        return;

    uint8_t* fastRam = _sprinterMemory->FastRam();
    const bool reload = std::memcmp(fastRam + 0xFEF0, kReloadSignature, sizeof(kReloadSignature)) == 0;
    const uint8_t* stream = reload ? fastRam + kFastRamStreamStart
                                   : _sprinterMemory->ROMPageHostAddress(SprinterMemory::kLoaderRomPage) + kRomStreamStart;
    const size_t available = reload ? 0x10000u - kFastRamStreamStart : 4u * PAGE_SIZE - kRomStreamStart;
    const uint16_t destination = (fastRam[0xFEE0] == 'I' && fastRam[0xFEE1] == 'M') ? 0xFD00 : 0xFE00;

    const size_t bytes = SprinterPldConfig::kPldConfigurationWrites / 8;
    for (size_t i = 0; i < bytes; i++)
    {
        uint8_t value = i < available ? stream[i] : 0xFF;
        const size_t cell = destination + (i & 0xFF);
        for (int bit = 0; bit < 8; bit++)
        {
            fastRam[cell] = value;
            SprinterPldConfig::OnWrite(_pld, value);
            value = static_cast<uint8_t>((value >> 1) | (value << 7));  // RRCA
        }
    }
}

void PortDecoder_Sprinter::OnConfigurationWrite(uint8_t value)
{
    if (_pld.configState != SprinterConfigState::Loading)
        return;
    if (SprinterPldConfig::OnWrite(_pld, value))
        FinishLoad(false);
}

/// The stream ended (or the watchdog fired): pick the module, then the PLD
/// resets the CPU into the BIOS at the next instruction boundary
void PortDecoder_Sprinter::FinishLoad(bool watchdog)
{
    int index = watchdog ? -1 : _registry.Find(_pld.bitstreamHashFull, _pld.bitstreamHashHead);
    if (watchdog)
    {
        MLOGWARNING("PortDecoder_Sprinter: the PLD load stopped after %u of %u writes - watchdog, using Standard",
                    _pld.bitstreamCount, SprinterPldConfig::kPldConfigurationWrites);
    }
    else if (index < 0)
    {
        MLOGWARNING("PortDecoder_Sprinter: unknown PLD bitstream, full hash %08X, head hash %08X, using Standard",
                    _pld.bitstreamHashFull, _pld.bitstreamHashHead);
    }
    if (index < 0)
        index = static_cast<int>(SprinterPldConfigurationRegistry::kStandardIndex);

    _pld.configModule = static_cast<uint8_t>(index);
    RequestCpuReset(SprinterResetKind::Configured);
}

void PortDecoder_Sprinter::RequestCpuReset(SprinterResetKind kind)
{
    _pld.resetPending = static_cast<uint8_t>(static_cast<uint8_t>(kind) + 1);
    RefreshStepHook();
}

void PortDecoder_Sprinter::PerformPendingReset()
{
    if (!_pld.resetPending)
        return;

    const auto kind = static_cast<SprinterResetKind>(_pld.resetPending - 1);
    _pld.resetPending = 0;

    switch (kind)
    {
        case SprinterResetKind::Configured:
            _pld.configState = SprinterConfigState::Configured;
            ActiveModule().OnActivate(_pld);
            ResetPld(kind);
            ResetCpu();
            _z84.Reset();
            ApplyTurbo();
            UpdateBanks();
            break;
        case SprinterResetKind::Reload:
            ResetPld(kind);
            ResetCpu();
            _z84.Reset();
            BeginLoading();
            break;
        default:  // SoftReset: CPU reset only, the PLD stays configured
            ResetPld(kind);
            ResetCpu();
            _z84.Reset();
            ApplyTurbo();
            UpdateBanks();
            break;
    }
    RefreshStepHook();
}

/// endregion </Resets>

/// region <Hooks>

void PortDecoder_Sprinter::InstallHooks()
{
    Core* core = _context->pCore;
    if (!core || !core->GetZ80())
        return;

    Z80* z80 = core->GetZ80();
    if (z80->GetInterruptSource() != &_intSource)
        z80->SetInterruptSource(&_intSource);
    z80->machineM1Hook = this;
    if (_sprinterMemory)
        core->AddBusOverlay(&_sprinterMemory->GetWriteIntercept());
    if (!_waits)
        _waits = std::make_unique<SprinterWaits>(z80);
}

/// The step hook runs only while a load is in progress (the watchdog) or a CPU
/// reset waits for the instruction boundary: other machines and the configured
/// Sprinter pay nothing per instruction
void PortDecoder_Sprinter::RefreshStepHook()
{
    Core* core = _context->pCore;
    if (!core || !core->GetZ80())
        return;

    Z80* z80 = core->GetZ80();
    const bool needed = _pld.resetPending || _pld.configState == SprinterConfigState::Loading;
    if (needed)
        z80->SetMachineStepHook(this);
    else if (z80->GetMachineStepHook() == this)
        z80->SetMachineStepHook(nullptr);
}

void PortDecoder_Sprinter::OnMachineStep([[maybe_unused]] uint32_t t)
{
    if (_pld.resetPending)
        PerformPendingReset();
}

void PortDecoder_Sprinter::BeforeMachineM1(uint16_t address)
{
    if (_pld.configState != SprinterConfigState::Configured)
        return;
    if (address >= 0x4000)
    {
        if (!_pld.dos)
        {
            _pld.dos = 1;
            UpdateBanks();
        }
    }
    else if ((address & 0xFF00) == 0x3D00 && _pld.dos && (_pld.pn & 0x10))
    {
        _pld.dos = 0;
        UpdateBanks();
    }
}

void PortDecoder_Sprinter::OnMachineFrameRollover([[maybe_unused]] uint32_t frameLength)
{
    if (_pld.configState == SprinterConfigState::Loading && !_pld.resetPending && SprinterPldConfig::OnFrame(_pld))
        FinishLoad(true);
}

void PortDecoder_Sprinter::ApplyTurbo()
{
    const uint8_t ratio = (_pld.turbo && _pld.turboHard) ? 6 : 1;
    Core* core = _context->pCore;
    if (_state->hw_turbo_ratio != ratio)
    {
        _state->hw_turbo_ratio = ratio;
        if (core && core->GetZ80())
            core->GetZ80()->ApplyHardwareTurboNow();
    }

    // Turbo waits (technical-design §4): an overlay only while the CPU runs at 21 MHz
    if (core && _waits)
    {
        if (ratio > 1)
        {
            OnBanksChanged();
            core->AddBusOverlay(_waits.get());
        }
        else
        {
            core->RemoveBusOverlay(_waits.get());
        }
    }
}

void PortDecoder_Sprinter::AddPortWait()
{
    if (_state->hw_turbo_ratio <= 1)
        return;
    Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    if (z80)
        z80->AddWaitStates(SprinterWaits::Rule(z80->AccessStartClock(), SprinterWaits::kPortTaken));
}

void PortDecoder_Sprinter::OnBanksChanged()
{
    if (!_waits)
        return;
    // Main RAM waits; ROM and fast RAM do not (MAME: only ram_r / ram_w / isa_r / isa_w call do_mem_wait)
    for (uint8_t bank = 0; bank < 4; bank++)
        _waits->SetSlotWaits(bank, _memory->GetMemoryBankMode(bank) == BANK_RAM);
}

/// endregion </Hooks>

/// region <Port table>

uint16_t PortDecoder_Sprinter::LookupIndex(uint16_t port, bool isRead) const
{
    return static_cast<uint16_t>(((_pld.cnf >> 3) & 0x03) << 12      // map 0-3
                                 | ((_pld.pn >> 5) & 0x01) << 11      // PN5 (#7FFD bit 5)
                                 | (_pld.dos ? 1 : 0) << 10           // /DOS
                                 | (isRead ? 1 : 0) << 9              // /WR
                                 | ((port >> 14) & 0x03) << 7         // A15, A14
                                 | ((port >> 13) & 0x01) << 4         // A13
                                 | ((port >> 7) & 0x01) << 3          // A7
                                 | (port & 0x67));                    // A6, A5, A2, A1, A0
}

uint8_t PortDecoder_Sprinter::LookupCode(uint16_t port, bool isRead) const
{
    // The table is ordinary RAM page #40 (it never moves)
    return _memory->RAMPageAddress(SprinterMemory::kPortTablePage)[LookupIndex(port, isRead)];
}

uint8_t PortDecoder_Sprinter::ComputePg3(const SprinterPldState& pld)
{
    const bool cnf7 = (pld.cnf & 0x80) != 0;
    const bool extended = ((pld.sc & 0x10) && !cnf7) || (cnf7 && (pld.pn & 0x40));
    return static_cast<uint8_t>((((pld.pn & 0x80) ? 0 : 1) << 5) | 0x10 | (extended ? 0x08 : 0) | (pld.pn & 0x07));
}

void PortDecoder_Sprinter::UpdateBanks()
{
    _pld.pg3 = ComputePg3(_pld);
    _memory->UpdateZ80Banks();
}

/// endregion </Port table>

/// region <Port access>

uint8_t PortDecoder_Sprinter::DecodePortIn(uint16_t port, uint16_t pc)
{
    _pc = pc;
    // The PLD answers every cycle: an unmapped port reads #FF, never the floating bus
    _lastPortDecoded = true;

    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;
    disp.decodedPort = port;
    disp.wasDecoded = true;
    disp.wasHandledInline = true;

    const uint8_t low = static_cast<uint8_t>(port);
    uint8_t value = 0xFF;
    if (Z84C15::Owns(low))
    {
        // The Z84C15 decodes its own ports; the PLD does not see the read (MAME internal map)
        value = _z84.Read(low);
        disp.internalCode = static_cast<uint16_t>(kTraceZ84Base + low);
    }
    else if (_pld.configState == SprinterConfigState::Configured)
    {
        if (_pld.starting)
        {
            // The first IN opens the port decoder (hardware-reference §4.2); window 3
            // keeps page #40 until the next remap (MAME dcp_r)
            _pld.starting = 0;
            _dcpOpenedFrame = static_cast<int64_t>(_state->frame_counter);
            _dcpOpenedPc = pc;
        }
        if ((port & 0x7F) == 0x7B)
        {
            // Fast RAM in window 0: IN #FB on, IN #7B off ("nailed" before the table)
            _pld.cacheOn = (port >> 7) & 1;
            UpdateBanks();
        }
        AddPortWait();

        const uint8_t code = LookupCode(port, true);
        disp.internalCode = code;
        if (!ActiveModule().ReadCode(*this, code, port, value))
            _registry.Standard().ReadCode(*this, code, port, value);
    }

    OnPortInComplete(port, value, pc, disp);
    return value;
}

void PortDecoder_Sprinter::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    _pc = pc;

    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;
    disp.decodedPort = port;
    disp.wasDecoded = true;
    disp.wasHandledInline = true;

    const uint8_t low = static_cast<uint8_t>(port);
    const bool z84Port = Z84C15::Owns(low);
    if (z84Port)
    {
        _z84.Write(low, value);
        disp.internalCode = static_cast<uint16_t>(kTraceZ84Base + low);
        // A layout change of the loader's chip selects moves its fast RAM window
        if (_pld.configState != SprinterConfigState::Configured && (low == 0xEF))
            UpdateBanks();
    }

    // The PLD sees the Z84C15's writes as well (MAME write tap, sprinter.cpp:1445-1458;
    // unverified on the board, tdd-ports-memory §3.2); writes are ignored until the first IN
    if (_pld.configState == SprinterConfigState::Configured && !_pld.starting)
    {
        // The "nailed" decodes, before the table (MAME dcp_w)
        if ((port & 0xBF) == 0x3C)
        {
            _pld.romOff = (port & 0x40) ? 0 : 1;  // #3C: ROM out of window 0, #7C: ROM in
            if (!(value & 0x02))
                _pld.sysPg = ((_pld.romRg & 0x10) || (value & 0x01)) ? 1 : 0;
            UpdateBanks();
        }
        if (!_pld.romOff && low == 0x5C)
        {
            _pld.romRg = value;
            _pld.sysPg |= (_pld.romRg >> 4) & 1;
            UpdateBanks();
        }
        AddPortWait();

        const uint8_t code = LookupCode(port, false);
        if (!z84Port)
            disp.internalCode = code;
        if (code >= 0xC0 && code < 0xF0)
            _pld.Cell(code) = value;  // every cell is storage first
        if (!ActiveModule().WriteCode(*this, code, port, value))
            _registry.Standard().WriteCode(*this, code, port, value);
    }

    OnPortOutComplete(port, value, pc, disp);
}

uint8_t PortDecoder_Sprinter::FdcRead(uint8_t code)
{
    static constexpr uint16_t kFdcPorts[4] = {0x1F, 0x3F, 0x5F, 0x7F};
    return PeripheralPortIn(kFdcPorts[code & 3]);
}

void PortDecoder_Sprinter::FdcWrite(uint8_t code, uint8_t value)
{
    static constexpr uint16_t kFdcPorts[4] = {0x1F, 0x3F, 0x5F, 0x7F};
    PeripheralPortOut(kFdcPorts[code & 3], value);
}

/// The standard configuration's reads (hardware-reference §4.3; MAME dcp_r)
uint8_t PortDecoder_Sprinter::StandardReadCode(uint8_t code, uint16_t port)
{
    switch (code)
    {
        case SprinterCode::None:
            return 0xFF;

        case 0x10: case 0x11: case 0x12: case 0x13:
            return FdcRead(code);
        case SprinterCode::BetaState:
            return PeripheralPortIn(0xFF);  // DRQ / INTRQ; the joystick bits come with phase S3a

        case SprinterCode::CmosRead:
            return _rtc.ReadData();

        case SprinterCode::Keyboard:
            return Default_Port_FE_In(port, _pc);

        case SprinterCode::AyRead:
            return PeripheralPortIn(0xFFFD);

        case SprinterCode::KempstonMouse:
            return Default_Port_KempstonMouse_In(port, _pc);

        case SprinterCode::CovoxBlaster:
            return _cblControl;

        default:
            break;
    }

    if (code >= 0xC0 && code < 0xF0)
        return _pld.Cell(code);
    if (code >= 0xF0)
        return _pld.cells[_pld.pg3 & 0x3F];

    // IDE (#20-#29, phase S3b) and the codes the standard configuration does not answer
    return 0xFF;
}

/// The standard configuration's writes (hardware-reference §4.3; MAME dcp_w).
/// The cells #C0-#EF are stored already
void PortDecoder_Sprinter::StandardWriteCode(uint8_t code, uint16_t port, uint8_t value)
{
    switch (code)
    {
        case SprinterCode::None:
            return;

        case 0x10: case 0x11: case 0x12: case 0x13:
            FdcWrite(code, value);
            return;
        case SprinterCode::BetaSystem:
            PeripheralPortOut(0xFF, value);
            return;
        case SprinterCode::DensityDD:
        case SprinterCode::DensityHD:
            return;  // the #BD density latch is wired to the WD1793 in phase S3a

        case SprinterCode::IsaControl:
            _pld.isaAddrExt = value & 0x3F;
            return;
        case SprinterCode::CmosAddress:
            _rtc.WriteAddress(value);
            return;
        case SprinterCode::CmosWrite:
            _rtc.WriteData(value);
            return;

        case SprinterCode::IdeSecondary:
            _pld.ideChannel = 1;
            return;
        case SprinterCode::IdePrimary:
            _pld.ideChannel = 0;
            return;
        case SprinterCode::Frame320:
        case SprinterCode::Frame312:
            // The frame length itself follows with the renderer (phase S2); the INT list uses it now
            _pld.frameLines = code & 1;
            _intSource.SetFrameLines(_pld.frameLines ? 312 : 320);
            return;
        case SprinterCode::PldReload:
            RequestCpuReset(SprinterResetKind::Reload);
            return;

        case SprinterCode::Covox:
            return;  // Covox / Covox-Blaster samples: phase S6
        case SprinterCode::CovoxBlaster:
            _cblControl = value;
            return;

        case SprinterCode::RomPage:
            _pld.romRg = value;
            _pld.sysPg = ((_pld.romRg & 0x10) || (value & 0x01)) ? 1 : 0;
            UpdateBanks();
            return;

        case SprinterCode::AyAddress:
            PeripheralPortOut(0xFFFD, value);
            return;
        case SprinterCode::AyData:
            PeripheralPortOut(0xBFFD, value);
            return;

        case SprinterCode::Port1FFD:
        case 0xC8:
            _pld.sc = (_pld.cnf & 0x40) ? 0 : value;  // CNF bit 6: "SC clean"
            UpdateBanks();
            return;
        case SprinterCode::Port7FFD:
        case 0xC9:
            _pld.pn = value;
            if (!(_pld.cnf & 0x80))
                _pld.pn &= 0x3F;  // CNF_PN[7..6]_CLEAN
            if (!(_pld.cnf & 0x80) && (_pld.cnf & 0x20))
                _pld.pn &= 0xDF;  // CNF_PN[5]_CLEAN
            if (_pld.cnf & 0x20)
                _pld.pn &= 0xE0;  // CNF_PN[4..0]_CLEAN
            UpdateBanks();
            return;
        case SprinterCode::Border:
            Default_Port_FE_Out(port, value, _pc);
            return;
        case SprinterCode::AllMode:
            _pld.allMode = value;
            return;
        case SprinterCode::Hold:
            _pld.hold = value;
            return;
        case SprinterCode::PortY:
        case 0xCC:
            _pld.portY = value;
            return;
        case SprinterCode::RgMod:
        case 0xCD:
            _pld.rgMod = value;
            _intSource.SetModePage(value & 1);
            return;
        case SprinterCode::SysCnf:
        case 0xCE:
            _pld.ramSys = (port & 0x40) ? 0 : 1;  // #24 / #3C: 1, #74 / #7C: 0
            if (value & 0x02)
            {
                _pld.turbo = value & 1;
                ApplyTurbo();
            }
            else
            {
                _pld.arom16 = value & 1;
            }
            if (value & 0x04)
            {
                _pld.cnf = value;
                if (_pld.cnf & 0x40)
                    _pld.sc = 0;
                if (!(_pld.cnf & 0x80))
                    _pld.pn &= 0x3F;
                if (!(_pld.cnf & 0x80) && (_pld.cnf & 0x20))
                    _pld.pn &= 0xDF;
                if (_pld.cnf & 0x20)
                    _pld.pn &= 0xE0;
            }
            UpdateBanks();
            return;
        case SprinterCode::Scale:
        case 0xCF:
            return;  // accelerator alternate addressing: phase S5
        default:
            break;
    }

    if (code >= 0xD0 && code < 0xF0)
    {
        UpdateBanks();  // page cells
        return;
    }
    if (code >= 0xF0)
    {
        _pld.cells[_pld.pg3 & 0x3F] = value;  // the cell of the current Spectrum page (hardware-reference §3.2)
        UpdateBanks();
        return;
    }
    if (code >= 0xC0)
        return;  // plain storage cells (#CA ...)

    // IDE (#20-#29, phase S3b) and unknown codes: ignored, logged once per code
    uint64_t& bits = _loggedUnknownCodes[code >> 6];
    const uint64_t bit = 1ull << (code & 63);
    if (!(bits & bit))
    {
        bits |= bit;
        MLOGDEBUG("PortDecoder_Sprinter: OUT #%04X <- #%02X, code #%02X not handled", port, value, code);
    }
}

/// endregion </Port access>

/// region <Surfaces>

std::vector<ttd::PeripheralId> PortDecoder_Sprinter::GetTTDModelStateIds() const
{
    // The PLD blob comes with phase S7: declaring it makes TTD refuse to record
    // this machine until then instead of recording a state it cannot restore
    return {ttd::PeripheralId::SprinterPld, ttd::PeripheralId::Ds12887};
}

std::vector<std::unique_ptr<ttd::TTDSerializable>> PortDecoder_Sprinter::CreateTTDSerializers() const
{
    std::vector<std::unique_ptr<ttd::TTDSerializable>> serializers;
    serializers.push_back(std::make_unique<ttd::TTDDs12887>(const_cast<Ds12887&>(_rtc)));
    return serializers;
}

std::vector<PortTraceCodeName> PortDecoder_Sprinter::GetPortTraceCodeTable() const
{
    std::vector<PortTraceCodeName> table = {
        {0x00, "None"},
        {0x10, "FdcCommand"}, {0x11, "FdcTrack"}, {0x12, "FdcSector"}, {0x13, "FdcData"},
        {0x14, "BetaSystem"}, {0x15, "BetaState"}, {0x16, "Density720K"}, {0x17, "Density1440K"},
        {0x1B, "IsaControl"}, {0x1C, "CmosRead"}, {0x1D, "CmosAddress"}, {0x1E, "CmosWrite"},
        {0x20, "IdeData"}, {0x21, "IdeError"}, {0x22, "IdeCount"}, {0x23, "IdeSector"}, {0x24, "IdeCylLow"},
        {0x25, "IdeCylHigh"}, {0x26, "IdeHead"}, {0x27, "IdeCommand"}, {0x28, "IdeAltStatus"},
        {0x29, "IdeDriveAddress"}, {0x2A, "IdeSecondary"}, {0x2B, "IdePrimary"}, {0x2C, "Frame320"},
        {0x2D, "Frame312"}, {0x2E, "PldReload"}, {0x2F, "Code2F"}, {0x32, "IsaControl32"},
        {0x40, "Keyboard"}, {0x52, "AyRead"}, {0x58, "KempstonMouse"}, {0x88, "Covox"}, {0x89, "CovoxBlaster"},
        {0x8F, "RomPage"}, {0x90, "AyAddress"}, {0x91, "AyData"},
        {0xC0, "1FFD"}, {0xC1, "7FFD"}, {0xC2, "Border"}, {0xC3, "AllMode"}, {0xC4, "PortY"}, {0xC5, "RgMod"},
        {0xC6, "SysCnf"}, {0xC7, "Scale"}, {0xC8, "1FFD'"}, {0xC9, "7FFD'"}, {0xCB, "Hold"}, {0xCC, "PortY'"},
        {0xCD, "RgMod'"}, {0xCE, "SysCnf'"}, {0xCF, "Scale'"},
    };
    for (uint16_t cell = 0xD0; cell <= 0xDF; cell++)
        table.push_back({cell, "Cell" + StringHelper::Format("%02X", cell)});
    static const char* const kRomCells[8] = {"VRom0", "VRom1", "VRom2", "VRom3", "VRom4", "VRom5", "VRom6", "VRom7"};
    for (uint16_t i = 0; i < 8; i++)
        table.push_back({static_cast<uint16_t>(0xE0 + i), kRomCells[i]});
    table.push_back({0xE8, "Page0"});
    table.push_back({0xE9, "Page1"});
    table.push_back({0xEA, "Page2"});
    for (uint16_t cell = 0xEB; cell <= 0xEF; cell++)
        table.push_back({cell, "Cell" + StringHelper::Format("%02X", cell)});
    for (uint16_t cell = 0xF0; cell <= 0xFF; cell++)
        table.push_back({cell, "Page3"});

    static const struct
    {
        uint8_t port;
        const char* name;
    } kZ84[] = {
        {0x10, "Z84 CTC0"}, {0x11, "Z84 CTC1"}, {0x12, "Z84 CTC2"}, {0x13, "Z84 CTC3"},
        {0x18, "Z84 SIO A data"}, {0x19, "Z84 SIO A control"}, {0x1A, "Z84 SIO B data"}, {0x1B, "Z84 SIO B control"},
        {0x1C, "Z84 PIO A data"}, {0x1D, "Z84 PIO A control"}, {0x1E, "Z84 PIO B data"}, {0x1F, "Z84 PIO B control"},
        {0xEE, "Z84 SCRP"}, {0xEF, "Z84 SCDP"}, {0xF0, "Z84 WDTMR"}, {0xF1, "Z84 WDTCR"}, {0xF4, "Z84 IRQ priority"},
    };
    for (const auto& z84 : kZ84)
        table.push_back({static_cast<uint16_t>(kTraceZ84Base + z84.port), z84.name});
    return table;
}

PortDecoder::RtcBinding PortDecoder_Sprinter::GetRtcBinding()
{
    RtcBinding binding;
    binding.chip = &_rtc;
    binding.ports = "#DFBD address, #BFBD data write, #FFBD data read (port table codes #1D, #1E, #1C)";
    binding.nvramFile = _context->config.sprinter.cmos_path;
    return binding;
}

/// endregion </Surfaces>
