#include "stdafx.h"

#include "common/modulelogger.h"

#include "portdecoder_scorpiongmx.h"

#include "emulator/cpu/core.h"
#include "debugger/ttd/atm/ttdevosdcard.h"
#include "debugger/ttd/gmx/ttdgmxpaging.h"
#include "emulator/cpu/z80.h"
#include "emulator/media/mediamanager.h"
#include "emulator/slots/portowners.h"
#include "emulator/sound/audio.h"
#include "emulator/video/screen.h"

/// region <Constructors / Destructors>

PortDecoder_ScorpionGMX::PortDecoder_ScorpionGMX(EmulatorContext* context) : PortDecoder_Scorpion256(context)
{
    // The fitting cuts the host board's WAIT and CLOCK lines: the Turbo+ EPLD neither strobes nor waits (Q5, Q21)
    _turboPlusHost = false;

    // Z-Controller SD (GMX design 2.7): a guest write changes the medium, so it is a TTD replay barrier (the media
    // manager's rule)
    _zc.SetDevice(&_sdCard);
    _spiTap.SetDevice(0, "sd");
    _spiTap.Attach(_context);
    _zc.SetTap(&_spiTap);
    _sdCard.SetBusTap(&_spiTap, 0);
    _sdCard.SetClock([this]() { return EmulatedMicroseconds(); });
    _sdCard.AttachHistory(_context, [context = _context]() { return slots::PortOwnerName(context, 0x0057, "sd-zc"); });
    _sdCard.setWriteListener([this](uint64_t) {
        if (_context->pMediaManager)
            _context->pMediaManager->NoteWrite(_sdSlot.Descriptor().id);
    });
    if (_context->pMediaManager)
        _context->pMediaManager->RegisterSlot(_sdSlot);
}

PortDecoder_ScorpionGMX::~PortDecoder_ScorpionGMX()
{
    if (_context->pMediaManager)
        _context->pMediaManager->UnregisterSlot(_sdSlot.Descriptor().id);
}

/// endregion </Constructors / Destructors>

/// region <Interface methods>

void PortDecoder_ScorpionGMX::reset()
{
    EmulatorState& state = *_state;
    GmxState& gmx = state.gmx;

    // FIXROM: RESET leaves the plane where it is (manual, #00 bit 4)
    const uint8_t keptPlane = (gmx.p00 & P00_FIXROM) ? static_cast<uint8_t>(state.scorpion.p7EFD & P7EFD_PLANE_MASK) : 0;

    // Every card latch to 0 before the host reset rebuilds the banks from them; #00 survives (manual section 2)
    gmx.p78FD = 0;
    gmx.p7AFD = 0;
    gmx.p7CFD = 0;
    gmx.magicShift = 0;
    gmx.magicLock = 0;
    gmx.lock7AFD = 0;
    gmx.lock7EFD = 0;
    state.pDFFD = 0;
    _reconfPending = false;
    _zc.Reset();  // the card and its session writes stay, the controller deselects it

    PortDecoder_Scorpion256::reset();

    state.scorpion.p7EFD = keptPlane;
    state.scorpion.profromBank = Plane();
    _context->pMemory->UpdateZ80Banks();
    RefreshStepHook();

    // RESET clears #7EFD bit 3: back to the Scorpion raster
    if (_context->pScreen)
        _context->pScreen->InitRaster();
}

void PortDecoder_ScorpionGMX::PowerCycle()
{
    // Power-off is the only thing besides RECONF that clears #00 (manual section 2)
    _state->gmx.p00 = 0;
    reset();
}

