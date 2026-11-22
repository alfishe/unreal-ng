#include "stdafx.h"
#include "portdecoder_p2666.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "common/modulelogger.h"
#include "debugger/ttd/atm/ttdevosdcard.h"
#include "debugger/ttd/p2666/ttdp2666.h"
#include "debugger/ttd/ttdds12887.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/memory/memory.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/rom.h"
#include "emulator/slots/portowners.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/p2666/p2666font.h"
#include "emulator/video/screen.h"

/// region <Constructors / Destructors>

PortDecoderP2666::PortDecoderP2666(EmulatorContext* context) : PortDecoder(context)
{
    BindRtc(_rtc);
    // The MCU keeps BCD, 24 hours (cell #0B reads #02, PaE.vhd:1499)
    _rtc.WriteAddress(0x0B);
    _rtc.WriteData(0x02);

    if (_context->pCore && _context->pCore->GetZ80())
        _context->pCore->GetZ80()->machineM1Hook = this;

    // Z-Controller SD: a guest write changes the medium, so it is a TTD replay barrier (the media manager's rule)
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

    // Beta Disk: the board's emulator on the RAM disk (pages #4E..#76 of the SRAM), or path A, the WD1793
    _ramDisk = !_context->config.p2666_betadisk_wd1793;
    _beta.SetMemory(this, [](void* owner, uint8_t page) -> uint8_t* {
        auto* self = static_cast<PortDecoderP2666*>(owner);
        return self->_memory ? self->_memory->RAMPageAddress(page) : nullptr;
    });

    // UART: clocked on mainclk, its interrupt is a level on /INT (internal_int = vsync_int AND uart_int)
    _uart.SetClock([this]() { return Edge(); });
    _uart.SetIntListener([this](bool held) {
        if (_context->pCore && _context->pCore->GetZ80())
            _context->pCore->GetZ80()->SetDeviceIntLine(Z80::kDeviceIntP2666Uart, held);
    });

    // The board's own sound, mixed by SoundManager on the AY 1 / AY 2 / COVOX / beeper rows; the standard beeper is
    // part of its 8-bit sum, so its own output goes silent
    _audio = std::make_unique<P2666Audio>(_context);
    _audio->SetTimeSource([this]() { return Now(); });
    if (_context->pSoundManager)
    {
        _context->pSoundManager->attachModelAudioSource(_audio.get());
        _audio->SetHistory(_context->pSoundManager->ayHistory());
        _audio->ApplyBeeperLevels();
    }
}

double PortDecoderP2666::Now() const
{
    const uint32_t multiplier = _state->current_z80_frequency_multiplier ? _state->current_z80_frequency_multiplier : 1u;
    const Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    return static_cast<double>(_state->t_states) + (z80 ? static_cast<double>(z80->t) / multiplier : 0.0);
}

PortDecoderP2666::~PortDecoderP2666()
{
    // The MCU's CMOS cells outlive the machine ([P2666] CmosFile)
    const char* cmosPath = _context->config.p2666_cmos_path;
    if (_mcuCmosLoaded && cmosPath[0] != '\0')
    {
        if (FILE* f = std::fopen(cmosPath, "wb"))
        {
            if (std::fwrite(_mcuCmos.data(), 1, _mcuCmos.size(), f) != _mcuCmos.size())
                MLOGWARNING("PortDecoderP2666: cannot save the CMOS cells to '%s'", cmosPath);
            std::fclose(f);
        }
        else
            MLOGWARNING("PortDecoderP2666: cannot save the CMOS cells to '%s'", cmosPath);
    }
    if (_context->pMediaManager)
        _context->pMediaManager->UnregisterSlot(_sdSlot.Descriptor().id);
    if (_context->pSoundManager && _audio)
    {
        _context->pSoundManager->detachModelAudioSource(_audio.get());
        _audio->ReleaseBeeperLevels();
    }
    // The peer outlives the decoder (the network manager owns it): it must not call back into this one
    _uart.SetIntListener(nullptr);
    _uart.SetPeer(nullptr);
    if (_context->pCore && _context->pCore->GetZ80())
    {
        _context->pCore->GetZ80()->SetDeviceIntLine(Z80::kDeviceIntP2666Uart, false);
        if (_context->pCore->GetZ80()->machineM1Hook == this)
            _context->pCore->GetZ80()->machineM1Hook = nullptr;
    }
}

PortDecoder::NetworkCapabilities PortDecoderP2666::DescribeNetwork()
{
    // The FPGA's UART is the machine's serial port, on #xxEF (a ZX-WiFi card would clash with it)
    NetworkCapabilities caps;
    caps.serialPort = NetworkCapabilities::SerialPort::P2666Uart;
    caps.attachSerialPeer = [this](ISerialPeer* peer) { _uart.SetPeer(peer); };
    caps.serialBaud = [this]() { return _uart.Baud(); };
    return caps;
}

void PortDecoderP2666::OnFrameEnd()
{
    _uart.Sync();
}

/// endregion </Constructors / Destructors>

/// region <PortDecoder>

