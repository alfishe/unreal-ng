#include "stdafx.h"

#include "vdac4tang.h"

#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"
#include "vdac4bomzh.h"
#include "vdac4espstate.h"

#include <cstdlib>
#include <cstring>

#ifdef ENABLE_VDAC2
#include <eve/eve.h>
#endif

namespace
{

EveHostCard::Params TangParams(const EmulatorContext* context)
{
    EveHostCard::Params params;
    params.name = "VDAC4";
    params.historyId = "vdac4";
    params.historyLabel = "VDAC4 FT812 core SPI transactions";
    params.crystalHz = Vdac4Tang::kCrystalHz;
#ifdef ENABLE_VDAC2
    params.model = EVE_MODEL_VDAC4;  // power-up ACTIVE at 64 MHz, 2/3 MB RAM_G, the BL616 coprocessor table
#endif
    if (context)
    {
        params.romPath = context->config.vdac2_rom_path;
        params.capturePath = context->config.vdac2_capture_path;
        params.lineBudgetMargin = context->config.vdac2_line_budget_margin;
        params.drawThread = context->config.vdac4_ft_draw_thread;
        params.drawThreads = context->config.vdac4_ft_draw_threads;
    }
    // UNREAL_VDAC4_FT_DRAW_THREAD=0/1 overrides [VDAC4] FtDrawThread (A/B runs of one build)
    if (const char* env = std::getenv("UNREAL_VDAC4_FT_DRAW_THREAD"); env && *env)
        params.drawThread = std::strcmp(env, "0") != 0;
    // UNREAL_VDAC4_FT_DRAW_THREADS=N overrides [VDAC4] FtDrawThreads
    if (const char* env = std::getenv("UNREAL_VDAC4_FT_DRAW_THREADS"); env && *env)
        params.drawThreads = static_cast<uint32_t>(std::strtoul(env, nullptr, 10));
    return params;
}

/// FT812 host commands (DS FT81x §4.1.5)

/// LINK ID "VDC4" and the version word: personality [31:24], protocol 01, core 0x012D (R3 §2)
constexpr uint32_t kLinkIdValue = 0x5644'4334;
constexpr uint32_t kCoreVersion = 0x012D;
/// CP_INFO: BL616 firmware 41 (R3 §2)
constexpr uint32_t kCpInfoValue = 0xC029'0201;
/// FT_FREE: the 512-deep FT_BURST FIFO, always empty here
constexpr uint32_t kFtFreeEntries = 512;

uint32_t GrayCode(uint32_t value)
{
    return value ^ (value >> 1);
}

}  // namespace

Vdac4Tang::Vdac4Tang(EmulatorContext* context, std::function<uint32_t()> rasterInFrame)
    : EveHostCard(context, std::move(rasterInFrame), TangParams(context))
{
    PowerOn();
}

Vdac4Tang::~Vdac4Tang()
{
    if (_q2)
    {
        if (Screen* screen = Context() ? Context()->pScreen : nullptr)
            screen->ClearExternalPicture();
    }
}

void Vdac4Tang::SetState(const State& state)
{
    const bool q2 = state.personality == Personality::Q2;
    if (!q2 && _q2)
        LeaveQ2();
    _state = state;
    if (q2 && !_q2)
    {
        _state.personality = Personality::Ts;
        EnterQ2(/*fresh=*/false);
    }
}

void Vdac4Tang::PowerOn()
{
    WaitEsp();
    if (_q2)
        LeaveQ2();
    EveHostCard::PowerOn();  // the chip powered down: slot 0 has no FT812
    const uint64_t now = Now();
    const EmulatorContext* context = Context();
    const uint64_t bootMs = context ? context->config.vdac4_esp_boot_ms : 400;
    _state = State{};
    _state.personality = Personality::Ts;
    _state.armAt = now + bootMs * kTactsPerMs;
    _state.ftCfg = (context && context->config.vdac4_ramg_mb == 3) ? kFtCfgRamG3M : 0;
    _state.lastEvoTraffic = now;
}