uint8_t PortDecoder_ScorpionGMX::DecodePortIn(uint16_t port, uint16_t pc)
{
    GmxState& gmx = _state->gmx;

    // A read of #FF releases the read-backs the Magic button froze (manual section 2); the cycle itself goes on to
    // the host board (the FDC system port or the floating bus)
    if (gmx.magicLock && (port & 0x00FF) == 0x00FF) [[unlikely]]
        gmx.magicLock = 0;

    // Z-Controller: #77 reads 0 ("inserted, writable": the driver finds the card by initializing it; Unreal,
    // ZEsarUX and the BaseConf FPGA alike), #57 the byte the previous exchange received
    const uint8_t low = static_cast<uint8_t>(port);
    if (low == 0x57 || low == 0x77)
    {
        const uint8_t value = (low == 0x57) ? _zc.ReadData() : 0x00;
        _lastPortDecoded = true;
        PortDecodeDisposition disp;
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.decodedPort = low;
        disp.device = PortDeviceId::SdCard;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        OnPortInComplete(port, value, pc, disp);
        return value;
    }

    // The Pentagon decode: #7FFD is write-only, nothing drives the bus there
    if (IsPentagonDecode() && (port & 0x8002) == 0)
    {
        _lastPortDecoded = true;
        PortDecodeDisposition disp;
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.decodedPort = 0x7FFD;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        OnPortInComplete(port, 0xFF, pc, disp);
        return 0xFF;
    }

    const uint8_t reg = (gmx.p00 & P00_BLKEXT) ? 0 : GmxRegisterOf(port);
    uint8_t result = 0xFF;
    switch (reg)
    {
        case 0x78:
            result = Read78FD();
            break;
        case 0x7A:
            result = Read7AFD();
            break;
        case 0x7E:
            result = Read7EFD();
            break;
        default:
            // #7CFD and #DFFD are write-only; their reads, #00 and everything else are the host board's
            return PortDecoder_Scorpion256::DecodePortIn(port, pc);
    }

    _lastPortDecoded = true;
    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;
    disp.decodedPort = static_cast<uint16_t>((reg << 8) | 0xFD);
    disp.wasDecoded = true;
    disp.wasHandledInline = true;
    OnPortInComplete(port, result, pc, disp);
    return result;
}

void PortDecoder_ScorpionGMX::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    EmulatorState& state = *_state;
    GmxState& gmx = state.gmx;

    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;
    disp.wasDecoded = true;
    disp.wasHandledInline = true;

    // #xx00: full decode of A7-A0, any high byte, reachable even while BLKEXT blocks the register file. It must win
    // over the AY (#C000 matches the #FFFD mask) and every mirror decode of the host board
    if ((port & 0x00FF) == 0x0000)
    {
        Out00(value);
        disp.decodedPort = 0x0000;
        OnPortOutComplete(port, value, pc, disp);
        return;
    }

    // Z-Controller: #77 config (D1 = SD /CS), #57 data
    if ((port & 0x00FF) == 0x0077 || (port & 0x00FF) == 0x0057)
    {
        if ((port & 0x00FF) == 0x0077)
            _zc.WriteConfig(value);
        else
            _zc.WriteData(value);
        disp.decodedPort = port & 0x00FF;
        disp.device = PortDeviceId::SdCard;
        OnPortOutComplete(port, value, pc, disp);
        return;
    }

    // The Pentagon scheme (manual, "the Pentagon scheme"; #00 BLKEX2): every #xxFD port of the card and the host is
    // off and #7FFD answers at A1 = A15 = 0, #1FFD included, as on a Pentagon 128
    if (IsPentagonDecode() && (port & 0x8002) == 0)
    {
        PortDecoder_Scorpion256::DecodePortOut(0x7FFD, value, pc);
        return;
    }

    // The card's #xxFD file before the host decode: the Scorpion #7FFD mask matches all four #7xFD ports and the AY
    // #FFFD mask matches #DFFD
    const uint8_t reg = (gmx.p00 & P00_BLKEXT) ? 0 : GmxRegisterOf(port);
    if (reg == 0)
    {
        PortDecoder_Scorpion256::DecodePortOut(port, value, pc);
        return;
    }

    Memory& memory = *_context->pMemory;
    switch (reg)
    {
        case 0x78:
            gmx.p78FD = static_cast<uint8_t>(value & 0x7F);
            memory.UpdateZ80Banks();
            break;
        case 0x7A:
            // The scroller moves the 640x200 picture: the beam draws up to this T with the old start first
            if (Screen* screen = _context->pScreen)
                screen->UpdateScreen();
            gmx.p7AFD = static_cast<uint8_t>(value & 0xF0);  // the low four bits are reset in hardware (manual)
            if (Screen* screen = _context->pScreen)
                screen->NoteVideoWrite();
            break;
        case 0x7C:
            if (Screen* screen = _context->pScreen)
                screen->UpdateScreen();
            gmx.p7CFD = static_cast<uint8_t>(value & 0x3F);
            if (Screen* screen = _context->pScreen)
                screen->NoteVideoWrite();
            break;
        case 0x7E:
            Out7EFD(value);
            break;
        default:  // 0xDF
            state.pDFFD = static_cast<uint8_t>(value & 0x07);
            memory.UpdateZ80Banks();
            break;
    }

    disp.decodedPort = static_cast<uint16_t>((reg << 8) | 0xFD);
    OnPortOutComplete(port, value, pc, disp);
}