void PortDecoderP2666::reset()
{
    EmulatorState& state = *_state;
    P2666State& p = state.p2666;

    // A CPU never reset by the MCU yet: the power-on part first (a bare core without Emulator::Reset(hard))
    if (!p.powered)
        PowerCycle();

    // CPU reset (HR §7, PaE.vhd:1240-1262): RAM, ROM pages, palette, CMOS and dos_1 stay
    state.p7FFD = 0;
    state.pEFF7 = 0;
    state.pFE = 0;
    p.pxx77 = 0xAB;
    p.cpm = 0;
    p.pen2 = 1;
    p.bf0 = 0;
    p.covox = 0;
    // sd_config and the SPI shift register have no reset term (PaE.vhd:1293, 972-1024): the card stays selected or not
    p.border3 = 0;
    for (auto& map : p.window)
    {
        map[0] = 0x3BF;
        map[1] = map[2] = map[3] = 0x3FF;
    }
    p.intFrame = 0xFFFFFFFF;
    p.intFlagClearFrame = state.frame_counter;
    p.intFlagClearEdge = kFrameEdges;  // no strobe before the reset counts
    state.atm.borderBright = 0;
    state.hw_turbo_ratio = SelectedRatio();  // 28 MHz: xx77 = #AB (HR §1.1)

    // The YM cores, their address latches and the chip select, Covox (PaE.vhd:1249-1250, RESET_L = cpu_res). Their
    // clock enable is mainclk / 31 and the tone counter divides it by 16 more: an AY at 114 MHz / 62 (HR §5.1, D-12)
    _audio->Reset();
    _audio->ApplyBeeperLevels();
    _uart.Reset();  // reset = cpu_res (PaE.vhd:630)

    // DOS on (Core::Reset cleared the flags; ResolveResetROMMode asks for RM_SYS, which sets CF_TRDOS too)
    state.flags |= CF_TRDOS;

    if (_context->pCore && _context->pCore->GetZ80())
    {
        Z80* z80 = _context->pCore->GetZ80();
        z80->machineM1Hook = this;
        z80->ApplyHardwareTurboNow();
    }
}

void PortDecoderP2666::PowerCycle()
{
    EmulatorState& state = *_state;
    P2666State& p = state.p2666;

    // Power-on signal defaults (PaE.vhd: dos, dos_1, cpm = '1')
    p.dos1Off = 1;
    p.powered = 1;

    // The text mode font (FPGA block ROM otmfnt_rom) and the default palette (pollitra.hex); kept across CPU resets
    std::memcpy(state.atm.fontRam, P2666Font::kFont, sizeof(state.atm.fontRam));
    state.atm.InitPalette();
    const auto& def = DefaultPalette();
    for (uint8_t i = 0; i < 16; i++)
        SetPaletteEntry(i, def[i], false);

    // The boot logo the MCU shows while it uploads (image page 5 -> RAM page 5, 6912 bytes)
    if (_memory)
    {
        uint8_t* logo = _memory->ROMPageHostAddress(5);
        uint8_t* screen = _memory->RAMPageAddress(5);
        if (logo && screen)
            std::memcpy(screen, logo, 6912);
    }

    // The FPGA's CMOS RAM powers up empty; the MCU uploads its 64 cells into it (PaE.vhd:871-904)
    if (!_mcuCmosLoaded)
    {
        _mcuCmosLoaded = true;
        const char* cmosPath = _context->config.p2666_cmos_path;
        if (cmosPath[0] != '\0')
        {
            if (FILE* f = std::fopen(cmosPath, "rb"))
            {
                std::array<uint8_t, 64> image{};
                if (std::fread(image.data(), 1, image.size(), f) == image.size())
                    _mcuCmos = image;
                std::fclose(f);
            }
            else
                MLOGINFO("PortDecoderP2666: no CMOS cells at '%s' yet, starting blank", cmosPath);
        }
    }
    _cmos.fill(0);
    std::copy(_mcuCmos.begin(), _mcuCmos.end(), _cmos.begin());
    _cmosAddress = 0;

    // sd_config and spi_rd_data power up 0 (no initial value in PaE.vhd:551-553): the card is selected (D1 = 0)
    _zc.SetState(ZControllerSpi::State{0x00, 0x00});

    // The MCU's hardware_reset of the Beta Disk emulator, then the image it loads into the RAM disk
    _beta.PowerOn(Edge());
    _ramDiskImage = nullptr;
    if (_ramDisk)
        LoadRamDisk();
}

void PortDecoderP2666::LoadRamDisk()
{
    // On the board the MCU copies firmware/test.trd from its SD card into the RAM disk at power-on. Here it is the
    // disk in drive A, loaded again whenever another disk goes in (the RAM disk is the drive: no write-back)
    FDD* drive = _context->coreState.diskDrives[0];
    DiskImage* image = (drive && drive->isDiskInserted()) ? drive->getDiskImage() : nullptr;
    if (image == _ramDiskImage)
        return;
    _ramDiskImage = image;
    if (!image)
        return;
    std::vector<uint8_t> trd(80u * 2u * 16u * 256u, 0x00);
    size_t sectors = 0;
    for (uint8_t cylinder = 0; cylinder < 80; cylinder++)
    {
        for (uint8_t side = 0; side < 2; side++)
        {
            auto* track = image->getTrackForCylinderAndSide(cylinder, side);
            if (!track)
                continue;
            for (uint8_t sector = 1; sector <= 16; sector++)
            {
                DiskImage::Sector* found = track->findSector(sector);
                if (!found || !found->hasData || !found->data)
                    continue;
                const size_t at = ((cylinder * 2u + side) * 16u + (sector - 1u)) * 256u;
                std::memcpy(trd.data() + at, found->data, 256);
                sectors++;
            }
        }
    }
    _beta.LoadTrd(trd.data(), trd.size());
    MLOGINFO("PortDecoderP2666: drive A's disk in the RAM disk (%zu sectors)", sectors);
}

PortDecoder::RtcBinding PortDecoderP2666::GetRtcBinding()
{
    RtcBinding binding;
    binding.chip = &_rtc;
    binding.ports = "Gluk: #DFF7 address / #BFF7 data after #EFF7 bit 7";
    return binding;
}

bool PortDecoderP2666::IsPagingLocked() const
{
    // The lock counts in 128 mode out of DOS only (HR §2.5)
    return (_state->pEFF7 & kEff7Lock128) && (_state->p7FFD & PORT_7FFD_LOCK) && !DosOn();
}

uint16_t PortDecoderP2666::GetClockSelect() const
{
    return static_cast<uint16_t>((_state->p2666.pxx77 & kX77Turbo) | ((_state->pEFF7 & kEff7Slow) << 8));
}

