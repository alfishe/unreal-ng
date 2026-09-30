#include "stdafx.h"

#include "common/modulelogger.h"

#include "portdecoder_tsconf.h"

#include <cstring>

#include "debugger/ttd/atm/ttdevops2.h"
#include "debugger/ttd/atm/ttdevosdcard.h"
#include "debugger/ttd/tsconf/ttdtsconfstate.h"
#include "debugger/ttd/ttdds12887.h"
#include "emulator/media/mediaformatregistry.h"
#include "emulator/media/mediamanager.h"
#include "emulator/io/tape/tape.h"
#include "emulator/sound/covox.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/tsconf/screentsconf.h"
#include "emulator/cpu/core.h"
#include "emulator/memory/tsconf/tsconfmemory.h"
#include "emulator/platforms/tsconf/tsconfcraminit.h"
#include "emulator/video/screen.h"

/// region <Constructors / Destructors>

PortDecoder_TSConf::PortDecoder_TSConf(EmulatorContext* context) : PortDecoder(context)
{
    _evoAvr.SetEmulatedClock([this]() { return EmulatedMicroseconds(); });

    // SD card: the Z-Controller registers and the DMA share the board's SPI
    // master ([V] top.v:1168-1189); a guest write is a TTD replay barrier (the
    // media manager's rule)
    _zc.SetDevice(&_sdCard);
    _dma.SetSpi([this](bool read, uint8_t out) -> uint8_t {
        if (read)
            return _zc.ReadData();
        _zc.WriteData(out);
        return 0xFF;
    });
    _sdCard.setWriteListener([this](uint64_t) {
        if (_context->pMediaManager)
            _context->pMediaManager->NoteWrite(_sdSlot.Descriptor().id);
    });
    if (_context->pMediaManager)
        _context->pMediaManager->RegisterSlot(_sdSlot);

    // Core creates TsConfMemory for this model; the windows are mapped from our state
    _tsMemory = dynamic_cast<TsConfMemory*>(_memory);
    if (_tsMemory)
        _tsMemory->AttachState(&_ts);
    else
        MLOGWARNING("PortDecoder_TSConf: the memory subsystem is not TsConfMemory - windows stay at the reset layout");

    // The AVR is the board's PS/2 keyboard controller, as on ATM3: physical
    // host keys reach its scan code log (Wild Commander and NedoOS read only
    // that; the ZX matrix gets the translated keys as before)
    if (_context->pKeyboard)
        _context->pKeyboard->SetPs2Sink(&_evoAvr);
}