void PortDecoder_ScorpionGMX::SetRAMPage(uint8_t page)
{
    // Debugger-forced #C000 page: the 7-bit formula split back into #7FFD bits 0-2, #1FFD bit 4 and #DFFD bits 0-2
    EmulatorState& state = *_state;
    state.p7FFD = static_cast<uint8_t>((state.p7FFD & ~0b111) | (page & 0b111));
    state.p1FFD = static_cast<uint8_t>((state.p1FFD & ~0x10) | ((page & 0x08) ? 0x10 : 0));
    state.pDFFD = static_cast<uint8_t>((page >> 4) & 0x07);
    _context->pMemory->UpdateZ80Banks();
}

void PortDecoder_ScorpionGMX::SyncTurboWaits()
{
    // No host wait states and no /INT stretch. The card's DRAM slot: an opcode fetch from RAM on an odd 7 MHz T
    // waits one T, at 3.5 MHz none (GMX design 2.6, Q21). Set here as well as by the model's timing defaults: an INI
    // run reads [ULA] EvenM1, which describes the host board
    _context->config.even_M1 = EVEN_M1_TURBO;
    SyncFrame();
    RefreshStepHook();
}

void PortDecoder_ScorpionGMX::SyncFrame()
{
    CONFIG& config = _context->config;
    const bool pentagon = (_state->gmx.p00 & P00_PENTAGON_INT) != 0;
    const bool extended = (_state->scorpion.p7EFD & P7EFD_GFX) != 0;
    const uint32_t frame = pentagon ? kPentagonFrame : kScorpionFrame;
    const uint32_t intstart = !pentagon ? kScorpionIntStart : (extended ? kPentagonIntStartGmxRaster : kPentagonIntStart);
    if (config.frame == frame && config.intstart == intstart && config.intlen == kIntLength)
        return;

    config.frame = frame;
    config.t_line = 224;
    config.intstart = intstart;
    config.intlen = kIntLength;
    config.frame_duration_us = CalculateFrameDurationUs(frame);
    if (_context->pCore && _context->pCore->GetZ80())
        _context->pCore->GetZ80()->RecomputeFrameTiming();
    // The ZX screen follows: the Pentagon raster with PENT (Screen::DetectModeGMX)
    if (_context->pScreen)
        _context->pScreen->InitRaster();
}

void PortDecoder_ScorpionGMX::OnMachineStep([[maybe_unused]] uint32_t t)
{
    if (_reconfPending)
        RunReconfiguration();
}

void PortDecoder_ScorpionGMX::OnMachineFrameRollover([[maybe_unused]] uint32_t frameLength)
{
}

bool PortDecoder_ScorpionGMX::IsPagingLocked() const
{
    return PortDecoder_Scorpion256::IsPagingLocked() && !(_state->scorpion.p7EFD & P7EFD_MAGIC_BLOCK);
}

bool PortDecoder_ScorpionGMX::OnMagicButton()
{
    EmulatorState& state = *_state;
    if (state.scorpion.p7EFD & P7EFD_MAGIC_BLOCK)
        return false;

    // The marked bits of #7AFD (0-6) and #7EFD (0-3, 6) keep the values of the moment of the press (manual
    // section 2; MAME m_port_7afd_lock_data / m_port_7efd_lock_data)
    GmxState& gmx = state.gmx;
    if (!gmx.magicLock)
    {
        gmx.lock7AFD = Read7AFD();
        gmx.lock7EFD = Read7EFD();
        gmx.magicLock = 1;
    }
    return true;
}

void PortDecoder_ScorpionGMX::EnterSpectrum128Paging(uint16_t pc)
{
    DecodePortOut(0x7EFD, 0x40, pc);
    DecodePortOut(0x78FD, 0x00, pc);
    DecodePortOut(0xDFFD, 0x00, pc);
}

std::vector<ttd::TTDDeviceType> PortDecoder_ScorpionGMX::GetTTDModelStateTypes() const
{
    std::vector<ttd::TTDDeviceType> types = PortDecoder_Scorpion256::GetTTDModelStateTypes();
    types.push_back(ttd::TTDDeviceType::GmxPaging);
    types.push_back(ttd::TTDDeviceType::EvoSdCard);
    return types;
}

std::vector<std::unique_ptr<ttd::TTDSerializable>> PortDecoder_ScorpionGMX::CreateTTDSerializers() const
{
    std::vector<std::unique_ptr<ttd::TTDSerializable>> serializers = PortDecoder_Scorpion256::CreateTTDSerializers();
    serializers.push_back(std::make_unique<ttd::TTDGmxPaging>(_context));
    auto* self = const_cast<PortDecoder_ScorpionGMX*>(this);
    serializers.push_back(std::make_unique<ttd::TTDEvoSdCard>(self->_sdCard, self->_zc));
    return serializers;
}