void PortDecoderP2666::EnterSpectrum128Paging(uint16_t pc)
{
    (void)pc;
    // The reset leaves the board in Gluk at 28 MHz with DOS on. A Spectrum snapshot's program expects a Pentagon 128,
    // which is what the board is with these values (HR §2.2, §2.5, §1.1):
    //   - both maps' windows in Pentagon mode (the CPU reset's values): ROM 0 from DOS / #7FFD.4, RAM 5, 2, #7FFD page
    //   - DOS off, and switching like a Pentagon's (cpm = 1: on at #3Dxx in the 48K BASIC, off out of the ROM)
    //   - #EFF7 = 128 mode (bits 5..7 of #7FFD are the lock and nothing else)
    //   - the ZX picture (#xx77 video mode 3); palette writes gated in DOS as at reset (A14 = 1)
    // The clock select (#xx77 bit 3, #EFF7 bit 4) is kept: a snapshot has no clock, its program runs at the clock the
    // machine ran at (Core::ResetKeepingClock put it back; owner rule 2026-10-18, as on the ATMs)
    EmulatorState& state = *_state;
    P2666State& p = state.p2666;
    for (auto& map : p.window)
    {
        map[0] = 0x3BF;
        map[1] = map[2] = map[3] = 0x3FF;
    }
    constexpr uint16_t kPort77 = 0x4000 | 0x0200 | 0x0077;
    Write77Atm(kPort77, static_cast<uint8_t>((p.pxx77 & 0xF8) | 0x03));
    p.bf0 = 0;
    p.dos1Off = 1;
    state.pEFF7 = static_cast<uint8_t>(kEff7Lock128 | (state.pEFF7 & kEff7Slow));
    state.flags &= ~CF_TRDOS;
    state.hw_turbo_ratio = SelectedRatio();
    if (_context->pCore && _context->pCore->GetZ80())
        _context->pCore->GetZ80()->ApplyHardwareTurboNow();
    if (_memory)
        _memory->UpdateZ80Banks();
}

void PortDecoderP2666::RestoreClockSelect(uint16_t select)
{
    _state->p2666.pxx77 = static_cast<uint8_t>((_state->p2666.pxx77 & ~kX77Turbo) | (select & kX77Turbo));
    _state->pEFF7 = static_cast<uint8_t>((_state->pEFF7 & ~kEff7Slow) | ((select >> 8) & kEff7Slow));
    _state->hw_turbo_ratio = SelectedRatio();
}

/// endregion </PortDecoder>

/// region <Board model>

uint8_t PortDecoderP2666::SelectedRatio() const
{
    if (_state->p2666.pxx77 & kX77Turbo)
        return 8;
    return (_state->pEFF7 & kEff7Slow) ? 1 : 4;
}

void PortDecoderP2666::FinishSpectrum128State()
{
    // The board as built (D-3): in 128 mode a locked machine (#7FFD bit 5 = 1) maps page 32 + bank at #C000, so a
    // 48K program runs in page 32 on the board. The state's bank is in page `bank`: copy it where the window looks
    const EmulatorState& state = *_state;
    const uint8_t bank = state.p7FFD & 0x07;
    const uint8_t shown = WindowPage(3);
    const uint16_t reg = state.p2666.window[(state.p7FFD & 0x10) ? 1 : 0][3];
    if (!(reg & 0x80) || shown == bank)
        return;
    uint8_t* from = _memory->RAMPageAddress(bank);
    uint8_t* to = _memory->RAMPageAddress(shown);
    if (from && to)
        std::memcpy(to, from, 0x4000);
    _memory->UpdateZ80Banks();
}

uint8_t PortDecoderP2666::WindowPage(uint8_t window) const
{
    const EmulatorState& state = *_state;
    const P2666State& p = state.p2666;
    const uint8_t p7FFD = state.p7FFD;
    const uint16_t reg = p.window[(p7FFD & 0x10) ? 1 : 0][window & 3];

    if (!(reg & 0x80))
    {
        // Dispatcher page (PaE.vhd:1042-1052)
        if (reg & 0x40)
            return static_cast<uint8_t>(~((((reg >> 8) & 1) << 6) | (reg & 0x3F)) & 0x7F);
        return static_cast<uint8_t>(kRomFirstPage + 2 * (reg & 1) + ((reg >> 1) & 1));  // bits 0 and 1 swapped
    }

    // Pentagon mode
    switch (window & 3)
    {
        case 3:
            // port7ffdadd is never written: 64 pages (HR §2.2)
            return static_cast<uint8_t>((((p7FFD >> 5) & 1) << 5) | (((p7FFD >> 7) & 1) << 4) |
                                        (((p7FFD >> 6) & 1) << 3) | (p7FFD & 0x07));
        case 2:
            return 2;
        case 1:
            return 5;
        default:
            if (state.pEFF7 & kEff7Ram0)
                return 0;
            return static_cast<uint8_t>(kRomFirstPage + 2 * (DosOn() ? 0 : 1) + ((p7FFD >> 4) & 1));
    }
}

void PortDecoderP2666::UpdateModelMemoryBanks()
{
    if (!_memory)
        return;

    for (uint8_t window = 0; window < 4; window++)
    {
        const uint8_t page = WindowPage(window);
        // The write-protected pages hold the MCU's upload: the emulator keeps them as its ROM pages 0..4, so a
        // RAM-mode mapping of #48..#4C (x7F7) sees the same bytes and drops writes as romprotect does (PaE.vhd:1066)
        if (page >= kRomFirstPage && page <= kRomLastPage)
        {
            _memory->SetROMPageToBank(window, static_cast<uint16_t>(page - kRomFirstPage));
            continue;
        }
        switch (window)
        {
            case 0: _memory->SetRAMPageToBank0(page); break;
            case 1: _memory->SetRAMPageToBank1(page); break;
            case 2: _memory->SetRAMPageToBank2(page); break;
            default: _memory->SetRAMPageToBank3(page); break;
        }
    }
}