void Vdac4Tang::Reset()
{
    WaitEsp();
    // TS-Conf clears M_SEL; the slot-1 core leaves by the idle rule (R3 §3.4).
    // The Evo transfer in flight is cut by the hub's reset
    _state.msel = 0;
    _state.windowBytes = 0;
    _state.windowFirst = 0;
    _state.windowAllZero = 1;
}

void Vdac4Tang::Update(uint64_t now)
{
    if (!_state.armed && now >= _state.armAt)
        _state.armed = 1;

    if (_state.personality == Personality::Loading && now >= _state.loadDoneAt)
        EnterFt();

    // The ESP as host freezes the idle timer (R3 §3.5): it restarts when the Evo gets the bus back
    if (_state.personality == Personality::Ft && IsEspHost())
        _state.lastEvoTraffic = now;

    if (_state.personality == Personality::Ft && !_state.msel && !IsEspHost() && !_state.selected &&
        now >= _state.lastEvoTraffic + kSwitchIdleMs * kTactsPerMs)
        EnterTs();

    // The Quake II core asks the BL616 for slot 0 (BOOT, the Evo's reset, M_SEL idle)
    if (_state.personality == Personality::Q2 && _q2 && _q2->BootRequested(now / kTactsPerMs))
        EnterTs();
}

void Vdac4Tang::StartLoading(uint64_t now)
{
    const EmulatorContext* context = Context();
    const uint64_t loadMs = context ? context->config.vdac4_load_delay_ms : 174;
    _state.personality = Personality::Loading;
    _state.loadDoneAt = now + loadMs * kTactsPerMs;
    _state.lastEvoTraffic = now;
}

void Vdac4Tang::EnterFt()
{
    // The slot-1 core comes up already ACTIVE at 64 MHz: CORERST / CLKEXT /
    // CLKSEL / ACTIVE went to a core without an FT812 (ft_core.sv:33-37, R3 F1);
    // eve-emu's VDAC4 model powers up that way. A fresh chip: RAM_G, display
    // lists, registers at reset values
    // The core came up at loadDoneAt, not when the change was noticed: its reset sweep is over by now
    PowerOnAt(_state.loadDoneAt);
    _state.personality = Personality::Ft;
    _state.lastEvoTraffic = _state.loadDoneAt;
    ApplyRamMode();
    Synchronize();
#ifdef ENABLE_VDAC2
    // The ROM font image is in place once the core runs (the ESP's copy is not modelled as a delay)
    if (Chip())
        EveVdac4SetFontsReady(Chip(), 1);
#endif
}

void Vdac4Tang::ApplyRamMode()
{
#ifdef ENABLE_VDAC2
    // FT_CFG bit 0: 3 MB RAM_G without the ROM fonts, else 2 MB with the ROM at 0x200000
    if (Chip())
        EveSetRamGLimit(Chip(), IsRamG3M() ? EVE_VDAC4_RAMG_3MB : EVE_VDAC4_RAMG_2MB);
#endif
}

void Vdac4Tang::EnterTs()
{
    const bool fromQ2 = _q2 != nullptr;
    if (fromQ2)
        LeaveQ2();
    // The slot-0 core is loaded again: the FT812 core and all its memory are gone
    EveHostCard::PowerOn();
    _state.personality = Personality::Ts;
    _state.windowBytes = 0;
    _state.windowAllZero = 1;
    // The Evo picture again, blanked while M_SEL = 1 as slot 0 does
    if (fromQ2)
        EveHostCard::SetShowing(_state.msel != 0);
}

Vdac4BomzhCore* Vdac4Tang::Q2Model()
{
    if (!_q2Model)
        _q2Model = std::make_unique<Vdac4BomzhCore>();
    return _q2Model.get();
}

