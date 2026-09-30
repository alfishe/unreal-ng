#include "stdafx.h"

#include "common/modulelogger.h"

#include "portdecoder_tsconf.h"

#include <cstring>

#include "debugger/ttd/tsconf/ttdtsconfstate.h"
#include "debugger/ttd/ttdds12887.h"
#include "emulator/cpu/core.h"
#include "emulator/memory/tsconf/tsconfmemory.h"
#include "emulator/platforms/tsconf/tsconfcraminit.h"
#include "emulator/video/screen.h"

/// region <Constructors / Destructors>

PortDecoder_TSConf::PortDecoder_TSConf(EmulatorContext* context) : PortDecoder(context)
{
    _evoAvr.SetEmulatedClock([this]() { return EmulatedMicroseconds(); });

    // Core creates TsConfMemory for this model; the windows are mapped from our state
    _tsMemory = dynamic_cast<TsConfMemory*>(_memory);
    if (_tsMemory)
        _tsMemory->AttachState(&_ts);
    else
        MLOGWARNING("PortDecoder_TSConf: the memory subsystem is not TsConfMemory - windows stay at the reset layout");
}

PortDecoder_TSConf::~PortDecoder_TSConf()
{
    Core* core = _context->pCore;
    if (core)
    {
        core->RemoveBusOverlay(&_fmWindow);
        core->RemoveBusOverlay(&_cacheSnoop);
        Z80* z80 = core->GetZ80();
        if (z80 && z80->machineM1Hook == this)
            z80->machineM1Hook = nullptr;
        if (z80 && z80->GetInterruptSource() == &_interrupts)
            z80->SetInterruptSource(nullptr);
        if (z80 && z80->GetMachineStepHook() == &_engine)
            z80->SetMachineStepHook(nullptr);
    }

    if (_tsMemory)
    {
        _tsMemory->SetCacheActive(false);
        _tsMemory->AttachState(nullptr);
    }

    // Battery-backed state outlives the machine ([EVO] NvramFile, shared with ATM3)
    const char* nvramPath = _context->config.atm.evo_nvram_path;
    if (_nvramLoaded && nvramPath[0] != '\0' && !_evoAvr.SaveNvram(nvramPath))
        MLOGWARNING("PortDecoder_TSConf: cannot save the ZX-Evo NVRAM to '%s'", nvramPath);
}

/// endregion </Constructors / Destructors>

/// region <Reset>

void PortDecoder_TSConf::PowerOn()
{
    // Registers the Z80 reset leaves alone start at 0 (FPGA configuration)
    std::memset(&_ts, 0, sizeof(_ts));
    std::memcpy(_ts.cram, kTsConfCramPowerOn, sizeof(_ts.cram));
    _ts.pwrUp = 1;
    _poweredOn = true;
}