uint32_t PortDecoderP2666::PaletteWordToAbgr(uint16_t word)
{
    // HR §6.2: each component inverted, 5-bit DAC with LSB 0 (PaE.vhd:1434-1446)
    const unsigned d = word & 0xFF;
    const unsigned a = word >> 8;  // A15..A8
    auto bit = [](unsigned v, unsigned n) { return ((v >> n) & 1u) ^ 1u; };
    const unsigned r5 = (bit(d, 1) << 4) | (bit(d, 6) << 3) | (bit(a, 1) << 2) | (bit(a, 6) << 1);
    const unsigned g5 = (bit(d, 4) << 4) | (bit(d, 7) << 3) | (bit(a, 4) << 2) | (bit(a, 7) << 1);
    const unsigned b5 = (bit(d, 0) << 4) | (bit(d, 5) << 3) | (bit(a, 0) << 2) | (bit(a, 5) << 1);
    auto to8 = [](unsigned c5) { return (c5 * 255u + 15u) / 31u; };
    return 0xFF000000u | (to8(b5) << 16) | (to8(g5) << 8) | to8(r5);
}

const std::array<uint16_t, 16>& PortDecoderP2666::DefaultPalette()
{
    // common/pollitra.hex (HR §6.2)
    static const std::array<uint16_t, 16> palette = {0xFFFF, 0xFEFE, 0xFDFD, 0xFCFC, 0xEFEF, 0xEEEE, 0xEDED, 0xECEC,
                                                     0xFFFF, 0xDEDE, 0xBDBD, 0x9C9C, 0x6F6F, 0x4E4E, 0x2D2D, 0x0C0C};
    return palette;
}

void PortDecoderP2666::SetPaletteEntry(uint8_t index, uint16_t word, bool live)
{
    // The picture up to the write keeps the old colours (the palette is looked up at the output, zx_main.vhd:617)
    if (_screen && live)
        _screen->UpdateScreen();
    _state->p2666.palette[index & 15] = word;
    _state->atm.palette[index & 15] = PaletteWordToAbgr(word);
    _state->atm.paletteRegs[index & 15] = static_cast<uint8_t>(word);
    if (_screen)
    {
        _screen->RefreshProgrammablePalette();
        if (live)
            _screen->NoteVideoTableWrite(videomap::VideoTable::Palette, index & 15u);  // the video change log
    }
}

bool PortDecoderP2666::IntFlag() const
{
    // Set by every INT strobe (PaE.vhd:1558-1561), cleared by OUT (#FE), #20: was there a strobe after the clear?
    const EmulatorState& state = *_state;
    const P2666State& p = state.p2666;
    const uint32_t frame = state.frame_counter;
    uint32_t edgeNow = 0;
    if (_context->pCore && _context->pCore->GetZ80())
        edgeNow = EdgeOfClock(_context->pCore->GetZ80()->t, std::max<uint8_t>(1, state.current_z80_frequency_multiplier));
    // The last strobe: this frame's when passed, else the previous frame's
    if (edgeNow > kIntStrobeEdge)
        return p.intFlagClearFrame < frame || (p.intFlagClearFrame == frame && p.intFlagClearEdge <= kIntStrobeEdge);
    if (frame == 0)
        return false;
    return p.intFlagClearFrame < frame - 1 || (p.intFlagClearFrame == frame - 1 && p.intFlagClearEdge <= kIntStrobeEdge);
}

uint8_t PortDecoderP2666::CmosRead(uint8_t address)
{
    // PaE.vhd:1497-1515, first match wins
    switch (address & 0x3F)
    {
        case 0x11: return 0xAA;
        case 0x0A: return 0x20;
        case 0x0B: return 0x02;
        case 0x0C: return 0x00;
        case 0x0D: return 0x80;
        default: break;
    }
    switch (address)
    {
        case 0x00: case 0x02: case 0x04: case 0x06: case 0x07: case 0x08: case 0x09:
            // The MCU's RTC (a write is passed on at once: no pending flag stays set)
            _rtc.WriteAddress(address);
            return _rtc.ReadData();
        case 0x31: case 0x32: case 0x33: case 0x34:
            return 0x00;  // the MCU's ADC inputs: nothing connected
        default:
            return _cmos[address];
    }
}

void PortDecoderP2666::CmosWrite(uint8_t address, uint8_t value)
{
    _cmos[address] = value;
    // cmos_mcu_update: the MCU reads cells #00..#3F back into its own memory
    std::copy(_cmos.begin(), _cmos.begin() + static_cast<std::ptrdiff_t>(_mcuCmos.size()), _mcuCmos.begin());
    switch (address)
    {
        case 0x00: case 0x02: case 0x04: case 0x06: case 0x07: case 0x08: case 0x09:
            // The MCU picks up the write flag and sets its clock (HR §5.4)
            _rtc.WriteAddress(address);
            _rtc.WriteData(value);
            break;
        default:
            break;
    }
}

/// endregion </Board model>

/// region <DOS signal>

bool PortDecoderP2666::DosChangeAt(uint16_t address) const
{
    // PaE.vhd:1207-1226: at the M1 strobe (not INTA): #3D00..#3DFF with #7FFD.4 = 1 -> DOS on; #4000.. -> dos_1
    if ((address & 0xFF00) == 0x3D00)
        return !DosOn() && (_state->p7FFD & 0x10);
    if (address >= 0x4000)
        return DosOn() == (_state->p2666.dos1Off != 0);
    return false;
}

void PortDecoderP2666::BeforeMachineM1(uint16_t address)
{
    if (!DosChangeAt(address))
        return;
    if (DosOn())
        _state->flags &= ~CF_TRDOS;
    else
        _state->flags |= CF_TRDOS;
    if (_memory)
        _memory->UpdateZ80Banks();
}

void PortDecoderP2666::LatchDos1()
{
    const P2666State& p = _state->p2666;
    _state->p2666.dos1Off = static_cast<uint8_t>(p.cpm && !p.bf0);
}

/// endregion </DOS signal>

/// region <Ports>