void PortDecoder_ScorpionGMX::RestoreReconfigurationPending(bool pending)
{
    _reconfPending = pending;
    SyncFrame();  // the restored #00 bit 7
    RefreshStepHook();
}

/// region <SD card (Z-Controller)>

PortDecoder_ScorpionGMX::GmxSdSlot::GmxSdSlot(PortDecoder_ScorpionGMX& owner) : _owner(owner)
{
    _descriptor.id = "sd.zc";
    _descriptor.kind = MediaKind::Block;
    _descriptor.label = "SD card (Z-Controller)";
    _descriptor.removable = true;
    _descriptor.swapDelayMs = 500;
    _descriptor.acceptsFolder = true;
    _descriptor.defaultAccess = AccessMode::Session;
    // The 6.44 monitor knows FAT32 (and MFS) only (GMX Q20): folder volumes are FAT32
    _descriptor.defaultFs = FatType::Fat32;
    _descriptor.fsCompatibility = {FatType::Fat32};
    _descriptor.tags = {"sd", "zcontroller", "primary", "boot"};
    _descriptor.aliases = {"sd"};
    _descriptor.guestName = "the SD card of the GMX shadow monitor (OSZ reads \\OSZ\\ from it)";
}

void PortDecoder_ScorpionGMX::GmxSdSlot::Attach(Medium& medium)
{
    _owner._sdCard.attach(*medium.Block());
    _owner._sdCard.select(_owner._zc.IsSelected());
}

void PortDecoder_ScorpionGMX::GmxSdSlot::Detach()
{
    _owner._sdCard.detach();
}

bool PortDecoder_ScorpionGMX::GmxSdSlot::IsBusy() const
{
    return _owner._sdCard.busy();
}

/// endregion </SD card (Z-Controller)>

/// endregion </Interface methods>

/// region <Register file>

uint8_t PortDecoder_ScorpionGMX::GmxRegisterOf(uint16_t port)
{
    //    GMX register file: the host Scorpion's #xxFD rule (A5 = 1, A2 = 1, A1 = 0, A0 = 1) and the exact high byte
    //    (MAME scorpiongmx_state, pico-speccy, the web Unreal Speccy)
    static constexpr uint16_t port_FD_mask = 0b0000'0000'0010'0111;
    static constexpr uint16_t port_FD_match = 0b0000'0000'0010'0101;
    static_assert((0x78FD & port_FD_mask) == port_FD_match && "#78FD must be inside the family");
    static_assert((0xDFFD & port_FD_mask) == port_FD_match && "#DFFD must be inside the family");

    if ((port & port_FD_mask) != port_FD_match)
        return 0;

    const uint8_t high = static_cast<uint8_t>(port >> 8);
    switch (high)
    {
        case 0x78:
        case 0x7A:
        case 0x7C:
        case 0x7E:
        case 0xDF:
            return high;
        default:
            return 0;
    }
}

uint8_t PortDecoder_ScorpionGMX::Read78FD()
{
    // Bit 7 = BRD1, bits 0-6 = the stored page, bit 0 OR-ed with the magic shift register, which every read shifts
    GmxState& gmx = _state->gmx;
    uint8_t value = static_cast<uint8_t>(((_state->pFE & 0x02) << 6) | (gmx.p78FD & 0x7F) | (gmx.magicShift & 0x01));
    gmx.magicShift = static_cast<uint8_t>(gmx.magicShift >> 1);
    return value;
}

uint8_t PortDecoder_ScorpionGMX::Read7AFD() const
{
    // Paging status: bits 0-2 = #7FFD bits 0-2, bit 3 = #1FFD bit 4, bits 4-6 = #DFFD bits 0-2, bit 7 = BRD0
    const EmulatorState& state = *_state;
    uint8_t value = static_cast<uint8_t>((state.p7FFD & 0x07) | ((state.p1FFD & 0x10) >> 1) |
                                         ((state.pDFFD & 0x07) << 4) | ((state.pFE & 0x01) << 7));
    if (state.gmx.magicLock)
        value = static_cast<uint8_t>((value & 0x80) | (state.gmx.lock7AFD & 0x7F));
    return value;
}