/// Warm reset (hardware-spec §10). Not reset: BORDER, T_MAP_PAGE, T0/T1_G_PAGE,
/// SG_PAGE, the T0/T1 offsets, CRAM, SFILE, the cache contents, the FM
/// window address nibble
void PortDecoder_TSConf::reset()
{
    if (!_poweredOn)
        PowerOn();

    uint8_t* r = _ts.regs;
    r[TsConfReg::VConfig] = 0x00;
    r[TsConfReg::VPage] = 0x05;
    r[TsConfReg::GXOffsL] = 0x00;
    r[TsConfReg::GXOffsH] = 0x00;
    r[TsConfReg::GYOffsL] = 0x00;
    r[TsConfReg::GYOffsH] = 0x00;
    r[TsConfReg::TConfig] = 0x00;
    r[TsConfReg::PalSel] = 0x0F;
    r[TsConfReg::Page0] = 0x00;
    r[TsConfReg::Page1] = 0x05;
    r[TsConfReg::Page2] = 0x02;
    r[TsConfReg::Page3] = 0x00;
    r[TsConfReg::FMaps] &= 0x0F;                // MEN cleared, address kept
    r[TsConfReg::SysConfig] = 0x00;
    r[TsConfReg::MemConfig] = TsConfMemConfig::Reset;
    r[TsConfReg::HsInt] = 0x01;
    r[TsConfReg::VsIntL] = 0x00;
    r[TsConfReg::VsIntH] = 0x00;
    r[TsConfReg::FddVirt] = 0x00;
    r[TsConfReg::IntMask] = 0x01;
    r[TsConfReg::CacheConfig] = 0x00;

    _interrupts.Reset();
    _ts.eff7 = 0;
    _ts.dos = 0;
    _ts.vdos = 0;
    _ts.lock48 = 0;
    _ts.opcodeLatch128 = 0;

    // Shared latches the generic surfaces show (debugger, snapshots)
    _state->p7FFD = 0x00;
    _state->pFE = 0xFF;
    _state->border_attr = 0x07;
    _state->hw_turbo_ratio = 1;
    _engine.Reset();

    // The battery-backed NVRAM comes from [EVO] NvramFile once, at power-on;
    // a Z80 reset does not touch the AVR
    if (!_nvramLoaded)
    {
        _nvramLoaded = true;
        const char* nvramPath = _context->config.atm.evo_nvram_path;
        if (nvramPath[0] != '\0' && !_evoAvr.LoadNvram(nvramPath))
            MLOGINFO("PortDecoder_TSConf: no ZX-Evo NVRAM at '%s' yet, starting blank", nvramPath);
    }

    if (_screen)
        _screen->SetBorderColor(COLOR_WHITE);

    ApplyState();
}

void PortDecoder_TSConf::ApplyState()
{
    InstallInterrupts();
    _engine.RebuildLineTable();
    UpdateBanks();
    RefreshM1Hook();
    RefreshFmWindow();
    RefreshCache();
    ApplyClock();
    ApplyVideoPage();
}

/// endregion </Reset>

/// region <Port decode>

PortDecoder_TSConf::PortArm PortDecoder_TSConf::ClassifyPort(uint16_t port) const
{
    const uint8_t low = static_cast<uint8_t>(port);
    const bool fdcOpen = _ts.dos || (_ts.regs[TsConfReg::FddVirt] & 0x80);

    switch (low)
    {
        case 0xAF:
            return PortArm::TsRegister;
        case 0xFD:
            return (port & 0x8000) ? PortArm::Ay : PortArm::Paging7FFD;
        case 0xFE:
            return PortArm::KeyboardBorder;
        case 0xFB:
            return PortArm::Covox;
        case 0x1F:
            return fdcOpen ? PortArm::Fdc : PortArm::Joystick;
        case 0x3F:
        case 0x5F:
        case 0x7F:
        case 0xFF:
            return fdcOpen ? PortArm::Fdc : PortArm::ZxBus;
        case 0xF7:
            return (port & 0x0100) ? PortArm::Gluk : PortArm::ZxBus;
        case 0xDF:
            return PortArm::Mouse;
        case 0x57:
            return PortArm::SdData;
        case 0x77:
            return PortArm::SdConfig;
        case 0xEF:
            return PortArm::ComPort;
        default:
            return PortArm::ZxBus;
    }
}

PortDecodeDisposition PortDecoder_TSConf::TraceDisposition(PortArm arm, uint16_t port)
{
    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;
    switch (arm)
    {
        case PortArm::TsRegister:
        case PortArm::Gluk:
        case PortArm::Mouse:
            disp.decodedPort = port;
            disp.wasHandledInline = true;
            break;
        case PortArm::Paging7FFD:
            disp.decodedPort = 0x7FFD;
            disp.wasHandledInline = true;
            break;
        case PortArm::Ay:
            disp.decodedPort = (port & 0x4000) ? 0xFFFD : 0xBFFD;
            break;
        case PortArm::KeyboardBorder:
            disp.decodedPort = 0x00FE;
            disp.wasHandledInline = true;
            break;
        case PortArm::Fdc:
            disp.decodedPort = static_cast<uint16_t>(port & 0x00FF);
            break;
        case PortArm::Covox:
        case PortArm::Joystick:
        case PortArm::SdData:
        case PortArm::SdConfig:
        case PortArm::ComPort:
            disp.decodedPort = static_cast<uint16_t>(port & 0x00FF);
            disp.wasHandledInline = true;
            break;
        case PortArm::ZxBus:
        default:
            break;
    }
    return disp;
}