uint8_t PortDecoderP2666::DecodePortIn(uint16_t port, uint16_t pc)
{
    _lastPortDecoded = true;
    const uint8_t result = ReadPort(port, pc);

    PortDecodeDisposition disp;
    disp.decodedPort = port;
    disp.wasDecoded = _lastPortDecoded;
    disp.wasHandledInline = true;
    OnPortInComplete(port, result, pc, disp);

    LatchDos1();
    IoCycleTiming(SelectedRatio());
    return result;
}

void PortDecoderP2666::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    WritePorts(port, value, pc);

    PortDecodeDisposition disp;
    disp.decodedPort = port;
    disp.wasDecoded = true;
    disp.wasHandledInline = true;
    OnPortOutComplete(port, value, pc, disp);

    // The latches are written inside the I/O cycle (wr_ports_single); dos_1 and the new clock follow at its end
    LatchDos1();
    IoCycleTiming(SelectedRatio());
    if (_memory)
        _memory->UpdateZ80Banks();
}

uint8_t PortDecoderP2666::ReadPort(uint16_t port, uint16_t pc)
{
    const EmulatorState& state = *_state;
    const bool dos = DosOn();
    const uint8_t low = static_cast<uint8_t>(port);

    // The read mux, first match wins (PaE.vhd:1493-1541, HR §4.2)
    if ((port & 0x0001) == 0)
    {
        // #FE: keyboard 4..0, bit 5 = 1 in 128 mode else the INT flag, bit 6 tape, bit 7 = 1.
        // Every even port lands here, the Nemo IDE's even registers and #xxBE included (D-5, D-13)
        //
        // P2666-FIX D-5 / D-13 (disabled, hardware-reference.md §11): the mux answers every even port with the FE
        // value, so the even Nemo IDE registers (#10, #30..#F0) and the window readback #00BE..#07BE never reach
        // the bus. To make them work, test them before this row:
        //   if ((port & 0xF8FF) == 0x00BE) return static_cast<uint8_t>(~WindowRegister(port >> 8));
        //   if ((port & 0x0006) == 0) { uint8_t v; if (TryIdePortIn(port, pc, v)) return v; }
        // T-I2 / T-M readback would then expect sector data / the window values instead of FE
        uint8_t fe = Default_Port_FE_In(port, pc);
        fe = static_cast<uint8_t>((fe & 0x5F) | 0x80);
        if ((state.pEFF7 & kEff7Lock128) || IntFlag())
            fe |= 0x20;
        return fe;
    }
    if ((port & 0xE003) == 0xE001)
        return _audio->ReadData();  // the selected YM's register (PaE.vhd:1404, 1496)
    if (port == 0xBFF7 && (state.pEFF7 & kEff7Cmos))
        return CmosRead(_cmosAddress);
    // (#00BE..#07BE: even, answered by the FE row above)
    if (low == 0x57)
        return _zc.ReadData();  // spi_rd_data in and out of DOS, and a #FF exchange starts (PaE.vhd:976-995, 1525)
    if (low == 0x1F && !dos)
        return static_cast<uint8_t>(Default_Port_KempstonJoystick_In() & 0x1F);
    if (dos && _ramDisk)
    {
        // The Beta Disk emulator (PaE.vhd:1530-1534): any odd port by A7..5; vg93_cs (A1 = A0 = 1) strobes
        LoadRamDisk();
        const uint8_t a = static_cast<uint8_t>(low >> 5);
        if (a < 4)
            return _beta.ReadRegister(a, Edge(), (low & 0x03) == 0x03);
        if (a == 7)
            return _beta.ReadSystem(Edge());
    }
    else if (dos)
    {
        switch (low >> 5)
        {
            case 0: return PeripheralPortIn(0x1F);
            case 1: return PeripheralPortIn(0x3F);
            case 2: return PeripheralPortIn(0x5F);
            case 3: return PeripheralPortIn(0x7F);
            case 7: return static_cast<uint8_t>((PeripheralPortIn(0xFF) & 0xC0) | 0x3F);
            default: break;
        }
    }
    if (port == 0xFBDF || port == 0xFFDF || port == 0xFADF)
        return Default_Port_KempstonMouse_In(port, pc);
    if ((port & 0xF8FF) == 0xF8EF)
        return _uart.Read(static_cast<uint8_t>((port >> 8) & 7));  // reg_data_out (PaE.vhd:1539-1540)

    // The external data bus: a General Sound on the ZX-BUS answers its mailbox (#33 / #B3 / #BB, the low byte, as the
    // card decodes it); the Nemo IDE (odd registers only reach here); else nothing drives it
    if (IsGsPort(low))
    {
        if (PortDevice* card = PeripheralDevice(low))
            return card->portDeviceInMethod(low);
    }
    if ((port & 0x0006) == 0)
    {
        uint8_t ide = 0xFF;
        if (TryIdePortIn(port, pc, ide))
            return ide;
    }
    _lastPortDecoded = false;
    return 0xFF;
}