void Vdac4Tang::EnterQ2(bool fresh)
{
    // The FT812 core (if it ran) is gone; the picture is the new core's own HDMI output, whatever M_SEL says
    if (_state.personality == Personality::Ft)
        EveHostCard::PowerOn();
    EveHostCard::SetShowing(false);
    _q2 = Q2Model();
    ++_q2Epoch;
    if (fresh)
        _q2->Reset();
    _state.personality = Personality::Q2;
    _state.windowBytes = 0;
    _state.windowAllZero = 1;
    if (fresh)
        _q2->SetMsel(_state.msel != 0, NowMs());
    _q2Frames = 0;
    _q2Latched = 0;
    // Black until the first FRAME, at the core's HDMI size
    _q2Width = 424;
    _q2Height = 240;
    _q2Picture.assign(static_cast<size_t>(_q2Width) * _q2Height * 4, 0);
    for (size_t i = 3; i < _q2Picture.size(); i += 4)
        _q2Picture[i] = 0xFF;
    _q2PictureSource = Q2Picture::Black;
    if (Screen* screen = Context() ? Context()->pScreen : nullptr)
    {
        screen->SetExternalPicture(_q2Picture.data(), _q2Width, _q2Height, kQ2FramePeriodUs);
        // The black frame goes out now: until the first FRAME the monitor would keep showing the last TS-Conf frame
        // in the new 424x240 geometry (a quarter of the old picture, zoomed). A restore latches nothing: the seek
        // presents its own picture
        if (fresh && !Context()->ttdReplayActive)
            screen->LatchExternalFrame();
    }
}

void Vdac4Tang::LeaveQ2()
{
    if (Screen* screen = Context() ? Context()->pScreen : nullptr)
        screen->ClearExternalPicture();
    // The model and its SDRAM stay (a TTD region); the next core_load resets them
    _q2 = nullptr;
    ++_q2Epoch;
    _q2PictureSource = Q2Picture::Black;
    _q2Picture.clear();
    _q2Picture.shrink_to_fit();
    _q2Width = _q2Height = 0;
    _q2Frames = 0;
    // A state without the core carries no counter: a restore to before core_load reads 0, as the run did there
    _q2Latched = 0;
}

void Vdac4Tang::PresentQ2()
{
    if (!_q2 || _q2->Frames() == _q2Frames)
        return;
    _q2Frames = _q2->Frames();
    Screen* screen = Context() ? Context()->pScreen : nullptr;
    const uint16_t oldWidth = _q2Width, oldHeight = _q2Height;
    if (!_q2->ShownPicture(_q2Picture, _q2Width, _q2Height))
    {
        // The picture stays the last one: still black, or a core picture its state may no longer give
        if (_q2PictureSource == Q2Picture::Core)
            _q2PictureSource = Q2Picture::Raw;
        return;
    }
    _q2PictureSource = Q2Picture::Core;
    _q2PictureSerial = _q2->ShownSerial();
    // Counted in a TTD replay too: the count is state, the same as the run recorded
    ++_q2Latched;
    if (!screen)
        return;
    // The buffer may have moved with a new size: the Screen follows it in a replay as well
    if (_q2Width != oldWidth || _q2Height != oldHeight)
        screen->SetExternalPicture(_q2Picture.data(), _q2Width, _q2Height, kQ2FramePeriodUs);
    if (!Context()->ttdReplayActive)
        screen->LatchExternalFrame();
}

int Vdac4Tang::LoadCore(uint32_t usercode, uint32_t& version)
{
    WaitEsp();
    Update(Now());
    if (usercode != Vdac4BomzhCore::kUserCode)
        return -1;
    if (_q2)
        LeaveQ2();
    EnterQ2();
    if (!_q2->IsReady())
    {
        EnterTs();
        return -1;
    }
    version = _q2->LinkRead(LinkVersion);
    return 0;
}

bool Vdac4Tang::CoreBack()
{
    WaitEsp();
    Update(Now());
    if (_state.personality == Personality::Q2)
        EnterTs();
    return _state.personality == Personality::Ts || _state.personality == Personality::Ft;
}

