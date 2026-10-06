#pragma once
#include "emulator/memory/memory.h"

#include "emulator/platforms/tsconf/tsconfarbiter.h"

struct TsConfState;
class EvoFlash;
class Z80;

/// TS-Conf memory subsystem (TSConf technical-design §3.5, hardware-spec §2).
///
/// Owns the whole latch-to-bank translation (UpdateModelBanks override):
/// - window 0: normal mode (PAGE0 directly) or mapped mode
///   ({PAGE0[7:2], ~DOS, ROM128}), ROM (page[4:0]) or RAM, W0_WE write
///   protection, vdos -> RAM page 0xFF writable;
/// - windows 1-3: PAGE1-3, RAM only.
/// And the functional CPU cache (§2.5): 256 one-word entries indexed by
/// A[8:1], filled by every CPU DRAM read whatever CACHE_CONFIG says, hit only
/// when CACHE_CONFIG enables the window, invalidated by a CPU write to the
/// entry (cache on or off), never cleared (not by a reset, not by switching
/// it off). DMA and video writes do not invalidate: stale reads after DMA are
/// hardware-correct, also for words read while the cache was off.
/// At 14 MHz a read that takes a DRAM cycle waits for the arbiter (phase 8
/// TIM-1, DramWait); at 3.5 / 7 MHz the read path pays one pointer test.
///
/// The state lives in TsConfState, owned by PortDecoder_TSConf, which attaches
/// it here (AttachState). Without a state (before the decoder exists) the
/// banks show the reset layout: ROM 0, RAM 5, 2, 0.
class TsConfMemory : public Memory
{
    /// region <Constructors / Destructors>
public:
    TsConfMemory() = delete;
    explicit TsConfMemory(EmulatorContext* context);
    ~TsConfMemory() override = default;
    /// endregion </Constructors / Destructors>

    /// region <Model overrides>
public:
    uint8_t MemoryReadFast(uint16_t addr, bool isExecution) override;
    uint8_t MemoryReadDebug(uint16_t addr, bool isExecution) override;

    void AttachState(TsConfState* state) { _ts = state; }
    /// The board's ROM chip: told at every bank mapping whether window 0 writes reach it (W0_WE with ROM mapped,
    /// [V] zmem.v:297 `romwe_n = !(memwr && w0_we)` gated by `csrom`)
    void AttachFlash(EvoFlash* flash) { _flash = flash; }
    TsConfState* GetState() const { return _ts; }

    /// 14 MHz DRAM waits (phase 8 TIM-1, hardware-spec §2.5, TsConfArbiter):
    /// `cpu` while the CPU runs at 14 MHz, null otherwise (the read path then
    /// pays one test)
    void SetDramWaits(Z80* cpu, TsConfArbiter* arbiter)
    {
        _waitCpu = cpu;
        _arbiter = arbiter;
    }
    /// A CPU write at addr has just been done: if it went to DRAM (a RAM window
    /// that takes writes) it invalidates the cache entry it hits, counts in the
    /// DRAM budget and, at 14 MHz, waits. The decoder's write overlay calls it
    /// on every write
    void AfterWrite(uint16_t addr);
    /// The next read is an opcode fetch (M1): the decoder's M1 hook says so
    /// right before it (at 14 MHz), so an M1 miss waits one fclk longer than a
    /// data read. Refused DRAM cycles that stopped the clock before this fetch
    /// are charged here, to the machine cycles before it
    void NoteM1Fetch()
    {
        _nextIsM1 = true;
        if (_waitCpu && _arbiter && _arbiter->Refusing()) [[unlikely]]
            RefusedBeforeM1();
    }
    /// endregion </Model overrides>

    /// region <Latch-to-bank translation>
protected:
    bool UpdateModelBanks() override;
    /// endregion </Latch-to-bank translation>

private:
    /// The byte a CPU read at addr gets: the cached word on a hit, else the
    /// normal byte, the read taking a DRAM cycle that fills the entry
    /// @param dram set when the read took a DRAM cycle (RAM: a miss or a window without the cache)
    uint8_t CacheRead(uint16_t addr, uint8_t normal, bool& dram);
    /// The RAM page behind a RAM window (the read pointer: W0 may be write-protected)
    uint8_t WindowPage(uint8_t bank) const
    {
        return static_cast<uint8_t>(static_cast<size_t>(_bank_read[bank] - _ramBase) / PAGE_SIZE);
    }
    /// Cache, DRAM accounting and 14 MHz waits after the normal read
    uint8_t AfterRead(uint16_t addr, uint8_t normal);
    void DramWait(TsConfArbiter::Access kind);
    /// 14 MHz, a machine cycle without a DRAM cycle while video refuses the CPU cycles (TsConfArbiter::Settle)
    void RefusedWait(TsConfArbiter::Access kind);
    void RefusedBeforeM1();

    TsConfState* _ts = nullptr;
    EvoFlash* _flash = nullptr;
    Z80* _waitCpu = nullptr;
    TsConfArbiter* _arbiter = nullptr;
    bool _nextIsM1 = false;
};