uint8_t PortDecoder_TSConf::DecodePortIn(uint16_t port, uint16_t pc)
{
    // The Nemo IDE decodes first (technical-design §3.11)
    if (uint8_t ideValue = 0xFF; TryIdePortIn(port, pc, ideValue))
        return ideValue;

    uint8_t result = 0xFF;
    const PortArm arm = ClassifyPort(port);

    // A registered ZX-Bus card that fully decodes this low byte owns the cycle
    // (see portdecoder.h); the FDC arm passes its canonical low byte for the
    // Beta-128 session arbitration
    PortDecodeDisposition claim;
    uint16_t decodedPort = (arm == PortArm::Fdc) ? static_cast<uint16_t>(port & 0x00FF) : port;
    if (arm != PortArm::TsRegister && OverrideDecodeForFullDecodeClaim(port, decodedPort, claim, /*isRead*/ true))
    {
        result = GetCachedFullDecodeInValue(port);
        _lastPortDecoded = true;
        OnPortInComplete(port, result, pc, claim);
        return result;
    }

    _lastPortDecoded = true;
    switch (arm)
    {
        case PortArm::TsRegister:
            result = ReadRegister(static_cast<uint8_t>(port >> 8));
            break;
        case PortArm::KeyboardBorder:
            result = Default_Port_FE_In(port, pc);
            break;
        case PortArm::Ay:
            // #FFFD reads the selected AY register; #BFFD is write-only
            result = (port & 0x4000) ? PeripheralPortIn(PORT_FFFD) : 0xFF;
            break;
        case PortArm::Fdc:
            result = PeripheralPortIn(static_cast<uint16_t>(port & 0x00FF));
            break;
        case PortArm::Joystick:
            // Kempston joystick outside DOS; no joystick model is attached (as ATM3)
            result = 0x00;
            break;
        case PortArm::Gluk:
            result = DecodeF7In(port);
            break;
        case PortArm::Mouse:
        {
            // #xxDF: A8 = 0 buttons + wheel, A8 = 1: A10 ? Y : X ([V] zkbdmus.v:107)
            const uint8_t reg = (port & 0x0100) ? ((port & 0x0400) ? 2 : 1) : 0;
            result = (_mouse && _mouse->IsPresent()) ? _mouse->ReadRegister(reg) : 0xFF;
            break;
        }
        case PortArm::SdConfig:
            // Constant "card present, writable" ([V] zports.v:460-465)
            result = 0x00;
            break;
        case PortArm::SdData:
        case PortArm::ComPort:
        case PortArm::Paging7FFD:
            // SD card (phase 6) and COM port (not emulated) read #FF; #7FFD is write-only
            result = 0xFF;
            break;
        case PortArm::Covox:
        case PortArm::ZxBus:
        default:
            _lastPortDecoded = false;
            // General Sound host ports: #B3/#BB by the low byte with bit 3 masked
            if ((port & 0x00F7) == 0x00B3)
            {
                const uint16_t gsPort = (port & 0x00FF) == 0x00BB ? 0x00BB : 0x00B3;
                result = PeripheralPortIn(gsPort);
            }
            break;
    }

    PortDecodeDisposition trace = TraceDisposition(arm, port);
    trace.wasDecoded = _lastPortDecoded;
    OnPortInComplete(port, result, pc, trace);
    return result;
}