void PortDecoderP2666::WritePorts(uint16_t port, uint8_t value, uint16_t pc)
{
    EmulatorState& state = *_state;
    P2666State& p = state.p2666;
    const bool dos = DosOn();
    const uint8_t low = static_cast<uint8_t>(port);

    // ebl (PaE.vhd:1658): ports with A2 = A1 = 0 are the external Nemo IDE's, the FPGA decodes none of them
    if ((port & 0x0006) == 0)
    {
        TryIdePortOut(port, value, pc);
        return;
    }

    // Every matching register takes the write (HR §4.1, the overlaps included)
    if ((port & 0x0001) == 0)
        WriteFE(port, value, pc);
    if ((port & 0x8002) == 0)
        Write7FFD(value);
    if ((port & 0xF029) == 0xE021)
    {
        // #EFF7: A15..13 = 111, A12 = 0, A5 = 1, A3 = 0, A0 = 1
        state.pEFF7 = value;
    }
    if (port == 0xDFF7 && (state.pEFF7 & kEff7Cmos))
        _cmosAddress = value;
    if (port == 0xBFF7 && (state.pEFF7 & kEff7Cmos))
        CmosWrite(_cmosAddress, value);
    if (low == 0xFB)
    {
        p.covox = value;
        _audio->WriteCovox(value);
        DispatchSelfDecodingOut(port, value);
    }
    // SD (PaE.vhd:977-985, 1288-1294): #xx57 is data out of DOS, in DOS only with A15 = 0; with A15 = 1 in DOS it
    // is the config, which #xx77 is out of DOS
    if (low == 0x57)
    {
        if (dos && (port & 0x8000))
            _zc.WriteConfig(value);
        else
            _zc.WriteData(value);
    }
    if (low == 0x77)
    {
        if (dos)
            Write77Atm(port, value);
        else
            _zc.WriteConfig(value);
    }
    if (low == 0xFF && (!dos || !p.pen2) && !(state.pEFF7 & kEff7Lock128))
        WritePalette(port, value);
    if (low == 0xBF)
        p.bf0 = value & 1;
    if ((port & 0xF8FF) == 0xF8EF)
        _uart.Write(static_cast<uint8_t>((port >> 8) & 7), value);  // PaE.vhd:1257-1262
    if ((port & 0xC003) == 0xC001)
        _audio->WriteAddress(value);
    else if ((port & 0xC003) == 0x8001)
        _audio->WriteData(value);
    if (dos)
    {
        WriteDispatcher(port, value);
        // Beta Disk: A7 = 0, A1..0 = 11 -> register A6..5; A7 = 1 -> system (betadisk_reg)
        if ((low & 0x03) == 0x03 && _ramDisk)
        {
            LoadRamDisk();
            if (low & 0x80)
                _beta.WriteSystem(value, Edge());
            else
                _beta.WriteRegister(static_cast<uint8_t>((low >> 5) & 3), value, Edge());
        }
        else if ((low & 0x03) == 0x03)
        {
            // Path A: the WD1793 on drives A..D
            if (low & 0x80)
                PeripheralPortOut(0xFF, value);
            else
                PeripheralPortOut(static_cast<uint16_t>(((low & 0x60) | 0x1F)), value);
        }
    }
    // The ZX-BUS carries every write: a General Sound there takes its mailbox (#33 / #B3 / #BB by the low byte)
    if (IsGsPort(low))
    {
        if (PortDevice* card = PeripheralDevice(low))
            card->portDeviceOutMethod(low, value);
    }
}

void PortDecoderP2666::WriteFE(uint16_t port, uint8_t value, uint16_t pc)
{
    EmulatorState& state = *_state;
    // HR §4.1, D-2: in 128 mode OUT (FE),#20 is dropped whole; otherwise it also clears the INT flag
    if ((state.pEFF7 & kEff7Lock128) && value == 0x20)
        return;
    if (!(state.pEFF7 & kEff7Lock128) && value == 0x20)
    {
        state.p2666.intFlagClearFrame = state.frame_counter;
        state.p2666.intFlagClearEdge =
            _context->pCore ? EdgeOfClock(_context->pCore->GetZ80()->t, std::max<uint8_t>(1, state.current_z80_frequency_multiplier)) : 0;
    }
    // The palette entry's bit 3 (HR §6.3): the screen draws up to this write with the old border, then takes both
    state.p2666.border3 = (port & 0x0008) ? 0 : 1;
    _borderHighBits = static_cast<uint8_t>(state.p2666.border3 << 3);
    Default_Port_FE_Out(port, value, pc);
    _audio->WriteFe(value);  // beeper = FE.4, tapeout = FE.3 into the DAC sum (PaE.vhd:1451-1452)
    state.atm.borderBright = state.p2666.border3;
}

void PortDecoderP2666::Write7FFD(uint8_t value)
{
    EmulatorState& state = *_state;
    // PaE.vhd:1268-1277 (HR §2.5)
    if (!(state.pEFF7 & kEff7Lock128) || DosOn())
        state.p7FFD = value;
    else if (!(state.p7FFD & PORT_7FFD_LOCK))
        state.p7FFD = static_cast<uint8_t>((state.p7FFD & 0xC0) | (value & 0x3F));

    // #7FFD.3 picks screen 0 / 1: ZX pages 5 / 7 (HR §6 mode table)
    if (_screen)
    {
        const SpectrumScreenEnum screen = (state.p7FFD & 0x08) ? SCREEN_SHADOW : SCREEN_NORMAL;
        if (static_cast<uint8_t>(screen) != _screen->GetActiveScreen())
            _screen->SetActiveScreen(screen);
    }
}

void PortDecoderP2666::Write77Atm(uint16_t port, uint8_t value)
{
    P2666State& p = _state->p2666;
    p.pxx77 = value;
    p.cpm = (port & 0x0200) ? 1 : 0;
    p.pen2 = (port & 0x4000) ? 1 : 0;
    _state->atm.aFF77 = port;
}

void PortDecoderP2666::WriteDispatcher(uint16_t port, uint8_t value)
{
    // HR §2.3: full 16-bit decode, the active map only (PaE.vhd:1279-1300)
    P2666State& p = _state->p2666;
    const unsigned map = (_state->p7FFD & 0x10) ? 1 : 0;
    const uint8_t low = static_cast<uint8_t>(port);
    const uint8_t high = static_cast<uint8_t>(port >> 8);
    if (low != 0xF7)
        return;
    int window = -1;
    bool x7F7 = false;
    switch (high)
    {
        case 0x3F: window = 0; break;
        case 0x7F: window = 1; break;
        case 0xBF: window = 2; break;
        case 0xFF: window = 3; break;
        case 0x37: window = 0; x7F7 = true; break;
        case 0x77: window = 1; x7F7 = true; break;
        case 0xB7: window = 2; x7F7 = true; break;
        case 0xF7: window = 3; x7F7 = true; break;
        default: return;
    }
    if (x7F7)
        p.window[map][window] = static_cast<uint16_t>(((value & 0xC0) << 2) | 0x40 | (value & 0x3F));  // v7 v6 0 1 v5..v0
    else
        p.window[map][window] = static_cast<uint16_t>(0x300 | value);
}