uint8_t PortDecoder_ScorpionGMX::Read7EFD() const
{
    // Bit 0 = #7FFD bit 5, 1 = #7FFD bit 3, 2 = turbo, 3 = 640x200, 4 = #00 bit 7, 5 = #00 bit 5, 6 = #1FFD bit 0,
    // 7 = BRD2
    const EmulatorState& state = *_state;
    const uint8_t p7EFD = state.scorpion.p7EFD;
    uint8_t value = static_cast<uint8_t>(((state.p7FFD & 0x20) >> 5) | ((state.p7FFD & 0x08) >> 2) |
                                         ((p7EFD & P7EFD_TURBO) >> 5) | (p7EFD & P7EFD_GFX) |
                                         ((state.gmx.p00 & P00_PENTAGON_INT) >> 3) | (state.gmx.p00 & P00_BLKEXT) |
                                         ((state.p1FFD & 0x01) << 6) | ((state.pFE & 0x04) << 5));
    if (state.gmx.magicLock)
    {
        constexpr uint8_t frozen = 0x4F;
        value = static_cast<uint8_t>((value & ~frozen) | (state.gmx.lock7EFD & frozen));
    }
    return value;
}

uint8_t PortDecoder_ScorpionGMX::Plane() const
{
    return static_cast<uint8_t>((_state->scorpion.p7EFD & P7EFD_PLANE_MASK) >> 4);
}

void PortDecoder_ScorpionGMX::Out00(uint8_t value)
{
    GmxState& gmx = _state->gmx;
    gmx.p00 = value;
    SyncFrame();  // bit 7 ST/PENT

    // RECONF reloads scheme CONF0-2: the loader learns which one through the magic byte. With FIXROM the scheme is
    // not reloaded and the CPU runs on. A write without bit 3 only stores (the firmware and OSZ write #02 around
    // every monitor call)
    if (value & P00_RECONF)
    {
        gmx.magicShift = static_cast<uint8_t>(MAGIC_BASE | (value & P00_SCHEME_MASK));
        if (!(value & P00_FIXROM))
        {
            _reconfPending = true;
            RefreshStepHook();
        }
    }
}

void PortDecoder_ScorpionGMX::Out7EFD(uint8_t value)
{
    EmulatorState& state = *_state;

    // FIXROM freezes the plane bits (manual, #00 bit 4)
    if (state.gmx.p00 & P00_FIXROM)
        value = static_cast<uint8_t>((value & ~P7EFD_PLANE_MASK) | (state.scorpion.p7EFD & P7EFD_PLANE_MASK));

    const uint8_t changed = static_cast<uint8_t>(state.scorpion.p7EFD ^ value);
    state.scorpion.p7EFD = value;
    state.scorpion.profromBank = Plane();
    _context->pMemory->UpdateZ80Banks();
    ApplyTurbo();

    // Bit 3 switches the 640x200 extended mode (Screen::DetectModeGMX) and with PENT the INT position of its raster
    if ((changed & P7EFD_GFX) && _context->pScreen)
    {
        _context->pScreen->InitRaster();
        SyncFrame();
    }
}

void PortDecoder_ScorpionGMX::ApplyTurbo()
{
    EmulatorState& state = *_state;
    const uint8_t turbo = (state.scorpion.p7EFD & P7EFD_TURBO) ? 1 : 0;
    if (turbo == state.scorpion.turbo && state.hw_turbo_ratio == (turbo ? 2 : 1))
        return;

    state.scorpion.turbo = turbo;
    state.hw_turbo_ratio = turbo ? 2 : 1;
    if (_context->pCore && _context->pCore->GetZ80())
        _context->pCore->GetZ80()->ApplyHardwareTurboNow();
}

void PortDecoder_ScorpionGMX::RunReconfiguration()
{
    _reconfPending = false;

    // The card reloads the FPGA and resets the CPU; RAM and the host board stay as they are, #00 keeps the scheme and
    // the magic byte waits for the loader (manual section 2, loader trace in research-2026-10-30-web-unreal.md 4.1)
    const uint8_t magic = _state->gmx.magicShift;
    if (Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr)
    {
        z80->pc = 0x0000;
        z80->ir_ = 0;
        z80->int_flags = 0;  // IFF1, IFF2, HALT
        z80->im = 0;
        z80->boundary = Z80_BOUNDARY_NONE;
        z80->ClearInterruptRequests();
    }
    reset();
    _state->gmx.magicShift = magic;
    if (Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr)
        z80->ApplyHardwareTurboNow();  // the reset dropped turbo: 3.5 MHz from the next instruction
}

void PortDecoder_ScorpionGMX::RefreshStepHook()
{
    Core* core = _context->pCore;
    if (!core || !core->GetZ80())
        return;

    Z80* z80 = core->GetZ80();
    if (_reconfPending)
    {
        z80->SetMachineStepHook(this);
        z80->SetMachineStepWork(true);
    }
    else if (z80->GetMachineStepHook() == this)
    {
        z80->SetMachineStepHook(nullptr);
    }
}

/// endregion </Register file>