void PortDecoder_TSConf::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    if (TryIdePortOut(port, value, pc))
        return;

    const PortArm arm = ClassifyPort(port);

    PortDecodeDisposition claim;
    uint16_t decodedPort = (arm == PortArm::Fdc) ? static_cast<uint16_t>(port & 0x00FF) : port;
    if (arm != PortArm::TsRegister && OverrideDecodeForFullDecodeClaim(port, decodedPort, claim, /*isRead*/ false))
    {
        OnPortOutComplete(port, value, pc, claim);
        return;
    }

    switch (arm)
    {
        case PortArm::TsRegister:
            WriteRegister(static_cast<uint8_t>(port >> 8), value);
            break;
        case PortArm::Paging7FFD:
            Write7FFD(value);
            break;
        case PortArm::Ay:
            PeripheralPortOut((port & 0x4000) ? PORT_FFFD : PORT_BFFD, value);
            break;
        case PortArm::KeyboardBorder:
            FlushVideo();
            Default_Port_FE_Out(port, value, pc);
            // BORDER = {PAL_SEL[3:0], 0, D[2:0]} with the latched PAL_SEL (§3.4)
            _ts.regs[TsConfReg::Border] =
                static_cast<uint8_t>(((_ts.regs[TsConfReg::PalSel] & 0x0F) << 4) | (value & 0x07));
            break;
        case PortArm::Covox:
            // The shared beeper / Covox DAC (§7): the self-decoding Covox device owns it
            DispatchSelfDecodingOut(port, value);
            break;
        case PortArm::Fdc:
            PeripheralPortOut(static_cast<uint16_t>(port & 0x00FF), value);
            break;
        case PortArm::Gluk:
            DecodeF7Out(port, value);
            break;
        case PortArm::Joystick:
        case PortArm::Mouse:
        case PortArm::SdData:
        case PortArm::SdConfig:
        case PortArm::ComPort:
            // No write side emulated yet (SD: phase 6); swallowed, never reaches the ZX-Bus
            break;
        case PortArm::ZxBus:
        default:
            // General Sound host ports: #B3/#BB (bit 3 masked) and #33 (NeoGS)
            if ((port & 0x00F7) == 0x00B3)
                PeripheralPortOut((port & 0x00FF) == 0x00BB ? 0x00BB : 0x00B3, value);
            else if ((port & 0x00FF) == 0x0033)
                PeripheralPortOut(0x0033, value);
            break;
    }

    PortDecodeDisposition trace = TraceDisposition(arm, port);
    trace.wasDecoded = trace.decodedPort != 0x0000;
    OnPortOutComplete(port, value, pc, trace);
}

/// endregion </Port decode>

/// region <Registers>

uint8_t PortDecoder_TSConf::ReadRegister(uint8_t reg)
{
    switch (reg)
    {
        case TsConfReg::VConfig:
        {
            // STATUS: [6] PWR_UP (cleared by the read), [2:0] VDAC_VER (§3.3)
            const uint8_t status = static_cast<uint8_t>((_ts.pwrUp ? 0x40 : 0x00) | kVdacVersion);
            _ts.pwrUp = 0;
            return status;
        }
        case TsConfReg::Page2:
        case TsConfReg::Page3:
            return _ts.regs[reg];
        case TsConfReg::DmaCtrl:
            return 0x00;  // DMA_STATUS: [7] busy - no DMA engine yet (phase 5)
        default:
            return 0xFF;
    }
}

void PortDecoder_TSConf::WriteRegister(uint8_t reg, uint8_t value)
{
    if (reg >= TsConfReg::kCount)
        return;  // not a built register

    if (IsVideoRegister(reg))
        FlushVideo();
    _ts.regs[reg] = value;

    switch (reg)
    {
        case TsConfReg::GYOffsL:
        case TsConfReg::GYOffsH:
            _ts.yOffsPending = 1;  // the row counter reloads at the next line start (§4.2)
            break;
        case TsConfReg::VConfig:
            // Latched at the next line start; the screen's mode label follows
            if (_context->pScreen)
                _context->pScreen->InitRaster();
            break;
        case TsConfReg::VPage:
            ApplyVideoPage();
            break;
        case TsConfReg::Page0:
        case TsConfReg::Page1:
        case TsConfReg::Page2:
        case TsConfReg::Page3:
            UpdateBanks();
            break;
        case TsConfReg::FMaps:
            RefreshFmWindow();
            break;
        case TsConfReg::SysConfig:
            // Bit 2 is copied into all four CACHE_CONFIG bits (§2.5); the clock switches now (§11)
            _ts.regs[TsConfReg::CacheConfig] = (value & 0x04) ? 0x0F : 0x00;
            RefreshCache();
            ApplyClock();
            break;
        case TsConfReg::MemConfig:
            UpdateBanks();
            RefreshM1Hook();
            break;
        case TsConfReg::FddVirt:
            UpdateBanks();  // CF_DOSPORTS follows VG_OPEN
            break;
        case TsConfReg::CacheConfig:
            RefreshCache();
            break;
        case TsConfReg::IntMask:
            _interrupts.OnMaskWrite(value);
            break;
        default:
            break;
    }
}