void PortDecoderP2666::WritePalette(uint16_t port, uint8_t value)
{
    // The entry is the current border colour: border3 & FE[2:0] (HR §6.2)
    const uint8_t index = static_cast<uint8_t>((_state->p2666.border3 << 3) | (_state->pFE & 0x07));
    SetPaletteEntry(index, static_cast<uint16_t>((port & 0xFF00) | value));
}

/// endregion </Ports>

/// region <Clock>

void PortDecoderP2666::IoCycleTiming(uint8_t newRatio)
{
    if (!_context->pCore || !_context->pCore->GetZ80())
        return;
    Z80& cpu = *_context->pCore->GetZ80();
    EmulatorState& state = *_state;

    const uint8_t u = state.current_z80_frequency_multiplier;
    const bool exact = state.next_z80_frequency_multiplier == 1 && (u == 1 || u == 4 || u == 8) && cpu.rate == 256 &&
                       state.ClockDen() == 1;
    if (!exact)
    {
        // Host speed-up: the slow cycle's length without its phase. 4 slow T = 4 x ratio clocks
        const uint8_t r = state.hw_turbo_ratio_applied ? state.hw_turbo_ratio_applied : 1;
        if (r > 1)
            cpu.AddWaitStates(4u * (r - 1u));
        if (newRatio != state.hw_turbo_ratio)
        {
            state.hw_turbo_ratio = newRatio;
            cpu.ApplyHardwareTurboNow();
        }
        return;
    }

    // The cycle's T1 rising edge: the CPU has charged 1 T of it
    const uint32_t tNow = cpu.tt >> 8;
    const uint32_t m = EdgeOfClock(tNow - 1, u);
    // iocycle switches cpu_speed to the 3.5 MHz branch right after T1 rose: the next clock edges come at
    // main_state = 01111 (fall, edge = 16 mod 32) and 11111 (rise, 0 mod 32) (PaE.vhd:1579-1609)
    uint32_t d = (16u + 32u - (m & 31u)) & 31u;
    if (d == 0)
        d = 32;
    const uint32_t f = m + d;          // T1 falls on the slow grid
    const uint32_t e = f + 112;        // T2, TW, T3 rise at f + 16, f + 48, f + 80; the next T1 at f + 112
    const uint8_t u2 = newRatio;

    NoteIoForIntPulse(m, f, e, u, u2);

    // Back to the fast clock at the next M-cycle: at 28 MHz its T1 lasts 6 edges (the first fast rise is e + 6), the
    // same as an M-cycle that starts on the 28 MHz grid at e + 2
    const uint32_t endEdge = e + (u2 == 8 ? 2u : 0u);
    if (u2 != state.hw_turbo_ratio || u2 != u)
    {
        state.hw_turbo_ratio = u2;
        cpu.ApplyHardwareTurboNow();
    }
    const uint32_t p2 = 32u / u2;
    const uint32_t tEnd = (endEdge - (u2 == 8 ? 2u : 0u)) / p2;
    // The instruction charges the cycle's last 3 T after the decoder
    const uint32_t target = tEnd - 3;
    const uint32_t tCur = cpu.tt >> 8;
    if (target > tCur)
        cpu.AddWaitStates(target - tCur);
    else if (target < tCur)
        cpu.tt = (cpu.tt & 0xFFu) | (target << 8);  // the rescale of a clock switch rounded up
    RecomputeIntWindow();
}

void PortDecoderP2666::SyncIntPulse()
{
    P2666State& p = _state->p2666;
    if (p.intFrame == _state->frame_counter)
        return;
    p.intFrame = _state->frame_counter;
    p.intFall = 0;
    p.intRise = 0;
    // The strobe holds the counter at 31 for its 4 edges; the 32 counted rises come after it (PaE.vhd:1550-1563)
    p.intAnchor = kIntStrobeEdge + 3;
    p.intRisesLeft = 32;
}

uint32_t PortDecoderP2666::IntFallEdge(uint8_t u) const
{
    const P2666State& p = _state->p2666;
    if (p.intFall)
        return p.intFall;
    // A CPU rise on the edge before the strobe is counted on the strobe's first edge with the counter at 0: that
    // edge raises INT again (the later assignment wins), INT falls one edge later. Only the 14 MHz grid has it
    const uint32_t before = kIntStrobeEdge - 1;
    const uint32_t period = 32u / u;
    const uint32_t phase = (u == 8) ? 2u : 0u;
    return ((before - phase) % period == 0) ? kIntStrobeEdge + 1 : kIntStrobeEdge;
}

uint32_t PortDecoderP2666::IntRiseEdge(uint8_t u) const
{
    const P2666State& p = _state->p2666;
    if (p.intRise)
        return p.intRise;
    const uint32_t period = 32u / u;
    const uint32_t phase = (u == 8) ? 2u : 0u;
    const uint32_t first = phase + ((p.intAnchor - phase + period - 1) / period) * period;
    // The 32nd counted rise is seen one edge later (cpu_clk_old), INT rises on that edge
    return first + (p.intRisesLeft - 1u) * period + 1u;
}