void Vdac4Tang::OnFrameEnd()
{
    WaitEsp();
    EveHostCard::OnFrameEnd();
    Update(GetTime().frameBase);
    // The Quake II picture: PresentFrame, once the ESP is through this frame end too
}

void Vdac4Tang::SetShowing(bool showing)
{
    WaitEsp();
    _state.msel = showing ? 1 : 0;
    if (_q2)
    {
        // The Quake II core shows its own picture; M_SEL only arms its reset rule
        _q2->SetMsel(showing, NowMs());
        return;
    }
    // M_SEL = 1 in slot 0 blanks the Evo picture; in slot 1 the FT picture is shown.
    // The chip of a slot-0 Tang is powered down: its picture is black
    EveHostCard::SetShowing(showing);
}

size_t Vdac4Tang::TakeIntEdges(uint32_t rasterInFrame, uint32_t* out, size_t max)
{
    if (_espJob)
        return 0;  // the ESP is the FT host (INT_N held inactive) and its job may be using the chip right now
    const size_t taken = EveHostCard::TakeIntEdges(rasterInFrame, out, max);
    if (IsEspHost() || _state.personality != Personality::Ft)
        return 0;  // INT_N held inactive (R3 §3.5)
    return taken;
}

void Vdac4Tang::select(bool selected)
{
    WaitEsp();
    const uint64_t now = Now();
    Update(now);
    _state.selected = selected ? 1 : 0;
    _state.lastEvoTraffic = now;
    _state.ftSeen = 1;
    if (_q2)
    {
        if (!selected)
        {
            _q2->EvoDeselect(now / kTactsPerMs);
            Update(now);  // the Evo's reset (RST_PULSE) boots slot 0 at once
        }
        return;
    }

    if (_state.personality == Personality::Ft && !IsEspHost())
    {
        EveHostCard::select(selected);
        return;
    }
    if (_state.personality == Personality::Ts && !selected)
        DecodeEvoWindow(now);
    _state.windowBytes = 0;
    _state.windowFirst = 0;
    _state.windowAllZero = 1;
}

uint8_t Vdac4Tang::exchange(uint8_t mosi)
{
    WaitEsp();
    const uint64_t now = Now();
    Update(now);
    _state.lastEvoTraffic = now;
    if (_q2)
    {
        _q2->EvoByte(mosi, now / kTactsPerMs);
        return 0;  // the Evo's SPI goes to bz_evo_in (SDRAM uploads), which answers nothing
    }
    if (_state.personality == Personality::Ft)
    {
        if (IsEspHost())
            return 0;  // the Evo's SPI is cut off (R3 §3.5)
        return EveHostCard::exchange(mosi);
    }
    if (_state.windowBytes == 0)
        _state.windowFirst = mosi;
    if (mosi != 0)
        _state.windowAllZero = 0;
    if (_state.windowBytes < UINT32_MAX)
        _state.windowBytes++;
    return 0;  // slot 0 has no FT812: MISO reads 0
}

void Vdac4Tang::DecodeEvoWindow(uint64_t now)
{
    // go = (24 bits all zero) || (>= 32 bits && fs_sr[30] == 0): an ACTIVE host
    // command, or a memory read / write (first byte's bit 6 clear) of 4+ bytes.
    // CORERST / CLKEXT / CLKSEL do not trigger (core 1.16+)
    if (!_state.armed)
        return;
    const bool active = _state.windowBytes == 3 && _state.windowAllZero;
    const bool memory = _state.windowBytes >= 4 && (_state.windowFirst & 0x40) == 0;
    if (active || memory)
        StartLoading(now);
}

/// region <LINK>

void Vdac4Tang::ChipHostCommand(uint8_t command, uint8_t parameter)
{
    EveHostCard::select(true);
    EveHostCard::exchange(command);
    EveHostCard::exchange(parameter);
    EveHostCard::exchange(0);
    EveHostCard::select(false);
}