/// #7FFD (hardware-spec §2.3): ROM128 = D4, V_PAGE = D3 ? 7 : 5 immediately,
/// PAGE3 per LCK128, lock48 = D5 outside 1024K mode; ignored while locked
void PortDecoder_TSConf::Write7FFD(uint8_t value)
{
    if (_ts.lock48)
        return;

    FlushVideo();
    _state->p7FFD = value;

    uint8_t& memConfig = _ts.regs[TsConfReg::MemConfig];
    memConfig = static_cast<uint8_t>((memConfig & ~TsConfMemConfig::Rom128) | ((value >> 4) & 0x01));
    _ts.regs[TsConfReg::VPage] = (value & 0x08) ? 0x07 : 0x05;

    const uint8_t low3 = value & 0x07;
    const uint8_t d7d6 = static_cast<uint8_t>((value & 0xC0) >> 3);  // D7:D6 -> bits 4:3
    TsConfLck128 mode = _ts.Lck128();
    if (mode == TsConfLck128::Auto)
        mode = _ts.opcodeLatch128 ? TsConfLck128::Mode128K : TsConfLck128::Mode512K;

    switch (mode)
    {
        case TsConfLck128::Mode512K:
            _ts.regs[TsConfReg::Page3] = static_cast<uint8_t>(d7d6 | low3);
            break;
        case TsConfLck128::Mode128K:
            _ts.regs[TsConfReg::Page3] = low3;
            break;
        case TsConfLck128::Mode1024K:
        default:
            _ts.regs[TsConfReg::Page3] = static_cast<uint8_t>((value & 0x20) | d7d6 | low3);
            break;
    }

    if (_ts.Lck128() != TsConfLck128::Mode1024K)
        _ts.lock48 = (value & 0x20) ? 1 : 0;

    // V_PAGE bypasses the line latch ([V] video_ports.v:150-151)
    _engine.SetLiveVideoPage(_ts.regs[TsConfReg::VPage]);

    ApplyVideoPage();
    UpdateBanks();
    RefreshM1Hook();
}

/// endregion </Registers>

/// region <Gluk CMOS and #EFF7 (hardware-spec §9)>

/// Reachable when (EFF7[7] || DOS) && (!DOS || vdos): not from the TR-DOS ROM,
/// yes inside vdos ([V] zports.v:719-732)
bool PortDecoder_TSConf::CmosReachable() const
{
    return ((_ts.eff7 & 0x80) || _ts.dos) && (!_ts.dos || _ts.vdos);
}

uint8_t PortDecoder_TSConf::DecodeF7In(uint16_t port)
{
    // Only the data port (#BFF7, A14 = 0) drives the bus
    if ((port & 0x4000) == 0 && CmosReachable())
        return _evoAvr.ReadData();
    return 0xFF;
}

void PortDecoder_TSConf::DecodeF7Out(uint16_t port, uint8_t value)
{
    // Gating as latched before this cycle
    const bool cmos = CmosReachable();

    // #EFF7 (A12 = 0): writable only outside DOS, only bit 7 is used
    if ((port & 0x1000) == 0 && !_ts.dos)
        _ts.eff7 = value;

    if (cmos && (port & 0x2000) == 0)
        _evoAvr.WriteAddress(value);
    if (cmos && (port & 0x4000) == 0)
        _evoAvr.WriteData(value);
}

PortDecoder::RtcBinding PortDecoder_TSConf::GetRtcBinding()
{
    RtcBinding binding;
    binding.chip = &_evoAvr;
    binding.ports = "#DFF7 address, #BFF7 data (after #EFF7 bit 7, never from the TR-DOS ROM)";
    binding.nvramFile = _context->config.atm.evo_nvram_path;
    return binding;
}

