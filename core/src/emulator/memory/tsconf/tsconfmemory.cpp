#include "tsconfmemory.h"

#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platforms/tsconf/tsconfstate.h"

TsConfMemory::TsConfMemory(EmulatorContext* context) : Memory(context)
{
}

/// region <Latch-to-bank translation>

/// TS-Conf window map (hardware-spec §2.1-§2.2, [V] zmem.v:73,81):
///   W0 page = vdos     ? 0xFF (RAM, writable regardless of W0_WE)
///           : !W0_MAP  ? {PAGE0[7:2], ~DOS, ROM128}   (mapped mode)
///           :            PAGE0                          (normal mode)
///   W0 source = W0_RAM || vdos ? RAM : ROM (ROM uses page[4:0])
///   W1-W3 = RAM PAGE1-3
/// The DOS signal is TSConf's own (TsConfState::dos, switched by the decoder's
/// M1 hook); CF_TRDOS mirrors it for the shared consumers (debugger, FDC
/// session arbitration) and the generic #3Dxx trap flags stay clear - the
/// trap and its exit are the decoder's (technical-design §3.6)
bool TsConfMemory::UpdateModelBanks()
{
    EmulatorState& state = _context->emulatorState;

    state.flags &= ~(CF_TRDOS | CF_DOSPORTS | CF_Z80FBUS | CF_LEAVEDOSRAM | CF_LEAVEDOSADR | CF_SETDOSROM);

    if (!_ts)
    {
        // Before the decoder attaches its state: the reset layout
        SetROMPageToBank(0, 0);
        SetRAMPageToBank1(5);
        SetRAMPageToBank2(2);
        SetRAMPageToBank3(0);
        return true;
    }

    const TsConfState& ts = *_ts;
    const uint8_t memConfig = ts.MemConfig();

    uint8_t page = ts.Page(0);
    bool ram = (memConfig & TsConfMemConfig::W0Ram) != 0;
    bool writable = (memConfig & TsConfMemConfig::W0We) != 0;

    if (ts.vdos)
    {
        page = 0xFF;
        ram = true;
        writable = true;
    }
    else if (!(memConfig & TsConfMemConfig::W0NoMap))
    {
        // Mapped mode: the 4-page group {service, TR-DOS, 128, 48} selected by
        // ~DOS and ROM128 - for RAM too (a ROM set can live in RAM)
        page = static_cast<uint8_t>((page & 0xFC) | (ts.dos ? 0 : 2) | (memConfig & TsConfMemConfig::Rom128));
    }

    if (ram)
    {
        SetRAMPageToBank0(page);
        if (!writable)
        {
            // Write-protected RAM: stores go to the trash page. The bank stays
            // a RAM bank of that page for TTD (reads, execution and the write
            // cycles are still that page's; the write journal and the probe
            // record bus cycles, and replay re-executes instead of applying it)
            _bank_write[0] = _memory + TRASH_MEMORY_OFFSET;
        }
    }
    else
    {
        SetROMPageToBank(0, page & 0x1F);
    }

    SetRAMPageToBank1(ts.Page(1));
    SetRAMPageToBank2(ts.Page(2));
    SetRAMPageToBank3(ts.Page(3));

    if (ts.dos)
        state.flags |= CF_TRDOS;
    // The Beta-128 ports answer while DOS or FDD_VIRT[7] (VG_OPEN), §8.2
    if (ts.dos || (ts.regs[TsConfReg::FddVirt] & 0x80))
        state.flags |= CF_DOSPORTS;

    return true;
}

/// endregion </Latch-to-bank translation>

/// region <CPU cache>

namespace
{
    /// 13-bit tag {page[7:0], A[13:9]} with the valid bit (hardware-spec §2.5)
    inline uint16_t CacheTag(uint8_t page, uint16_t addr)
    {
        return static_cast<uint16_t>(0x8000 | (page << 5) | ((addr >> 9) & 0x1F));
    }
}

uint8_t TsConfMemory::CacheRead(uint16_t addr, uint8_t normal, bool& dram)
{
    dram = false;
    const uint8_t bank = static_cast<uint8_t>(addr >> 14);
    if (_bank_mode[bank] != BANK_RAM)
        return normal;  // ROM is never cached

    // The RAM page behind the window (the read pointer: W0 may be write-protected)
    const uint8_t page = static_cast<uint8_t>((_bank_read[bank] - RAMPageAddress(0)) / PAGE_SIZE);
    const uint8_t index = static_cast<uint8_t>(addr >> 1);
    const uint16_t tag = CacheTag(page, addr);

    const bool enabled = (_ts->regs[TsConfReg::CacheConfig] >> bank) & 1;
    if (enabled && _ts->cacheTag[index] == tag)
    {
        const uint16_t word = _ts->cacheWord[index];
        return static_cast<uint8_t>((addr & 1) ? (word >> 8) : word);  // a hit takes no DRAM cycle
    }
    _ts->cpuAccesses++;
    dram = true;

    // A DRAM read fills the whole word
    const uint8_t* even = _bank_read[bank] + (addr & 0x3FFE);
    _ts->cacheTag[index] = tag;
    _ts->cacheWord[index] = static_cast<uint16_t>(even[0] | (even[1] << 8));
    return normal;
}