void PortDecoderP2666::NoteIoForIntPulse(uint32_t m, uint32_t f, uint32_t e, uint8_t u, uint8_t u2)
{
    SyncIntPulse();
    P2666State& p = _state->p2666;

    // The fall: the edge before the strobe falls inside this cycle (only 14 MHz has a fast rise there)
    if (!p.intFall && kIntStrobeEdge - 1 <= e)
    {
        const uint32_t before = kIntStrobeEdge - 1;
        if (before <= m)
            p.intFall = IntFallEdge(u);
        else
            p.intFall = (before == f + 16 || before == f + 48 || before == f + 80 || before == e) ? kIntStrobeEdge + 1
                                                                                                    : kIntStrobeEdge;
    }

    if (!p.intRise)
    {
        const uint32_t anchor = p.intAnchor;
        uint32_t left = p.intRisesLeft;
        bool ended = false;
        // Fast rises of the old clock from the anchor up to T1 of this cycle
        if (m >= anchor)
        {
            const uint32_t period = 32u / u;
            const uint32_t phase = (u == 8) ? 2u : 0u;
            const uint32_t first = phase + ((anchor - phase + period - 1) / period) * period;
            if (first <= m)
            {
                const uint32_t k = (m - first) / period + 1;
                if (k >= left)
                {
                    p.intRise = first + (left - 1u) * period + 1u;
                    ended = true;
                }
                else
                    left -= k;
            }
        }
        // The cycle's own rises on the slow grid
        const uint32_t ioRises[4] = {f + 16, f + 48, f + 80, e};
        for (uint32_t r : ioRises)
        {
            if (ended || r < anchor || r <= m)
                continue;
            if (--left == 0)
            {
                p.intRise = r + 1;
                ended = true;
            }
        }
        if (!ended)
            p.intRisesLeft = static_cast<uint8_t>(left);
    }

    // After the cycle the fast clock rises first at e + 6 (28 MHz), e + 8 (14), e + 32 (3.5)
    const uint32_t next = (u2 == 8) ? e + 3 : e + 1;
    p.intAnchor = std::max(p.intAnchor, next);
}

bool PortDecoderP2666::FrameIntWindow(uint32_t& start, uint32_t& end)
{
    const EmulatorState& state = *_state;
    const uint8_t u = state.current_z80_frequency_multiplier;
    if (state.next_z80_frequency_multiplier != 1 || !(u == 1 || u == 4 || u == 8) || state.ClockDen() != 1)
        return false;  // host speed-up: the generic window scaled from the config

    SyncIntPulse();
    const uint32_t period = 32u / u;
    const uint32_t phase = (u == 8) ? 2u : 0u;
    // T80 tests INT_n at the rising edge that starts T1 of the next M-cycle (T80.VHD:1378), the clock t the
    // instruction ends on; an edge on the same mainclk as an INT change still sees the old level (the INT loop
    // through the ZX-BUS pins): taken when fall < edge(t) <= rise
    const uint32_t fall = IntFallEdge(u);
    const uint32_t rise = IntRiseEdge(u);
    start = (fall - phase) / period;       // accepted when t > start
    end = (rise - phase) / period + 1;     // accepted when t < end
    return true;
}

void PortDecoderP2666::RecomputeIntWindow()
{
    if (_context->pCore && _context->pCore->GetZ80())
        _context->pCore->GetZ80()->RecomputeFrameTiming();
}

/// endregion </Clock>

/// region <SD card (Z-Controller)>

std::vector<ttd::TTDDeviceType> PortDecoderP2666::GetTTDModelStateTypes() const
{
    std::vector<ttd::TTDDeviceType> types = {ttd::TTDDeviceType::EvoSdCard, ttd::TTDDeviceType::Ds12887,
                                             ttd::TTDDeviceType::P2666Board, ttd::TTDDeviceType::P2666Sound,
                                             ttd::TTDDeviceType::P2666Uart};
    if (_ramDisk)
        types.push_back(ttd::TTDDeviceType::P2666BetaDisk);  // with [P2666] BetaDisk=wd1793 the WD1793 rides as BetaDisk
    return types;
}

std::vector<std::unique_ptr<ttd::TTDSerializable>> PortDecoderP2666::CreateTTDSerializers() const
{
    std::vector<std::unique_ptr<ttd::TTDSerializable>> serializers;
    auto* self = const_cast<PortDecoderP2666*>(this);
    serializers.push_back(std::make_unique<ttd::TTDEvoSdCard>(self->_sdCard, self->_zc));
    serializers.push_back(std::make_unique<ttd::TTDDs12887>(self->_rtc));
    using Part = ttd::TTDP2666Device::Part;
    serializers.push_back(std::make_unique<ttd::TTDP2666Device>(*self, Part::Board));
    serializers.push_back(std::make_unique<ttd::TTDP2666Device>(*self, Part::Sound));
    serializers.push_back(std::make_unique<ttd::TTDP2666Device>(*self, Part::Uart));
    if (_ramDisk)
        serializers.push_back(std::make_unique<ttd::TTDP2666Device>(*self, Part::BetaDisk));
    return serializers;
}

PortDecoderP2666::SdSlot::SdSlot(PortDecoderP2666& owner) : _owner(owner)
{
    _descriptor.id = "sd.zc";
    _descriptor.kind = MediaKind::Block;
    _descriptor.label = "SD card (Z-Controller)";
    _descriptor.removable = true;
    _descriptor.swapDelayMs = 500;
    _descriptor.acceptsFolder = true;
    _descriptor.defaultAccess = AccessMode::Session;
    // FATALL and NedoOS (osp26sd) read FAT16 and FAT32
    _descriptor.defaultFs = FatType::Fat32;
    _descriptor.fsCompatibility = {FatType::Fat32, FatType::Fat16};
    _descriptor.tags = {"sd", "zcontroller", "primary", "boot"};
    _descriptor.aliases = {"sd"};
    _descriptor.guestName = "the Z-Controller SD card (Gluk's FATALL, NedoOS osp26sd)";
}

void PortDecoderP2666::SdSlot::Attach(Medium& medium)
{
    _owner._sdCard.attach(*medium.Block());
    _owner._sdCard.select(_owner._zc.IsSelected());
}

void PortDecoderP2666::SdSlot::Detach()
{
    _owner._sdCard.detach();
}

bool PortDecoderP2666::SdSlot::IsBusy() const
{
    return _owner._sdCard.busy();
}

/// endregion </SD card (Z-Controller)>