/// endregion </Gluk CMOS>

/// region <Derived machinery>

void PortDecoder_TSConf::UpdateBanks()
{
    if (_memory)
        _memory->UpdateZ80Banks();
}

/// The M1 hook runs only while it has work: the auto-LCK128 opcode latch, an
/// armed DOS trap (mapped mode and ROM128 = 1) or an open DOS session
void PortDecoder_TSConf::RefreshM1Hook()
{
    Core* core = _context->pCore;
    if (!core || !core->GetZ80())
        return;

    const uint8_t memConfig = _ts.MemConfig();
    const bool trapArmed = !(memConfig & TsConfMemConfig::W0NoMap) && (memConfig & TsConfMemConfig::Rom128);
    const bool needed = _ts.Lck128() == TsConfLck128::Auto || trapArmed || _ts.dos;

    Z80* z80 = core->GetZ80();
    if (needed)
        z80->machineM1Hook = this;
    else if (z80->machineM1Hook == this)
        z80->machineM1Hook = nullptr;
}

/// DOS switching before the opcode read ([V] zmem.v:80,87-88), so the fetch
/// at #3Dxx already sees TR-DOS:
///   on:  fetch at #3D00-#3DFF, mapped mode, ROM128 = 1
///   off: fetch at >= #4000, not while vdos
void PortDecoder_TSConf::BeforeMachineM1(uint16_t address)
{
    if (!_ts.dos)
    {
        const uint8_t memConfig = _ts.MemConfig();
        if ((address & 0xFF00) == 0x3D00 && !(memConfig & TsConfMemConfig::W0NoMap) &&
            (memConfig & TsConfMemConfig::Rom128))
        {
            _ts.dos = 1;
            UpdateBanks();
            RefreshM1Hook();
        }
    }
    else if (address >= 0x4000 && !_ts.vdos)
    {
        _ts.dos = 0;
        UpdateBanks();
        RefreshM1Hook();
    }
}

/// Auto LCK128: every M1 latches !(D7 ^ D6) of the fetched byte (§2.3)
void PortDecoder_TSConf::OnMachineM1(uint16_t address)
{
    if (_ts.Lck128() != TsConfLck128::Auto || !_memory)
        return;

    const uint8_t opcode = _memory->DirectReadFromZ80Memory(address);
    _ts.opcodeLatch128 = (((opcode >> 7) ^ (opcode >> 6)) & 1) ? 0 : 1;
}

bool PortDecoder_TSConf::IsVideoRegister(uint8_t reg)
{
    return reg <= TsConfReg::PalSel || reg == TsConfReg::Border ||
           (reg >= TsConfReg::TMapPage && reg <= TsConfReg::SGPage) ||
           (reg >= TsConfReg::T0XOffsL && reg <= TsConfReg::T1YOffsH);
}

void PortDecoder_TSConf::FlushVideo()
{
    Core* core = _context->pCore;
    if (!core || !core->GetZ80())
        return;
    _engine.CatchUp(core->GetZ80()->t);
    if (_context->pScreen)
        _context->pScreen->UpdateScreen();
}

/// The interrupt controller owns /INT; the engine advances with the CPU and
/// drives it (§3.8)
void PortDecoder_TSConf::InstallInterrupts()
{
    Core* core = _context->pCore;
    if (!core || !core->GetZ80())
        return;

    Z80* z80 = core->GetZ80();
    if (z80->GetInterruptSource() != &_interrupts)
        z80->SetInterruptSource(&_interrupts);
    if (z80->GetMachineStepHook() != &_engine)
        z80->SetMachineStepHook(&_engine);
}

void PortDecoder_TSConf::RefreshFmWindow()
{
    Core* core = _context->pCore;
    if (!core)
        return;

    if (_ts.FmEnabled())
    {
        // Only 0x000-0x4FF of the 4 KB window has an effect (§2.4)
        _fmWindow.windowStart = _ts.FmBase();
        _fmWindow.windowEnd = static_cast<uint32_t>(_ts.FmBase()) + 0x500;
        core->AddBusOverlay(&_fmWindow);
    }
    else
    {
        core->RemoveBusOverlay(&_fmWindow);
    }
}

