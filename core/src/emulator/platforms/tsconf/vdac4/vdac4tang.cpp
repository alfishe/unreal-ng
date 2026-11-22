#include "stdafx.h"

#include "vdac4tang.h"

#include "emulator/emulatorcontext.h"

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
    }
    // UNREAL_VDAC4_FT_DRAW_THREAD=0/1 overrides [VDAC4] FtDrawThread (A/B runs of one build)
    if (const char* env = std::getenv("UNREAL_VDAC4_FT_DRAW_THREAD"); env && *env)
        params.drawThread = std::strcmp(env, "0") != 0;
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

void Vdac4Tang::PowerOn()
{
    WaitEsp();
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
    // The slot-0 core is loaded again: the FT812 core and all its memory are gone
    EveHostCard::PowerOn();
    _state.personality = Personality::Ts;
    _state.windowBytes = 0;
    _state.windowAllZero = 1;
}

void Vdac4Tang::OnFrameEnd()
{
    WaitEsp();
    EveHostCard::OnFrameEnd();
    Update(GetTime().frameBase);
}

void Vdac4Tang::SetShowing(bool showing)
{
    WaitEsp();
    _state.msel = showing ? 1 : 0;
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

void Vdac4Tang::LinkFtWrite(uint32_t ftAddress, const uint8_t* words, size_t count)
{
    // The time does not move between the words: Update has nothing to change after the first call
    Update(Now());
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