PortDecoder_TSConf::~PortDecoder_TSConf()
{
    if (_context->pMediaManager)
        _context->pMediaManager->UnregisterSlot(_sdSlot.Descriptor().id);

    if (_context->pKeyboard && _context->pKeyboard->GetPs2Sink() == &_evoAvr)
        _context->pKeyboard->SetPs2Sink(nullptr);

    Core* core = _context->pCore;
    if (core)
    {
        core->RemoveBusOverlay(&_fmWindow);
        core->RemoveBusOverlay(&_cacheSnoop);
        core->RemoveBusOverlay(&_dramWriteWait);
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
    _ts.preVdos = 0;
    _ts.lock48 = 0;
    _ts.opcodeLatch128 = 0;

    // Shared latches the generic surfaces show (debugger, snapshots)
    _state->p7FFD = 0x00;
    _state->pFE = 0xFF;
    _state->border_attr = 0x07;
    _state->hw_turbo_ratio = 1;
    _dma.Reset();
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

    // The card and its session writes survive a reset; the controller
    // deselects it ([V] zports.v: SPI chip selects reset to 1)
    _zc.Reset();

    ApplyState();
}

void PortDecoder_TSConf::ApplyState()
{
    _cramVersion++;  // a reset or a state load replaced CRAM
    if (_memory)
        _dma.Attach(_memory->RAMBase(), &GetIdeAdapter(), _memory);
    // The VDAC builds are XTR_FEAT builds: they have DMA BLT2 (hardware-spec §0.1)
    _dma.SetBlt2Built(TsConfDma::kBuildHasBlt2 || _context->config.ts_vdac != 0);
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

const char* PortDecoder_TSConf::RegisterName(uint8_t reg)
{
    switch (reg)
    {
        case 0x00: return "V_CONFIG";
        case 0x01: return "V_PAGE";
        case 0x02: return "G_X_OFFS_L";
        case 0x03: return "G_X_OFFS_H";
        case 0x04: return "G_Y_OFFS_L";
        case 0x05: return "G_Y_OFFS_H";
        case 0x06: return "T_CONFIG";
        case 0x07: return "PAL_SEL";
        case 0x0F: return "BORDER";
        case 0x10: return "PAGE0";
        case 0x11: return "PAGE1";
        case 0x12: return "PAGE2";
        case 0x13: return "PAGE3";
        case 0x15: return "FMAPS";
        case 0x16: return "T_MAP_PAGE";
        case 0x17: return "T0_G_PAGE";
        case 0x18: return "T1_G_PAGE";
        case 0x19: return "SG_PAGE";
        case 0x1A: return "DMAS_AL";
        case 0x1B: return "DMAS_AH";
        case 0x1C: return "DMAS_AX";
        case 0x1D: return "DMAD_AL";
        case 0x1E: return "DMAD_AH";
        case 0x1F: return "DMAD_AX";
        case 0x20: return "SYS_CONFIG";
        case 0x21: return "MEM_CONFIG";
        case 0x22: return "HS_INT";
        case 0x23: return "VS_INT_L";
        case 0x24: return "VS_INT_H";
        case 0x25: return "DMA_WPD";
        case 0x26: return "DMA_LEN";
        case 0x27: return "DMA_CTRL";
        case 0x28: return "DMA_NUM";
        case 0x29: return "FDD_VIRT";
        case 0x2A: return "INT_MASK";
        case 0x2B: return "CACHE_CONFIG";
        case 0x2D: return "DMA_WPA";
        case 0x40: return "T0_X_OFFS_L";
        case 0x41: return "T0_X_OFFS_H";
        case 0x42: return "T0_Y_OFFS_L";
        case 0x43: return "T0_Y_OFFS_H";
        case 0x44: return "T1_X_OFFS_L";
        case 0x45: return "T1_X_OFFS_H";
        case 0x46: return "T1_Y_OFFS_L";
        case 0x47: return "T1_Y_OFFS_H";
        default: return "";
    }
}

std::vector<PortTraceCodeName> PortDecoder_TSConf::GetPortTraceCodeTable() const
{
    std::vector<PortTraceCodeName> table = {
        {static_cast<uint16_t>(PortArm::ZxBus), "ZxBus"},
        {static_cast<uint16_t>(PortArm::TsRegister), "TsRegister"},
        {static_cast<uint16_t>(PortArm::Paging7FFD), "Paging7FFD"},
        {static_cast<uint16_t>(PortArm::Ay), "Ay"},
        {static_cast<uint16_t>(PortArm::KeyboardBorder), "KeyboardBorder"},
        {static_cast<uint16_t>(PortArm::Covox), "SoundDac"},
        {static_cast<uint16_t>(PortArm::Fdc), "Fdc"},
        {static_cast<uint16_t>(PortArm::Joystick), "Joystick"},
        {static_cast<uint16_t>(PortArm::Gluk), "Gluk"},
        {static_cast<uint16_t>(PortArm::Mouse), "Mouse"},
        {static_cast<uint16_t>(PortArm::SdData), "SdData"},
        {static_cast<uint16_t>(PortArm::SdConfig), "SdConfig"},
        {static_cast<uint16_t>(PortArm::ComPort), "ComPort"},
    };
    for (uint32_t reg = 0; reg < 0x100; reg++)
    {
        const char* name = RegisterName(static_cast<uint8_t>(reg));
        if (name[0])
            table.push_back({static_cast<uint16_t>(kTraceRegisterBase + reg), name});
    }
    return table;
}

PortDecodeDisposition PortDecoder_TSConf::TraceDisposition(PortArm arm, uint16_t port)
{
    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;
    // The internal code: the register for #xxAF, else the decode arm
    disp.internalCode = arm == PortArm::TsRegister ? static_cast<uint16_t>(kTraceRegisterBase + (port >> 8))
                                                   : static_cast<uint16_t>(arm);
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
    {
        ApplyIdeStall();
        return ideValue;
    }

    uint8_t result = 0xFF;
    const PortArm arm = ClassifyPort(port);
    ApplyExternalIoStall(port, arm);

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
            result = FdcAccess(static_cast<uint8_t>(port), /*isWrite*/ false, 0);
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
            // Constant "card present, writable" ([V] zports.v:460-465); the
            // real detect / write-protect switches are in AVR register C
            result = 0x00;
            break;
        case PortArm::SdData:
            CatchUpEngine();  // a running SPI DMA owns the master up to now
            result = _zc.ReadData();
            break;
        case PortArm::ComPort:
        case PortArm::Paging7FFD:
            // COM port (not emulated) reads #FF; #7FFD is write-only
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
    {
        ApplyIdeStall();
        return;
    }

    const PortArm arm = ClassifyPort(port);
    ApplyExternalIoStall(port, arm);

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
            PortFeOut(port, value, pc);
            // BORDER = {PAL_SEL[3:0], 0, D[2:0]} with the latched PAL_SEL (§3.4)
            _ts.regs[TsConfReg::Border] =
                static_cast<uint8_t>(((_ts.regs[TsConfReg::PalSel] & 0x0F) << 4) | (value & 0x07));
            break;
        case PortArm::Covox:
            DacWrite(value);  // any #xxFB, never gated ([V] zports.v:490)
            break;
        case PortArm::Fdc:
            FdcAccess(static_cast<uint8_t>(port), /*isWrite*/ true, value);
            break;
        case PortArm::Gluk:
            DecodeF7Out(port, value);
            break;
        case PortArm::SdData:
            CatchUpEngine();
            _zc.WriteData(value);
            break;
        case PortArm::SdConfig:
            // [1] SD /CS; [2] FT812, [3] SD2, [4] ESP chip selects are not fitted
            CatchUpEngine();
            _zc.WriteConfig(value);
            break;
        case PortArm::Joystick:
        case PortArm::Mouse:
        case PortArm::ComPort:
            // No write side; swallowed, never reaches the ZX-Bus
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
            const uint8_t status = static_cast<uint8_t>((_ts.pwrUp ? 0x40 : 0x00) | VdacVersion());
            _ts.pwrUp = 0;
            return status;
        }
        case TsConfReg::Page2:
        case TsConfReg::Page3:
            return _ts.regs[reg];
        case TsConfReg::DmaCtrl:
            // DMA_STATUS: [7] busy, as of this cycle
            CatchUpEngine();
            return _dma.Busy() ? 0x80 : 0x00;
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
    else if (reg >= TsConfReg::DmaSAl && reg <= TsConfReg::DmaNum)
        CatchUpEngine();  // the DMA runs up to the write
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
        case TsConfReg::DmaSAl:
        case TsConfReg::DmaSAh:
        case TsConfReg::DmaSAx:
        case TsConfReg::DmaDAl:
        case TsConfReg::DmaDAh:
        case TsConfReg::DmaDAx:
            _dma.WriteAddress(reg, value);
            break;
        case TsConfReg::DmaCtrl:
            _dma.Launch(value);
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

/// region <Beta-128 and the virtual TR-DOS (hardware-spec §8.2)>

/// One cycle on #1F/#3F/#5F/#7F (VG93) or #FF (system register), reached while
/// DOS || VG_OPEN ([V] zports.v:638-651):
/// - the VG93 is selected only outside vdos and when the latched drive is not
///   virtual (FDD_VIRT bit); the drive select bits of a #FF write latch always;
/// - vdos starts at the next M1 after any such access in DOS to a virtual
///   drive, and ends at once on a VG93 register access (not #FF) inside vdos.
/// The virtual drive is Z80 code in RAM page #FF (placed by the BIOS); the
/// emulator only swaps it in
uint8_t PortDecoder_TSConf::FdcAccess(uint8_t port, bool isWrite, uint8_t value)
{
    const bool systemPort = port == 0xFF;
    const bool virtualDrive = (_ts.regs[TsConfReg::FddVirt] >> (_ts.vgDrive & 0x03)) & 1;
    const bool chipSelected = !_ts.vdos && !virtualDrive;

    uint8_t result = 0xFF;  // an unselected VG93 leaves the bus floating
    if (chipSelected)
    {
        if (isWrite)
            PeripheralPortOut(port, value);
        else
            result = PeripheralPortIn(port);
    }
    if (isWrite && systemPort)
        _ts.vgDrive = value & 0x03;

    if (_ts.dos && !_ts.vdos && virtualDrive)
    {
        _ts.preVdos = 1;
        RefreshM1Hook();
    }
    else if (_ts.vdos && !systemPort)
    {
        _ts.vdos = 0;
        UpdateBanks();
        RefreshM1Hook();
    }
    return result;
}

/// endregion </Beta-128>

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
/// armed DOS trap (mapped mode and ROM128 = 1), an open DOS session or the
/// 14 MHz DRAM waits (an M1 miss waits longer than a data read)
void PortDecoder_TSConf::RefreshM1Hook()
{
    Core* core = _context->pCore;
    if (!core || !core->GetZ80())
        return;

    const uint8_t memConfig = _ts.MemConfig();
    const bool trapArmed = !(memConfig & TsConfMemConfig::W0NoMap) && (memConfig & TsConfMemConfig::Rom128);
    const bool waits14 = (_ts.regs[TsConfReg::SysConfig] & 0x02) != 0;
    const bool needed = _ts.Lck128() == TsConfLck128::Auto || trapArmed || _ts.dos || _ts.preVdos || waits14;

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
    if (_tsMemory && (_ts.regs[TsConfReg::SysConfig] & 0x02))
        _tsMemory->NoteM1Fetch();

    // A trapped FDC access enters vdos at the next M1 ([V] zmem.v pre_vdos)
    if (_ts.preVdos)
    {
        _ts.preVdos = 0;
        _ts.vdos = 1;
        UpdateBanks();
    }

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

void PortDecoder_TSConf::CatchUpEngine()
{
    Core* core = _context->pCore;
    if (core && core->GetZ80())
        _engine.CatchUp(core->GetZ80()->t);
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

    // A DMA CRAM write draws the picture up to its moment first (TIM-5)
    _engine.SetVideoFlush([this](uint32_t raster) {
        if (auto* screen = dynamic_cast<ScreenTSConf*>(_context->pScreen))
            screen->DrawTo(raster);
    });
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
        {
            ts.cram[index] = word;
            _owner._cramVersion++;
        }
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

void PortDecoder_TSConf::DramWriteWait::onWrite(uint16_t addr, [[maybe_unused]] uint8_t value,
                                                [[maybe_unused]] bool romPaged)
{
    if (_owner._tsMemory)
        _owner._tsMemory->AfterWrite(addr);
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
    Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    // 14 MHz: DRAM reads wait for the arbiter (TIM-1); the M1 hook tells the
    // memory which read is the opcode fetch
    const bool waits14 = ratio == 4 && z80;
    if (_tsMemory)
        _tsMemory->SetDramWaits(waits14 ? z80 : nullptr, &_arbiter);
    _arbiter.Reset();
    if (Core* core = _context->pCore)
    {
        if (waits14)
            core->AddBusOverlay(&_dramWriteWait);
        else
            core->RemoveBusOverlay(&_dramWriteWait);
    }
    RefreshM1Hook();
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
    return {ttd::PeripheralId::TsConfPaging, ttd::PeripheralId::EvoSdCard, ttd::PeripheralId::Ds12887,
            ttd::PeripheralId::EvoPs2};
}

std::vector<std::unique_ptr<ttd::TTDSerializable>> PortDecoder_TSConf::CreateTTDSerializers() const
{
    auto* self = const_cast<PortDecoder_TSConf*>(this);
    std::vector<std::unique_ptr<ttd::TTDSerializable>> serializers;
    serializers.push_back(std::make_unique<ttd::TTDTsConfState>(*self));
    serializers.push_back(std::make_unique<ttd::TTDEvoSdCard>(self->_sdCard, self->_zc));
    serializers.push_back(std::make_unique<ttd::TTDDs12887>(self->_evoAvr));
    serializers.push_back(std::make_unique<ttd::TTDEvoPs2>(self->_evoAvr));
    return serializers;
}

/// endregion </TTD>

/// region <SD card>

namespace
{
    AccessMode AccessOf(SdCardSpi::WriteMode mode)
    {
        switch (mode)
        {
            case SdCardSpi::WriteMode::Persist: return AccessMode::WriteThrough;
            case SdCardSpi::WriteMode::Off: return AccessMode::ReadOnly;
            default: return AccessMode::Session;
        }
    }
}  // namespace

bool PortDecoder_TSConf::InsertSdCard(const std::string& path, SdCardSpi::WriteMode mode, bool writeProtect)
{
    if (MediaManager* manager = _context->pMediaManager)
    {
        MediaSource source;
        source.path = path;
        InsertOptions options;
        options.access = AccessOf(mode);
        options.writeProtect = writeProtect;
        options.disposition = Disposition::Discard;
        const MediaResult result = manager->Insert(_sdSlot.Descriptor().id, source, options);
        if (!result.Ok())
            MLOGWARNING("PortDecoder_TSConf: SD card '%s' not inserted: %s", path.c_str(), result.message.c_str());
        return result.Ok();
    }
    const bool inserted = _sdCard.open(path, mode);
    _sdWriteProtect = writeProtect;
    UpdateSdStatus();
    return inserted;
}

bool PortDecoder_TSConf::InsertSdCard(std::unique_ptr<IBlockDevice> media, SdCardSpi::WriteMode mode, bool writeProtect)
{
    if (MediaManager* manager = _context->pMediaManager)
    {
        MediaSource source;
        source.type = MediaSourceType::Blank;
        InsertOptions options;
        options.writeProtect = writeProtect;
        options.disposition = Disposition::Discard;
        auto medium = MediaFormatRegistry::WrapBlock(source, AccessOf(mode), "memory", std::move(media));
        return manager->Insert(_sdSlot.Descriptor().id, std::move(medium), options).Ok();
    }
    const bool inserted = _sdCard.insert(std::move(media), mode);
    _sdWriteProtect = writeProtect;
    UpdateSdStatus();
    return inserted;
}

void PortDecoder_TSConf::EjectSdCard()
{
    if (MediaManager* manager = _context->pMediaManager)
    {
        EjectOptions options;
        options.disposition = Disposition::Discard;
        manager->Eject(_sdSlot.Descriptor().id, options);
        return;
    }
    _sdCard.close();
    UpdateSdStatus();
}

void PortDecoder_TSConf::UpdateSdStatus()
{
    // AVR register C: b3 card present, b2 write-protected (the slot's switches)
    _evoAvr.SetSdStatus(_sdCard.present(), _sdCard.present() && _sdWriteProtect);
}

PortDecoder_TSConf::SdSlot::SdSlot(PortDecoder_TSConf& owner) : _owner(owner)
{
    _descriptor.id = "sd.zc";
    _descriptor.kind = MediaKind::Block;
    _descriptor.label = "SD card (Z-Controller)";
    _descriptor.removable = true;
    _descriptor.swapDelayMs = 500;
    _descriptor.acceptsFolder = true;
    _descriptor.defaultAccess = AccessMode::Session;
    _descriptor.defaultFs = FatType::Fat16;
    _descriptor.hasCardDetect = true;          // AVR register C bit 3
    _descriptor.hasWriteProtectSwitch = true;  // AVR register C bit 2
    _descriptor.tags = {"sd", "zcontroller", "primary", "boot"};
    _descriptor.aliases = {"sd"};
    _descriptor.guestName = "the SD card of TS-BIOS (Boot Device: SD Z-contr) and Wild Commander";
}

void PortDecoder_TSConf::SdSlot::Attach(Medium& medium)
{
    _owner._sdCard.attach(*medium.Block());
    _owner._sdCard.select(_owner._zc.IsSelected());
    _owner.UpdateSdStatus();
}

void PortDecoder_TSConf::SdSlot::Detach()
{
    _owner._sdCard.detach();
    _owner.UpdateSdStatus();
}

bool PortDecoder_TSConf::SdSlot::IsBusy() const
{
    return _owner._sdCard.busy();
}

void PortDecoder_TSConf::SdSlot::SetWriteProtectSwitch(bool on)
{
    _owner._sdWriteProtect = on;
    _owner.UpdateSdStatus();
}

/// endregion </SD card>

/// A CPU access that reached the drive (a register or the data word, not a
/// latch) freezes the Z80 for the IDE bus cycle: +1 / +2 / +3 T at 3.5 / 7 /
/// 14 MHz ([V] zclock.v ide_stall; hardware-spec §8.3). Off by default
/// 14 MHz: an I/O cycle to an "external" port - the AY (#FD with A15 = 1)
/// or the VG93 (#1F/#3F/#5F/#7F while it is open; not #FF) - freezes the CPU
/// clock for 8 fclk = 4 T, IN and OUT alike ([V] zclock.v:76-90, zports.v:344-345;
/// hardware-spec §11). TIM-2
void PortDecoder_TSConf::ApplyExternalIoStall(uint16_t port, PortArm arm)
{
    if (!(_ts.regs[TsConfReg::SysConfig] & 0x02)) [[likely]]
        return;
    const bool external = arm == PortArm::Ay || (arm == PortArm::Fdc && (port & 0xFF) != 0xFF);
    if (external && _context->pCore && _context->pCore->GetZ80())
        _context->pCore->GetZ80()->AddWaitStates(4);
}

void PortDecoder_TSConf::ApplyIdeStall()
{
    static constexpr uint8_t kStall[4] = {1, 2, 3, 3};
    if (!_context->config.ide_stall || !GetIdeAdapter().LastAccessReachedDrive())
        return;
    if (_context->pCore && _context->pCore->GetZ80())
        _context->pCore->GetZ80()->AddWaitStates(kStall[_ts.regs[TsConfReg::SysConfig] & 0x03]);
}

/// region <Sound DAC (hardware-spec §7)>

/// TS-Conf has no separate beeper: one 8-bit register feeds the board's PWM
/// output ([V] sound/sound.v). #FB writes the byte; an #FE write sets it to
/// #FF / #00 from bit 4 (bit 3 when the AVR's "beeper mux" setting selects the
/// tape out - off by default, not modeled); the last write wins. The shared
/// Covox device is that register here (all four channels: a mono DAC, its
/// state already travels in TTD), so the generic beeper stays silent on this
/// machine. Without a Covox in the config the beeper plays bit 4 instead
void PortDecoder_TSConf::DacWrite(uint8_t value)
{
    Covox* covox = _soundManager ? _soundManager->getCovox() : nullptr;
    if (!covox)
        return;
    for (uint16_t channel : {Covox::PORT_LEFT_A, Covox::PORT_LEFT_B, Covox::PORT_RIGHT_A, Covox::PORT_RIGHT_B})
        covox->portDeviceOutMethod(channel, value);
}

void PortDecoder_TSConf::PortFeOut(uint16_t port, uint8_t value, uint16_t pc)
{
    const bool dac = _soundManager && _soundManager->hasCovox();
    if (!dac)
    {
        Default_Port_FE_Out(port, value, pc);
        return;
    }

    // Default_Port_FE_Out without the beeper: border, tape MIC out
    _context->emulatorState.pFE = value;
    _context->emulatorState.border_attr = value & 0x07;
    _tape->handlePortOut(value);
    _screen->SetBorderColor(value & 0x07);
    DacWrite((value & 0x10) ? 0xFF : 0x00);
}

/// endregion </Sound DAC>