void TsConfMemory::CacheInvalidate(uint16_t addr)
{
    if (!_ts)
        return;

    const uint8_t bank = static_cast<uint8_t>(addr >> 14);
    if (_bank_mode[bank] != BANK_RAM)
        return;

    const uint8_t page = static_cast<uint8_t>((_bank_read[bank] - RAMPageAddress(0)) / PAGE_SIZE);
    const uint8_t index = static_cast<uint8_t>(addr >> 1);
    if (_ts->cacheTag[index] == CacheTag(page, addr))
        _ts->cacheTag[index] = 0;
}

void TsConfMemory::CacheClear()
{
    if (!_ts)
        return;

    for (uint16_t& tag : _ts->cacheTag)
        tag = 0;
}

/// A CPU read from RAM takes a DRAM cycle the DMA cannot use (the engine's
/// budget, technical-design §3.8); ROM is a separate chip
inline bool TsConfMemory::CountDramRead(uint16_t addr)
{
    if (!_ts || _bank_mode[addr >> 14] != BANK_RAM)
        return false;
    _ts->cpuAccesses++;
    return true;
}

/// 14 MHz: a CPU access that goes to DRAM waits for the arbiter
/// (TsConfArbiter: the zmem.v wait by the DRAM phase its request falls in,
/// plus the cycles video holds). The request comes 3 fclk after T1; the
/// 14 MHz clock is not locked to the DRAM phases and every stall shifts it,
/// so the phase comes from the stretched counter itself - 2 fclk per clock, a
/// frame starting at c0
void TsConfMemory::DramWait(TsConfArbiter::Access kind)
{
    const uint32_t fclkTicks = _waitCpu->rate / 2;  // a CPU clock is `rate` ticks at every speed (turbo scales the frame)
    if (!fclkTicks || !_arbiter)
        return;
    const uint32_t start = _waitCpu->tt - 3u * _waitCpu->rate;  // T1 of the access (rd / wd charged its 3 T)
    const uint32_t fclks = _arbiter->CpuAccess(start / fclkTicks + 3u, kind);
    if (fclks)
        _waitCpu->AddWaitTicks(fclks * fclkTicks);
}

void TsConfMemory::AfterWrite(uint16_t addr)
{
    const uint8_t bank = static_cast<uint8_t>(addr >> 14);
    if (_bank_mode[bank] != BANK_RAM || _bank_write[bank] == _memory + TRASH_MEMORY_OFFSET)
        return;  // ROM is a separate chip; a write-protected window starts no DRAM cycle
    // A RAM write takes a DRAM cycle the DMA and the TSU cannot use ([V] zmem.v:121 memwr && ramwr_en)
    if (_ts)
        _ts->cpuAccesses++;
    if (_waitCpu) [[unlikely]]
        DramWait(TsConfArbiter::Access::Write);
}

inline uint8_t TsConfMemory::AfterRead(uint16_t addr, uint8_t normal)
{
    bool dram;
    uint8_t value = normal;
    if (_cacheActive) [[unlikely]]
        value = CacheRead(addr, normal, dram);
    else
        dram = CountDramRead(addr);
    if (_waitCpu) [[unlikely]]
    {
        const bool m1 = _nextIsM1;
        _nextIsM1 = false;
        if (dram)
            DramWait(m1 ? TsConfArbiter::Access::M1 : TsConfArbiter::Access::Read);
    }
    return value;
}

/// Cache model on the CPU read path. The normal read runs first, so access
/// tracking, breakpoints and TTD see every read; a hit only changes the byte
/// the CPU gets. Filling happens only while the cache is active: entries the
/// hardware fills while every window has the cache off are not modeled (they
/// only differ from RAM after a DMA write, and CacheClear drops them when the
/// cache goes off) - technical-design §3.5 item 3
uint8_t TsConfMemory::MemoryReadFast(uint16_t addr, bool isExecution)
{
    return AfterRead(addr, Memory::MemoryReadFast(addr, isExecution));
}

uint8_t TsConfMemory::MemoryReadDebug(uint16_t addr, bool isExecution)
{
    return AfterRead(addr, Memory::MemoryReadDebug(addr, isExecution));
}

/// endregion </CPU cache>