void Vdac4Tang::ChipWrite32(uint32_t address, uint32_t value)
{
    const uint8_t mosi[7] = {static_cast<uint8_t>(0x80 | ((address >> 16) & 0x3F)), static_cast<uint8_t>(address >> 8),
                             static_cast<uint8_t>(address),       static_cast<uint8_t>(value),
                             static_cast<uint8_t>(value >> 8),    static_cast<uint8_t>(value >> 16),
                             static_cast<uint8_t>(value >> 24)};
    EveHostCard::Transaction(mosi, nullptr, sizeof(mosi));
}

uint32_t Vdac4Tang::ChipRead32(uint32_t address)
{
    // Address, the dummy byte, four data bytes
    const uint8_t mosi[8] = {static_cast<uint8_t>((address >> 16) & 0x3F), static_cast<uint8_t>(address >> 8),
                             static_cast<uint8_t>(address), 0, 0, 0, 0, 0};
    uint8_t miso[8];
    EveHostCard::Transaction(mosi, miso, sizeof(mosi));
    return static_cast<uint32_t>(miso[4]) | (static_cast<uint32_t>(miso[5]) << 8) |
           (static_cast<uint32_t>(miso[6]) << 16) | (static_cast<uint32_t>(miso[7]) << 24);
}

uint32_t Vdac4Tang::LinkRead(uint32_t address)
{
    const uint64_t now = Now();
    Update(now);
    if (_q2)
        return _q2->LinkRead(address);
    const bool ft = _state.personality == Personality::Ft;
    switch (address)
    {
        case LinkId:
            return kLinkIdValue;
        case LinkVersion:
            return (static_cast<uint32_t>(_state.personality) << 24) | (0x01u << 16) | kCoreVersion;
        case LinkStatus:
            return _state.msel ? (1u << 12) : 0u;
        case LinkCpInfo:
            return kCpInfoValue;
        case LinkFtFree:
            return GrayCode(kFtFreeEntries);
        case LinkScratch:
            return _state.scratch;
        case LinkFtSeen:
        {
            const uint32_t seen = _state.ftSeen ? 0x0000'0001u : 0u;
            _state.ftSeen = 0;
            return seen;
        }
        case LinkFtData:
        {
            if (!ft || _state.selected)
                return 0;
            // A read does not move the window (ft_host.cpp readWords: an address per word)
            return ChipRead32(_state.ftAddr);
        }
        case LinkFtStat:
        {
            uint32_t stat = 0;
#ifdef ENABLE_VDAC2
            if (ft && Chip())
            {
                if (EveIntAsserted(Chip()))
                    stat |= 1u << 30;
                const uint32_t cmdWrite = EvePeek(Chip(), 0x3020FC) | (EvePeek(Chip(), 0x3020FD) << 8);
                stat |= cmdWrite & 0xFFF;
            }
#endif
            if (IsRamG3M())
                stat |= 1u << 27;
            if (_state.ftCfg & kFtCfgNoDither)
                stat |= 1u << 13;
            if (IsEspHost())
                stat |= 1u << 12;
            return stat;
        }
        case LinkBoot:
        {
            const uint32_t own = _state.personality == Personality::Ft ? 1u : 0u;
            uint32_t value = 0xB00u << 20;
            value |= own;
            value |= 1u << 8;  // next slot: the FT core
            if (_state.personality != Personality::Ft)
                value |= 1u << 16;  // Evo core
            else
                value |= 1u << 17;
            if (_state.armed)
                value |= 1u << 18;
            return value;
        }
        default:
            return 0;
    }
}

void Vdac4Tang::FirmwareLinkWrite(uint32_t address, uint32_t value)
{
    if (_q2)
        ++_q2Outside;
    LinkWrite(address, value);
}

void Vdac4Tang::Q2ReplayLinkWrite(uint32_t address, uint32_t value)
{
    if (_q2)
        _q2->LinkWrite(address, value);
}