void PortDecoder_TSConf::FmWindow::onWrite(uint16_t addr, uint8_t value, [[maybe_unused]] bool romPaged)
{
    TsConfState& ts = _owner._ts;
    const uint16_t offset = static_cast<uint16_t>(addr - ts.FmBase());

    if (offset < 0x400)
    {
        // CRAM (0x000-0x1FF) / SFILE (0x200-0x3FF): the even byte is stashed,
        // the odd one commits {D, stash} to entry A[8:1]
        if ((offset & 1) == 0)
        {
            ts.fmStash = value;
            return;
        }
        const uint8_t index = static_cast<uint8_t>(offset >> 1);
        const uint16_t word = static_cast<uint16_t>((value << 8) | ts.fmStash);
        _owner.FlushVideo();  // CRAM is read per dot; SFILE per line (TSU)
        if (offset < 0x200)
            ts.cram[index] = word;
        else
            ts.sfile[index] = word;
    }
    else
    {
        // 0x400-0x4FF: +0x400+n is OUT (n << 8 | #AF)
        _owner.WriteRegister(static_cast<uint8_t>(offset), value);
    }
}

void PortDecoder_TSConf::RefreshCache()
{
    const bool active = _ts.regs[TsConfReg::CacheConfig] & 0x0F;
    Core* core = _context->pCore;

    if (_tsMemory)
    {
        if (!active)
            _tsMemory->CacheClear();
        _tsMemory->SetCacheActive(active);
    }

    if (!core)
        return;
    if (active)
        core->AddBusOverlay(&_cacheSnoop);
    else
        core->RemoveBusOverlay(&_cacheSnoop);
}

void PortDecoder_TSConf::CacheWriteSnoop::onWrite(uint16_t addr, [[maybe_unused]] uint8_t value,
                                                  [[maybe_unused]] bool romPaged)
{
    if (_owner._tsMemory)
        _owner._tsMemory->CacheInvalidate(addr);
}

/// SYS_CONFIG[1:0]: 3.5, 7, 14, 14 MHz, switched right after the OUT (§11).
/// Only hw_turbo_ratio is written: the host speed control composes on top
void PortDecoder_TSConf::ApplyClock()
{
    static constexpr uint8_t kRatio[4] = {1, 2, 4, 4};
    const uint8_t ratio = kRatio[_ts.regs[TsConfReg::SysConfig] & 0x03];
    if (_state->hw_turbo_ratio == ratio)
        return;

    _state->hw_turbo_ratio = ratio;
    if (_context->pCore && _context->pCore->GetZ80())
        _context->pCore->GetZ80()->ApplyHardwareTurboNow();
}

/// V_PAGE on the ZX screen until the TS video engine (phase 3): pages 5 and 7
/// are the two 128K screens
void PortDecoder_TSConf::ApplyVideoPage()
{
    if (_screen)
        _screen->SetActiveScreen(_ts.regs[TsConfReg::VPage] == 0x07 ? SCREEN_SHADOW : SCREEN_NORMAL);
}

/// endregion </Derived machinery>

/// region <TTD>

std::vector<ttd::PeripheralId> PortDecoder_TSConf::GetTTDModelStateIds() const
{
    return {ttd::PeripheralId::TsConfPaging, ttd::PeripheralId::Ds12887};
}

std::vector<std::unique_ptr<ttd::TTDSerializable>> PortDecoder_TSConf::CreateTTDSerializers() const
{
    auto* self = const_cast<PortDecoder_TSConf*>(this);
    std::vector<std::unique_ptr<ttd::TTDSerializable>> serializers;
    serializers.push_back(std::make_unique<ttd::TTDTsConfState>(*self));
    serializers.push_back(std::make_unique<ttd::TTDDs12887>(self->_evoAvr));
    return serializers;
}

/// endregion </TTD>