void Vdac4Tang::Q2ReplayLinkFtWrite(uint32_t ftAddress, const uint8_t* words, size_t count)
{
    if (!_q2)
        return;
    // FT_ADDR / FT_DATA are not the Quake II core's registers
    _q2->LinkWrite(LinkFtAddr, ftAddress);
    for (size_t i = 0; i < count; ++i)
    {
        const uint8_t* p = words + 4 * i;
        _q2->LinkWrite(LinkFtData, static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
    }
}

void Vdac4Tang::LinkFtWrite(uint32_t ftAddress, const uint8_t* words, size_t count)
{
    // The time does not move between the words: Update has nothing to change after the first call
    Update(Now());
    if (_q2)
    {
        Q2ReplayLinkFtWrite(ftAddress, words, count);
        return;
    }
    _state.ftAddr = ftAddress & 0x3FFFFF;
    if (count == 0)
        return;
    if (_state.personality == Personality::Ft && !_state.selected)
        WriteWords(_state.ftAddr, words, count);
    _state.ftAddr += 4 * static_cast<uint32_t>(count);
}

void Vdac4Tang::LinkWrite(uint32_t address, uint32_t value)
{
    const uint64_t now = Now();
    Update(now);
    if (_q2)
    {
        _q2->LinkWrite(address, value);
        if (address == LinkBoot)
            Update(now);  // BOOT: the BL616 loads slot 0
        return;
    }
    const bool ft = _state.personality == Personality::Ft;
    if (address >= LinkFtBurst && address < LinkFtBurstEnd)
    {
        // Stream writes into FT space at the FT window address
        if (ft && !_state.selected)
            ChipWrite32(_state.ftAddr, value);
        _state.ftAddr += 4;
        return;
    }
    switch (address)
    {
        case LinkScratch:
            _state.scratch = value;
            break;
        case LinkFtAddr:
            _state.ftAddr = value & 0x3FFFFF;
            break;
        case LinkFtData:
            if (ft && !_state.selected)
                ChipWrite32(_state.ftAddr, value);
            _state.ftAddr += 4;
            break;
        case LinkFtStat:  // FT_CFG
        {
            const bool ramModeChanged = ((_state.ftCfg ^ value) & kFtCfgRamG3M) != 0;
            // The Evo gets the FT host back: INT_N reaches it again from now on, not with the edges of the time
            // the ESP drove the chip (nobody took them while INT_N was held inactive)
            if (IsEspHost() && (value & kFtCfgEspHost) == 0 && ft)
            {
                Synchronize();
                DropIntEdges();
            }
            _state.ftCfg = value & 0x0F;
            if (ramModeChanged && ft)
                ApplyRamMode();
            break;
        }
        case LinkFtHost:
            if (ft && !_state.selected)
                ChipHostCommand(static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8));
            break;
        case LinkBoot:
            if (value == 0x4152'4D31)  // "ARM1"
                _state.armed = 1;
            else if (value == 0x4152'4D30)  // "ARM0"
            {
                _state.armed = 0;
                _state.armAt = UINT64_MAX;
            }
            else if (value == 0x424F'4F54)  // "BOOT": the next slot (the FT core) loads
            {
                if (_state.personality == Personality::Ts)
                    StartLoading(now);
            }
            break;
        default:
            break;
    }
}

/// endregion

/// region <Quake II core state (TTD)>

namespace
{
constexpr uint32_t kQ2StateMagic = 0x5132'5444;  // "DT2Q"
constexpr uint32_t kQ2LiveMagic = 0x5132'4C44;   // "DL2Q": Q2Part::Live
}  // namespace

void Vdac4Tang::SaveQ2(Vdac4EspStateWriter& out, Q2Part part) const
{
    out.U32(part == Q2Part::Live ? kQ2LiveMagic : kQ2StateMagic);
    out.Bool(_q2 != nullptr);
    if (!_q2)
        return;  // the core does not run: the next core_load resets it
    out.U16(_q2Width);
    out.U16(_q2Height);
    out.U32(_q2Frames);
    out.U64(_q2Latched);
    if (part == Q2Part::Live)
    {
        _q2->SaveEvoSide(out);
        out.U64(_q2->StateHash());
    }
    else
        _q2->Save(out);
    // The latched picture last: most states give it from the core (no bytes); one that does not carries it
    Q2Picture source = _q2PictureSource;
    if (source == Q2Picture::Core && _q2PictureSerial != _q2->ShownSerial())
        source = Q2Picture::Raw;
    if (source == Q2Picture::Raw && _q2Picture.size() != static_cast<size_t>(_q2Width) * _q2Height * 4)
        source = Q2Picture::Black;  // cannot happen: the picture always has its size
    out.U8(static_cast<uint8_t>(source));
    if (source == Q2Picture::Raw)
        out.Bytes(_q2Picture.data(), _q2Picture.size());
}

bool Vdac4Tang::LoadQ2(Vdac4EspStateReader& in, Q2Part part, bool* coreMatches)
{
    if (coreMatches)
        *coreMatches = true;
    if (in.U32() != (part == Q2Part::Live ? kQ2LiveMagic : kQ2StateMagic))
        return false;
    const bool running = in.Bool();
    if (!in.Ok())
        return false;
    // The core's state moved under a recording: its next capture keys the ESP (Vdac4EspCpu)
    ++_q2Epoch;
    if (!running)
        return true;  // SetState left the core already (or never entered it)
    const uint16_t width = in.U16();
    const uint16_t height = in.U16();
    const uint32_t frames = in.U32();
    const uint64_t latched = in.U64();
    Vdac4BomzhCore* model = Q2Model();
    if (!in.Ok() || !model)
        return false;
    if (part == Q2Part::Live)
    {
        // The ESP's replay gave the rest of the core: the Evo side of this checkpoint over it, then the check
        if (!model->LoadEvoSide(in))
            return false;
        const uint64_t hash = in.U64();
        if (!in.Ok())
            return false;
        if (coreMatches)
            *coreMatches = _q2 != nullptr && model->StateHash() == hash;
    }
    else if (!model->Load(in))
        return false;
    const auto source = static_cast<Q2Picture>(in.U8());
    if (!in.Ok() || static_cast<uint8_t>(source) > static_cast<uint8_t>(Q2Picture::Raw) ||
        width > Vdac4BomzhCore::kMaxWidth || height > Vdac4BomzhCore::kMaxHeight)
        return false;
    if (!_q2)
    {
        // a state of a running core over a Tang that does not run it: the core waits for its core_load
        const size_t bytes = source == Q2Picture::Raw ? static_cast<size_t>(width) * height * 4 : 0;
        std::vector<uint8_t> skipped(bytes);
        if (bytes != 0)
            in.Bytes(skipped.data(), bytes);
        return in.Ok();
    }
    _q2Width = width;
    _q2Height = height;
    _q2Frames = frames;
    _q2Latched = latched;
    _q2PictureSource = source;
    const size_t bytes = static_cast<size_t>(width) * height * 4;
    if (source == Q2Picture::Raw)
    {
        _q2Picture.resize(bytes);
        in.Bytes(_q2Picture.data(), bytes);
        if (!in.Ok())
            return false;
    }
    else if (source == Q2Picture::Core && _q2->ShownPicture(_q2Picture, _q2Width, _q2Height))
        _q2PictureSerial = _q2->ShownSerial();
    else
    {
        _q2PictureSource = Q2Picture::Black;
        _q2Picture.assign(bytes, 0);
        for (size_t i = 3; i < _q2Picture.size(); i += 4)
            _q2Picture[i] = 0xFF;
    }
    ApplyQ2Picture();
    return true;
}

void Vdac4Tang::ApplyQ2Picture()
{
    // The geometry and buffer only: the seek presents its picture (nothing is latched here)
    if (Screen* screen = Context() ? Context()->pScreen : nullptr)
        screen->SetExternalPicture(_q2Picture.data(), _q2Width, _q2Height, kQ2FramePeriodUs);
}

/// endregion
